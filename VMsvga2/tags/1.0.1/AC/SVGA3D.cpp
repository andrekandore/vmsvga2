/*
 *  SVGA3D.cpp
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on August 11th 2009.
 *  Copyright 2009 Zenith432. All rights reserved.
 *
 */

/**********************************************************
 * Portions Copyright 2009 VMware, Inc.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **********************************************************/

#include <IOKit/IOLib.h>
#include "SVGA3D.h"
#include "SVGADevice.h"

#if LOGGING_LEVEL >= 1
#define SLog(fmt, ...) IOLog(fmt, ##__VA_ARGS__)
#else
#define SLog(fmt, ...)
#endif

#define CLASS SVGA3D

bool CLASS::Init(SVGADevice* device)
{
	UInt32* fifo_ptr;

	if (!device) {
		m_svga = 0;
		return false;
	}
	m_svga = device;
	if (!device->HasCapability(SVGA_CAP_EXTENDED_FIFO)) {
		SLog("SVGA3D requires the Extended FIFO capability.\n");
		return false;
	}
	fifo_ptr = device->getFifoPtr();
	if (fifo_ptr[SVGA_FIFO_MIN] <= sizeof(UInt32) * SVGA_FIFO_GUEST_3D_HWVERSION) {
		SLog("SVGA3D: GUEST_3D_HWVERSION register not present.\n");
		return false;
	}

	/*
	 * Check the host's version, make sure we're binary compatible.
	 */

	if (fifo_ptr[SVGA_FIFO_3D_HWVERSION] == 0) {
		SLog("SVGA3D: 3D disabled by host.\n");
		return false;
	}
	if (fifo_ptr[SVGA_FIFO_3D_HWVERSION] < SVGA3D_HWVERSION_WS65_B1) {
		SLog("SVGA3D: Host SVGA3D protocol is too old, not binary compatible.\n");
		return false;
	}
	return true;
}

void* CLASS::FIFOReserve(UInt32 cmd, UInt32 cmdSize)
{
	SVGA3dCmdHeader* header;

	header = static_cast<SVGA3dCmdHeader*>(m_svga->FIFOReserve(sizeof *header + cmdSize));
	header->id = cmd;
	header->size = cmdSize;

	return &header[1];
}

void CLASS::BeginDefineSurface(UInt32 sid,                  // IN
							   SVGA3dSurfaceFlags flags,    // IN
							   SVGA3dSurfaceFormat format,  // IN
							   SVGA3dSurfaceFace **faces,   // OUT
							   SVGA3dSize **mipSizes,       // OUT
							   UInt32 numMipSizes)          // IN
{
	SVGA3dCmdDefineSurface* cmd;

	cmd = static_cast<SVGA3dCmdDefineSurface*>(FIFOReserve(SVGA_3D_CMD_SURFACE_DEFINE, sizeof *cmd + sizeof **mipSizes * numMipSizes));

	cmd->sid = sid;
	cmd->surfaceFlags = flags;
	cmd->format = format;

	*faces = &cmd->face[0];
	*mipSizes = reinterpret_cast<SVGA3dSize*>(&cmd[1]);

	memset(*faces, 0, sizeof **faces * SVGA3D_MAX_SURFACE_FACES);
	memset(*mipSizes, 0, sizeof **mipSizes * numMipSizes);
}

void CLASS::DestroySurface(UInt32 sid)  // IN
{
	SVGA3dCmdDestroySurface* cmd;
	cmd = static_cast<SVGA3dCmdDestroySurface*>(FIFOReserve(SVGA_3D_CMD_SURFACE_DESTROY, sizeof *cmd));
	cmd->sid = sid;
	m_svga->FIFOCommitAll();
}

void CLASS::BeginSurfaceDMA(SVGA3dGuestImage *guestImage,     // IN
							SVGA3dSurfaceImageId *hostImage,  // IN
							SVGA3dTransferType transfer,      // IN
							SVGA3dCopyBox **boxes,            // OUT
							UInt32 numBoxes)                  // IN
{
	SVGA3dCmdSurfaceDMA* cmd;
	UInt32 boxesSize = sizeof **boxes * numBoxes;

	cmd = static_cast<SVGA3dCmdSurfaceDMA*>(FIFOReserve(SVGA_3D_CMD_SURFACE_DMA, sizeof *cmd + boxesSize));

	cmd->guest = *guestImage;
	cmd->host = *hostImage;
	cmd->transfer = transfer;
	*boxes = reinterpret_cast<SVGA3dCopyBox*>(&cmd[1]);

	memset(*boxes, 0, boxesSize);
}

void CLASS::DefineContext(UInt32 cid)
{
	SVGA3dCmdDefineContext* cmd;
	cmd = static_cast<SVGA3dCmdDefineContext*>(FIFOReserve(SVGA_3D_CMD_CONTEXT_DEFINE, sizeof *cmd));
	cmd->cid = cid;
	m_svga->FIFOCommitAll();
}

void CLASS::DestroyContext(UInt32 cid)
{
	SVGA3dCmdDestroyContext* cmd;
	cmd = static_cast<SVGA3dCmdDestroyContext*>(FIFOReserve(SVGA_3D_CMD_CONTEXT_DESTROY, sizeof *cmd));
	cmd->cid = cid;
	m_svga->FIFOCommitAll();
}

void CLASS::SetRenderTarget(UInt32 cid,                    // IN
							SVGA3dRenderTargetType type,   // IN
							SVGA3dSurfaceImageId *target)  // IN
{
	SVGA3dCmdSetRenderTarget* cmd;
	cmd = static_cast<SVGA3dCmdSetRenderTarget*>(FIFOReserve(SVGA_3D_CMD_SETRENDERTARGET, sizeof *cmd));
	cmd->cid = cid;
	cmd->type = type;
	cmd->target = *target;
	m_svga->FIFOCommitAll();
}

void CLASS::SetTransform(UInt32 cid,                // IN
						 SVGA3dTransformType type,  // IN
						 const float *matrix)       // IN
{
	SVGA3dCmdSetTransform* cmd;
	cmd = static_cast<SVGA3dCmdSetTransform*>(FIFOReserve(SVGA_3D_CMD_SETTRANSFORM, sizeof *cmd));
	cmd->cid = cid;
	cmd->type = type;
	memcpy(&cmd->matrix[0], matrix, sizeof(float) * 16);
	m_svga->FIFOCommitAll();
}

void CLASS::SetMaterial(UInt32 cid,                      // IN
						SVGA3dFace face,                 // IN
						const SVGA3dMaterial *material)  // IN
{
	SVGA3dCmdSetMaterial* cmd;
	cmd = static_cast<SVGA3dCmdSetMaterial*>(FIFOReserve(SVGA_3D_CMD_SETMATERIAL, sizeof *cmd));
	cmd->cid = cid;
	cmd->face = face;
	memcpy(&cmd->material, material, sizeof *material);
	m_svga->FIFOCommitAll();
}

void CLASS::SetLightEnabled(UInt32 cid,    // IN
							UInt32 index,  // IN
							bool enabled)  // IN
{
	SVGA3dCmdSetLightEnabled* cmd;
	cmd = static_cast<SVGA3dCmdSetLightEnabled*>(FIFOReserve(SVGA_3D_CMD_SETLIGHTENABLED, sizeof *cmd));
	cmd->cid = cid;
	cmd->index = index;
	cmd->enabled = enabled;
	m_svga->FIFOCommitAll();
}

void CLASS::SetLightData(UInt32 cid,                   // IN
						 UInt32 index,                 // IN
						 const SVGA3dLightData *data)  // IN
{
	SVGA3dCmdSetLightData* cmd;
	cmd = static_cast<SVGA3dCmdSetLightData*>(FIFOReserve(SVGA_3D_CMD_SETLIGHTDATA, sizeof *cmd));
	cmd->cid = cid;
	cmd->index = index;
	memcpy(&cmd->data, data, sizeof *data);
	m_svga->FIFOCommitAll();
}

void CLASS::DefineShader(UInt32 cid,                   // IN
						 UInt32 shid,                  // IN
						 SVGA3dShaderType type,        // IN
						 const UInt32 *bytecode,       // IN
						 UInt32 bytecodeLen)           // IN
{
	SVGA3dCmdDefineShader* cmd;

	if (bytecodeLen & 3) {
		SLog("SVGA3D: Shader bytecode length isn't a multiple of 32 bits!\n");
		return;
	}

	cmd = static_cast<SVGA3dCmdDefineShader*>(FIFOReserve(SVGA_3D_CMD_SHADER_DEFINE, sizeof *cmd + bytecodeLen));
	cmd->cid = cid;
	cmd->shid = shid;
	cmd->type = type;
	memcpy(&cmd[1], bytecode, bytecodeLen);
	m_svga->FIFOCommitAll();
}

void CLASS::DestroyShader(UInt32 cid,             // IN
						  UInt32 shid,            // IN
						  SVGA3dShaderType type)  // IN
{
	SVGA3dCmdDestroyShader* cmd;
	cmd = static_cast<SVGA3dCmdDestroyShader*>(FIFOReserve(SVGA_3D_CMD_SHADER_DESTROY, sizeof *cmd));
	cmd->cid = cid;
	cmd->shid = shid;
	cmd->type = type;
	m_svga->FIFOCommitAll();
}

void CLASS::SetShaderConst(UInt32 cid,                   // IN
						   UInt32 reg,                   // IN
						   SVGA3dShaderType type,        // IN
						   SVGA3dShaderConstType ctype,  // IN
						   const void *value)            // IN
{
	SVGA3dCmdSetShaderConst* cmd;
	cmd = static_cast<SVGA3dCmdSetShaderConst*>(FIFOReserve(SVGA_3D_CMD_SET_SHADER_CONST, sizeof *cmd));
	cmd->cid = cid;
	cmd->reg = reg;
	cmd->type = type;
	cmd->ctype = ctype;

	switch (ctype) {
		case SVGA3D_CONST_TYPE_FLOAT:
		case SVGA3D_CONST_TYPE_INT:
			memcpy(&cmd->values, value, sizeof cmd->values);
			break;
		case SVGA3D_CONST_TYPE_BOOL:
			memset(&cmd->values, 0, sizeof cmd->values);
			cmd->values[0] = *static_cast<UInt32 const*>(value);
			break;
		default:
			SLog("SVGA3D: Bad shader constant type.\n");
			m_svga->FIFOCommit(0);
			return;
			
	}
	m_svga->FIFOCommitAll();
}

void CLASS::SetShader(UInt32 cid,             // IN
					  SVGA3dShaderType type,  // IN
					  UInt32 shid)            // IN
{
	SVGA3dCmdSetShader* cmd;
	cmd = static_cast<SVGA3dCmdSetShader*>(FIFOReserve(SVGA_3D_CMD_SET_SHADER, sizeof *cmd));
	cmd->cid = cid;
	cmd->type = type;
	cmd->shid = shid;
	m_svga->FIFOCommitAll();
}

void CLASS::BeginPresent(UInt32 sid,              // IN
						 SVGA3dCopyRect **rects,  // OUT
						 UInt32 numRects)         // IN
{
	SVGA3dCmdPresent* cmd;
	cmd = static_cast<SVGA3dCmdPresent*>(FIFOReserve(SVGA_3D_CMD_PRESENT, sizeof *cmd + sizeof **rects * numRects));
	cmd->sid = sid;
	*rects = reinterpret_cast<SVGA3dCopyRect*>(&cmd[1]);
}

void CLASS::BeginClear(UInt32 cid,             // IN
					   SVGA3dClearFlag flags,  // IN
					   UInt32 color,           // IN
					   float depth,            // IN
					   UInt32 stencil,         // IN
					   SVGA3dRect **rects,     // OUT
					   UInt32 numRects)        // IN
{
	SVGA3dCmdClear* cmd;
	cmd = static_cast<SVGA3dCmdClear*>(FIFOReserve(SVGA_3D_CMD_CLEAR, sizeof *cmd + sizeof **rects * numRects));
	cmd->cid = cid;
	cmd->clearFlag = flags;
	cmd->color = color;
	cmd->depth = depth;
	cmd->stencil = stencil;
	*rects = reinterpret_cast<SVGA3dRect*>(&cmd[1]);
}

void CLASS::BeginDrawPrimitives(UInt32 cid,                    // IN
								SVGA3dVertexDecl **decls,      // OUT
								UInt32 numVertexDecls,         // IN
								SVGA3dPrimitiveRange **ranges, // OUT
								UInt32 numRanges)              // IN
{
	SVGA3dCmdDrawPrimitives *cmd;
	SVGA3dVertexDecl *declArray;
	SVGA3dPrimitiveRange *rangeArray;
	UInt32 declSize = sizeof **decls * numVertexDecls;
	UInt32 rangeSize = sizeof **ranges * numRanges;

	cmd = static_cast<SVGA3dCmdDrawPrimitives*>(FIFOReserve(SVGA_3D_CMD_DRAW_PRIMITIVES, sizeof *cmd + declSize + rangeSize));

	cmd->cid = cid;
	cmd->numVertexDecls = numVertexDecls;
	cmd->numRanges = numRanges;

	declArray = reinterpret_cast<SVGA3dVertexDecl*>(&cmd[1]);
	rangeArray = reinterpret_cast<SVGA3dPrimitiveRange*>(&declArray[numVertexDecls]);

	memset(declArray, 0, declSize);
	memset(rangeArray, 0, rangeSize);

	*decls = declArray;
	*ranges = rangeArray;
}

void CLASS::BeginSurfaceCopy(SVGA3dSurfaceImageId *src,   // IN
							 SVGA3dSurfaceImageId *dest,  // IN
							 SVGA3dCopyBox **boxes,       // OUT
							 UInt32 numBoxes)             // IN
{
	SVGA3dCmdSurfaceCopy *cmd;
	UInt32 boxesSize = sizeof **boxes * numBoxes;

	cmd = static_cast<SVGA3dCmdSurfaceCopy*>(FIFOReserve(SVGA_3D_CMD_SURFACE_COPY, sizeof *cmd + boxesSize));

	cmd->src = *src;
	cmd->dest = *dest;
	*boxes = reinterpret_cast<SVGA3dCopyBox*>(&cmd[1]);

	memset(*boxes, 0, boxesSize);
}

void CLASS::SurfaceStretchBlt(SVGA3dSurfaceImageId *src,   // IN
							  SVGA3dSurfaceImageId *dest,  // IN
							  SVGA3dBox *boxSrc,           // IN
							  SVGA3dBox *boxDest,          // IN
							  SVGA3dStretchBltMode mode)   // IN
{
	SVGA3dCmdSurfaceStretchBlt *cmd;
	cmd = static_cast<SVGA3dCmdSurfaceStretchBlt*>(FIFOReserve(SVGA_3D_CMD_SURFACE_STRETCHBLT, sizeof *cmd));
	cmd->src = *src;
	cmd->dest = *dest;
	cmd->boxSrc = *boxSrc;
	cmd->boxDest = *boxDest;
	cmd->mode = mode;
	m_svga->FIFOCommitAll();
}

void CLASS::SetViewport(UInt32 cid,        // IN
						SVGA3dRect *rect)  // IN
{
	SVGA3dCmdSetViewport *cmd;
	cmd = static_cast<SVGA3dCmdSetViewport*>(FIFOReserve(SVGA_3D_CMD_SETVIEWPORT, sizeof *cmd));
	cmd->cid = cid;
	cmd->rect = *rect;
	m_svga->FIFOCommitAll();
}

void CLASS::SetZRange(UInt32 cid,  // IN
					  float zMin,  // IN
					  float zMax)  // IN
{
	SVGA3dCmdSetZRange *cmd;
	cmd = static_cast<SVGA3dCmdSetZRange*>(FIFOReserve(SVGA_3D_CMD_SETZRANGE, sizeof *cmd));
	cmd->cid = cid;
	cmd->zRange.min = zMin;
	cmd->zRange.max = zMax;
	m_svga->FIFOCommitAll();
}

void CLASS::BeginSetTextureState(UInt32 cid,                   // IN
								 SVGA3dTextureState **states,  // OUT
								 UInt32 numStates)             // IN
{
	SVGA3dCmdSetTextureState *cmd;
	cmd = static_cast<SVGA3dCmdSetTextureState*>(FIFOReserve(SVGA_3D_CMD_SETTEXTURESTATE, sizeof *cmd + sizeof **states * numStates));
	cmd->cid = cid;
	*states = reinterpret_cast<SVGA3dTextureState*>(&cmd[1]);
}

void CLASS::BeginSetRenderState(UInt32 cid,                  // IN
								SVGA3dRenderState **states,  // OUT
								UInt32 numStates)            // IN
{
	SVGA3dCmdSetRenderState *cmd;
	cmd = static_cast<SVGA3dCmdSetRenderState*>(FIFOReserve(SVGA_3D_CMD_SETRENDERSTATE, sizeof *cmd + sizeof **states * numStates));
	cmd->cid = cid;
	*states = reinterpret_cast<SVGA3dRenderState*>(&cmd[1]);
}

void CLASS::BeginPresentReadback(SVGA3dRect **rects,  // OUT
								 UInt32 numRects)     // IN
{
	void *cmd;
	cmd = FIFOReserve(SVGA_3D_CMD_PRESENT_READBACK, sizeof **rects * numRects);
	*rects = static_cast<SVGA3dRect*>(cmd);
}
