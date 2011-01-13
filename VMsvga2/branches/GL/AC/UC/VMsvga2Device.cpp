/*
 *  VMsvga2Device.cpp
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on October 11th 2009.
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

#include <IOKit/IOLib.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#define GL_INCL_SHARED
#include "GLCommon.h"
#include "VLog.h"
#include "UCGLDCommonTypes.h"
#include "UCMethods.h"
#include "VMsvga2Accel.h"
#include "VMsvga2Device.h"
#include "VMsvga2Shared.h"
#include "VMsvga2Surface.h"

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

	*c1 = 0U;	// used by GLD to discern Intel 915/965/Ironlake(HD)
#ifdef GL_DEV
#if 0
	*c2 = static_cast<uint32_t>(m_provider->getLogLevelGLD()) & 7U;		// TBD: is this safe?
#else
	*c2 = 1U;	// set GLD logging to error level
#endif
#else
	*c2 = 0U;
#endif
	*c3 = vram_size;	// total memory available for textures (no accounting by VMsvga2)
	*c4 = vram_size;	// total VRAM size
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1060
	*c5 = m_provider->getSurfaceRootUUID();
#else
	*c5 = 0;
#endif
	DVLog(2, "%s(*%u, *%u, *%u, *%u, *%#x)\n", __FUNCTION__, *c1, *c2, *c3, *c4, *c5);
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::get_surface_info(uintptr_t surface_id, uint32_t* mode_bits, uint32_t* width, uint32_t* height)
{
	VMsvga2Surface* surface_client;
	uint32_t inner_width, inner_height, outer_width, outer_height;

	if (!m_provider)
		return kIOReturnNotReady;
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
	DVLog(2, "%s(%#lx, *%#x, *%u, *%u)\n", __FUNCTION__, surface_id, *mode_bits, *width, *height);
	return kIOReturnSuccess;

bad_exit:
	*mode_bits = 0U;
	*width = 0U;
	*height = 0U;
	return kIOReturnBadArgument;
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
	/*
	 * TBD
	 */
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::new_texture(struct VendorNewTextureDataStruc const* struct_in,
							struct sIONewTextureReturnData* struct_out,
							size_t struct_in_size,
							size_t* struct_out_size)
{
	VMsvga2TextureBuffer* p = 0;

	DVLog(2, "%s(struct_in, struct_out, %lu, %lu)\n", __FUNCTION__, struct_in_size, *struct_out_size);
	if (struct_in_size < sizeof *struct_in ||
		*struct_out_size < sizeof *struct_out)
		return kIOReturnBadArgument;
	if (!m_shared)
		return kIOReturnNotReady;
#if LOGGING_LEVEL >= 1
	if (m_log_level >= 3) {
		DVLog(3, "%s:   type == %u, num_faces == %u, num_mipmaps == %u, min_mipmap == %u, bytespp == %u\n", __FUNCTION__,
			  struct_in->type, struct_in->num_faces, struct_in->num_mipmaps, struct_in->min_mipmap, struct_in->bytespp);
		DVLog(3, "%s:   width == %u, height == %u, depth == %u, f0 == %#x, pitch == %u, read_only == %u\n", __FUNCTION__,
			  struct_in->width, struct_in->height, struct_in->depth, struct_in->f0, struct_in->pitch, struct_in->read_only);
		DVLog(3, "%s:   f1... %#x, %#x, %#x\n", __FUNCTION__,
			  struct_in->f1[0], struct_in->f1[1], struct_in->f1[2]);
		DVLog(3, "%s:   size... %u, %u\n", __FUNCTION__,
			  struct_in->size[0], struct_in->size[1]);
		DVLog(3, "%s:   pixels... %#llx, %#llx, %#llx\n", __FUNCTION__,
			  struct_in->pixels[0], struct_in->pixels[1], struct_in->pixels[2]);
	}
#endif
	if (struct_in->num_faces != 1U &&
		struct_in->num_faces != 6U)
		return kIOReturnBadArgument;
	*struct_out_size = sizeof *struct_out;
	bzero(struct_out, sizeof *struct_out);
	m_shared->lockShared();
#if 0
	m_provider->acceleratorWaitEnabled();
#endif
	switch (struct_in->type) {
		case TEX_TYPE_SURFACE:
			p = m_shared->new_surface_texture(struct_in->size[0],
											  struct_in->size[1],
											  &struct_out->sys_obj_addr);
			if (!p) {
				m_shared->unlockShared();
				return kIOReturnNoResources;
			}
			break;
		case TEX_TYPE_IOSURFACE:
			p = m_shared->new_iosurface_texture(struct_in->size[0],
												struct_in->size[1],
												static_cast<uint32_t>(struct_in->pixels[0]),
												static_cast<uint32_t>(struct_in->pixels[0] >> 32),
												&struct_out->sys_obj_addr);
			if (!p) {
				m_shared->unlockShared();
				return kIOReturnNoResources;
			}
			break;
		case TEX_TYPE_VB:
			p = m_shared->new_texture(struct_in->size[0],
									  0,
									  0ULL,
									  0,
									  struct_in->read_only,
									  &struct_out->tx_data,
									  &struct_out->sys_obj_addr);
			if (!p) {
				m_shared->unlockShared();
				return kIOReturnNoResources;
			}
			break;
		case TEX_TYPE_AGPREF:
			p = m_shared->new_agpref_texture(struct_in->pixels[0],
											 struct_in->pixels[1],
											 struct_in->size[0],
											 struct_in->read_only,
											 &struct_out->sys_obj_addr);
			if (!p) {
				m_shared->unlockShared();
				return kIOReturnNoResources;
			}
			if (p->linked_agp)
				struct_out->tx_data = p->linked_agp->agp_addr;
			break;
		case TEX_TYPE_STD:
			p = m_shared->new_texture(struct_in->size[0],
									  struct_in->size[1],
									  0ULL,
									  0,
									  struct_in->read_only,
									  &struct_out->tx_data,
									  &struct_out->sys_obj_addr);
			if (!p) {
				m_shared->unlockShared();
				return kIOReturnNoResources;
			}
			break;
		case TEX_TYPE_OOB:
			p = m_shared->new_texture(struct_in->size[0],
									  struct_in->size[1],
									  struct_in->pixels[1],
									  static_cast<uint32_t>(struct_in->pixels[0]),
									  struct_in->read_only,
									  &struct_out->tx_data,
									  &struct_out->sys_obj_addr);
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
	p->num_faces = struct_in->num_faces;
	p->num_mipmaps = struct_in->num_mipmaps;
	p->min_mipmap = struct_in->min_mipmap;
	p->width = struct_in->width;
	p->height = struct_in->height;
	p->depth = struct_in->depth;
	p->f0 = struct_in->f0;
	p->pitch = struct_in->pitch;
	p->bytespp = struct_in->bytespp;
	if (!m_shared->initializeTexture(p, struct_in)) {
		m_shared->unlockShared();
		return kIOReturnError;
	}
	m_shared->unlockShared();
#if LOGGING_LEVEL >= 1
	if (m_log_level >= 3) {
		DVLog(3, "%s:   struct_out.pad == %#x\n", __FUNCTION__, struct_out->pad);
		DVLog(3, "%s:   struct_out.tx_data == %#llx\n", __FUNCTION__, struct_out->tx_data);
		DVLog(3, "%s:   struct_out.sys_obj_addr == %#llx\n", __FUNCTION__, struct_out->sys_obj_addr);
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
	/*
	 * TBD
	 */
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
	DVLog(3, "%s:   mapped to client @%#llx\n", __FUNCTION__, struct_out->addr);
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
