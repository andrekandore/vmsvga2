/*
 *  VertexArray.cpp
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on January 24th 2011.
 *  Copyright 2011 Zenith432. All rights reserved.
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

#include "VMsvga2Accel.h"
#include "VertexArray.h"

#define CLASS VertexArray

#define HIDDEN __attribute__((visibility("hidden")))

#pragma mark -
#pragma mark Global Functions
#pragma mark -

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

#pragma mark -
#pragma mark Public Methods
#pragma mark -

HIDDEN
void CLASS::init(void)
{
	sid = SVGA_ID_INVALID;
	gmr_id = SVGA_ID_INVALID;
}

HIDDEN
void CLASS::purge(VMsvga2Accel* provider)
{
	if (!provider)
		return;
	if (fence) {
		provider->SyncToFence(fence);
		fence = 0U;
	}
	gmr_id = SVGA_ID_INVALID;
	if (kernel_ptr) {
		provider->VRAMFree(kernel_ptr);
		kernel_ptr = 0;
		size_bytes = 0U;
		offset_in_gmr = 0U;
		next_avail = 0U;
	}
	if (isIdValid(sid)) {
		provider->destroySurface(sid);
		provider->FreeSurfaceID(sid);
		sid = SVGA_ID_INVALID;
	}
}

HIDDEN
IOReturn CLASS::alloc(VMsvga2Accel* provider, size_t num_bytes, uint8_t** ptr)
{
	size_t alloc_bytes;
	IOReturn rc;

	if (!provider)
		return kIOReturnNotReady;
	num_bytes = (num_bytes + sizeof(uint32_t) - 1U) & -sizeof(uint32_t);
	if (kernel_ptr) {
		if (next_avail + num_bytes <= size_bytes)
			goto done;
		if (num_bytes <= size_bytes) {
			if (fence) {
				provider->SyncToFence(fence);
				fence = 0U;
			}
			next_avail = 0U;
			goto done;
		}
		purge(provider);
	}
	alloc_bytes = (num_bytes + PAGE_MASK) & -PAGE_SIZE;
	sid = provider->AllocSurfaceID();
	rc = provider->createSurface(sid,
								 SVGA3dSurfaceFlags(SVGA3D_SURFACE_HINT_VERTEXBUFFER |
													SVGA3D_SURFACE_HINT_DYNAMIC |
													SVGA3D_SURFACE_HINT_WRITEONLY),
								 SVGA3D_BUFFER,
								 static_cast<uint32_t>(alloc_bytes),
								 1U);
	if (rc != kIOReturnSuccess) {
		provider->FreeSurfaceID(sid);
		sid = SVGA_ID_INVALID;
		return rc;
	}
	kernel_ptr = static_cast<uint8_t*>(provider->VRAMMalloc(alloc_bytes));
	if (!kernel_ptr) {
		purge(provider);
		return kIOReturnNoMemory;
	}
	size_bytes = alloc_bytes;
	offset_in_gmr = provider->offsetInVRAM(kernel_ptr);
	gmr_id = GMR_VRAM();
done:
	*ptr = kernel_ptr + next_avail;
	next_avail += num_bytes;
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::upload(VMsvga2Accel* provider, uint8_t const* ptr, size_t num_bytes, uint32_t* _sid)
{
	SVGA3dSurfaceImageId hostImage;
	SVGA3dCopyBox copyBox;
	VMsvga2Accel::ExtraInfoEx extra;
	size_t additional_offset;

	if (!provider)
		return kIOReturnNotReady;
	if (ptr < kernel_ptr)
		return kIOReturnUnderrun;
	additional_offset = ptr - kernel_ptr;
	if (additional_offset + num_bytes > size_bytes)
		return kIOReturnOverrun;
	extra.mem_gmr_id = gmr_id;
	extra.mem_offset_in_gmr = offset_in_gmr + additional_offset;
	extra.mem_pitch = 0U;
	extra.mem_limit = num_bytes;
	extra.suffix_flags = 3U;
	bzero(&copyBox, sizeof copyBox);
	copyBox.w = static_cast<uint32_t>(num_bytes);
	copyBox.h = 1U;
	copyBox.d = 1U;
	hostImage.sid = sid;
	if (_sid)
		*_sid = sid;
	hostImage.face = 0U;
	hostImage.mipmap = 0U;
	return provider->surfaceDMA3DEx(&hostImage,
									SVGA3D_WRITE_HOST_VRAM,
									&copyBox,
									&extra,
									&fence);
}
