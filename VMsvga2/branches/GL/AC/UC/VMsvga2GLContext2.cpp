/*
 *  VMsvga2GLContext2.cpp
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on January 7th 2011.
 *  Copyright 2011 Zenith432. All rights reserved.
 *  Portions Copyright (c) Apple Computer, Inc.
 *
 *  Permission is hereby granted, free of charge, to any person
 *  obtaining a copy of this software and associated documentation
 *  files (the "Software"), to deal in the Software without
 *  restriction, including without limitation the rights to use, copy,
 *  modify, merge, publish, distribute, sublicense, and/or sell copies
 *  of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be
 *  included in all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 *  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 *  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 *  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

#include <IOKit/IOLib.h>
#define GL_INCL_SHARED
#define GL_INCL_PUBLIC
#define GL_INCL_PRIVATE
#include "GLCommon.h"
#include "UCGLDCommonTypes.h"
#include "VLog.h"
#include "VMsvga2Accel.h"
#include "VMsvga2GLContext.h"
#include "VMsvga2Shared.h"
#include "VMsvga2Surface.h"

#define CLASS VMsvga2GLContext

#if LOGGING_LEVEL >= 1
#define GLLog(log_level, ...) do { if (log_level <= m_log_level) VLog("IOGL: ", ##__VA_ARGS__); } while (false)
#else
#define GLLog(log_level, ...)
#endif

#define HIDDEN __attribute__((visibility("hidden")))

#pragma mark -
#pragma mark Struct Definitions
#pragma mark -

struct VendorGLStreamInfo {	// start 0x20C
	uint32_t* p;
	uint32_t cmd;
	uint32_t dso_bytes;	// offset 0x8, downstream offset [bytes]
	int ds_count_dwords; // offset 0xC, downstream counter [dwords]
	uint32_t f2;		// offset 0x10
	uint8_t f3;			// offset 0x14
	uint32_t flags;		// offset 0x18
	uint32_t* limit;	// added
};

struct GLDTexSubImage2DStruc {	// starting info->p[1]
	uint16_t dest_z;	// in planes
	uint16_t fixed;		// always 0x3CCU
	uint16_t width;		// in bytes
	uint16_t height;	// in scan-lines
	uint16_t dest_x;	// in bytes
	uint16_t dest_y;	// in scan-lines
	uint32_t source_pitch; // in bytes
	uint32_t source_addr; // offset inside command buffer in bytes
	uint32_t texture_obj_id;
	uint32_t intel_cmd; // 0x50C00004U or 0x50F00004U (SRC_COPY_BLIT)
	uint16_t mipmap;
	uint16_t face;
};

/*
 * A GLD Texture of types TEX_TYPE_STD or TEX_TYPE_OOB begins
 *   with an array of 72 structures of the following type.
 *   This corresponds to 6 faces X 12 mipmaps per face
 *   [A total of 0x900 bytes of headers].  After that the
 *   pixel data is stored, as pointed to by pixels_in_client
 *   (equivalently offset_in_client).
 *
 * The f0 and f1 are used to calculate the addresses of
 *   multiple layers when the depth is > 1.  There are
 *   different volume texture layouts for GMA900 and GMA950.
 */
struct GLDTextureHeader {
	uint64_t pixels_in_client;	// 0 (64-bit virtual address of mipmap)
	uint32_t offset_in_client;	// 8 (offset from beginning of texture storage)
	uint32_t f0;				// 12
	uint16_t width_bytes;		// 16
	uint16_t height;			// 18 (in scan lines)
	uint32_t fixed;				// 20 (always 0x3CC0000U, used to build SRC_COPY_BLT commands)
	uint32_t f1;				// 24
	uint16_t pitch;				// 28 (in bytes)
	uint16_t depth;				// 30 (in planes)
};

/*
 * format of SRC_COPY_BLIT (5 DWORD params)
 */
struct SRC_COPY_BLIT_STRUC {
	uint16_t dest_pitch;	// in bytes
	uint16_t fixed;			// always 0x3CCU, optional direction flag in bit 14
	uint16_t width;			// in bytes
	uint16_t height;		// in scan-lines
	uint32_t dst_addr;		// gart address
	uint32_t source_pitch;	// in bytes
	uint32_t src_addr;		// gart address
};

#pragma mark -
#pragma mark Global Functions
#pragma mark -

static
int decipher_format(uint8_t mapsurf, uint8_t mt)
{
	switch (mapsurf) {
		case 1: // MAPSURF_8BIT
			switch (mt) {
				case 1: // MT_8BIT_L8
					return SVGA3D_LUMINANCE8;
				case 4: // MT_8BIT_A8
					return SVGA3D_ALPHA8;
				case 0: // MT_8BIT_I8
				case 5: // MT_8BIT_MONO8
					break;
			}
			break;
		case 2: // MAPSURF_16BIT
			switch (mt) {
				case 0: // MT_16BIT_RGB565
					return SVGA3D_R5G6B5;
				case 1: // MT_16BIT_ARGB1555
					return SVGA3D_A1R5G5B5;
				case 2: // MT_16BIT_ARGB4444
					return SVGA3D_A4R4G4B4;
				case 3: // MT_16BIT_AY88
					return SVGA3D_LUMINANCE8_ALPHA8; // hmm...
				case 5: // MT_16BIT_88DVDU
					return SVGA3D_V8U8;	// maybe SVGA3D_CxV8U8
				case 6: // MT_16BIT_BUMP_655LDVDU
					return SVGA3D_BUMPL6V5U5;
				case 8: // MT_16BIT_L16
					return SVGA3D_LUMINANCE16;
				case 7: // MT_16BIT_I16
				case 9: // MT_16BIT_A16
					break;
			}
			break;
		case 3: // MAPSURF_32BIT
			switch (mt) {
				case 0: // MT_32BIT_ARGB8888
					return SVGA3D_A8R8G8B8;
				case 2: // MT_32BIT_XRGB8888
					return SVGA3D_X8R8G8B8;
				case 4: // MT_32BIT_QWVU8888
					return SVGA3D_Q8W8V8U8;
				case 7: // MT_32BIT_XLVU8888
					return SVGA3D_X8L8V8U8;
				case 8: // MT_32BIT_ARGB2101010
					return SVGA3D_A2R10G10B10;
				case 10: // MT_32BIT_AWVU2101010
					return  SVGA3D_A2W10V10U10;
				case 11: // MT_32BIT_GR1616
					return SVGA3D_G16R16;
				case 12: //  MT_32BIT_VU1616
					return SVGA3D_V16U16;
				case 1: // MT_32BIT_ABGR8888
				case 3: // MT_32BIT_XBGR8888
				case 5: // MT_32BIT_AXVU8888
				case 6: // MT_32BIT_LXVU8888
				case 9: // MT_32BIT_ABGR2101010
				case 13: // MT_32BIT_xI824
				case 14: // MT_32BIT_xA824
				case 15: // MT_32BIT_xL824
					break;
			}
			break;
		case 5: // MAPSURF_422
			switch (mt) {
				case 0: // MT_422_YCRCB_SWAPY
					return SVGA3D_UYVY;
				case 1: // MT_422_YCRCB_NORMAL
					return SVGA3D_YUY2;
				case 2: // MT_422_YCRCB_SWAPUV
				case 3: // MT_422_YCRCB_SWAPUVY
					break;
			}
			break;
		case 6: // MAPSURF_COMPRESSED
			switch (mt) {
				case 0: // MT_COMPRESS_DXT1
					return SVGA3D_DXT1;
				case 1: // MT_COMPRESS_DXT2_3
					return SVGA3D_DXT3;
				case 2: // MT_COMPRESS_DXT4_5
					return SVGA3D_DXT5;
				case 3: // MT_COMPRESS_FXT1
				case 4: // MT_COMPRESS_DXT1_RGB
					break;
			}
			break;
		case 7: // MAPSURF_4BIT_INDEXED
			// mt == 7, MT_4BIT_IDX_ARGB8888
			break;
	}
	return SVGA3D_FORMAT_INVALID;
}

static
int default_format(uint8_t bytespp)
{
	switch (bytespp) {
		case 4U:
			return SVGA3D_A8R8G8B8;
		case 2U:
			return SVGA3D_A1R5G5B5;
		case 1U:
			return SVGA3D_BUFFER;
	}
	return SVGA3D_FORMAT_INVALID;
}

static
bool isPagedOff(GLDSysObject* sys_obj, uint8_t face, uint8_t mipmap)
{
	if (!sys_obj || face >= 6U || mipmap >= 16U)
		return false;
	return (sys_obj->pageoff[face] & ~sys_obj->pageon[face] & (1U << mipmap)) != 0U;
}

static
IOReturn readGLDTextureHeader(VMsvga2TextureBuffer* tx, VMsvga2Accel::ExtraInfoEx* extra, uint8_t face, uint8_t mipmap)
{
	IOReturn rc;
	GLDTextureHeader gld_th;
	IOByteCount bc;

	if (!tx ||
		!extra ||
		face >= 6U ||
		mipmap >= 12U)
		return kIOReturnBadArgument;
	if (!tx->md)
		return kIOReturnNoMemory;
	rc = tx->md->prepare();
	if (rc != kIOReturnSuccess)
		return rc;
	bc = tx->md->readBytes((face * 12U + mipmap) * sizeof gld_th, &gld_th, sizeof gld_th);
	tx->md->complete();
	if (bc < sizeof gld_th)
		return kIOReturnError;
	if (tx->sys_obj_type == TEX_TYPE_STD) {
		extra->mem_offset_in_gmr = gld_th.offset_in_client;
		extra->mem_limit = tx->md->getLength() - extra->mem_offset_in_gmr;
	} else {	// Note: should be TEX_TYPE_OOB here
		extra->mem_offset_in_gmr += gld_th.offset_in_client;
		extra->mem_limit -= gld_th.offset_in_client;
	}
	if (gld_th.pitch) {
		extra->mem_pitch = gld_th.pitch;
		tx->pitch = gld_th.pitch;
	}
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark Private Dispatch Tables [Apple]
#pragma mark -

typedef void (CLASS::*dispatch_function_t)(VendorGLStreamInfo*);

static
dispatch_function_t dispatch_discard_1[] =
{
	&CLASS::process_token_Noop,
	&CLASS::process_token_Noop,
	&CLASS::process_token_Noop,
	&CLASS::process_token_Noop,
	&CLASS::process_token_TextureVolatile,
	&CLASS::process_token_TextureNonVolatile,
	&CLASS::process_token_SetSurfaceState,
	&CLASS::process_token_BindDrawFBO,
	&CLASS::process_token_BindReadFBO,
	&CLASS::process_token_UnbindDrawFBO,
	&CLASS::process_token_UnbindReadFBO,
};

static
dispatch_function_t dispatch_discard_2[] =
{
	&CLASS::discard_token_Texture,
	&CLASS::discard_token_Texture,
	&CLASS::discard_token_Texture,
	&CLASS::discard_token_Texture,
	&CLASS::discard_token_NoTex,
	&CLASS::discard_token_NoTex,
	&CLASS::discard_token_NoTex,
	&CLASS::discard_token_NoTex,
	&CLASS::discard_token_NoTex,
	&CLASS::discard_token_NoTex,
	&CLASS::discard_token_NoTex,
	&CLASS::discard_token_NoTex,
	&CLASS::discard_token_NoTex,
	&CLASS::discard_token_NoTex,
	&CLASS::discard_token_NoTex,
	&CLASS::discard_token_NoTex,
	&CLASS::discard_token_NoTex,
	&CLASS::discard_token_NoTex,
	&CLASS::discard_token_NoTex,
	&CLASS::discard_token_NoTex,
	&CLASS::discard_token_VertexBuffer,
	&CLASS::discard_token_NoVertexBuffer,
	&CLASS::discard_token_DrawBuffer,
	&CLASS::discard_token_Noop,
	&CLASS::discard_token_TexSubImage2D,
	&CLASS::discard_token_CopyPixelsDst,
	&CLASS::discard_token_Noop,
	&CLASS::discard_token_Noop,
	&CLASS::discard_token_Noop,
	&CLASS::discard_token_AsyncReadDrawBuffer,
};

static
dispatch_function_t dispatch_process_1[] =
{
	&CLASS::process_token_Start,
	&CLASS::process_token_End,
	&CLASS::process_token_Swap,
	&CLASS::process_token_Flush,
	&CLASS::process_token_TextureVolatile,
	&CLASS::process_token_TextureNonVolatile,
	&CLASS::process_token_SetSurfaceState,
	&CLASS::process_token_BindDrawFBO,
	&CLASS::process_token_BindReadFBO,
	&CLASS::process_token_UnbindDrawFBO,
	&CLASS::process_token_UnbindReadFBO,
};

static
dispatch_function_t dispatch_process_2[] =
{
	&CLASS::process_token_Texture,
	&CLASS::process_token_Texture,
	&CLASS::process_token_Texture,
	&CLASS::process_token_Texture,
	&CLASS::process_token_NoTex,
	&CLASS::process_token_NoTex,
	&CLASS::process_token_NoTex,
	&CLASS::process_token_NoTex,
	&CLASS::process_token_NoTex,
	&CLASS::process_token_NoTex,
	&CLASS::process_token_NoTex,
	&CLASS::process_token_NoTex,
	&CLASS::process_token_NoTex,
	&CLASS::process_token_NoTex,
	&CLASS::process_token_NoTex,
	&CLASS::process_token_NoTex,
	&CLASS::process_token_NoTex,
	&CLASS::process_token_NoTex,
	&CLASS::process_token_NoTex,
	&CLASS::process_token_NoTex,
	&CLASS::process_token_VertexBuffer,
	&CLASS::process_token_NoVertexBuffer,
	&CLASS::process_token_DrawBuffer,
	&CLASS::process_token_SetFence,
	&CLASS::process_token_TexSubImage2D,
	&CLASS::process_token_CopyPixelsDst,
	&CLASS::process_token_CopyPixelsSrc,
	&CLASS::process_token_CopyPixelsSrcFBO,
	&CLASS::process_token_DrawRect,
	&CLASS::process_token_AsyncReadDrawBuffer,
};

#pragma mark -
#pragma mark Private Methods
#pragma mark -

HIDDEN
uint32_t CLASS::processCommandBuffer(VendorCommandDescriptor* result)
{
	VendorGLStreamInfo cb_iter;
	uint32_t upper, commands_processed;
#if 0
	int i, lim;
#endif

	cb_iter.dso_bytes = 0U;
	cb_iter.p = &m_command_buffer.kernel_ptr->downstream[-1];
	cb_iter.limit = &m_command_buffer.kernel_ptr->data[0] +  m_command_buffer.size / sizeof(uint32_t);
	cb_iter.ds_count_dwords = -1;
	cb_iter.flags = 0U;
	cb_iter.f2 = 0U;
	cb_iter.f3 = 1U;
	commands_processed = 0;
	m_stream_error = 0;
	do {
		cb_iter.cmd = *cb_iter.p;
		upper = cb_iter.cmd >> 24;
#if 0
		GLLog(3, "%s:   cmd %#x length %u\n", __FUNCTION__, upper, cb_iter.cmd & 0xFFFFFFU);
#endif
		if (upper <= 10U)
			(this->*dispatch_process_1[upper])(&cb_iter);
		else if (upper >= 32U && upper <= 61U)
			(this->*dispatch_process_2[upper - 32U])(&cb_iter);
		if (m_stream_error)
			break;
		++commands_processed;
		cb_iter.cmd &= 0xFFFFFFU;
		cb_iter.p += cb_iter.cmd;
		cb_iter.ds_count_dwords += cb_iter.cmd;
		if (cb_iter.limit <= cb_iter.p)
			break;
	} while (cb_iter.cmd);
	GLLog(3, "%s:   processed %d stream commands, error == %#x\n", __FUNCTION__, commands_processed, m_stream_error);
	result->next = &m_command_buffer.kernel_ptr->downstream[0] + cb_iter.dso_bytes / sizeof(uint32_t);
	result->ds_ptr = m_command_buffer.gart_ptr + static_cast<uint32_t>(sizeof(VendorCommandBufferHeader)) + cb_iter.dso_bytes;
	result->ds_count_dwords = cb_iter.ds_count_dwords;
	result->f2 = cb_iter.f2;
	return cb_iter.flags;
}

HIDDEN
void CLASS::discardCommandBuffer()
{
	VendorGLStreamInfo cb_iter;
	uint32_t upper;
	cb_iter.dso_bytes = 0U;
	cb_iter.p = &m_command_buffer.kernel_ptr->downstream[-1];
	cb_iter.limit = &m_command_buffer.kernel_ptr->data[0] +  m_command_buffer.size / sizeof(uint32_t);
	cb_iter.ds_count_dwords = -1;
	cb_iter.flags = 0U;
	cb_iter.f2 = 0U;
	cb_iter.f3 = 1U;
	do {
		cb_iter.cmd = *cb_iter.p;
		upper = cb_iter.cmd >> 24;
		if (upper <= 10U)
			(this->*dispatch_discard_1[upper])(&cb_iter);
		else if (upper >= 32U && upper <= 61U)
			(this->*dispatch_discard_2[upper - 32U])(&cb_iter);
		else
			m_stream_error = 5;
		/*
		 * Note: ignores errors
		 */
		cb_iter.cmd &= 0xFFFFFFU;
		cb_iter.p += cb_iter.cmd;
		cb_iter.ds_count_dwords += cb_iter.cmd;
		if (cb_iter.limit <= cb_iter.p)
			break;
	} while (cb_iter.cmd);
}

HIDDEN
void CLASS::removeTextureFromStream(VMsvga2TextureBuffer* tx)
{
#if 0
	/*
	 * TBD
	 */
	// edx = this
	// ecx = tx
	// ebx = m_provider->0x50
	uint32_t stamp;
redo:
	stamp = 0U /* m_provider->0x50 */;
	tx->sys_obj->stamps[0] = stamp;
	switch (tx->sys_obj_type) {
		case TEX_TYPE_AGPREF:
			// 69D1
			tx->linked_agp->sys_obj->stamps[0] = stamp;
			--tx->linked_agp->ofs_0xE;
			break;
		case TEX_TYPE_SURFACE:
			// 6A05
			if (!tx->linked_agp)
				break;
			break;
		case TEX_TYPE_IOSURFACE:
			tx = tx->linked_agp;
			if (tx)
				goto redo;
			break;
		case TEX_TYPE_AGP:
			tx->sys_obj->stamps[0] = ebx;
			--tx->ofs_0xE;
			break;
	}
#endif
}

HIDDEN
void CLASS::addTextureToStream(VMsvga2TextureBuffer* tx)
{
	/*
	 * TBD
	 */
#if 0
	GLDSysObject* sys_obj = tx->sys_obj;
	if (!sys_obj)
		return;
	GLLog(3, "%s: obj %u, pageoff[0] == %u, pageon[0] == %u\n", __FUNCTION__,
		  sys_obj->object_id, sys_obj->pageoff[0], sys_obj->pageon[0]);
#endif
}

HIDDEN
void CLASS::submit_midbuffer(VendorGLStreamInfo* info)
{
	uint32_t wc = static_cast<uint32_t>(info->ds_count_dwords);
	if (!wc)
		return;
	if (wc > 2U) {
#if 0
		m_provider->0x78C += (wc - 2U) * static_cast<uint32_t>(sizeof(uint32_t));
		m_command_buffer.submit_stamp =
		m_provider->submit_buffer(&m_command_buffer.kernel_ptr.downstream[0] + info->dso_bytes / sizeof(uint32_t),
								  m_command_buffer.gart_ptr + sizeof(VendorCommandBufferHeader) + info->dso_bytes,
								  wc - 2U,
								  __FILE__,
								  __LINE__);
#else
		m_command_buffer.submit_stamp =
		submit_buffer(&m_command_buffer.kernel_ptr->downstream[0] + info->dso_bytes / sizeof(uint32_t),
					  wc - 2U);
#endif
	} 
	info->dso_bytes += wc * static_cast<uint32_t>(sizeof(uint32_t));
	info->ds_count_dwords = 0;
	info->f2 = 0U;
}

HIDDEN
void CLASS::get_texture(VendorGLStreamInfo* info, VMsvga2TextureBuffer* tx, bool flag)
{
#if 0
	VMsvga2TextureBuffer* otx;
#endif
	if (tx->sys_obj->in_use) {
		submit_midbuffer(info);
		alloc_and_load_texture(tx);
#if 0
		if (flag & !m_command_buffer.gart_ptr)
			mapTransferToGART(&m_command_buffer);
#endif
	}
#if 0
	otx = tx;
	if (otx->sys_obj_type == TEX_TYPE_IOSURFACE)
		otx = otx->linked_agp;
	if (otx->0x6C) {
		/*
		 * TBD: circular linked-list juggling
		 *   on m_provider->0x688, and prev-next pair
		 *   at otx->0x5C and otx->0x60 [moves otx to head of list]
		 */
	}
#endif
}

HIDDEN
void CLASS::dirtyTexture(VMsvga2TextureBuffer* tx, uint8_t face, uint8_t mipmap)
{
	if (!tx || !tx->sys_obj)
		return;
	if (tx->sys_obj_type == TEX_TYPE_IOSURFACE)	{
		// TBD: stuff
		return;
	}
	tx->sys_obj->pageoff[face] &= ~(1U << mipmap);
	tx->sys_obj->pageon[face] |= (1U << mipmap);
}

HIDDEN
void CLASS::get_tex_data(VMsvga2TextureBuffer* tx, uint32_t* tex_gart_address, uint32_t* tex_pitch)
{
	/*
	 * TBD
	 */
	*tex_gart_address = 0U;
	*tex_pitch = 512U;
}

HIDDEN
void CLASS::write_tex_data(uint32_t i, uint32_t* q, VMsvga2TextureBuffer* tx)
{
	/*
	 * TBD
	 */
	int fmt;
	uint32_t width, pitch;
	uint16_t height, depth;
	uint8_t mapsurf, mt;
	uint32_t d0 = q[1];
	uint32_t d1 = q[2];
	width  = bit_select(d0, 10, 11) + 1U;
	height = bit_select(d0, 21, 11) + 1U;
	depth  = bit_select(d1,  0,  8) + 1U;
	pitch = (((d1 >> 21) + 1U) & 0x7FFU) * static_cast<uint32_t>(sizeof(uint32_t));
	mapsurf = bit_select(d0, 7, 3);
	mt = bit_select(d0, 3, 4);
#if 0
	GLLog(3, "%s:   tex %u data1 width %u, height %u, MAPSURF %u, MT %u, UF %u, TS %u, TW %u\n", __FUNCTION__,
		  q[0], width, height, mapsurf, mt,
		  bit_select(d0, 2, 1),
		  bit_select(d0, 1, 1),
		  d0 & 1U);
	GLLog(3, "%s:   tex %u data2 pitch %u, cube-face-ena %#x, max-lod %#x, mip-layout %u, depth %u\n", __FUNCTION__,
		  q[0], pitch,
		  bit_select(d1, 15, 6),
		  bit_select(d1,  9, 6),
		  bit_select(d1,  8, 1),
		  depth);
#endif
	fmt = decipher_format(mapsurf, mt);
	if (fmt != tx->surface_format ||
		width != tx->width ||
		height != tx->height ||
		depth != tx->depth) {
		tx->surface_changed = 1;
	}
	tx->surface_format = fmt;
	tx->width = width;
	tx->height = height;
	tx->depth = depth;
	if (pitch)
		tx->pitch = pitch;
	else if (!tx->pitch)
		tx->pitch = width * tx->bytespp;
}

HIDDEN
IOReturn CLASS::create_host_surface_for_texture(VMsvga2TextureBuffer* tx)
{
	IOReturn rc;

	if (!tx)
		return kIOReturnBadArgument;
	if (!isIdValid(tx->surface_id)) {
		tx->surface_id = m_provider->AllocSurfaceID();	// Note: doesn't fail
		tx->surface_changed = 2;
	}
	if (tx->surface_changed) {
		if (tx->surface_changed != 2)
			m_provider->destroySurface(tx->surface_id);
		tx->surface_changed = 0;
		rc = m_provider->createTexture(tx->surface_id,
									   static_cast<SVGA3dSurfaceFormat>(tx->surface_format),
									   tx->width,
									   tx->height,
									   tx->depth);
		if (rc != kIOReturnSuccess) {
			m_provider->FreeSurfaceID(tx->surface_id);
			tx->surface_id = SVGA_ID_INVALID;
			return rc;
		}
	}
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::alloc_and_load_texture(VMsvga2TextureBuffer* tx)
{
	IOReturn rc;
	uint32_t fence;
	uint8_t sys_obj_type, face, mipmap;
	IOMemoryDescriptor* data_md;
	SVGA3dCopyBox copyBox;
	VMsvga2Accel::ExtraInfoEx extra;
	VMsvga2TextureBuffer* ltx;

	if (!tx)
		return kIOReturnBadArgument;
	if (tx->surface_format == SVGA3D_FORMAT_INVALID) {
		GLLog(1, "%s: invalid surface format\n", __FUNCTION__);
		return kIOReturnNotReady;
	}
	sys_obj_type = tx->sys_obj_type;
	face = 0U;
	mipmap = 0U;
	bzero(&extra, sizeof extra);
	extra.mem_pitch = tx->pitch;
	extra.suffix_flags = 3U;
	ltx = tx;
	switch (sys_obj_type) {
		case TEX_TYPE_SURFACE:
			return kIOReturnSuccess;
		case TEX_TYPE_AGPREF:
			if (tx->linked_agp) {
				ltx = tx->linked_agp;
				extra.mem_offset_in_gmr = static_cast<vm_offset_t>(tx->agp_addr);	// Note: offset of pixels1, not pixels2
				extra.mem_limit = ltx->agp_size - extra.mem_offset_in_gmr;
			}
			/*
			 * TBD: What if linked_agp is NULL??
			 */
			break;
		case TEX_TYPE_STD:
		case TEX_TYPE_OOB:
			if (!isPagedOff(tx->sys_obj, face, mipmap))
				return kIOReturnSuccess;
			dirtyTexture(tx, face, mipmap);
			if (sys_obj_type == TEX_TYPE_OOB && tx->linked_agp) {
				ltx = tx->linked_agp;
				extra.mem_offset_in_gmr = static_cast<vm_offset_t>(ltx->agp_offset_in_page);
				extra.mem_limit = ltx->agp_size - extra.mem_offset_in_gmr;
			}
			/*
			 * TBD: is this needed for types TEX_TYPE_VB as well?
			 * TBD: What if linked_agp is NULL for TEX_TYPE_OOB??
			 */
			rc = readGLDTextureHeader(tx, &extra, face, mipmap);
			if (rc != kIOReturnSuccess) {
				GLLog(1, "%s: readGLDTextureHeader return %#x\n", __FUNCTION__, rc);
				return rc;
			}
			break;
		default:
			GLLog(1, "%s: tex type %u - Unsupported\n", __FUNCTION__, sys_obj_type);
			return kIOReturnUnsupported;
	}
	data_md = ltx->md;
	if (!data_md) {
		GLLog(1, "%s: no memory descriptor for texture data\n", __FUNCTION__);
		return kIOReturnError;
	}
	bzero(&copyBox, sizeof copyBox);
	copyBox.w = tx->width;
	copyBox.h = tx->height;
	copyBox.d = tx->depth;
	rc = create_host_surface_for_texture(tx);
	if (rc != kIOReturnSuccess) {
		GLLog(1, "%s: create_host_surface_for_texture return %#x\n", __FUNCTION__, rc);
		return rc;
	}
	extra.mem_gmr_id = m_provider->AllocGMRID();
	if (!isIdValid(extra.mem_gmr_id)) {
		GLLog(1, "%s: Out of GMR IDs\n", __FUNCTION__);
		return kIOReturnNoResources;
	}
	rc = data_md->prepare();
	if (rc != kIOReturnSuccess) {
		GLLog(1, "%s: prepare failed, return %#x\n", __FUNCTION__, rc);
		m_provider->FreeGMRID(extra.mem_gmr_id);
		return rc;
	}
	rc = m_provider->createGMR(extra.mem_gmr_id, data_md);
	if (rc != kIOReturnSuccess) {
		data_md->complete();
		m_provider->FreeGMRID(extra.mem_gmr_id);
		GLLog(1, "%s: createGMR return %#x\n", __FUNCTION__, rc);
		return rc;
	}
#if 0
	GLLog(3, "%s: width == %u, height == %u, gmr_id == %u, pitch == %lu, offset_in_gmr == %#lx, limit == %#lx\n",
		  __FUNCTION__,
		  tx->width, tx->height, extra.mem_gmr_id,
		  static_cast<size_t>(extra.mem_pitch),
		  static_cast<size_t>(extra.mem_offset_in_gmr),
		  static_cast<size_t>(extra.mem_limit));
#endif
	rc = m_provider->surfaceDMA3DEx(tx->surface_id,
									SVGA3D_WRITE_HOST_VRAM,
									&copyBox,
									&extra,
									&fence);
	if (rc == kIOReturnSuccess)
		m_provider->SyncToFence(fence);	// ugh... to get rid of this need set up a garbage-collection mechanism
	m_provider->destroyGMR(extra.mem_gmr_id);
	data_md->complete();
	m_provider->FreeGMRID(extra.mem_gmr_id);
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::tex_subimage_2d(VMsvga2TextureBuffer* tx,
								size_t command_buffer_offset,
								size_t source_pitch,
								uint32_t width,
								uint32_t height,
								uint32_t destX,
								uint32_t destY,
								uint32_t destZ)
{
	IOReturn rc;
	SVGA3dCopyBox copyBox;
	VMsvga2Accel::ExtraInfoEx extra;

	if (!tx)
		return kIOReturnBadArgument;
	if (!tx->sys_obj)
		return kIOReturnNotReady;
	if (tx->surface_format == SVGA3D_FORMAT_INVALID) {
		GLLog(1, "%s: invalid surface format\n", __FUNCTION__);
		return kIOReturnNotReady;
	}
#if 0
	GLLog(3, "%s(tx %u, offset %#lx, sourcep %lu, w %u, h %u, x %u, y %u, z %u)\n", __FUNCTION__,
		  tx->sys_obj->object_id,
		  command_buffer_offset,
		  source_pitch,
		  width, height, destX, destY, destZ);
#endif
	bzero(&extra, sizeof extra);
	extra.mem_offset_in_gmr = command_buffer_offset;
	extra.mem_pitch = source_pitch;
	extra.mem_limit = m_command_buffer.size - command_buffer_offset;
	extra.suffix_flags = 2U;
	bzero(&copyBox, sizeof copyBox);
	copyBox.w = width;
	copyBox.h = height;
	copyBox.d = 1U;
	copyBox.x = destX;
	copyBox.y = destY;
	copyBox.z = destZ;
	rc = create_host_surface_for_texture(tx);
	if (rc != kIOReturnSuccess) {
		GLLog(1, "%s: create_host_surface_for_texture return %#x\n", __FUNCTION__, rc);
		return rc;
	}
	rc = prepare_command_buffer_io();
	if (rc != kIOReturnSuccess) {
		GLLog(1, "%s: prepare_command_buffer_io return %#x\n", __FUNCTION__, rc);
		return rc;
	}
	extra.mem_gmr_id = m_command_buffer.gmr_id;
	return m_provider->surfaceDMA3DEx(tx->surface_id,
									  SVGA3D_WRITE_HOST_VRAM,
									  &copyBox,
									  &extra,
									  &m_command_buffer.fence);
}

HIDDEN
IOReturn CLASS::bind_texture(uint32_t index, VMsvga2TextureBuffer* tx)
{
	uint32_t i, surface_id, width, height;
	float* f;
	SVGA3dTextureState tstate[7];
	uint32_t const num_states = sizeof tstate / sizeof tstate[0];

	if (!m_provider)
		return kIOReturnNotReady;
	if (index >= 8U || !tx)
		return kIOReturnBadArgument;
	switch (tx->sys_obj_type) {
		case TEX_TYPE_SURFACE:
			if (!tx->linked_surface->getDataForGLBind(&surface_id, &width, &height))
				return kIOReturnNotReady;
			break;
		default:
			surface_id = tx->surface_id;
			width = tx->width;
			height = tx->height;
			break;
	}
	/*
	 * Cache texture size
	 */
	f = m_float_cache + index * 4U;
	f[0] = 1.0F / static_cast<float>(width);
	f[1] = 1.0F / static_cast<float>(height);
	f[2] = 1.0F;
	f[3] = 1.0F;
	for (i = 0U; i != num_states; ++i)
		tstate[i].stage = index;
	tstate[0].name = SVGA3D_TS_BIND_TEXTURE;
	tstate[0].value = surface_id;
	tstate[1].name = SVGA3D_TS_COLOROP;
	tstate[1].value = SVGA3D_TC_MODULATE;
	tstate[2].name = SVGA3D_TS_ALPHAOP;
	tstate[2].value = SVGA3D_TC_MODULATE;
	tstate[3].name = SVGA3D_TS_COLORARG1;
	tstate[3].value = SVGA3D_TA_TEXTURE;
	tstate[4].name = SVGA3D_TS_COLORARG2;
	tstate[4].value = SVGA3D_TA_PREVIOUS;
	tstate[5].name = SVGA3D_TS_ALPHAARG1;
	tstate[5].value = SVGA3D_TA_TEXTURE;
	tstate[6].name = SVGA3D_TS_ALPHAARG2;
	tstate[6].value = SVGA3D_TA_PREVIOUS;
#if 0
	GLLog(3, "%s:   binding texture %u at index %u\n", __FUNCTION__,
		  tx->sys_obj->object_id, index);
#endif
	return m_provider->setTextureState(m_context_id, num_states, &tstate[0]);
}

HIDDEN
void CLASS::setup_drawbuffer_registers(uint32_t* p)
{
#if 0
	/*
	 * First command sets gart_ptr and pitch of
	 *   render target color buffer
	 */
	p[0] = 0x7D8E0001U; /* 3DSTATE_BUF_INFO_CMD */
	p[1] = 0x3800000U;	/* BUF_3D_USE_FENCE | BUF_3D_ID_COLOR_BACK */
	p[2] = 0U;
	/*
	 * Second command sets gart_ptr and pitch of
	 *   render target depth buffer
	 */
	p[3] = 0x7D8E0001U; /* 3DSTATE_BUF_INFO_CMD */
	p[4] = 0x7800000U;	/* BUF_3D_USE_FENCE | BUF_3D_ID_DEPTH */
	p[5] = 0U;
	/*
	 * DRAW_RECT is Intel's version of setViewPort()
	 */
	p[6]  = 0x7D800003U; /* 3DSTATE_DRAW_RECT_CMD */
	p[7]  = this->0x304;	/* DRAW_RECT_DIS_DEPTH_OFS, DRAW_DITHER_OFS_X, DRAW_DITHER_OFS_Y */
	p[8]  = this->0x308;	/* ymin:xmin */
	p[9]  = this->0x30C;	/* ymax:xmax */
	p[10] = this->0x310;	/* yorg:xorg */
#else
	bzero(&p[0], 11U * sizeof(uint32_t));
#endif
}

#pragma mark -
#pragma mark Dispatch Funtions [Apple]
#pragma mark -

HIDDEN void CLASS::process_token_Noop(VendorGLStreamInfo*) {} // Null

HIDDEN
void CLASS::process_token_TextureVolatile(VendorGLStreamInfo* info)
{
	VMsvga2TextureBuffer* tx;
	IOOptionBits old_state;
	uint32_t q = info->p[2];

	tx = m_shared->findTextureBuffer(info->p[1]);
	if (!tx) {
		m_stream_error = 2;
		return;
	}
	switch (tx->sys_obj_type) {
		case TEX_TYPE_STD:
			tx->sys_obj->f1 |= static_cast<uint8_t>(q);
			if (q & 0x10U)
				tx->md->setPurgeable(kIOMemoryPurgeableVolatile, &old_state);
			break;
		case TEX_TYPE_OOB:
			tx->sys_obj->f1 |= static_cast<uint8_t>(q);
			if (q & 0x10U) {
				tx->linked_agp->sys_obj->f1 |= 0x10U;
				tx->linked_agp->md->setPurgeable(kIOMemoryPurgeableVolatile, &old_state);
			}
			break;
		case TEX_TYPE_AGPREF:
			if (q & 0x10U) {
				tx->sys_obj->f1 |= 0x10U;
				tx->linked_agp->sys_obj->f1 |= 0x10U;
				tx->linked_agp->md->setPurgeable(kIOMemoryPurgeableVolatile, &old_state);
			}
			break;
	}
	bzero(&info->p[0], 3U * sizeof(uint32_t));
	if (__sync_fetch_and_add(&tx->sys_obj->refcount, -0x10000) == 0x10000)
		m_shared->delete_texture(tx);
}

HIDDEN
void CLASS::process_token_TextureNonVolatile(VendorGLStreamInfo* info)
{
	VMsvga2TextureBuffer* tx;
	IOOptionBits old_state;

	tx = m_shared->findTextureBuffer(info->p[1]);
	if (!tx) {
		m_stream_error = 2;
		return;
	}
	switch (tx->sys_obj_type) {
		case TEX_TYPE_STD:
			tx->sys_obj->f1 &= ~0x11U;
			tx->md->setPurgeable(kIOMemoryPurgeableNonVolatile, &old_state);
			if (old_state == kIOMemoryPurgeableEmpty)
				tx->sys_obj->f1 |= 0x20U;
			break;
		case TEX_TYPE_OOB:
			tx->sys_obj->f1 &= ~0x11U;
			tx->linked_agp->sys_obj->f1 &= ~0x11U;
			tx->linked_agp->md->setPurgeable(kIOMemoryPurgeableNonVolatile, &old_state);
			if (old_state == kIOMemoryPurgeableEmpty) {
				tx->sys_obj->f1 |= 0x20U;
				tx->linked_agp->sys_obj->f1 |= 0x20U;
			}
			break;
		case TEX_TYPE_AGPREF:
			tx->sys_obj->f1 &= ~0x11U;
			tx->linked_agp->sys_obj->f1 &= ~0x11U;
			tx->linked_agp->md->setPurgeable(kIOMemoryPurgeableNonVolatile, &old_state);
			if (old_state == kIOMemoryPurgeableEmpty) {
				tx->sys_obj->f1 |= 0x20U;
				tx->linked_agp->sys_obj->f1 |= 0x20U;
				tx->sys_obj->in_use = 1U;
			}
			break;
	}
	bzero(&info->p[0], 2U * sizeof(uint32_t));
	if (__sync_fetch_and_add(&tx->sys_obj->refcount, -0x10000) == 0x10000)
		m_shared->delete_texture(tx);
}

HIDDEN
void CLASS::process_token_SetSurfaceState(VendorGLStreamInfo* info)
{
#if 0
	if (info->p[1] == 306U &&
		m_surface_client) {
		m_surface_client->set_volatile_state(info->p[2]);
	}
#endif
	bzero(&info->p[0], 3U * sizeof(uint32_t));
}

HIDDEN
void CLASS::process_token_BindDrawFBO(VendorGLStreamInfo* info)
{
	uint32_t i, tx_id, count = info->p[3];
	VMsvga2TextureBuffer* tx;

	GLLog(1, "%s() Unsupported\n", __FUNCTION__);
	/*
	 * TBD
	 *   Note: The wipe size is correct.  The loop releases
	 *   the texture references to avoid a leak.
	 */
	if (count > 2U)
		count = 2U;
	for (i = 0U; i != count; ++i) {
		tx_id = info->p[4U * i + 4U];
		if (tx_id == 0xFFFFFFFFU)
			continue;
		tx = m_shared->findTextureBuffer(tx_id);
		if (!tx) {
			m_stream_error = 2;
			return;
		}
#if 0
		__sync_fetch_and_add(&tx->sys_obj->refcount, -0xFFFF);
#endif
		if (__sync_fetch_and_add(&tx->sys_obj->refcount, -0x10000) == 0x10000)
			m_shared->delete_texture(tx);
	}
	bzero(&info->p[0], 12U * sizeof(uint32_t));
}

HIDDEN
void CLASS::process_token_BindReadFBO(VendorGLStreamInfo* info)
{
	uint32_t i, tx_id, count = info->p[3];
	VMsvga2TextureBuffer* tx;

	GLLog(1, "%s() Unsupported\n", __FUNCTION__);
	/*
	 * TBD
	 *   Note: The wipe size is correct.  The loop releases
	 *   the texture references to avoid a leak.
	 */
	if (count > 2U)
		count = 2U;
	for (i = 0U; i != count; ++i) {
		tx_id = info->p[4U * i + 4U];
		if (tx_id == 0xFFFFFFFFU)
			continue;
		tx = m_shared->findTextureBuffer(tx_id);
		if (!tx) {
			m_stream_error = 2;
			return;
		}
#if 0
		__sync_fetch_and_add(&tx->sys_obj->refcount, -0xFFFF);
#endif
		if (__sync_fetch_and_add(&tx->sys_obj->refcount, -0x10000) == 0x10000)
			m_shared->delete_texture(tx);
	}
	bzero(&info->p[0], 12U * sizeof(uint32_t));
}

HIDDEN
void CLASS::process_token_UnbindDrawFBO(VendorGLStreamInfo* info)
{
#if 0
	uint32_t i;
	VMsvga2TextureBuffer* tx;
#endif

	GLLog(1, "%s() Unsupported\n", __FUNCTION__);
#if 0
	if (this->0x204) {
		for (i = 0U; i != 2U; ++i) {	// loops over color & depth buffers
			tx = m_txs[17U + i];
			if (!tx)
				continue;
			dirtyTexture(tx,
						 byte ptr this->0x204->[12U * i + 8U],	// face
						 byte ptr this->0x204->[12U * i + 9U]);	// mipmap
			removeTextureFromStream(tx);
			if (__sync_fetch_and_add(&tx->sys_obj->refcount, -1) == 1)
				m_shared->delete_texture(tx);
			m_txs[17U + i] = 0;
		}
		IOFree(this->0x204, 32U);
		this->0x204 = 0;
	}
#endif
	info->p[0] = 0U;
}

HIDDEN
void CLASS::process_token_UnbindReadFBO(VendorGLStreamInfo* info)
{
#if 0
	uint32_t i;
	VMsvga2TextureBuffer* tx;
#endif

	GLLog(1, "%s() Unsupported\n", __FUNCTION__);
#if 0
	if (this->0x208) {
		for (i = 0U; i != 2U; ++i) {	// loops over color & depth buffers
			tx = m_txs[19U + i];
			if (!tx)
				continue;
			removeTextureFromStream(tx);
			if (__sync_fetch_and_add(&tx->sys_obj->refcount, -1) == 1)
				m_shared->delete_texture(tx);
			m_txs[19U + i] = 0;
		}
		IOFree(this->0x208, 32U);
		this->0x208 = 0;
	}
#endif
	info->p[0] = 0U;
}

HIDDEN void CLASS::process_token_Start(VendorGLStreamInfo*) {} // Null
HIDDEN void CLASS::process_token_End(VendorGLStreamInfo*) {} // Null

HIDDEN
void CLASS::process_token_Swap(VendorGLStreamInfo* info)
{
	info->flags = 3U;
}

HIDDEN
void CLASS::process_token_Flush(VendorGLStreamInfo* info)
{
	info->flags = 2U;
}

HIDDEN
void CLASS::discard_token_Texture(VendorGLStreamInfo* info)
{
	VMsvga2TextureBuffer* tx;
	uint32_t *q, i, bit_mask;
	q = &info->p[2];	// edi
	bit_mask = info->p[1];	// var_2c
	for (i = 0U; i != 16U; ++i) {
		tx = m_txs[i];
		if (tx) {
			removeTextureFromStream(tx);
			if (__sync_fetch_and_add(&tx->sys_obj->refcount, -1) == 1)
				m_shared->delete_texture(tx);
			m_txs[i] = 0;
		}
		if (!((bit_mask >> i) & 1U))
			continue;
		tx = m_shared->findTextureBuffer(*q);
		if (!tx) {
			info->cmd = 0U;
			info->ds_count_dwords = 0;
#if 0
			m_provider->0x50 -= info->f2;
#endif
			m_stream_error = 2;
			return;
		}
		addTextureToStream(tx);
		__sync_fetch_and_add(&tx->sys_obj->refcount, -0xFFFF);
		m_txs[i] = tx;
		q += 3;
	}
}

HIDDEN
void CLASS::discard_token_NoTex(VendorGLStreamInfo* info)
{
	uint32_t i;
	VMsvga2TextureBuffer* tx;

	if (info->p[1] != 0xFFFFFFFFU) {
		tx = m_shared->findTextureBuffer(info->p[1]);
		if (!tx) {
			info->cmd = 0U;
			info->ds_count_dwords = 0;
#if 0
			m_provider->0x50 -= info->f2;
#endif
			m_stream_error = 2;
			return;
		}
		if (__sync_fetch_and_add(&tx->sys_obj->refcount, -0x10000) == 0x10000)
			m_shared->delete_texture(tx);
	} else {
		i = (info->cmd >> 24) - 36U;
		tx = m_txs[i];
		if (tx) {
			removeTextureFromStream(tx);
			if (__sync_fetch_and_add(&tx->sys_obj->refcount, -1) == 1)
				m_shared->delete_texture(tx);
			m_txs[i] = 0;
		}
	}
}

HIDDEN
void CLASS::discard_token_VertexBuffer(VendorGLStreamInfo* info)
{
	VMsvga2TextureBuffer* tx;
	tx = m_shared->findTextureBuffer(info->p[1]);
	if (!tx) {
		info->cmd = 0U;
		info->ds_count_dwords = 0;
#if 0
		m_provider->0x50 -= info->f2;
#endif
		m_stream_error = 2;
		return;
	}
	m_txs[16] = tx;
}

HIDDEN
void CLASS::discard_token_NoVertexBuffer(VendorGLStreamInfo*)
{
	m_txs[16] = 0;
}

HIDDEN
void CLASS::discard_token_DrawBuffer(VendorGLStreamInfo* info)
{
#if 0
	/*
	 * word ptr this->0xA8
	 * word ptr this->0xAA
	 */
	switch (info->p[1]) {
		case 1:
			this->0xA8 = 0;
			break;
		case 7:
			this->0xA8 = 2;
			break;
		case 8:
			this->0xA8 = 3;
			break;
		default:
			this->0xA8 = 1;
			break;
	}
	if (!(byte ptr this->0x90 & 0x20U))
		return;
	switch (info->p[1]) {
		case 7:
			this->0xAA = 5;
			break;
		case 8:
			this->0xAA = 6;
			break;
		default:
			this->0xAA = 4;
			break;
	}
#endif
}

HIDDEN void CLASS::discard_token_Noop(VendorGLStreamInfo*) {} // Null

HIDDEN
void CLASS::discard_token_TexSubImage2D(VendorGLStreamInfo* info)
{
	VMsvga2TextureBuffer* tx;
	tx = m_shared->findTextureBuffer(info->p[6]);
	if (!tx) {
		info->cmd = 0U;
		info->ds_count_dwords = 0;
#if 0
		m_provider->0x50 -= info->f2;
#endif
		m_stream_error = 2;
		return;
	}
	if (__sync_fetch_and_add(&tx->sys_obj->refcount, -0x10000) == 0x10000)
		m_shared->delete_texture(tx);
}

HIDDEN
void CLASS::discard_token_CopyPixelsDst(VendorGLStreamInfo* info)
{
	VMsvga2TextureBuffer* tx;
	tx = m_shared->findTextureBuffer(info->p[1]);
	if (!tx) {
		info->cmd = 0U;
		info->ds_count_dwords = 0;
#if 0
		m_provider->0x50 -= info->f2;
#endif
		m_stream_error = 2;
		return;
	}
	if (__sync_fetch_and_add(&tx->sys_obj->refcount, -0x10000) == 0x10000)
		m_shared->delete_texture(tx);
}

HIDDEN
void CLASS::discard_token_AsyncReadDrawBuffer(VendorGLStreamInfo* info)
{
	VMsvga2TextureBuffer* tx;

	tx = m_shared->findTextureBuffer(info->p[1]);
	if (!tx) {
		info->cmd = 0U;
		info->ds_count_dwords = 0;
#if 0
		m_provider->0x50 -= info->f2;
#endif
		m_stream_error = 2;
		return;
	}
	if (__sync_fetch_and_add(&tx->sys_obj->refcount, -0x10000) == 0x10000)
		m_shared->delete_texture(tx);
	bzero(&info->p[0], 10U * sizeof(uint32_t));
}

HIDDEN
void CLASS::process_token_Texture(VendorGLStreamInfo* info)
{
	VMsvga2TextureBuffer* tx;
	uint32_t *p, *q, bit_mask, i, count;
	p = info->p; // var_38
	q = &info->p[2];	// var_2c
	bit_mask = p[1];	// var_34
#if 0
	this->0x238 = bit_mask;
#endif
	count = 0U; // var_30
	for (i = 0U; i != 16U; ++i) {
		tx = m_txs[i];
		if (tx) {
			removeTextureFromStream(tx);
			if (__sync_fetch_and_add(&tx->sys_obj->refcount, -1) == 1)
				m_shared->delete_texture(tx);
			m_txs[i] = 0;
		}
		if (!((bit_mask >> i) & 1U))
			continue;
		tx = m_shared->findTextureBuffer(*q);	// tx now in var_1c
		if (!tx) {
			info->cmd = 0U;
			info->ds_count_dwords = 0;
#if 0
			m_provider->0x50 -= info->f2;
#endif
			m_stream_error = 2;
			return;
		}
		++count;
		addTextureToStream(tx);
		if (tx->surface_format == SVGA3D_FORMAT_INVALID)
			tx->surface_format = decipher_format(bit_select(q[1], 7, 3), bit_select(q[1], 3, 4));
		get_texture(info, tx, true);
		__sync_fetch_and_add(&tx->sys_obj->refcount, -0xFFFF);
#if 0
		this->0x240 = 0U;
		this->0x244 = q[1];
		this->0x248 = q[2];
		write_tex_data(i, q, tx);
#endif
		m_txs[i] = tx;
		bind_texture(i, tx);	// Added
		q += 3;
	}
	if (count > 1U)
		GLLog(1, "%s: %u texture stages - Unsupported blend\n", __FUNCTION__, count);
#if 0
	this->0x23C = count;
#endif
	if (count)
		*p = 0x7D000000U | (3U * count); /* 3DSTATE_MAP_STATE */
	else
		*p = 0;
}

HIDDEN
void CLASS::process_token_NoTex(VendorGLStreamInfo* info)
{
	uint32_t i;
	VMsvga2TextureBuffer* tx;

	if (info->p[1] != 0xFFFFFFFFU) {
		tx = m_shared->findTextureBuffer(info->p[1]);
		if (!tx) {
			info->cmd = 0U;
			info->ds_count_dwords = 0;
#if 0
			m_provider->0x50 -= info->f2;
#endif
			m_stream_error = 2;
			return;
		}
		if (__sync_fetch_and_add(&tx->sys_obj->refcount, -0x10000) == 0x10000)
			m_shared->delete_texture(tx);
	} else {
		i = (info->cmd >> 24) - 36U;
		tx = m_txs[i];
		if (tx) {
			removeTextureFromStream(tx);
			if (__sync_fetch_and_add(&tx->sys_obj->refcount, -1) == 1)
				m_shared->delete_texture(tx);
			m_txs[i] = 0;
		}
	}
	info->p[0] = 0U;
	info->p[1] = 0U;
}

HIDDEN
void CLASS::process_token_VertexBuffer(VendorGLStreamInfo* info)
{
	VMsvga2TextureBuffer* tx;

	GLLog(1, "%s() Unsupported\n", __FUNCTION__);
	tx = m_shared->findTextureBuffer(info->p[1]);
	if (!tx) {
		info->cmd = 0U;
		info->ds_count_dwords = 0;
#if 0
		m_provider->0x50 -= info->f2;
#endif
		m_stream_error = 2;
		return;
	}
	if (tx != m_txs[16]) {
		if (m_txs[16]) {
			m_txs[16]->sys_obj->stamps[0] = 0U /* m_provider->0x50 */;
			--m_txs[16]->counter14;
		}
		++tx->counter14;
		m_txs[16] = tx;
	}
	if (!m_txs[16]->gart_ptr) {
		submit_midbuffer(info);
#if 0
		mapTransferToGART(tx);
		if (!m_command_buffer.gart_ptr)
			mapTransferToGART(&m_command_buffer);
#endif
	}
#if 0
	info->p[0] = 0x4CU; // TBD... strange doesn't look like an opcode, maybe middle of an Intel CMD
	info->p[1] = tx->gart_ptr + 128U;	// Looks like this is a gart_ptr for a TEX_TYPE_VB texture with offset 0x80
#else
	info->p[0] = 0U;
	info->p[1] = 0U;
#endif
}

HIDDEN
void CLASS::process_token_NoVertexBuffer(VendorGLStreamInfo* info)
{
	GLLog(1, "%s() Unsupported\n", __FUNCTION__);
	if (m_txs[16]) {
		m_txs[16]->sys_obj->stamps[0] = 0U /* m_provider->0x50 */;
		--m_txs[16]->counter14;
		m_txs[16] = 0;
	}
#if 0
	info->p[0] = 0x4CU;	// TBD... and again
#else
	info->p[0] = 0U;
#endif
}

HIDDEN
void CLASS::process_token_DrawBuffer(VendorGLStreamInfo* info)
{
#if 0
	/*
	 * word ptr this->0xA8
	 * word ptr this->0xAA
	 */
	if (dword ptr this->0x204) {
		this->0xA8 = info->p[1] == 17U ? 1 : info->p[1];
		this->0xAA = m_txs[18] != 0 ? 1 : this->0xA8;
		goto finish;
	}
	if (!m_surface_client)
		goto finish;
	switch (info->p[1]) {
		case 1:
			this->0xA8 = 0;
			break;
		case 7:
			this->0xA8 = 2;
			break;
		case 8:
			this->0xA8 = 3;
			break;
		default:
			this->0xA8 = 1;
			break;
	}
	switch (info->p[1]) {
		case 7:
			this->0xAA = 5;
			break;
		case 8:
			this->0xAA = 6;
			break;
		default:
			this->0xAA = 4;
			break;
	}
finish:
#endif
	setup_drawbuffer_registers(info->p);
}

HIDDEN
void CLASS::process_token_SetFence(VendorGLStreamInfo* info)
{
	/*
	 * TBD: support pipeline fences somehow
	 */
	GLLog(1, "%s() Unsupported\n", __FUNCTION__);
#if 0
	uint32_t val = info->p[1];
	struct { uint32_t u; uint32_t v; } *q;
	if (val * sizeof *q >= m_type2_len) {
		info->cmd = 0U;
		info->ds_count_dwords = 0;
#if 0
		m_provider->0x50 -= info->f2;
#endif
		m_stream_error = 2;
		return;
	}
	info->p[0] = 0x2000000U;	// MI_FLUSH
	info->p[1] = 0x10800001U;	// MI_STORE_DATA_INDEX
	info->p[2] = 64U;
	info->p[3] = 0U /* m_provider->0x50 */;
	q = reinterpret_cast<typeof q>(m_type2_ptr);
	q[val].u = 0U /* m_provider->0x50++ */;
	q[val].v = 0U;
	++info->f2;
#else
	bzero(&info->p[0], 4U * sizeof(uint32_t));
#endif
}

HIDDEN
void CLASS::process_token_TexSubImage2D(VendorGLStreamInfo* info)
{
	VMsvga2TextureBuffer* tx;
	GLDTexSubImage2DStruc* helper = reinterpret_cast<typeof helper>(&info->p[1]);
#if 0
	uint32_t dest_gart_addr /* var_20 */, dest_pitch /* var_24 */, face, mipmap, height /* var_2c */;
	uint32_t ip1 /* var_30 */, ip3 /* esi */, ip8 /* var_34 */;
	// edi = info
	// ebx = info->p
	dest_pitch = 0U;
	// var_44 = &info->p[1]
	ip1 = info->p[1];
	// var_40 = &info->p[3]
	ip3 = dest_gart_addr = info->p[3];
	// var_3c = &info->p[8]
	ip8 = info->p[8];
	// var_38 = &info->p[6]
#endif
	tx = m_shared->findTextureBuffer(helper->texture_obj_id);	// to var_1c
	if (!tx) {
		info->cmd = 0U;
		info->ds_count_dwords = 0;
#if 0
		m_provider->0x50 -= info->f2;
#endif
		m_stream_error = 2;
		return;
	}
	addTextureToStream(tx);
	/*
	 * TBD:
	 * Temporary hack, need to figure out how to handle this
	 * [On Intel 915, the blit only needs to know bytespp,
	 *  not complete format]
	 */
	if (tx->surface_format == SVGA3D_FORMAT_INVALID)
		tx->surface_format = default_format(tx->bytespp);
	get_texture(info, tx, true);
#if 0
	get_tex_data(tx, &dest_gart_addr, &dest_pitch);
	height = tx->vram_tile_pages >> 32; /* should be texture height, how did it get here? */;
	face = helper->face;
	mipmap = helper->mipmap;
	if (face > 6U)
		face = 0U;
	if (mipmap >= 16U)
		mipmap = 0U;
	dirtyTexture(tx, face, mipmap);
	info->p[5] += m_command_buffer.gart_ptr;
	info->p[0] = helper->intel_cmd;
	info->p[1] = (ip1 & 0xFFFF0000U) | dest_pitch;
	info->p[3] = dest_pitch * ((ip3 >> 16) + height * (ip1 & 0xFFFFU)) + (ip3 & 0xFFFFU) + dest_gart_addr;
	info->p[6] = 0x2000001U;	// MI_FLUSH
	info->p[7] = 0U;
	info->p[8] = 0U;
#else
	if (!tx->bytespp ||
		helper->face != 0U ||
		helper->mipmap != 0U)
		goto skip;
	dirtyTexture(tx, helper->face, helper->mipmap);
	/*
	 * Note: Blits to face 0U, mipmap 0U
	 */
	tex_subimage_2d(tx,
					helper->source_addr,
					helper->source_pitch,
					helper->width / tx->bytespp,
					helper->height,
					helper->dest_x / tx->bytespp,
					helper->dest_y,
					helper->dest_z);
skip:
	bzero(&info->p[0], 9U * sizeof(uint32_t));
#endif
	removeTextureFromStream(tx);
	if (__sync_fetch_and_add(&tx->sys_obj->refcount, -0x10000) == 0x10000)
		m_shared->delete_texture(tx);
}

HIDDEN
void CLASS::process_token_CopyPixelsDst(VendorGLStreamInfo* info)
{
	VMsvga2TextureBuffer* tx;
#if 0
	uint32_t gart_ptr, gart_pitch;
	uint8_t face, mipmap;
#endif

	GLLog(1, "%s() Unsupported\n", __FUNCTION__);
	tx = m_shared->findTextureBuffer(info->p[1]);
	if (!tx) {
		info->cmd = 0U;
		info->ds_count_dwords = 0;
#if 0
		m_provider->0x50 -= info->f2;
#endif
		m_stream_error = 2;
		return;
	}
#if 0
	addTextureToStream(tx);
	get_texture(info, tx, true);
	face = info->p[4] >> 16;
	mipmap = info->p[4] & 0xFFFFU;
	if (face >= 6U)
		face = 0U;
	if (mipmap >= 16U)
		mipmap = 0U;
	dirtyTexture(tx, face, mipmap);
	get_tex_data(tx, &gart_ptr, &gart_pitch);
	gart_ptr += (info->p[2] & 0xFFFFU) + (info->p[2] >> 16) * gart_pitch;
	/*
	 * Note: this switches the render target to the texture...
	 */
	info->p[0] = 0x7D8E0001U; /* _3DSTATE_BUF_INFO_CMD */
	info->p[1] = 0x3800000U | gart_pitch;
	info->p[2] = gart_ptr;
	info->p[3] = 0U;
	info->p[4] = 0x7D850000U; /* 3DSTATE_DST_BUF_VARS_CMD */
	/* Another dword should follow */
#endif
	if (__sync_fetch_and_add(&tx->sys_obj->refcount, -0x10000) == 0x10000)
		m_shared->delete_texture(tx);
	bzero(&info->p[0], 6U * sizeof(uint32_t));
}

HIDDEN
void CLASS::process_token_CopyPixelsSrc(VendorGLStreamInfo* info)
{
	GLLog(1, "%s() Unsupported\n", __FUNCTION__);
	/*
	 * Note: also sets info->p[11] thru info->p[26] to a bunch of floats.
	 *   This function copies pixels from the render target.
	 */
	bzero(&info->p[0], 5U * sizeof(uint32_t));
}

HIDDEN
void CLASS::process_token_CopyPixelsSrcFBO(VendorGLStreamInfo* info)
{
	GLLog(1, "%s() Unsupported\n", __FUNCTION__);
	/*
	 * Note: also sets info->p[11] thru info->p[26] to a bunch of floats.
	 * Note: There's a texture reference at info->p[1] in this command,
	 *   and it's not released... looks like a leak in Apple's code.
	 */
	discard_token_CopyPixelsDst(info);	// plug the leak - discards a texture reference @ info->p[1]
	bzero(&info->p[0], 5U * sizeof(uint32_t));
}

HIDDEN
void CLASS::process_token_DrawRect(VendorGLStreamInfo* info)
{
	GLLog(1, "%s() Unsupported\n", __FUNCTION__);
	/*
	 * TBD: Should prepare parameters for setViewPort() here
	 */
	info->p[0] = 0x7D800003U; /* 3DSTATE_DRAW_RECT_CMD */
#if 0
	memcpy(&info->p[1], this->0x304U, 4U * sizeof(uint32_t));
#else
	bzero(&info->p[1], 4U * sizeof(uint32_t));
#endif
}

HIDDEN
void CLASS::process_token_AsyncReadDrawBuffer(VendorGLStreamInfo* info)
{
	GLLog(1, "%s() Unsupported\n", __FUNCTION__);
	discard_token_AsyncReadDrawBuffer(info);
}
