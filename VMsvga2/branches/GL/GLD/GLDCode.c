/*
 *  GLDCode.c
 *  VMsvga2GLDriver
 *
 *  Created by Zenith432 on December 5th 2010.
 *  Copyright 2010-2011 Zenith432. All rights reserved.
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

#include <dlfcn.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <IOKit/IOKitLib.h>
#include <OpenGL/CGLTypes.h>
#include <OpenGL/gl.h>
#include "EntryPointNames.h"
#include "GLDTypes.h"
#include "GLDCode.h"
#include "GLDData.h"
#include "UCMethods.h"
#include "VLog.h"

#if LOGGING_LEVEL >= 1
#define GLDLog(log_level, ...) do { if (log_level <= logLevel) VLog("GLD: ", ##__VA_ARGS__); } while(false)
#else
#define GLDLog(log_level, ...)
#endif

#pragma GCC visibility push(hidden)

kern_return_t glrPopulateDevice(io_connect_t connect, display_info_t* dinfo)
{
	/*
	 * For Intel, dinfo->config[0] determines type
	 *
	 *   0x100000U - "HD Graphics"
	 *   0x080000U - "GMA X3100"
	 *   0x040000U - "GMA 950"
	 *   else      - "GMA 900"
	 *
	 * Strings are
	 *   "Intel %s OpenGL Engine" (%s == model)
	 *   "Intel %s Compute Engine" (%s == model)
	 */
	strcpy(dinfo->engine1, "VMware SVGA II OpenGL Engine");
	strcpy(dinfo->engine2, "VMware SVGA II Compute Engine");
	return ERR_SUCCESS;
}

#define STEP(thold, mask) \
	r |= mask; \
	if (num >= thold) \
		return r;

uint32_t glrGLIBitsGE(uint32_t num)
{
	uint32_t r = 0;

	STEP(128U, kCGL128Bit);
	STEP(96U,  kCGL96Bit);
	STEP(64U,  kCGL64Bit);
	STEP(48U,  kCGL48Bit);
	STEP(32U,  kCGL32Bit);
	STEP(24U,  kCGL24Bit);
	STEP(16U,  kCGL16Bit);
	STEP(12U,  kCGL12Bit);
	STEP(10U,  kCGL10Bit);
	STEP(8U,   kCGL8Bit);
	STEP(6U,   kCGL6Bit);
	STEP(5U,   kCGL5Bit);
	STEP(4U,   kCGL4Bit);
	STEP(3U,   kCGL3Bit);
	STEP(2U,   kCGL2Bit);
	STEP(1U,   kCGL1Bit);
	return r | 0x20000U | kCGL0Bit;
}

#undef STEP

uint32_t glrGLIColorsGE(uint32_t num)
{
	if (num > 256U)
		return 0;
	if (num > 128U)
		return kCGLRGBFloat256Bit | kCGLRGBAFloat256Bit;
	if (num > 64U)
		return 0x3C000000U; /* kCGLRGBFloat128Bit thru kCGLRGBAFloat256Bit (4 modes) */
	if (num > 32U)
		return 0x3FF00000U;	/* kCGLRGB121212Bit thru kCGLRGBAFloat256Bit (10 modes) */
	if (num > 16U)
		return 0x3FFFC000U; /* kCGLRGB888Bit thru kCGLRGBAFloat256Bit (16 modes) */
	if (num > 8U)
		return 0x3FFFFFC0U; /* kCGLRGB444Bit thru kCGLRGBAFloat256Bit (24 modes) */
	if (num > 0U)
		return 0x3FFFFFFCU;	/* hmm... */
	return 0xBFFFFFFCU;		/* double hmm... */
}

uint32_t glrGLIAlphaGE(int alpha_bits)
{
	if (alpha_bits > 16)
		return 0x8000000U;
	if (alpha_bits > 12)
		return 0xA800000U;
	if (alpha_bits > 8)
		return 0xAA00000U;
	if (alpha_bits > 4)
		return 0xAA9A928U;
	if (alpha_bits > 2)
		return 0xAA9A9A8U;
	if (alpha_bits > 1)
		return 0xAADA9A8U;
	if (alpha_bits > 0)
		return 0xAADADA8U;
	return 0xFFFFFFFFU;
}

static
int glrCheckTimeStampShared(gld_shared_t* shared, int32_t num_to_compare)
{
	int32_t const* p = (int32_t const*) shared->channel_memory;
	return (num_to_compare - p[16]) <= 0;
}

void glrWaitSharedObject(gld_shared_t* shared, gld_sys_object_t* obj)
{
	uint64_t input;
	int index;
	switch (obj->type) {
		case 6:
			index = 0;
			break;
		case 2:
		case 9:
			index = 1;
			break;
		default:
			return;
	}
	if (glrCheckTimeStampShared(shared, obj->stamps[index]))
		return;
	input = (uint64_t) obj->stamps[index];
	IOConnectCallMethod(shared->obj,
						kIOVMDeviceWaitForStamp,
						&input, 1,
						0, 0, 0, 0, 0, 0);
}

void glrInitializeHardwareShared(gld_shared_t* shared, void* channel_memory)
{
	shared->channel_memory = channel_memory;
}

void glrDestroyZeroTexture(gld_shared_t* shared, gld_texture_t* texture)
{
	void* tmp = texture->f11;
	texture->f11 = 0;
	gldDestroyTexture(shared, texture);
	free(tmp);
}

void glrDestroyHardwareShared(gld_shared_t* shared)
{
}

void glrFlushSysObject(gld_context_t* context, gld_sys_object_t* obj, int arg2)
{
	if (arg2) {
		if (arg2 == 1)
			switch (obj->type) {
				case 6:
				case 8:
				case 9:
					break;
				default:
					return;
			}
	} else switch (obj->type) {
		case 9:
		case 6:
		case 2:
			break;
		default:
			return;
	}

	if (obj->refcount != 0x10000)
		gldFlush(context);
}

void load_libs()
{
	bzero(&libglimage, sizeof libglimage);
	libglimage.handle = dlopen(LIBGLIMAGE, 0);
	if (libglimage.handle) {
		libglimage.glg_processor_default_data = dlsym(libglimage.handle, "glg_processor_default_data");
		libglimage.glgConvertType = dlsym(libglimage.handle, "glgConvertType");
		libglimage.glgPixelCenters = dlsym(libglimage.handle, "glgPixelCenters");
		libglimage.glgProcessPixelsWithProcessor = dlsym(libglimage.handle, "glgProcessPixelsWithProcessor");
		libglimage.glgTerminateProcessor = dlsym(libglimage.handle, "glgTerminateProcessor");
	}
	bzero(&libglprogrammability, sizeof libglprogrammability);
	libglprogrammability.handle = dlopen(LIBGLPROGRAMMABILITY, 0);
	if (libglprogrammability.handle) {
		libglprogrammability.glpFreePPShaderLinearize = dlsym(libglprogrammability.handle, "glpFreePPShaderLinearize");
		libglprogrammability.glpFreePPShaderToProgram = dlsym(libglprogrammability.handle, "glpFreePPShaderToProgram");
		libglprogrammability.glpPPShaderLinearize = dlsym(libglprogrammability.handle, "glpPPShaderLinearize");
		libglprogrammability.glpPPShaderToProgram = dlsym(libglprogrammability.handle, "glpPPShaderToProgram");
	}
}

void unload_libs()
{
	if (libglimage.handle)
		dlclose(libglimage.handle);
	bzero(&libglimage, sizeof libglimage);
	if (libglprogrammability.handle)
		dlclose(libglprogrammability.handle);
	bzero(&libglprogrammability, sizeof libglprogrammability);
}

void SubmitPacketsToken(gld_context_t* context, int mode)
{
	/*
	 * FIXME
	 */
}

void glrReleaseVendShrPipeProg(gld_shared_t* shared, void* arg1)
{
}

void glrDeleteCachedProgram(gld_pipeline_program_t* pp, void* arg1)
{
	// FIXME
}

void glrDeleteSysPipelineProgram(gld_shared_t* shared, gld_pipeline_program_t* pp)
{
	if (pp->f0.f) {
		libglprogrammability.glpFreePPShaderToProgram(pp->f0.p);
		pp->f0.p = 0;
		pp->f0.f = 0;
	}
}

GLDReturn glrValidatePixelFormat(PixelFormat* pixel_format)
{
	// FIXME
	return kCGLNoError;
}

void glrKillClient(kern_return_t kr)
{
	// FIXME
}

uint32_t xlateColorMode(uint32_t colorMode)
{
	if (colorMode & 0xFFFFFCU) {
		if (colorMode & 0x3FC0U)
			return 3U; /* kIOAccelSurfaceModeColorDepth1555 */
		if (colorMode & 0xFC000U)
			return 4U; /* kIOAccelSurfaceModeColorDepth8888 */
		if (colorMode & 0x3F00000U)
			return 11U; /* kIOAccelSurfaceModeColorDepthRGBA64 */
		return 0;
	}
	if (colorMode & 0x3F000000U) {
		if (colorMode & 0x3F00000U)
			return 12U; /* kIOAccelSurfaceModeColorDepthRGBAFloat64 */
		if (colorMode & 0xC000000U)
			return 13U; /* kIOAccelSurfaceModeColorDepthRGBAFloat128 */
	}
	return 0;
}

void glrSetWindowModeBits(uint32_t* wmb, PixelFormat* pixel_format)
{
	uint32_t mb = *wmb;
	switch (pixel_format->depthModes) {
		case kCGL0Bit:
			break;
		case kCGL16Bit:
			mb |= CGLCMB_DepthMode16;
			break;
		case kCGL32Bit:
			mb |= CGLCMB_DepthMode32;
			break;
		default:
			mb |= (CGLCMB_DepthMode16 | CGLCMB_DepthMode32);
			break;
	}
	if (pixel_format->sampleBuffers > 0 &&
		pixel_format->samples == 4)
		mb |= 0x4000000U;
	*wmb = mb;
}

void glrInitializeHardwareState(gld_context_t* context, PixelFormat* pixel_format)
{
	// FIXME
}

void glrSetConfigData(gld_context_t* context, void* arg3, PixelFormat* pixel_format)
{
	// FIXME
}

char const* glrGetString(display_info_t* dinfo, int string_code)
{
	switch (string_code) {
		case GL_VENDOR:
			return "Zenith432"; /* "Intel Inc." */
		case GL_RENDERER:
			return &dinfo->engine1[0];
		case GL_VERSION:
#if 0
			if (dinfo->config[0] & 0x100000U)
				return "2.1 APPLE-1.6.24";
			if (dinfo->config[0] & 0x80000U)
				return "2.0 APPLE-1.6.24";
#endif
			return "1.4 APPLE-1.6.24";
		case GL_EXTENSIONS:
			break;
		case GL_VENDOR + 4:
#if 0
			if (dinfo->config[0] & 0x100000U)
				return "IntelHDGraphicsGLDriver";
			if (dinfo->config[0] & 0x80000U)
				return "Intel965GLDriver";
			return "Intel915GLDriver";
#endif
			return "VMsvga2GLDriver";
		case GL_VENDOR + 5:
			return &dinfo->engine2[0];
	}
	return 0;
}

void glrFlushMemory(int arg0, void* arg1, int arg2)
{
	// FIXME: neat function, flushes cache line
}

void glrReleaseDrawable(gld_context_t* context)
{
	// FIXME
}

int glrSanitizeWindowModeBits(uint32_t mode_bits)
{
	return 1;
}

void glrDrawableChanged(gld_context_t* context)
{
	// FIXME
}

int glrGetKernelTextureAGPRef(gld_shared_t* shared,
							  gld_texture_t* texture,
							  void const* pixels1,
							  void const* pixels2,
							  uint32_t texture_size)
{
	uint8_t const* client_texture = (uint8_t const*) texture->client_texture; // r13
	gld_sys_object_t* sys_obj = texture->obj; // r15
	int rc = 0;
	kern_return_t kr;
	uint64_t input;
	size_t outputStructCnt;
	struct sIONewTextureReturnData outputStruct;
#if 0
	uint8_t ecx, edi, esi;
#endif

	// var_58 = shared;
	// r14 = texture;
#if 0
	ecx = *MKOFS(uint8_t, 0x75, 0x7D, client_texture);
	edi = *MKOFS(uint8_t, 0x78, 0x80, client_texture);
	esi = 0;
	if (edi) {
		// 1442C
		if (!(*MKOFS(uint16_t, 0x7A, 0x82, client_texture) & (1U << ecx))) {
			
		}
	}
#endif
	// skip to 14489
	texture->obj = 0;
	texture->raw_data = 0;
	texture->tds.type = 6;
	texture->tds.pixels[0] = (uintptr_t) pixels1;
	texture->tds.pixels[1] = (uintptr_t) pixels2;
	texture->tds.size[0] = texture_size;
	texture->tds.num_faces = *MKOFS(uint8_t, 0x78, 0x80, client_texture);
	texture->tds.num_mipmaps = *MKOFS(uint8_t, 0x76, 0x7E, client_texture);
	texture->tds.min_mipmap = *MKOFS(uint8_t, 0x75, 0x7D, client_texture);
	// texture->flags[3] = glrPixelBytes(word rbx+0x14, word rbx+0x16);
	texture->tds.bytespp = 2;	// also to esi
	texture->tds.width = 0;	// taken from client_texture
	texture->tds.height = 0; // taken from client_texture
	texture->tds.depth = 1; // taken from client_texture
	texture->tds.read_only = 0;
	texture->tds.pitch = 0; // calculated... TBD
	outputStructCnt = sizeof outputStruct;
	kr = IOConnectCallMethod(shared->obj,
							 kIOVMDeviceNewTexture,
							 0, 0,
							 &texture->tds, sizeof texture->tds,
							 0, 0,
							 &outputStruct, &outputStructCnt);
	if (kr == ERR_SUCCESS) {
		texture->obj = (gld_sys_object_t*) (uintptr_t) outputStruct.sys_obj_addr;	// Truncation32
		__sync_fetch_and_add(&texture->obj->refcount, 0x10000);
		texture->obj->in_use = 1;
		rc = 1;
		if (sys_obj != texture->obj)
			texture->f14 |= 1;
	}
	if (sys_obj &&
		__sync_fetch_and_add(&sys_obj->refcount, -0x10000) == 0x10000) {
		input = sys_obj->object_id;
		kr = IOConnectCallMethod(shared->obj,
								 kIOVMDeviceDeleteTexture,
								 &input, 1,
								 0, 0, 0, 0, 0, 0);
	}
	return rc;
}

#if 0
int glrGetKernelTextureOrphanStandard(gld_shared_t* shared,
									  gld_texture_t* texture,
									  uint32_t size0,
									  uint32_t size1,
									  uint32_t arg4,
									  uint32_t bytespp,
									  uint32_t pitch)
{
	// var_60 = shared
	// r12 = texture
	// var_64 = size0
	// var_68 = size1
	// var_6C = arg4
	// var_70 = bytespp
	uint8_t const* client_texture = (uint8_t const*) texture->client_texture; // rbx
	kern_return_t kr;
	uint64_t input;
	size_t outputStructCnt;
	struct sIONewTextureReturnData outputStruct;
	uint8_t edi = *MKOFS(uint8_t, 0x75, 0x7D, client_texture);
	uint8_t r8b = *MKOFS(uint8_t, 0x78, 0x80, client_texture);
	uint32_t width; // var_54
	uint32_t edx;
	for (edx = 0U; edx < r8b; ++edx)
		if (MKOFS(uint16_t, 0x7A, 0x82, client_texture)[edx] & (1U << edi))
				break;
//	edx *= 15U;
	rax = 32U * (edi + 15U * edx);
	rcx = client_texture + 0xC8 + rax;
	width = word ptr [rcx+8] - 2U * byte ptr [rcx + 0xE];
	// continues calculations 14986-149AD
	// r13 = texture->obj;
	if (texture->obj) {
		if (texture->obj->type != 8U) {
			texture->f14 |= 1U;
			if (__sync_fetch_and_add(&texture->obj->refcount, -0x10000) == 0x10000) {
				// TBD 14C52
			}
			texture->obj = 0;
			texture->tds.type = 8U;
			texture->tds.size[0] = size0;
			texture->tds.size[1] = size1;
			texture->tds.version = *MKOFS(uint8_t, 0x78, 0x80, client_texture);
			texture->tds.flags[0] = *MKOFS(uint8_t, 0x76, 0x7E, client_texture);
			texture->tds.flags[1] = *MKOFS(uint8_t, 0x75, 0x7D, client_texture);
			texture->tds.bytespp = bytespp;
			texture->tds.width = width;
			texture->tds.height = r15w;
			texture->tds.depth = r14w;
			texture->tds.f0 = arg4;
			texture->tds.pitch = pitch;
			texture->tds.read_only = 0U;
			outputStructCnt = sizeof outputStruct;
			kr = IOConnectCallMethod(shared->obj,
									 kIOVMDeviceNewTexture,
									 0, 0,
									 &texture->tds, sizeof texture->tds,
									 0, 0,
									 &outputStruct, &outputStructCnt);
			if (kr != ERR_SUCCESS)
				return 0;
			texture->obj = outputStruct.addr;
			__sync_fetch_and_add(&texture->obj->refcount, 0x10000);
			texture->raw_data = outputStruct.data;
			texture->obj->in_use = 1U;
			return 1;
		} else {
			// 14AF0
		}
	} else {
		// 14C40
	}
}
#endif

void glrWriteAllHardwareState(gld_context_t* context)
{
	uint32_t count;
	uint32_t* mem1 = context->mem1_addr;
	mem1[5] |= 1U;
	memcpy(&mem1[8], &context->block1[0], sizeof context->block1);
	mem1[8 + 285] = 0x2000001U;
	count = 3U * context->f29;
	mem1[8 + 286] = (3U * count - 1U) | 0x7D050000U;
	memcpy(&mem1[8 + 287], &context->block2[0], count * sizeof(uint32_t));
	count += 287;
	mem1[4] = count;
	if (mem1[3] < count)
		exit(1);
}

#define GLD_DEFINE_GENERIC_DISPATCH(name, index) \
void name(gld_context_t* context) \
{ \
void *args = __builtin_apply_args(), *ret; \
GLDLog(2, "%s()\n", __FUNCTION__); \
if (remote_dispatch[index]) { \
ret = __builtin_apply(remote_dispatch[index], args, 64); \
__builtin_return(ret); \
} \
}

void glrAccum(gld_context_t* context, uint32_t mode, float accum)
{
	typeof(glrAccum) *addr;
	GLDLog(2, "%s(%p, %u, %f)\n", __FUNCTION__, context, mode, accum);
	cb_chkpt(context, 0);
	addr = (typeof(addr)) remote_dispatch[0];
	if (addr)
		addr(context, mode, accum);
	cb_chkpt(context, 1);
}

/*
 * GL_COLOR_BUFFER_BIT, GL_DEPTH_BUFFER_BIT, GL_ACCUM_BUFFER_BIT, GL_STENCIL_BUFFER_BIT
 */
void glrClear(gld_context_t* context, uint32_t mask)
{
	typeof(glrClear) *addr;
	GLDLog(2, "%s(%p, %#x)\n", __FUNCTION__, context, mask);
	cb_chkpt(context, 0);
	addr = (typeof(addr)) remote_dispatch[1];
	if (addr)
		addr(context, mask);
	cb_chkpt(context, 1);
}

int glrReadPixels(gld_context_t* context,
				  uint32_t arg1,
				  uint32_t arg2,
				  uint32_t arg3,
				  uint32_t arg4,
				  uint32_t arg5,
				  uint32_t arg6,
				  size_t arg7,
				  uint32_t arg8,
				  void* arg9)
{
	typeof(glrReadPixels) *addr;
	int rc = 0;
	GLDLog(2, "%s(%p, %u, %u, %u, %u, %u, %u, %lu, %u, %p)\n", __FUNCTION__, context, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9);
	cb_chkpt(context, 0);
	addr = (typeof(addr)) remote_dispatch[2];
	if (addr)
		rc = addr(context, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9);
	cb_chkpt(context, 1);
	return rc;
}

GLD_DEFINE_GENERIC_DISPATCH(glrDrawPixels, 3)
GLD_DEFINE_GENERIC_DISPATCH(glrCopyPixels, 4)
GLD_DEFINE_GENERIC_DISPATCH(glrRenderBitmap, 5)

void glrRenderPoints(gld_context_t* context, void* client_vertex_array, uint32_t num_vertices)
{
	typeof(glrRenderPoints) *addr;
	GLDLog(2, "%s(%p, %p, %u)\n", __FUNCTION__, context, client_vertex_array, num_vertices);
	cb_chkpt(context, 0);
	addr = (typeof(addr)) remote_dispatch[6];
	if (addr)
		addr(context, client_vertex_array, num_vertices);
	cb_chkpt(context, 1);
}

void glrRenderLines(gld_context_t* context, void* client_vertex_array, uint32_t num_vertices)
{
	typeof(glrRenderLines) *addr;
	GLDLog(2, "%s(%p, %p, %u)\n", __FUNCTION__, context, client_vertex_array, num_vertices);
	cb_chkpt(context, 0);
	addr = (typeof(addr)) remote_dispatch[7];
	if (addr)
		addr(context, client_vertex_array, num_vertices);
	cb_chkpt(context, 1);
}

void glrRenderLineStrip(gld_context_t* context, void* client_vertex_array, uint32_t num_vertices, uint32_t mode)
{
	typeof(glrRenderLineStrip) *addr;
	GLDLog(2, "%s(%p, %p, %u, %u)\n", __FUNCTION__, context, client_vertex_array, num_vertices, mode);
	cb_chkpt(context, 0);
	addr = (typeof(addr)) remote_dispatch[8];
	if (addr)
		addr(context, client_vertex_array, num_vertices, mode);
	cb_chkpt(context, 1);
}

void glrRenderLineLoop(gld_context_t* context, void* client_vertex_array, uint32_t num_vertices, uint32_t mode)
{
	typeof(glrRenderLineLoop) *addr;
	GLDLog(2, "%s(%p, %p, %u, %u)\n", __FUNCTION__, context, client_vertex_array, num_vertices, mode);
	cb_chkpt(context, 0);
	addr = (typeof(addr)) remote_dispatch[9];
	if (addr)
		addr(context, client_vertex_array, num_vertices, mode);
	cb_chkpt(context, 1);
}

void glrRenderPolygon(gld_context_t* context, void* client_vertex_array, uint32_t num_vertices, uint32_t mode)
{
	typeof(glrRenderPolygon) *addr;
	GLDLog(2, "%s(%p, %p, %u, %u)\n", __FUNCTION__, context, client_vertex_array, num_vertices, mode);
	cb_chkpt(context, 0);
	addr = (typeof(addr)) remote_dispatch[10];
	if (addr)
		addr(context, client_vertex_array, num_vertices, mode);
	cb_chkpt(context, 1);
}

void glrRenderTriangles(gld_context_t* context, void* client_vertex_array, uint32_t num_vertices, uint32_t mode)
{
	typeof(glrRenderTriangles) *addr;
	GLDLog(2, "%s(%p, %p, %u, %u)\n", __FUNCTION__, context, client_vertex_array, num_vertices, mode);
	cb_chkpt(context, 0);
	addr = (typeof(addr)) remote_dispatch[11];
	if (addr)
		addr(context, client_vertex_array, num_vertices, mode);
	cb_chkpt(context, 1);
}

void glrRenderTriangleFan(gld_context_t* context, void* client_vertex_array, uint32_t num_vertices, uint32_t arg3, uint32_t mode)
{
	typeof(glrRenderTriangleFan) *addr;
	GLDLog(2, "%s(%p, %p, %u, %u)\n", __FUNCTION__, context, client_vertex_array, num_vertices, arg3, mode);
	cb_chkpt(context, 0);
	addr = (typeof(addr)) remote_dispatch[12];
	if (addr)
		addr(context, client_vertex_array, num_vertices, arg3, mode);
	cb_chkpt(context, 1);
}

void glrRenderTriangleStrip(gld_context_t* context, void* client_vertex_array, uint32_t num_vertices, uint32_t mode)
{
	typeof(glrRenderTriangleStrip) *addr;
	GLDLog(2, "%s(%p, %p, %u, %u)\n", __FUNCTION__, context, client_vertex_array, num_vertices, mode);
	cb_chkpt(context, 0);
	addr = (typeof(addr)) remote_dispatch[13];
	if (addr)
		addr(context, client_vertex_array, num_vertices, mode);
	cb_chkpt(context, 1);
}

void glrRenderQuads(gld_context_t* context, void* client_vertex_array, uint32_t num_vertices, uint32_t mode)
{
	typeof(glrRenderQuads) *addr;
	GLDLog(2, "%s(%p, %p, %u, %u)\n", __FUNCTION__, context, client_vertex_array, num_vertices, mode);
	cb_chkpt(context, 0);
	addr = (typeof(addr)) remote_dispatch[14];
	if (addr)
		addr(context, client_vertex_array, num_vertices, mode);
	cb_chkpt(context, 1);
}

void glrRenderQuadStrip(gld_context_t* context, void* client_vertex_array, uint32_t num_vertices, uint32_t mode)
{
	typeof(glrRenderQuadStrip) *addr;
	GLDLog(2, "%s(%p, %p, %u, %u)\n", __FUNCTION__, context, client_vertex_array, num_vertices, mode);
	cb_chkpt(context, 0);
	addr = (typeof(addr)) remote_dispatch[15];
	if (addr)
		addr(context, client_vertex_array, num_vertices, mode);
	cb_chkpt(context, 1);
}

void glrRenderPointsPtr(gld_context_t* context, void* client_vertex_array, uint32_t num_vertices)
{
	typeof(glrRenderPointsPtr) *addr;
	GLDLog(2, "%s(%p, %p, %u)\n", __FUNCTION__, context, client_vertex_array, num_vertices);
	cb_chkpt(context, 0);
	addr = (typeof(addr)) remote_dispatch[20];
	if (addr)
		addr(context, client_vertex_array, num_vertices);
	cb_chkpt(context, 1);
}

void glrRenderLinesPtr(gld_context_t* context, void* client_vertex_array, uint32_t num_vertices, uint32_t mode)
{
	typeof(glrRenderLinesPtr) *addr;
	GLDLog(2, "%s(%p, %p, %u, %u)\n", __FUNCTION__, context, client_vertex_array, num_vertices, mode);
	cb_chkpt(context, 0);
	addr = (typeof(addr)) remote_dispatch[21];
	if (addr)
		addr(context, client_vertex_array, num_vertices, mode);
	cb_chkpt(context, 1);
}

void glrRenderPolygonPtr(gld_context_t* context, void* client_vertex_array, uint32_t num_vertices, uint32_t mode)
{
	typeof(glrRenderPolygonPtr) *addr;
	GLDLog(2, "%s(%p, %p, %u, %u)\n", __FUNCTION__, context, client_vertex_array, num_vertices, mode);
	cb_chkpt(context, 0);
	addr = (typeof(addr)) remote_dispatch[22];
	if (addr)
		addr(context, client_vertex_array, num_vertices, mode);
	cb_chkpt(context, 1);
}

void glrBeginPrimitiveBuffer(gld_context_t* context, uint32_t arg1, void* arg2)
{
	typeof(glrBeginPrimitiveBuffer) *addr;
	GLDLog(2, "%s(%p, %u, %p)\n", __FUNCTION__, context, arg1, arg2);
	cb_chkpt(context, 0);
	addr = (typeof(addr)) remote_dispatch[23];
	if (addr)
		addr(context, arg1, arg2);
	cb_chkpt(context, 1);
}

void glrEndPrimitiveBuffer(gld_context_t* context, void* arg1, void* arg2, uint32_t arg3)
{
	typeof(glrEndPrimitiveBuffer) *addr;
	GLDLog(2, "%s(%p, %p, %p, %u)\n", __FUNCTION__, context, arg1, arg2, arg3);
	cb_chkpt(context, 0);
	addr = (typeof(addr)) remote_dispatch[24];
	if (addr)
		addr(context, arg1, arg2, arg3);
	cb_chkpt(context, 1);
}

void glrHookFinish(gld_context_t* context)
{
	typeof(glrHookFinish) *addr;
	GLDLog(2, "%s(%p)\n", __FUNCTION__, context);
	cb_chkpt(context, 0);
	addr = (typeof(addr)) remote_dispatch[27];
	if (addr)
		addr(context);
	cb_chkpt(context, 1);
}

void glrHookFlush(gld_context_t* context)
{
	typeof(glrHookFlush) *addr;
	GLDLog(2, "%s(%p)\n", __FUNCTION__, context);
	cb_chkpt(context, 0);
	addr = (typeof(addr)) remote_dispatch[28];
	if (addr)
		addr(context);
	cb_chkpt(context, 1);
}

void glrHookSwap(gld_context_t* context)
{
	typeof(glrHookSwap) *addr;
	GLDLog(2, "%s(%p)\n", __FUNCTION__, context);
	cb_chkpt(context, 0);
	addr = (typeof(addr)) remote_dispatch[29];
	if (addr)
		addr(context);
	cb_chkpt(context, 1);
}

void glrSetFence(gld_context_t* context, void* arg1)
{
	typeof(glrSetFence) *addr;
	GLDLog(2, "%s(%p, %p)\n", __FUNCTION__, context, arg1);
	cb_chkpt(context, 0);
	addr = (typeof(addr)) remote_dispatch[30];
	if (addr)
		addr(context, arg1);
	cb_chkpt(context, 1);
}

int glrNoopRenderVertexArray(gld_context_t* context)
{
	/*
	 * Dispatch index 31
	 */
	GLDLog(2, "%s(%p)\n", __FUNCTION__, context);
	return 0;
}

void cb_chkpt(gld_context_t* context, int v)
{
	static uint32_t* remember = 0;
	ptrdiff_t num_done;
	if (!v) {
		if (remember && remember != context->cb_iter[1]) {
			num_done = context->cb_iter[1] - remember;
			if (num_done > 0)
				GLDLog(2, "      %s: skipped %ld dwords\n", __FUNCTION__, (long) num_done);
		}
		remember = context->cb_iter[1];
		return;
	}
	num_done = context->cb_iter[1] - remember;
	remember = context->cb_iter[1];
	if (num_done > 0)
		GLDLog(2, "      %s: added %ld dwords\n", __FUNCTION__, (long) num_done);
}

#pragma GCC visibility pop
