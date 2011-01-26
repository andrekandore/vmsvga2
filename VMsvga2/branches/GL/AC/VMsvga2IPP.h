/*
 *  VMsvga2IPP.h
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

#ifndef __VMSVGA2IPP_H__
#define __VMSVGA2IPP_H__

#include <libkern/c++/OSObject.h>
#include "VertexArray.h"

class VMsvga2IPP: public OSObject
{
	OSDeclareDefaultStructors(VMsvga2IPP);

private:
	class VMsvga2Accel* m_provider;
	struct GLDFence* m_fences_ptr;
	size_t m_fences_len;
	int m_log_level;

	uint32_t m_context_id;
	float* m_float_cache;
	struct ShaderEntry* m_shader_cache;
	/*
	 * TBD: should shader ids be unique system-wide???
	 */
	uint32_t m_next_shid;
	uint32_t m_active_shid;

	/*
	 * Buffers for vertex/index arrays (need GMRs)
	 */
	VertexArray m_arrays;

	/*
	 * Intel 915 Emulator State
	 */
	uint32_t imm_s[16];
	uint16_t param_cache_mask;
	uint16_t nonnormalized_tex_coordinates;
	struct {
		uint32_t mask;
		uint32_t color;
		float depth;
		uint32_t stencil;
	} clear_params;
	uint32_t surface_ids[16];	// for the 16 texture stages
	uint64_t s2t_map;	// maps samplers to texture stages (4 bits each)
	uint32_t tc2s_map;	// maps texcoords to samplers
	uint8_t tc2s_map_valids; // 1 bit for each tc mapped in tc2s_map
	uint16_t bound_samplers;

	/*
	 * Private Methods
	 */
	void Init();
	void Cleanup();
	struct ShaderEntry const* cache_shader(uint32_t const* source, uint32_t num_dwords);
	void purge_shader_cache();
	void unbind_samplers(uint16_t mask);
	void calc_adjustment_map(uint8_t* map) const;
	void adjust_texture_coords(uint8_t const* map,
							   uint8_t* vertex_array,
							   size_t num_vertices,
							   void const* decls,
							   size_t num_decls) const;
	uint8_t calc_color_write_enable(void) const;
	bool cache_misc_reg(uint8_t regnum, uint32_t value);
	void ip_prim3d_poly(uint32_t const* vertex_data, size_t num_vertex_dwords);
	void ip_prim3d_direct(uint32_t prim_kind, uint32_t const* vertex_data, size_t num_vertex_dwords);
	uint32_t ip_prim3d(uint32_t* p, uint32_t cmd);
	uint32_t ip_load_immediate(uint32_t* p, uint32_t cmd);
	uint32_t ip_clear_params(uint32_t* p, uint32_t cmd);
	void ip_3d_map_state(uint32_t* p);
	void ip_3d_sampler_state(uint32_t* p);
	void ip_misc_render_state(uint32_t selector, uint32_t* p);
	void ip_independent_alpha_blend(uint32_t cmd);
	void ip_backface_stencil_ops(uint32_t cmd);
	void ip_print_ps(uint32_t const*, uint32_t);
	void ip_select_and_load_ps(uint32_t* p, uint32_t cmd);
	void ip_load_ps_const(uint32_t* p);
	void ip_buf_info(uint32_t* p);
	void ip_draw_rect(uint32_t* p);
	uint32_t decode_mi(uint32_t* p, uint32_t cmd);
	uint32_t decode_2d(uint32_t* p, uint32_t cmd);
	uint32_t decode_3d_1d(uint32_t* p, uint32_t cmd);
	uint32_t decode_3d(uint32_t* p, uint32_t cmd);

public:
	/*
	 * Methods overridden from superclass
	 */
	bool init();
	static VMsvga2IPP* factory(void);

	bool start(class VMsvga2Accel* provider, int logLevel);
	void setFences(struct GLDFence* fences_ptr, size_t fences_len);
	void stop(void);
	void discard_cached_state(void);
	void detach_render_targets(void);
	uint32_t submit_buffer(uint32_t* kernel_buffer_ptr, uint32_t size_dwords);
};

#endif /* __VMSVGA2IPP_H__ */
