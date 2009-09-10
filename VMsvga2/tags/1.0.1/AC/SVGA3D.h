/*
 *  SVGA3D.h
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

#ifndef __SVGA3D_H__
#define __SVGA3D_H__

#include <libkern/OSTypes.h>
#include "svga_apple_header.h"
#include "svga3d_reg.h"
#include "svga_apple_footer.h"

class SVGADevice;

class SVGA3D
{
private:
	SVGADevice* m_svga;

	void* FIFOReserve(UInt32 cmd, UInt32 cmdSize);

public:
	/*
	 * SVGA Device Interoperability
	 */

	bool Init(SVGADevice*);
	void BeginPresent(UInt32 sid, SVGA3dCopyRect **rects, UInt32 numRects);
	void BeginPresentReadback(SVGA3dRect **rects, UInt32 numRects);

	/*
	 * Surface Management
	 */

	void BeginDefineSurface(UInt32 sid,
							SVGA3dSurfaceFlags flags,
							SVGA3dSurfaceFormat format,
							SVGA3dSurfaceFace **faces,
							SVGA3dSize **mipSizes,
							UInt32 numMipSizes);
	void DestroySurface(UInt32 sid);
	void BeginSurfaceDMA(SVGA3dGuestImage *guestImage,
						 SVGA3dSurfaceImageId *hostImage,
						 SVGA3dTransferType transfer,
						 SVGA3dCopyBox **boxes,
						 UInt32 numBoxes);

	/*
	 * Context Management
	 */

	void DefineContext(UInt32 cid);
	void DestroyContext(UInt32 cid);

	/*
	 * Drawing Operations
	 */

	void BeginClear(UInt32 cid, SVGA3dClearFlag flags,
					UInt32 color, float depth, UInt32 stencil,
					SVGA3dRect **rects, UInt32 numRects);
	void BeginDrawPrimitives(UInt32 cid,
							 SVGA3dVertexDecl **decls,
							 UInt32 numVertexDecls,
							 SVGA3dPrimitiveRange **ranges,
							 UInt32 numRanges);

	/*
	 * Blits
	 */

	void BeginSurfaceCopy(SVGA3dSurfaceImageId *src,
						  SVGA3dSurfaceImageId *dest,
						  SVGA3dCopyBox **boxes, UInt32 numBoxes);

	void SurfaceStretchBlt(SVGA3dSurfaceImageId *src,
						   SVGA3dSurfaceImageId *dest,
						   SVGA3dBox *boxSrc, SVGA3dBox *boxDest,
						   SVGA3dStretchBltMode mode);

	/*
	 * Shared FFP/Shader Render State
	 */

	void SetRenderTarget(UInt32 cid, SVGA3dRenderTargetType type, SVGA3dSurfaceImageId *target);
	void SetZRange(UInt32 cid, float zMin, float zMax);
	void SetViewport(UInt32 cid, SVGA3dRect *rect);
	void SetScissorRect(UInt32 cid, SVGA3dRect *rect);
	void SetClipPlane(UInt32 cid, UInt32 index, const float *plane);
	void BeginSetTextureState(UInt32 cid, SVGA3dTextureState **states, UInt32 numStates);
	void BeginSetRenderState(UInt32 cid, SVGA3dRenderState **states, UInt32 numStates);

	/*
	 * Fixed-function Render State
	 */

	void SetTransform(UInt32 cid, SVGA3dTransformType type, const float *matrix);
	void SetMaterial(UInt32 cid, SVGA3dFace face, const SVGA3dMaterial *material);
	void SetLightData(UInt32 cid, UInt32 index, const SVGA3dLightData *data);
	void SetLightEnabled(UInt32 cid, UInt32 index, bool enabled);

	/*
	 * Shaders
	 */

	void DefineShader(UInt32 cid, UInt32 shid, SVGA3dShaderType type, const UInt32 *bytecode, UInt32 bytecodeLen);
	void DestroyShader(UInt32 cid, UInt32 shid, SVGA3dShaderType type);
	void SetShaderConst(UInt32 cid, UInt32 reg, SVGA3dShaderType type, SVGA3dShaderConstType ctype, const void *value);
	void SetShader(UInt32 cid, SVGA3dShaderType type, UInt32 shid);
};

#endif /* __SVGA3D_H__ */
