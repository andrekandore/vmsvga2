/*
 *  VMsvga2GLDriver.c
 *  VMsvga2GLDriver
 *
 *  Created by Zenith432 on December 6th 2009.
 *  Copyright 2009-2010 Zenith432. All rights reserved.
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
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <IOKit/IOKitLib.h>
#include <OpenGL/CGLRenderers.h>
#include <OpenGL/CGLTypes.h>
#include "GLDTypes.h"
#include "VMsvga2GLDriver.h"
#include "EntryPointNames.h"
#include "GLDData.h"
#include "GLDCode.h"
#include "ACMethods.h"
#include "VLog.h"

#if LOGGING_LEVEL >= 1
#define GLDLog(log_level, ...) do { if (log_level <= logLevel) VLog("GLD: ", ##__VA_ARGS__); } while(false)
#else
#define GLDLog(log_level, ...)
#endif

#define GLD_DEFINE_GENERIC(name, index) \
	GLDReturn name(void* arg0, void* arg1, void* arg2, void* arg3, void* arg4, void* arg5) \
	{ \
		GLDLog(2, "%s(%p, %p, %p, %p, %p, %p)\n", __FUNCTION__, arg0, arg1, arg2, arg3, arg4, arg5); \
		if (bndl_ptrs[bndl_index][index]) return bndl_ptrs[bndl_index][index](arg0, arg1, arg2, arg3, arg4, arg5); \
		return -1; \
	}

#pragma mark -
#pragma mark Work Left
#pragma mark -

/*
 * Load libGLProgrammability
 * Pass log_level_gld via get_config
 * Complete autonomic version of gldChoosePixelFormat
 * Complete support functions autonomic version of gldCreateContext; implement gldDestroyContext
 * Complete autonomic version of gldSetInteger
 * Complete autonomic version of gldAttachDrawable
 * Complete autonomic version of SubmitPacketsToken
 * Complete autonomic version of gldUnbindTexture
 * Complete autonomic version of gldReclaimTexture
 * Complete autonomic version of gldUnbindPipelineProgram, support funcs for gldDestroyPipelineProgram
 *
 * Later
 *   Need autonomic:
 *     gldGetRendererInfo, gldReclaimContext, gldInitDispatch, gldUpdateDispatch, gldGetInteger
 *     gldTestObject, gldFinishObject, gldGenerateTexMipmaps, gldLoadTexture
 *     gldGetTextureLevelInfo, gldGetTextureLevelImage, gldPageoffBuffer
 *     gldGetMemoryPlugin, gldSetMemoryPlugin, gldTestMemoryPlugin, gldFlushMemoryPlugin, gldDestroyMemoryPlugin
 *     gldUnbindFramebuffer, gldGetPipelineProgramInfo, gldModifyPipelineProgram, gldUnbindPipelineProgram
 *     gldCreateFence, gldObjectPurgeable, gldObjectUnpurgeable
 *
 * Ugh
 *   gldCopyTexSubImage, gldModifyTexSubImage
 *
 * Obsolete (10.6.2)
 *   gldCreateTextureLevel, gldModifyTextureLevel, gldDestroyTextureLevel
 */

#pragma mark -
#pragma mark GLD
#pragma mark -

static
void gldInitializeLibrarySelf(io_service_t const* pServices,
							  uint8_t const* pServiceFlags,
							  uint32_t GLDisplayMask,
							  PIODataFlush,
							  PIODataBindSurface);

void gldInitializeLibrary(io_service_t const* pServices,
						  uint8_t const* pServiceFlags,
						  uint32_t GLDisplayMask,
						  PIODataFlush IODataFlush,
						  PIODataBindSurface IODataBindSurface)
{
	typeof(gldInitializeLibrary) *addr;
	char const* bndl_names[] = { BNDL1, BNDL2 };
	int i, j;

	GLDLog(1, "%s(%p, %p, %#x, %p, %p)\n", __FUNCTION__, pServices, pServiceFlags, GLDisplayMask, IODataFlush, IODataBindSurface);

	for (j = 0; j != 2; ++j) {
		bndl_handle[j] = dlopen(bndl_names[j], 0);
		if (!bndl_handle[j])
			continue;
		addr = (typeof(addr)) dlsym(bndl_handle[j], "gldInitializeLibrary");
		if (addr) {
			for (i = 0; i != NUM_ENTRIES; ++i)
				bndl_ptrs[j][i] = (GLD_GENERIC_FUNC) dlsym(bndl_handle[j], entry_point_names[i]);
			addr(pServices, pServiceFlags, GLDisplayMask, IODataFlush, IODataBindSurface);
		} else {
			dlclose(bndl_handle[j]);
			bndl_handle[j] = 0;
		}
	}

	if (bndl_handle[0])
		bndl_index = 0;
	else
		bndl_index = 1;

	load_libs();
	gldInitializeLibrarySelf(pServices, pServiceFlags, GLDisplayMask, IODataFlush, IODataBindSurface);
	// FIXME: find out how to signal an error
}

static
void gldInitializeLibrarySelf(io_service_t const* pServices,
							  uint8_t const* pServiceFlags,
							  uint32_t GLDisplayMask,
							  PIODataFlush IODataFlush,
							  PIODataBindSurface IODataBindSurface)
{
	uint32_t last_display, num_displays, display_num, outputCnt;
	kern_return_t rc;
	io_connect_t connect;
	display_info_t* dinfo;
	uint64_t output[5];
	io_service_t services[32];
	uint32_t masks[32];

	/*
	 * Initialization from GMA950
	 */
	glr_io_data.pServices = pServices;
	glr_io_data.pServiceFlags = pServiceFlags;
	glr_io_data.displayMask = GLDisplayMask;
	glr_io_data.IODataFlush = IODataFlush;
	glr_io_data.IODataBindSurface = IODataBindSurface;
	last_display = 0;
	num_displays = 0;
	for (display_num = 0U; display_num != 32U; ++display_num) {
		glr_io_data.surfaces[display_num] = 0;
		glr_io_data.surface_refcount[display_num] = 0;
		services[display_num] = 0;
		masks[display_num] = 0;
		if (GLDisplayMask & (1U << display_num)) {
			last_display = display_num + 1;
			if (!pServiceFlags[display_num]) {
				services[num_displays] = pServices[display_num];
				masks[num_displays] |= 1U << display_num;
				++num_displays;
			}
		}
	}
	glr_io_data.lastDisplay = last_display;
	dinfo = (display_info_t*) malloc(num_displays * sizeof *dinfo);
	GLDLog(1, "  %s: display_info @%p\n", __FUNCTION__, dinfo);
	for (display_num = 0U; display_num != num_displays; ++display_num) {
		connect = 0;
		dinfo[display_num].service = services[display_num];
		dinfo[display_num].mask = masks[display_num];
		rc = IOServiceOpen(services[display_num],
						   mach_task_self(),
						   4 /* Device UserClient */,
						   &connect);
		if (rc != ERR_SUCCESS) {
			free(dinfo);
			dinfo = 0;
			num_displays = 0;
			break;
		}
		outputCnt = 5;
		rc = IOConnectCallMethod(connect,
								 kIOVMDeviceGetConfig,
								 0,
								 0,
								 0,
								 0,
								 &output[0],
								 &outputCnt,
								 0,
								 0);
		if (rc != ERR_SUCCESS) {
			IOServiceClose(connect);
			free(dinfo);
			dinfo = 0;
			num_displays = 0;
			break;
		}
		dinfo->config[0] = (uint32_t) output[0];
#if LOGGING_LEVEL >= 1
		logLevel = (int) (output[1] & 7U);
#endif
		dinfo->config[1] = ((uint32_t) output[1]) & ~7U;
		dinfo->config[2] = (uint32_t) output[2];
		dinfo->config[3] = (uint32_t) output[3];
		dinfo->config[4] = (uint32_t) output[4];
		rc = glrPopulateDevice(connect, dinfo);
		IOServiceClose(connect);
		if (rc != ERR_SUCCESS) {
			free(dinfo);
			dinfo = 0;
			num_displays = 0;
			break;
		}
	}
	glr_io_data.num_displays = num_displays;
	glr_io_data.dinfo = dinfo;
}

void gldTerminateLibrary(void)
{
	typeof(gldTerminateLibrary) *addr;
	int j;

	GLDLog(1, "%s()\n", __FUNCTION__);

	glr_io_data.pServices = 0;
	glr_io_data.pServiceFlags = 0;
	glr_io_data.displayMask = 0;
	glr_io_data.lastDisplay = 0;
	glr_io_data.IODataFlush = 0;
	glr_io_data.IODataBindSurface = 0;
	if (glr_io_data.dinfo) {
		free(glr_io_data.dinfo);
		glr_io_data.dinfo = 0;
	}
	glr_io_data.num_displays = 0;

	unload_libs();
	for (j = 0; j != 2; ++j) {
		if (!bndl_handle[j])
			continue;
		addr = (typeof(addr)) dlsym(bndl_handle[j], "gldTerminateLibrary");
		if (addr)
			addr();
		dlclose(bndl_handle[j]);
		bndl_handle[j] = 0;
	}
}

_Bool gldGetVersion(int* arg0, int* arg1, int* arg2, int* arg3)
{
	if (!glr_io_data.dinfo)
		return 0;
	/*
	 * Note these numbers are from OS 10.6.5
	 *
	 * arg3 is the renderer ID, which is
	 *   0x4000 for Intel GMA 950
	 *   0x0400 for GLRendererFloat
	 *   (see CGLRenderers.h)
	 */
	*arg0 = 3;
	*arg1 = 1;
	*arg2 = 0;
	*arg3 = kCGLRendererIntel900ID & 0xFFFFU;
	GLDLog(2, "%s(%d, %d, %d, %#x)\n", __FUNCTION__, *arg0, *arg1, *arg2, (uint32_t) *arg3);
	return 1;
}

GLDReturn gldGetRendererInfo(void* struct_out, uint32_t GLDisplayMask)
{
	typeof(gldGetRendererInfo) *addr;
	int i;
	GLDReturn rc;
	uint32_t* p;

	GLDLog(2, "%s(%p, %#x)\n", __FUNCTION__, struct_out, GLDisplayMask);

	addr = (typeof(addr)) bndl_ptrs[0][1];
	if (addr) {
		rc = addr(struct_out, GLDisplayMask);
		GLDLog(2, "  %s: returns %d\n", __FUNCTION__, rc);
		if (rc != kCGLNoError)
			return rc;
		p = (uint32_t *) (((long*) struct_out) + 1);
#if 0
		*p = ((*p) & 0xFFFF0000U) | 0x4000U;
		p[1] = 0x17CDU;
		p[17] = 64;
		p[19] = 64;
#endif
		for (i = 0; i < 32; ++i)
			GLDLog(2, "  %s: [%d] == %#x\n", __FUNCTION__, i, p[i]);
		return kCGLNoError;
	}
	return -1;
}

static
GLDReturn gldChoosePixelFormatInternal(PixelFormat** struct_out, int const* attributes)
{
	uint32_t displayMask /* var_164 */,
		buffer_modes /* var_130 */,
		flags /* r14d */,
		color_mask /* var_168 */,
		sample_alpha /* var_160 */,
		sample_mode /* var_148 */,
		stencil_mode /* var_13c */,
		depth_mode /* var_138 */,
		alpha_trimmed_color_mask /* var_12c */;
	int const* pAttr;
	int attr,
		color_bits /* r15d */,
		depth_bits /* r13d */,
		stencil_bits /* esi */,
		accum_bits /* var_16c */,
		alpha_bits /* r12d */,
		min_policy /* var_174 */,
		max_policy /* var_178 */;
	int16_t sample_buffers /* var_140 */,
			spermsb /* var_142 */,
			aux_buffers /* var_13e */;

	// struct_out -->var_188
	// attributes -->rbx
	*struct_out = 0;
	if (getenv("GL_REJECT_HW"))
		return kCGLNoError;
	displayMask = glr_io_data.displayMask;
	pAttr = attributes;	// rdx
	max_policy = 0;
	min_policy = 0;
	color_bits = 0;
	depth_bits = 0;
	stencil_bits = 0;
	accum_bits = 0;
	alpha_bits = 0;
	color_mask = 0xFFFFFCU; /* All colors thru kCGLRGBA16161616Bit */
	sample_alpha = 0;
	sample_mode = 0;
	spermsb = 0;
	sample_buffers = 0;
	aux_buffers = 0;
	buffer_modes = 0;
	flags = 0x500U;	/* bAccelerated, bCompliant */
	while ((attr = *pAttr++)) {
		switch (attr) {
			case 4:
				break;
			case kCGLPFADoubleBuffer:
				buffer_modes |= kCGLDoubleBufferBit;
				break;
			case kCGLPFAStereo:
				buffer_modes |= kCGLStereoscopicBit;
				break;
			case kCGLPFAAuxBuffers:
				attr = *pAttr++;
				if (attr < 0)
					return kCGLBadValue;
				aux_buffers = (int16_t) attr;
				break;
			case kCGLPFAColorSize:
				attr = *pAttr++;
				if (attr < 0)
					return kCGLBadValue;
				if (attr > color_bits)
					color_bits = attr;
				break;
			case kCGLPFAAlphaSize:
				attr = *pAttr++;
				if (attr < 0)
					return kCGLBadValue;
				if (attr > alpha_bits)
					alpha_bits = attr;
				break;
			case kCGLPFADepthSize:
				attr = *pAttr++;
				if (attr < 0)
					return kCGLBadValue;
				if (attr > depth_bits)
					depth_bits = attr;
				break;
			case kCGLPFAStencilSize:
				attr = *pAttr++;
				if (attr < 0)
					return kCGLBadValue;
				if (attr > stencil_bits)
					stencil_bits = attr;
				break;
			case kCGLPFAAccumSize:
				attr = *pAttr++;
				if (attr < 0)
					return kCGLBadValue;
				if (attr > accum_bits)
					accum_bits = attr;
				break;
			case kCGLPFAMinimumPolicy:
				min_policy = 1;
				break;
			case kCGLPFAMaximumPolicy:
				max_policy = 1;
				break;
			case kCGLPFAOffScreen:
				flags |= 4U; /* bOffScreen */
				break;
			case kCGLPFAFullScreen:
				flags |= 2U; /* bFullScreen */
				break;
			case kCGLPFASampleBuffers:
				attr = *pAttr++;
				if (attr < 0)
					return kCGLBadValue;
				sample_buffers = (int16_t) attr;
				break;
			case kCGLPFASamples:
				attr = *pAttr++;
				if (attr < 0)
					return kCGLBadValue;
				spermsb = (int16_t) attr;
				break;
			case kCGLPFAAuxDepthStencil:
				flags |= 0x800U; /* bAuxDepthStencil */
				break;
			case kCGLPFAColorFloat:
				color_mask = 0x3F000000U; /* kCGLRGBFloat64Bit thru kCGLRGBAFloat256Bit */
				break;
			case kCGLPFAMultisample:
				sample_mode = kCGLMultisampleBit;
				break;
			case kCGLPFASupersample:
				sample_mode = kCGLSupersampleBit;
				break;
			case kCGLPFASampleAlpha:
				sample_alpha = 1;
				break;
			case kCGLPFABackingStore:
				flags |= 8U; /* bBackingStore */
				break;
			case kCGLPFAWindow:
				flags |= 1U; /* bWindow */
				break;
			case kCGLPFADisplayMask:
				attr = *pAttr++;
				displayMask &= (uint32_t) attr;
				break;
			case kCGLPFAPBuffer:
				flags |= 0x2000U; /* bPBuffer */
				break;
			default:
				return kCGLBadAttribute;
		}
		if ((pAttr - attributes) >= 49)
			return kCGLBadAttribute;
	}
	stencil_mode = glrGLIBitsGE(stencil_bits) & 0x3FFFFU;
	depth_mode = glrGLIBitsGE(depth_bits) & 0x3FFFFU;
	if (color_bits <= 32 && alpha_bits <= 8)
		color_mask &= 0xFF0FFFFFU; /* remove kCGLRGB121212Bit thru kCGLRGBA16161616Bit */
	alpha_trimmed_color_mask = 0;
	if (alpha_bits <= 32) {
		alpha_trimmed_color_mask = glrGLIColorsGE(color_bits) & 0x3FFFFFFCU;
		alpha_trimmed_color_mask &= color_mask;
		alpha_trimmed_color_mask &= glrGLIAlphaGE(alpha_bits);
	}
	// FIXME: continues 0x69EE-0x6F74
}

GLDReturn gldChoosePixelFormat(PixelFormat** struct_out, int const* attributes)
{
	typeof(gldChoosePixelFormat) *addr;
	int i;
	GLDReturn rc;
	uint32_t* p;

	GLDLog(2, "%s(struct_out, %p)\n", __FUNCTION__, attributes);

	for (i = 0; attributes[i] != 0; ++i)
		GLDLog(2, "  %s: attribute %d\n", __FUNCTION__, attributes[i]);
	addr = (typeof(addr)) bndl_ptrs[0][2];
	if (addr) {
		rc = addr(struct_out, attributes);
		GLDLog(2, "  %s: returns %d, struct_out is %p\n", __FUNCTION__, rc, *struct_out);
		if (rc != kCGLNoError || *struct_out == 0)
			return rc;
		p = (uint32_t *) (((long*) *struct_out) + 1);
#if 0
		*p = ((*p) & 0xFFFF0000U) | 0x4000U;
		p[1] = 0x501U;
#endif
		for (i = 0; i < 12; ++i)
			GLDLog(2, "  %s: [%d] == %#x\n", __FUNCTION__, i, p[i]);
		return kCGLNoError;
	}
	return -1;
}

GLDReturn gldDestroyPixelFormat(PixelFormat* pixel_format)
{
	GLDLog(2, "%s(%p)\n", __FUNCTION__, pixel_format);

	if (pixel_format)
		free(pixel_format);
	return kCGLNoError;
}

GLDReturn gldCreateShared(gld_shared_t** struct_out, uint32_t GLDisplayMask, long arg2)
{
	uint32_t i, outputCnt;
	kern_return_t kr;
	io_connect_t connect;
	size_t outputStructCnt;
	mach_vm_address_t outputStruct;
	gld_shared_t* p;

	GLDLog(2, "%s(struct_out, %#x, %ld)\n", __FUNCTION__, GLDisplayMask, arg2);

	for (i = 0; i != glr_io_data.num_displays; ++i)
		if (glr_io_data.dinfo[i].mask & GLDisplayMask) {
			if ((~glr_io_data.dinfo[i].mask) & GLDisplayMask)
				break;
			*struct_out = 0;
			connect = 0;
			kr = IOServiceOpen(glr_io_data.dinfo[i].service,
							   mach_task_self(),
							   4 /* Device User-Client */,
							   &connect);
			if (kr != ERR_SUCCESS)
				return kCGLBadCodeModule;
			outputCnt = 0;
			kr = IOConnectCallMethod(connect,
									 kIOVMDeviceCreateShared,
									 0, 0, 0, 0, 0, &outputCnt, 0, 0);
			if (kr != ERR_SUCCESS) {
				IOServiceClose(connect);
				return kCGLBadCodeModule;
			}
			outputStructCnt = sizeof outputStruct;
			kr = IOConnectCallMethod(connect,
									 kIOVMDeviceGetChannelMemory,
									 0, 0, 0, 0, 0, 0, &outputStruct, &outputStructCnt);
			if (kr != ERR_SUCCESS) {
				IOServiceClose(connect);
				return kCGLBadCodeModule;
			}
			p = malloc(sizeof *p);
			GLDLog(2, "  %s: shared @%p\n", __FUNCTION__, p);
			p->arg2 = arg2;
			p->dinfo = &glr_io_data.dinfo[i];
			p->obj = connect;
			p->config0 = glr_io_data.dinfo[i].config[0];
			pthread_mutex_init(&p->mutex, NULL);
			p->needs_locking = 0;
			p->num_contexts = 1;
			p->f2 = 0;
			p->f3 = 0;
			p->f4 = 0;
			p->f5 = 0;
			p->f6 = libglimage.glg_processor_default_data;
			bzero(&p->f7, sizeof p->f7);
			p->pp_list = 0;
			bzero(&p->t0, sizeof p->t0);
			bzero(&p->t1, sizeof p->t1);
			glrInitializeHardwareShared(p, (void*) (uintptr_t) outputStruct);	/* hope this is safe, Intel GMA 950 does it */
			*struct_out = p;
			return kCGLNoError;
		}
	return kCGLBadDisplay;
}

GLDReturn gldDestroyShared(gld_shared_t* shared)
{
	int i;

	GLDLog(2, "%s(%p)\n", __FUNCTION__, shared);

	for (i = 0; i != 7; ++i) {
		if (shared->t0[i]) {
			glrDestroyZeroTexture(shared, shared->t0[i]);
			shared->t0[i] = 0;
		}
		if (shared->t1[i]) {
			glrDestroyZeroTexture(shared, shared->t1[i]);
			shared->t1[i] = 0;
		}
	}
	glrDestroyHardwareShared(shared);
	IOServiceClose(shared->obj);
	if (libglimage.glgTerminateProcessor)
		libglimage.glgTerminateProcessor(&shared->f3);
	pthread_mutex_destroy(&shared->mutex);
	free(shared);
	return kCGLNoError;
}

static
GLDReturn gldCreateContextInternal(gld_context_t** struct_out,
						   PixelFormat* pixel_format,
						   gld_shared_t* shared,
						   void* arg3,
						   void* arg4,
						   void* arg5)
{
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1060
	uint64_t input;
	struct sIOGLGetCommandBuffer outputStruct;
#endif
	display_info_t* dinfo;
	gld_context_t* context;
	GLDReturn rc;
	kern_return_t kr;
	uint32_t disp_num;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1060
	size_t outputStructCnt;
#endif

	GLDLog(2, "%s(struct_out, %p, %p, %p, %p, %p)\n", __FUNCTION__, pixel_format, shared, arg3, arg4, arg5);

	*struct_out = 0;
	rc = glrValidatePixelFormat(pixel_format);
	if (rc != kCGLNoError)
		return rc;
	dinfo = shared->dinfo;
	if (!(dinfo->mask & pixel_format->displayMask) ||
		(~(dinfo->mask) & pixel_format->displayMask))
		return kCGLBadDisplay;
	context = (gld_context_t*) malloc(sizeof *context);
	GLDLog(2, "  %s: context @%p\n", __FUNCTION__, context);
	for (disp_num = 0; disp_num != glr_io_data.lastDisplay; ++disp_num)
		if (pixel_format->displayMask & (1U << disp_num))
			break;
	context->display_num = disp_num;
	context->context_obj = 0;
	kr = IOServiceOpen(glr_io_data.pServices[disp_num],
					   mach_task_self(),
					   1 /* GL Context User-Client */,
					   &context->context_obj);
	if (kr != ERR_SUCCESS) {
		free(context);
		return kCGLBadCodeModule;
	}
	context->command_buffer_ptr = 0;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1060
	input = 0;
	outputStructCnt = sizeof outputStruct;
	kr = IOConnectCallMethod(context->context_obj,
							 kIOVMGLSubmitCommandBuffer,
							 &input, 1,
							 0, 0, 0, 0,
							 &outputStruct, &outputStructCnt);
	context->command_buffer_ptr = (uint32_t*) (uintptr_t) outputStruct.addr[0];	// Note: truncation in 32-bits
	context->command_buffer_size = outputStruct.len[0];
#endif
	if (kr != ERR_SUCCESS) {
#if 0
		glrKillClient(kr);
#endif
		IOServiceClose(context->context_obj);
		free(context);
		return kCGLBadCodeModule;
	}
	context->mem1_addr = 0;
	kr = IOConnectMapMemory(context->context_obj,
							1,
							mach_task_self(),
#ifdef __LP64__
							(mach_vm_address_t*) &context->mem1_addr,
							(mach_vm_size_t*) &context->mem1_size,
#else
							(vm_address_t*) &context->mem1_addr,
							(vm_size_t*) &context->mem1_size,
#endif
							kIOMapAnywhere);
	if (kr != ERR_SUCCESS) {
		IOServiceClose(context->context_obj);
		free(context);
		return kCGLBadCodeModule;
	}
	context->mem2_addr = 0;
	kr = IOConnectMapMemory(context->context_obj,
							2,
							mach_task_self(),
#ifdef __LP64__
							(mach_vm_address_t*) &context->mem2_addr,
							(mach_vm_size_t*) &context->mem2_size,
#else
							(vm_address_t*) &context->mem2_addr,
							(vm_size_t*) &context->mem2_size,
#endif
							kIOMapAnywhere);
	if (kr != ERR_SUCCESS) {
		IOServiceClose(context->context_obj);
		free(context);
		return kCGLBadCodeModule;
	}
	pthread_mutex_lock(&shared->mutex);
	kr = IOConnectAddClient(context->context_obj, shared->obj);
	if (kr != ERR_SUCCESS) {
		pthread_mutex_unlock(&shared->mutex);
		IOServiceClose(context->context_obj);
		free(context);
		return kCGLBadCodeModule;
	}
	++shared->num_contexts;
	if (shared->num_contexts > 2)		// FIXME shouldn't this be > 1 ???
		shared->needs_locking = 1;
	pthread_mutex_unlock(&shared->mutex);
	context->config0 = shared->config0;
	context->vramSize = dinfo->config[2];
	context->mem2_local = malloc(context->mem2_size >> 5);
	bzero(context->mem2_local, context->mem2_size >> 5);
	context->f20 = 0;
	context->f21 = 0;
	context->f22 = 0;
	context->f23 = 0;
	context->f24 = 0;
	context->flags1[0] = 0x40U;
	bzero(&context->f26, sizeof context->f26);
	context->gpdd = libglimage.glg_processor_default_data;
	context->shared = shared;
	context->arg4 = arg4;
	context->arg5 = arg5;
	context->arg3 = 0;
	context->f0[0] = 0;
	context->f0[1] = 0;
	context->context_mode_bits = 0;
	context->f1 = 0;
	context->client_data = 0;
	context->f3[3] = 0;
	context->f3[4] = 0;
	context->f3[5] = 0;
	context->f3[6] = 0;
	context->f3[7] = 25;
	context->f3[8] = 50;
	context->f3[1] = 0;
	context->f3[2] = 0;
	context->f3[9] = 0;
	context->f3[10] = 0;
	context->flags3[7] = 0;
	context->f3[11] = 0;
	context->f3[12] = 0;
	context->f3[13] = 0;
	context->f3[14] = 0;
	context->flags3[4] = 0;
	context->flags3[2] = 0;
	context->flags3[6] = 0;
	context->flags3[5] = 0;
	context->flags3[3] = 0;
	context->f5 = 0;
	context->f6 = 0;
	context->flags3[0] = 0;
	context->f25 = 0;
	context->flags2[0] = 1;
	context->flags1[3] = 0;
	context->pad_size = 0;
	context->pad_addr = 0;
	context->f11 = 0;
	context->f12[0] = 0;
	context->flags1[2] = 0;
	context->f10 = 3;
	context->flags1[1] = (pixel_format->bufferModes & kCGLDoubleBufferBit) != 0;
	if (pixel_format->bAuxDepthStencil)
		context->context_mode_bits = CGLCMB_AuxDepthStencil;
	if (pixel_format->bufferModes & kCGLDoubleBufferBit)
		context->context_mode_bits |= CGLCMB_DoubleBuffer;
	else if (!pixel_format->bFullScreen)
		context->context_mode_bits |= CGLCMB_FullScreen;
	if (pixel_format->bufferModes & kCGLStereoscopicBit)
		context->context_mode_bits |= CGLCMB_Stereoscopic;
	context->context_mode_bits |= xlateColorMode(pixel_format->colorModes);
	if (pixel_format->depthModes == kCGL0Bit)
		context->context_mode_bits |= CGLCMB_DepthMode0;
	if (pixel_format->stencilModes == kCGL0Bit)
		context->context_mode_bits |= CGLCMB_StencilMode0;
	if (pixel_format->accumModes == kCGLARGB8888Bit)
		context->flags3[1] = 1;
	else if (pixel_format->accumModes == kCGLRGBA16161616Bit)
		context->flags3[1] = 2;
	else
		context->flags3[1] = 0;
	if (pixel_format->auxBuffers == 1)
		context->context_mode_bits |= 0x100U;
	else if (pixel_format->auxBuffers == 2)
		context->context_mode_bits |= 0x200U;
	if (pixel_format->sampleBuffers > 0)
		context->context_mode_bits |= CGLCMB_HaveSampleBuffers;
	context->flags2[1] = 0;
	if (pixel_format->bBackingStore) {
		context->context_mode_bits |= CGLCMB_BackingStore;
		context->flags2[1] = 1;
	}
	glrSetWindowModeBits(&context->context_mode_bits, pixel_format);
	context->f1 = 0x4000U;
	if (context->context_mode_bits & CGLCMB_DepthMode0)
		context->f1 = 0x4100U;
	if (context->context_mode_bits & 0x80000000U)
		context->f1 | 0x400U;
	if (context->flags3[1])
		context->f1 | 0x200U;
	context->f8[0] = context->context_mode_bits;
	context->f8[1] = context->f1;
	memcpy(&context->f9[0], &context->f3[3], 3 * sizeof(uint64_t));
	context->f7[0] = 0;
	context->f7[1] = 0;
	bzero(&context->ptr_pack[0], sizeof context->ptr_pack);
	bzero(&context->f13[0], sizeof context->f13);
	bzero(&context->f14[0], sizeof context->f14);
	glrInitializeHardwareState(context, pixel_format);
	if (arg3) {
		((uint32_t *) arg3)[29] = ((context->config0 & 2) ^ 2) + 1;
		glrSetConfigData(context, arg3, pixel_format);
		context->arg3 = arg3;
	}
	*struct_out = context;
	return rc;
}

GLDReturn gldCreateContext(gld_context_t** struct_out,
						   PixelFormat* pixel_format,
						   gld_shared_t* shared,
						   void* arg3,
						   void* arg4,
						   void* arg5)
{
	typeof(gldCreateContext) *addr;
	GLDReturn rc;

	GLDLog(2, "%s(struct_out, %p, %p, %p, %p, %p)\n", __FUNCTION__, pixel_format, shared, arg3, arg4, arg5);
	addr = (typeof(addr)) bndl_ptrs[bndl_index][6];
	if (addr) {
		rc = addr(struct_out, pixel_format, shared, arg3, arg4, arg5);
		GLDLog(2, "  %s: returns %d, struct_out is %p\n", __FUNCTION__, rc, *struct_out);
		return rc;
	}
	return -1;
}

GLDReturn gldReclaimContext(gld_context_t* context)
{
	typeof(gldReclaimContext) *addr;

	GLDLog(2, "%s(%p)\n", __FUNCTION__, context);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][7];
	if (addr)
		return addr(context);
	return -1;
}

GLDReturn gldDestroyContext(gld_context_t* context)
{
	typeof(gldDestroyContext) *addr;

	GLDLog(2, "%s(%p)\n", __FUNCTION__, context);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][8];
	if (addr)
		return addr(context);
	return -1;
}

static
GLDReturn gldAttachDrawableInternal(gld_context_t* context, int surface_type, uint32_t const* client_data, uint32_t options)
{
	GLDReturn rc = kCGLNoError;
	int r12d;
	uint32_t dr_options_hi /* var_d0 */, dr_options_lo /* var_cc */;
	uint64_t input[4];	// at var_60
	uint64_t output;	// at var_40
	size_t outputStructCnt; // at var_38
	kern_return_t kr;
	struct sIOGLContextSetSurfaceData inputStruct; // at var_90
	struct sIOGLContextGetConfigStatus outputStruct;  // at var_c0

	// int var_d4 = surface_type
	// r13 = client_data
	if (!context)
		return kCGLBadAddress;
	dr_options_hi = 0; dr_options_lo = 0;
	switch (surface_type) {
		case kCGLPFAPBuffer:
			dr_options_hi = (options & 0xFF00U) >> 8;
			dr_options_lo = options & 0xFFU;
		case kCGLPFAWindow:
			if (!client_data)
				return kCGLBadDrawable;
			r12d = 0;
			// var_e0 = client_data + 2;	// points to surface_id
			// var_e8 pts to inputStruct
			// var_f0 pts to outputStructCnt
			// var_f8 pts to outputStruct
			// var_100 pts to output
			// var_108 pts to input
			context->context_mode_bits = (context->context_mode_bits | CGLCMB_Windowed) & 0xFF80FFFFU;
			context->flags3[3] = (surface_type != kCGLPFAPBuffer);
			if (surface_type == kCGLPFAWindow) {
				/*
				 * Note: IODataBindSurface is a client callback that
				 *   creates a CGSSurface with the given surface_id
				 */
				if (glr_io_data.IODataBindSurface(client_data[0],
												  client_data[1],
												  &client_data[2],	// surface_id
												  context->context_mode_bits & 0x7F803FU,
												  glr_io_data.pServices[context->display_num]) != kCGLNoError)
					goto cleanup_bd;
			} else {
				if (!client_data[2])
					goto cleanup_bd;
			}
			inputStruct.surface_id = client_data[2];
			inputStruct.context_mode_bits = context->context_mode_bits & 0xFF803FC0U;
			if (r12d)
				inputStruct.surface_mode = 0xFFFFFFFFU;
			else
				inputStruct.surface_mode = context->context_mode_bits & 15U;
			inputStruct.dr_options_hi = dr_options_hi;
			inputStruct.dr_options_lo = dr_options_lo;
			inputStruct.volatile_state = context->flags3[7];
			inputStruct.set_scale = context->flags3[5];
			inputStruct.scale_options = context->f3[15];
			inputStruct.scale_width = context->f3[16];
			inputStruct.scale_height = context->f3[17];
			outputStructCnt = sizeof outputStruct;
			kr = IOConnectCallMethod(context->context_obj,
									 kIOVMGLSetSurfaceGetConfigStatus,
									 0, 0,
									 &inputStruct, sizeof inputStruct,
									 0, 0,
									 &outputStruct, &outputStructCnt);
			if (kr != ERR_SUCCESS) {
				if (r12d)
					goto cleanup_bd;
				outputStructCnt = 3;
				kr = IOConnectCallMethod(context->context_obj,
										 kIOVMGLGetSurfaceInfo,
										 &output, 1,
										 0, 0,
										 &input[0], (uint32_t*) &outputStructCnt,
										 0, 0);
				// TBD: 53F7
			}
			context->config0 = outputStruct.config[0];
			context->vramSize = outputStruct.config[1];
			// var_c4 = outputStruct.inner_width;
			// var_c8 = outputStruct.inner_height;
			// r12d   = outputStruct.outer_width;
			// r15d   = outputStruct.outer_height;
			// r14d   = outputStruct.status;
			context->context_mode_bits =
				(context->context_mode_bits & 0xFF80FFEFU) |
				(outputStruct.surface_mode_bits & 0x7F0010U);
			/*
			 * Note: 0x10U is Stereo bit
			 */
			((uint8_t*) context->arg3)[46] = ((outputStruct.surface_mode_bits & 0x10U) != 0);
			if (!glrSanitizeWindowModeBits(context->context_mode_bits))
				return kCGLBadDrawable;
			glrDrawableChanged(context);
			if (outputStruct.status) {
				if (context->client_data == client_data) {
					if (context->f3[3] == outputStruct.inner_width &&
						context->f3[4] == outputStruct.inner_height) {
						rc = kCGLNoError;
						goto skip_copy;
					}
					rc = 3;
				} else
					rc = 2;
			} else
				rc = kCGLBadAlloc;
			context->f3[3]  = outputStruct.inner_width;
			context->f3[4]  = outputStruct.inner_height;
			context->f3[9]  = outputStruct.outer_width;
			context->f3[10] = outputStruct.outer_height;
		skip_copy:
			if (context->flags3[1]) {
				uint32_t bpp = context->flags3[1] == 1 ? 4U : 8U;
				uint32_t w = (outputStruct.inner_width + 7U) & ~7U;
				uint32_t total = w * outputStruct.inner_height * bpp;
				if (context->f6 != total) {
					if (context->f5)
						free(context->f5);
					context->f5 = malloc(total);
					context->f6 = total;
				}
			}
			glrReleaseDrawable(context);
			context->client_data = (void*) client_data;
			context->f3[0] = surface_type;
			context->f3[1] = dr_options_hi;
			context->f3[2] = dr_options_lo;
			/*
			 * Note: arg5 seems to be a pointer table
			 */
			if (!((uintptr_t const*) context->arg5)[139]) {
				context->f8[0] = context->context_mode_bits;
				memcpy(&context->f9[0], &context->f3[3], 3 * sizeof(uint64_t));
				context->flags2[0] = context->flags3[3];
				if (context->flags3[3])
					((uint8_t*) context->arg3)[32] |= 2U;
				else
					((uint8_t*) context->arg3)[32] &= ~2U;
			}
			return rc;
		case 54:
			if (!client_data)
				return kCGLBadDrawable;
			/* TBD: 5582 */
			break;
		case 53:
			goto cleanup_bd;
		case 0:
			if (client_data)
				goto cleanup_bd;
			else {
				rc = 1;
				goto cleanup;
			}
		default:
			/* TBD: 5A8E */
			break;
	}

cleanup_bd:
	rc = kCGLBadDrawable;
cleanup:
	glrReleaseDrawable(context);
	bzero(&input[0], sizeof input);
	IOConnectCallMethod(context->context_obj,
						kIOVMGLSetSurface,
						&input[0], 4,
						0, 0, 0, 0, 0, 0);
	return rc;
}

GLDReturn gldAttachDrawable(gld_context_t* context, int surface_type, uint32_t const* client_data, uint32_t options)
{
	typeof(gldAttachDrawable) *addr;

	GLDLog(2, "%s(%p, %d, %p, %#x)\n", __FUNCTION__, context, surface_type, client_data, options);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][9];
	if (addr)
		return addr(context, surface_type, client_data, options);
	return -1;
}

/*
 * Notes:
 *   dispatch_table - pointer to 33-entry dispatch table.
 *     table is prefilled with pointers to gliDispatchNoop, which returns 0.
 *   data_out - array of 6 uint32_t, filled from context->f3[3]
 */
GLDReturn gldInitDispatch(gld_context_t* context, GLD_GENERIC_DISPATCH* dispatch_table, uint32_t* data_out)
{
	typeof(gldInitDispatch) *addr;
	int rc;

	GLDLog(2, "%s(%p, %p, %p)\n", __FUNCTION__, context, dispatch_table, data_out);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][10];
	if (!addr)
		return -1;
	rc = addr(context, &remote_dispatch[0], data_out);
	dispatch_table[0] = (GLD_GENERIC_DISPATCH) glrAccum;
	dispatch_table[1] = glrClear;
	dispatch_table[2] = (GLD_GENERIC_DISPATCH) glrReadPixels;
	dispatch_table[3] = glrDrawPixels;
	dispatch_table[4] = glrCopyPixels;
	dispatch_table[5] = glrRenderBitmap;
	dispatch_table[6] = glrRenderPoints;
	dispatch_table[7] = glrRenderLines;
	dispatch_table[8] = glrRenderLineStrip;
	dispatch_table[9] = glrRenderLineLoop;
	dispatch_table[10] = glrRenderPolygon;
	dispatch_table[11] = glrRenderTriangles;
	dispatch_table[12] = glrRenderTriangleFan;
	dispatch_table[13] = glrRenderTriangleStrip;
	dispatch_table[14] = glrRenderQuads;
	dispatch_table[15] = glrRenderQuadStrip;
	dispatch_table[16] = 0;
	dispatch_table[17] = 0;
	dispatch_table[18] = 0;
	dispatch_table[19] = 0;
	dispatch_table[20] = glrRenderPointsPtr;
	dispatch_table[21] = glrRenderLinesPtr;
	dispatch_table[22] = glrRenderPolygonPtr;
	dispatch_table[23] = glrBeginPrimitiveBuffer;
	dispatch_table[24] = glrEndPrimitiveBuffer;
	dispatch_table[25] = 0;
	dispatch_table[26] = 0;
	dispatch_table[27] = glrHookFinish;
	dispatch_table[28] = glrHookFlush;
	dispatch_table[29] = glrHookSwap;
	dispatch_table[30] = glrSetFence;
	dispatch_table[31] = (GLD_GENERIC_DISPATCH) glrNoopRenderVertexArray;
	dispatch_table[32] = 0;
	return rc;
}

GLDReturn gldUpdateDispatch(gld_context_t* context, GLD_GENERIC_DISPATCH* dispatch_table, uint32_t* data_out)
{
	typeof(gldUpdateDispatch) *addr;
	GLDReturn rc;

	GLDLog(2, "%s(%p, %p, %p)\n", __FUNCTION__, context, dispatch_table, data_out);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][11];
	if (addr) {
		cb_chkpt(context, 0);
		rc = addr(context, &remote_dispatch[0], data_out);
		cb_chkpt(context, 1);
		return rc;
	}
	return -1;
}

char const* gldGetString(uint32_t GLDisplayMask, int string_code)
{
	char const* r = 0;
	uint32_t i;

	GLDLog(2, "%s(%#x, %#x)\n", __FUNCTION__, GLDisplayMask, string_code);

	for (i = 0; i != glr_io_data.num_displays; ++i)
		if (glr_io_data.dinfo[i].mask & GLDisplayMask) {
			if ((~glr_io_data.dinfo[i].mask) & GLDisplayMask)
				break;
			r = glrGetString(&glr_io_data.dinfo[i], string_code);
			break;
		}
	GLDLog(2, "  %s returns %s\n", __FUNCTION__, r ? : "NULL");
	return r;
}

GLDReturn gldGetError(gld_context_t* context)
{
	GLDReturn rc;

	GLDLog(2, "%s(%p)\n", __FUNCTION__, context);

	rc = (GLDReturn) context->f11;
	context->f11 = kCGLNoError;
	return rc;
}

GLDReturn gldSetInteger(gld_context_t* context, int arg1, void* arg2)
{
	typeof(gldSetInteger) *addr;

	GLDLog(2, "%s(%p, %d, %p)\n", __FUNCTION__, context, arg1, arg2);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][14];
	if (addr)
		return addr(context, arg1, arg2);
	return -1;
}

GLDReturn gldGetInteger(gld_context_t* context, int arg1, void* arg2)
{
	typeof(gldGetInteger) *addr;

	GLDLog(2, "%s(%p, %d, %p)\n", __FUNCTION__, context, arg1, arg2);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][15];
	if (addr)
		return addr(context, arg1, arg2);
	return -1;
}

void gldFlush(gld_context_t* context)
{
	GLDLog(2, "%s(%p)\n", __FUNCTION__, context);

	if (context->command_buffer_ptr + 8 < context->cb_iter[1])
		SubmitPacketsToken(context, 1);
}

void gldFinish(gld_context_t* context)
{
	GLDLog(2, "%s(%p)\n", __FUNCTION__, context);

	if (context->command_buffer_ptr + 8 < context->cb_iter[1])
		SubmitPacketsToken(context, 1);
	IOConnectCallMethod(context->context_obj,
						kIOVMGLFinish,
						0, 0, 0, 0, 0, 0, 0, 0);
}

GLDReturn gldTestObject(gld_context_t* context, int object_type, void const* object)
{
	typeof(gldTestObject) *addr;

	GLDLog(2, "%s(%p, %d, %p)\n", __FUNCTION__, context, object_type, object);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][18];
	if (addr)
		return addr(context, object_type, object);
	return -1;
}

GLDReturn gldFlushObject(gld_context_t* context, int object_type, int arg2, void const* object)
{
	gld_texture_t const* object_1;
	gld_buffer_t const* object_3;

	GLDLog(2, "%s(%p, %d, %d, %p)\n", __FUNCTION__, context, object_type, arg2, object);

	switch (object_type) {
		case 1:
			object_1 = (gld_texture_t const*) object;
			if (object_1->obj)
				glrFlushSysObject(context, object_1->obj, arg2);
			break;
		case 3:
			object_3 = (gld_buffer_t const*) object;
			if (object_3->f2 && *(object_3->f2))
				glrFlushSysObject(context, *(object_3->f2), arg2);
			break;
		default:
			return kCGLBadEnumeration;
	}
	return kCGLNoError;
}

GLDReturn gldFinishObject(gld_context_t* context, int object_type, void const* object)
{
	typeof(gldFinishObject) *addr;

	GLDLog(2, "%s(%p, %d, %p)\n", __FUNCTION__, context, object_type, object);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][20];
	if (addr)
		return addr(context, object_type, object);
	return -1;
}

GLDReturn gldWaitObject(gld_shared_t* shared, int object_type, void const* object, void const* arg3)
{
	gld_texture_t const* object_1;
	gld_buffer_t const* object_3;

	GLDLog(2, "%s(%p, %d, %p, %p)\n", __FUNCTION__, shared, object_type, object, arg3);

	switch (object_type) {
		case 1:
			object_1 = (gld_texture_t const*) object;
			if (object_1->obj)
				glrWaitSharedObject(shared, object_1->obj);
			break;
		case 3:
			object_3 = (gld_buffer_t const*) object;
			if (object_3->f2 && *(object_3->f2))
				glrWaitSharedObject(shared, *(object_3->f2));
			break;
		default:
			return kCGLBadEnumeration;
	}
	return kCGLNoError;
}

GLDReturn gldCreateTexture(gld_shared_t* shared, gld_texture_t** texture, void* client_texture)
{
	GLDLog(2, "%s(%p, struct_out, %p)\n", __FUNCTION__, shared, client_texture);

	gld_texture_t* p = (gld_texture_t*) calloc(1 , sizeof(gld_texture_t));
	GLDLog(2, "  %s: gld_texture @%p\n", __FUNCTION__, p);
	p->client_texture = client_texture;
	p->f11 = 0;
	p->obj = 0;
	p->raw_data = 0;
	p->tds.type = 8U;
	p->tds.size[0] = 0;
	p->tds.size[1] = 0;
	p->f6 = &p->f7;
	p->f7 = 0;
	p->f8 = &p->f6;
	p->f14 = 3;
	*texture = p;
	return kCGLNoError;
}

int gldIsTextureResident(gld_shared_t* shared, gld_texture_t* texture)
{
	GLDLog(2, "%s(%p, %p)\n", __FUNCTION__, shared, texture);

	return texture->obj != 0;
}

GLDReturn gldModifyTexture(gld_shared_t* shared, gld_texture_t* texture, uint8_t arg2, int arg3, uint8_t arg4)
{
	uint64_t input;
	gld_sys_object_t* sys_obj;

	GLDLog(2, "%s(%p, %p, %u, %d, %u)\n", __FUNCTION__, shared, texture, arg2, arg3, arg4);

	texture->f14 |= arg2;
	if (arg2 & 4)
		texture->f9[arg3] |= (1U << arg4);
	if (!(arg2 & 1))
		return kCGLNoError;
	texture->f13 = 0;
	sys_obj = texture->obj;
	if (!sys_obj)
		return kCGLNoError;
	switch (sys_obj->type) {
		case 6:
		case 9:
			if (shared->needs_locking)
				pthread_mutex_lock(&shared->mutex);
			if (__sync_fetch_and_add(&sys_obj->refcount, 0-0x10000) == 0x10000) {
				input = (uint64_t) sys_obj->object_id;
				IOConnectCallMethod(shared->obj,
									kIOVMDeviceDeleteTexture,
									&input, 1,
									0, 0, 0, 0, 0, 0);
			}
			texture->obj = 0;
			if (shared->needs_locking)
				pthread_mutex_unlock(&shared->mutex);
			break;
	}
	return kCGLNoError;
}

GLDReturn gldGenerateTexMipmaps(gld_shared_t* shared, gld_texture_t* texture, int arg2)
{
	typeof(gldGenerateTexMipmaps) *addr;

	GLDLog(2, "%s(%p, %p, %d)\n", __FUNCTION__, shared, texture, arg2);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][75];
	if (addr)
		return addr(shared, texture, arg2);
	return -1;
}

GLDReturn gldLoadTexture(gld_context_t* context, gld_texture_t* texture)
{
	typeof(gldLoadTexture) *addr;

	GLDLog(2, "%s(%p, %p)\n", __FUNCTION__, context, texture);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][25];
	if (addr)
		return addr(context, texture);
	return -1;
}

void gldUnbindTexture(gld_context_t* context, gld_texture_t* texture)
{
	typeof(gldUnbindTexture) *addr;

	GLDLog(2, "%s(%p, %p)\n", __FUNCTION__, context, texture);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][26];
	if (addr)
		addr(context, texture);
}

void gldReclaimTexture(gld_shared_t* shared, gld_texture_t* texture)
{
	typeof(gldReclaimTexture) *addr;

	GLDLog(2, "%s(%p, %p)\n", __FUNCTION__, shared, texture);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][27];
	if (addr)
		addr(shared, texture);
}

void gldDestroyTexture(gld_shared_t* shared, gld_texture_t* texture)
{
	GLDLog(2, "%s(%p, %p)\n", __FUNCTION__, shared, texture);

	gldReclaimTexture(shared, texture);
	free(texture);
}

/*
 * It's a mad mad mad world
 */
GLDReturn gldCopyTexSubImage(void* arg0,
							 void* arg1,
							 int arg2,
							 int arg3,
							 int arg4,
							 int arg5,
							 int arg6,
							 int arg7,
							 int arg8,
							 int arg9,
							 int arg10)
{
	typeof(gldCopyTexSubImage) *addr;

	GLDLog(2, "%s(%p, %p, %d, %d, %d, %d, %d, %d, %d, %d, %d)\n", __FUNCTION__,
		   arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][76];
	if (addr)
		return addr(arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10);
	return -1;
}

GLDReturn gldModifyTexSubImage(void* arg0,
							   void* arg1,
							   int arg2,
							   int arg3,
							   int arg4,
							   int arg5,
							   int arg6,
							   int arg7,
							   int arg8,
							   int arg9,
							   int arg10,
							   int arg11,
							   int arg12,
							   int arg13,
							   int arg14)
{
	typeof(gldModifyTexSubImage) *addr;

	GLDLog(2, "%s(%p, %p, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d)\n", __FUNCTION__,
		   arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10, arg11, arg12, arg13, arg14);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][77];
	if (addr)
		return addr(arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10, arg11, arg12, arg13, arg14);
	return -1;
}

GLD_DEFINE_GENERIC(gldCreateTextureLevel, 29)

GLDReturn gldGetTextureLevelInfo(void* arg0, void* arg1, void* arg2, int arg3, int arg4, void* arg5)
{
	typeof(gldGetTextureLevelInfo) *addr;

	GLDLog(2, "%s(%p, %p, %p, %d, %d, %p)\n", __FUNCTION__, arg0, arg1, arg2, arg3, arg4, arg5);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][30];
	if (addr)
		return addr(arg0, arg1, arg2, arg3, arg4, arg5);
	return -1;
}

GLDReturn gldGetTextureLevelImage(void* arg0, void* arg1, int arg2, int arg3)
{
	typeof(gldGetTextureLevelImage) *addr;

	GLDLog(2, "%s(%p, %p, %d, %d)\n", __FUNCTION__, arg0, arg1, arg2, arg3);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][31];
	if (addr)
		return addr(arg0, arg1, arg2, arg3);
	return -1;
}

GLD_DEFINE_GENERIC(gldModifyTextureLevel, 32)
GLD_DEFINE_GENERIC(gldDestroyTextureLevel, 33)

GLDReturn gldCreateBuffer(gld_shared_t* shared, gld_buffer_t** struct_out, void* arg2, void* arg3)
{
	gld_buffer_t* p;

	GLDLog(2, "%s(%p, struct_out, %p, %p)\n", __FUNCTION__, shared, arg2, arg3);

	p = calloc(1, sizeof *p);
	GLDLog(2, "  %s: buffer @%p\n", __FUNCTION__, p);
	p->f0 = arg2;
	p->f1 = arg3;
	p->f2 = 0;
	*struct_out = p;
	return kCGLNoError;
}

GLDReturn gldBufferSubData(void)
{
	GLDLog(2, "%s()\n", __FUNCTION__);
	return kCGLNoError;
}

GLDReturn gldLoadBuffer(void)
{
	GLDLog(2, "%s()\n", __FUNCTION__);
	return kCGLNoError;
}

GLDReturn gldFlushBuffer(gld_shared_t* shared, gld_buffer_t* buffer, void* arg2, int arg3)
{
	uint8_t* p;

	GLDLog(2, "%s(%p, %p, %p, %d)\n", __FUNCTION__, shared, buffer, arg2, arg3);
	p = buffer->f1;
	if ((*p) & 1U)
		return kCGLNoError;
	if (arg2)
		glrFlushMemory(0, arg2, arg3);
	*p &= (~2);
	return kCGLNoError;
}

void gldPageoffBuffer(gld_shared_t* shared, void* arg1, gld_buffer_t* buffer)
{
	typeof(gldPageoffBuffer) *addr;

	GLDLog(2, "%s(%p, %p, %p)\n", __FUNCTION__, shared, arg1, buffer);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][37];
	if (addr)
		addr(shared, arg1, buffer);
}

void gldUnbindBuffer(gld_context_t* context, gld_buffer_t* buffer)
{
	GLDLog(2, "%s(%p, %p)\n", __FUNCTION__, context, buffer);
	if (buffer->f2 &&
		*(buffer->f2) &&
		(*(buffer->f2))->refcount != 0x10000)
		gldFlush(context);
}

void gldReclaimBuffer(gld_shared_t* shared, gld_buffer_t* buffer)
{
	GLDLog(2, "%s(%p, %p)\n", __FUNCTION__, shared, buffer);
	if (buffer->f2)
		gldDestroyMemoryPlugin(shared, buffer->f2);
	buffer->f2 = 0;
}

GLDReturn gldDestroyBuffer(gld_shared_t* shared, gld_buffer_t* buffer)
{
	GLDLog(2, "%s(%p, %p)\n", __FUNCTION__, shared, buffer);
	gldReclaimBuffer(shared, buffer);
	free(buffer);
	return kCGLNoError;
}

void gldGetMemoryPlugin(void* arg0, void* arg1, void** struct_out)
{
	typeof(gldGetMemoryPlugin) *addr;

	GLDLog(2, "%s(%p, %p, struct_out)\n", __FUNCTION__, arg0, arg1);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][41];
	if (addr)
		addr(arg0, arg1, struct_out);
}

void gldSetMemoryPlugin(void* arg0, void* arg1, void** struct_out)
{
	typeof(gldSetMemoryPlugin) *addr;

	GLDLog(2, "%s(%p, %p, struct_out)\n", __FUNCTION__, arg0, arg1);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][42];
	if (addr)
		addr(arg0, arg1, struct_out);
}

int gldTestMemoryPlugin(void* arg0, void* arg1)
{
	typeof(gldTestMemoryPlugin) *addr;

	GLDLog(2, "%s(%p, %p\n", __FUNCTION__, arg0, arg1);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][43];
	if (addr)
		return addr(arg0, arg1);
	return -1;
}

void gldFlushMemoryPlugin(void* arg0, void* arg1)
{
	typeof(gldFlushMemoryPlugin) *addr;

	GLDLog(2, "%s(%p, %p\n", __FUNCTION__, arg0, arg1);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][44];
	if (addr)
		addr(arg0, arg1);
}

void gldDestroyMemoryPlugin(void* arg0, void* arg1)
{
	typeof(gldDestroyMemoryPlugin) *addr;

	GLDLog(2, "%s(%p, %p\n", __FUNCTION__, arg0, arg1);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][45];
	if (addr)
		addr(arg0, arg1);
}

GLDReturn gldCreateFramebuffer(gld_shared_t * shared, gld_framebuffer_t** struct_out, void* arg2, void* arg3)
{
	gld_framebuffer_t* p;

	GLDLog(2, "%s(%p, struct_out, %p, %p)\n", __FUNCTION__, shared, arg2, arg3);

	p = calloc(1, sizeof *p);
	GLDLog(2, "  %s: framebuffer @%p\n", __FUNCTION__, p);
	p->f0 = arg2;
	p->f1 = arg3;
	*struct_out = p;
	return kCGLNoError;
}

void gldUnbindFramebuffer(gld_context_t* context, gld_framebuffer_t* framebuffer)
{
	typeof(gldUnbindFramebuffer) *addr;

	GLDLog(2, "%s(%p, %p)\n", __FUNCTION__, context, framebuffer);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][47];
	if (addr)
		addr(context, framebuffer);
}

void gldReclaimFramebuffer(void)
{
	GLDLog(2, "%s()\n", __FUNCTION__);
}

void gldDiscardFramebuffer(void)
{
	GLDLog(2, "%s()\n", __FUNCTION__);
}

GLDReturn gldDestroyFramebuffer(gld_shared_t * shared, gld_framebuffer_t* framebuffer)
{
	GLDLog(2, "%s(%p, %p)\n", __FUNCTION__, shared, framebuffer);
	free(framebuffer);
	return kCGLNoError;
}

GLDReturn gldCreatePipelineProgram(gld_shared_t* shared, gld_pipeline_program_t** struct_out, void* arg2)
{
	gld_pipeline_program_t* p;

	GLDLog(2, "%s(%p, struct_out, %p)\n", __FUNCTION__, shared, arg2);

	p = calloc(1, sizeof *p);
	GLDLog(2, "  %s: pipeline_program @%p\n", __FUNCTION__, p);
	p->f1 = 3;
	p->arg2 = arg2;
	bzero(&p->f3, sizeof p->f3);
	if (shared->pp_list)
		shared->pp_list->prev = p;
	p->next = shared->pp_list;
	shared->pp_list = p;
	*struct_out = p;
	return kCGLNoError;
}

GLDReturn gldGetPipelineProgramInfo(gld_shared_t* shared, gld_pipeline_program_t* pp, int arg2, void* arg3)
{
	typeof(gldGetPipelineProgramInfo) *addr;

	GLDLog(2, "%s(%p, %p, %d, %p)\n", __FUNCTION__, shared, pp, arg2, arg3);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][51];
	if (addr)
		return addr(shared, pp, arg2, arg3);
	return -1;
}

GLDReturn gldModifyPipelineProgram(gld_shared_t* shared, gld_pipeline_program_t* pp, uint32_t arg2)
{
	typeof(gldModifyPipelineProgram) *addr;

	GLDLog(2, "%s(%p, %p, %d)\n", __FUNCTION__, shared, pp, arg2);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][52];
	if (addr)
		return addr(shared, pp, arg2);
	return -1;
}

GLDReturn gldUnbindPipelineProgram(gld_context_t* context, gld_pipeline_program_t* pp)
{
	typeof(gldUnbindPipelineProgram) *addr;

	GLDLog(2, "%s(%p, %p)\n", __FUNCTION__, context, pp);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][53];
	if (addr)
		return addr(context, pp);
	return -1;
}

GLDReturn gldDestroyPipelineProgram(gld_shared_t* shared, gld_pipeline_program_t* pp)
{
	GLDLog(2, "%s(%p, %p)\n", __FUNCTION__, shared, pp);

	if (pp->f2[0]) {
		glrReleaseVendShrPipeProg(shared, pp->f2[0]);
		pp->f2[0] = 0;
	}
	if (shared->needs_locking)
		pthread_mutex_lock(&shared->mutex);
	while (pp->f2[3])
		glrDeleteCachedProgram(pp, pp->f2[1]);
	if (shared->pp_list == pp)
		shared->pp_list = pp->next;
	if (pp->next)
		pp->next->prev = pp->prev;
	if (pp->prev)
		pp->prev->next = pp->next;
	if (shared->needs_locking)
		pthread_mutex_unlock(&shared->mutex);
	glrDeleteSysPipelineProgram(shared, pp);
	free(pp);
	return kCGLNoError;
}

GLDReturn gldCreateProgram(gld_shared_t* shared, gld_program_t** struct_out, void* arg2, void* arg3)
{
	gld_program_t* p;

	GLDLog(2, "%s(%p, struct_out, %p, %p)\n", __FUNCTION__, shared, arg2, arg3);

	p = calloc(1, sizeof *p);
	GLDLog(2, "  %s program @%p\n", __FUNCTION__, p);
	if (!p)
		return kCGLBadAlloc;
	p->f0 = arg2;
	p->f1 = arg3;
	*struct_out = p;
	return kCGLNoError;
}

GLDReturn gldDestroyProgram(gld_shared_t* shared, gld_program_t* program)
{
	GLDLog(2, "%s(%p, %p)\n", __FUNCTION__, shared, program);

	free(program);
	return kCGLNoError;
}

GLDReturn gldCreateVertexArray(gld_shared_t* shared, gld_vertex_array_t** struct_out, void* arg2, void* arg3)
{
	gld_vertex_array_t* p;

	GLDLog(2, "%s(%p, struct_out, %p, %p)\n", __FUNCTION__, shared, arg2, arg3);

	p = calloc(1, sizeof *p);
	GLDLog(2, "  %s: vertex array @%p\n", __FUNCTION__, p);
	p->f0 = arg2;
	p->f1 = arg3;
	*struct_out = p;
	return kCGLNoError;
}

GLDReturn gldModifyVertexArray(void)
{
	GLDLog(2, "%s()\n", __FUNCTION__);
	return kCGLNoError;
}

GLDReturn gldFlushVertexArray(void)
{
	GLDLog(2, "%s()\n", __FUNCTION__);
	return kCGLNoError;
}

void gldUnbindVertexArray(void)
{
	GLDLog(2, "%s()\n", __FUNCTION__);
}

void gldReclaimVertexArray(void)
{
	GLDLog(2, "%s()\n", __FUNCTION__);
}

GLDReturn gldDestroyVertexArray(gld_shared_t* shared, gld_vertex_array_t* vertex_array)
{
	GLDLog(2, "%s(%p, %p)\n", __FUNCTION__, shared, vertex_array);
	free(vertex_array);
	return kCGLNoError;
}

GLDReturn gldCreateFence(gld_context_t* context, gld_fence_t** struct_out)
{
	typeof(gldCreateFence) *addr;
	GLDReturn rc;

	GLDLog(2, "%s(%p, struct_out)\n", __FUNCTION__, context);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][63];
	if (addr) {
		rc = addr(context, struct_out);
		GLDLog(2, "  %s: returns %d, struct_out is %p\n", __FUNCTION__, rc, *struct_out);
		return rc;
	}
	return -1;
}

GLDReturn gldDestroyFence(gld_context_t* context, gld_fence_t* fence)
{
	GLDLog(2, "%s(%p, %p)\n", __FUNCTION__, context, fence);

	uint32_t t = fence->f0 >> 5;
	uintptr_t u = (uintptr_t) context->mem2_local + (((uintptr_t) t) << 2);
	*(uint32_t*) u &= ~(1U << (fence->f0 & 0x1FU));
	free(fence);
	return kCGLNoError;
}

GLDReturn gldCreateQuery(gld_context_t* context, uintptr_t* struct_out)
{
	GLDLog(2, "%s(%p, struct_out)\n", __FUNCTION__, context);
	*struct_out = 16;
	return kCGLNoError;
}

GLDReturn gldModifyQuery(void)
{
	GLDLog(2, "%s()\n", __FUNCTION__);
	return kCGLNoError;
}

GLDReturn gldGetQueryInfo(void)
{
	GLDLog(2, "%s()\n", __FUNCTION__);
	return kCGLNoError;
}

GLDReturn gldDestroyQuery(void)
{
	GLDLog(2, "%s()\n", __FUNCTION__);
	return kCGLNoError;
}

GLDReturn gldObjectPurgeable(gld_context_t* context, int object_type, void* object, int arg3, int arg4)
{
	typeof(gldObjectPurgeable) *addr;

	GLDLog(2, "%s(%p, %d, %p, %d, %d)\n", __FUNCTION__, context, object_type, object, arg3, arg4);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][68];
	if (addr)
		return addr(context, object_type, object, arg3, arg4);
	return -1;
}

GLDReturn gldObjectUnpurgeable(gld_context_t* context, int object_type, void* object, int arg3, void* arg4)
{
	typeof(gldObjectUnpurgeable) *addr;

	GLDLog(2, "%s(%p, %d, %p, %d, %p)\n", __FUNCTION__, context, object_type, object, arg3, arg4);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][69];
	if (addr)
		return addr(context, object_type, object, arg3, arg4);
	return -1;
}

GLDReturn gldCreateComputeContext(void)
{
	GLDLog(2, "%s()\n", __FUNCTION__);
	return -1;
}

GLDReturn gldDestroyComputeContext(void)
{
	GLDLog(2, "%s()\n", __FUNCTION__);
	return -1;
}

GLDReturn gldLoadHostBuffer(void)
{
	GLDLog(2, "%s()\n", __FUNCTION__);
	return kCGLNoError;
}

GLDReturn gldSyncBufferObject(void)
{
	GLDLog(2, "%s()\n", __FUNCTION__);
	return kCGLNoError;
}

GLDReturn gldSyncTexture(void)
{
	GLDLog(2, "%s()\n", __FUNCTION__);
	return kCGLNoError;
}

#pragma mark -
#pragma mark OS X Leopard
#pragma mark -

#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
GLD_DEFINE_GENERIC(gldGetTextureLevel, 81);
GLD_DEFINE_GENERIC(gldDeleteTextureLevel, 82);
GLD_DEFINE_GENERIC(gldDeleteTexture, 83);
GLD_DEFINE_GENERIC(gldAllocVertexBuffer, 84);
GLD_DEFINE_GENERIC(gldCompleteVertexBuffer, 85);
GLD_DEFINE_GENERIC(gldFreeVertexBuffer, 86);
GLD_DEFINE_GENERIC(gldGetMemoryPluginData, 87);
GLD_DEFINE_GENERIC(gldSetMemoryPluginData, 88);
GLD_DEFINE_GENERIC(gldFinishMemoryPluginData, 89);
GLD_DEFINE_GENERIC(gldTestMemoryPluginData, 90);
GLD_DEFINE_GENERIC(gldDestroyMemoryPluginData, 91);
#endif
