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
#include <libkern/OSAtomic.h>
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
#pragma mark Struct Accessors
#pragma mark -

static inline
uint32_t get32(void const* p, size_t ofs32, size_t ofs64)
{
#ifdef __LP64__
	return *(uint32_t const*) (((uint8_t const*) p) + ofs64);
#else
	return *(uint32_t const*) (((uint8_t const*) p) + ofs32);
#endif
}

static inline
uintptr_t getp(void const* p, size_t ofs32, size_t ofs64)
{
#ifdef __LP64__
	return *(uintptr_t const*) (((uint8_t const*) p) + ofs64);
#else
	return *(uintptr_t const*) (((uint8_t const*) p) + ofs32);
#endif
}

#pragma mark -
#pragma mark GLD
#pragma mark -

static
void gldInitializeLibrarySelf(io_service_t const* pServices,
							  uint8_t const* pServiceFlags,
							  uint32_t GLDisplayMask,
							  void const* arg3,
							  void const* arg4);

void gldInitializeLibrary(io_service_t const* pServices,
						  uint8_t const* pServiceFlags,
						  uint32_t GLDisplayMask,
						  void const* arg3,
						  void const* arg4)
{
	typeof(gldInitializeLibrary) *addr;
	char const* bndl_names[] = { BNDL1, BNDL2 };
	int i, j;

	GLDLog(2, "%s(%p, %p, %#x, %p, %p)\n", __FUNCTION__, pServices, pServiceFlags, GLDisplayMask, arg3, arg4);

	for (j = 0; j != 2; ++j) {
		bndl_handle[j] = dlopen(bndl_names[j], 0);
		if (!bndl_handle[j])
			continue;
		addr = (typeof(addr)) dlsym(bndl_handle[j], "gldInitializeLibrary");
		if (addr) {
			for (i = 0; i != NUM_ENTRIES; ++i)
				bndl_ptrs[j][i] = (GLD_GENERIC_FUNC) dlsym(bndl_handle[j], entry_point_names[i]);
			addr(pServices, pServiceFlags, GLDisplayMask, arg3, arg4);
		} else {
			dlclose(bndl_handle[j]);
			bndl_handle[j] = 0;
		}
	}

	if (bndl_handle[0])
		bndl_index = 0;
	else
		bndl_index = 1;

	load_libGLImage(&libglimage);
	gldInitializeLibrarySelf(pServices, pServiceFlags, GLDisplayMask, arg3, arg4);
	// FIXME: find out how to signal an error
}

static
void gldInitializeLibrarySelf(io_service_t const* pServices,
							  uint8_t const* pServiceFlags,
							  uint32_t GLDisplayMask,
							  void const* arg3,
							  void const* arg4)
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
	glr_io_data.arg3 = arg3;
	glr_io_data.arg4 = arg4;
	last_display = 0;
	num_displays = 0;
	for (display_num = 0U; display_num != 32U; ++display_num) {
		glr_io_data.arr1[display_num] = 0;
		glr_io_data.arr2[display_num] = 0;
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
	GLDLog(2, "  %s: display_info @%p\n", __FUNCTION__, dinfo);
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
		dinfo->config[1] = (uint32_t) output[1];
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

	GLDLog(2, "%s()\n", __FUNCTION__);

	glr_io_data.pServices = 0;
	glr_io_data.pServiceFlags = 0;
	glr_io_data.displayMask = 0;
	glr_io_data.lastDisplay = 0;
	glr_io_data.arg3 = 0;
	glr_io_data.arg4 = 0;
	if (glr_io_data.dinfo) {
		free(glr_io_data.dinfo);
		glr_io_data.dinfo = 0;
	}
	glr_io_data.num_displays = 0;

	unload_libGLImage(&libglimage);
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
	uint32_t displayMask;	// var_164
	uint32_t r14d, var_130, var_178, var_174, var_168, var_160, var_148, var_13c, var_138, var_12c;
	int const* pAttr;
	int attr, r15d, r13d, esi, var_16c, r12d;
	uint16_t var_142, var_140, var_13e;

	// struct_out -->var_188
	// attributes -->rbx
	*struct_out = 0;
	if (getenv("GL_REJECT_HW"))
		return 0;
	displayMask = glr_io_data.displayMask;
	pAttr = attributes;	// rdx
	var_178 = 0;
	var_174 = 0;
	r15d = 0;
	r13d = 0;
	esi = 0;
	var_16c = 0;
	r12d = 0;
	var_168 = 0xFFFFFCU; /* All colors thru kCGLRGBA16161616Bit */
	var_160 = 0;
	var_148 = 0;
	var_142 = 0;
	var_140 = 0;
	var_13e = 0;
	var_130 = 0;
	r14d = 0x500U;
	while ((attr = *pAttr)) {
		++pAttr;
		switch (attr) {
			case 4:
				break;
			case kCGLPFADoubleBuffer:
				var_130 |= 8;
				break;
			case kCGLPFAStereo:
				var_130 |= 2;
				break;
			case kCGLPFAAuxBuffers:
				attr = *pAttr;
				if (attr < 0)
					return kCGLBadValue;
				++pAttr;
				var_13e = (uint16_t) attr;
				break;
			case kCGLPFAColorSize:
				attr = *pAttr++;
				if (attr < 0)
					return kCGLBadValue;
				if (r15d < attr)
					r15d = attr;
				break;
			case kCGLPFAAlphaSize:
				attr = *pAttr++;
				if (attr < 0)
					return kCGLBadValue;
				if (r12d < attr)
					r12d = attr;
				break;
			case kCGLPFADepthSize:
				attr = *pAttr++;
				if (attr < 0)
					return kCGLBadValue;
				if (r13d < attr)
					r13d = attr;
				break;
			case kCGLPFAStencilSize:
				attr = *pAttr++;
				if (attr < 0)
					return kCGLBadValue;
				if (esi < attr)
					esi = attr;
				break;
			case kCGLPFAAccumSize:
				attr = *pAttr++;
				if (attr < 0)
					return kCGLBadValue;
				if (var_16c < attr)
					var_16c = attr;
				break;
			case kCGLPFAMinimumPolicy:
				var_174 = 1;
				break;
			case kCGLPFAMaximumPolicy:
				var_178 = 1;
				break;
			case kCGLPFAOffScreen:
				r14d |= 4;
				break;
			case kCGLPFAFullScreen:
				r14d |= 2;
				break;
			case kCGLPFASampleBuffers:
				attr = *pAttr;
				if (attr < 0)
					return kCGLBadValue;
				++pAttr;
				var_140 = (uint16_t) attr;
				break;
			case kCGLPFASamples:
				attr = *pAttr;
				if (attr < 0)
					return kCGLBadValue;
				++pAttr;
				var_142 = (uint16_t) attr;
				break;
			case kCGLPFAAuxDepthStencil:
				r14d |= 0x800U;
				break;
			case kCGLPFAColorFloat:
				var_168 = 0x3F000000U; /* kCGLRGBFloat64Bit thru kCGLRGBAFloat256Bit */
				break;
			case kCGLPFAMultisample:
				var_148 = 2;
				break;
			case kCGLPFASupersample:
				var_148 = 1;
				break;
			case kCGLPFASampleAlpha:
				var_160 = 1;
				break;
			case kCGLPFABackingStore:
				r14d |= 8;
				break;
			case kCGLPFAWindow:
				r14d |= 1;
				break;
			case kCGLPFADisplayMask:
				attr = *pAttr++;
				displayMask &= (uint32_t) attr;
				break;
			case kCGLPFAPBuffer:
				r14d |= 0x2000U;
				break;
			default:
				return kCGLBadAttribute;
		}
		if ((pAttr - attributes) >= 49)
			return kCGLBadAttribute;
	}
	var_13c = glrGLIBitsGE(esi) & 0x3FFFFU;
	var_138 = glrGLIBitsGE(r13d) & 0x3FFFFU;
	if (r15d <= 32 && r12d <= 8)
		var_168 &= 0xFF0FFFFFU; /* remove kCGLRGB121212Bit thru kCGLRGBA16161616Bit */
	var_12c = 0;
	if (r12d <= 32) {
		uint32_t tmp = glrGLIColorsGE(r15d);
		tmp &= 0x3FFFFFFCU;
		tmp &= var_168;
#if 0
		// FIXME: crazy_func
		if (r12d <= 16) {
			if (r12d <= 12) {
				if (r12d <= 8) {
				} else
					tmp &= 0xAA00000U;
			} else
				tmp &= 0xA800000U;
		} else
			tmp &= 0x8000000U;
#endif
		var_12c = crazy_func(tmp);
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
			p->f0 = 0;
			p->f1 = 1;
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

GLDReturn gldCreateContext(gld_context_t** struct_out,
						   void* arg1,
						   gld_shared_t* shared,
						   void* arg3,
						   void* arg4,
						   void* arg5)
{
	typeof(gldCreateContext) *addr;
	GLDReturn rc;

	GLDLog(2, "%s(struct_out, %p, %p, %p, %p, %p)\n", __FUNCTION__, arg1, shared, arg3, arg4, arg5);
	addr = (typeof(addr)) bndl_ptrs[bndl_index][6];
	if (addr) {
		rc = addr(struct_out, arg1, shared, arg3, arg4, arg5);
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
	// FIXME: TEMPORARY_BUG
#ifdef TEMPORARY_BUG
	typeof(gldDestroyContext) *addr;
#endif

	GLDLog(2, "%s(%p)\n", __FUNCTION__, context);

#ifdef TEMPORARY_BUG
	addr = (typeof(addr)) bndl_ptrs[bndl_index][8];
	if (addr)
		return addr(context);
#else
	return kCGLNoError;
#endif
	return -1;
}

#if 0
static
GLDReturn gldAttachDrawableInternal(gld_context_t* context, int surface_type, void* arg2, void* arg3)
{
	/*
	 * The fudge consts are for 64-bit
	 */
	int r14b, r15b, r12d, edx;
	uint32_t var_d0, var_cc;
	uintptr_t rax, var_e0, var_e8, var_f0, var_f8, var_100, var_108;

	if (!context)
		return kCGLBadAddress;
	r14b = (surface_type == 80);
	r15b = (surface_type == 90);
	var_d0 = 0; var_cc = 0;
	if (r14b) {
		// 528A
		if (!arg2)
			return kCGLBadDrawable;
		r12d = 0;
		edx = get32(context, 0, 0x54);
		rax = (uintptr_t) arg2 + 8;
		var_e0 = rax;
		// skipped 52A4-52DA
		edx |= 32;
		edx &= 0xFF80FFFF;
		// FIXME: dword ptr context+0x54 = edx
		// FIXME: byte ptr context+0xAB = r15d ^ 1
		if 
	}
}
#endif

GLDReturn gldAttachDrawable(gld_context_t* context, int surface_type, void* arg2, void* arg3)
{
	typeof(gldAttachDrawable) *addr;

	GLDLog(2, "%s(%p, %d, %p, %p)\n", __FUNCTION__, context, surface_type, arg2, arg3);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][9];
	if (addr)
		return addr(context, surface_type, arg2, arg3);
	return -1;
}

GLDReturn gldInitDispatch(void* arg0, void* arg1, void* arg2)
{
	typeof(gldInitDispatch) *addr;

	GLDLog(2, "%s(%p, %p, %p)\n", __FUNCTION__, arg0, arg1, arg2);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][10];
	if (addr)
		return addr(arg0, arg1, arg2);
	return -1;
}

GLDReturn gldUpdateDispatch(void* arg0, void* arg1, void* arg2)
{
	typeof(gldUpdateDispatch) *addr;

	GLDLog(2, "%s(%p, %p, %p)\n", __FUNCTION__, arg0, arg1, arg2);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][11];
	if (addr)
		return addr(arg0, arg1, arg2);
	return -1;
}

char const* gldGetString(uint32_t GLDisplayMask, int string_code)
{
	typeof(gldGetString) *addr;
	char const* r = 0;

	GLDLog(2, "%s(%#x, %#x)\n", __FUNCTION__, GLDisplayMask, string_code);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][12];
	if (addr) {
		r = addr(GLDisplayMask, string_code);
		if (r)
			GLDLog(2, "  %s returns %s\n", __FUNCTION__, r);
	}
	switch (string_code) {
		case 0x1F00:
			r = "Zenith432";
			break;
		case 0x1F01:
			r = "VMware SVGA II OpenGL Engine";
			break;
		case 0x1F04:
			r = "VMsvga2GLDriver";
			break;
	}
	return r;
}

void gldGetError(void* arg0)
{
	typeof(gldGetError) *addr;

	GLDLog(2, "%s(%p)\n", __FUNCTION__, arg0);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][13];
	if (addr)
		addr(arg0);
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

	if (getp(context, 0x150, 0x1e0) + 32 < getp(context, 0x148, 0x1d0))
		SubmitPacketsToken(context, 1);
}

void gldFinish(gld_context_t* context)
{
	GLDLog(2, "%s(%p)\n", __FUNCTION__, context);

	if (getp(context, 0x150, 0x1e0) + 32 < getp(context, 0x148, 0x1d0))
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
	void* const* object_3;
	void* p;

	GLDLog(2, "%s(%p, %d, %d, %p)\n", __FUNCTION__, context, object_type, arg2, object);

	switch (object_type) {
		case 1:
			object_1 = (gld_texture_t const*) object;
			if (object_1->waitable)
				glrFlushSysObject(context, object_1->waitable, arg2);
			break;
		case 3:
			object_3 = (void* const*) object;
			p = object_3[2];
			if (!p)
				break;
			p = *(void* const *) p;
			if (p)
				glrFlushSysObject(context, (gld_waitable_t*) p, arg2);
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
	void* const* object_3;
	void* p;

	GLDLog(2, "%s(%p, %d, %p, %p)\n", __FUNCTION__, shared, object_type, object, arg3);

	switch (object_type) {
		case 1:
			object_1 = (gld_texture_t const*) object;
			if (object_1->waitable)
				glrWaitSharedObject(shared, object_1->waitable);
			break;
		case 3:
			object_3 = (void* const*) object;
			p = object_3[2];
			if (!p)
				break;
			p = *(void* const *) p;
			if (!p)
				break;
			glrWaitSharedObject(shared, (gld_waitable_t*) p);
			break;
		default:
			return kCGLBadEnumeration;
	}
	return kCGLNoError;
}

GLDReturn gldCreateTexture(gld_shared_t* shared, gld_texture_t** texture, void* arg2)
{
	GLDLog(2, "%s(%p, struct_out, %p)\n", __FUNCTION__, shared, arg2);

	gld_texture_t* p = (gld_texture_t*) calloc(1 , sizeof(gld_texture_t));
	GLDLog(2, "  %s: gld_texture @%p\n", __FUNCTION__, p);
	p->f10 = arg2;
	p->f11 = 0;
	p->waitable = 0;
	p->f0 = 0;
	p->f1 = 8;
	p->f3 = 0;
	p->f4 = 0;
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

	return texture->waitable != 0;
}

GLDReturn gldModifyTexture(gld_shared_t* shared, gld_texture_t* texture, uint8_t arg2, int arg3, uint8_t arg4)
{
	uint64_t input;
	gld_waitable_t* w;

	GLDLog(2, "%s(%p, %p, %u, %d, %u)\n", __FUNCTION__, shared, texture, arg2, arg3, arg4);

	texture->f14 |= arg2;
	if (arg2 & 4)
		texture->f9[arg3] |= (1U << arg4);
	if (!(arg2 & 1))
		return kCGLNoError;
	texture->f13 = 0;
	w = texture->waitable;
	if (!w)
		return kCGLNoError;
	switch (w->type) {
		case 6:
		case 9:
			if (shared->f0)
				pthread_mutex_lock(&shared->mutex);
			if (!OSAtomicAdd32(0xFFFF0000, &w->stamps[3])) {
				input = (uint64_t) w->texture_id;
				IOConnectCallMethod(shared->obj,
									kIOVMDeviceDeleteTexture,
									&input, 1,
									0, 0, 0, 0, 0, 0);
			}
			texture->waitable = 0;
			if (shared->f0)
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

GLD_DEFINE_GENERIC(gldCopyTexSubImage, 76)
GLD_DEFINE_GENERIC(gldModifyTexSubImage, 77)
GLD_DEFINE_GENERIC(gldCreateTextureLevel, 29)
GLD_DEFINE_GENERIC(gldGetTextureLevelInfo, 30)
GLD_DEFINE_GENERIC(gldGetTextureLevelImage, 31)
GLD_DEFINE_GENERIC(gldModifyTextureLevel, 32)
GLD_DEFINE_GENERIC(gldDestroyTextureLevel, 33)
GLD_DEFINE_GENERIC(gldCreateBuffer, 34)
GLD_DEFINE_GENERIC(gldBufferSubData, 78)
GLD_DEFINE_GENERIC(gldLoadBuffer, 35)
GLD_DEFINE_GENERIC(gldFlushBuffer, 36)
GLD_DEFINE_GENERIC(gldPageoffBuffer, 37)
GLD_DEFINE_GENERIC(gldUnbindBuffer, 38)
GLD_DEFINE_GENERIC(gldReclaimBuffer, 39)
GLD_DEFINE_GENERIC(gldDestroyBuffer, 40)
GLD_DEFINE_GENERIC(gldGetMemoryPlugin, 41)
GLD_DEFINE_GENERIC(gldSetMemoryPlugin, 42)
GLD_DEFINE_GENERIC(gldTestMemoryPlugin, 43)
GLD_DEFINE_GENERIC(gldFlushMemoryPlugin, 44)
GLD_DEFINE_GENERIC(gldDestroyMemoryPlugin, 45)
GLD_DEFINE_GENERIC(gldCreateFramebuffer, 46)
GLD_DEFINE_GENERIC(gldUnbindFramebuffer, 47)
GLD_DEFINE_GENERIC(gldReclaimFramebuffer, 48)
GLD_DEFINE_GENERIC(gldDestroyFramebuffer, 49)

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
	if (shared->f0)
		pthread_mutex_lock(&shared->mutex);
	while (pp->f2[3])
		glrDeleteCachedProgram(pp, pp->f2[1]);
	if (shared->pp_list == pp)
		shared->pp_list = pp->next;
	if (pp->next)
		pp->next->prev = pp->prev;
	if (pp->prev)
		pp->prev->next = pp->next;
	if (shared->f0)
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
	uintptr_t u = getp(context, 0x170, 0x220) + (((uintptr_t) t) << 2);
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

void gldDiscardFramebuffer(void)
{
	GLDLog(2, "%s()\n", __FUNCTION__);
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
