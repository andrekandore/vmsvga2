/*
 *  VMsvga2GLContext.cpp
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on August 21st 2009.
 *  Copyright 2009 Zenith432. All rights reserved.
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

#include <IOKit/IOLib.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include "VLog.h"
#include "VMsvga2Accel.h"
#include "VMsvga2GLContext.h"
#include "VMsvga2Surface.h"
#include "ACMethods.h"

#define CLASS VMsvga2GLContext
#define super IOUserClient
OSDefineMetaClassAndStructors(VMsvga2GLContext, IOUserClient);

#if LOGGING_LEVEL >= 1
#define GLLog(log_level, ...) do { if (log_level <= m_log_level) VLog("IOGL: ", ##__VA_ARGS__); } while (false)
#else
#define GLLog(log_level, ...)
#endif

static IOExternalMethod iofbFuncsCache[kIOVMGLNumMethods] =
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
#pragma mark struct definitions
#pragma mark -

struct sIOGLContextReadBufferData
{
	uint32_t data[8];		// Note: only uses four
};

#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
/*
 * Note: used in OS 10.5, copied from Device UC
 */
struct sIOGLNewTextureData
{
	uint32_t f0;
	uint8_t f1[4];
	uint32_t f2;
	uint16_t f3[2];
	uint32_t f4;
	uint32_t f5;
	uint32_t f6[4];
	uint64_t f7[4];
};

/*
 * Note: used in OS 10.5, copied from Device UC
 */
struct sIOGLNewTextureReturnData
{
	uint32_t data[5];
};

/*
 * Note: used in OS 10.5, copied from Device UC
 */
struct sIOGLContextPageoffTexture
{
	uint32_t data[2];
};
#endif

struct sIOGLContextSetSurfaceData
{
	uint32_t surface_id;
	uint32_t context_mode_bits;
	uint32_t surface_mode;
	uint32_t dr_options_hi;		// high byte of options passed to gldAttachDrawable
	uint32_t dr_options_lo;		// low byte of options passed to gldAttachDrawable
	uint32_t volatile_state;
	uint32_t set_scale;
	uint32_t scale_options;
	uint32_t scale_width;		// lower 16 bits
	uint32_t scale_height;		// lower 16 bits
};

struct sIOGLContextGetConfigStatus
{
	uint32_t config[3];
	uint32_t inner_width;
	uint32_t inner_height;
	uint32_t outer_width;
	uint32_t outer_height;
	uint32_t status;		// boolean 0 or 1
	uint32_t surface_mode_bits;
	uint32_t reserved;
};

struct sIOGLContextGetDataBuffer
{
	uint32_t len;
	mach_vm_address_t addr;
} __attribute__((packed));

#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
/*
 * Note: used in OS 10.5, copied from Device UC
 */
struct sIOGLChannelMemoryData
{
	mach_vm_address_t addr;
};
#else
struct sIOGLGetCommandBuffer
{
	uint32_t len[2];
	mach_vm_address_t addr[2];
};
#endif

struct VendorCommandBufferHeader
{
	uint32_t data[8];
};

#pragma mark -
#pragma mark Private Methods
#pragma mark -

static inline
void lockAccel(VMsvga2Accel* accel)
{
}

static inline
void unlockAccel(VMsvga2Accel* accel)
{
}

void CLASS::Cleanup()
{
	if (surface_client) {
		surface_client->release();
		surface_client = 0;
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
}

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

void CLASS::initCommandBufferHeader(VendorCommandBufferHeader* buffer_ptr, size_t size)
{
	bzero(buffer_ptr, sizeof *buffer_ptr);
	buffer_ptr->data[4] = static_cast<uint32_t>((size - sizeof *buffer_ptr) / sizeof(uint32_t));
	buffer_ptr->data[6] = 0;	// Intel915 puts (submitStamp - 1) here
}

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
	p->data[5] = 1;
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
	p->data[5] = 1;
	p->data[3] = 1007;
	// TBD: allocates another one like this
	return true;
}

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

IOReturn CLASS::get_status(uint32_t* status)
{
	/*
	 * crazy function that sets the status to 0 or 1
	 */
	*status = 1;
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark IOUserClient Methods
#pragma mark -

IOExternalMethod* CLASS::getTargetAndMethodForIndex(IOService** targetP, UInt32 index)
{
	if (index >= kIOVMGLNumMethods)
		GLLog(2, "%s(%p, %u)\n", __FUNCTION__, targetP, index);
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
	return &m_funcs_cache[index];
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

	GLLog(2, "%s(%u, %p, %p)\n", __FUNCTION__, type, options, memory);
	if (type > 4 || !options || !memory)
		return kIOReturnBadArgument;
#if 0
	if (dword @ this+0x194 != 0) {
		*options = dword @ this+0x194;
		*memory = 0;
		return kIOReturnBadArgument;
	}
#endif
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
			p->data[5] = 0;				// TBD: from this+0x88
			p->data[7] = 1;
			p->data[8] = 0x1000000U;	// terminating token
			p->data[6] = 0;				// TBD: from this+0x7C
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
			p->data[5] = 1;
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
			p->data[5] = 0;
			p->data[7] = 1;
			p->data[8] = 0x1000000U;	// terminating token
			p->data[6] = 0;				// TBD: from this+0x7C
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
	GLLog(2, "%s(%p), name == %s\n", __FUNCTION__, client, client ? client->getName() : "NULL");
	return kIOReturnSuccess;
#if 0
	return super::connectClient(client);
#endif
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
	m_mem_type = 4;
	m_gc = OSSet::withCapacity(2);
	if (!m_gc) {
		super::stop(provider);
		return false;
	}
	// TBD getVRAMDescriptors
	if (!allocAllContextBuffers()) {
		Cleanup();
		super::stop(provider);
		return false;
	}
	if (!allocCommandBuffer(&m_command_buffer, 0x10000U)) {
		Cleanup();
		super::stop(provider);
		return false;
	}
	m_command_buffer.kernel_ptr->data[5] = 2U;
	m_command_buffer.kernel_ptr->data[9] = 1000000U;
	return true;
}

bool CLASS::initWithTask(task_t owningTask, void* securityToken, UInt32 type)
{
	m_log_level = LOGGING_LEVEL;
	if (!super::initWithTask(owningTask, securityToken, type))
		return false;
	m_owning_task = owningTask;
	m_funcs_cache = &iofbFuncsCache[0];
	return true;
}

CLASS* CLASS::withTask(task_t owningTask, void* securityToken, UInt32 type)
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

IOReturn CLASS::set_surface(uintptr_t surface_id, eIOGLContextModeBits context_mode_bits, uintptr_t c3, uintptr_t c4)
{
	GLLog(2, "%s(%#lx, %#lx, %lu, %lu)\n", __FUNCTION__, surface_id, context_mode_bits, c3, c4);
	return kIOReturnSuccess;
}

IOReturn CLASS::set_swap_rect(intptr_t c1, intptr_t c2, intptr_t c3, intptr_t c4)
{
	GLLog(2, "%s(%ld, %ld, %ld, %ld)\n", __FUNCTION__, c1, c2, c3, c4);
	return kIOReturnSuccess;
}

IOReturn CLASS::set_swap_interval(intptr_t c1, intptr_t c2)
{
	GLLog(2, "%s(%ld, %ld)\n", __FUNCTION__, c1, c2);
	return kIOReturnSuccess;
}

IOReturn CLASS::get_config(uint32_t* c1, uint32_t* c2, uint32_t* c3)
{
	UInt32 const vram_size = m_provider->getVRAMSize();

	*c1 = 0;
	*c2 = vram_size;
	*c3 = vram_size;
	GLLog(2, "%s(*%u, *%u, *%u)\n", __FUNCTION__, *c1, *c2, *c3);
	return kIOReturnSuccess;
}

IOReturn CLASS::get_surface_size(UInt32* c1, UInt32* c2, UInt32* c3, UInt32* c4)
{
	*c1 = 1;
	*c2 = 2;
	*c3 = 3;
	*c4 = 4;
	GLLog(2, "%s(*%u, *%u, *%u, *%u)\n", __FUNCTION__, *c1, *c2, *c3, *c4);
	return kIOReturnSuccess;
}

IOReturn CLASS::get_surface_info(uintptr_t c1, UInt32* c2, UInt32* c3, UInt32* c4)
{
	*c2 = 0x4081;
	*c3 = 0x4082;
	*c4 = 0x4083;
	GLLog(2, "%s(%lu, *%u, *%u, *%u)\n", __FUNCTION__, c1, *c2, *c3, *c4);
	return kIOReturnSuccess;
}

IOReturn CLASS::read_buffer(struct sIOGLContextReadBufferData const* struct_in, size_t struct_in_size)
{
	GLLog(2, "%s(%p, %lu)\n", __FUNCTION__, struct_in, struct_in_size);
	if (struct_in_size < sizeof *struct_in)
		return kIOReturnBadArgument;
	for (int i = 0; i < 8; ++i)
		GLLog(2, "%s:  struct_in[%d] == %#x\n", __FUNCTION__, i, struct_in->data[i]);
	return kIOReturnSuccess;
}

IOReturn CLASS::finish()
{
	GLLog(2, "%s()\n", __FUNCTION__);
	return kIOReturnSuccess;
}

IOReturn CLASS::wait_for_stamp(uintptr_t c1)
{
	GLLog(2, "%s(%lu)\n", __FUNCTION__, c1);
	return kIOReturnSuccess;
}

#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
IOReturn CLASS::new_texture(struct sIOGLNewTextureData const* struct_in,
							struct sIOGLNewTextureReturnData* struct_out,
							size_t struct_in_size,
							size_t* struct_out_size)
{
	GLLog(2, "%s(%p, %p, %lu, %lu)\n", __FUNCTION__, struct_in, struct_out, struct_in_size, *struct_out_size);
	bzero(struct_out, *struct_out_size);
	return kIOReturnSuccess;
}

IOReturn CLASS::delete_texture(uintptr_t c1)
{
	GLLog(2, "%s(%lu)\n", __FUNCTION__, c1);
	return kIOReturnSuccess;
}
#endif

IOReturn CLASS::become_global_shared(uintptr_t c1)
{
	GLLog(2, "%s(%lu)\n", __FUNCTION__, c1);
	return kIOReturnSuccess;
}

#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
IOReturn CLASS::page_off_texture(struct sIOGLContextPageoffTexture const* struct_in, size_t struct_in_size)
{
	GLLog(2, "%s(%p, %lu)\n", __FUNCTION__, struct_in, struct_in_size);
	return kIOReturnSuccess;
}
#endif

IOReturn CLASS::purge_texture(uintptr_t c1)
{
	GLLog(2, "%s(%lu)\n", __FUNCTION__, c1);
	return kIOReturnSuccess;
}

IOReturn CLASS::set_surface_volatile_state(uintptr_t c1)
{
	GLLog(2, "%s(%lu)\n", __FUNCTION__, c1);
	return kIOReturnSuccess;
}

IOReturn CLASS::set_surface_get_config_status(struct sIOGLContextSetSurfaceData const* struct_in,
											  struct sIOGLContextGetConfigStatus* struct_out,
											  size_t struct_in_size,
											  size_t* struct_out_size)
{
	VMsvga2Surface* surface_obj = 0;
	int i;
	IOReturn rc;
	eIOAccelSurfaceScaleBits scale_options;
	IOAccelSurfaceScaling scale;	// var_44

	GLLog(2, "%s(struct_in, struct_out, %lu, %lu)\n", __FUNCTION__, struct_in_size, *struct_out_size);
	if (struct_in_size < sizeof *struct_in ||
		*struct_out_size < sizeof *struct_out)
		return kIOReturnBadArgument;
	for (i = 0; i < 10; ++i)
		GLLog(2, "%s:   struct_in[%d] == %#x\n", __FUNCTION__, i, reinterpret_cast<uint32_t const*>(struct_in)[i]);
	*struct_out_size = sizeof *struct_out;
	bzero(struct_out, sizeof *struct_out);
	*struct_out_size = sizeof *struct_out;
	if (struct_in->surface_id &&
		struct_in->surface_mode != 0xFFFFFFFFU) {
		lockAccel(m_provider);
		surface_obj = findSurfaceforID(struct_in->surface_id);
		unlockAccel(m_provider);
		if (surface_obj &&
			((surface_obj->getOriginalModeBits() & 15) != static_cast<int>(struct_in->surface_mode & 15U)))
			return kIOReturnError;
		/*
		 * Note: added by me
		 */
		if (surface_obj) {
			if (surface_client)
				surface_client->release();
			surface_client = surface_obj;
			surface_client->retain();
			GLLog(2, "%s:   surface_client %p\n", __FUNCTION__, surface_client);
		}
	}
	rc = set_surface(struct_in->surface_id,
					 struct_in->context_mode_bits,
					 struct_in->dr_options_hi,
					 struct_in->dr_options_lo);
	if (rc != kIOReturnSuccess)
		return rc;
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
		lockAccel(m_provider);
#if 0
		while (1) {
			if (!surface_client) {
				unlockAccel(m_provider);
				return kIOReturnUnsupported;
			}
			if (surface_client->(byte @0x10CA) == 0)
				break;
			IOLockSleep(/* lock @m_provider+0x960 */, surface_client, 1);
		}
#endif
		scale_options = static_cast<eIOAccelSurfaceScaleBits>(((struct_in->scale_options >> 2) & 1U) | (struct_in->scale_options & 2U));
		/*
		 * Note: Intel GMA 950 actually calls set_scaling.  set_scale there is a wrapper
		 *   for set_scaling that validates the struct size and locks the accelerator
		 */
		rc = surface_client->set_scale(scale_options, &scale, sizeof scale);
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
	if (!surface_client)
		return kIOReturnError;
	surface_client->getBoundsForStatus(&struct_out->inner_width);
	rc = get_status(&struct_out->status);
	if (rc != kIOReturnSuccess)
		return rc;
	struct_out->surface_mode_bits = static_cast<uint32_t>(surface_client->getOriginalModeBits());
	for (i = 0; i < 10; ++i)
		GLLog(2, "%s:   struct_out[%d] == %#x\n", __FUNCTION__, i, reinterpret_cast<uint32_t const*>(struct_out)[i]);
	return kIOReturnSuccess;
}

IOReturn CLASS::reclaim_resources()
{
	GLLog(2, "%s()\n", __FUNCTION__);
	return kIOReturnSuccess;
}

IOReturn CLASS::get_data_buffer(struct sIOGLContextGetDataBuffer* struct_out, size_t* struct_out_size)
{
	GLLog(2, "%s(%p, %lu)\n", __FUNCTION__, struct_out, *struct_out_size);
	if (*struct_out_size < sizeof *struct_out)
		return kIOReturnBadArgument;
	*struct_out_size = sizeof *struct_out;
	bzero(struct_out, *struct_out_size);
	return kIOReturnSuccess;
}

IOReturn CLASS::set_stereo(uintptr_t c1, uintptr_t c2)
{
	GLLog(2, "%s(%lu, %lu)\n", __FUNCTION__, c1, c2);
	return kIOReturnSuccess;
}

IOReturn CLASS::purge_accelerator(uintptr_t c1)
{
	GLLog(2, "%s(%lu)\n", __FUNCTION__, c1);
	return kIOReturnSuccess;
}

#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
IOReturn CLASS::get_channel_memory(struct sIOGLChannelMemoryData* struct_out, size_t* struct_out_size)
{
	GLLog(2, "%s(%p, %lu)\n", __FUNCTION__, struct_out, *struct_out_size);
	bzero(struct_out, *struct_out_size);
	return kIOReturnSuccess;
}
#else
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
	m_mem_type = 0;
	if (rc != kIOReturnSuccess)
		return rc;
	mm = md->createMappingInTask(m_owning_task,
								 0,
								 kIOMapAnywhere);
	md->release();
	if (!mm)
		return kIOReturnNoMemory;
	struct_out->addr[0] = mm->getAddress();
	struct_out->len[0] = static_cast<UInt32>(mm->getLength());
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

IOReturn CLASS::get_query_buffer(uintptr_t c1, struct sIOGLGetQueryBuffer* struct_out, size_t* struct_out_size)
{
	GLLog(2, "%s(%lu, %p, %lu)\n", __FUNCTION__, c1, struct_out, *struct_out_size);
	return kIOReturnUnsupported;
}

IOReturn CLASS::get_notifiers(UInt32*, UInt32*)
{
	GLLog(2, "%s(out1, out2)\n", __FUNCTION__);
	return kIOReturnUnsupported;
}

#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
IOReturn CLASS::new_heap_object(struct sNVGLNewHeapObjectData const* struct_in,
								struct sIOGLNewTextureReturnData* struct_out,
								size_t struct_in_size,
								size_t* struct_out_size)
{
	GLLog(2, "%s(%p, %p, %lu, %lu)\n", __FUNCTION__, struct_in, struct_out, struct_in_size, *struct_out_size);
	return kIOReturnUnsupported;
}
#endif

IOReturn CLASS::kernel_printf(char const* str, size_t str_size)
{
	GLLog(2, "%s: %s\n", __FUNCTION__, str);
	return kIOReturnUnsupported;
}

IOReturn CLASS::nv_rm_config_get(UInt32 const* struct_in,
								 UInt32* struct_out,
								 size_t struct_in_size,
								 size_t* struct_out_size)
{
	GLLog(2, "%s(%p, %p, %lu, %lu)\n", __FUNCTION__, struct_in, struct_out, struct_in_size, *struct_out_size);
	return kIOReturnUnsupported;
}

IOReturn CLASS::nv_rm_config_get_ex(UInt32 const* struct_in,
									UInt32* struct_out,
									size_t struct_in_size,
									size_t* struct_out_size)
{
	GLLog(2, "%s(%p, %p, %lu, %lu)\n", __FUNCTION__, struct_in, struct_out, struct_in_size, *struct_out_size);
	return kIOReturnUnsupported;
}

IOReturn CLASS::nv_client_request(void const* struct_in,
								  void* struct_out,
								  size_t struct_in_size,
								  size_t* struct_out_size)
{
	GLLog(2, "%s(%p, %p, %lu, %lu)\n", __FUNCTION__, struct_in, struct_out, struct_in_size, *struct_out_size);
	return kIOReturnUnsupported;
}

IOReturn CLASS::pageoff_surface_texture(struct sNVGLContextPageoffSurfaceTextureData const* struct_in, size_t struct_in_size)
{
	GLLog(2, "%s(%p, %lu)\n", __FUNCTION__, struct_in, struct_in_size);
	return kIOReturnUnsupported;
}

IOReturn CLASS::get_data_buffer_with_offset(struct sIOGLContextGetDataBuffer* struct_out, size_t* struct_out_size)
{
	GLLog(2, "%s(%p, %lu)\n", __FUNCTION__, struct_out, struct_out_size);
	return kIOReturnUnsupported;
}

IOReturn CLASS::nv_rm_control(UInt32 const* struct_in,
							  UInt32* struct_out,
							  size_t struct_in_size,
							  size_t* struct_out_size)
{
	GLLog(2, "%s(%p, %p, %lu, %lu)\n", __FUNCTION__, struct_in, struct_out, struct_in_size, *struct_out_size);
	return kIOReturnUnsupported;
}

IOReturn CLASS::get_power_state(UInt32*, UInt32*)
{
	GLLog(2, "%s(out1, out2)\n", __FUNCTION__);
	return kIOReturnUnsupported;
}

#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1060
IOReturn CLASS::set_watchdog_timer(uintptr_t c1)
{
	GLLog(2, "%s(%lu)\n", __FUNCTION__, c1);
	return kIOReturnUnsupported;
}

IOReturn CLASS::GetHandleIndex(UInt32*, UInt32*)
{
	GLLog(2, "%s(out1, out2)\n", __FUNCTION__);
	return kIOReturnUnsupported;
}

IOReturn CLASS::ForceTextureLargePages(uintptr_t c1)
{
	GLLog(2, "%s(%lu)\n", __FUNCTION__, c1);
	return kIOReturnUnsupported;
}
#endif
