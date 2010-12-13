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

#define CLIENT_SYS_OBJS_SIZE 8192U

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
#pragma mark struct definitions
#pragma mark -

struct VMsvga2TextureBuffer
{
	uint32_t object_id;	// offset 0x00
	int32_t stamps[3];	// offset 0x04
	int32_t refcount;	// offset 0x10
	uint8_t reserved[2];// offset 0x14
	uint8_t obj_type;	// offset 0x16
	uint8_t pad[38];	// offset 0x17
	uint8_t version;	// offset 0x3D
	uint8_t flags[2];	// offset 0x3E
	uint32_t width;		// offset 0x40
	uint16_t height;	// offset 0x44
	uint16_t depth;		// offset 0x46
	uint32_t f1;		// offset 0x48
	uint32_t pitch;		// offset 0x4C
	uint32_t pad2[2];	// offset 0x50
	uint16_t bytespp;	// offset 0x58
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
	if (m_client_sys_objs_map) {
		m_client_sys_objs_map->release();
		m_client_sys_objs_map = 0;
		m_client_sys_objs_ptr = 0;
	}
}

HIDDEN
IOReturn CLASS::allocClientSysObj(struct GLDSysObject** sys_obj, mach_vm_address_t* client_addr)
{
	size_t i;
	GLDSysObject* p;
	IOBufferMemoryDescriptor* client_sys_objs;
	if (!m_client_sys_objs_map) {
		client_sys_objs = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,
																	  kIOMemoryKernelUserShared | kIOMemoryPageable,
																	  CLIENT_SYS_OBJS_SIZE,
																	  page_size);
		if (!client_sys_objs)
			return kIOReturnNoMemory;
		m_client_sys_objs_map = client_sys_objs->createMappingInTask(m_owning_task,
																	 0,
																	 kIOMapAnywhere);
		if (!m_client_sys_objs_map) {
			client_sys_objs->release();
			return kIOReturnNoMemory;
		}
		m_client_sys_objs_ptr = client_sys_objs->getBytesNoCopy();
		client_sys_objs->release();
		bzero(m_client_sys_objs_ptr, CLIENT_SYS_OBJS_SIZE);
	}
	p = static_cast<GLDSysObject*>(m_client_sys_objs_ptr);
	for (i = 0U; i != CLIENT_SYS_OBJS_SIZE / sizeof *p; ++i)
		if (!p[i].in_use) {
			bzero(&p[i], sizeof *p);
			p[i].in_use = 1;
			*sys_obj = &p[i];
			*client_addr = m_client_sys_objs_map->getAddress() + i * sizeof *p;
			return kIOReturnSuccess;
		}
	return kIOReturnNoResources;
}

HIDDEN
GLDSysObject* CLASS::findClientSysObj(uint32_t object_id)
{
	GLDSysObject* p;
	size_t i;
	if (!m_client_sys_objs_map)
		return 0;
	p = static_cast<GLDSysObject*>(m_client_sys_objs_ptr);
	for (i = 0; i != CLIENT_SYS_OBJS_SIZE / sizeof *p; ++i)
		if (p[i].in_use && p[i].object_id == object_id)
			return &p[i];
	return 0;
}

HIDDEN
VMsvga2TextureBuffer* CLASS::new_surface_texture(uint32_t, uint32_t, uint64_t*)
{
	return 0;
}

HIDDEN
VMsvga2TextureBuffer* CLASS::new_iosurface_texture(uint32_t, uint32_t, uint32_t, uint32_t, uint64_t*)
{
	return 0;
}

HIDDEN
VMsvga2TextureBuffer* CLASS::new_texture_internal(uint32_t, uint32_t, uint64_t, uint32_t, uint32_t, uint64_t*, uint64_t*)
{
	return 0;
}

HIDDEN
VMsvga2TextureBuffer* CLASS::new_agpref_texture(uint64_t pixels1,
												uint64_t pixels2,
												uint32_t texture_size,
												uint32_t unknown /* always 0 */,
												uint64_t* client_addr)
{
#if 0
	void* p;	// var_3c
	uint64_t var_28;

	p = new_agp_texture(pixels2, texture_size, unknown, &var_28);
	if (!p)
		return p;
	// 17D06
	return 0;
#endif
	static VMsvga2TextureBuffer dummy;
	GLDSysObject* sys_obj;

	if (allocClientSysObj(&sys_obj, client_addr) != kIOReturnSuccess)
		return 0;
	sys_obj->object_id = m_next_obj_id++;
	sys_obj->type = 6;
	return &dummy;
}

HIDDEN
bool CLASS::initializeTexture(VMsvga2TextureBuffer*, VendorNewTextureDataStruc const*)
{
	return 1;
}

/*
 * TBD:
 *   very inefficient, especially since there
 *   are a lot of instances in each command buffer
 */
HIDDEN
void CLASS::derefSysObject(uint32_t object_id)
{
	GLDSysObject* p = findClientSysObj(object_id);
	if (!p)
		return;
	if (__sync_fetch_and_add(&p->refcount, -0x10000) == 0x10000)
		bzero(p, sizeof *p);	// delete texture
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
	m_log_level = imax(m_provider->getLogLevelGLD(), m_provider->getLogLevelAC());
	m_next_obj_id = 483U;
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
	int i;
	VMsvga2TextureBuffer* p = 0;
#if 0
	uint8_t* q;
#endif

	DVLog(2, "%s(struct_in, struct_out, %lu, %lu)\n", __FUNCTION__, struct_in_size, *struct_out_size);
	if (struct_in_size < sizeof *struct_in ||
		*struct_out_size < sizeof *struct_out)
		return kIOReturnBadArgument;
	for (i = 0; i < 12; ++i)
		DVLog(2, "%s:   struct_t[%d] == %#x\n", __FUNCTION__, i, reinterpret_cast<uint32_t const*>(struct_in)[i]);
	for (i = 0; i < 3; ++i)
		DVLog(2, "%s:   struct_t[%d] == %#llx\n", __FUNCTION__, i + 12, struct_in->pixels[i]);
	if (struct_in->version != 1U &&
		struct_in->version != 6U)
		return kIOReturnBadArgument;
	*struct_out_size = sizeof *struct_out;
	bzero(struct_out, sizeof *struct_out);
	// m_provider @+0x84
//	lockAccel(m_provider);
//	m_provider->acceleratorWaitEnabled();
	// shared object @+0x7C
	switch (struct_in->type) {
		case 1:
			p = new_surface_texture(struct_in->size,
									struct_in->f4,
									&struct_out->addr);
			if (!p) {
				// unlockAccel(m_provider);
				return kIOReturnNoResources;
			}
			break;
		case 2:
			p = new_iosurface_texture(struct_in->size,
									  struct_in->f4,
									  static_cast<uint32_t>(struct_in->pixels[0]),
									  static_cast<uint32_t>(struct_in->pixels[0] >> 32),
									  &struct_out->addr);
			if (!p) {
				// unlockAccel(m_provider);
				return kIOReturnNoResources;
			}
			break;
		case 3:
			p = new_texture_internal(struct_in->size,
									 0,
									 0ULL,
									 0,
									 struct_in->f3[0],
									 &struct_out->data,
									 &struct_out->addr);
			if (!p) {
				// unlockAccel(m_provider);
				return kIOReturnNoResources;
			}
			break;
		case 6:
			p = new_agpref_texture(struct_in->pixels[0],
								   struct_in->pixels[1],
								   struct_in->size,
								   struct_in->f3[0],
								   &struct_out->addr);
			if (!p) {
				// unlockAccel(m_provider);
				return kIOReturnNoResources;
			}
#if 0
			q = reinterpret_cast<uint8_t**>(p)[32];
			struct_out->data = *reinterpret_cast<uint64_t*>(&q[140]);
#endif
			break;
		case 8:
			p = new_texture_internal(struct_in->size,
									 struct_in->f4,
									 0ULL,
									 0,
									 struct_in->f3[0],
									 &struct_out->data,
									 &struct_out->addr);
			if (!p) {
				// unlockAccel(m_provider);
				return kIOReturnNoResources;
			}
			break;
		case 9:
			p = new_texture_internal(struct_in->size,
									 struct_in->f4,
									 struct_in->pixels[1],
									 static_cast<uint32_t>(struct_in->pixels[0]),
									 struct_in->f3[0],
									 &struct_out->data,
									 &struct_out->addr);
			if (!p) {
				// unlockAccel(m_provider);
				return kIOReturnNoResources;
			}
			break;
		case 0:
		case 4:
		case 5:
		case 7:
		default:
			// zeros struct_out
			// unlockAccel(m_provider);
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
	if (!initializeTexture(p, struct_in)) {
//		unlockAccel(m_provider);
		return kIOReturnError;
	}
//	unlockAccel(m_provider);
#if 0
	DVLog(2, "%s:   struct_out.pad == %#x\n", __FUNCTION__, struct_out->pad);
	DVLog(2, "%s:   struct_out.data == %#llx\n", __FUNCTION__, struct_out->data);
	DVLog(2, "%s:   struct_out.addr == %#llx\n", __FUNCTION__, struct_out->addr);
#endif
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::delete_texture(uintptr_t texture_id)
{
	DVLog(2, "%s(%lu)\n", __FUNCTION__, texture_id);
	GLDSysObject* p = findClientSysObj(static_cast<uint32_t>(texture_id));
	if (!p)
		return kIOReturnSuccess;
	bzero(p, sizeof *p);
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
