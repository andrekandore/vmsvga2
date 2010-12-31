/*
 *  VMsvga2Shared.cpp
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on December 15th 2010.
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

#include <IOKit/IOBufferMemoryDescriptor.h>
#include "VMsvga2Accel.h"
#include "VMsvga2Shared.h"
#include "UCTypes.h"
#include "VLog.h"

#define CLASS VMsvga2Shared
#define super OSObject
OSDefineMetaClassAndStructors(VMsvga2Shared, OSObject);

#if LOGGING_LEVEL >= 1
#define SHLog(log_level, ...) do { if (log_level <= m_log_level) VLog("IOSH: ", ##__VA_ARGS__); } while (false)
#else
#define SHLog(log_level, ...)
#endif

#define HIDDEN __attribute__((visibility("hidden")))

#define CLIENT_SYS_OBJS_SIZE 8192U
#define NUM_TEXTURE_HANDLES 128U

extern "C" {

/*
 * defined in com.apple.kpi.unsupported
 */
vm_map_t get_task_map(task_t);
kern_return_t mach_vm_region(vm_map_t               map,
							 mach_vm_offset_t       *address,              /* IN/OUT */
							 mach_vm_size_t			*size,				   /* OUT */
							 vm_region_flavor_t     flavor,                /* IN */
							 vm_region_info_t       info,                  /* OUT */
							 mach_msg_type_number_t *count,                /* IN/OUT */
							 mach_port_t            *object_name);         /* OUT */
}

#pragma mark -
#pragma mark Private Methods
#pragma mark -

HIDDEN
IOReturn CLASS::alloc_client_shared(struct GLDSysObject** sys_obj, mach_vm_address_t* client_addr)
{
	size_t i;
	GLDSysObject* p;
	IOBufferMemoryDescriptor* client_sys_objs;
	if (!m_client_sys_objs_map) {
		client_sys_objs = IOBufferMemoryDescriptor::withOptions(kIOMemoryKernelUserShared |
																kIOMemoryPageable |
																kIODirectionInOut,
																CLIENT_SYS_OBJS_SIZE,
																PAGE_SIZE);
		if (!client_sys_objs)
			return kIOReturnNoMemory;
		m_client_sys_objs_map = client_sys_objs->createMappingInTask(m_owning_task,
																	 0,
																	 kIOMapAnywhere);
		if (!m_client_sys_objs_map) {
			client_sys_objs->release();
			return kIOReturnNoMemory;
		}
		m_client_sys_objs_kernel_ptr = client_sys_objs->getBytesNoCopy();
		client_sys_objs->release();
		bzero(m_client_sys_objs_kernel_ptr, CLIENT_SYS_OBJS_SIZE);
	}
	p = static_cast<GLDSysObject*>(m_client_sys_objs_kernel_ptr);
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
void CLASS::free_client_shared(GLDSysObject* p)
{
	if (p)
		bzero(p, sizeof *p);
}

#if 0
HIDDEN
GLDSysObject* CLASS::find_client_shared(uint32_t object_id)
{
	GLDSysObject* p;
	size_t i;
	if (!m_client_sys_objs_map)
		return 0;
	p = static_cast<GLDSysObject*>(m_client_sys_objs_kernel_ptr);
	for (i = 0U; i != CLIENT_SYS_OBJS_SIZE / sizeof *p; ++i)
		if (p[i].in_use && p[i].object_id == object_id)
			return &p[i];
	return 0;
}
#endif

#if 0
HIDDEN
void CLASS::deref_client_shared(uint32_t object_id, int addend)
{
	GLDSysObject* p = findClientSysObj(object_id);
	if (!p)
		return;
	if (__sync_fetch_and_add(&p->refcount, addend) == -addend)
		free_client_shared(p);
}
#endif

HIDDEN
void CLASS::free_handles()
{
	if (m_handle_table) {
		IOFree(m_handle_table, m_num_handles * sizeof(void*));
		m_handle_table = 0;
		m_num_handles = 0;
	}
}

HIDDEN
bool CLASS::alloc_handles()
{
	uint32_t num_handles = NUM_TEXTURE_HANDLES;
	m_handle_table = static_cast<void**>(IOMalloc(num_handles * sizeof(void*)));
	if (!m_handle_table)
		return false;
	bzero(m_handle_table, num_handles * sizeof(void*));
	m_num_handles = num_handles;
	return true;
}

HIDDEN
bool CLASS::alloc_buf_handle(void* entry, uint32_t* object_id)
{
	uint32_t i;
	if (!entry)
		return false;
	for (i = 0U; i != m_num_handles; ++i)
		if (!m_handle_table[i]) {
			m_handle_table[i] = entry;
			if (object_id)
				*object_id = i;
			return true;
		}
	return false;
}

HIDDEN
void CLASS::free_buf_handle(void* entry, uint32_t object_id)
{
	if (!m_handle_table ||
		object_id >= m_num_handles ||
		m_handle_table[object_id] != entry)
		return;
	m_handle_table[object_id] = 0;
}

HIDDEN
void CLASS::Cleanup()
{
	uint32_t i;
	VMsvga2TextureBuffer* texture;
	for (i = 0U; i != m_num_handles; ++i)
		if (m_handle_table[i]) {
			texture = static_cast<typeof texture>(m_handle_table[i]);
			free_buf_handle(texture, texture->sys_obj->object_id);
			free_client_shared(texture->sys_obj);
			finalize_texture(m_provider, texture);
			releaseVendorTextureBuffer(texture, sizeof *texture);
		}
	m_texture_list = 0;
	free_handles();
	if (m_client_sys_objs_map) {
		m_client_sys_objs_map->release();
		m_client_sys_objs_map = 0;
		m_client_sys_objs_kernel_ptr = 0;
	}
	if (m_shared_lock) {
		IOLockFree(m_shared_lock);
		m_shared_lock = 0;
	}
}

HIDDEN
void CLASS::delete_texture_internal(class VMsvga2Accel* provider,
									VMsvga2Shared* shared,
									VMsvga2TextureBuffer* texture)
{
	if (!texture)
		return;
	if (texture->sys_obj_type != 4U &&
		texture->linked_agp) {
		delete_texture_internal(provider, shared, texture->linked_agp);
		texture->linked_agp = 0;
	}
	if (!shared)
		shared = texture->creator;
	if (shared) {
		shared->free_buf_handle(texture, texture->sys_obj->object_id);
		shared->free_client_shared(texture->sys_obj);
		shared->unlink_texture(texture);
	}
	finalize_texture(provider, texture);
	releaseVendorTextureBuffer(texture, sizeof *texture);
}

HIDDEN
void CLASS::free_orphan_texture(class VMsvga2Accel*, struct IOTextureBuffer*)
{
	/*
	 * TBD
	 */
}

HIDDEN
void CLASS::finalize_texture(class VMsvga2Accel* provider, VMsvga2TextureBuffer* texture)
{
	if (texture->client_map) {
		texture->client_map->release();
		texture->client_map = 0;
	}
	if (texture->md) {
		texture->md->release();
		texture->md = 0;
	}
	if (static_cast<int>(texture->surface_id) >= 0) {
		provider->destroySurface(texture->surface_id);
		provider->FreeSurfaceID(texture->surface_id);
		texture->surface_id = SVGA_ID_INVALID;
	}
}

HIDDEN
int isUserMemoryReadOnly(task_t owningTask, mach_vm_address_t address, mach_vm_size_t size)
{
	mach_vm_offset_t _address;
	mach_vm_size_t _size;
	vm_region_basic_info_64 info;
	mach_msg_type_number_t info_count;
	_address = address;
	_size = size;
	info_count = VM_REGION_BASIC_INFO_COUNT_64;
	if (mach_vm_region(get_task_map(owningTask),
					   &_address,
					   &_size,
					   VM_REGION_BASIC_INFO_64,
					   reinterpret_cast<vm_region_info_t>(&info),
					   &info_count,
					   0) != ERR_SUCCESS)
		return -1;
	return !(info.protection & VM_PROT_WRITE);
}

HIDDEN
VMsvga2TextureBuffer* CLASS::new_agp_texture(mach_vm_address_t pixels,
											 size_t texture_size,
											 uint32_t read_only,
											 mach_vm_address_t* sys_obj_client_addr)
{
	mach_vm_address_t down_pixels; // var_6c
	mach_vm_offset_t offset_in_page; // var_80
	vm_size_t up_size; // var_74
	VMsvga2TextureBuffer *p1, *p2;
	IOMemoryDescriptor* md;
	IOOptionBits md_opts;
	int ro;
	// [esi,ebx] = pixels
	// edi = page_size
	// var_90 = page_size - 1
	// var_8c = 0
	SHLog(2, "%s(%#llx, %lu, %u, out)\n", __FUNCTION__, pixels, texture_size, read_only);
	down_pixels = pixels & -PAGE_SIZE;
	offset_in_page = pixels & (PAGE_SIZE - 1U);
	up_size = static_cast<vm_size_t>((offset_in_page + texture_size + (PAGE_SIZE - 1U)) & -PAGE_SIZE);
	// var_84 = m_provider
#if 0
	vm_size_t limit = 3U * (m_provider->0x93C << PAGE_SHIFT) >> 2;
	if (up_size > limit)
		return 0;
#endif
	if (!down_pixels) {
		IOBufferMemoryDescriptor* bmd =
		IOBufferMemoryDescriptor::inTaskWithOptions(m_owning_task,
													kIOMemoryKernelUserShared |
													kIOMemoryPageable |
													kIOMemoryPurgeable |
													kIODirectionInOut /* | m_provider->0x934 */,
													up_size,
													PAGE_SIZE);
		if (!bmd) {
			SHLog(1, "%s: IOBufferMemoryDescriptor allocation failed\n", __FUNCTION__);
			return 0;
		}
		md = bmd;
		IOMemoryMap* mm = bmd->map(kIODirectionIn /* | m_provider->0x934 */);
		if (mm) {
			bzero(reinterpret_cast<void*>(mm->getVirtualAddress()), up_size);
			mm->release();
		}
		down_pixels = reinterpret_cast<mach_vm_address_t>(bmd->getBytesNoCopy()); // TBD: won't work with 32-bit kernel, 64-bit user
		goto common;
	}
	for (p1 = m_texture_list; p1; p1 = p1->next) {	// p1 in ebx
		if (p1->sys_obj_type != 4U ||
			p1->agp_flag ||
			p1->agp_offset_in_page != offset_in_page ||
			p1->agp_addr != down_pixels ||
			p1->agp_size != up_size)
			continue;
		md = IOMemoryDescriptor::withPersistentMemoryDescriptor(p1->md);	// md in esi
		if (md != p1->md) {
			p1->agp_flag = 1U;
			if (md)
				goto common;
			else
				break;
		}
		md->release();
		*sys_obj_client_addr = p1->sys_obj_client_addr;
		if (p1 != m_texture_list) {
			unlink_nonhead_texture(p1);
			link_texture_at_head(p1);
		}
		return p1;
	}
	ro = isUserMemoryReadOnly(m_owning_task, down_pixels, up_size);
	if (ro < 0)
		return 0;
	md_opts = ((read_only & 1U) || ro) ? kIODirectionOut : kIODirectionInOut;
	md = IOMemoryDescriptor::withAddressRange(down_pixels,
											  up_size,
											  md_opts | kIOMemoryPersistent,
											  m_owning_task);
	if (!md) {
		SHLog(1, "%s: IOMemoryDescriptor::withAddressRange failed\n", __FUNCTION__);
		return 0;
	}
common:
	p2 = common_texture_init(4U);
	if (!p2) {
		SHLog(1, "%s: common_texture_init failed\n", __FUNCTION__);
		md->release();
		return 0;
	}
	p2->md = md;
	p2->agp_offset_in_page = offset_in_page;
	p2->agp_addr = down_pixels;
	p2->agp_size = md->getLength();
	p2->agp_flag = 0U;
	link_texture_at_head(p2);
	*sys_obj_client_addr = p2->sys_obj_client_addr;
#if 0
	if (!m_provider->addTransferToGART(p2))
		m_provider->freeToAllocGART(0, 0, this, p2);
	if (p2->offset0x4) {
		clock_get_system_nanotime(&p2->0x10, &p2->0x14);
		p2->0x24 = this;
		/*
		 * link to a doubly-linked list in m_provider
		 */
		edx = p2->0x18;
		eax = p2->0x1C;
		edx->0x1C = eax;
		eax->0x18 = edx;
		p2->0x18 = m_provider->0x774;
		p2->0x1C = m_provider + 0x75C;
		m_provider->0x774 = p2;
		p2->0x18->0x1C = p2;
	}
	++m_provider->0x7BC;
#endif
	return p2;
}

HIDDEN
void CLASS::link_texture_at_head(VMsvga2TextureBuffer* texture)
{
	if (m_texture_list)
		m_texture_list->prev = texture;
	texture->prev = 0;
	texture->next = m_texture_list;
	m_texture_list = texture;
}

/*
 * texture != m_texture_list required here
 */
HIDDEN
void CLASS::unlink_nonhead_texture(VMsvga2TextureBuffer* texture)
{
	texture->prev->next = texture->next;
	if (texture->next)
		texture->next->prev = texture->prev;
}

HIDDEN
void CLASS::unlink_texture(VMsvga2TextureBuffer* texture)
{
	if (texture == m_texture_list) {
		m_texture_list = texture->next;
		if (m_texture_list)
			m_texture_list->prev = 0;
	} else
		unlink_nonhead_texture(texture);
}

HIDDEN
VMsvga2TextureBuffer* CLASS::common_texture_init(uint8_t object_type)
{
	mach_vm_address_t client_addr;	// var_30
	VMsvga2TextureBuffer* p;
	GLDSysObject* sys_obj;			// var_20
	uint32_t object_id;				// var_1c

	p = static_cast<typeof p>(allocVendorTextureBuffer(sizeof *p));
	if (!p) {
		SHLog(1, "%s: allocVendorTextureBuffer failed\n", __FUNCTION__);
		return 0;
	}
	if (!alloc_buf_handle(p, &object_id)) {
		SHLog(1, "%s: alloc_buf_handle failed\n", __FUNCTION__);
		releaseVendorTextureBuffer(p, sizeof *p);
		return 0;
	}
	if (alloc_client_shared(&sys_obj, &client_addr) != kIOReturnSuccess) {
		SHLog(1, "%s: alloc_client_shared failed\n", __FUNCTION__);
		free_buf_handle(p, object_id);
		releaseVendorTextureBuffer(p, sizeof *p);
		return 0;
	}
	bzero(p, sizeof *p);	// Added
	p->sys_obj = sys_obj;
	p->sys_obj_client_addr = client_addr;
	sys_obj->object_id = object_id;
	sys_obj->stamps[0] = 0U;  // from m_provider->0x54
	sys_obj->stamps[1] = 0U;  // from m_provider->0x54
	sys_obj->type = object_type;
	sys_obj->in_use = 1U;
	p->sys_obj_type = object_type;
	p->surface_id = static_cast<uint32_t>(-1);
	p->gmr_id = static_cast<uint32_t>(-1);
	p->surface_format = SVGA3D_FORMAT_INVALID;
	return p;
}

#pragma mark -
#pragma mark Public Methods
#pragma mark -

HIDDEN
CLASS* CLASS::factory(task_t owningTask, class VMsvga2Accel* provider, int logLevel)
{
	CLASS* obj = new CLASS;

	if (obj &&
		!obj->init(owningTask, provider, logLevel)) {
		obj->release();
		return 0;
	}

	return obj;
}

bool CLASS::init(task_t owningTask, class VMsvga2Accel* provider, int logLevel)
{
	if (!super::init())
		return false;
	m_owning_task = owningTask;
	m_provider = provider;
	m_log_level = logLevel;
#if 0
	m_texture_list = 0;
	m_handle_table = 0;
	m_bitmap = 0;
	m_num_handles = 0;
	offset0x1C = 0;
	m_client_sys_objs_map = 0;
#endif
	m_shared_lock = IOLockAlloc();
	if (!m_shared_lock)
		return false;
	if (!alloc_handles()) {
		IOLockFree(m_shared_lock);
		m_shared_lock = 0;
		return false;
	}
	return true;
}

void CLASS::free()
{
	Cleanup();
	super::free();
}

HIDDEN
VMsvga2TextureBuffer* CLASS::new_surface_texture(uint32_t, uint32_t, mach_vm_address_t*)
{
	/*
	 * TBD
	 */
	return 0;
}

HIDDEN
VMsvga2TextureBuffer* CLASS::new_iosurface_texture(uint32_t, uint32_t, uint32_t, uint32_t, mach_vm_address_t*)
{
	/*
	 * TBD
	 */
	return 0;
}

HIDDEN
VMsvga2TextureBuffer* CLASS::new_texture(uint32_t size0,
										 uint32_t size1,
										 mach_vm_address_t pixels,
										 size_t texture_size,
										 uint32_t read_only,
										 mach_vm_address_t* client_texture_buffer_addr,
										 mach_vm_address_t* sys_obj_client_addr)
{
	VMsvga2TextureBuffer *p1 /* var_3c */, *p2;
	IOBufferMemoryDescriptor* bmd;
	mach_vm_address_t agp_sys_obj_client_addr;	// var_30
	vm_size_t total_size, overhead;
	uint8_t obj_type;
	SHLog(2, "%s(%#x, %u, %#llx, %lu, %u, out1, out2)\n", __FUNCTION__,
		  size0, size1, pixels, texture_size, read_only);
	if (pixels && texture_size) {
		p1 = new_agp_texture(pixels, texture_size, read_only, &agp_sys_obj_client_addr);
		if (!p1) {
			SHLog(1, "%s: new_agp_texture failed\n", __FUNCTION__);
			return 0;
		}
		obj_type = 9U;
		overhead = 0x900U;
	} else {
		p1 = 0;
		if (size1) {
			obj_type = 8U;
			overhead = 0x900U;
		} else {
			obj_type = 3U;
			overhead = 0x80U;
		}
	}
	total_size = (3U * size0 + overhead + (PAGE_SIZE - 1U)) & -PAGE_SIZE;
#if 0
	vm_size_t limit = 3U * (m_provider->0x93C << PAGE_SHIFT) >> 2;
	if (total_size > limit)
		goto clean1;
#endif
	p2 = common_texture_init(obj_type);
	if (!p2) {
		SHLog(1, "%s: common_texture_init failed\n", __FUNCTION__);
		goto clean1;
	}
	bmd = IOBufferMemoryDescriptor::inTaskWithOptions(m_owning_task,
													  kIOMemoryKernelUserShared |
													  kIOMemoryPageable |
													  kIOMemoryPurgeable |
													  kIODirectionInOut /* | m_provider->0x934 */,
													  total_size,
													  PAGE_SIZE);
	if (!bmd) {
		SHLog(1, "%s: IOBufferMemoryDescriptor allocation failed\n", __FUNCTION__);
		goto clean2;
	}
	p2->md = bmd;
	p2->creator = this;
	p2->client_map = bmd->createMappingInTask(m_owning_task,
											  0U,
											  kIOMapAnywhere);
	if (!p2->client_map) {
		SHLog(1, "%s: createMappingInTask failed\n", __FUNCTION__);
		goto clean3;
	}
	p2->pad2[0] = size1;
	if (obj_type == 9U) {
		p2->linked_agp = p1;
		__sync_fetch_and_add(&p1->sys_obj->refcount, 1);
	}
	link_texture_at_head(p2);
	*client_texture_buffer_addr = p2->client_map->getAddress();
	*sys_obj_client_addr = p2->sys_obj_client_addr;
#if 0
	++m_provider->0x7BC;
#endif
	return p2;

clean3:
	bmd->release();
	p2->md = 0;
clean2:
	free_buf_handle(p2, p2->sys_obj->object_id);
	free_client_shared(p2->sys_obj);
	releaseVendorTextureBuffer(p2, sizeof *p2);
clean1:
	if (p1 && !p1->sys_obj->refcount)
		delete_texture(p1);
	return 0;
}

HIDDEN
VMsvga2TextureBuffer* CLASS::new_agpref_texture(mach_vm_address_t pixels1,
												mach_vm_address_t pixels2,
												size_t texture_size,
												uint32_t read_only,
												mach_vm_address_t* sys_obj_client_addr)
{
	VMsvga2TextureBuffer *p1 /* var_3c */, *p2;
	mach_vm_address_t agp_sys_obj_client_addr;	// var_28
	SHLog(2, "%s(%#llx, %#llx, %lu, %u, out)\n", __FUNCTION__,
		 pixels1, pixels2, texture_size, read_only);
	p1 = new_agp_texture(pixels2, texture_size, read_only, &agp_sys_obj_client_addr);
	if (!p1) {
		SHLog(1, "%s: new_agp_texture failed\n", __FUNCTION__);
		return 0;
	}
	p2 = common_texture_init(6U);
	if (!p2) {
		SHLog(1, "%s: common_texture_init failed\n", __FUNCTION__);
		if (!p1->sys_obj->refcount)
			delete_texture(p1);
		return 0;
	}
	p2->creator = this;
	if (pixels1)
		p2->agp_addr = pixels1 - p1->agp_addr;
	else
		p2->agp_addr = 0U;
	p2->linked_agp = p1;
	__sync_fetch_and_add(&p1->sys_obj->refcount, 1);
	link_texture_at_head(p2);
	*sys_obj_client_addr = p2->sys_obj_client_addr;
#if 0
	++m_provider->0x7BC;
#endif
	return p2;
}

HIDDEN
bool CLASS::initializeTexture(VMsvga2TextureBuffer*, VendorNewTextureDataStruc const*)
{
	/*
	 * TBD
	 */
	return 1;
}

HIDDEN
VMsvga2TextureBuffer* CLASS::findTextureBuffer(uint32_t object_id)
{
	if (!m_handle_table || object_id >= m_num_handles)
		return 0;
	return static_cast<VMsvga2TextureBuffer*>(m_handle_table[object_id]);
}
