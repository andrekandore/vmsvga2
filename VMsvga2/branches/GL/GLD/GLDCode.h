/*
 *  GLDCode.h
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

#ifndef __GLDCODE_H__
#define __GLDCODE_H__

#ifdef __cplusplus
extern "C" {
#endif

kern_return_t glrPopulateDevice(io_connect_t connect, display_info_t* dinfo);
uint32_t glrGLIBitsGE(uint32_t num);
uint32_t glrGLIColorsGE(uint32_t num);
void glrWaitSharedObject(gld_shared_t* shared, gld_waitable_t* w);
void glrInitializeHardwareShared(gld_shared_t* shared, void* channel_memory);
void glrDestroyZeroTexture(gld_shared_t* shared, gld_texture_t* texture);
void glrDestroyHardwareShared(gld_shared_t* shared);
void glrFlushSysObject(gld_context_t* context, gld_waitable_t* waitable, int arg2);
void load_libGLImage(libglimage_t* interface);
void unload_libGLImage(libglimage_t* interface);
void SubmitPacketsToken(gld_context_t*, int);
void glrReleaseVendShrPipeProg(gld_shared_t* shared, void* arg1);
void glrDeleteCachedProgram(gld_pipeline_program_t* pp, void* arg1);
void glrDeleteSysPipelineProgram(gld_shared_t* shared, gld_pipeline_program_t* pp);
GLDReturn glrValidatePixelFormat(PixelFormat* pixel_format);
void glrKillClient(kern_return_t kr);
uint32_t xlateColorMode(uint32_t colorMode);
void glrSetWindowModeBits(uint32_t* wmb, PixelFormat* pixel_format);
void glrInitializeHardwareState(gld_context_t* context, PixelFormat* pixel_format);
void glrSetConfigData(gld_context_t* context, void* arg3, PixelFormat* pixel_format);
uint32_t glrGLIAlphaGE(int alpha_bits);
char const* glrGetString(display_info_t* dinfo, int string_code);

#ifdef __cplusplus
}
#endif

#endif /* __GLDCODE_H__ */
