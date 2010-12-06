/*
 *  VMsvga2GLDriver.h
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

#ifndef __VMSVGA2GLDRIVER_H__
#define __VMSVGA2GLDRIVER_H__

#ifdef __cplusplus
extern "C" {
#endif

#define GLD_DECLARE_GENERIC(name) GLDReturn name(void*, void*, void*, void*, void*, void*)

void gldInitializeLibrary(io_service_t const* pServices,
						  uint8_t const* pServiceFlags,
						  uint32_t GLDisplayMask,
						  void const* arg3,
						  void const* arg4);
void gldTerminateLibrary(void);

_Bool gldGetVersion(int*, int*, int*, int*);
GLDReturn gldGetRendererInfo(void* struct_out, uint32_t GLDisplayMask);
GLDReturn gldChoosePixelFormat(PixelFormat** struct_out, int const* attributes);
GLDReturn gldDestroyPixelFormat(PixelFormat* pixel_format);
GLDReturn gldCreateShared(gld_shared_t** struct_out, uint32_t GLDisplayMask, long arg2);
GLDReturn gldDestroyShared(gld_shared_t* shared);
GLDReturn gldCreateContext(gld_context_t** struct_out,
						   void* arg1,
						   gld_shared_t* shared,
						   void* arg3,
						   void* arg4,
						   void* arg5);
GLDReturn gldReclaimContext(gld_context_t* context);
GLDReturn gldDestroyContext(gld_context_t* context);
GLDReturn gldAttachDrawable(gld_context_t* context, int surface_type, void* arg2, void* arg3);	// note: arg2+arg3 may be a 64-bit param
GLDReturn gldInitDispatch(void* arg0, void* arg1, void* arg2);
GLDReturn gldUpdateDispatch(void* arg0, void* arg1, void* arg2);
char const* gldGetString(uint32_t GLDisplayMask, int string_code);
void gldGetError(void* arg0);
GLDReturn gldSetInteger(gld_context_t* context, int arg1, void* arg2);
GLDReturn gldGetInteger(gld_context_t* context, int arg1, void* arg2);
void gldFlush(gld_context_t* context);
void gldFinish(gld_context_t* context);
GLDReturn gldTestObject(gld_context_t* context, int object_type, void const* object);
GLDReturn gldFlushObject(gld_context_t* context, int object_type, int arg2, void const* object);
GLDReturn gldFinishObject(gld_context_t* context, int object_type, void const* object);
GLDReturn gldWaitObject(gld_shared_t* shared, int object_type, void const* object, void const* arg3);
GLDReturn gldCreateTexture(gld_shared_t* shared, gld_texture_t** texture, void* arg2);
int gldIsTextureResident(gld_shared_t* shared, gld_texture_t* texture);
GLDReturn gldModifyTexture(gld_shared_t* shared, gld_texture_t* texture, uint8_t arg2, int arg3, uint8_t arg4);
GLDReturn gldGenerateTexMipmaps(gld_shared_t* shared, gld_texture_t* texture, int arg2);
GLDReturn gldLoadTexture(gld_context_t* context, gld_texture_t* texture);
void gldUnbindTexture(gld_context_t* context, gld_texture_t* texture);
void gldReclaimTexture(gld_shared_t* shared, gld_texture_t* texture);
void gldDestroyTexture(gld_shared_t* shared, gld_texture_t* texture);
GLD_DECLARE_GENERIC(gldCopyTexSubImage);
GLD_DECLARE_GENERIC(gldModifyTexSubImage);
GLD_DECLARE_GENERIC(gldCreateTextureLevel);
GLD_DECLARE_GENERIC(gldGetTextureLevelInfo);
GLD_DECLARE_GENERIC(gldGetTextureLevelImage);
GLD_DECLARE_GENERIC(gldModifyTextureLevel);
GLD_DECLARE_GENERIC(gldDestroyTextureLevel);
GLD_DECLARE_GENERIC(gldCreateBuffer);
GLD_DECLARE_GENERIC(gldBufferSubData);
GLD_DECLARE_GENERIC(gldLoadBuffer);
GLD_DECLARE_GENERIC(gldFlushBuffer);
GLD_DECLARE_GENERIC(gldPageoffBuffer);
GLD_DECLARE_GENERIC(gldUnbindBuffer);
GLD_DECLARE_GENERIC(gldReclaimBuffer);
GLD_DECLARE_GENERIC(gldDestroyBuffer);
GLD_DECLARE_GENERIC(gldGetMemoryPlugin);
GLD_DECLARE_GENERIC(gldSetMemoryPlugin);
GLD_DECLARE_GENERIC(gldTestMemoryPlugin);
GLD_DECLARE_GENERIC(gldFlushMemoryPlugin);
GLD_DECLARE_GENERIC(gldDestroyMemoryPlugin);
GLD_DECLARE_GENERIC(gldCreateFramebuffer);
GLD_DECLARE_GENERIC(gldUnbindFramebuffer);
GLD_DECLARE_GENERIC(gldReclaimFramebuffer);
GLD_DECLARE_GENERIC(gldDestroyFramebuffer);
GLDReturn gldCreatePipelineProgram(gld_shared_t* shared, gld_pipeline_program_t** struct_out, void* arg2);
GLDReturn gldGetPipelineProgramInfo(gld_shared_t* shared, gld_pipeline_program_t* pp, int arg2, void* arg3);
GLDReturn gldModifyPipelineProgram(gld_shared_t* shared, gld_pipeline_program_t* pp, uint32_t arg2);
GLDReturn gldUnbindPipelineProgram(gld_context_t* context, gld_pipeline_program_t* pp);
GLDReturn gldDestroyPipelineProgram(gld_shared_t* shared, gld_pipeline_program_t* pp);
GLDReturn gldCreateProgram(gld_shared_t* shared, gld_program_t** struct_out, void* arg2, void* arg3);
GLDReturn gldDestroyProgram(gld_shared_t* shared, gld_program_t* program);
GLDReturn gldCreateVertexArray(gld_shared_t* shared, gld_vertex_array_t** struct_out, void* arg2, void* arg3);
GLDReturn gldModifyVertexArray(void);
GLDReturn gldFlushVertexArray(void);
void gldUnbindVertexArray(void);
void gldReclaimVertexArray(void);
GLDReturn gldDestroyVertexArray(gld_shared_t* shared, gld_vertex_array_t* vertex_array);
GLDReturn gldCreateFence(gld_context_t* context, gld_fence_t** struct_out);
GLDReturn gldDestroyFence(gld_context_t* context, gld_fence_t* fence);
GLDReturn gldCreateQuery(gld_context_t* context, uintptr_t* struct_out);
GLDReturn gldModifyQuery(void);
GLDReturn gldGetQueryInfo(void);
GLDReturn gldDestroyQuery(void);
GLDReturn gldObjectPurgeable(gld_context_t* context, int object_type, void* object, int arg3, int arg4);
GLDReturn gldObjectUnpurgeable(gld_context_t* context, int object_type, void* object, int arg3, void* arg4);
GLDReturn gldCreateComputeContext(void);
GLDReturn gldDestroyComputeContext(void);
GLDReturn gldLoadHostBuffer(void);
GLDReturn gldSyncBufferObject(void);
GLDReturn gldSyncTexture(void);
void gldDiscardFramebuffer(void);
GLD_DECLARE_GENERIC(gldGetTextureLevel);
GLD_DECLARE_GENERIC(gldDeleteTextureLevel);
GLD_DECLARE_GENERIC(gldDeleteTexture);
GLD_DECLARE_GENERIC(gldAllocVertexBuffer);
GLD_DECLARE_GENERIC(gldCompleteVertexBuffer);
GLD_DECLARE_GENERIC(gldFreeVertexBuffer);
GLD_DECLARE_GENERIC(gldGetMemoryPluginData);
GLD_DECLARE_GENERIC(gldSetMemoryPluginData);
GLD_DECLARE_GENERIC(gldFinishMemoryPluginData);
GLD_DECLARE_GENERIC(gldTestMemoryPluginData);
GLD_DECLARE_GENERIC(gldDestroyMemoryPluginData);
	
#ifdef __cplusplus
}
#endif

#endif /* __VMSVGA2GLDRIVER_H__ */
