/*
 *  VMsvga2GLContext.cpp
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on August 21st 2009.
 *  Copyright 2009-2010 Zenith432. All rights reserved.
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

#include <IOKit/IOBufferMemoryDescriptor.h>
#include "ACMethods.h"
#include "VLog.h"
#include "VMsvga2Accel.h"
#include "VMsvga2Device.h"
#include "VMsvga2GLContext.h"
#include "VMsvga2Shared.h"
#include "VMsvga2Surface.h"
#include "UCTypes.h"

#define CLASS VMsvga2GLContext
#define super IOUserClient
OSDefineMetaClassAndStructors(VMsvga2GLContext, IOUserClient);

#if LOGGING_LEVEL >= 1
#define GLLog(log_level, ...) do { if (log_level <= m_log_level) VLog("IOGL: ", ##__VA_ARGS__); } while (false)
#else
#define GLLog(log_level, ...)
#endif

#define HIDDEN __attribute__((visibility("hidden")))

#define MAX_NUM_DECLS 12U

static
IOExternalMethod iofbFuncsCache[kIOVMGLNumMethods] =
{
// Note: methods from IONVGLContext
{0, reinterpret_cast<IOMethod>(&CLASS::set_surface), kIOUCScalarIScalarO, 4, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::set_swap_rect), kIOUCScalarIScalarO, 4, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::set_swap_interval), kIOUCScalarIScalarO, 2, 0},
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
{0, reinterpret_cast<IOMethod>(&CLASS::get_config), kIOUCScalarIScalarO, 0, 3},
#endif
{0, reinterpret_cast<IOMethod>(&CLASS::get_surface_size), kIOUCScalarIScalarO, 0, 4},
{0, reinterpret_cast<IOMethod>(&CLASS::get_surface_info), kIOUCScalarIScalarO, 1, 3},
{0, reinterpret_cast<IOMethod>(&CLASS::read_buffer), kIOUCScalarIStructI, 0, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::finish), kIOUCScalarIScalarO, 0, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::wait_for_stamp), kIOUCScalarIScalarO, 1, 0},
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
{0, reinterpret_cast<IOMethod>(&CLASS::new_texture), kIOUCStructIStructO, kIOUCVariableStructureSize, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::delete_texture), kIOUCScalarIScalarO, 1, 0},
#endif
{0, reinterpret_cast<IOMethod>(&CLASS::become_global_shared), kIOUCScalarIScalarO, 1, 0},
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
{0, reinterpret_cast<IOMethod>(&CLASS::page_off_texture), kIOUCScalarIStructI, 0, kIOUCVariableStructureSize},
#endif
{0, reinterpret_cast<IOMethod>(&CLASS::purge_texture), kIOUCScalarIScalarO, 1, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::set_surface_volatile_state), kIOUCScalarIScalarO, 1, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::set_surface_get_config_status), kIOUCStructIStructO, kIOUCVariableStructureSize, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::reclaim_resources), kIOUCScalarIScalarO, 0, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::get_data_buffer), kIOUCScalarIStructO, 0, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::set_stereo), kIOUCScalarIScalarO, 2, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::purge_accelerator), kIOUCScalarIScalarO, 1, 0},
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
{0, reinterpret_cast<IOMethod>(&CLASS::get_channel_memory), kIOUCScalarIStructO, 0, kIOUCVariableStructureSize},
#else
{0, reinterpret_cast<IOMethod>(&CLASS::submit_command_buffer), kIOUCScalarIStructO, 1, kIOUCVariableStructureSize},
#endif
// Note: Methods from NVGLContext
{0, reinterpret_cast<IOMethod>(&CLASS::get_query_buffer), kIOUCScalarIStructO, 1, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::get_notifiers), kIOUCScalarIScalarO, 0, 2},
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
{0, reinterpret_cast<IOMethod>(&CLASS::new_heap_object), kIOUCStructIStructO, kIOUCVariableStructureSize, kIOUCVariableStructureSize},
#endif
{0, reinterpret_cast<IOMethod>(&CLASS::kernel_printf), kIOUCScalarIStructI, 0, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::nv_rm_config_get), kIOUCStructIStructO, kIOUCVariableStructureSize, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::nv_rm_config_get_ex), kIOUCStructIStructO, kIOUCVariableStructureSize, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::nv_client_request), kIOUCStructIStructO, kIOUCVariableStructureSize, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::pageoff_surface_texture), kIOUCScalarIStructI, 0, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::get_data_buffer_with_offset), kIOUCScalarIStructO, 0, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::nv_rm_control), kIOUCStructIStructO, kIOUCVariableStructureSize, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::get_power_state), kIOUCScalarIScalarO, 0, 2},
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1060
{0, reinterpret_cast<IOMethod>(&CLASS::set_watchdog_timer), kIOUCScalarIScalarO, 1, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::GetHandleIndex), kIOUCScalarIScalarO, 0, 2},
{0, reinterpret_cast<IOMethod>(&CLASS::ForceTextureLargePages), kIOUCScalarIScalarO, 1, 0},
#endif
// Note: VM Methods
};

#pragma mark -
#pragma mark Struct Definitions
#pragma mark -

/*
 * Known:
 *   - data[4] is the size of the buffer (in dwords) starting
 *     right after the header.
 *   - data[5] is a flag word, bit 1 (mask 0x2) if on, tells
 *     the client to flush the surface attached to the context.
 *   - data[7] is really the first word of the pipeline
 *   - each pipeline command begins with a count of the number
 *     of dwords in that command.  A terminating token has a
 *     non-zero upper nibble (0x10000000 or 0x20000000).
 */
struct VendorCommandBufferHeader
{
	uint32_t data[4];
	uint32_t size_dwords;
	uint32_t flags;
	uint32_t stamp;
	uint32_t begin;
	uint32_t downstream[0];
};

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

struct VendorCommandDescriptor {
	uint32_t* next;		// residual downstream pointer, kernel
	uint8_t* ds_ptr;	// residual downstream pointer, gart
	uint32_t ds_count_dwords;	// residual downstream count
	uint32_t f2;
};

struct GLDTextureHeader {
	uint64_t pixels_in_client;	// 0
	uint32_t offset_in_client;	// 8
	uint32_t f0;				// 12
	uint16_t fake_pitch;		// 16 (calculated width * bytespp)
	uint16_t height;			// 18
	uint32_t f1;				// 20
	uint32_t f2;				// 24
	uint16_t pitch;				// 28 (the real pitch)
	uint16_t depth;				// 30
};

template<unsigned N>
union DefineRegion
{
	uint8_t b[sizeof(IOAccelDeviceRegion) + N * sizeof(IOAccelBounds)];
	IOAccelDeviceRegion r;
};

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
#pragma mark Global Functions
#pragma mark -

static inline
void lockAccel(VMsvga2Accel* accel)
{
}

static inline
void unlockAccel(VMsvga2Accel* accel)
{
}

static inline
uint32_t GMR_VRAM(void)
{
	return static_cast<uint32_t>(-2) /* SVGA_GMR_FRAMEBUFFER */;
}

static inline
bool isIdValid(uint32_t id)
{
	return static_cast<int>(id) >= 0;
}

void set_region(IOAccelDeviceRegion* rgn,
				uint32_t x,
				uint32_t y,
				uint32_t w,
				uint32_t h);

static
IOReturn analyze_vertex_format(uint32_t s2,
							   uint32_t s4,
							   SVGA3dVertexDecl* pArray, /* OUT */
							   size_t* num_decls) /* IN/OUT */
{
	size_t i = 0U, d3d_size = 0U;
	uint32_t tc;

	if (!num_decls)
		return kIOReturnBadArgument;
	if (i >= *num_decls)
		return kIOReturnSuccess;
	if (!pArray)
		return kIOReturnBadArgument;
	bzero(pArray, (*num_decls) * sizeof *pArray);
	pArray[i].identity.usage = SVGA3D_DECLUSAGE_POSITIONT;
	pArray[i].array.offset = static_cast<uint32_t>(d3d_size);
	switch ((s4 >> 6) & 7U) {
		case 1:	/* XYZ */
			pArray[i].identity.type = SVGA3D_DECLTYPE_FLOAT3;
			d3d_size += 3U * sizeof(float);
			break;
		case 2: /* XYZW */
			pArray[i].identity.type = SVGA3D_DECLTYPE_FLOAT4;
			d3d_size += 4U * sizeof(float);
			break;
		case 3: /* XY */
			pArray[i].identity.type = SVGA3D_DECLTYPE_FLOAT2;
			d3d_size += 2U * sizeof(float);
			break;
		case 4: /* XYW */
			return kIOReturnUnsupported;
		default:
			return kIOReturnError;
	}
	++i;
	if (i >= *num_decls)
		goto set_strides;
	if (s4 & (1U << 10)) {
		pArray[i].identity.usage = SVGA3D_DECLUSAGE_COLOR;
		pArray[i].identity.type = SVGA3D_DECLTYPE_D3DCOLOR;
		pArray[i].array.offset = static_cast<uint32_t>(d3d_size);
		d3d_size += sizeof(uint32_t);
		++i;
		if (i >= *num_decls)
			goto set_strides;
	}
	if (s4 & (1U << 11)) {
		pArray[i].identity.usage = SVGA3D_DECLUSAGE_COLOR;
		pArray[i].identity.usageIndex = 1U;
		pArray[i].identity.type = SVGA3D_DECLTYPE_D3DCOLOR;
		pArray[i].array.offset = static_cast<uint32_t>(d3d_size);
		d3d_size += sizeof(uint32_t);
		++i;
		if (i >= *num_decls)
			goto set_strides;
	}
	if (s4 & (1U << 12)) {
		pArray[i].identity.usage = SVGA3D_DECLUSAGE_PSIZE;
		pArray[i].identity.type = SVGA3D_DECLTYPE_FLOAT1;
		pArray[i].array.offset = static_cast<uint32_t>(d3d_size);
		d3d_size += sizeof(float);
		++i;
		if (i >= *num_decls)
			goto set_strides;
	}
	for (tc = 0U; tc != 8U; ++tc)
		switch ((s2 >> (tc * 4)) & 0xFU) {
			case 0: /* TEXCOORDFMT_2D */
				pArray[i].identity.usage = SVGA3D_DECLUSAGE_TEXCOORD;
				pArray[i].identity.usageIndex = tc;
				pArray[i].identity.type = SVGA3D_DECLTYPE_FLOAT2;
				pArray[i].array.offset = static_cast<uint32_t>(d3d_size);
				d3d_size += 2U * sizeof(float);
				++i;
				if (i >= *num_decls)
					goto set_strides;
				break;
			case 1: /* TEXCOORDFMT_3D */
				pArray[i].identity.usage = SVGA3D_DECLUSAGE_TEXCOORD;
				pArray[i].identity.usageIndex = tc;
				pArray[i].identity.type = SVGA3D_DECLTYPE_FLOAT3;
				pArray[i].array.offset = static_cast<uint32_t>(d3d_size);
				d3d_size += 3U * sizeof(float);
				++i;
				if (i >= *num_decls)
					goto set_strides;
				break;
			case 2: /* TEXCOORDFMT_4D */
				pArray[i].identity.usage = SVGA3D_DECLUSAGE_TEXCOORD;
				pArray[i].identity.usageIndex = tc;
#if 0
				pArray[i].identity.type = SVGA3D_DECLTYPE_FLOAT4;
#else
				pArray[i].identity.type = SVGA3D_DECLTYPE_FLOAT2;
#endif
				pArray[i].array.offset = static_cast<uint32_t>(d3d_size);
				d3d_size += 4U * sizeof(float);
				++i;
				if (i >= *num_decls)
					goto set_strides;
				break;
			case 3: /* TEXCOORDFMT_1D */
				pArray[i].identity.usage = SVGA3D_DECLUSAGE_TEXCOORD;
				pArray[i].identity.usageIndex = tc;
				pArray[i].identity.type = SVGA3D_DECLTYPE_FLOAT1;
				pArray[i].array.offset = static_cast<uint32_t>(d3d_size);
				d3d_size += sizeof(float);
				++i;
				if (i >= *num_decls)
					goto set_strides;
				break;
			case 4: /* TEXCOORDFMT_2D_16 */
				pArray[i].identity.usage = SVGA3D_DECLUSAGE_TEXCOORD;
				pArray[i].identity.usageIndex = tc;
				pArray[i].identity.type = SVGA3D_DECLTYPE_FLOAT16_2;
				pArray[i].array.offset = static_cast<uint32_t>(d3d_size);
				d3d_size += sizeof(uint32_t);
				++i;
				if (i >= *num_decls)
					goto set_strides;
				break;
			case 5: /* TEXCOORDFMT_4D_16 */
				pArray[i].identity.usage = SVGA3D_DECLUSAGE_TEXCOORD;
				pArray[i].identity.usageIndex = tc;
				pArray[i].identity.type = SVGA3D_DECLTYPE_FLOAT16_4;
				pArray[i].array.offset = static_cast<uint32_t>(d3d_size);
				d3d_size += 2U * sizeof(uint32_t);
				++i;
				if (i >= *num_decls)
					goto set_strides;
				break;
			case 15U:
				break;
			default:
				return kIOReturnError;
		}
set_strides:
	*num_decls = i;
	for (i = 0U; i != *num_decls; ++i)
		pArray[i].array.stride = static_cast<uint32_t>(d3d_size);
	return kIOReturnSuccess;
}

static
void make_polygon_index_array(uint16_t* arr, uint16_t base_index, uint16_t num_vertices)
{
	uint16_t v1, v2;
	if (!num_vertices)
		return;
	v1 = 0U;
	v2 = num_vertices - 1U;
	*arr++ = base_index + (v1++);
	while (v1 < v2) {
		*arr++ = base_index + (v1++);
		*arr++ = base_index + (v2--);
	}
	if (v1 == v2)
		*arr = base_index + v1;
}

static
void make_diag(float* matrix, float d0, float d1, float d2, float d3)
{
	bzero(matrix, 16U * sizeof(float));
	matrix[0]  = d0;
	matrix[5]  = d1;
	matrix[10] = d2;
	matrix[15] = d3;
}

#pragma mark -
#pragma mark Private Methods
#pragma mark -

HIDDEN
void CLASS::Init()
{
	m_context_id = SVGA_ID_INVALID;
	m_arrays.sid = SVGA_ID_INVALID;
	m_arrays.gmr_id = SVGA_ID_INVALID;
}

HIDDEN
void CLASS::Cleanup()
{
	if (m_shared) {
		m_shared->release();
		m_shared = 0;
	}
	if (m_surface_client) {
		m_surface_client->detachGL();
		m_surface_client->release();
		m_surface_client = 0;
	}
	if (m_gc) {
		m_gc->flushCollection();
		m_gc->release();
		m_gc = 0;
	}
	if (m_command_buffer.md) {
		m_command_buffer.md->release();
		m_command_buffer.md = 0;
	}
	if (m_context_buffer0.md) {
		m_context_buffer0.md->release();
		m_context_buffer0.md = 0;
	}
	if (m_context_buffer1.md) {
		m_context_buffer1.md->release();
		m_context_buffer1.md = 0;
	}
	if (m_type2) {
		m_type2->release();
		m_type2 = 0;
	}
	if (m_provider && isIdValid(m_context_id)) {
		m_provider->destroyContext(m_context_id);
		m_provider->FreeContextID(m_context_id);
		m_context_id = SVGA_ID_INVALID;
	}
	purge_arrays();
	if (m_float_cache) {
		IOFreeAligned(m_float_cache, 32U * sizeof(float));
		m_float_cache = 0;
	}
}

HIDDEN
bool CLASS::allocCommandBuffer(VMsvga2CommandBuffer* buffer, size_t size)
{
	IOBufferMemoryDescriptor* bmd;

	/*
	 * Intel915 ors an optional flag @ IOAccelerator+0x924
	 */
	bmd = IOBufferMemoryDescriptor::withOptions(kIOMemoryKernelUserShared |
												kIOMemoryPageable |
												kIODirectionOut,
												size,
												page_size);
	buffer->md = bmd;
	if (!bmd)
		return false;
	buffer->size = size;
	buffer->kernel_ptr = static_cast<VendorCommandBufferHeader*>(bmd->getBytesNoCopy());
	initCommandBufferHeader(buffer->kernel_ptr, size);
	return true;
}

HIDDEN
void CLASS::initCommandBufferHeader(VendorCommandBufferHeader* buffer_ptr, size_t size)
{
	bzero(buffer_ptr, sizeof *buffer_ptr);
	buffer_ptr->size_dwords = static_cast<uint32_t>((size - sizeof *buffer_ptr) / sizeof(uint32_t));
	buffer_ptr->stamp = 0;	// Intel915 puts (submitStamp - 1) here
}

HIDDEN
bool CLASS::allocAllContextBuffers()
{
	IOBufferMemoryDescriptor* bmd;
	VendorCommandBufferHeader* p;

	bmd = IOBufferMemoryDescriptor::withOptions(kIOMemoryKernelUserShared |
												kIOMemoryPageable |
												kIODirectionInOut,
												4096U,
												page_size);
	m_context_buffer0.md = bmd;
	if (!bmd)
		return false;
	m_context_buffer0.kernel_ptr = static_cast<VendorCommandBufferHeader*>(bmd->getBytesNoCopy());
	p = m_context_buffer0.kernel_ptr;
	bzero(p, sizeof *p);
	p->flags = 1;
	p->data[3] = 1007;

	/*
	 * Intel915 ors an optional flag @ IOAccelerator+0x924
	 */
	bmd = IOBufferMemoryDescriptor::withOptions(kIOMemoryKernelUserShared |
												kIOMemoryPageable |
												kIODirectionInOut,
												4096U,
												page_size);
	if (!bmd)
		return false; // TBD frees previous ones
	m_context_buffer1.md = bmd;
	m_context_buffer1.kernel_ptr = static_cast<VendorCommandBufferHeader*>(bmd->getBytesNoCopy());
	p = m_context_buffer1.kernel_ptr;
	bzero(p, sizeof *p);
	p->flags = 1;
	p->data[3] = 1007;
	// TBD: allocates another one like this
	return true;
}

HIDDEN
VMsvga2Surface* CLASS::findSurfaceforID(uint32_t surface_id)
{
	VMsvga2Accel::FindSurface fs;

	if (!m_provider)
		return 0;
	bzero(&fs, sizeof fs);
	fs.cgsSurfaceID = surface_id;
	m_provider->messageClients(kIOMessageFindSurface, &fs, sizeof fs);
	return OSDynamicCast(VMsvga2Surface, fs.client);
}

HIDDEN
IOReturn CLASS::get_status(uint32_t* status)
{
	/*
	 * crazy function that sets the status to 0 or 1
	 */
	*status = 1;
	return kIOReturnSuccess;
}

HIDDEN
uint32_t CLASS::processCommandBuffer(VendorCommandDescriptor* result)
{
	VendorGLStreamInfo cb_iter;
	uint32_t upper, commands_processed;
#if 0
	int i, lim;
#endif

#if 0
	/*
	 * Print header
	 */
	for (i = 0; i != 8; ++i)
		GLLog(2, "%s:   command_buffer_header[%d] == %#x\n", __FUNCTION__,
			  i, m_command_buffer.kernel_ptr->data[i]);
	lim = m_command_buffer.kernel_ptr->downstream[-1];
	for (i = 0; i != lim; ++i)
		GLLog(2, "%s:   command[%d] == %#x\n", __FUNCTION__,
			  i, m_command_buffer.kernel_ptr->downstream[i]);
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
		GLLog(2, "%s:   cmd %#x length %u\n", __FUNCTION__, upper, cb_iter.cmd & 0xFFFFFFU);
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
	GLLog(2, "%s:   processed %d stream commands, error == %#x\n", __FUNCTION__, commands_processed, m_stream_error);
	result->next = &m_command_buffer.kernel_ptr->downstream[0] + cb_iter.dso_bytes / sizeof(uint32_t);
	result->ds_ptr = m_command_buffer.gart_ptr + sizeof(VendorCommandBufferHeader) + cb_iter.dso_bytes;
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
		case 6:
			// 69D1
			tx->linked_agp->sys_obj->stamps[0] = stamp;
			--tx->linked_agp->ofs_0xE;
			break;
		case 1:
			// 6A05
			if (!tx->linked_agp)
				break;
			break;
		case 2:
			tx = tx->linked_agp;
			if (tx)
				goto redo;
			break;
		case 4:
			tx->sys_obj->stamps[0] = ebx;
			--tx->ofs_0xE;
			break;
	}
#endif
}

HIDDEN
void CLASS::addTextureToStream(VMsvga2TextureBuffer*)
{
	/*
	 * TBD
	 */
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
		m_command_buffer.pad2[7] =
		m_provider->submit_buffer(&m_command_buffer.kernel_ptr.downstream[0] + info->dso_bytes / sizeof(uint32_t),
								  m_command_buffer.gart_ptr + sizeof(VendorCommandBufferHeader) + info->dso_bytes,
								  wc - 2U,
								  __FILE__,
								  __LINE__);
#else
		m_command_buffer.pad2[7] =
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
#if 0
		alloc_and_load_texture(tx);
#endif
#if 0
		if (flag & !m_command_buffer.gart_ptr)
			mapTransferToGART(&m_command_buffer);
#endif
	}
#if 0
	otx = tx;
	if (otx->sys_obj_type == 2U)
		otx = otx->linked_agp;
	if (otx->0x6C) {
		/*
		 * TBD: some linked-list chaining
		 *   on m_provider->0x688, and prev-next pair
		 *   at otx->0x5C and otx->0x6C
		 */
	}
#endif
}

HIDDEN
void CLASS::dirtyTexture(VMsvga2TextureBuffer*, uint32_t, uint32_t)
{
}

HIDDEN
void CLASS::get_tex_data(VMsvga2TextureBuffer* tx, uint32_t* q0, uint32_t* q1)
{
	/*
	 * TBD
	 */
	*q0 = 0U;
	*q1 = 0U;
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
	width = ((d0 >> 10) & 0x7FFU) + 1U;
	height = ((d0 >> 21) & 0x7FFU) + 1U;
	depth = (d1 & 0xFFU) + 1U;
	pitch = (((d1 >> 21) + 1U) & 0x7FFU) * static_cast<uint32_t>(sizeof(uint32_t));
	mapsurf = (d0 >> 7) & 0x7U;
	mt = (d0 >> 3) & 0xFU;
	GLLog(3, "%s:   tex %u data1 width %u, height %u, MAPSURF %u, MT %u, UF %u, TS %u, TW %u\n", __FUNCTION__,
		  q[0], width, height, mapsurf, mt,
		  (d0 >> 2) & 1U,
		  (d0 >> 1) & 1U,
		  d0 & 1U);
	GLLog(3, "%s:   tex %u data2 pitch %u, cube-face-ena %#x, max-lod %#x, mip-layout %u, depth %u\n", __FUNCTION__,
		  q[0], pitch,
		  (d1 >> 15) & 0x3FU,
		  (d1 >> 9) & 0x3FU,
		  (d1 >> 8) & 1U,
		  depth);
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
void CLASS::alloc_and_load_texture(VMsvga2TextureBuffer* tx)
{
	IOReturn rc;
	uint32_t fence;
	IOMemoryDescriptor* md;
	SVGA3dCopyBox copyBox;
	VMsvga2Accel::ExtraInfoEx extra;
	IOByteCount bc;
	GLDTextureHeader gld_th;
	VMsvga2TextureBuffer* ltx;

	if (tx->surface_format == SVGA3D_FORMAT_INVALID) {
		GLLog(1, "%s: invalid surface format\n", __FUNCTION__);
		return;
	}
	bzero(&extra, sizeof extra);
	extra.mem_pitch = tx->pitch;
	extra.suffix_flags = 3U;
	ltx = tx;
	if (tx->sys_obj_type != 4 && tx->linked_agp) {
		ltx = tx->linked_agp;
		extra.mem_offset_in_gmr = static_cast<vm_offset_t>(ltx->agp_offset_in_page);
		extra.mem_limit = ltx->agp_size - extra.mem_offset_in_gmr;
	}
	md = ltx->md;
	if (!md) {
		GLLog(1, "%s: no memory descriptor\n", __FUNCTION__);
		return;
	}
	bzero(&copyBox, sizeof copyBox);
	copyBox.w = tx->width;
	copyBox.h = tx->height;
	copyBox.d = tx->depth;
	if (!isIdValid(tx->surface_id)) {
		tx->surface_id = m_provider->AllocSurfaceID();	// Note: doesn't fail
		tx->surface_changed = 1;
	}
	if (tx->surface_changed) {
		rc = m_provider->createTexture(tx->surface_id,
									   static_cast<SVGA3dSurfaceFormat>(tx->surface_format),
									   tx->width,
									   tx->height,
									   tx->depth);
		if (rc != kIOReturnSuccess) {
			m_provider->FreeSurfaceID(tx->surface_id);
			tx->surface_id = SVGA_ID_INVALID;
			GLLog(1, "%s: createSurface return %#x\n", __FUNCTION__, rc);
			return;
		}
		tx->surface_changed = 0;
	}
	extra.mem_gmr_id = m_provider->AllocGMRID();
	if (!isIdValid(extra.mem_gmr_id)) {
		GLLog(1, "%s: Out of GMR IDs\n", __FUNCTION__, rc);
		return;
	}
	md->prepare();
	if (ltx->sys_obj_type == 8U ||
		ltx->sys_obj_type == 9U) {	// TBD: is this needed for types 3 as well?
		bc = md->readBytes(0U, &gld_th, sizeof gld_th);
		if (bc >= sizeof gld_th) {
			extra.mem_offset_in_gmr = gld_th.offset_in_client;
			extra.mem_limit = md->getLength() - extra.mem_offset_in_gmr;
			if (gld_th.pitch) {
				extra.mem_pitch = gld_th.pitch;
				tx->pitch = gld_th.pitch;
			}
		} else {
			md->complete();
			m_provider->FreeGMRID(extra.mem_gmr_id);
			GLLog(1, "%s: Unable to read Texture Header for Type %u\n", __FUNCTION__, ltx->sys_obj_type);
			return;
		}
	}
	rc = m_provider->createGMR(extra.mem_gmr_id, md);
	if (rc != kIOReturnSuccess) {
		md->complete();
		m_provider->FreeGMRID(extra.mem_gmr_id);
		GLLog(1, "%s: createGMR return %#x\n", __FUNCTION__, rc);
		return;
	}
	GLLog(3, "%s: width == %u, height == %u, gmr_id == %u, pitch == %lu, offset_in_gmr == %#lx, limit == %#lx\n",
		  __FUNCTION__,
		  tx->width, tx->height, extra.mem_gmr_id,
		  static_cast<size_t>(extra.mem_pitch),
		  static_cast<size_t>(extra.mem_offset_in_gmr),
		  static_cast<size_t>(extra.mem_limit));
	rc = m_provider->surfaceDMA3DEx(tx->surface_id,
									SVGA3D_WRITE_HOST_VRAM,
									&copyBox,
									&extra,
									&fence);
	if (rc == kIOReturnSuccess)
		m_provider->SyncToFence(fence);	// ugh...
	m_provider->destroyGMR(extra.mem_gmr_id);
	md->complete();
	m_provider->FreeGMRID(extra.mem_gmr_id);
}

HIDDEN
IOReturn CLASS::bind_texture(uint32_t index, VMsvga2TextureBuffer* tx)
{
	SVGA3dTextureState tstate[2];
	float* f;
	if (!m_provider)
		return kIOReturnNotReady;
	if (index >= 8U || !tx)
		return kIOReturnBadArgument;
	/*
	 * Cache texture size
	 */
	f = m_float_cache + index * 4U;
	f[0] = 1.0F / static_cast<float>(tx->width);
	f[1] = 1.0F / static_cast<float>(tx->height);
	f[2] = 1.0F;
	f[3] = 1.0F;
	tstate[0].stage = index;
	tstate[0].name = SVGA3D_TS_BIND_TEXTURE;
	tstate[0].value = tx->surface_id;
	tstate[1].stage = index;
	tstate[1].name = SVGA3D_TS_COLOROP;
	tstate[1].value = SVGA3D_TC_SELECTARG1;
	GLLog(3, "%s:   binding texture %u at index %u\n", __FUNCTION__,
		  tx->sys_obj->object_id, index);
	return m_provider->setTextureState(m_context_id, 2U, &tstate[0]);
}

HIDDEN
void CLASS::setup_drawbuffer_registers(uint32_t* p)
{
	/*
	 * TBD
	 */
#if 0
	p[0] = 0x7D8E0001U; /* 3DSTATE_BUF_INFO_CMD */
	p[1] = 0U;
	p[2] = 0U;
	p[3] = 0x7D8E0001U; /* 3DSTATE_BUF_INFO_CMD */
	p[4] = 0U;
	p[5] = 0U;
	p[6] = 0x7D800003U; /* 3DSTATE_DRAW_RECT_CMD */
	p[7] = 0U;
	p[8] = 0U;
	p[9] = 0U;
	p[10] = 0U;
#else
	bzero(&p[0], 11U * sizeof(uint32_t));
	GLLog(3, "%s()\n", __FUNCTION__);
#endif
}

HIDDEN
IOReturn CLASS::alloc_arrays(size_t num_bytes)
{
	IOReturn rc;

	if (!m_provider)
		return kIOReturnNotReady;
	if (m_arrays.fence) {
		m_provider->SyncToFence(m_arrays.fence);
		m_arrays.fence = 0U;
	}
	num_bytes = (num_bytes + (PAGE_SIZE - 1U)) & -PAGE_SIZE;
	if (m_arrays.kernel_ptr) {
		if (m_arrays.size_bytes >= num_bytes)
			return kIOReturnSuccess;
		purge_arrays();
	}
	m_arrays.sid = m_provider->AllocSurfaceID();
	rc = m_provider->createSurface(m_arrays.sid,
								   SVGA3D_SURFACE_HINT_VERTEXBUFFER,
								   SVGA3D_BUFFER,
								   static_cast<uint32_t>(num_bytes),
								   1U);
	if (rc != kIOReturnSuccess) {
		m_provider->FreeSurfaceID(m_arrays.sid);
		m_arrays.sid = SVGA_ID_INVALID;
		return rc;
	}
	m_arrays.kernel_ptr = static_cast<uint8_t*>(m_provider->VRAMMalloc(num_bytes));
	if (!m_arrays.kernel_ptr) {
		purge_arrays();
		return kIOReturnNoMemory;
	}
	m_arrays.size_bytes = num_bytes;
	m_arrays.offset_in_gmr = m_provider->offsetInVRAM(m_arrays.kernel_ptr);
	m_arrays.gmr_id = GMR_VRAM();
	return kIOReturnSuccess;
}

HIDDEN
void CLASS::purge_arrays()
{
	if (!m_provider)
		return;
	if (m_arrays.fence) {
		m_provider->SyncToFence(m_arrays.fence);
		m_arrays.fence = 0U;
	}
	m_arrays.gmr_id = SVGA_ID_INVALID;
	if (m_arrays.kernel_ptr) {
		m_provider->VRAMFree(m_arrays.kernel_ptr);
		m_arrays.kernel_ptr = 0;
		m_arrays.size_bytes = 0U;
		m_arrays.offset_in_gmr = 0U;
	}
	if (isIdValid(m_arrays.sid)) {
		m_provider->destroySurface(m_arrays.sid);
		m_provider->FreeSurfaceID(m_arrays.sid);
		m_arrays.sid = SVGA_ID_INVALID;
	}
}

HIDDEN
IOReturn CLASS::upload_arrays(size_t num_bytes)
{
	SVGA3dCopyBox copyBox;
	VMsvga2Accel::ExtraInfoEx extra;

	if (!m_provider)
		return kIOReturnNotReady;
	if (num_bytes > m_arrays.size_bytes)
		return kIOReturnOverrun;
	extra.mem_gmr_id = m_arrays.gmr_id;
	extra.mem_offset_in_gmr = m_arrays.offset_in_gmr;
	extra.mem_pitch = 0U;
	extra.mem_limit = num_bytes;
	extra.suffix_flags = 3U;
	bzero(&copyBox, sizeof copyBox);
	copyBox.w = static_cast<uint32_t>(num_bytes);
	copyBox.h = 1U;
	copyBox.d = 1U;
	return m_provider->surfaceDMA3DEx(m_arrays.sid,
									  SVGA3D_WRITE_HOST_VRAM,
									  &copyBox,
									  &extra,
									  &m_arrays.fence);
}

HIDDEN
void CLASS::adjust_texture_coords(uint8_t* vertex_array,
								  size_t num_vertices,
								  void const* decls,
								  size_t num_decls)
{
	SVGA3dVertexDecl const* _decls = static_cast<SVGA3dVertexDecl const*>(decls);
	size_t i, j;

	for (i = 0U; i != num_decls; ++i)
		if (_decls[i].identity.usage == SVGA3D_DECLUSAGE_TEXCOORD &&
			_decls[i].identity.usageIndex < 8U) {
			float const* f = m_float_cache + _decls[i].identity.usageIndex * 4U;
			/*
			 * Barbarically assume at least FLOAT2 and adjust
			 */
			for (j = 0U; j != num_vertices; ++j) {
				float* q = reinterpret_cast<float*>(vertex_array + j * _decls[i].array.stride + _decls[i].array.offset);
				GLLog(3, "%s:   adjusting X == %d, Y == %d, Z == %d, W == %d\n", __FUNCTION__,
					  static_cast<int>(q[0]), static_cast<int>(q[1]), static_cast<int>(q[2]), static_cast<int>(q[3]));
				q[0] *= f[0];
				q[1] *= f[1];
			}
		}
	for (j = 0U; j != num_vertices; ++j) {
		float const* q = reinterpret_cast<float const*>(vertex_array + j * _decls[0].array.stride + _decls[0].array.offset);
		GLLog(3, "%s:   vertex coord X == %d, Y == %d, Z == %d, W == %d\n", __FUNCTION__,
			  static_cast<int>(q[0]), static_cast<int>(q[1]), static_cast<int>(q[2]), static_cast<int>(q[3]));
	}
}

#pragma mark -
#pragma mark Intel Pipeline Processor
#pragma mark -

HIDDEN
int CLASS::decipher_format(uint8_t mapsurf, uint8_t mt)
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

HIDDEN
int CLASS::translate_clear_mask(uint32_t mask)
{
	int out_mask = 0U;
	if (mask & 4U)
		out_mask |= SVGA3D_CLEAR_COLOR;
	if (mask & 2U)
		out_mask |= SVGA3D_CLEAR_DEPTH;
	if (mask & 1U)
		out_mask |= SVGA3D_CLEAR_STENCIL;
	return out_mask;
}

HIDDEN
void CLASS::ipp_discard_renderstate(void)
{
	m_intel_state.param_cache_mask = 0U;
}

HIDDEN
void CLASS::ip_prim3d_poly(uint32_t const* vertex_data, size_t num_vertex_dwords)
{
	size_t i, num_decls, num_vertices, vsize, isize;
	IOReturn rc;
	SVGA3dVertexDecl decls[MAX_NUM_DECLS];
	SVGA3dPrimitiveRange range;

	if (!num_vertex_dwords || !vertex_data)
		return; // nothing to do
	num_decls = sizeof decls / sizeof decls[0];
	rc = analyze_vertex_format(m_intel_state.imm_s[2],
							   m_intel_state.imm_s[4],
							   &decls[0],
							   &num_decls);
	if (rc != kIOReturnSuccess) {
		GLLog(1, "%s: analyze_vertex_format return %#x\n", __FUNCTION__, rc);
		return;
	}
	if (!num_decls || !decls[0].array.stride)
		return;	// nothing to do
#if 0
	GLLog(3, "%s:   num vertex decls == %lu\n", __FUNCTION__, num_decls);
#endif
	vsize = num_vertex_dwords * sizeof(uint32_t);
	num_vertices = vsize / decls[0].array.stride;
	if (num_vertices < 3U)
		return; // nothing to do
	isize = num_vertices * sizeof(uint16_t);
	rc = alloc_arrays(vsize + isize);
	if (rc != kIOReturnSuccess) {
		GLLog(1, "%s: alloc_arrays return %#x\n", __FUNCTION__, rc);
		return;
	}
	memcpy(m_arrays.kernel_ptr, vertex_data, vsize);
#if 0
	GLLog(3, "%s:   vertex_size == %u, num_vertices == %lu, copied %lu bytes\n",__FUNCTION__,
		  decls[0].array.stride, num_vertices, vsize);
#endif
	/*
	 * Prepare Index Array for a polygon
	 */
	make_polygon_index_array(reinterpret_cast<uint16_t*>(m_arrays.kernel_ptr + vsize),
							 0U,
							 static_cast<uint16_t>(num_vertices));
	adjust_texture_coords(m_arrays.kernel_ptr,
						  num_vertices,
						  &decls,
						  num_decls);
	rc = upload_arrays(vsize + isize);
	if (rc != kIOReturnSuccess) {
		GLLog(1, "%s: upload_arrays return %#x\n", __FUNCTION__, rc);
		return;
	}
	range.primType = SVGA3D_PRIMITIVE_TRIANGLESTRIP;
	range.primitiveCount = static_cast<uint32_t>(num_vertices - 2U);
	range.indexArray.surfaceId = m_arrays.sid;
	range.indexArray.offset = static_cast<uint32_t>(vsize);
	range.indexArray.stride = sizeof(uint16_t);
	range.indexWidth = sizeof(uint16_t);
	range.indexBias = 0U;
	for (i = 0U; i != num_decls; ++i) {
		decls[i].array.surfaceId = m_arrays.sid;
		decls[i].rangeHint.last = static_cast<uint32_t>(num_vertices);
	}
	rc = m_provider->drawPrimitives(m_context_id,
									static_cast<uint32_t>(num_decls),
									1U,
									&decls[0],
									&range);
	if (rc != kIOReturnSuccess)
		GLLog(1, "%s: drawPrimitives return %#x\n", __FUNCTION__, rc);
}

HIDDEN
void CLASS::ip_prim3d_direct(uint32_t prim_kind, uint32_t const* vertex_data, size_t num_vertex_dwords)
{
	size_t i, num_decls, num_vertices, vsize;
	IOReturn rc;
	SVGA3dVertexDecl decls[MAX_NUM_DECLS];
	SVGA3dPrimitiveRange range;

	if (!num_vertex_dwords || !vertex_data)
		return; // nothing to do
	num_decls = sizeof decls / sizeof decls[0];
	rc = analyze_vertex_format(m_intel_state.imm_s[2],
							   m_intel_state.imm_s[4],
							   &decls[0],
							   &num_decls);
	if (rc != kIOReturnSuccess) {
		GLLog(1, "%s: analyze_vertex_format return %#x\n", __FUNCTION__, rc);
		return;
	}
	if (!num_decls || !decls[0].array.stride)
		return;	// nothing to do
#if 0
	GLLog(3, "%s:   num vertex decls == %lu\n", __FUNCTION__, num_decls);
#endif
	vsize = num_vertex_dwords * sizeof(uint32_t);
	num_vertices = vsize / decls[0].array.stride;
	switch (prim_kind) {
		case 0: /* PRIM3D_TRILIST */
			if (num_vertices < 3U)
				return; // nothing to do
			range.primType = SVGA3D_PRIMITIVE_TRIANGLELIST;
			range.primitiveCount = static_cast<uint32_t>(num_vertices / 3U);
			break;
		case 1: /* PRIM3D_TRISTRIP */
			if (num_vertices < 3U)
				return; // nothing to do
			range.primType = SVGA3D_PRIMITIVE_TRIANGLESTRIP;
			range.primitiveCount = static_cast<uint32_t>(num_vertices - 2U);
			break;
		case 3: /* PRIM3D_TRIFAN */
			if (num_vertices < 3U)
				return; // nothing to do
			range.primType = SVGA3D_PRIMITIVE_TRIANGLEFAN;
			range.primitiveCount = static_cast<uint32_t>(num_vertices - 2U);
			break;
		case 5: /* PRIM3D_LINELIST */
			if (num_vertices < 2U)
				return; // nothing to do
			range.primType = SVGA3D_PRIMITIVE_LINELIST;
			range.primitiveCount = static_cast<uint32_t>(num_vertices >> 1);
			break;
		case 6: /* PRIM3D_LINESTRIP */
			if (num_vertices < 2U)
				return; // nothing to do
			range.primType = SVGA3D_PRIMITIVE_LINESTRIP;
			range.primitiveCount = static_cast<uint32_t>(num_vertices - 1U);
			break;
		case 8: /* PRIM3D_POINTLIST */
			if (!num_vertices)
				return; // nothing to do
			range.primType = SVGA3D_PRIMITIVE_POINTLIST;
			range.primitiveCount = static_cast<uint32_t>(num_vertices);
			break;
		default:
			return;	// error, shouldn't get here
	}
	rc = alloc_arrays(vsize);
	if (rc != kIOReturnSuccess) {
		GLLog(1, "%s: alloc_arrays return %#x\n", __FUNCTION__, rc);
		return;
	}
	memcpy(m_arrays.kernel_ptr, vertex_data, vsize);
#if 0
	GLLog(3, "%s:   vertex_size == %u, num_vertices == %lu, copied %lu bytes\n",__FUNCTION__,
		  decls[0].array.stride, num_vertices, vsize);
#endif
	adjust_texture_coords(m_arrays.kernel_ptr,
						  num_vertices,
						  &decls,
						  num_decls);
	rc = upload_arrays(vsize);
	if (rc != kIOReturnSuccess) {
		GLLog(1, "%s: upload_arrays return %#x\n", __FUNCTION__, rc);
		return;
	}
	range.indexArray.surfaceId = SVGA_ID_INVALID;
	range.indexArray.offset = 0U;
	range.indexArray.stride = sizeof(uint16_t);
	range.indexWidth = sizeof(uint16_t);
	range.indexBias = 0U;
	for (i = 0U; i != num_decls; ++i) {
		decls[i].array.surfaceId = m_arrays.sid;
		decls[i].rangeHint.last = static_cast<uint32_t>(num_vertices);
	}
	rc = m_provider->drawPrimitives(m_context_id,
									static_cast<uint32_t>(num_decls),
									1U,
									&decls[0],
									&range);
	if (rc != kIOReturnSuccess)
		GLLog(1, "%s: drawPrimitives return %#x\n", __FUNCTION__, rc);
}

HIDDEN
uint32_t CLASS::ip_prim3d(uint32_t* p)
{
	DefineRegion<1U> tmpRegion;
	uint32_t cmd = *p, skip = (cmd & 0xFFFFU) + 2U, primkind = (cmd >> 18) & 0x1FU;
	float const* pf;

	if (cmd & (1U << 23)) {
		GLLog(1, "%s: indirect primitive\n", __FUNCTION__);
		if (cmd & (1U << 17)) {
			skip = cmd & 0xFFFFU;
			if (!skip) {	// variable length, look for 0xFFFFU terminator
				uint16_t const* q = reinterpret_cast<typeof q>(&p[1]);
				for (skip = 0U; q[skip++] != 0xFFFFU;);
			}
			// skip == number of uint16s
			skip = (skip + 1U) / 2U + 1U;
		} else
			skip = 2U;
		return skip;	// Indirect Primitive, not handled
	}
	/*
	 * Direct Primitive
	 */
	GLLog(3, "%s:   primkind == %u, s2 == %#x, s4 == %#x\n", __FUNCTION__,
		  primkind, m_intel_state.imm_s[2], m_intel_state.imm_s[4]);
	switch (primkind) {
		case 0: /* PRIM3D_TRILIST */
		case 1: /* PRIM3D_TRISTRIP */
		case 3: /* PRIM3D_TRIFAN */
		case 5: /* PRIM3D_LINELIST */
		case 6: /* PRIM3D_LINESTRIP */
		case 8: /* PRIM3D_POINTLIST */
			ip_prim3d_direct(primkind, &p[1], skip -1U);
			break;
		case 4: /* PRIM3D_POLY */
			ip_prim3d_poly(&p[1], skip - 1U);
			break;
		case 10: /* PRIM3D_CLEAR_RECT */
			if (!(m_intel_state.clear.mask & 7U))
				break;	// nothing to do
			pf = reinterpret_cast<float const*>(p + 1);
			set_region(&tmpRegion.r,
					   static_cast<uint32_t>(pf[4]),
					   static_cast<uint32_t>(pf[5]),
					   static_cast<uint32_t>(pf[0] - pf[4]),
					   static_cast<uint32_t>(pf[1] - pf[5]));
#if 0
			GLLog(3, "%s:     issuing clear cmd, cid == %u, X == %d, Y == %d, W == %d, H == %d, mask == %u\n", __FUNCTION__,
				  m_context_id,
				  static_cast<int>(tmpRegion.r.bounds.x),
				  static_cast<int>(tmpRegion.r.bounds.y),
				  static_cast<int>(tmpRegion.r.bounds.w),
				  static_cast<int>(tmpRegion.r.bounds.h),
				  translate_clear_mask(m_intel_state.clear.mask));
#endif
			m_provider->clear(m_context_id,
							  SVGA3dClearFlag(translate_clear_mask(m_intel_state.clear.mask)),
							  &tmpRegion.r,
							  m_intel_state.clear.color,
							  m_intel_state.clear.depth,
							  m_intel_state.clear.stencil);
			break;
		case 2: /* PRIM3D_TRISTRIP_RVRSE */
		case 7: /* PRIM3D_RECTLIST */
		case 9: /* PRIM3D_DIB */
		case 13: /* PRIM3D_ZONE_INIT */
			GLLog(1, "%s:   primkind == %u unimplemented\n", __FUNCTION__, primkind);
			break;
	}
	return skip;
}

HIDDEN
uint32_t CLASS::ip_load_immediate(uint32_t* p)
{
	uint32_t i, cmd = p[0], skip = (cmd & 0xFU) + 2U, *limit = p + skip;
	uint16_t point_width;
	uint8_t line_width, flat_shade, cull_mode, residual;
	SVGA3dRenderState rs[3];

	for (i = 0U, ++p; i != 8U && p < limit; ++i)
		if (cmd & (1U << (4 + i)))
			switch (i) {
				case 2:
				case 3:
					m_intel_state.imm_s[i] = *p++;
					break;
				case 4:
					if ((m_intel_state.param_cache_mask & (1U << i)) &&
						m_intel_state.imm_s[i] == *p) {
						++p;
						break;
					}
					m_intel_state.imm_s[i] = *p++;
					m_intel_state.param_cache_mask |= (1U << i);
					point_width = (m_intel_state.imm_s[i] >> 23) & 0x1FFU;
					line_width = (m_intel_state.imm_s[i] >> 19) & 15U;
					flat_shade = (m_intel_state.imm_s[i] >> 15) & 15U;
					cull_mode = (m_intel_state.imm_s[i] >> 13) & 3U;
					residual = m_intel_state.imm_s[i] & 0x3FU;
					GLLog(3, "%s: imm4 - PW %u, LW %u, FS %#x, CM %u, residual %#x", __FUNCTION__,
						  point_width, line_width, flat_shade, cull_mode, residual);
					rs[0].state = SVGA3D_RS_POINTSIZE;
					rs[0].floatValue = static_cast<float>(point_width);
					rs[1].state = SVGA3D_RS_SHADEMODE;
					rs[1].uintValue = (flat_shade & 1U) ? SVGA3D_SHADEMODE_FLAT : SVGA3D_SHADEMODE_SMOOTH;
					rs[2].state = SVGA3D_RS_CULLMODE;
					rs[2].uintValue = cull_mode;
					m_provider->setRenderState(m_context_id, 3U, &rs[0]);
					break;
				default:
					m_intel_state.imm_s[i] = *p++;
					GLLog(3, "%s: imm%u - %#x\n", __FUNCTION__, i, m_intel_state.imm_s[i]);
					break;
			}
	return skip;
}

HIDDEN
uint32_t CLASS::ip_clear_params(uint32_t* p)
{
	uint32_t skip = (p[0] & 0xFFFFU) + 2U;
	if (skip < 7U)
		return skip;
	m_intel_state.clear.mask = p[1];
	/*
	 * Note: this is for CLEAR_RECT (guess).  ZONE_INIT... who knows.
	 */
	m_intel_state.clear.color = p[4];
	m_intel_state.clear.depth = *reinterpret_cast<float const*>(p + 5);
	m_intel_state.clear.stencil = p[6];
	return skip;
}

HIDDEN
uint32_t CLASS::decode_mi(uint32_t* p)
{
	uint32_t cmd = *p, skip = 0U;
	switch ((cmd >> 23) & 63U) {
		case  0U: /* MI_NOOP */
		case  2U: /* MI_USER_INTERRUPT */
		case  3U: /* MI_WAIT_FOR_EVENT */
		case  4U: /* MI_FLUSH */
		case  7U: /* MI_REPORT_HEAD */
		case  8U: /* MI_ARB_ON_OFF */
		case 10U: /* MI_BATCH_BUFFER_END */
			skip = 1U;
			break;
		case 20U: /* MI_DISPLAY_BUFFER_INFO */
		case 34U: /* MI_LOAD_REGISTER_IMM */
		case 36U: /* MI_STORE_REGISTER_MEM */
		case 48U: /* MI_BATCH_BUFFER */
			skip = 3U;
			break;
		case 17U: /* MI_OVERLAY_FLIP */
		case 18U: /* MI_LOAD_SCAN_LINES_INCL */
		case 19U: /* MI_LOAD_SCAN_LINES_EXCL */
		case 24U: /* MI_SET_CONTEXT */
		case 49U: /* MI_BATCH_BUFFER_START */
			skip = 2U;
			break;
		case 32U: /* MI_STORE_DATA_IMM */
		case 33U: /* MI_STORE_DATA_INDEX */
			skip = (cmd & 0x3FU) + 2U;
			break;
	}
	if (!skip) {
		GLLog(1, "%s:   Unknown cmd %#x\n", __FUNCTION__, cmd);
		skip = 1U;
	}
	return skip;
}

HIDDEN
uint32_t CLASS::decode_2d(uint32_t* p)
{
	uint32_t cmd = *p, skip = 0U;
	switch ((cmd >> 22) & 0x7FU) {
		case    1U: /* XY_SETUP_BLT */
		case    3U: /* XY_SETUP_CLIP_BLT */
		case 0x11U: /* XY_SETUP_MONO_PATTERN_SL_BLT */
		case 0x24U: /* XY_PIXEL_BLT */
		case 0x25U: /* XY_SCANLINES_BLT */
		case 0x26U: /* Y_TEXT_BLT */
		case 0x31U: /* XY_TEXT_IMMEDIATE_BLT */
		case 0x40U: /* COLOR_BLT */
		case 0x50U: /* XY_COLOR_BLT */
		case 0x51U: /* XY_PAT_BLT */
		case 0x52U: /* XY_MONO_PAT_BLT */
		case 0x53U: /* XY_SRC_COPY_BLT */
		case 0x54U: /* XY_MONO_SRC_COPY_BLT */
		case 0x55U: /* XY_FULL_BLT */
		case 0x56U: /* XY_FULL_MONO_SRC_BLT */
		case 0x57U: /* XY_FULL_MONO_PATTERN_BLT */
		case 0x58U: /* XY_FULL_MONO_PATTERN_MONO_SRC_BLT */
		case 0x59U: /* XY_MONO_PAT_FIXED_BLT */
		case 0x71U: /* XY_MONO_SRC_COPY_IMMEDIATE_BLT */
		case 0x72U: /* XY_PAT_BLT_IMMEDIATE */
		case 0x75U: /* XY_FULL_MONO_SRC_IMMEDIATE_PATTERN_BLT */
		case 0x76U: /* XY_PAT_CHROMA_BLT */
		case 0x77U: /* XY_PAT_CHROMA_BLT_IMMEDIATE */
			skip = (cmd & 0xFFU) + 2U;
			break;
		case 0x43U: /* SRC_COPY_BLT */
			skip = (cmd & 0xFFU) + 2U;
			GLLog(3, "%s:   SRC_COPY_BLT\n", __FUNCTION__);
			break;
	}
	if (!skip) {
		GLLog(1, "%s:   Unknown cmd %#x\n", __FUNCTION__, cmd);
		skip = 1U;
	}
	return skip;
}

HIDDEN
uint32_t CLASS::decode_3d(uint32_t* p)
{
	uint32_t cmd = *p, skip = 0U;
	switch ((cmd >> 24) & 0x1FU) {
		case 0x06U: /* 3DSTATE_AA_CMD */
		case 0x07U: /* 3DSTATE_RASTER_RULES_CMD */
		case 0x08U: /* 3DSTATE_BACKFACE_STENCIL_OPS */
		case 0x09U: /* 3DSTATE_BACKFACE_STENCIL_MASKS */
		case 0x0BU: /* 3DSTATE_INDEPENDENT_ALPHA_BLEND_CMD */
		case 0x15U: /* 3DSTATE_FOG_COLOR_CMD */
			skip = 1U;
			break;
		case 0x0CU: /* 3DSTATE_MODES_5_CMD */
			m_intel_state.modes5 = cmd & 0xFFFFFFU;
			GLLog(3, "%s:   modes5 FRC %u, FTC %u\n", __FUNCTION__,
				  (m_intel_state.modes5 >> 18) & 1U,
				  (m_intel_state.modes5 >> 16) & 1U);
			skip = 1U;
			break;
		case 0x0DU: /* 3DSTATE_MODES_4_CMD */
			m_intel_state.modes4 = cmd & 0xFFFFFFU;
			GLLog(3, "%s:   modes4 ELO %u, LO %u, EST %u, ST %u, ESW %u, SW %u\n", __FUNCTION__,
				  (m_intel_state.modes4 >> 23) & 1U,
				  (m_intel_state.modes4 >> 18) & 15U,
				  (m_intel_state.modes4 >> 17) & 1U,
				  (m_intel_state.modes4 >> 8) & 255U,
				  (m_intel_state.modes4 >> 16) & 1U,
				  m_intel_state.modes4 & 255U);
			skip = 1U;
			break;
		case 0x1CU:
			switch ((cmd >> 16) & 0xFFU) {
				case 0x80U: /* 3DSTATE_SCISSOR_ENABLE */
				case 0x88U: /* 3DSTATE_DEPTH_SUBRECT_DISABLE */
					skip = 1U;
					break;
			}
			break;
		case 0x1DU:
			switch ((cmd >> 16) & 0xFFU) {
				case 0x04U: /* 3DSTATE_LOAD_STATE_IMMEDIATE_1 */
					skip = ip_load_immediate(p);
					break;
				case 0x9CU: /* 3DSTATE_CLEAR_PARAMETERS */
					skip = ip_clear_params(p);
					break;
				case 0x00U: /* 3DSTATE_MAP_STATE */
				case 0x01U: /* 3DSTATE_SAMPLER_STATE */
				case 0x05U: /* 3DSTATE_PIXEL_SHADER_PROGRAM */
				case 0x06U: /* 3DSTATE_PIXEL_SHADER_CONSTANTS */
				case 0x80U: /* 3DSTATE_DRAW_RECT_CMD */
				case 0x81U: /* 3DSTATE_SCISSOR_RECT_0_CMD */
				case 0x83U: /* 3DSTATE_STIPPLE */
				case 0x85U: /* 3DSTATE_DST_BUF_VARS_CMD */
				case 0x88U: /* 3DSTATE_CONST_BLEND_COLOR_CMD */
				case 0x89U: /* 3DSTATE_FOG_MODE_CMD */
				case 0x8EU: /* 3DSTATE_BUF_INFO_CMD */
				case 0x97U: /* 3DSTATE_DEPTH_OFFSET_SCALE */
					skip = (cmd & 0xFFFFU) + 2U;
					break;
			}
			break;
		case 0x1FU: /* PRIM3D */
			skip = ip_prim3d(p);
			break;
	}
	if (!skip) {
		GLLog(1, "%s:   Unknown cmd %#x\n", __FUNCTION__, cmd);
		skip = 1U;
	}
	return skip;
}

HIDDEN
uint32_t CLASS::submit_buffer(uint32_t* kernel_buffer_ptr, uint32_t size_dwords)
{
	uint32_t *p, *limit, cmd, skip;
	GLLog(3, "%s:   offset %d, size %u [in dwords]\n", __FUNCTION__,
		  static_cast<int>(kernel_buffer_ptr - &m_command_buffer.kernel_ptr->downstream[0]),
		  size_dwords);
	p = kernel_buffer_ptr;
	limit = p + size_dwords;
	for (; p < limit; p += skip) {
		cmd = *p;
		skip = 0U;
#if 0
		if (opcode_u <= 0xAU || (opcode_u >= 0x20U && opcode_u <= 0x3DU)) {
			GLLog(3, "%s:   Apple cmd in stream %#x\n", __FUNCTION__, cmd);
			skip = cmd & 0xFFFFU;
			if (!skip)
				++skip;
			continue;
		}
#endif
		switch (cmd >> 29) {
			case 0U:
				skip = decode_mi(p);
				break;
			case 2U:
				skip = decode_2d(p);
				break;
			case 3U:
				skip = decode_3d(p);
				break;
		}
		if (!skip) {
			GLLog(1, "%s:   Unknown cmd %#x\n", __FUNCTION__, cmd);
			skip = 1U;
		}
	}
	return 0U;
}

#pragma mark -
#pragma mark IOUserClient Methods
#pragma mark -

IOExternalMethod* CLASS::getTargetAndMethodForIndex(IOService** targetP, UInt32 index)
{
	if (index >= kIOVMGLNumMethods)
		GLLog(2, "%s(target_out, %u)\n", __FUNCTION__, static_cast<unsigned>(index));
	if (!targetP || index >= kIOVMGLNumMethods)
		return 0;
#if 0
	if (index >= kIOVMGLNumMethods) {
		if (m_provider)
			*targetP = m_provider;
		else
			return 0;
	} else
		*targetP = this;
#else
	*targetP = this;
#endif
	return &iofbFuncsCache[index];
}

IOReturn CLASS::clientClose()
{
	GLLog(2, "%s\n", __FUNCTION__);
	Cleanup();
	if (!terminate(0))
		IOLog("%s: terminate failed\n", __FUNCTION__);
	m_owning_task = 0;
	m_provider = 0;
	return kIOReturnSuccess;
}

IOReturn CLASS::clientMemoryForType(UInt32 type, IOOptionBits* options, IOMemoryDescriptor** memory)
{
	VendorCommandBufferHeader* p;
	IOMemoryDescriptor* md;
	IOBufferMemoryDescriptor* bmd;
	size_t d;
	VendorCommandDescriptor result;
	uint32_t pcbRet;

	GLLog(2, "%s(%u, options_out, memory_out)\n", __FUNCTION__, static_cast<unsigned>(type));
	if (type > 4U || !options || !memory)
		return kIOReturnBadArgument;
	if (m_stream_error) {
		*options = m_stream_error;
		*memory = 0;
		return kIOReturnBadArgument;
	}
	/*
	 * Notes:
	 *   Cases 1 & 2 are called from GLD gldCreateContext
	 *   Cases 0 & 4 are called from submit_command_buffer
	 *   Case 3 is not used
	 */
	switch (type) {
		case 0:
			/*
			 * TBD: Huge amount of code to process previous command buffer
			 *   A0B7-AB58
			 */
			if (!m_shared)
				return kIOReturnNotReady;
			m_shared->lockShared();
			pcbRet = processCommandBuffer(&result);
			if (!m_stream_error)
				submit_buffer(result.next, result.ds_count_dwords);
#if 0
			discardCommandBuffer();
#endif
			for (d = 0U; d != 16U; ++d)
				if (m_txs[d]) {
					removeTextureFromStream(m_txs[d]);
					if (__sync_fetch_and_add(&m_txs[d]->sys_obj->refcount, -1) == 1)
						m_shared->delete_texture(m_txs[d]);
					m_txs[d] = 0;
				}
			m_shared->unlockShared();
			if ((pcbRet & 2U) && m_surface_client)
				m_surface_client->touchRenderTarget();
			lockAccel(m_provider);
			/*
			 * AB58: reinitialize buffer
			 */
			md = m_command_buffer.md;
			if (!md)
				return kIOReturnNoResources;
			md->retain();
			*options = 0;
			*memory = md;
			p = m_command_buffer.kernel_ptr;
			initCommandBufferHeader(p, m_command_buffer.size);
#if 0
			p->flags = 0;				// TBD: from this+0x88
#else
			if (!m_stream_error)
				p->flags = pcbRet;
#endif
			p->downstream[-1] = 1;
			p->downstream[0] = 1U << 24;// terminating token
			p->stamp = 0;				// TBD: from this+0x7C
			unlockAccel(m_provider);
			// sleepForSwapCompleteNoLock(this+0x84)
			return kIOReturnSuccess;
		case 1:
			lockAccel(m_provider);
			md = m_context_buffer0.md;
			if (!md) {
				unlockAccel(m_provider);
				return kIOReturnNoResources;
			}
			md->retain();
			*options = 0;
			*memory = md;
			p = m_context_buffer0.kernel_ptr;
			bzero(p, sizeof *p);
			p->flags = 1;
			p->data[3] = 1007;
			unlockAccel(m_provider);
			return kIOReturnSuccess;
		case 2:
			if (!m_type2) {
				m_type2_len = page_size;
				bmd = IOBufferMemoryDescriptor::withOptions(0x10023U,
															m_type2_len,
															page_size);
				m_type2 = bmd;
				if (!bmd)
					return kIOReturnNoResources;
				m_type2_ptr = static_cast<VendorCommandBufferHeader*>(bmd->getBytesNoCopy());
			} else {
				d = 2U * m_type2_len;
				bmd = IOBufferMemoryDescriptor::withOptions(0x10023U,
															d,
															page_size);
				if (!bmd)
					return kIOReturnNoResources;
				p = static_cast<VendorCommandBufferHeader*>(bmd->getBytesNoCopy());
				memcpy(p, m_type2_ptr, m_type2_len);
				m_type2->release();
				m_type2 = bmd;
				m_type2_len = d;
				m_type2_ptr = p;
			}
			m_type2->retain();
			*options = 0;
			*memory = m_type2;
			return kIOReturnSuccess;
		case 3:
#if 0
			md = m_provider->offset 0xB4;
			if (!md)
				return kIOReturnNoResources;
			md->retain();
			*options = 0;
			*memory = md;
			return kIOReturnSuccess;
#endif
			break;
		case 4:
			lockAccel(m_provider);
			md = m_command_buffer.md;
			if (!md) {
				unlockAccel(m_provider);
				return kIOReturnNoResources;
			}
			md->retain();
			*options = 0;
			*memory = md;
			p = m_command_buffer.kernel_ptr;
			initCommandBufferHeader(p, m_command_buffer.size);
			p->flags = 0;
			p->downstream[-1] = 1;
			p->downstream[0] = 1U << 24;// terminating token
			p->stamp = 0;				// TBD: from this+0x7C
			unlockAccel(m_provider);
			return kIOReturnSuccess;
	}
	/*
	 * Note: Intel GMA 950 defaults to returning kIOReturnBadArgument
	 */
	return super::clientMemoryForType(type, options, memory);
}

IOReturn CLASS::connectClient(IOUserClient* client)
{
	VMsvga2Device* d;
	VMsvga2Shared* s;
	GLLog(2, "%s(%p), name == %s\n", __FUNCTION__, client, client ? client->getName() : "NULL");
	d = OSDynamicCast(VMsvga2Device, client);
	if (!d)
		return kIOReturnError;
	if (d->getOwningTask() != m_owning_task)
		return kIOReturnNotPermitted;
	s = d->getShared();
	if (!s)
		return kIOReturnNotReady;
	s->retain();
	if (m_shared)
		m_shared->release();
	m_shared = s;
	return kIOReturnSuccess;
}

#if 0
/*
 * Note: IONVGLContext has an override on this method
 *   In OS 10.5 it redirects function number 17 to get_data_buffer()
 *   In OS 10.6 it redirects function number 13 to get_data_buffer()
 *     Sets structureOutputSize for get_data_buffer() to 12
 */
IOReturn CLASS::externalMethod(uint32_t selector, IOExternalMethodArguments* arguments, IOExternalMethodDispatch* dispatch, OSObject* target, void* reference)
{
	return super::externalMethod(selector, arguments, dispatch, target, reference);
}
#endif

bool CLASS::start(IOService* provider)
{
	m_provider = OSDynamicCast(VMsvga2Accel, provider);
	if (!m_provider)
		return false;
	if (!super::start(provider))
		return false;
	m_log_level = imax(m_provider->getLogLevelGLD(), m_provider->getLogLevelAC());
	m_mem_type = 4U;
	m_gc = OSSet::withCapacity(2U);
	if (!m_gc) {
		GLLog(1, "%s: OSSet::withCapacity failed\n", __FUNCTION__);
		super::stop(provider);
		return false;
	}
	m_float_cache = static_cast<float*>(IOMallocAligned(32U * sizeof(float), 16U));	// SSE-ready
	if (!m_float_cache) {
		GLLog(1, "%s: IOMallocAligned failed\n", __FUNCTION__);
		goto bad;
	}
	m_context_id = m_provider->AllocContextID();	// Note: doesn't fail
	if (m_provider->createContext(m_context_id) != kIOReturnSuccess) {
		GLLog(1, "%s: Unable to create SVGA3D context\n", __FUNCTION__);
		m_provider->FreeContextID(m_context_id);
		m_context_id = SVGA_ID_INVALID;
		goto bad;
	}
	// TBD getVRAMDescriptors
	if (!allocAllContextBuffers()) {
		GLLog(1, "%s: allocAllContextBuffers failed\n", __FUNCTION__);
		goto bad;
	}
	if (!allocCommandBuffer(&m_command_buffer, 0x10000U)) {
		GLLog(1, "%s: allocCommandBuffer failed\n", __FUNCTION__);
		goto bad;
	}
	m_command_buffer.kernel_ptr->flags = 2U;
	m_command_buffer.kernel_ptr->downstream[1] = 1U << 24;
	return true;

bad:
	Cleanup();
	super::stop(provider);
	return false;
}

bool CLASS::initWithTask(task_t owningTask, void* securityToken, UInt32 type)
{
	m_log_level = LOGGING_LEVEL;
	if (!super::initWithTask(owningTask, securityToken, type))
		return false;
	m_owning_task = owningTask;
	Init();
	return true;
}

HIDDEN
CLASS* CLASS::withTask(task_t owningTask, void* securityToken, uint32_t type)
{
	CLASS* inst;

	inst = new CLASS;

	if (inst && !inst->initWithTask(owningTask, securityToken, type))
	{
		inst->release();
		inst = 0;
	}

	return (inst);
}

#pragma mark -
#pragma mark IONVGLContext Methods
#pragma mark -

HIDDEN
IOReturn CLASS::set_surface(uintptr_t surface_id, uintptr_t /* eIOGLContextModeBits */ context_mode_bits, uintptr_t c3, uintptr_t c4)
{
	VMsvga2Surface* surface_client;
	IOReturn rc;
	GLLog(2, "%s(%#lx, %#lx, %lu, %lu)\n", __FUNCTION__, surface_id, context_mode_bits, c3, c4);
	ipp_discard_renderstate();
	if (!surface_id) {
		if (m_surface_client) {
			m_surface_client->detachGL();
			m_surface_client->release();
			m_surface_client = 0;
		}
		return kIOReturnSuccess;
	}
	surface_client = findSurfaceforID(static_cast<uint32_t>(surface_id));
	if (!surface_client)
		return kIOReturnNotFound;
	if (surface_client == m_surface_client)
		return m_surface_client->resizeGL();
	surface_client->retain();
	if (m_surface_client) {
		m_surface_client->detachGL();
		m_surface_client->release();
		m_surface_client = 0;
	}
	rc = surface_client->attachGL(m_context_id, static_cast<uint32_t>(context_mode_bits));
	if (rc != kIOReturnSuccess) {
		surface_client->release();
		return rc;
	}
	m_surface_client = surface_client;
	GLLog(2, "%s:   surface_client %p\n", __FUNCTION__, m_surface_client);
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::set_swap_rect(intptr_t x, intptr_t y, intptr_t w, intptr_t h)
{
	GLLog(2, "%s(%ld, %ld, %ld, %ld)\n", __FUNCTION__, x, y, w, h);
#if 0
	IOAccelBounds this->0x94;
	this->0x94.x = x;
	this->0x94.y = y;
	this->0x94.w = w;
	this->0x94.h = h;
#endif
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::set_swap_interval(intptr_t c1, intptr_t c2)
{
	GLLog(2, "%s(%ld, %ld)\n", __FUNCTION__, c1, c2);
#if 0
	IOAccelSize this->0x9C;
	this->0x9C.w = c1;
	this->0x9C.h = c2;
#endif
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::get_config(uint32_t* c1, uint32_t* c2, uint32_t* c3)
{
	uint32_t const vram_size = m_provider->getVRAMSize();

	*c1 = 0;				// same as c1 in VMsvga2Device::get_config, used by GLD to discern Intel 915/965/Ironlake(HD)
	*c2 = vram_size;		// same as c3 in VMsvga2Device::get_config, total VRAM size
	*c3 = vram_size;		// same as c4 in VMsvga2Device::get_config, total memory available for textures (no accounting by VMsvga2)
	GLLog(2, "%s(*%u, *%u, *%u)\n", __FUNCTION__, *c1, *c2, *c3);
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::get_surface_size(uint32_t* inner_width, uint32_t* inner_height, uint32_t* outer_width, uint32_t* outer_height)
{
	if (!m_surface_client)
		return kIOReturnError;
	m_surface_client->getBoundsForGL(inner_width, inner_height, outer_width, outer_height);
	GLLog(2, "%s(*%u, *%u, *%u, *%u)\n", __FUNCTION__, *inner_width, *inner_height, *outer_width, *outer_height);
#if 0
	ebx = m_surface_client->0xE70.w;	// sign extended
	ecx = m_surface_client->0xE70.h;	// sign extended
	edi = m_surface_client->0xE6C.w;	// zero extended
	eax = m_surface_client->0xE6C.h;	// zero extended
	if (ebx != edi || ecx != eax) {
		esi = ebx;
		ebx = ecx;
		ecx = edi;
		edx = eax;
	} else {
		ebx = this->0x1A8;
		eax = m_surface_client->0xE14;	// associated Intel915TextureBuffer*
		ecx = eax->width;
		edx = eax->height;	// zero extended
		while (ebx > 0) {
			if (ecx >= 2) ecx /= 2;
			if (edx >= 2) edx /= 2;
			--ebx;
		}
		esi = ecx;
		ebx = edx;
	}
	*inner_width  = esi;
	*inner_height = ebx;
	*outer_width  = ecx;
	*outer_height = edx;
#endif
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::get_surface_info(uintptr_t surface_id, uint32_t* mode_bits, uint32_t* width, uint32_t* height)
{
	VMsvga2Surface* surface_client;
	uint32_t inner_width, inner_height, outer_width, outer_height;

	lockAccel(m_provider);
	if (!surface_id)
		goto bad_exit;
	surface_client = findSurfaceforID(static_cast<uint32_t>(surface_id));
	if (!surface_client)
		goto bad_exit;
	*mode_bits = static_cast<uint32_t>(surface_client->getOriginalModeBits());
#if 0
	uint32_t some_mask = surface_client->0x10F0;
	if (some_mask & 8U)
		*mode_bits |= 0x200U;
	else if (some_mask & 4U)
		*mode_bits |= 0x100U;
#endif
	surface_client->getBoundsForGL(&inner_width,
								   &inner_height,
								   &outer_width,
								   &outer_height);
#if 0
	if (inner_width != outer_width || inner_height != outer_height) {
		*width  = inner_width;
		*height = inner_height;
	} else {
		*width  = surface_client->0xE14->width;
		*height = surface_client->0xE14->height;	// zero extended
	}
#else
	*width  = outer_width;
	*height = outer_height;
#endif
	unlockAccel(m_provider);
	GLLog(2, "%s(%lu, *%u, *%u, *%u)\n", __FUNCTION__, surface_id, *mode_bits, *width, *height);
	return kIOReturnSuccess;

bad_exit:
	*mode_bits = 0U;
	*width = 0U;
	*height = 0U;
	unlockAccel(m_provider);
	return kIOReturnBadArgument;
}

HIDDEN
IOReturn CLASS::read_buffer(struct sIOGLContextReadBufferData const* struct_in, size_t struct_in_size)
{
	GLLog(2, "%s(struct_in, %lu)\n", __FUNCTION__, struct_in_size);
	if (struct_in_size < sizeof *struct_in)
		return kIOReturnBadArgument;
	for (int i = 0; i < 8; ++i)
		GLLog(2, "%s:  struct_in[%d] == %#x\n", __FUNCTION__, i, struct_in->data[i]);
	/*
	 * TBD
	 */
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::finish()
{
	GLLog(2, "%s()\n", __FUNCTION__);
	/*
	 * TBD
	 */
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::wait_for_stamp(uintptr_t c1)
{
	GLLog(2, "%s(%lu)\n", __FUNCTION__, c1);
	/*
	 * TBD
	 */
	return kIOReturnSuccess;
}

#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
HIDDEN
IOReturn CLASS::new_texture(struct VendorNewTextureDataStruc const* struct_in,
							struct sIONewTextureReturnData* struct_out,
							size_t struct_in_size,
							size_t* struct_out_size)
{
	GLLog(2, "%s(struct_in, struct_out, %lu, %lu)\n", __FUNCTION__, struct_in_size, *struct_out_size);
	bzero(struct_out, *struct_out_size);
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::delete_texture(uintptr_t c1)
{
	GLLog(2, "%s(%lu)\n", __FUNCTION__, c1);
	return kIOReturnSuccess;
}
#endif

HIDDEN
IOReturn CLASS::become_global_shared(uintptr_t c1)
{
	GLLog(2, "%s(%lu)\n", __FUNCTION__, c1);
	return kIOReturnUnsupported;	// Not implemented in Intel GMA 950
}

#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
HIDDEN
IOReturn CLASS::page_off_texture(struct sIODevicePageoffTexture const* struct_in, size_t struct_in_size)
{
	GLLog(2, "%s(struct_in, %lu)\n", __FUNCTION__, struct_in_size);
	return kIOReturnSuccess;
}
#endif

HIDDEN
IOReturn CLASS::purge_texture(uintptr_t c1)
{
	GLLog(2, "%s(%lu)\n", __FUNCTION__, c1);
	/*
	 * TBD
	 */
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::set_surface_volatile_state(uintptr_t /* eSurfaceVolatileState */ new_state)
{
	GLLog(2, "%s(%lu)\n", __FUNCTION__, new_state);
#if 0
	lockAccel(m_provider);
	this->0xC0 = new_state;
	if (surface_client)
		surface_client->set_volatile_state(new_state);
	unlockAccel(m_provider);
#endif
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::set_surface_get_config_status(struct sIOGLContextSetSurfaceData const* struct_in,
											  struct sIOGLContextGetConfigStatus* struct_out,
											  size_t struct_in_size,
											  size_t* struct_out_size)
{
	VMsvga2Surface* surface_client;
#if LOGGING_LEVEL >= 1
	int i;
#endif
	IOReturn rc;
	eIOAccelSurfaceScaleBits scale_options;
	IOAccelSurfaceScaling scale;	// var_44

	GLLog(2, "%s(struct_in, struct_out, %lu, %lu)\n", __FUNCTION__, struct_in_size, *struct_out_size);
	if (struct_in_size < sizeof *struct_in ||
		*struct_out_size < sizeof *struct_out)
		return kIOReturnBadArgument;
#if LOGGING_LEVEL >= 1
	if (m_log_level >= 3)
		for (i = 0; i < 10; ++i)
			GLLog(3, "%s:   struct_in[%d] == %#x\n", __FUNCTION__, i, reinterpret_cast<uint32_t const*>(struct_in)[i]);
#endif
	*struct_out_size = sizeof *struct_out;
	bzero(struct_out, sizeof *struct_out);
	if (struct_in->surface_id &&
		struct_in->surface_mode != 0xFFFFFFFFU) {
		lockAccel(m_provider);
		surface_client = findSurfaceforID(struct_in->surface_id);
		unlockAccel(m_provider);
		if (surface_client &&
			((surface_client->getOriginalModeBits() & 15) != static_cast<int>(struct_in->surface_mode & 15U)))
			return kIOReturnError;
	}
	rc = set_surface(struct_in->surface_id,
					 struct_in->context_mode_bits,
					 struct_in->dr_options_hi,
					 struct_in->dr_options_lo);
	if (rc != kIOReturnSuccess)
		return rc;
	if (!m_surface_client)	// Moved
		return kIOReturnError;
	if (struct_in->set_scale) {
		if (!(struct_in->scale_options & 1U))
			return kIOReturnUnsupported;
		bzero(&scale, sizeof scale);
		scale.buffer.x = 0;
		scale.buffer.y = 0;
		scale.buffer.w = static_cast<int16_t>(struct_in->scale_width);
		scale.buffer.h = static_cast<int16_t>(struct_in->scale_height);
		scale.source.w = scale.buffer.w;
		scale.source.h = scale.buffer.h;
		lockAccel(m_provider);	// Moved
#if 0
		while (1) {
			if (!m_surface_client) {
				unlockAccel(m_provider);
				return kIOReturnUnsupported;
			}
			if (m_surface_client->(byte @0x10CA) == 0)
				break;
			IOLockSleep(/* lock @m_provider+0x960 */, m_surface_client, 1);
		}
#endif
		scale_options = static_cast<eIOAccelSurfaceScaleBits>(((struct_in->scale_options >> 2) & 1U) | (struct_in->scale_options & 2U));
		/*
		 * Note: Intel GMA 950 actually calls set_scaling.  set_scale there is a wrapper
		 *   for set_scaling that validates the struct size and locks the accelerator
		 */
		rc = m_surface_client->set_scale(scale_options, &scale, sizeof scale);
		unlockAccel(m_provider);
		if (rc != kIOReturnSuccess)
			return rc;
	}
	rc = set_surface_volatile_state(struct_in->volatile_state);
	if (rc != kIOReturnSuccess)
		return rc;
	rc = get_config(&struct_out->config[0],
					&struct_out->config[1],
					&struct_out->config[2]);
	if (rc != kIOReturnSuccess)
		return rc;
	rc = get_surface_size(&struct_out->inner_width,
						  &struct_out->inner_height,
						  &struct_out->outer_width,
						  &struct_out->outer_height);
	if (rc != kIOReturnSuccess)
		return rc;
	rc = get_status(&struct_out->status);
	if (rc != kIOReturnSuccess)
		return rc;
	struct_out->surface_mode_bits = static_cast<uint32_t>(m_surface_client->getOriginalModeBits());
	for (i = 0; i < 10; ++i)
		GLLog(2, "%s:   struct_out[%d] == %#x\n", __FUNCTION__, i, reinterpret_cast<uint32_t const*>(struct_out)[i]);
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::reclaim_resources()
{
	GLLog(2, "%s()\n", __FUNCTION__);
#if 0
	lockAccel(m_provider);
	dword ptr m_provider->0x628 = 0x10000;
	unlockAccel(m_provider);
#endif
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::get_data_buffer(struct sIOGLContextGetDataBuffer* struct_out, size_t* struct_out_size)
{
	GLLog(2, "%s(struct_out, %lu)\n", __FUNCTION__, *struct_out_size);
#if 0
	if (*struct_out_size < sizeof *struct_out)
		return kIOReturnBadArgument;
	*struct_out_size = sizeof *struct_out;
	bzero(struct_out, *struct_out_size);
	return kIOReturnSuccess;
#else
	return kIOReturnError;	// Not implemented in Intel GMA 950
#endif
}

HIDDEN
IOReturn CLASS::set_stereo(uintptr_t c1, uintptr_t c2)
{
	GLLog(2, "%s(%lu, %lu)\n", __FUNCTION__, c1, c2);
#if 0
	IOReturn rc;

	lockAccel(m_provider);
	dword ptr m_provider->0x148[c2] = c1;
	if (dword ptr m_provider->0xFC[9 * c2] && (c1 & 2U))
		c1 &= 1U;
	rc = m_provider->setupStereo(c2, c1);
	unlockAccel(m_provider);
	return rc;
#endif
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::purge_accelerator(uintptr_t c1)
{
	GLLog(2, "%s(%lu)\n", __FUNCTION__, c1);
#if 0
	return m_provider->purgeAccelerator(c1);
#endif
	return kIOReturnSuccess;
}

#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
HIDDEN
IOReturn CLASS::get_channel_memory(struct sIODeviceChannelMemoryData* struct_out, size_t* struct_out_size)
{
	GLLog(2, "%s(struct_out, %lu)\n", __FUNCTION__, *struct_out_size);
	bzero(struct_out, *struct_out_size);
	return kIOReturnSuccess;
}
#else
HIDDEN
IOReturn CLASS::submit_command_buffer(uintptr_t do_get_data,
									  struct sIOGLGetCommandBuffer* struct_out,
									  size_t* struct_out_size)
{
	IOReturn rc;
	IOOptionBits options; // var_1c
	IOMemoryDescriptor* md; // var_20
	IOMemoryMap* mm; // var_3c
	struct sIOGLContextGetDataBuffer db;
	size_t dbsize;

	GLLog(2, "%s(%lu, struct_out, %lu)\n", __FUNCTION__, do_get_data, *struct_out_size);
	if (*struct_out_size < sizeof *struct_out)
		return kIOReturnBadArgument;
	options = 0;
	md = 0;
	*struct_out_size = sizeof *struct_out;
	bzero(struct_out, sizeof *struct_out);
	rc = clientMemoryForType(m_mem_type, &options, &md);
	m_mem_type = 0U;
	if (rc != kIOReturnSuccess)
		return rc;
	mm = md->createMappingInTask(m_owning_task,
								 0,
								 kIOMapAnywhere);
	md->release();
	if (!mm)
		return kIOReturnNoMemory;
	struct_out->addr[0] = mm->getAddress();
	struct_out->len[0] = static_cast<uint32_t>(mm->getLength());
	if (do_get_data != 0) {
		// increment dword @offset 0x8ec on the provider
		dbsize = sizeof db;
		rc = get_data_buffer(&db, &dbsize);
		if (rc != kIOReturnSuccess) {
			mm->release();	// Added
			return rc;
		}
		struct_out->addr[1] = db.addr;
		struct_out->len[1] = db.len;
	}
	// IOLockLock on provider @+0xC4
	m_gc->setObject(mm);
	// IOUnlockLock on provider+0xC4
	mm->release();
	return rc;
}
#endif

#pragma mark -
#pragma mark NVGLContext Methods
#pragma mark -

HIDDEN
IOReturn CLASS::get_query_buffer(uintptr_t c1, struct sIOGLGetQueryBuffer* struct_out, size_t* struct_out_size)
{
	GLLog(2, "%s(%lu, struct_out, %lu)\n", __FUNCTION__, c1, *struct_out_size);
	return kIOReturnUnsupported;
}

HIDDEN
IOReturn CLASS::get_notifiers(uint32_t*, uint32_t*)
{
	GLLog(2, "%s(out1, out2)\n", __FUNCTION__);
	return kIOReturnUnsupported;
}

#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
HIDDEN
IOReturn CLASS::new_heap_object(struct sNVGLNewHeapObjectData const* struct_in,
								struct sIONewTextureReturnData* struct_out,
								size_t struct_in_size,
								size_t* struct_out_size)
{
	GLLog(2, "%s(struct_in, struct_out, %lu, %lu)\n", __FUNCTION__, struct_in_size, *struct_out_size);
	return kIOReturnUnsupported;
}
#endif

HIDDEN
IOReturn CLASS::kernel_printf(char const* str, size_t str_size)
{
	GLLog(2, "%s: %.80s\n", __FUNCTION__, str);
	return kIOReturnUnsupported;
}

HIDDEN
IOReturn CLASS::nv_rm_config_get(uint32_t const* struct_in,
								 uint32_t* struct_out,
								 size_t struct_in_size,
								 size_t* struct_out_size)
{
	GLLog(2, "%s(struct_in, struct_out, %lu, %lu)\n", __FUNCTION__, struct_in_size, *struct_out_size);
	return kIOReturnUnsupported;
}

HIDDEN
IOReturn CLASS::nv_rm_config_get_ex(uint32_t const* struct_in,
									uint32_t* struct_out,
									size_t struct_in_size,
									size_t* struct_out_size)
{
	GLLog(2, "%s(struct_in, struct_out, %lu, %lu)\n", __FUNCTION__, struct_in_size, *struct_out_size);
	return kIOReturnUnsupported;
}

HIDDEN
IOReturn CLASS::nv_client_request(void const* struct_in,
								  void* struct_out,
								  size_t struct_in_size,
								  size_t* struct_out_size)
{
	GLLog(2, "%s(struct_in, struct_out, %lu, %lu)\n", __FUNCTION__, struct_in_size, *struct_out_size);
	return kIOReturnUnsupported;
}

HIDDEN
IOReturn CLASS::pageoff_surface_texture(struct sNVGLContextPageoffSurfaceTextureData const* struct_in, size_t struct_in_size)
{
	GLLog(2, "%s(struct_in, %lu)\n", __FUNCTION__, struct_in_size);
	return kIOReturnUnsupported;
}

HIDDEN
IOReturn CLASS::get_data_buffer_with_offset(struct sIOGLContextGetDataBuffer* struct_out, size_t* struct_out_size)
{
	GLLog(2, "%s(struct_out, %lu)\n", __FUNCTION__, *struct_out_size);
	return kIOReturnUnsupported;
}

HIDDEN
IOReturn CLASS::nv_rm_control(uint32_t const* struct_in,
							  uint32_t* struct_out,
							  size_t struct_in_size,
							  size_t* struct_out_size)
{
	GLLog(2, "%s(struct_in, struct_out, %lu, %lu)\n", __FUNCTION__, struct_in_size, *struct_out_size);
	return kIOReturnUnsupported;
}

HIDDEN
IOReturn CLASS::get_power_state(uint32_t*, uint32_t*)
{
	GLLog(2, "%s(out1, out2)\n", __FUNCTION__);
	return kIOReturnUnsupported;
}

#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1060
HIDDEN
IOReturn CLASS::set_watchdog_timer(uintptr_t c1)
{
	GLLog(2, "%s(%lu)\n", __FUNCTION__, c1);
	return kIOReturnUnsupported;
}

HIDDEN
IOReturn CLASS::GetHandleIndex(uint32_t*, uint32_t*)
{
	GLLog(2, "%s(out1, out2)\n", __FUNCTION__);
	return kIOReturnUnsupported;
}

HIDDEN
IOReturn CLASS::ForceTextureLargePages(uintptr_t c1)
{
	GLLog(2, "%s(%lu)\n", __FUNCTION__, c1);
	return kIOReturnUnsupported;
}
#endif

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
		case 8U:
			tx->sys_obj->f1 |= static_cast<uint8_t>(q);
			if (q & 0x10U)
				tx->md->setPurgeable(kIOMemoryPurgeableVolatile, &old_state);
			break;
		case 9U:
			tx->sys_obj->f1 |= static_cast<uint8_t>(q);
			if (q & 0x10U) {
				tx->linked_agp->sys_obj->f1 |= 0x10U;
				tx->linked_agp->md->setPurgeable(kIOMemoryPurgeableVolatile, &old_state);
			}
			break;
		case 6U:
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
		case 8U:
			tx->sys_obj->f1 &= ~0x11U;
			tx->md->setPurgeable(kIOMemoryPurgeableNonVolatile, &old_state);
			if (old_state == kIOMemoryPurgeableEmpty)
				tx->sys_obj->f1 |= 0x20U;
			break;
		case 9U:
			tx->sys_obj->f1 &= ~0x11U;
			tx->linked_agp->sys_obj->f1 &= ~0x11U;
			tx->linked_agp->md->setPurgeable(kIOMemoryPurgeableNonVolatile, &old_state);
			if (old_state == kIOMemoryPurgeableEmpty) {
				tx->sys_obj->f1 |= 0x20U;
				tx->linked_agp->sys_obj->f1 |= 0x20U;
			}
			break;
		case 6U:
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
	/*
	 * TBD
	 */
	GLLog(1, "%s: Unimplemented\n", __FUNCTION__);
}

HIDDEN
void CLASS::process_token_BindReadFBO(VendorGLStreamInfo* info)
{
	/*
	 * TBD
	 */
	GLLog(1, "%s: Unimplemented\n", __FUNCTION__);
}

HIDDEN
void CLASS::process_token_UnbindDrawFBO(VendorGLStreamInfo* info)
{
	/*
	 * TBD
	 */
	GLLog(2, "%s: Unimplemented\n", __FUNCTION__);
	info->p[0] = 0U;
}

HIDDEN
void CLASS::process_token_UnbindReadFBO(VendorGLStreamInfo* info)
{
	/*
	 * TBD
	 */
	GLLog(2, "%s: Unimplemented\n", __FUNCTION__);
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
	m_tx_vb = tx;
}

HIDDEN
void CLASS::discard_token_NoVertexBuffer(VendorGLStreamInfo*)
{
	m_tx_vb = 0;
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
	if (__sync_fetch_and_add(&tx->sys_obj->refcount, -1) == -1)
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
	if (__sync_fetch_and_add(&tx->sys_obj->refcount, -1) == -1)
		m_shared->delete_texture(tx);
}

HIDDEN
void CLASS::discard_token_AsyncReadDrawBuffer(VendorGLStreamInfo* info)
{
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
		get_texture(info, tx, true);
		__sync_fetch_and_add(&tx->sys_obj->refcount, -0xFFFF);
#if 0
		this->0x240 = 0U;
		this->0x244 = q[1];
		this->0x248 = q[2];
#endif
		write_tex_data(i, q, tx);
		alloc_and_load_texture(tx);	// Added
		m_txs[i] = tx;
		bind_texture(i, tx);	// Added
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

HIDDEN
void CLASS::process_token_VertexBuffer(VendorGLStreamInfo* info)
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
	if (tx != m_tx_vb) {
		if (m_tx_vb) {
			m_tx_vb->sys_obj->stamps[0] = 0U /* m_provider->0x50 */;
			--m_tx_vb->counter14;
		}
		++tx->counter14;
		m_tx_vb = tx;
	}
	if (!m_tx_vb->provider_flag) {
		submit_midbuffer(info);
#if 0
		mapTransferToGART(tx);
		if (!m_command_buffer.gart_ptr)
			mapTransferToGART(&m_command_buffer);
#endif
	}
#if 0
	info->p[0] = 0x4CU; // TBD... strange MI_NOOP for Intel with Len 1
	info->p[1] = tx->provider_flag + 128U;
#else
	info->p[0] = 0U;
	info->p[1] = 0U;
	GLLog(3, "%s()\n", __FUNCTION__);
#endif
}

HIDDEN
void CLASS::process_token_NoVertexBuffer(VendorGLStreamInfo* info)
{
	if (m_tx_vb) {
		m_tx_vb->sys_obj->stamps[0] = 0U /* m_provider->0x50 */;
		--m_tx_vb->counter14;
		m_tx_vb = 0;
	}
#if 0
	info->p[0] = 0x4CU;	// TBD... and again
#else
	info->p[0] = 0U;
	GLLog(3, "%s()\n", __FUNCTION__);
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
		this->0xAA = dword ptr this->0x1F8 != 0 ? 1 : this->0xA8;
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
	info->p[1] = 0x10800001U;	// MI command 0x21U, unknown
	info->p[2] = 64U;
	info->p[3] = 0U /* m_provider->0x50 */;
	q = reinterpret_cast<typeof q>(m_type2_ptr);
	q[val].u = 0U /* m_provider->0x50++ */;
	q[val].v = 0U;
	++info->f2;
#else
	bzero(&info->p[0], 4U * sizeof(uint32_t));
	GLLog(3, "%s()\n", __FUNCTION__);
#endif
}

HIDDEN
void CLASS::process_token_TexSubImage2D(VendorGLStreamInfo* info)
{
	VMsvga2TextureBuffer* tx;
	uint32_t t0 /* var_20 */, t1 /* var_24 */, t2, t3, t4 /* var_2c */, t5, t6;
	uint32_t ip1 /* var_30 */, ip3 /* esi */, ip8 /* var_34 */;
	// edi = info
	// ebx = info->p
	t1 = 0U;
	// var_44 = &info->p[1]
	ip1 = info->p[1];
	// var_40 = &info->p[3]
	ip3 = t0 = info->p[3];
	// var_3c = &info->p[8]
	ip8 = info->p[8];
	// var_38 = &info->p[6]
	tx = m_shared->findTextureBuffer(info->p[6]);	// to var_1c
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
	get_texture(info, tx, true);
	get_tex_data(tx, &t0, &t1);
	t4 = 0U /* tx->0x9C */;
	t2 = ip8 >> 16;	// ecx
	t3 = ip8 & 0xFFFFU;	// edx
	if (t2 > 6U)
		t2 = 0U;
	if (t3 >= 16U)
		t3 = 0U;
	dirtyTexture(tx, t2, t3);
#if 0
	info->p[5] += m_command_buffer.gart_ptr;
#endif
	info->p[0] = info->p[7];
	info->p[1] = (ip1 & 0xFFFF0000U) | t1;
	t5 = (ip3 & 0xFFFFU) + t0;	// in edx
	t6 = ip1 & 0xFFFFU;	// in eax
	t6 *= t4;
	t6 += (ip1 >> 16);
	t6 *= t1;
	info->p[3] = t5 + t6;
	info->p[6] = 0x2000001U;	// MI_FLUSH
	info->p[7] = 0U;
	info->p[8] = 0U;
	removeTextureFromStream(tx);
	if (__sync_fetch_and_add(&tx->sys_obj->refcount, -0x10000) == 0x10000)
		m_shared->delete_texture(tx);
#if LOGGING_LEVEL >= 1
	if (m_log_level >= 3)
		for (int i = 0; i != 9; ++i)
			GLLog(3, "%s:   dword[%d] == %#x\n", __FUNCTION__, i, info->p[i]);
#endif
}

HIDDEN
void CLASS::process_token_CopyPixelsDst(VendorGLStreamInfo* info)
{
	GLLog(3, "%s()\n", __FUNCTION__);
	discard_token_CopyPixelsDst(info);
}

HIDDEN
void CLASS::process_token_CopyPixelsSrc(VendorGLStreamInfo* info)
{
	GLLog(3, "%s()\n", __FUNCTION__);
	discard_token_Noop(info);
}

HIDDEN
void CLASS::process_token_CopyPixelsSrcFBO(VendorGLStreamInfo* info)
{
	GLLog(3, "%s()\n", __FUNCTION__);
	discard_token_Noop(info);
}

HIDDEN
void CLASS::process_token_DrawRect(VendorGLStreamInfo* info)
{
	GLLog(3, "%s()\n", __FUNCTION__);
	*(info->p) = 0x7D800003U; /* 3DSTATE_DRAW_RECT_CMD */
#if 0
	memcpy(&info->p[1], this->0x304U, 4U * sizeof(uint32_t));
#else
	bzero(&info->p[1], 4U * sizeof(uint32_t));
#endif
}

HIDDEN
void CLASS::process_token_AsyncReadDrawBuffer(VendorGLStreamInfo* info)
{
	GLLog(3, "%s()\n", __FUNCTION__);
	discard_token_AsyncReadDrawBuffer(info);
}
