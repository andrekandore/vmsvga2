/*
 *  GLDCode.c
 *  VMsvga2GLDriver
 *
 *  Created by Zenith432 on December 5th 2010.
 *  Copyright 2010 Zenith432. All rights reserved.
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
#include <stdlib.h>
#include <string.h>
#include <IOKit/IOKitLib.h>
#include <OpenGL/CGLTypes.h>
#include <OpenGL/gl.h>
#include "EntryPointNames.h"
#include "GLDTypes.h"
#include "GLDCode.h"
#include "GLDData.h"
#include "ACMethods.h"

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
				case 8:
				case 9:
				case 14:
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
	texture->f0 = 0;
	texture->tds.type = 6;
	texture->tds.pixels[0] = (uintptr_t) pixels1;
	texture->tds.pixels[1] = (uintptr_t) pixels2;
	texture->tds.size = texture_size;
	texture->tds.version = *MKOFS(uint8_t, 0x78, 0x80, client_texture);
	texture->tds.flags[0] = *MKOFS(uint8_t, 0x76, 0x7E, client_texture);
	texture->tds.flags[1] = *MKOFS(uint8_t, 0x75, 0x7D, client_texture);
	// texture->flags[3] = glrPixelBytes(word rbx+0x14, word rbx+0x16);
	texture->tds.bytespp = 2;	// also to esi
	texture->tds.width = 0;	// taken from client_texture
	texture->tds.height = 0; // taken from client_texture
	texture->tds.depth = 1; // taken from client_texture
	texture->tds.f3[0] = 0;
	texture->tds.pitch = 0; // calculated... TBD
	outputStructCnt = sizeof outputStruct;
	kr = IOConnectCallMethod(shared->obj,
							 kIOVMDeviceNewTexture,
							 0, 0,
							 &texture->tds, sizeof texture->tds,
							 0, 0,
							 &outputStruct, &outputStructCnt);
	if (kr == ERR_SUCCESS) {
		texture->obj = (gld_sys_object_t*) (uintptr_t) outputStruct.addr;	// Truncation32
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

#pragma GCC visibility pop
