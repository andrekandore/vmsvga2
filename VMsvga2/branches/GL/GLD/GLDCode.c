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
#include <string.h>
#include <IOKit/IOKitLib.h>
#include <OpenGL/CGLTypes.h>
#include "EntryPointNames.h"
#include "GLDTypes.h"
#include "GLDCode.h"
#include "GLDData.h"
#include "ACMethods.h"

__attribute__((visibility("hidden")))
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
	strcpy(dinfo->engine1, "VMsvga2 OpenGL Engine");
	strcpy(dinfo->engine2, "VMsvga2 Compute Engine");
	return ERR_SUCCESS;
}

#define STEP(thold, mask) \
	r |= mask; \
	if (num >= thold) \
		return r;

__attribute__((visibility("hidden")))
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

__attribute__((visibility("hidden")))
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

static
int glrCheckTimeStampShared(gld_shared_t* shared, int32_t num_to_compare)
{
	int32_t const* p = (int32_t const*) shared->channel_memory;
	return (num_to_compare - p[16]) <= 0;
}

__attribute__((visibility("hidden")))
void glrWaitSharedObject(gld_shared_t* shared, gld_waitable_t* w)
{
	uint64_t input;
	int index;
	switch (w->type) {
		case 6:
			index = 1;
			break;
		case 2:
		case 9:
			index = 2;
			break;
		default:
			return;
	}
	if (glrCheckTimeStampShared(shared, w->stamps[index]))
		return;
	input = (uint64_t) w->stamps[index];
	IOConnectCallMethod(shared->obj,
						kIOVMDeviceWaitForStamp,
						&input, 1,
						0, 0, 0, 0, 0, 0);
}

__attribute__((visibility("hidden")))
void glrInitializeHardwareShared(gld_shared_t* shared, void* channel_memory)
{
	shared->channel_memory = channel_memory;
}

__attribute__((visibility("hidden")))
void glrDestroyZeroTexture(gld_shared_t* shared, gld_texture_t* texture)
{
	void* tmp = texture->f11;
	texture->f11 = 0;
	gldDestroyTexture(shared, texture);
	free(tmp);
}

__attribute__((visibility("hidden")))
void glrDestroyHardwareShared(gld_shared_t* shared)
{
}

__attribute__((visibility("hidden")))
void glrFlushSysObject(gld_context_t* context, gld_waitable_t* waitable, int arg2)
{
	if (arg2) {
		if (arg2 == 1)
			switch (waitable->type) {
				case 8:
				case 9:
				case 14:
					break;
				default:
					return;
			}
	} else switch (waitable->type) {
		case 9:
		case 6:
		case 2:
			break;
		default:
			return;
	}

	if (waitable->stamps[3] != 0x10000)
		gldFlush(context);
}

__attribute__((visibility("hidden")))
void load_libGLImage(libglimage_t* interface)
{
	bzero(interface, sizeof *interface);
	interface->handle = dlopen(LIBGLIMAGE, 0);
	if (!interface->handle)
		return;
	interface->glg_processor_default_data = dlsym(interface->handle, "glg_processor_default_data");
	interface->glgTerminateProcessor = dlsym(interface->handle, "glgTerminateProcessor");
}

__attribute__((visibility("hidden")))
void unload_libGLImage(libglimage_t* interface)
{
	if (interface->handle)
		dlclose(interface->handle);
	bzero(interface, sizeof *interface);
}

__attribute__((visibility("hidden")))
void SubmitPacketsToken(gld_context_t* context, int mode)
{
	/*
	 * FIXME
	 */
}

__attribute__((visibility("hidden")))
void glrReleaseVendShrPipeProg(gld_shared_t* shared, void* arg1)
{
}

__attribute__((visibility("hidden")))
void glrDeleteCachedProgram(gld_pipeline_program_t* pp, void* arg1)
{
	// FIXME
}

__attribute__((visibility("hidden")))
void glrDeleteSysPipelineProgram(gld_shared_t* shared, gld_pipeline_program_t* pp)
{
#if 0
	if (pp->f0.f) {
		glpFreePPShaderToProgram(pp->f0.p);	// FIXME: in libGLProgrammability
		pp->f0.p = 0;
		pp->f0.f = 0;
	}
#endif
}
