/*
 *  VMsvga2Device.cpp
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on October 11th 2009.
 *  Copyright 2009-2010 Zenith432. All rights reserved.
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
#include "VMsvga2Device.h"
#include "VMsvga2Shared.h"
#include "ACMethods.h"
#include "UCTypes.h"

#define CLASS VMsvga2Device
#define super IOUserClient
OSDefineMetaClassAndStructors(VMsvga2Device, IOUserClient);

#if LOGGING_LEVEL >= 1
#define DVLog(log_level, ...) do { if (log_level <= m_log_level) VLog("IODV: ", ##__VA_ARGS__); } while (false)
#else
#define DVLog(log_level, ...)
#endif

#define HIDDEN __attribute__((visibility("hidden")))

static
IOExternalMethod iofbFuncsCache[kIOVMDeviceNumMethods] =
{
// IONVDevice
{0, reinterpret_cast<IOMethod>(&CLASS::create_shared), kIOUCScalarIScalarO, 0, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::get_config), kIOUCScalarIScalarO, 0, 5},
{0, reinterpret_cast<IOMethod>(&CLASS::get_surface_info), kIOUCScalarIScalarO, 1, 3},
{0, reinterpret_cast<IOMethod>(&CLASS::get_name), kIOUCScalarIStructO, 0, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::wait_for_stamp), kIOUCScalarIScalarO, 1, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::new_texture), kIOUCStructIStructO, kIOUCVariableStructureSize, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::delete_texture), kIOUCScalarIScalarO, 1, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::page_off_texture), kIOUCScalarIStructI, 0, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::get_channel_memory), kIOUCScalarIStructO, 0, kIOUCVariableStructureSize},
// NVDevice
{0, reinterpret_cast<IOMethod>(&CLASS::kernel_printf), kIOUCScalarIStructI, 0, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::nv_rm_config_get), kIOUCStructIStructO, kIOUCVariableStructureSize, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::nv_rm_config_get_ex), kIOUCStructIStructO, kIOUCVariableStructureSize, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::nv_rm_control), kIOUCStructIStructO, kIOUCVariableStructureSize, kIOUCVariableStructureSize},
};

#pragma mark -
#pragma mark Private Methods
#pragma mark -

HIDDEN
void CLASS::Cleanup()
{
	if (m_channel_memory_map) {
		m_channel_memory_map->release();
		m_channel_memory_map = 0;
	}
	if (m_shared) {
		m_shared->release();
		m_shared = 0;
	}
}

#pragma mark -
#pragma mark IOUserClient Methods
#pragma mark -

IOExternalMethod* CLASS::getTargetAndMethodForIndex(IOService** targetP, UInt32 index)
{
	if (index >= kIOVMDeviceNumMethods)
		DVLog(2, "%s(target_out, %u)\n", __FUNCTION__, static_cast<unsigned>(index));
	if (!targetP || index >= kIOVMDeviceNumMethods)
		return 0;
#if 0
	if (index >= kIOVMDeviceNumMethods) {
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
	DVLog(2, "%s\n", __FUNCTION__);
	Cleanup();
	if (!terminate(0))
		IOLog("%s: terminate failed\n", __FUNCTION__);
	m_owning_task = 0;
	m_provider = 0;
	return kIOReturnSuccess;
}

IOReturn CLASS::clientMemoryForType(UInt32 type, IOOptionBits* options, IOMemoryDescriptor** memory)
{
	DVLog(2, "%s(%u, options_out, memory_out)\n", __FUNCTION__, static_cast<unsigned>(type));
	if (!options || !memory)
		return kIOReturnBadArgument;
	IOBufferMemoryDescriptor* md = IOBufferMemoryDescriptor::withOptions(kIOMemoryPageable |
																		 kIOMemoryKernelUserShared |
																		 kIODirectionInOut,
																		 PAGE_SIZE,
																		 PAGE_SIZE);
	*memory = md;
	*options = kIOMapAnywhere;
	return kIOReturnSuccess;
#if 0
	return super::clientMemoryForType(type, options, memory);
#endif
}

#if 0
/*
 * Note: NVDevice has an override on this method
 *   In OS 10.6 redirects methods 14 - 19 to local code implemented in externalMethod()
 *     methods 16 - 17 call NVDevice::getSupportedEngines(ulong *,ulong &,ulong)
 *     method 18 calls NVDevice::getGRInfo(NV2080_CTRL_GR_INFO *,ulong)
 *     method 19 calls NVDevice::getArchImplBus(ulong *,ulong)
 *
 *   iofbFuncsCache only defines methods 0 - 12, so 13 is missing and 14 - 19 as above.
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
	m_log_level = imax(m_provider->getLogLevelGLD(), m_provider->getLogLevelAC());
	return super::start(provider);
}

bool CLASS::initWithTask(task_t owningTask, void* securityToken, UInt32 type)
{
	m_log_level = LOGGING_LEVEL;
	if (!super::initWithTask(owningTask, securityToken, type))
		return false;
	m_owning_task = owningTask;
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
#pragma mark IONVDevice Methods
#pragma mark -

HIDDEN
IOReturn CLASS::create_shared()
{
	DVLog(2, "%s()\n", __FUNCTION__);
	// lockAccel()
	if (m_shared) {
		// unlockAccel
		return kIOReturnError;
	}
	m_shared = VMsvga2Shared::factory(m_owning_task, m_provider, m_log_level);
	if (!m_shared) {
		// unlockAccel
		return kIOReturnNoResources;
	}
	// unlockAccel()
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::get_config(uint32_t* c1, uint32_t* c2, uint32_t* c3, uint32_t* c4, uint32_t* c5)
{
	uint32_t const vram_size = m_provider->getVRAMSize();

	*c1 = 0;
	*c2 = static_cast<uint32_t>(m_provider->getLogLevelGLD()) & 7U;		// TBD: is this safe?
	*c3 = vram_size;
	*c4 = vram_size;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1060
	*c5 = m_provider->getSurfaceRootUUID();
#else
	*c5 = 0;
#endif
	DVLog(2, "%s(*%u, *%u, *%u, *%u, *%#x)\n", __FUNCTION__, *c1, *c2, *c3, *c4, *c5);
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::get_surface_info(uintptr_t c1, uint32_t* c2, uint32_t* c3, uint32_t* c4)
{
	*c2 = 0x4061U;
	*c3 = 0x4062U;
	*c4 = 0x4063U;
	DVLog(2, "%s(%lu, *%u, *%u, *%u)\n", __FUNCTION__, c1, *c2, *c3, *c4);
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::get_name(char* name_out, size_t* name_out_size)
{
	DVLog(2, "%s(name_out, %lu)\n", __FUNCTION__, *name_out_size);
	strlcpy(name_out, "VMsvga2", *name_out_size);
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::wait_for_stamp(uintptr_t c1)
{
	DVLog(2, "%s(%lu)\n", __FUNCTION__, c1);
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::new_texture(struct VendorNewTextureDataStruc const* struct_in,
							struct sIONewTextureReturnData* struct_out,
							size_t struct_in_size,
							size_t* struct_out_size)
{
#if LOGGING_LEVEL >= 1
	int i;
#endif
	VMsvga2TextureBuffer* p = 0;

	DVLog(2, "%s(struct_in, struct_out, %lu, %lu)\n", __FUNCTION__, struct_in_size, *struct_out_size);
	if (struct_in_size < sizeof *struct_in ||
		*struct_out_size < sizeof *struct_out)
		return kIOReturnBadArgument;
	if (!m_shared)
		return kIOReturnNotReady;
#if LOGGING_LEVEL >= 1
	if (m_log_level >= 3) {
		for (i = 0; i < 12; ++i)
			DVLog(3, "%s:   struct_t[%d] == %#x\n", __FUNCTION__, i, reinterpret_cast<uint32_t const*>(struct_in)[i]);
		for (i = 0; i < 3; ++i)
			DVLog(3, "%s:   struct_t[%d] == %#llx\n", __FUNCTION__, i + 12, struct_in->pixels[i]);
	}
#endif
	if (struct_in->version != 1U &&
		struct_in->version != 6U)
		return kIOReturnBadArgument;
	*struct_out_size = sizeof *struct_out;
	bzero(struct_out, sizeof *struct_out);
	m_shared->lockShared();
#if 0
	m_provider->acceleratorWaitEnabled();
#endif
	switch (struct_in->type) {
		case 1:
			p = m_shared->new_surface_texture(struct_in->size,
											  struct_in->f4,
											  &struct_out->addr);
			if (!p) {
				m_shared->unlockShared();
				return kIOReturnNoResources;
			}
			break;
		case 2:
			p = m_shared->new_iosurface_texture(struct_in->size,
												struct_in->f4,
												static_cast<uint32_t>(struct_in->pixels[0]),
												static_cast<uint32_t>(struct_in->pixels[0] >> 32),
												&struct_out->addr);
			if (!p) {
				m_shared->unlockShared();
				return kIOReturnNoResources;
			}
			break;
		case 3:
			p = m_shared->new_texture(struct_in->size,
									  0,
									  0ULL,
									  0,
									  struct_in->f3[0],
									  &struct_out->data,
									  &struct_out->addr);
			if (!p) {
				m_shared->unlockShared();
				return kIOReturnNoResources;
			}
			break;
		case 6:
			p = m_shared->new_agpref_texture(struct_in->pixels[0],
											 struct_in->pixels[1],
											 struct_in->size,
											 struct_in->f3[0],
											 &struct_out->addr);
			if (!p) {
				m_shared->unlockShared();
				return kIOReturnNoResources;
			}
			if (p->linked_agp)
				struct_out->data = p->linked_agp->agp_addr;
			break;
		case 8:
			p = m_shared->new_texture(struct_in->size,
									  struct_in->f4,
									  0ULL,
									  0,
									  struct_in->f3[0],
									  &struct_out->data,
									  &struct_out->addr);
			if (!p) {
				m_shared->unlockShared();
				return kIOReturnNoResources;
			}
			break;
		case 9:
			p = m_shared->new_texture(struct_in->size,
									  struct_in->f4,
									  struct_in->pixels[1],
									  static_cast<uint32_t>(struct_in->pixels[0]),
									  struct_in->f3[0],
									  &struct_out->data,
									  &struct_out->addr);
			if (!p) {
				m_shared->unlockShared();
				return kIOReturnNoResources;
			}
			break;
		default:
			// zeros struct_out
			m_shared->unlockShared();
			return kIOReturnNoResources;
	}
	p->version = struct_in->version;
	p->flags[0] = struct_in->flags[0];
	p->flags[1] = struct_in->flags[1];
	p->width = struct_in->width;
	p->height = struct_in->height;
	p->depth = struct_in->depth;
	p->f1 = struct_in->f1;
	p->pitch = struct_in->pitch;
	p->bytespp = struct_in->bytespp;
	if (!m_shared->initializeTexture(p, struct_in)) {
		m_shared->unlockShared();
		return kIOReturnError;
	}
	m_shared->unlockShared();
#if LOGGING_LEVEL >= 1
	if (m_log_level >= 3) {
		DVLog(2, "%s:   struct_out.pad == %#x\n", __FUNCTION__, struct_out->pad);
		DVLog(2, "%s:   struct_out.data == %#llx\n", __FUNCTION__, struct_out->data);
		DVLog(2, "%s:   struct_out.addr == %#llx\n", __FUNCTION__, struct_out->addr);
	}
#endif
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::delete_texture(uintptr_t texture_id)
{
	VMsvga2TextureBuffer* p;
	DVLog(2, "%s(%lu)\n", __FUNCTION__, texture_id);
	if (!m_shared)
		return kIOReturnNotReady;
	m_shared->lockShared();
	p = m_shared->findTextureBuffer(static_cast<uint32_t>(texture_id));
	if (p)
		m_shared->delete_texture(p);
	m_shared->unlockShared();
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::page_off_texture(struct sIODevicePageoffTexture const* struct_in, size_t struct_in_size)
{
	DVLog(2, "%s(struct_in, %lu)\n", __FUNCTION__, struct_in_size);
	DVLog(2, "%s:   struct_in { %#x, %#x }\n", __FUNCTION__, struct_in->data[0], struct_in->data[1]);
	return kIOReturnSuccess;
}

/*
 * Note: a dword at offset 0x40 in the channel memory is
 *   a stamp, indicating completion of operations
 *   on GLD sys objects (textures, etc.)
 */
HIDDEN
IOReturn CLASS::get_channel_memory(struct sIODeviceChannelMemoryData* struct_out, size_t* struct_out_size)
{
	IOMemoryDescriptor* channel_memory;
	DVLog(2, "%s(struct_out, %lu)\n", __FUNCTION__, *struct_out_size);
	if (*struct_out_size < sizeof *struct_out)
		return kIOReturnBadArgument;
	if (m_channel_memory_map) {
		*struct_out_size = sizeof *struct_out;
		struct_out->addr = m_channel_memory_map->getAddress();
		return kIOReturnSuccess;
	}
	channel_memory = m_provider->getChannelMemory();
	if (!channel_memory)
		return kIOReturnNoResources;
	m_channel_memory_map = channel_memory->createMappingInTask(m_owning_task,
															   0,
															   kIOMapAnywhere /* | kIOMapUnique */);
	if (!m_channel_memory_map)
		return kIOReturnNoResources;
	*struct_out_size = sizeof *struct_out;
	struct_out->addr = m_channel_memory_map->getAddress();
	DVLog(2, "%s:   mapped to client @%#llx\n", __FUNCTION__, struct_out->addr);
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark NVDevice Methods
#pragma mark -

HIDDEN
IOReturn CLASS::kernel_printf(char const* str, size_t str_size)
{
	DVLog(2, "%s: %.80s\n", __FUNCTION__, str);	// TBD: limit str by str_size
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::nv_rm_config_get(uint32_t const* struct_in,
								 uint32_t* struct_out,
								 size_t struct_in_size,
								 size_t* struct_out_size)
{
	DVLog(2, "%s(struct_in, struct_out, %lu, %lu)\n", __FUNCTION__, struct_in_size, *struct_out_size);
	if (*struct_out_size < struct_in_size)
		struct_in_size = *struct_out_size;
	memcpy(struct_out, struct_in, struct_in_size);
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::nv_rm_config_get_ex(uint32_t const* struct_in,
									uint32_t* struct_out,
									size_t struct_in_size,
									size_t* struct_out_size)
{
	DVLog(2, "%s(struct_in, struct_out, %lu, %lu)\n", __FUNCTION__, struct_in_size, *struct_out_size);
	if (*struct_out_size < struct_in_size)
		struct_in_size = *struct_out_size;
	memcpy(struct_out, struct_in, struct_in_size);
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::nv_rm_control(uint32_t const* struct_in,
							  uint32_t* struct_out,
							  size_t struct_in_size,
							  size_t* struct_out_size)
{
	DVLog(2, "%s(struct_in, struct_out, %lu, %lu)\n", __FUNCTION__, struct_in_size, *struct_out_size);
	if (*struct_out_size < struct_in_size)
		struct_in_size = *struct_out_size;
	memcpy(struct_out, struct_in, struct_in_size);
	return kIOReturnSuccess;
}
