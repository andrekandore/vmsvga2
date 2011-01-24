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
#include "VMsvga2IPP.h"
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

struct FBODescriptor
{
	uint16_t width;
	uint16_t height;
	uint8_t f0;
	struct {
		uint8_t face;
		uint8_t mipmap;
		uint16_t g0;
		uint16_t x;
		uint16_t y;
		uint32_t g1;
	} txs[2];
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
				case 5: // MT_8BIT_MONO8
					return SVGA3D_LUMINANCE8;
				case 0: // MT_8BIT_I8
				case 4: // MT_8BIT_A8
					return SVGA3D_ALPHA8;
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
int default_color_format(uint8_t bytespp, uint8_t sys_obj_type)
{
	switch (bytespp) {
		case 4U:
			return SVGA3D_A8R8G8B8;
		case 2U:
			return SVGA3D_LUMINANCE16 /* SVGA3D_A1R5G5B5 */;
		case 1U:
			return sys_obj_type != TEX_TYPE_AGPREF ? SVGA3D_LUMINANCE8 : SVGA3D_BUFFER;
	}
	return SVGA3D_FORMAT_INVALID;
}

static
int default_depth_format(uint8_t bytespp)
{
	switch (bytespp) {
		case 4U:
			return SVGA3D_Z_D24S8;
		case 2U:
			return SVGA3D_Z_D16;
		case 1U:
			return SVGA3D_LUMINANCE8;
	}
	return SVGA3D_FORMAT_INVALID;
}

static inline
int select_default_format(VMsvga2TextureBuffer* tx, int usage)
{
	return usage ? default_depth_format(tx->bytespp) : default_color_format(tx->bytespp, tx->sys_obj_type);
}

static inline
uint8_t sanitize_face(uint8_t face)
{
	return face >= 6U ? 0U : face;
}

static inline
uint8_t sanitize_mipmap(uint8_t mipmap)
{
	return mipmap >= 12U ? 0U : mipmap;
}

static
bool isPagedOff(GLDSysObject* sys_obj, uint8_t face, uint8_t mipmap)
{
	if (!sys_obj || face >= 6U || mipmap >= 16U)
		return false;
	return ((sys_obj->pageoff[face] & ~sys_obj->pageon[face]) >> mipmap) & 1U;
}

static
bool areAnyPagedOff(GLDSysObject* sys_obj)
{
	int i;
	if (!sys_obj)
		return false;
	for (i = 0; i != 6; ++i)
		if (sys_obj->pageoff[i] & ~sys_obj->pageon[i])
			return true;
	return false;
}

static
bool need_yuv_shadow(VMsvga2Accel* provider, int surface_format)
{
	uint32_t const combined = SVGA3DFORMAT_OP_CONVERT_TO_ARGB | SVGA3DFORMAT_OP_TEXTURE;
	/*
	 * Can generalize this for all surface types, but then to
	 *   be fully general, also need to do this for
	 *   TEX_TYPE_STD, TEX_TYPE_OOB, which requires handling
	 *   faces, mipping - argh.
	 */
	static int const pairs[][2] = {
		{ SVGA3D_UYVY, SVGA3D_DEVCAP_SURFACEFMT_UYVY },
		{ SVGA3D_YUY2, SVGA3D_DEVCAP_SURFACEFMT_YUY2 }
	};
	for (int i = 0; i != 2; ++i)
		if (surface_format == pairs[i][0] &&
			(provider->getDevCap(pairs[i][1]) & combined) == SVGA3DFORMAT_OP_CONVERT_TO_ARGB)
			return true;
	return false;
}

IOReturn mapGLDTextureHeader(VMsvga2TextureBuffer* tx, IOMemoryMap** map);

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

void CLASS::CleanupApp()
{
	int i;
	/*
	 * TBD: There may be left over refcounted textures on m_txs.
	 *   should we expect m_shared to destroy them?
	 *   We shouldn't because there may be other GL contexts
	 *   connected to the same m_shared, so we should lock
	 *   m_shared and deref them. [Make sure to do this
	 *   before releasing m_shared.]
	 */
	for (i = 0; i != 2; ++i)
		if (m_fbo[i]) {
			IOFree(m_fbo[i], sizeof(FBODescriptor));
			m_fbo[i] = 0;
		}
}

HIDDEN
uint32_t CLASS::processCommandBuffer(VendorCommandDescriptor* result)
{
	VendorGLStreamInfo cb_iter;
	uint32_t upper, commands_processed;

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
#if LOGGING_LEVEL >= 4
		GLLog(4, "%s:   cmd %#x length %u\n", __FUNCTION__, upper, cb_iter.cmd & 0xFFFFFFU);
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
#if LOGGING_LEVEL >= 3
	GLLog(3, "%s:   processed %d stream commands, error == %#x\n", __FUNCTION__, commands_processed, m_stream_error);
#endif
	result->next = &m_command_buffer.kernel_ptr->downstream[0] + cb_iter.dso_bytes / sizeof(uint32_t);
	result->ds_ptr = m_command_buffer.xfer.gart_ptr + static_cast<uint32_t>(sizeof(VendorCommandBufferHeader)) + cb_iter.dso_bytes;
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
#if LOGGING_LEVEL >= 4
	GLDSysObject* sys_obj = tx->sys_obj;
	if (!sys_obj)
		return;
	GLLog(4, "%s: obj %u, pageoff[0] == %u, pageon[0] == %u\n", __FUNCTION__,
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
		m_ipp->submit_buffer(&m_command_buffer.kernel_ptr->downstream[0] + info->dso_bytes / sizeof(uint32_t),
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
	VMsvga2TextureBuffer* ltx;
#endif
	if (tx->sys_obj->in_use) {
		submit_midbuffer(info);
		alloc_and_load_texture(tx);
#if 0
		if (flag & !m_command_buffer.xfer.gart_ptr)
			mapTransferToGART(&m_command_buffer.xfer);
#endif
	}
#if 0
	ltx = tx;
	if (ltx->sys_obj_type == TEX_TYPE_IOSURFACE)
		ltx = ltx->linked_agp;
	if (ltx->glk.gart_ptr) {
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
void CLASS::get_tex_data(VMsvga2TextureBuffer* tx, uint32_t* tex_gart_address, uint32_t* tex_pitch, int usage)
{
	switch (tx->sys_obj_type) {
		case TEX_TYPE_SURFACE:
			if (tx->linked_surface) {
				if (usage)
					tx->linked_surface->getSurfacesForGL(0, tex_gart_address);	// Note: depth, ignores error
				else
					tx->linked_surface->getSurfacesForGL(tex_gart_address, 0);	// Note: color, ignores error
			} else
				*tex_gart_address = SVGA_ID_INVALID;
			break;
		default:
			*tex_gart_address = tx->surface_id;
			break;
	}
	*tex_pitch = 512U;
}

HIDDEN
void CLASS::write_tex_data(uint32_t i, uint32_t* q, VMsvga2TextureBuffer* tx)
{
#if 0
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
#endif
	switch (tx->sys_obj_type) {
		case TEX_TYPE_SURFACE:
			if (tx->linked_surface)
				tx->linked_surface->getSurfacesForGL(&q[0], 0);	// Note: ignores error
			else
				q[0] = SVGA_ID_INVALID;
			/*
			 * Note: Assumes that the texture width & height needed to
			 *   normalize the tex coordinates are sent by the GLD in
			 *   q[1] for all surface types, including TEX_TYPE_SURFACE.
			 */
			break;
		default:
			q[0] = tx->surface_id;
			break;
	}
}

HIDDEN
IOReturn CLASS::create_host_surface_for_texture(VMsvga2TextureBuffer* tx)
{
	IOReturn rc;
	uint32_t face, mipmap, surface_flags, surface_format;
	bool yuv;
	SVGA3D* svga3d;
	SVGA3dSurfaceFace* faces;
	SVGA3dSize* mipmaps;
	IOMemoryMap* mmap;
	GLDTextureHeader *headers, *gld_th;

	if (!m_provider)
		return kIOReturnNotReady;
	if (!tx)
		return kIOReturnBadArgument;
	if (tx->sys_obj_type == TEX_TYPE_SURFACE)
		return kIOReturnSuccess;
	if (isIdValid(tx->surface_id))
		return kIOReturnSuccess;
	tx->surface_id = m_provider->AllocSurfaceID();	// Note: doesn't fail
	switch (tx->sys_obj_type) {
		case TEX_TYPE_AGPREF:
			surface_format = tx->surface_format;
			surface_flags = SVGA3D_SURFACE_HINT_TEXTURE;
			yuv = need_yuv_shadow(m_provider, surface_format);
			if (yuv) {
				surface_format = SVGA3D_A8R8G8B8;
				surface_flags |= SVGA3D_SURFACE_HINT_RENDERTARGET;	// Note: maybe do this for all textures
				tx->yuv_shadow = m_provider->AllocSurfaceID();	// Note: doesn't fail
				GLLog(3, "%s: Using YUV Alternate for texture %u\n", __FUNCTION__, tx->sys_obj->object_id);
			}
			svga3d = m_provider->lock3D();
			if (!svga3d) {
				GLLog(1, "%s: cannot lock device\n", __FUNCTION__);
				rc = kIOReturnNotReady;
				if (yuv) {
					m_provider->FreeSurfaceID(tx->yuv_shadow);
					tx->yuv_shadow = SVGA_ID_INVALID;
				}
				goto clean1;
			}
			svga3d->BeginDefineSurface(tx->surface_id,
									   SVGA3dSurfaceFlags(surface_flags),
									   SVGA3dSurfaceFormat(surface_format),
									   &faces,
									   &mipmaps,
									   1U);
			faces->numMipLevels = 1U;
			mipmaps->width = tx->width;
			mipmaps->height = tx->height;
			mipmaps->depth = tx->depth;
			svga3d->FIFOCommitAll();
			if (yuv) {
				svga3d->BeginDefineSurface(tx->yuv_shadow,
										   SVGA3dSurfaceFlags(0),
										   SVGA3dSurfaceFormat(tx->surface_format),
										   &faces,
										   &mipmaps,
										   1U);
				faces->numMipLevels = 1U;
				mipmaps->width = tx->width;
				mipmaps->height = tx->height;
				mipmaps->depth = tx->depth;
				svga3d->FIFOCommitAll();
			}
			m_provider->unlock3D();
			break;
		case TEX_TYPE_STD:
		case TEX_TYPE_OOB:
			switch (tx->surface_format) {
				case SVGA3D_Z_D24S8:
				case SVGA3D_Z_D16:
					surface_flags = SVGA3D_SURFACE_HINT_DEPTHSTENCIL;
					break;
				default:
					surface_flags = SVGA3D_SURFACE_HINT_TEXTURE;
					if (tx->num_faces == 6U)
						surface_flags |= SVGA3D_SURFACE_CUBEMAP;
					break;
			}
			rc = mapGLDTextureHeader(tx, &mmap);
			if (rc != kIOReturnSuccess) {
				GLLog(1, "%s: mapGLDTextureHeader return %#x\n", __FUNCTION__, rc);
				goto clean1;
			}
			headers = reinterpret_cast<typeof headers>(mmap->getVirtualAddress()) + tx->min_mipmap;
			svga3d = m_provider->lock3D();
			if (!svga3d) {
				GLLog(1, "%s: cannot lock device\n", __FUNCTION__);
				rc = kIOReturnNotReady;
				mmap->release();
				goto clean1;
			}
			svga3d->BeginDefineSurface(tx->surface_id,
									   SVGA3dSurfaceFlags(surface_flags),
									   SVGA3dSurfaceFormat(tx->surface_format),
									   &faces,
									   &mipmaps,
									   tx->num_faces * tx->num_mipmaps);
			for (face = 0U; face != tx->num_faces; ++face, headers += 12) {
				faces[face].numMipLevels = tx->num_mipmaps;
				for (gld_th = headers, mipmap = 0U; mipmap != tx->num_mipmaps; ++mipmap, ++gld_th, ++mipmaps) {
					if (!gld_th->pixels_in_client)
						continue;
					mipmaps->width  = gld_th->width_bytes / tx->bytespp;
					mipmaps->height = gld_th->height;
					mipmaps->depth  = gld_th->depth;
				}
			}
			svga3d->FIFOCommitAll();
			m_provider->unlock3D();
			mmap->release();
			break;
		default:
			rc = kIOReturnUnsupported;
			goto clean1;
	}
	return kIOReturnSuccess;

clean1:
	m_provider->FreeSurfaceID(tx->surface_id);
	tx->surface_id = SVGA_ID_INVALID;
	return rc;
}

HIDDEN
IOReturn CLASS::alloc_and_load_texture(VMsvga2TextureBuffer* tx)
{
	IOReturn rc;
	uint8_t sys_obj_type;
	IOMemoryMap* mmap;
	SVGA3dSurfaceImageId hostImage;
	SVGA3dCopyBox copyBox;
	VMsvga2Accel::ExtraInfoEx extra;
	IOAccelBounds rect;
	VMsvga2TextureBuffer* ltx;
	GLDTextureHeader *headers, *gld_th;

	if (!m_provider)
		return kIOReturnNotReady;
	if (!tx)
		return kIOReturnBadArgument;
	if (tx->surface_format == SVGA3D_FORMAT_INVALID) {
		GLLog(1, "%s: invalid surface format\n", __FUNCTION__);
		return kIOReturnNotReady;
	}
	sys_obj_type = tx->sys_obj_type;
	if (sys_obj_type == TEX_TYPE_SURFACE)
		return kIOReturnSuccess;
	if (tx->sys_obj->vstate & 0x10U) {
		tx->sys_obj->vstate |= 2U;
		return kIOReturnSuccess;
	}
	mmap = 0;
	bzero(&copyBox, sizeof copyBox);
	bzero(&extra, sizeof extra);
	extra.mem_limit = 0xFFFFFFFFU;
	extra.suffix_flags = 3U;
	ltx = tx;
	headers = 0;
	switch (sys_obj_type) {
		case TEX_TYPE_AGPREF:
			ltx = tx->linked_agp;
			if (!ltx)
				return kIOReturnBadArgument;
			if (ltx->sys_obj->vstate & 0x10U) {
				tx->sys_obj->vstate |= 2U;
				return kIOReturnSuccess;
			}
			extra.mem_offset_in_gmr = static_cast<vm_offset_t>(tx->agp_addr);	// Note: offset of pixels1, not pixels2
			extra.mem_pitch = tx->pitch;
			break;
		case TEX_TYPE_STD:
		case TEX_TYPE_OOB:
			if (!areAnyPagedOff(tx->sys_obj))
				return kIOReturnSuccess;
			if (sys_obj_type == TEX_TYPE_OOB) {
				ltx = tx->linked_agp;
				if (!ltx)
					return kIOReturnBadArgument;
				if (ltx->sys_obj->vstate & 0x10U) {
					tx->sys_obj->vstate |= 2U;
					return kIOReturnSuccess;
				}
			}
			/*
			 * TBD: is this needed for types TEX_TYPE_VB as well?
			 */
			rc = mapGLDTextureHeader(tx, &mmap);
			if (rc != kIOReturnSuccess) {
				GLLog(1, "%s: mapGLDTextureHeader return %#x\n", __FUNCTION__, rc);
				return rc;
			}
			headers = reinterpret_cast<typeof headers>(mmap->getVirtualAddress()) + tx->min_mipmap;
			break;
		default:
			GLLog(1, "%s: tex type %u - Unsupported\n", __FUNCTION__, sys_obj_type);
			return kIOReturnUnsupported;
	}
	rc = create_host_surface_for_texture(tx);
	if (rc != kIOReturnSuccess) {
		GLLog(1, "%s: create_host_surface_for_texture return %#x\n", __FUNCTION__, rc);
		goto clean1;
	}
	hostImage.sid = tx->surface_id;
	rc = ltx->xfer.prepare(m_provider);
	if (rc != kIOReturnSuccess) {
		GLLog(1, "%s: prepare_transfer_for_io return %#x\n", __FUNCTION__, rc);
		goto clean1;
	}
	extra.mem_gmr_id = ltx->xfer.gmr_id;
	switch (sys_obj_type) {
		case TEX_TYPE_AGPREF:
			copyBox.w = tx->width;
			copyBox.h = tx->height;
			copyBox.d = tx->depth;
			if (isIdValid(tx->yuv_shadow))
				hostImage.sid = tx->yuv_shadow;
			hostImage.face = 0U;
			hostImage.mipmap = 0U;
			rc = m_provider->surfaceDMA3DEx(&hostImage,
											SVGA3D_WRITE_HOST_VRAM,
											&copyBox,
											&extra,
											&ltx->xfer.fence);
			if (isIdValid(tx->yuv_shadow)) {
				rect.x = 0;
				rect.y = 0;
				rect.w = tx->width;
				rect.h = tx->height;
				m_provider->surfaceStretch(tx->yuv_shadow,
										   tx->surface_id,
										   SVGA3D_STRETCH_BLT_POINT,
										   &rect,
										   &rect);
			}
			break;
		case TEX_TYPE_STD:
		case TEX_TYPE_OOB:
			for (hostImage.face = 0U;
				 hostImage.face != tx->num_faces;
				 ++hostImage.face, headers += 12) {
				for (gld_th = headers, hostImage.mipmap = 0U;
					 hostImage.mipmap != tx->num_mipmaps;
					 ++hostImage.mipmap, ++gld_th) {
					if (!gld_th->pixels_in_client ||
						!isPagedOff(tx->sys_obj, hostImage.face, hostImage.mipmap))
						continue;
					copyBox.w = gld_th->width_bytes / tx->bytespp;
					copyBox.h = gld_th->height;
					copyBox.d = gld_th->depth;
					extra.mem_offset_in_gmr = gld_th->offset_in_client;
					extra.mem_pitch = gld_th->pitch;
					rc = m_provider->surfaceDMA3DEx(&hostImage,
													SVGA3D_WRITE_HOST_VRAM,
													&copyBox,
													&extra,
													&ltx->xfer.fence);
					if (rc != kIOReturnSuccess)
						goto clean2;
				}
			}
			mmap->release();
			for (hostImage.face = 0U; hostImage.face != tx->num_faces; ++hostImage.face)
				tx->sys_obj->pageon[hostImage.face] |= tx->sys_obj->pageoff[hostImage.face];
			break;
	}
	ltx->xfer.complete(m_provider); // ugh... to get rid of this need set up a garbage-collection mechanism
	return kIOReturnSuccess;

clean2:
	ltx->xfer.complete(m_provider);
clean1:
	if (mmap)
		mmap->release();
	return rc;
}

HIDDEN
IOReturn CLASS::tex_subimage_2d(VMsvga2TextureBuffer* tx,
								struct GLDTexSubImage2DStruc const* desc)
{
	IOReturn rc;
	SVGA3dCopyBox copyBox;
	SVGA3dSurfaceImageId hostImage;
	VMsvga2Accel::ExtraInfoEx extra;

	if (!tx || !desc || !tx->bytespp)
		return kIOReturnBadArgument;
	if (!tx->sys_obj)
		return kIOReturnNotReady;
	if (tx->surface_format == SVGA3D_FORMAT_INVALID) {
		GLLog(1, "%s: invalid surface format\n", __FUNCTION__);
		return kIOReturnNotReady;
	}
	bzero(&extra, sizeof extra);
	extra.mem_offset_in_gmr = desc->source_addr;
	extra.mem_pitch = desc->source_pitch;
	extra.mem_limit = 0xFFFFFFFFU;
	extra.suffix_flags = 2U;
	bzero(&copyBox, sizeof copyBox);
	copyBox.w = desc->width / tx->bytespp;
	copyBox.h = desc->height;
	copyBox.d = 1U;
	copyBox.x = desc->dest_x / tx->bytespp;
	copyBox.y = desc->dest_y;
	copyBox.z = desc->dest_z;
	hostImage.face = desc->face;
	hostImage.mipmap = desc->mipmap;
	rc = create_host_surface_for_texture(tx);
	if (rc != kIOReturnSuccess) {
		GLLog(1, "%s: create_host_surface_for_texture return %#x\n", __FUNCTION__, rc);
		return rc;
	}
	hostImage.sid = tx->surface_id;
	rc = m_command_buffer.xfer.prepare(m_provider);
	if (rc != kIOReturnSuccess) {
		GLLog(1, "%s: prepare_transfer_for_io return %#x\n", __FUNCTION__, rc);
		return rc;
	}
	extra.mem_gmr_id = m_command_buffer.xfer.gmr_id;
	return m_provider->surfaceDMA3DEx(&hostImage,
									  SVGA3D_WRITE_HOST_VRAM,
									  &copyBox,
									  &extra,
									  &m_command_buffer.xfer.fence);
}

HIDDEN
void CLASS::setup_drawbuffer_registers(uint32_t* p)
{
	uint32_t gart_pitch;

	/*
	 * First command sets gart_ptr and pitch of
	 *   render target color buffer
	 */
	p[0] = 0x7D8E0001U; /* 3DSTATE_BUF_INFO_CMD */
	p[1] = 0x03800000U; /* BUF_3D_ID_COLOR_BACK | BUF_3D_USE_FENCE */
	/*
	 * Second command sets gart_ptr and pitch of
	 *   render target depth buffer
	 */
	p[3] = 0x7D8E0001U; /* 3DSTATE_BUF_INFO_CMD */
	p[4] = 0x07800000U;	/* BUF_3D_ID_DEPTH | BUF_3D_USE_FENCE */
	/*
	 * DRAW_RECT is Intel's version of setViewPort()
	 */
	p[6]  = 0x7D800003U; /* 3DSTATE_DRAW_RECT_CMD */
	if (m_fbo[0]) {
		if (m_txs[17]) {
			get_tex_data(m_txs[17], &p[2], &gart_pitch, 0);
			p[1] |= (m_fbo[0]->txs[0].face << 8) | m_fbo[0]->txs[0].mipmap;
		} else
			p[2] = SVGA_ID_INVALID;
		if (m_txs[18]) {
			get_tex_data(m_txs[18], &p[5], &gart_pitch, 1);
			p[1] |= (m_fbo[0]->txs[1].face << 8) | m_fbo[0]->txs[1].mipmap;
		} else
			p[5] = SVGA_ID_INVALID;
		m_drawrect[0] = m_fbo[0]->txs[m_drawbuf_params[0]].x;
		m_drawrect[1] = m_fbo[0]->txs[m_drawbuf_params[0]].y;
		m_drawrect[2] = m_fbo[0]->width;
		m_drawrect[3] = m_fbo[0]->height;
	} else if (m_surface_client) {
		m_surface_client->getSurfacesForGL(&p[2], &p[5]);
		/*
		 * This viewPort setting is probably not right,
		 *   but it'll do for now.
		 */
		m_drawrect[0] = 0U;
		m_drawrect[1] = 0U;
		m_surface_client->getBoundsForGL(&m_drawrect[2], &m_drawrect[3], 0, 0);
	} else {
		p[2] = SVGA_ID_INVALID;
		p[5] = SVGA_ID_INVALID;
		bzero(&m_drawrect[0], sizeof m_drawrect);
	}
	memcpy(&p[7], &m_drawrect[0], sizeof m_drawrect);
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
			tx->sys_obj->vstate |= static_cast<uint8_t>(q);
			if (q & 0x10U)
				tx->xfer.md->setPurgeable(kIOMemoryPurgeableVolatile, &old_state);
			break;
		case TEX_TYPE_OOB:
			tx->sys_obj->vstate |= static_cast<uint8_t>(q);
			if (q & 0x10U) {
				tx->linked_agp->sys_obj->vstate |= 0x10U;
				tx->linked_agp->xfer.md->setPurgeable(kIOMemoryPurgeableVolatile, &old_state);
			}
			break;
		case TEX_TYPE_AGPREF:
			if (q & 0x10U) {
				tx->sys_obj->vstate |= 0x10U;
				tx->linked_agp->sys_obj->vstate |= 0x10U;
				tx->linked_agp->xfer.md->setPurgeable(kIOMemoryPurgeableVolatile, &old_state);
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
			tx->sys_obj->vstate &= ~0x11U;
			tx->xfer.md->setPurgeable(kIOMemoryPurgeableNonVolatile, &old_state);
			if (old_state == kIOMemoryPurgeableEmpty)
				tx->sys_obj->vstate |= 0x20U;
			break;
		case TEX_TYPE_OOB:
			tx->sys_obj->vstate &= ~0x11U;
			tx->linked_agp->sys_obj->vstate &= ~0x11U;
			tx->linked_agp->xfer.md->setPurgeable(kIOMemoryPurgeableNonVolatile, &old_state);
			if (old_state == kIOMemoryPurgeableEmpty) {
				tx->sys_obj->vstate |= 0x20U;
				tx->linked_agp->sys_obj->vstate |= 0x20U;
			}
			break;
		case TEX_TYPE_AGPREF:
			tx->sys_obj->vstate &= ~0x11U;
			tx->linked_agp->sys_obj->vstate &= ~0x11U;
			tx->linked_agp->xfer.md->setPurgeable(kIOMemoryPurgeableNonVolatile, &old_state);
			if (old_state == kIOMemoryPurgeableEmpty) {
				tx->sys_obj->vstate |= 0x20U;
				tx->linked_agp->sys_obj->vstate |= 0x20U;
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
	uint32_t i, tx_id, count;
	uint8_t face, mipmap;
	VMsvga2TextureBuffer* tx;
	FBODescriptor* fbo;

	if (m_fbo[0])
		for (i = 0U; i != 2U; ++i) {
			tx = m_txs[17U + i];
			if (!tx)
				continue;
			dirtyTexture(tx,
						 m_fbo[0]->txs[i].face,
						 m_fbo[0]->txs[i].mipmap);
			removeTextureFromStream(tx);
			if (__sync_fetch_and_add(&tx->sys_obj->refcount, -1) == 1)
				m_shared->delete_texture(tx);
			m_txs[17U + i] = 0;
		}
	else {
		m_fbo[0] = static_cast<typeof fbo>(IOMalloc(sizeof *fbo));
		if (!m_fbo[0]) {
			m_stream_error = 2;
			return;
		}
	}
	fbo = m_fbo[0];
	bzero(fbo, sizeof *fbo);
	fbo->width = info->p[1] & 0xFFFFU;
	fbo->height = info->p[1] >> 16;
	fbo->f0 = info->p[2];
#if LOGGING_LEVEL >= 2
	GLLog(3, "%s: w == %u, h == %u, f0 == %u\n", __FUNCTION__,
		  fbo->width, fbo->height, fbo->f0);
#endif
	count = info->p[3];
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
#if LOGGING_LEVEL >= 2
		GLLog(3, "%s: tx_id[%u] == %u, sid == %u\n", __FUNCTION__, i, tx_id, tx->surface_id);
#endif
		addTextureToStream(tx);
		if (tx->surface_format == SVGA3D_FORMAT_INVALID) {
			tx->surface_format = select_default_format(tx, i);
#if LOGGING_LEVEL >= 2
			GLLog(3, "%s: texture %u format %u\n", __FUNCTION__, tx->sys_obj->object_id, tx->surface_format);
#endif
		}
		if (info->f3 && !m_stream_error)
			get_texture(info, tx, 0);
		/*
		 * In case alloc_and_load_texture didn't find any data
		 *   to load, make the sure the host texture exists
		 *   so it can be used as a render target.
		 */
		if (!isIdValid(tx->surface_id))
			create_host_surface_for_texture(tx);
		face = sanitize_face(info->p[4U * i + 5U] >> 24);
		mipmap = sanitize_mipmap(info->p[4U * i + 5U] >> 18);
		dirtyTexture(tx, face, mipmap);
		fbo->txs[i].face = face;
		fbo->txs[i].mipmap = mipmap;
		fbo->txs[i].g0 = info->p[4U * i + 5U] & 0xFFFFU;
		fbo->txs[i].x = info->p[4U * i + 6U] & 0xFFFFU;
		fbo->txs[i].y = info->p[4U * i + 6U] >> 16;
		fbo->txs[i].g1 = info->p[4U * i + 7U];
#if LOGGING_LEVEL >= 2
		GLLog(3, "%s: %u - face == %u, mipmap == %u, g0 == %#x, x == %u, y == %u, g1 == %#x\n", __FUNCTION__,
			  i,
			  fbo->txs[i].face,
			  fbo->txs[i].mipmap,
			  fbo->txs[i].g0,
			  fbo->txs[i].x,
			  fbo->txs[i].y,
			  fbo->txs[i].g1);
#endif
		m_txs[17U + i] = tx;
		__sync_fetch_and_add(&tx->sys_obj->refcount, -0xFFFF);
	}
	bzero(&info->p[0], 12U * sizeof(uint32_t));
#if 0
	if (info->f3 && !m_command_buffer.xfer.gart_ptr)
		mapTransferToGART(&m_command_buffer.xfer);
#endif
}

HIDDEN
void CLASS::process_token_BindReadFBO(VendorGLStreamInfo* info)
{
	uint32_t i, tx_id, count;
	VMsvga2TextureBuffer* tx;
	FBODescriptor* fbo;

	if (m_fbo[1])
		for (i = 0U; i != 2U; ++i) {
			tx = m_txs[19U + i];
			if (!tx)
				continue;
			removeTextureFromStream(tx);
			if (__sync_fetch_and_add(&tx->sys_obj->refcount, -1) == 1)
				m_shared->delete_texture(tx);
			m_txs[19U + i] = 0;
		}
	else {
		m_fbo[1] = static_cast<typeof fbo>(IOMalloc(sizeof *fbo));
		if (!m_fbo[1]) {
			m_stream_error = 2;
			return;
		}
	}
	fbo = m_fbo[1];
	bzero(fbo, sizeof *fbo);
	fbo->width = info->p[1] & 0xFFFFU;
	fbo->height = info->p[1] >> 16;
	fbo->f0 = info->p[2];
#if LOGGING_LEVEL >= 2
	GLLog(3, "%s: w == %u, h == %u, f0 == %u\n", __FUNCTION__,
		  fbo->width, fbo->height, fbo->f0);
#endif
	count = info->p[3];
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
#if LOGGING_LEVEL >= 2
		GLLog(3, "%s: tx_id[%u] == %u, sid == %u\n", __FUNCTION__, i, tx_id, tx->surface_id);
#endif
		addTextureToStream(tx);
		if (tx->surface_format == SVGA3D_FORMAT_INVALID) {
			tx->surface_format = select_default_format(tx, i);
#if LOGGING_LEVEL >= 2
			GLLog(3, "%s: texture %u format %u\n", __FUNCTION__, tx->sys_obj->object_id, tx->surface_format);
#endif
		}
		if (info->f3 && !m_stream_error)
			get_texture(info, tx, 0);
		fbo->txs[i].face = sanitize_face(info->p[4U * i + 5U] >> 24);
		fbo->txs[i].mipmap = sanitize_mipmap(info->p[4U * i + 5U] >> 16);
		fbo->txs[i].g0 = info->p[4U * i + 5U] & 0xFFFFU;
		fbo->txs[i].x = info->p[4U * i + 6U] & 0xFFFFU;
		fbo->txs[i].y = info->p[4U * i + 6U] >> 16;
		fbo->txs[i].g1 = info->p[4U * i + 7U];
#if LOGGING_LEVEL >= 2
		GLLog(3, "%s: %u - face == %u, mipmap == %u, g0 == %#x, x == %u, y == %u, g1 == %#x\n", __FUNCTION__,
			  i,
			  fbo->txs[i].face,
			  fbo->txs[i].mipmap,
			  fbo->txs[i].g0,
			  fbo->txs[i].x,
			  fbo->txs[i].y,
			  fbo->txs[i].g1);
#endif
		m_txs[19U + i] = tx;
		__sync_fetch_and_add(&tx->sys_obj->refcount, -0xFFFF);
	}
	bzero(&info->p[0], 12U * sizeof(uint32_t));
#if 0
	if (info->f3 && !m_command_buffer.xfer.gart_ptr)
		mapTransferToGART(&m_command_buffer.xfer);
#endif
}

HIDDEN
void CLASS::process_token_UnbindDrawFBO(VendorGLStreamInfo* info)
{
	uint32_t i;
	VMsvga2TextureBuffer* tx;

	if (m_fbo[0]) {
		for (i = 0U; i != 2U; ++i) {	// loops over color & depth buffers
			tx = m_txs[17U + i];
			if (!tx)
				continue;
			dirtyTexture(tx,
						 m_fbo[0]->txs[i].face,
						 m_fbo[0]->txs[i].mipmap);
			removeTextureFromStream(tx);
			if (__sync_fetch_and_add(&tx->sys_obj->refcount, -1) == 1)
				m_shared->delete_texture(tx);
			m_txs[17U + i] = 0;
		}
		IOFree(m_fbo[0], sizeof(FBODescriptor));
		m_fbo[0] = 0;
	}
	info->p[0] = 0U;
}

HIDDEN
void CLASS::process_token_UnbindReadFBO(VendorGLStreamInfo* info)
{
	uint32_t i;
	VMsvga2TextureBuffer* tx;

	if (m_fbo[1]) {
		for (i = 0U; i != 2U; ++i) {	// loops over color & depth buffers
			tx = m_txs[19U + i];
			if (!tx)
				continue;
			removeTextureFromStream(tx);
			if (__sync_fetch_and_add(&tx->sys_obj->refcount, -1) == 1)
				m_shared->delete_texture(tx);
			m_txs[19U + i] = 0;
		}
		IOFree(m_fbo[1], sizeof(FBODescriptor));
		m_fbo[1] = 0;
	}
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
	switch (info->p[1]) {
		case 1U:
			m_drawbuf_params[0] = 0U;
			break;
		case 7U:
			m_drawbuf_params[0] = 2U;
			break;
		case 8U:
			m_drawbuf_params[0] = 3U;
			break;
		default:
			m_drawbuf_params[0] = 1U;
			break;
	}
#if 0
	/*
	 * Some test for a depth buffer
	 */
	if (!(byte ptr this->0x90 & 0x20U))
		return;
#endif
	switch (info->p[1]) {
		case 7U:
			m_drawbuf_params[1] = 5U;
			break;
		case 8U:
			m_drawbuf_params[1] = 6U;
			break;
		default:
			m_drawbuf_params[1] = 4U;
			break;
	}
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
		if (tx->surface_format == SVGA3D_FORMAT_INVALID) {
			tx->surface_format = decipher_format(bit_select(q[1], 7, 3), bit_select(q[1], 3, 4));
#if LOGGING_LEVEL >= 4
			GLLog(4, "%s: texture %u format %u\n", __FUNCTION__, tx->sys_obj->object_id, tx->surface_format);
#endif
		}
		get_texture(info, tx, true);
		__sync_fetch_and_add(&tx->sys_obj->refcount, -0xFFFF);
#if 0
		this->0x240 = 0U;
		this->0x244 = q[1];
		this->0x248 = q[2];
#endif
		write_tex_data(i, q, tx);
		m_txs[i] = tx;
		q += 3;
	}
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

/*
 * Note: Couldn't find any place in the GLD that calls process_token_VertexBuffer
 */
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
			--m_txs[16]->xfer.counter14;
		}
		++tx->xfer.counter14;
		m_txs[16] = tx;
	}
	if (!m_txs[16]->xfer.gart_ptr) {
		submit_midbuffer(info);
#if 0
		mapTransferToGART(&tx.xfer);
		if (!m_command_buffer.xfer.gart_ptr)
			mapTransferToGART(&m_command_buffer.xfer);
#endif
	}
#if 0
	info->p[0] = 0x4CU; // TBD... strange doesn't look like an opcode, maybe middle of an Intel CMD
	info->p[1] = tx->xfer.gart_ptr + 128U;	// Looks like this is a gart_ptr for a TEX_TYPE_VB texture with offset 0x80
#else
	info->p[0] = 0U;
	info->p[1] = 0U;
#endif
}

/*
 * Note: Couldn't find any place in the GLD that calls process_token_NoVertexBuffer
 */
HIDDEN
void CLASS::process_token_NoVertexBuffer(VendorGLStreamInfo* info)
{
	GLLog(1, "%s() Unsupported\n", __FUNCTION__);
	if (m_txs[16]) {
		m_txs[16]->sys_obj->stamps[0] = 0U /* m_provider->0x50 */;
		--m_txs[16]->xfer.counter14;
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
	if (m_fbo[0]) {
		m_drawbuf_params[0] = (info->p[1] != 17U ? info->p[1] : 1U);
		m_drawbuf_params[1] = (m_txs[18] ? 1U : m_drawbuf_params[0]);
		goto finish;
	}
	if (!m_surface_client)
		goto finish;
	switch (info->p[1]) {
		case 1U:
			m_drawbuf_params[0] = 0U;
			break;
		case 7U:
			m_drawbuf_params[0] = 2U;
			break;
		case 8U:
			m_drawbuf_params[0] = 3U;
			break;
		default:
			m_drawbuf_params[0] = 1U;
			break;
	}
	switch (info->p[1]) {
		case 7U:
			m_drawbuf_params[1] = 5U;
			break;
		case 8U:
			m_drawbuf_params[1] = 6U;
			break;
		default:
			m_drawbuf_params[1] = 4U;
			break;
	}
finish:
	setup_drawbuffer_registers(info->p);
}

HIDDEN
void CLASS::process_token_SetFence(VendorGLStreamInfo* info)
{
	uint32_t fence_num = info->p[1];
	if (fence_num * sizeof(GLDFence) >= m_fences_len) {
		info->cmd = 0U;
		info->ds_count_dwords = 0;
#if 0
		m_provider->0x50 -= info->f2;
#endif
		m_stream_error = 2;
		return;
	}
	info->p[0] = 0x02000000U;	// MI_FLUSH
	info->p[1] = 0x10800001U;	// MI_STORE_DATA_INDEX
	info->p[2] = 64U;
	info->p[3] = fence_num /* m_provider->0x50 */;
#if 0
	m_fences_ptr[fence_num].u = 0U /* m_provider->0x50++ */;
	m_fences_ptr[fence_num].v = 0U;
	++info->f2;
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
	if (tx->surface_format == SVGA3D_FORMAT_INVALID) {
		tx->surface_format = default_color_format(tx->bytespp, tx->sys_obj_type);
#if LOGGING_LEVEL >= 2
		GLLog(3, "%s: texture %u format %u\n", __FUNCTION__, tx->sys_obj->object_id, tx->surface_format);
#endif
	}
	get_texture(info, tx, true);
#if 0
	get_tex_data(tx, &dest_gart_addr, &dest_pitch, 0);
	height = tx->vram_tile_pages >> 32; /* should be texture height, how did it get here? */;
	face = helper->face;
	mipmap = helper->mipmap;
	if (face >= 6U)
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
	if (helper->face >= 6U)
		helper->face = 0U;
	if (helper->mipmap >= 16U)
		helper->mipmap = 0U;
	dirtyTexture(tx, helper->face, helper->mipmap);
	tex_subimage_2d(tx, helper);
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
	get_tex_data(tx, &gart_ptr, &gart_pitch, 0);
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
	info->p[0] = 0x7D800003U; /* 3DSTATE_DRAW_RECT_CMD */
	memcpy(&info->p[1], &m_drawrect[0], sizeof m_drawrect);
}

HIDDEN
void CLASS::process_token_AsyncReadDrawBuffer(VendorGLStreamInfo* info)
{
	GLLog(1, "%s() Unsupported\n", __FUNCTION__);
	discard_token_AsyncReadDrawBuffer(info);
}
