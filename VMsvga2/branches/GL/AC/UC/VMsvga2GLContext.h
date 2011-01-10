/*
 *  VMsvga2GLContext.h
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

#ifndef __VMSVGA2GLCONTEXT_H__
#define __VMSVGA2GLCONTEXT_H__

#include <IOKit/IOUserClient.h>

struct VendorCommandBufferHeader;
struct VendorGLStreamInfo;
struct VMsvga2TextureBuffer;
class IOMemoryDescriptor;

class VMsvga2GLContext: public IOUserClient
{
	OSDeclareDefaultStructors(VMsvga2GLContext);

private:
	task_t m_owning_task;					// offset 0x78
											// offset 0x7C: unknown
	class VMsvga2Accel* m_provider;			// offset 0x80
											// offset 0x84 - 0x8C: unknown
	class VMsvga2Shared* m_shared;			// offset 0x8C
											// offset 0x90 - 0xB4: unknown 
	IOMemoryDescriptor* m_type2;			// offset 0xB4
	size_t m_type2_len;						// offset 0xB8
	VendorCommandBufferHeader* m_type2_ptr; // offset 0xBC
											// offset 0xC0: unknown
	class VMsvga2Surface* m_surface_client;	// offset 0xC4
	VMsvga2CommandBuffer m_command_buffer;	// offset 0xC8 - 0xFC
	class OSSet* m_gc;						// offset 0xFC
	VMsvga2CommandBuffer m_context_buffer0; // offset 0x100 - 0x134
	VMsvga2CommandBuffer m_context_buffer1; // offset 0x130 - 0x154
											// offset 0x154 - 0x194: unknown
	int m_stream_error;						// offset 0x194
											// offset 0x198: unknown
	uint32_t m_mem_type;					// offset 0x19C
	VMsvga2TextureBuffer* m_txs[21U];		// offset 0x1B0
											// textures 0 - 15 used for texture stages
											// texture 16 used for vertex buffer
											// textures 17 - 18 used for DrawFBO (color & depth)
											// textures 19 - 20 used for ReadFBO (color & depth)

	/*
	 * VMsvga2 Specific
	 */
	int m_log_level;
	uint32_t m_context_id;
	float* m_float_cache;
	bool m_shaders_loaded;

	/*
	 * Buffers for vertex/index arrays (need GMRs)
	 */
	struct {
		uint8_t* kernel_ptr;
		size_t size_bytes;
		size_t offset_in_gmr;
		uint32_t sid;
		uint32_t gmr_id;
		uint32_t fence;
	} m_arrays;

	/*
	 * Intel 915 Emulator State
	 */
	struct {
		uint32_t imm_s[16];
		uint16_t param_cache_mask;
		uint16_t prettex_coordinates; // pre-transformed tex coordinates
		struct {
			uint32_t mask;
			uint32_t color;
			float depth;
			uint32_t stencil;
		} clear;
		uint32_t surface_ids[16];	// for the 16 texture stages
#if 0
		uint64_t sampler_to_texstage;	// associates texture stages with samplers (16 x 4)
#endif
	} m_intel_state;

	/*
	 * Private Methods
	 */
	void Init();
	void Cleanup();
	bool allocCommandBuffer(VMsvga2CommandBuffer*, size_t);
	void initCommandBufferHeader(VendorCommandBufferHeader*, size_t);
	bool allocAllContextBuffers();
	IOReturn get_status(uint32_t*);
	uint32_t processCommandBuffer(struct VendorCommandDescriptor*);
	void discardCommandBuffer();
	IOReturn prepare_command_buffer_io();
	void sync_command_buffer_io();
	void complete_command_buffer_io();
	void removeTextureFromStream(VMsvga2TextureBuffer*);
	void addTextureToStream(VMsvga2TextureBuffer*);
	void submit_midbuffer(VendorGLStreamInfo*);
	void get_texture(VendorGLStreamInfo*, VMsvga2TextureBuffer*, bool);
	void dirtyTexture(VMsvga2TextureBuffer* tx, uint8_t face, uint8_t mipmap);
	void get_tex_data(VMsvga2TextureBuffer* tx, uint32_t* tex_gart_address, uint32_t* tex_pitch);
	void write_tex_data(uint32_t, uint32_t*, VMsvga2TextureBuffer*);
	IOReturn create_host_surface_for_texture(VMsvga2TextureBuffer*);
	IOReturn alloc_and_load_texture(VMsvga2TextureBuffer*);
	IOReturn tex_subimage_2d(VMsvga2TextureBuffer* tx,
							 size_t command_buffer_offset,
							 size_t source_pitch,
							 uint32_t width,
							 uint32_t height,
							 uint32_t destX,
							 uint32_t destY,
							 uint32_t destZ);
	IOReturn bind_texture(uint32_t index, VMsvga2TextureBuffer* tx);
	void setup_drawbuffer_registers(uint32_t*);
	IOReturn alloc_arrays(size_t num_bytes);
	void purge_arrays();
	IOReturn upload_arrays(size_t num_bytes);
	IOReturn load_fixed_shaders();
	void unload_fixed_shaders();
	void adjust_texture_coords(uint8_t* vertex_array,
							   size_t num_vertices,
							   void const* decls,
							   size_t num_decls);

	/*
	 * Intel Pipeline processor
	 */
	uint8_t calc_color_write_enable(void);
	bool cache_misc_reg(uint8_t regnum, uint32_t value);
	void ipp_discard_renderstate(void);
	void ip_prim3d_poly(uint32_t const* vertex_data, size_t num_vertex_dwords);
	void ip_prim3d_direct(uint32_t prim_kind, uint32_t const* vertex_data, size_t num_vertex_dwords);
	uint32_t ip_prim3d(uint32_t* p);
	uint32_t ip_load_immediate(uint32_t* p);
	uint32_t ip_clear_params(uint32_t* p);
	void ip_3d_map_state(uint32_t* p);
	void ip_3d_sampler_state(uint32_t* p);
	void ip_misc_render_state(uint32_t selector, uint32_t* p);
	void ip_independent_alpha_blend(uint32_t cmd);
	void ip_backface_stencil_ops(uint32_t cmd);
	void ip_print_ps(uint32_t* p);
	void ip_select_and_load_ps(uint32_t* p);
	uint32_t decode_mi(uint32_t* p);
	uint32_t decode_2d(uint32_t* p);
	uint32_t decode_3d(uint32_t* p);
	uint32_t submit_buffer(uint32_t* kernel_buffer_ptr, uint32_t size_dwords);

public:
	/*
	 * Command buffer process & discard methods [Apple]
	 */
	/*
	 * Tokens 0 - 10
	 */
	void process_token_Noop(VendorGLStreamInfo*);
	void process_token_TextureVolatile(VendorGLStreamInfo*);
	void process_token_TextureNonVolatile(VendorGLStreamInfo*);
	void process_token_SetSurfaceState(VendorGLStreamInfo*);
	void process_token_BindDrawFBO(VendorGLStreamInfo*);
	void process_token_BindReadFBO(VendorGLStreamInfo*);
	void process_token_UnbindDrawFBO(VendorGLStreamInfo*);
	void process_token_UnbindReadFBO(VendorGLStreamInfo*);
	void process_token_Start(VendorGLStreamInfo*);
	void process_token_End(VendorGLStreamInfo*);
	void process_token_Swap(VendorGLStreamInfo*);
	void process_token_Flush(VendorGLStreamInfo*);

	/*
	 * Tokens 32 - 61
	 */
	void discard_token_Texture(VendorGLStreamInfo*);
	void discard_token_NoTex(VendorGLStreamInfo*);
	void discard_token_VertexBuffer(VendorGLStreamInfo*);
	void discard_token_NoVertexBuffer(VendorGLStreamInfo*);
	void discard_token_DrawBuffer(VendorGLStreamInfo*);
	void discard_token_Noop(VendorGLStreamInfo*);
	void discard_token_TexSubImage2D(VendorGLStreamInfo*);
	void discard_token_CopyPixelsDst(VendorGLStreamInfo*);
	void discard_token_AsyncReadDrawBuffer(VendorGLStreamInfo*);
	void process_token_Texture(VendorGLStreamInfo*);
	void process_token_NoTex(VendorGLStreamInfo*);
	void process_token_VertexBuffer(VendorGLStreamInfo*);
	void process_token_NoVertexBuffer(VendorGLStreamInfo*);
	void process_token_DrawBuffer(VendorGLStreamInfo*);
	void process_token_SetFence(VendorGLStreamInfo*);
	void process_token_TexSubImage2D(VendorGLStreamInfo*);
	void process_token_CopyPixelsDst(VendorGLStreamInfo*);
	void process_token_CopyPixelsSrc(VendorGLStreamInfo*);
	void process_token_CopyPixelsSrcFBO(VendorGLStreamInfo*);
	void process_token_DrawRect(VendorGLStreamInfo*);
	void process_token_AsyncReadDrawBuffer(VendorGLStreamInfo*);

public:
	/*
	 * Methods overridden from superclass
	 */
	IOExternalMethod* getTargetAndMethodForIndex(IOService** targetP, UInt32 index);
	IOReturn clientClose();
	IOReturn clientMemoryForType(UInt32 type, IOOptionBits* options, IOMemoryDescriptor** memory);
	IOReturn connectClient(IOUserClient* client);
#if 0
	IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments* arguments, IOExternalMethodDispatch* dispatch = 0, OSObject* target = 0, void* reference = 0);
#endif
	bool start(IOService* provider);
	bool initWithTask(task_t owningTask, void* securityToken, UInt32 type);
	static VMsvga2GLContext* withTask(task_t owningTask, void* securityToken, uint32_t type);

	/*
	 * Methods corresponding to Apple's GeForce.kext GL Context User Client
	 */
	/*
	 * IONVGLContext
	 */
	IOReturn set_surface(uintptr_t, uintptr_t /* eIOGLContextModeBits */, uintptr_t, uintptr_t);
	IOReturn set_swap_rect(intptr_t, intptr_t, intptr_t, intptr_t);
	IOReturn set_swap_interval(intptr_t, intptr_t);
	IOReturn get_config(uint32_t*, uint32_t*, uint32_t*);
	IOReturn get_surface_size(uint32_t*, uint32_t*, uint32_t*, uint32_t*);
	IOReturn get_surface_info(uintptr_t, uint32_t*, uint32_t*, uint32_t*);
	IOReturn read_buffer(struct sIOGLContextReadBufferData const*, size_t);
	IOReturn finish();
	IOReturn wait_for_stamp(uintptr_t);
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
	IOReturn new_texture(struct VendorNewTextureDataStruc const*,
						 struct sIONewTextureReturnData*,
						 size_t,
						 size_t*);
	IOReturn delete_texture(uintptr_t);
#endif
	IOReturn become_global_shared(uintptr_t);
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
	IOReturn page_off_texture(struct sIODevicePageoffTexture const*, size_t);
#endif
	IOReturn purge_texture(uintptr_t);
	IOReturn set_surface_volatile_state(uintptr_t);
	IOReturn set_surface_get_config_status(struct sIOGLContextSetSurfaceData const*,
										   struct sIOGLContextGetConfigStatus*,
										   size_t,
										   size_t*);
	IOReturn reclaim_resources();
	IOReturn get_data_buffer(struct sIOGLContextGetDataBuffer*, size_t*);
	IOReturn set_stereo(uintptr_t, uintptr_t);
	IOReturn purge_accelerator(uintptr_t);
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
	IOReturn get_channel_memory(struct sIODeviceChannelMemoryData*, size_t*);
#else
	IOReturn submit_command_buffer(uintptr_t do_get_data,
								   struct sIOGLGetCommandBuffer*,
								   size_t*);
#endif

	/*
	 * NVGLContext
	 */
	IOReturn get_query_buffer(uintptr_t c1, struct sIOGLGetQueryBuffer*, size_t*);
	IOReturn get_notifiers(uint32_t*, uint32_t*);
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
	IOReturn new_heap_object(struct sNVGLNewHeapObjectData const*,
							 struct sIONewTextureReturnData*,
							 size_t,
							 size_t*);
#endif
	IOReturn kernel_printf(char const*, size_t);
	IOReturn nv_rm_config_get(uint32_t const*, uint32_t*, size_t, size_t*);
	IOReturn nv_rm_config_get_ex(uint32_t const*, uint32_t*, size_t, size_t*);
	IOReturn nv_client_request(void const*, void*, size_t, size_t*);
	IOReturn pageoff_surface_texture(struct sNVGLContextPageoffSurfaceTextureData const*, size_t);
	IOReturn get_data_buffer_with_offset(struct sIOGLContextGetDataBuffer*, size_t*);
	IOReturn nv_rm_control(uint32_t const*, uint32_t*, size_t, size_t*);
	IOReturn get_power_state(uint32_t*, uint32_t*);
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1060
	IOReturn set_watchdog_timer(uintptr_t);
	IOReturn GetHandleIndex(uint32_t*, uint32_t*);
	IOReturn ForceTextureLargePages(uintptr_t);
#endif
};

#endif /* __VMSVGA2GLCONTEXT_H__ */
