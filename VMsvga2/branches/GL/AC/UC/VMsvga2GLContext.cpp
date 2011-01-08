/*
 *  VMsvga2GLContext.cpp
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on August 21st 2009.
 *  Copyright 2009-2011 Zenith432. All rights reserved.
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
#define GL_INCL_SHARED
#define GL_INCL_PUBLIC
#define GL_INCL_PRIVATE
#include "GLCommon.h"
#include "UCGLDCommonTypes.h"
#include "UCMethods.h"
#include "VLog.h"
#include "VMsvga2Accel.h"
#include "VMsvga2Device.h"
#include "VMsvga2GLContext.h"
#include "VMsvga2Shared.h"
#include "VMsvga2Surface.h"

#define CLASS VMsvga2GLContext
#define super IOUserClient
OSDefineMetaClassAndStructors(VMsvga2GLContext, IOUserClient);

#if LOGGING_LEVEL >= 1
#define GLLog(log_level, ...) do { if (log_level <= m_log_level) VLog("IOGL: ", ##__VA_ARGS__); } while (false)
#else
#define GLLog(log_level, ...)
#endif

#define HIDDEN __attribute__((visibility("hidden")))

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
	if (m_provider && isIdValid(m_command_buffer.gmr_id))
		complete_command_buffer_io();
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
												kIODirectionInOut,
												size,
												page_size);
	buffer->md = bmd;
	if (!bmd)
		return false;
	buffer->size = size;
	buffer->kernel_ptr = static_cast<VendorCommandBufferHeader*>(bmd->getBytesNoCopy());
	buffer->gmr_id = SVGA_ID_INVALID;
	buffer->fence = 0U;
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
IOReturn CLASS::get_status(uint32_t* status)
{
	/*
	 * crazy function that sets the status to 0 or 1
	 */
	*status = 1;
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::prepare_command_buffer_io()
{
	IOReturn rc;

	if (isIdValid(m_command_buffer.gmr_id))
		return kIOReturnSuccess;
	m_command_buffer.gmr_id = m_provider->AllocGMRID();
	if (!isIdValid(m_command_buffer.gmr_id)) {
		GLLog(1, "%s: Out of GMR IDs\n", __FUNCTION__);
		return kIOReturnNoResources;
	}
	rc = m_command_buffer.md->prepare();
	if (rc != kIOReturnSuccess)
		goto clean1;
	rc = m_provider->createGMR(m_command_buffer.gmr_id, m_command_buffer.md);
	if (rc != kIOReturnSuccess)
		goto clean2;
	return kIOReturnSuccess;

clean2:
	m_command_buffer.md->complete();
clean1:
	m_provider->FreeGMRID(m_command_buffer.gmr_id);
	m_command_buffer.gmr_id = SVGA_ID_INVALID;
	return rc;
}

HIDDEN
void CLASS::sync_command_buffer_io()
{
	if (!m_command_buffer.fence)
		return;
	m_provider->SyncToFence(m_command_buffer.fence);
	m_command_buffer.fence = 0U;
}

HIDDEN
void CLASS::complete_command_buffer_io()
{
	if (!isIdValid(m_command_buffer.gmr_id))
		return;
	sync_command_buffer_io();
	m_provider->destroyGMR(m_command_buffer.gmr_id);
	m_command_buffer.md->complete();
	m_provider->FreeGMRID(m_command_buffer.gmr_id);
	m_command_buffer.gmr_id = SVGA_ID_INVALID;
}

HIDDEN
IOReturn CLASS::alloc_arrays(size_t num_bytes)
{
	IOReturn rc;

	if (!m_provider)
		return kIOReturnNotReady;
	if (m_arrays.fence) {
		m_provider->SyncToFence(m_arrays.fence);	// TBD: set up circular buffer so as not to sync so often
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
			if (m_stream_error) {
				m_shared->unlockShared();
				*options = m_stream_error;
				*memory = 0;
				return kIOReturnBadArgument;
			}
			submit_buffer(result.next, result.ds_count_dwords);
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
			complete_command_buffer_io();
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
			p->flags = pcbRet;
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
				bmd = IOBufferMemoryDescriptor::withOptions(kIOMemoryKernelUserShared |
															kIOMemoryPageable |
															kIODirectionInOut,
															m_type2_len,
															page_size);
				m_type2 = bmd;
				if (!bmd)
					return kIOReturnNoResources;
				m_type2_ptr = static_cast<VendorCommandBufferHeader*>(bmd->getBytesNoCopy());
			} else {
				d = 2U * m_type2_len;
				bmd = IOBufferMemoryDescriptor::withOptions(kIOMemoryKernelUserShared |
															kIOMemoryPageable |
															kIODirectionInOut,
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
	if (!m_provider)
		return kIOReturnNotReady;
	surface_client = m_provider->findSurfaceForID(static_cast<uint32_t>(surface_id));
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
	GLLog(3, "%s:   surface_client %p\n", __FUNCTION__, m_surface_client);
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
	*c2 = vram_size;		// same as c3 in VMsvga2Device::get_config, total memory available for textures (no accounting by VMsvga2)
	*c3 = vram_size;		// same as c4 in VMsvga2Device::get_config, total VRAM size
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

	if (!m_provider)
		return kIOReturnNotReady;
	lockAccel(m_provider);
	if (!surface_id)
		goto bad_exit;
	surface_client = m_provider->findSurfaceForID(static_cast<uint32_t>(surface_id));
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
	GLLog(3, "%s(%#lx, *%#x, *%u, *%u)\n", __FUNCTION__, surface_id, *mode_bits, *width, *height);
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
#if LOG_LEVEL >= 1
	if (m_log_level >= 3) {
		for (int i = 0; i < 8; ++i)
			GLLog(3, "%s:  struct_in[%d] == %#x\n", __FUNCTION__, i, struct_in->data[i]);
	}
#endif
	/*
	 * TBD
	 */
	return kIOReturnUnsupported /* kIOReturnSuccess */;
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
	IOReturn rc;
	eIOAccelSurfaceScaleBits scale_options;
	IOAccelSurfaceScaling scale;	// var_44

	GLLog(2, "%s(struct_in, struct_out, %lu, %lu)\n", __FUNCTION__, struct_in_size, *struct_out_size);
	if (struct_in_size < sizeof *struct_in ||
		*struct_out_size < sizeof *struct_out)
		return kIOReturnBadArgument;
	if (!m_provider)
		return kIOReturnNotReady;
#if LOGGING_LEVEL >= 1
	if (m_log_level >= 3) {
		GLLog(3, "%s:   surface_id == %#x, cmb == %#x, smb == %#x, dr_hi == %u, dr_lo == %u, volatile == %u\n", __FUNCTION__,
			  struct_in->surface_id, struct_in->context_mode_bits, struct_in->surface_mode,
			  struct_in->dr_options_hi, struct_in->dr_options_lo, struct_in->volatile_state);
		GLLog(3, "%s:   set_scale == %u, scale_options == %#x, scale_width == %u, scale_height == %u\n", __FUNCTION__,
			  struct_in->set_scale, struct_in->scale_options,
			  static_cast<uint16_t>(struct_in->scale_width),
			  static_cast<uint16_t>(struct_in->scale_height));
	}
#endif
	*struct_out_size = sizeof *struct_out;
	bzero(struct_out, sizeof *struct_out);
	if (struct_in->surface_id &&
		struct_in->surface_mode != 0xFFFFFFFFU) {
		lockAccel(m_provider);
		surface_client = m_provider->findSurfaceForID(struct_in->surface_id);
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
#if LOGGING_LEVEL >= 1
	if (m_log_level >= 3) {
		GLLog(3, "%s:   config %#x, %u, %u, inner [%u, %u], outer [%u, %u], status %u, smb %#x\n", __FUNCTION__,
			  struct_out->config[0], struct_out->config[1], struct_out->config[2],
			  struct_out->inner_width, struct_out->inner_height, struct_out->outer_width, struct_out->outer_height,
			  struct_out->status, struct_out->surface_mode_bits);
	}
#endif
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
	return kIOReturnError;	// Not implemented in Intel GMA 950
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
