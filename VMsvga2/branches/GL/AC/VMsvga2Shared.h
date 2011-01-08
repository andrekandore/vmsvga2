/*
 *  VMsvga2Shared.h
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on December 15th 2010.
 *  Copyright 2010-2011 Zenith432. All rights reserved.
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

#ifndef __VMSVGA2SHARED_H__
#define __VMSVGA2SHARED_H__

#include <IOKit/IOLib.h>

struct GLDSysObject;
struct VMsvga2TextureBuffer;

class VMsvga2Shared : public OSObject
{
	OSDeclareDefaultStructors(VMsvga2Shared);

private:
	task_t m_owning_task;				// offset  0x8
	class VMsvga2Accel* m_provider;		// offset  0xC
	void** m_handle_table;				// offset 0x10
	uint32_t m_num_handles;				// offset 0x14
#if 0
	uint8_t* m_bitmap;					// offset 0x18, attached to m_handle_table
#endif
										// offset 0x1C - unknown
										// offset 0x20, [linked list of structs describing sys object pools]
	VMsvga2TextureBuffer* m_texture_list;	// offset 0x24, linked listed of textures
										// ends   0x28
	class IOMemoryMap* m_client_sys_objs_map;
	void* m_client_sys_objs_kernel_ptr;
	IOLock* m_shared_lock;
	int m_log_level;

	void Cleanup();
	bool alloc_handles();
	void free_handles();
	bool alloc_buf_handle(void* entry, uint32_t* object_id);
	void free_buf_handle(void* entry, uint32_t object_id);
	static void* allocVendorTextureBuffer(size_t bytes) { return IOMalloc(bytes); }	// calls function with similar name on provider
	static void releaseVendorTextureBuffer(void* p, size_t bytes) { IOFree(p, bytes); } // calls function with similar name on provider
	void link_texture_at_head(VMsvga2TextureBuffer*);
	void unlink_nonhead_texture(VMsvga2TextureBuffer*);
	void unlink_texture(VMsvga2TextureBuffer*);
	IOReturn alloc_client_shared(GLDSysObject**, mach_vm_address_t*);
	void free_client_shared(GLDSysObject*);
#if 0
	GLDSysObject* find_client_shared(uint32_t object_id);
	void deref_client_shared(uint32_t object_id, int addend = -0x10000);
#endif
	static void delete_texture_internal(class VMsvga2Accel*,
										VMsvga2Shared*,
										VMsvga2TextureBuffer*);
	static void free_orphan_texture(class VMsvga2Accel*, struct IOTextureBuffer*);
	static void finalize_texture(class VMsvga2Accel*, VMsvga2TextureBuffer*);
	VMsvga2TextureBuffer* new_agp_texture(mach_vm_address_t pixels,
										  size_t texture_size,
										  uint32_t read_only,
										  mach_vm_address_t* sys_obj_client_addr);
	VMsvga2TextureBuffer* common_texture_init(uint8_t object_type);

public:
	/*
	 * Methods overridden from superclass
	 */
	bool init(task_t owningTask, class VMsvga2Accel* provider, int logLevel);
	void free();
	static VMsvga2Shared* factory(task_t owningTask, class VMsvga2Accel* provider, int logLevel);

	VMsvga2TextureBuffer* findTextureBuffer(uint32_t object_id);
	bool initializeTexture(VMsvga2TextureBuffer*, struct VendorNewTextureDataStruc const*);
	VMsvga2TextureBuffer* new_surface_texture(uint32_t surface_id,
											  uint32_t arg1,
											  mach_vm_address_t* sys_obj_client_addr);
	VMsvga2TextureBuffer* new_iosurface_texture(uint32_t, uint32_t, uint32_t, uint32_t, mach_vm_address_t*);
	VMsvga2TextureBuffer* new_texture(uint32_t size0,
									  uint32_t size1,
									  mach_vm_address_t pixels,
									  size_t texture_size,
									  uint32_t read_only,
									  mach_vm_address_t* client_texture_buffer_addr,
									  mach_vm_address_t* sys_obj_client_addr);
	VMsvga2TextureBuffer* new_agpref_texture(mach_vm_address_t pixels1,
											 mach_vm_address_t pixels2,
											 size_t texture_size,
											 uint32_t read_only,
											 mach_vm_address_t* sys_obj_client_addr);
	void delete_texture(VMsvga2TextureBuffer* texture) { delete_texture_internal(m_provider, this, texture); }
	void lockShared() { IOLockLock(m_shared_lock); }
	void unlockShared() { IOLockUnlock(m_shared_lock); }
};

#endif /* __VMSVGA2SHARED_H__ */
