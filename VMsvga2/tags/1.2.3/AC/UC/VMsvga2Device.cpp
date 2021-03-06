/*
 *  VMsvga2Device.cpp
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on October 11th 2009.
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
#include "VMsvga2Device.h"
#include "ACMethods.h"

#define CLASS VMsvga2Device
#define super IOUserClient
OSDefineMetaClassAndStructors(VMsvga2Device, IOUserClient);

#if LOGGING_LEVEL >= 1
#define DVLog(log_level, fmt, ...) do { if (log_level <= m_log_level) VLog("IODV: ", fmt, ##__VA_ARGS__); } while (false)
#else
#define DVLog(log_level, fmt, ...)
#endif

static IOExternalMethod iofbFuncsCache[kIOVMDeviceNumMethods] =
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
#pragma mark IOUserClient Methods
#pragma mark -

IOExternalMethod* CLASS::getTargetAndMethodForIndex(IOService** targetP, UInt32 index)
{
	if (index >= kIOVMDeviceNumMethods)
		DVLog(2, "%s(%p, %u)\n", __FUNCTION__, targetP, index);
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
	return &m_funcs_cache[index];
}

IOReturn CLASS::clientClose()
{
	DVLog(2, "%s\n", __FUNCTION__);
	if (!terminate(0))
		IOLog("%s: terminate failed\n", __FUNCTION__);
	m_owning_task = 0;
	m_provider = 0;
	return kIOReturnSuccess;
}

IOReturn CLASS::clientMemoryForType(UInt32 type, IOOptionBits* options, IOMemoryDescriptor** memory)
{
	DVLog(2, "%s(%u, %p, %p)\n", __FUNCTION__, type, options, memory);
	if (!options || !memory)
		return kIOReturnBadArgument;
	IOBufferMemoryDescriptor* md = IOBufferMemoryDescriptor::withOptions(kIOMemoryPageable | kIOMemoryKernelUserShared,
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
	m_log_level = m_provider->getLogLevelAC();
	return super::start(provider);
}

bool CLASS::initWithTask(task_t owningTask, void* securityToken, UInt32 type)
{
	m_log_level = 1;
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
#pragma mark IONVDevice Methods
#pragma mark -

IOReturn CLASS::create_shared()
{
	DVLog(2, "%s()\n", __FUNCTION__);
	return kIOReturnSuccess;
}

IOReturn CLASS::get_config(UInt32* c1, UInt32* c2, UInt32* c3, UInt32* c4, UInt32* c5)
{
	UInt32 const vram_size = m_provider->getVRAMSize();

	*c1 = 0;
	*c2 = 0;
	*c3 = vram_size;
	*c4 = vram_size;
	*c5 = 0;
	DVLog(2, "%s(*%u, *%u, *%u, *%u, *0x%x)\n", __FUNCTION__, *c1, *c2, *c3, *c4, *c5);
	return kIOReturnSuccess;
}

IOReturn CLASS::get_surface_info(uintptr_t c1, UInt32* c2, UInt32* c3, UInt32* c4)
{
	*c2 = 0x4061;
	*c3 = 0x4062;
	*c4 = 0x4063;
	DVLog(2, "%s(%lu, *%u, *%u, *%u)\n", __FUNCTION__, c1, *c2, *c3, *c4);
	return kIOReturnSuccess;
}

IOReturn CLASS::get_name(char* name_out, size_t* name_out_size)
{
	DVLog(2, "%s(%p, %lu)\n", __FUNCTION__, name_out, *name_out_size);
	strlcpy(name_out, "VMsvga2", *name_out_size);
	return kIOReturnSuccess;
}

IOReturn CLASS::wait_for_stamp(uintptr_t c1)
{
	DVLog(2, "%s(%lu)\n", __FUNCTION__, c1);
	return kIOReturnSuccess;
}

IOReturn CLASS::new_texture(struct VendorNewTextureDataRec const* struct_in,
							struct sIONewTextureReturnData* struct_out,
							size_t struct_in_size,
							size_t* struct_out_size)
{
	DVLog(2, "%s(%p, %p, %lu, %lu)\n", __FUNCTION__, struct_in, struct_out, struct_in_size, *struct_out_size);
	bzero(struct_out, *struct_out_size);
	return kIOReturnSuccess;
}

IOReturn CLASS::delete_texture(uintptr_t c1)
{
	DVLog(2, "%s(%lu)\n", __FUNCTION__, c1);
	return kIOReturnSuccess;
}

IOReturn CLASS::page_off_texture(struct sIODevicePageoffTexture const* struct_in, size_t struct_in_size)
{
	DVLog(2, "%s(%p, %lu)\n", __FUNCTION__, struct_in, struct_in_size);
	return kIOReturnSuccess;
}

IOReturn CLASS::get_channel_memory(struct sIODeviceChannelMemoryData* struct_out, size_t* struct_out_size)
{
	DVLog(2, "%s(%p, %lu)\n", __FUNCTION__, struct_out, *struct_out_size);
	memset(struct_out, 0x55U, *struct_out_size);
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark NVDevice Methods
#pragma mark -

IOReturn CLASS::kernel_printf(char const* str, size_t str_size)
{
	DVLog(2, "%s: %s\n", __FUNCTION__, str);	// TBD: limit str by str_size
	return kIOReturnSuccess;
}

IOReturn CLASS::nv_rm_config_get(UInt32 const* struct_in,
								 UInt32* struct_out,
								 size_t struct_in_size,
								 size_t* struct_out_size)
{
	DVLog(2, "%s(%p, %p, %lu, %lu)\n", __FUNCTION__, struct_in, struct_out, struct_in_size, *struct_out_size);
	if (*struct_out_size < struct_in_size)
		struct_in_size = *struct_out_size;
	memcpy(struct_out, struct_in, struct_in_size);
	return kIOReturnSuccess;
}

IOReturn CLASS::nv_rm_config_get_ex(UInt32 const* struct_in,
									UInt32* struct_out,
									size_t struct_in_size,
									size_t* struct_out_size)
{
	DVLog(2, "%s(%p, %p, %lu, %lu)\n", __FUNCTION__, struct_in, struct_out, struct_in_size, *struct_out_size);
	if (*struct_out_size < struct_in_size)
		struct_in_size = *struct_out_size;
	memcpy(struct_out, struct_in, struct_in_size);
	return kIOReturnSuccess;
}

IOReturn CLASS::nv_rm_control(UInt32 const* struct_in,
							  UInt32* struct_out,
							  size_t struct_in_size,
							  size_t* struct_out_size)
{
	DVLog(2, "%s(%p, %p, %lu, %lu)\n", __FUNCTION__, struct_in, struct_out, struct_in_size, *struct_out_size);
	if (*struct_out_size < struct_in_size)
		struct_in_size = *struct_out_size;
	memcpy(struct_out, struct_in, struct_in_size);
	return kIOReturnSuccess;
}
