/*
 *  VMsvga2GLContext3.cpp
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on January 7th 2011.
 *  Copyright 2011 Zenith432. All rights reserved.
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
#include <libkern/crypto/md5.h>
#define GL_INCL_PUBLIC
#define GL_INCL_PRIVATE
#include "GLCommon.h"
#include "Shaders.h"
#include "UCGLDCommonTypes.h"
#include "VLog.h"
#include "VMsvga2Accel.h"
#include "VMsvga2GLContext.h"
#include "VMsvga2Surface.h"

#define CLASS VMsvga2GLContext

#if LOGGING_LEVEL >= 1
#define GLLog(log_level, ...) do { if (log_level <= m_log_level) VLog("IOGL: ", ##__VA_ARGS__); } while (false)
#else
#define GLLog(log_level, ...)
#endif

#define HIDDEN __attribute__((visibility("hidden")))

#define MAX_NUM_DECLS 12U

#define PRINT_PS

#pragma mark -
#pragma mark Some Strings
#pragma mark -

#ifdef PRINT_PS
static
char const* ps_regtype_names[] =
{
	"R", "T", "C", "S", "OC", "OD", "U", "Invalid"
};

static
char const* ps_texture_ops[] =
{
	"texld", "texldp", "texldb", "texkill"
};

static
char const* ps_arith_ops[] =
{
	"nop", "add", "mov", "mul", "mad", "dp2add", "dp3", "dp4", "frc",
	"rcp", "rsq", "exp", "log", "cmp", "min", "max", "flr", "mod", "trc",
	"sge", "slt"
};

static
int ps_arith_num_params[] =
{
	0, 2, 1, 2, 3, 3, 2, 2, 1, 1, 1, 1, 1, 3, 2, 2, 1, 1, 1, 2, 2
};

static
char const* ps_sample_type[] =
{
	"_2d", "_cube", "_volume", ""
};

static
char const coord_letters[] = "xyzw01__";
#endif /* PRINT_PS */

#pragma mark -
#pragma mark Struct Definitions
#pragma mark -

template<unsigned N>
union DefineRegion
{
	uint8_t b[sizeof(IOAccelDeviceRegion) + N * sizeof(IOAccelBounds)];
	IOAccelDeviceRegion r;
};

struct ShaderEntry
{
	uint8_t md5[MD5_DIGEST_LENGTH];
	SVGA3dShaderType shader_type;
	uint32_t shader_id;
	ShaderEntry* next;
};

#pragma mark -
#pragma mark Global Functions
#pragma mark -

void set_region(IOAccelDeviceRegion* rgn,
				uint32_t x,
				uint32_t y,
				uint32_t w,
				uint32_t h);

static
uint8_t get4bits(uint64_t const* v, uint8_t index)
{
	return static_cast<uint8_t>((*v) >> ((index & 15U) * 4U)) & 15U;
}

static
void set4bits(uint64_t* v, uint8_t index, uint8_t n)
{
	index = (index & 15U) * 4U;
	*v &= ~(0xFULL << index);
	*v |= static_cast<uint64_t>(n & 15U) << index;
}

static
int translate_clear_mask(uint32_t mask)
{
	int out_mask = 0U;
	if (mask & 4U)
		out_mask |= SVGA3D_CLEAR_COLOR;
	if (mask & 2U)
		out_mask |= SVGA3D_CLEAR_DEPTH;
	if (mask & 1U)
		out_mask |= SVGA3D_CLEAR_STENCIL;
	return out_mask;
}

static
uint32_t xlate_taddress(uint32_t v)
{
	switch (v) {
		case 0U: /* TEXCOORDMODE_WRAP */
		case 1U: /* TEXCOORDMODE_MIRROR */
		case 2U: /* TEXCOORDMODE_CLAMP_EDGE */
			return v + 1U;
		case 3U: /* TEXCOORDMODE_CUBE */
			return SVGA3D_TEX_ADDRESS_EDGE;
		case 4U: /* TEXCOORDMODE_CLAMP_BORDER */
		case 5U: /* TEXCOORDMODE_MIRROR_ONCE */
			return v;
	}
	return SVGA3D_TEX_ADDRESS_INVALID;
}

static
uint32_t xlate_filter1(uint32_t v)
{
	switch (v) {
		case 0U: /* MIPFILTER_NONE */
			break;
		case 1U: /* MIPFILTER_NEAREST */
			return SVGA3D_TEX_FILTER_NEAREST;
		case 3U: /* MIPFILTER_LINEAR */
			return SVGA3D_TEX_FILTER_LINEAR;
	}
	return SVGA3D_TEX_FILTER_NONE;
}

static
uint32_t xlate_filter2(uint32_t v)
{
	switch (v) {
		case 0U: /* FILTER_NEAREST */
			return SVGA3D_TEX_FILTER_NEAREST;
		case 1U: /* FILTER_LINEAR */
			return SVGA3D_TEX_FILTER_LINEAR;
		case 2U: /* FILTER_ANISOTROPIC */
			return SVGA3D_TEX_FILTER_ANISOTROPIC;
		case 3U: /* FILTER_4X4_1 */
		case 4U: /* FILTER_4X4_2 */
		case 5U: /* FILTER_4X4_FLAT */
		case 6U: /* FILTER_6X5_MONO */
			break;
	}
	return SVGA3D_TEX_FILTER_NEAREST;
}

static inline
uint32_t xlate_comparefunc(uint32_t v)
{
	return v ? : SVGA3D_CMP_ALWAYS;
}

static inline
uint32_t xlate_stencilop(uint32_t v)
{
	switch (v) {
		case 0U: /* STENCILOP_KEEP */
		case 1U: /* STENCILOP_ZERO */
		case 2U: /* STENCILOP_REPLACE */
		case 3U: /* STENCILOP_INCRSAT */
		case 4U: /* STENCILOP_DECRSAT */
			return v + 1U;
		case 5U: /* STENCILOP_INCR */
		case 6U: /* STENCILOP_DECR */
			return v + 2U;
		case 7U: /* STENCILOP_INVERT */
			return SVGA3D_STENCILOP_INVERT;
	}
	return SVGA3D_STENCILOP_INVALID;
}

#ifdef PRINT_PS
static
char const* xlate_ps_reg_type(uint8_t u)
{
	if (u < 7U)
		return ps_regtype_names[u];
	return ps_regtype_names[7U];
}

static
char const* xlate_ps_mask(char* str, uint8_t mask)
{
	char* p = str;
	int i;
	if ((mask & 15U) == 15U)
		goto done;
	*p++ = '.';
	for (i = 0; i != 4; ++i)
		if (mask & (1U << i))
			*p++ = coord_letters[i];
done:
	*p = 0;
	return str;
}

static
char const* xlate_ps_arith_src_reg(char* str, uint8_t regtype, uint8_t regnum, uint16_t mask)
{
	char* p = str;
	int i;
	*p++ = ',';
	*p++ = ' ';
	p += snprintf(p, 31, "%s%u", xlate_ps_reg_type(regtype), regnum);
	if ((mask & 0xFFFFU) == 0x0123U)
		goto done;
	*p++ = '.';
	for (i = 3; i >= 0; --i) {
		if ((mask >> (4 * i)) & 8U)
			*p++ = '-';
		*p++ = coord_letters[(mask >> (4 * i)) & 7U];
	}
done:
	*p = 0;
	return str;
}
#endif /* PRINT_PS */

static
IOReturn analyze_vertex_format(uint32_t s2,
							   uint32_t s4,
							   uint8_t* texc_mask, /* OUT */
							   SVGA3dVertexDecl* pArray, /* OUT */
							   size_t* num_decls) /* IN/OUT */
{
	size_t i = 0U, d3d_size = 0U;
	uint32_t tc;
	uint8_t tc_mask = 0U;

	if (!num_decls)
		return kIOReturnBadArgument;
	if (i >= *num_decls)
		goto set_mask;
	if (!pArray)
		return kIOReturnBadArgument;
	bzero(pArray, (*num_decls) * sizeof *pArray);
	pArray[i].identity.usage = SVGA3D_DECLUSAGE_POSITIONT;
	pArray[i].array.offset = static_cast<uint32_t>(d3d_size);
	switch (bit_select(s4, 6, 3)) {
		case 1U: /* XYZ */
			pArray[i].identity.type = SVGA3D_DECLTYPE_FLOAT3;
			d3d_size += 3U * sizeof(float);
			break;
		case 2U: /* XYZW */
			pArray[i].identity.type = SVGA3D_DECLTYPE_FLOAT4;
			d3d_size += 4U * sizeof(float);
			break;
		case 3U: /* XY */
			pArray[i].identity.type = SVGA3D_DECLTYPE_FLOAT2;
			d3d_size += 2U * sizeof(float);
			break;
		case 4U: /* XYW */
			return kIOReturnUnsupported;
		default:
			return kIOReturnError;
	}
	++i;
	if (i >= *num_decls)
		goto set_strides;
	if (s4 & (1U << 12)) {	/* S4_VFMT_POINT_WIDTH */
		pArray[i].identity.usage = SVGA3D_DECLUSAGE_PSIZE;
		pArray[i].identity.type = SVGA3D_DECLTYPE_FLOAT1;
		pArray[i].array.offset = static_cast<uint32_t>(d3d_size);
		d3d_size += sizeof(float);
		++i;
		if (i >= *num_decls)
			goto set_strides;
	}
	if (s4 & (1U << 10)) { /* S4_VFMT_COLOR */
		pArray[i].identity.usage = SVGA3D_DECLUSAGE_COLOR;
		pArray[i].identity.type = SVGA3D_DECLTYPE_D3DCOLOR;
		pArray[i].array.offset = static_cast<uint32_t>(d3d_size);
		d3d_size += sizeof(uint32_t);
		++i;
		if (i >= *num_decls)
			goto set_strides;
	}
	if (s4 & (1U << 11)) { /* S4_VFMT_SPEC_FOG */
		pArray[i].identity.usage = SVGA3D_DECLUSAGE_COLOR;
		pArray[i].identity.usageIndex = 1U;
		pArray[i].identity.type = SVGA3D_DECLTYPE_D3DCOLOR;
		pArray[i].array.offset = static_cast<uint32_t>(d3d_size);
		d3d_size += sizeof(uint32_t);
		++i;
		if (i >= *num_decls)
			goto set_strides;
	}
	if (s4 & (1U << 2)) { /* S4_VFMT_FOG_PARAM */
		d3d_size += sizeof(float);
		++i;
		if (i >= *num_decls)
			goto set_strides;
	}
	for (tc = 0U; tc != 8U; ++tc) {
		switch (bit_select(s2, static_cast<int>(tc) * 4, 4)) {
			case 0U: /* TEXCOORDFMT_2D */
				pArray[i].identity.type = SVGA3D_DECLTYPE_FLOAT2;
				pArray[i].array.offset = static_cast<uint32_t>(d3d_size);
				d3d_size += 2U * sizeof(float);
				break;
			case 1U: /* TEXCOORDFMT_3D */
				pArray[i].identity.type = SVGA3D_DECLTYPE_FLOAT3;
				pArray[i].array.offset = static_cast<uint32_t>(d3d_size);
				d3d_size += 3U * sizeof(float);
				break;
			case 2U: /* TEXCOORDFMT_4D */
				pArray[i].identity.type = SVGA3D_DECLTYPE_FLOAT4;
				pArray[i].array.offset = static_cast<uint32_t>(d3d_size);
				d3d_size += 4U * sizeof(float);
				break;
			case 3U: /* TEXCOORDFMT_1D */
				pArray[i].identity.type = SVGA3D_DECLTYPE_FLOAT1;
				pArray[i].array.offset = static_cast<uint32_t>(d3d_size);
				d3d_size += sizeof(float);
				break;
			case 4U: /* TEXCOORDFMT_2D_16 */
				pArray[i].identity.type = SVGA3D_DECLTYPE_FLOAT16_2;
				pArray[i].array.offset = static_cast<uint32_t>(d3d_size);
				d3d_size += sizeof(uint32_t);
				break;
			case 5U: /* TEXCOORDFMT_4D_16 */
				pArray[i].identity.type = SVGA3D_DECLTYPE_FLOAT16_4;
				pArray[i].array.offset = static_cast<uint32_t>(d3d_size);
				d3d_size += 2U * sizeof(uint32_t);
				break;
			case 15U:
				continue;
			default:
				return kIOReturnError;
		}
		pArray[i].identity.usage = SVGA3D_DECLUSAGE_TEXCOORD;
		pArray[i].identity.usageIndex = tc;
		tc_mask |= 1U << tc;
		++i;
		if (i >= *num_decls)
			break;
	}
set_strides:
	*num_decls = i;
	for (i = 0U; i != *num_decls; ++i)
		pArray[i].array.stride = static_cast<uint32_t>(d3d_size);
set_mask:
	if (texc_mask)
		*texc_mask = tc_mask;
	return kIOReturnSuccess;
}

static
void make_polygon_index_array(uint16_t* arr, uint16_t base_index, uint16_t num_vertices)
{
	uint16_t v1, v2;
	if (!num_vertices)
		return;
	v1 = 0U;
	v2 = num_vertices - 1U;
	*arr++ = base_index + (v1++);
	while (v1 < v2) {
		*arr++ = base_index + (v1++);
		*arr++ = base_index + (v2--);
	}
	if (v1 == v2)
		*arr = base_index + v1;
}

static
void flatshade_polygon(uint8_t* vertex_array,
					   size_t num_vertices,
					   SVGA3dVertexDecl const* decls,
					   size_t num_decls)
{
	size_t i, j;

	for (i = 0U; i != num_decls; ++i)
		if (decls[i].identity.type == SVGA3D_DECLTYPE_D3DCOLOR) {
			uint8_t* p = vertex_array + decls[i].array.offset;
			uint32_t color = *reinterpret_cast<uint32_t const*>(p);
			p += decls[i].array.stride;
			for (j = 1U; j != num_vertices; ++j, p += decls[i].array.stride)
				*reinterpret_cast<uint32_t*>(p) = color;
		}
}

#if 0
static
void make_diag(float* matrix, float d0, float d1, float d2, float d3)
{
	bzero(matrix, 16U * sizeof(float));
	matrix[0]  = d0;
	matrix[5]  = d1;
	matrix[10] = d2;
	matrix[15] = d3;
}
#endif

#pragma mark -
#pragma mark Private Methods
#pragma mark -

HIDDEN
void CLASS::CleanupIpp()
{
	if (m_provider && isIdValid(m_context_id)) {
		purge_shader_cache();
#if 0
		unload_fixed_shaders();
#endif
		m_provider->destroyContext(m_context_id);
		m_provider->FreeContextID(m_context_id);
		m_context_id = SVGA_ID_INVALID;
	}
	purge_arrays();
	if (m_float_cache) {
		IOFreeAligned(m_float_cache, 64U * sizeof(float));
		m_float_cache = 0;
	}
}

HIDDEN
uint32_t CLASS::cache_shader(uint32_t const* source, uint32_t num_dwords)
{
	MD5_CTX md5_ctx;
	uint8_t hash[MD5_DIGEST_LENGTH];
	ShaderEntry *e, *f;
	SVGA3D* svga3d;
	uint32_t i;

	if (!source || !num_dwords)
		return SVGA_ID_INVALID;
	MD5Init(&md5_ctx);
	MD5Update(&md5_ctx, source, static_cast<unsigned>(num_dwords * sizeof(uint32_t)));
	MD5Final(&hash[0], &md5_ctx);
	for (f = 0, e = m_shader_cache; e; f = e, e = e->next)
		if (!memcmp(&hash[0], &e->md5[0], sizeof hash)) {
			if (e != m_shader_cache) {
				/*
				 * move-to-front
				 */
				if (f)
					f->next = e->next;
				e->next = m_shader_cache;
				m_shader_cache = e;
			}
			goto done;
		}
	e = static_cast<typeof e>(IOMalloc(sizeof *e));
	if (!e)
		return SVGA_ID_INVALID;
	memcpy(&e->md5[0], &hash[0], sizeof hash);
	e->shader_type = SVGA3D_SHADERTYPE_PS;
	e->shader_id = SVGA_ID_INVALID;
	e->next = m_shader_cache;
	m_shader_cache = e;
	for (i = 0U; i != NUM_FIXED_SHADERS; ++i)
		if (!memcmp(&hash[0], &g_hashes[2U * i], sizeof hash)) {
			svga3d = m_provider->lock3D();
			if (!svga3d)
				goto done;
			e->shader_id = m_next_shid++;
			svga3d->DefineShader(m_context_id,
								 e->shader_id,
								 e->shader_type,
								 reinterpret_cast<uint32_t const*>(g_pointers[i]),
								 g_lengths[i]);	// Note: ignores error
			m_provider->unlock3D();
			goto done;
		}
#ifdef PRINT_PS
	GLLog(3, "%s: shader hash { %#llx, %#llx }\n", __FUNCTION__,
		  *reinterpret_cast<uint64_t const*>(&hash[0]),
		  *reinterpret_cast<uint64_t const*>(&hash[8]));
	ip_print_ps(source, num_dwords);
#endif
done:
	return e->shader_id;
}

HIDDEN
void CLASS::purge_shader_cache()
{
	ShaderEntry *e, *f;
	SVGA3D* svga3d;
	if (!m_provider || !isIdValid(m_context_id))
		goto just_delete_them;
	svga3d = m_provider->lock3D();
	if (!svga3d)
		goto just_delete_them;
	svga3d->SetShader(m_context_id, SVGA3D_SHADERTYPE_PS, SVGA_ID_INVALID);
#if 0
	svga3d->SetShader(m_context_id, SVGA3D_SHADERTYPE_VS, SVGA_ID_INVALID);
#endif
	m_active_shid = SVGA_ID_INVALID - 1;
	for (e = m_shader_cache; e; e = e->next)
		if (isIdValid(e->shader_id))
			svga3d->DestroyShader(m_context_id, e->shader_id, e->shader_type);
	m_provider->unlock3D();

just_delete_them:
	for (e = m_shader_cache; e; e = f) {
		f = e->next;
		IOFree(e, sizeof *e);
	}
	m_shader_cache = 0;
	m_next_shid = 0U;
}

HIDDEN
void CLASS::adjust_texture_coords(uint8_t* vertex_array,
								  size_t num_vertices,
								  void const* decls,
								  size_t num_decls)
{
	SVGA3dVertexDecl const* _decls = static_cast<SVGA3dVertexDecl const*>(decls);
	size_t i, j;

	/*
	 * Note: The GLD always maps TexCoord set i to Sampler i.  Since
	 *   there are 16 samplers and only 8 coord-sets, this implies
	 *   samplers 8-15 can only be accessed from a pixel shader with
	 *   computed coordinates.
	 *   Since OpenGL uses normalized coordinates, any coordinates
	 *   computed by a pixel shader should always be normalized, and
	 *   the GLD should have marked the corresponding sampler as
	 *   using normalized cooridnates.
	 *   Additionally, the GLD always maps sampler i to texture stage i,
	 *   [there are 16 of each], however the code handles the general case.
	 */
#if 0
	GLLog(3, "%s:   s2 == %#x, s4 == %#x\n", __FUNCTION__,
		  m_intel_state.imm_s[2], m_intel_state.imm_s[4] & 0x1FC4U);
#endif
	for (i = 0U; i != num_decls; ++i)
		if (_decls[i].identity.usage == SVGA3D_DECLUSAGE_TEXCOORD &&
			bit_select(m_intel_state.prettex_coordinates, _decls[i].identity.usageIndex, 1)) {
			float const* f = m_float_cache + 4U *
				get4bits(&m_intel_state.s2t_map, _decls[i].identity.usageIndex);
			/*
			 * Barbarically assume at least FLOAT2 and adjust
			 *   Note: The GLD always uses FLOAT4 projective tex-coords.
			 */
			for (j = 0U; j != num_vertices; ++j) {
				float* q = reinterpret_cast<float*>(vertex_array + j * _decls[i].array.stride + _decls[i].array.offset);
#if 0
				GLLog(3, "%s:   adjusting X == %d, Y == %d, Z == %d, W == %d\n", __FUNCTION__,
					  static_cast<int>(q[0]),
					  static_cast<int>(q[1]),
					  static_cast<int>(q[2] * 32767.0F),
					  static_cast<int>(q[3] * 32767.0F));
#endif
				q[0] *= f[0];
				q[1] *= f[1];
			}
		}
#if 0
	for (j = 0U; j != num_vertices; ++j) {
		float* q = reinterpret_cast<float*>(vertex_array + j * _decls[0].array.stride + _decls[0].array.offset);
		unsigned const* r = reinterpret_cast<unsigned const*>(vertex_array + j * _decls[1].array.stride + _decls[1].array.offset);
		GLLog(3, "%s:   vertex coord X == %d, Y == %d, Z == %d, W == %d, color == %#x\n", __FUNCTION__,
			  static_cast<int>(q[0]),
			  static_cast<int>(q[1]),
			  static_cast<int>(q[2] * 32767.0F),
			  static_cast<int>(q[3] * 32767.0F),
			  r[0]);
	}
#endif
}

#pragma mark -
#pragma mark Intel Pipeline Processor
#pragma mark -

HIDDEN
uint8_t CLASS::calc_color_write_enable(void)
{
	uint8_t mask[2];
	if (!bit_select(m_intel_state.imm_s[6], 2, 1))
		return 0U;
	if (!(m_intel_state.param_cache_mask & (1U << 5)))
		return 0xFU;
	mask[0] = (~bit_select(m_intel_state.imm_s[5], 28, 4)) & 0xFU;
	mask[1] = mask[0] & 10U;
	mask[1] |= bit_select(mask[0], 2, 1);
	mask[1] |= bit_select(mask[0], 0, 1) << 2;
	return mask[1];
}

HIDDEN
bool CLASS::cache_misc_reg(uint8_t regnum, uint32_t value)
{
	uint16_t mask;
	if (regnum >= 8U)
		return true;
	mask = (1U << (8U + regnum));
	if (m_intel_state.param_cache_mask & mask) {
		if (value == m_intel_state.imm_s[8U + regnum])
			return false;
		m_intel_state.imm_s[8U + regnum] = value;
	} else {
		m_intel_state.imm_s[8U + regnum] = value;
		m_intel_state.param_cache_mask |= mask;
	}
	return true;
}

HIDDEN
void CLASS::ipp_discard_renderstate(void)
{
	m_intel_state.param_cache_mask = 0U;
}

HIDDEN
void CLASS::ip_prim3d_poly(uint32_t const* vertex_data, size_t num_vertex_dwords)
{
	size_t i, num_decls, num_vertices, vsize, isize;
	uint8_t texc_mask;
	IOReturn rc;
	SVGA3dVertexDecl decls[MAX_NUM_DECLS];
	SVGA3dPrimitiveRange range;

	if (!num_vertex_dwords || !vertex_data)
		return; // nothing to do
	num_decls = sizeof decls / sizeof decls[0];
	rc = analyze_vertex_format(m_intel_state.imm_s[2],
							   m_intel_state.imm_s[4],
							   &texc_mask,
							   &decls[0],
							   &num_decls);
	if (rc != kIOReturnSuccess) {
		GLLog(1, "%s: analyze_vertex_format return %#x\n", __FUNCTION__, rc);
		return;
	}
	if (!num_decls || !decls[0].array.stride)
		return;	// nothing to do
#if 0
	GLLog(3, "%s:   num vertex decls == %lu\n", __FUNCTION__, num_decls);
#endif
	vsize = num_vertex_dwords * sizeof(uint32_t);
	num_vertices = vsize / decls[0].array.stride;
	if (num_vertices < 3U)
		return; // nothing to do
	isize = num_vertices * sizeof(uint16_t);
	rc = alloc_arrays(vsize + isize);
	if (rc != kIOReturnSuccess) {
		GLLog(1, "%s: alloc_arrays return %#x\n", __FUNCTION__, rc);
		return;
	}
	memcpy(m_arrays.kernel_ptr, vertex_data, vsize);
#if 0
	GLLog(3, "%s:   vertex_size == %u, num_vertices == %lu, copied %lu bytes\n",__FUNCTION__,
		  decls[0].array.stride, num_vertices, vsize);
#endif
	/*
	 * Prepare Index Array for a polygon
	 */
	make_polygon_index_array(reinterpret_cast<uint16_t*>(m_arrays.kernel_ptr + vsize),
							 0U,
							 static_cast<uint16_t>(num_vertices));
	if (m_intel_state.prettex_coordinates & texc_mask)
		adjust_texture_coords(m_arrays.kernel_ptr,
							  num_vertices,
							  &decls[0],
							  num_decls);
	if (m_intel_state.imm_s[4] & (1U << 15))
		flatshade_polygon(m_arrays.kernel_ptr,
						  num_vertices,
						  &decls[0],
						  num_decls);
	rc = upload_arrays(vsize + isize);
	if (rc != kIOReturnSuccess) {
		GLLog(1, "%s: upload_arrays return %#x\n", __FUNCTION__, rc);
		return;
	}
	range.primType = SVGA3D_PRIMITIVE_TRIANGLESTRIP;
	range.primitiveCount = static_cast<uint32_t>(num_vertices - 2U);
	range.indexArray.surfaceId = m_arrays.sid;
	range.indexArray.offset = static_cast<uint32_t>(vsize);
	range.indexArray.stride = sizeof(uint16_t);
	range.indexWidth = sizeof(uint16_t);
	range.indexBias = 0U;
	for (i = 0U; i != num_decls; ++i) {
		decls[i].array.surfaceId = m_arrays.sid;
		decls[i].rangeHint.last = static_cast<uint32_t>(num_vertices);
	}
	rc = m_provider->drawPrimitives(m_context_id,
									static_cast<uint32_t>(num_decls),
									1U,
									&decls[0],
									&range);
	if (rc != kIOReturnSuccess)
		GLLog(1, "%s: drawPrimitives return %#x\n", __FUNCTION__, rc);
}

HIDDEN
void CLASS::ip_prim3d_direct(uint32_t prim_kind, uint32_t const* vertex_data, size_t num_vertex_dwords)
{
	size_t i, num_decls, num_vertices, vsize;
	uint8_t texc_mask;
	IOReturn rc;
	SVGA3dVertexDecl decls[MAX_NUM_DECLS];
	SVGA3dPrimitiveRange range;

	if (!num_vertex_dwords || !vertex_data)
		return; // nothing to do
	num_decls = sizeof decls / sizeof decls[0];
	rc = analyze_vertex_format(m_intel_state.imm_s[2],
							   m_intel_state.imm_s[4],
							   &texc_mask,
							   &decls[0],
							   &num_decls);
	if (rc != kIOReturnSuccess) {
		GLLog(1, "%s: analyze_vertex_format return %#x\n", __FUNCTION__, rc);
		return;
	}
	if (!num_decls || !decls[0].array.stride)
		return;	// nothing to do
#if 0
	GLLog(3, "%s:   num vertex decls == %lu\n", __FUNCTION__, num_decls);
#endif
	vsize = num_vertex_dwords * sizeof(uint32_t);
	num_vertices = vsize / decls[0].array.stride;
	switch (prim_kind) {
		case 0: /* PRIM3D_TRILIST */
			if (num_vertices < 3U)
				return; // nothing to do
			range.primType = SVGA3D_PRIMITIVE_TRIANGLELIST;
			range.primitiveCount = static_cast<uint32_t>(num_vertices / 3U);
			break;
		case 1: /* PRIM3D_TRISTRIP */
			if (num_vertices < 3U)
				return; // nothing to do
			range.primType = SVGA3D_PRIMITIVE_TRIANGLESTRIP;
			range.primitiveCount = static_cast<uint32_t>(num_vertices - 2U);
			break;
		case 3: /* PRIM3D_TRIFAN */
			if (num_vertices < 3U)
				return; // nothing to do
			range.primType = SVGA3D_PRIMITIVE_TRIANGLEFAN;
			range.primitiveCount = static_cast<uint32_t>(num_vertices - 2U);
			break;
		case 5: /* PRIM3D_LINELIST */
			if (num_vertices < 2U)
				return; // nothing to do
			range.primType = SVGA3D_PRIMITIVE_LINELIST;
			range.primitiveCount = static_cast<uint32_t>(num_vertices >> 1);
			break;
		case 6: /* PRIM3D_LINESTRIP */
			if (num_vertices < 2U)
				return; // nothing to do
			range.primType = SVGA3D_PRIMITIVE_LINESTRIP;
			range.primitiveCount = static_cast<uint32_t>(num_vertices - 1U);
			break;
		case 8: /* PRIM3D_POINTLIST */
			if (!num_vertices)
				return; // nothing to do
			range.primType = SVGA3D_PRIMITIVE_POINTLIST;
			range.primitiveCount = static_cast<uint32_t>(num_vertices);
			break;
		default:
			return;	// error, shouldn't get here
	}
	rc = alloc_arrays(vsize);
	if (rc != kIOReturnSuccess) {
		GLLog(1, "%s: alloc_arrays return %#x\n", __FUNCTION__, rc);
		return;
	}
	memcpy(m_arrays.kernel_ptr, vertex_data, vsize);
#if 0
	GLLog(3, "%s:   vertex_size == %u, num_vertices == %lu, copied %lu bytes\n",__FUNCTION__,
		  decls[0].array.stride, num_vertices, vsize);
#endif
	if (m_intel_state.prettex_coordinates & texc_mask)
		adjust_texture_coords(m_arrays.kernel_ptr,
							  num_vertices,
							  &decls[0],
							  num_decls);
	rc = upload_arrays(vsize);
	if (rc != kIOReturnSuccess) {
		GLLog(1, "%s: upload_arrays return %#x\n", __FUNCTION__, rc);
		return;
	}
	range.indexArray.surfaceId = SVGA_ID_INVALID;
	range.indexArray.offset = 0U;
	range.indexArray.stride = sizeof(uint16_t);
	range.indexWidth = sizeof(uint16_t);
	range.indexBias = 0U;
	for (i = 0U; i != num_decls; ++i) {
		decls[i].array.surfaceId = m_arrays.sid;
		decls[i].rangeHint.last = static_cast<uint32_t>(num_vertices);
	}
	rc = m_provider->drawPrimitives(m_context_id,
									static_cast<uint32_t>(num_decls),
									1U,
									&decls[0],
									&range);
	if (rc != kIOReturnSuccess)
		GLLog(1, "%s: drawPrimitives return %#x\n", __FUNCTION__, rc);
}

HIDDEN
uint32_t CLASS::ip_prim3d(uint32_t* p, uint32_t cmd)
{
	DefineRegion<1U> tmpRegion;
	uint32_t skip = (cmd & 0xFFFFU) + 2U, primkind = bit_select(cmd, 18, 5);
	float const* pf;

	if (cmd & (1U << 23)) {
		GLLog(1, "%s: indirect primitive\n", __FUNCTION__);
		if (cmd & (1U << 17)) {
			skip = cmd & 0xFFFFU;
			if (!skip) {	// variable length, look for 0xFFFFU terminator
				uint16_t const* q = reinterpret_cast<typeof q>(&p[1]);
				for (skip = 0U; q[skip++] != 0xFFFFU;);
			}
			// skip == number of uint16s
			skip = (skip + 1U) / 2U + 1U;
		} else
			skip = 2U;
		return skip;	// Indirect Primitive, not handled
	}
	/*
	 * Direct Primitive
	 */
#if 0
	GLLog(3, "%s:   primkind == %u, s2 == %#x, s4 == %#x\n", __FUNCTION__,
		  primkind, m_intel_state.imm_s[2], m_intel_state.imm_s[4]);
#endif
	/*
	 * Note: Vertex Fog isn't supported either, but
	 *   be silent about it
	 */
	if (m_intel_state.imm_s[4] & 0x200U)
		GLLog(1, "%s:   Unsupported vertex fields %#x\n", __FUNCTION__,
			  m_intel_state.imm_s[4] & 0x200U);
	switch (primkind) {
		case 0: /* PRIM3D_TRILIST */
		case 1: /* PRIM3D_TRISTRIP */
		case 3: /* PRIM3D_TRIFAN */
		case 5: /* PRIM3D_LINELIST */
		case 6: /* PRIM3D_LINESTRIP */
		case 8: /* PRIM3D_POINTLIST */
			ip_prim3d_direct(primkind, &p[1], skip -1U);
			break;
		case 4: /* PRIM3D_POLY */
			ip_prim3d_poly(&p[1], skip - 1U);
			break;
		case 10: /* PRIM3D_CLEAR_RECT */
			if (!(m_intel_state.clear.mask & 7U))
				break;	// nothing to do
			pf = reinterpret_cast<float const*>(p + 1);
			set_region(&tmpRegion.r,
					   static_cast<uint32_t>(pf[4]),
					   static_cast<uint32_t>(pf[5]),
					   static_cast<uint32_t>(pf[0] - pf[4]),
					   static_cast<uint32_t>(pf[1] - pf[5]));
#if 0
			GLLog(3, "%s:     issuing clear cmd, cid == %u, X == %d, Y == %d, W == %d, H == %d, mask == %u\n", __FUNCTION__,
				  m_context_id,
				  static_cast<int>(tmpRegion.r.bounds.x),
				  static_cast<int>(tmpRegion.r.bounds.y),
				  static_cast<int>(tmpRegion.r.bounds.w),
				  static_cast<int>(tmpRegion.r.bounds.h),
				  translate_clear_mask(m_intel_state.clear.mask));
#endif
			m_provider->clear(m_context_id,
							  SVGA3dClearFlag(translate_clear_mask(m_intel_state.clear.mask)),
							  &tmpRegion.r,
							  m_intel_state.clear.color,
							  m_intel_state.clear.depth,
							  m_intel_state.clear.stencil);
			break;
		case 2: /* PRIM3D_TRISTRIP_RVRSE */
		case 7: /* PRIM3D_RECTLIST */
		case 9: /* PRIM3D_DIB */
		case 13: /* PRIM3D_ZONE_INIT */
			GLLog(1, "%s:   primkind == %u Unsupported\n", __FUNCTION__, primkind);
			break;
	}
	return skip;
}

HIDDEN
uint32_t CLASS::ip_load_immediate(uint32_t* p, uint32_t cmd)
{
	uint32_t i, skip = (cmd & 0xFU) + 2U, *limit = p + skip;
	SVGA3dRenderState rs[11];

	for (i = 0U, ++p; i != 8U && p < limit; ++i)
		if (cmd & (1U << (4 + i))) {
			switch (i) {
				case 4U:
				case 5U:
				case 6U:
				case 7U:
					if ((m_intel_state.param_cache_mask & (1U << i)) &&
						m_intel_state.imm_s[i] == *p) {
						++p;
						continue;
					}
					m_intel_state.imm_s[i] = *p++;
					m_intel_state.param_cache_mask |= (1U << i);
					break;
				default:
					m_intel_state.imm_s[i] = *p++;
					break;
			}
			switch (i) {
				case 2U:
				case 3U:
					break;
				case 4U:
#if 0
					GLLog(3, "%s: imm4 - PW %u, LW %u, FS %#x, CM %u, "
						  "FDD %u, FDS %u, LDO %u, SP %u, LA %u\n", __FUNCTION__,
						  bit_select(m_intel_state.imm_s[i], 23, 9),
						  bit_select(m_intel_state.imm_s[i], 19, 4),
						  bit_select(m_intel_state.imm_s[i], 15, 4),
						  bit_select(m_intel_state.imm_s[i], 13, 2),
						  bit_select(m_intel_state.imm_s[i],  5, 1),
						  bit_select(m_intel_state.imm_s[i],  4, 1),
						  bit_select(m_intel_state.imm_s[i],  3, 1),
						  bit_select(m_intel_state.imm_s[i],  1, 1),
						  bit_select(m_intel_state.imm_s[i],  0, 1));
#endif
					rs[0].state = SVGA3D_RS_POINTSIZE;
					rs[0].floatValue = static_cast<float>(bit_select(m_intel_state.imm_s[i], 23, 9));
					rs[1].state = SVGA3D_RS_SHADEMODE;
					rs[1].uintValue = (m_intel_state.imm_s[i] & (1U << 15)) ? SVGA3D_SHADEMODE_FLAT : SVGA3D_SHADEMODE_SMOOTH;
					rs[2].state = SVGA3D_RS_CULLMODE;
					rs[2].uintValue = bit_select(m_intel_state.imm_s[i], 13, 2);
					rs[3].state = SVGA3D_RS_POINTSPRITEENABLE;
					rs[3].uintValue = bit_select(m_intel_state.imm_s[i], 1, 1);
					rs[4].state = SVGA3D_RS_ANTIALIASEDLINEENABLE;
					rs[4].uintValue = bit_select(m_intel_state.imm_s[i], 0, 1);
					m_provider->setRenderState(m_context_id, 5U, &rs[0]);
					break;
				case 5U:
#if 0
					GLLog(3, "%s: imm5 - WD %#x, FDPS %u, LP %u, GDO %u, FEn %u, "
						  "SR %#x, STF %u, SF %u, PZF %u, PZP %u, "
						  "SW %u, ST %u, CD %u, LO %u\n", __FUNCTION__,
						  bit_select(m_intel_state.imm_s[i], 28, 4),
						  bit_select(m_intel_state.imm_s[i], 27, 1),
						  bit_select(m_intel_state.imm_s[i], 26, 1),
						  bit_select(m_intel_state.imm_s[i], 25, 1),
						  bit_select(m_intel_state.imm_s[i], 24, 1),
						  bit_select(m_intel_state.imm_s[i], 16, 8),
						  bit_select(m_intel_state.imm_s[i], 13, 3),
						  bit_select(m_intel_state.imm_s[i], 10, 3),
						  bit_select(m_intel_state.imm_s[i],  7, 3),
						  bit_select(m_intel_state.imm_s[i],  4, 3),
						  bit_select(m_intel_state.imm_s[i],  3, 1),
						  bit_select(m_intel_state.imm_s[i],  2, 1),
						  bit_select(m_intel_state.imm_s[i],  1, 1),
						  bit_select(m_intel_state.imm_s[i],  0, 1));
#endif
					rs[0].state = SVGA3D_RS_DITHERENABLE;
					rs[0].uintValue = bit_select(m_intel_state.imm_s[i],  1, 1);
					rs[1].state = SVGA3D_RS_LASTPIXEL;
					rs[1].uintValue = bit_select(m_intel_state.imm_s[i], 26, 1);
					rs[2].state = SVGA3D_RS_FOGENABLE;
					rs[2].uintValue = bit_select(m_intel_state.imm_s[i], 24, 1);
					rs[3].state = SVGA3D_RS_STENCILREF;
					rs[3].uintValue = bit_select(m_intel_state.imm_s[i], 16, 8);
					rs[4].state = SVGA3D_RS_STENCILFUNC;
					rs[4].uintValue = bit_select(m_intel_state.imm_s[i], 2, 1) ?
					xlate_comparefunc(bit_select(m_intel_state.imm_s[i], 13, 3)) : SVGA3D_CMP_ALWAYS;
					rs[5].state = SVGA3D_RS_STENCILFAIL;
					rs[5].uintValue = xlate_stencilop(bit_select(m_intel_state.imm_s[i], 10, 3));
					rs[6].state = SVGA3D_RS_STENCILZFAIL;
					rs[6].uintValue = xlate_stencilop(bit_select(m_intel_state.imm_s[i], 7, 3));
					rs[7].state = SVGA3D_RS_STENCILPASS;
					rs[7].uintValue = xlate_stencilop(bit_select(m_intel_state.imm_s[i], 4, 3));
					rs[8].state = SVGA3D_RS_STENCILENABLE;
					rs[8].uintValue = bit_select(m_intel_state.imm_s[i],  3, 1);
					m_provider->setRenderState(m_context_id, 9U, &rs[0]);
					break;
				case 6U:
#if 0
					GLLog(3, "%s: imm6 - ATE %u, ATF %u, AR %u, DTE %u, DTF %u, "
						  "BE %u, BF %u, SBF %u , DBF %u, DWE %u, CWE %u, TPV %u\n", __FUNCTION__,
						  bit_select(m_intel_state.imm_s[i], 31, 1),
						  bit_select(m_intel_state.imm_s[i], 28, 3),
						  bit_select(m_intel_state.imm_s[i], 20, 8),
						  bit_select(m_intel_state.imm_s[i], 19, 1),
						  bit_select(m_intel_state.imm_s[i], 16, 3),
						  bit_select(m_intel_state.imm_s[i], 15, 1),
						  bit_select(m_intel_state.imm_s[i], 12, 3),
						  bit_select(m_intel_state.imm_s[i],  8, 4),
						  bit_select(m_intel_state.imm_s[i],  4, 4),
						  bit_select(m_intel_state.imm_s[i],  3, 1),
						  bit_select(m_intel_state.imm_s[i],  2, 1),
						  bit_select(m_intel_state.imm_s[i],  0, 2));
#endif
					rs[0].state = SVGA3D_RS_COLORWRITEENABLE;
					rs[0].uintValue = calc_color_write_enable();
					rs[1].state = SVGA3D_RS_BLENDENABLE;
					rs[1].uintValue = bit_select(m_intel_state.imm_s[i], 15, 1);
					rs[2].state = SVGA3D_RS_BLENDEQUATION;
					rs[2].uintValue = bit_select(m_intel_state.imm_s[i], 12, 3) + 1U;
					rs[3].state = SVGA3D_RS_SRCBLEND;
					rs[3].uintValue = bit_select(m_intel_state.imm_s[i],  8, 4);
					rs[4].state = SVGA3D_RS_DSTBLEND;
					rs[4].uintValue = bit_select(m_intel_state.imm_s[i],  4, 4);
					rs[5].state = SVGA3D_RS_ALPHATESTENABLE;
					rs[5].uintValue = bit_select(m_intel_state.imm_s[i], 31, 1);
					rs[6].state = SVGA3D_RS_ALPHAFUNC;
					rs[6].uintValue = xlate_comparefunc(bit_select(m_intel_state.imm_s[i], 28, 3));
					rs[7].state = SVGA3D_RS_ALPHAREF;
					rs[7].uintValue = bit_select(m_intel_state.imm_s[i], 20, 8);
					rs[8].state = SVGA3D_RS_ZENABLE;
					rs[8].uintValue = bit_select(m_intel_state.imm_s[i], 19, 1);
					rs[9].state = SVGA3D_RS_ZFUNC;
					rs[9].uintValue = xlate_comparefunc(bit_select(m_intel_state.imm_s[i], 16, 3));
					rs[10].state = SVGA3D_RS_ZWRITEENABLE;
					rs[10].uintValue = bit_select(m_intel_state.imm_s[i], 3, 1);
					/*
					 * Note: Direct3D doesn't seem to have Tristrip Provoking Vertex control
					 */
					m_provider->setRenderState(m_context_id, 11U, &rs[0]);
					break;
				case 7U:
#if 0
					GLLog(3, "%s: depth bias %u\n", __FUNCTION__,
						  static_cast<uint32_t>(*reinterpret_cast<float*>(&m_intel_state.imm_s[i]) * 32767.0F));
#endif
					rs[0].state = SVGA3D_RS_DEPTHBIAS;
					if (bit_select(m_intel_state.param_cache_mask, 5, 1) &&
						bit_select(m_intel_state.imm_s[5], 25, 1))	/* S5_GLOBAL_DEPTH_OFFSET_ENABLE */
						rs[0].uintValue = m_intel_state.imm_s[i];	// Note: this is in fact a float
					else
						rs[0].floatValue = 0.0F;
					m_provider->setRenderState(m_context_id, 1U, &rs[0]);
					break;
				default:
#ifdef GL_DEV
					GLLog(3, "%s: imm%u - %#x\n", __FUNCTION__, i, m_intel_state.imm_s[i]);
#endif
					break;
			}
		}
	return skip;
}

HIDDEN
uint32_t CLASS::ip_clear_params(uint32_t* p, uint32_t cmd)
{
	uint32_t skip = (cmd & 0xFFFFU) + 2U;
	if (skip < 7U)
		return skip;
	m_intel_state.clear.mask = p[1];
	/*
	 * Note: this is for CLEAR_RECT (guess).  ZONE_INIT... who knows.
	 */
#if 0
	GLLog(3, "%s: clear color == %#x\n", __FUNCTION__, p[4]);
#endif
	m_intel_state.clear.color = p[4];
	m_intel_state.clear.depth = *reinterpret_cast<float const*>(p + 5);
	m_intel_state.clear.stencil = p[6];
	return skip;
}

HIDDEN
void CLASS::ip_3d_map_state(uint32_t* p)
{
	uint32_t i, width, height, mask = p[1], *q = p + 2;
	float* f = m_float_cache;

	for (i = 0U; i != 16U; ++i, f += 4, mask >>= 1)
		if (mask & 1U) {
			/*
			 * cache surface ids, width & height
			 */
			m_intel_state.surface_ids[i] = q[0];
			width  = bit_select(q[1], 10, 11) + 1U;
			height = bit_select(q[1], 21, 11) + 1U;
#if 0
			GLLog(3, "%s: texture stage %u, sid == %u, w == %u, h == %u, mapsurf == %u, mt == %u\n", __FUNCTION__,
				  i, q[0], width, height,
				  bit_select(q[1], 7, 3),
				  bit_select(q[1], 3, 4));
#endif
#if 0
			GLLog(3, "%s:   tex %u data1 width %u, height %u, MAPSURF %u, MT %u, UF %u, TS %u, TW %u\n", __FUNCTION__,
				  q[0], width, height, mapsurf, mt,
				  bit_select(q[1], 2, 1),
				  bit_select(q[1], 1, 1),
				  q[1] & 1U);
			GLLog(3, "%s:   tex %u data2 pitch %u, cube-face-ena %#x, max-lod %#x, mip-layout %u, depth %u\n", __FUNCTION__,
				  q[0], pitch,
				  bit_select(q[2], 15, 6),
				  bit_select(q[2],  9, 6),
				  bit_select(q[2],  8, 1),
				  depth);
#endif
			f[0] = 1.0F / static_cast<float>(width);
			f[1] = 1.0F / static_cast<float>(height);
			f[2] = 1.0F;
			f[3] = 1.0F;
			q += 3;
		}
}

HIDDEN
void CLASS::ip_3d_sampler_state(uint32_t* p)
{
	uint32_t i, j, mask = p[1], *q = p + 2, max_aniso;
	uint8_t tmi;
	SVGA3dTextureState ts[10];
	uint32_t const num_states = sizeof ts / sizeof ts[0];

	max_aniso = m_provider->getDevCap(SVGA3D_DEVCAP_MAX_TEXTURE_ANISOTROPY);
	for (i = 0U; i != 16U; ++i, mask >>= 1)
		if (mask & 1U) {
			tmi = bit_select(q[1], 1, 4);
			set4bits(&m_intel_state.s2t_map, i, tmi);
#if 0
			GLLog(3, "%s:   txstage %u\n", __FUNCTION__, i);
			GLLog(3, "%s:     RGE %u, PPE %u, CSC %u, CKS %u, BML %u, "
				  "MipF %u, MagF %u, MinF %u, LB %#x, SE %u, MA %u, SF %u\n", __FUNCTION__,
				  bit_select(q[0], 31, 1),
				  bit_select(q[0], 30, 1),
				  bit_select(q[0], 29, 1),
				  bit_select(q[0], 27, 2),
				  bit_select(q[0], 22, 5),
				  bit_select(q[0], 20, 2),
				  bit_select(q[0], 17, 3),
				  bit_select(q[0], 14, 3),
				  bit_select(q[0],  5, 9),
				  bit_select(q[0],  4, 1),
				  bit_select(q[0],  3, 1),
				  bit_select(q[0],  0, 3));
			GLLog(3, "%s:     ML %u, KPE %u, TCX %u, TCY %u, TCZ %u, NC %u, TMI %u, DE %u\n", __FUNCTION__,
				  bit_select(q[1], 24, 8),
				  bit_select(q[1], 17, 1),
				  bit_select(q[1], 12, 3),
				  bit_select(q[1],  9, 3),
				  bit_select(q[1],  6, 3),
				  bit_select(q[1],  5, 1),
				  tmi,
				  bit_select(q[1],  0, 1));
			GLLog(3, "%s:     Border Color %#x\n", __FUNCTION__, q[2]);
#endif
#if 0
			GLLog(3, "%s:   binding S%u to T%u%s\n", __FUNCTION__,
				  i, tmi, bit_select(q[1], 5, 1) ? "" : " unnormalized-coords");
#endif
			if (bit_select(q[1], 5, 1))
				m_intel_state.prettex_coordinates &= ~(1U << i);
			else
				m_intel_state.prettex_coordinates |= (1U << i);
			for (j = 0U; j != num_states; ++j)
				ts[j].stage = i;
			ts[0].name = SVGA3D_TS_BIND_TEXTURE;
			ts[0].value = m_intel_state.surface_ids[tmi];
			ts[1].name = SVGA3D_TS_BORDERCOLOR;
			ts[1].value = q[2];
			ts[2].name = SVGA3D_TS_ADDRESSU;
			ts[2].value = xlate_taddress(bit_select(q[1], 12, 3));
			ts[3].name = SVGA3D_TS_ADDRESSV;
			ts[3].value = xlate_taddress(bit_select(q[1],  9, 3));
			ts[4].name = SVGA3D_TS_ADDRESSW;
			ts[4].value = xlate_taddress(bit_select(q[1],  6, 3));
			ts[5].name = SVGA3D_TS_MIPFILTER;
			ts[5].value = xlate_filter1(bit_select(q[0], 20, 2));
			ts[6].name = SVGA3D_TS_MAGFILTER;
			ts[6].value = xlate_filter2(bit_select(q[0], 17, 3));
			ts[7].name = SVGA3D_TS_MINFILTER;
			ts[7].value = xlate_filter2(bit_select(q[0], 14, 3));
			ts[8].name = SVGA3D_TS_TEXTURE_ANISOTROPIC_LEVEL;
			ts[8].value = ulmin(bit_select(q[0], 3, 1) ? 4U : 2U, max_aniso);
			ts[9].name = SVGA3D_TS_GAMMA;
			ts[9].floatValue = bit_select(q[0], 31, 1) ? 0.0F : 1.0F;
			/*
			 * TBD: handle base mip level, min lod, lod bias
			 */
			m_provider->setTextureState(m_context_id, num_states, &ts[0]);
			q += 3;
		}
}

HIDDEN
void CLASS::ip_misc_render_state(uint32_t selector, uint32_t* p)
{
	SVGA3dRenderState rs[2];
	IOAccelBounds scissorRect;
	/*
	 * TBD: optimize by caching existing values
	 */
	switch (selector) {
		case 0U:
#if 0
			GLLog(3, "%s: 3DSTATE_DEPTH_OFFSET_SCALE %u\n", __FUNCTION__,
				  static_cast<uint32_t>(*reinterpret_cast<float*>(&p[1]) * 32767.0F));
#endif
			rs[0].state = SVGA3D_RS_SLOPESCALEDEPTHBIAS;
			rs[0].uintValue = p[1];	// Note: this is in fact a float
			m_provider->setRenderState(m_context_id, 1U, &rs[0]);
			break;
		case 1U:
#if 0
			GLLog(3, "%s: 3DSTATE_SCISSOR_ENABLE %#x\n", __FUNCTION__, p[0] & 0xFFFFFFU);
#endif
			rs[0].state = SVGA3D_RS_SCISSORTESTENABLE;
			rs[0].uintValue = bit_select(p[0], 0, 1);
			m_provider->setRenderState(m_context_id, 1U, &rs[0]);
			break;
		case 2U:
#if 0
			GLLog(3, "%s: 3DSTATE_SCISSOR_RECT_0_CMD %u %u %u %u\n", __FUNCTION__,
				  bit_select(p[1], 16, 16),
				  bit_select(p[1],  0, 16),
				  bit_select(p[2], 16, 16),
				  bit_select(p[2],  0, 16));
#endif
			scissorRect.x = bit_select(p[1],  0, 16);
			scissorRect.y = bit_select(p[1], 16, 16);
			scissorRect.w = bit_select(p[2],  0, 16) - scissorRect.x + 1;
			scissorRect.h = bit_select(p[2], 16, 16) - scissorRect.y + 1;
			m_provider->setScissorRect(m_context_id, &scissorRect);
			break;
		case 3U:
#if 0
			GLLog(3, "%s: 3DSTATE_CONST_BLEND_COLOR_CMD %#x\n", __FUNCTION__, p[1]);
#endif
			rs[0].state = SVGA3D_RS_BLENDCOLOR;
			rs[0].uintValue = p[1];
			m_provider->setRenderState(m_context_id, 1U, &rs[0]);
			break;
		case 4U: /* 3DSTATE_MODES_4_CMD */
#if 0
			GLLog(3, "%s:   modes4 ELO %u, LO %u, EST %u, ST %u, ESW %u, SW %u\n", __FUNCTION__,
				  bit_select(p[0], 23, 1),
				  bit_select(p[0], 18, 4),
				  bit_select(p[0], 17, 1),
				  bit_select(p[0],  8, 8),
				  bit_select(p[0], 16, 1),
				  bit_select(p[0],  0, 8));
#endif
			rs[0].state = SVGA3D_RS_STENCILMASK;
			rs[0].uintValue = bit_select(p[0], 17, 1) ? bit_select(p[0], 8, 8) : 0xFFFFFFFFU;
			rs[1].state = SVGA3D_RS_STENCILWRITEMASK;
			rs[1].uintValue = bit_select(p[0], 16, 1) ? bit_select(p[0], 0, 8) : 0xFFFFFFFFU;
			m_provider->setRenderState(m_context_id, 2U, &rs[0]);
			break;
	}
}

HIDDEN
void CLASS::ip_independent_alpha_blend(uint32_t cmd)
{
	SVGA3dRenderState rs[4];
	uint32_t i = 0U;
#if 0
	GLLog(3, "%s: 3DSTATE_INDEPENDENT_ALPHA_BLEND_CMD MEn %u, En %u, MF %u, Func %u, "
		  "MSF %u, SF %u, MDF %u, DF %u\n", __FUNCTION__,
		  bit_select(cmd, 23, 1),
		  bit_select(cmd, 22, 1),
		  bit_select(cmd, 21, 1),
		  bit_select(cmd, 16, 3),
		  bit_select(cmd, 11, 1),
		  bit_select(cmd,  6, 4),
		  bit_select(cmd,  5, 1),
		  bit_select(cmd,  0, 4));
#endif
	if (bit_select(cmd, 23, 1)) {
		rs[i].state = SVGA3D_RS_SEPARATEALPHABLENDENABLE;
		rs[i++].uintValue = bit_select(cmd, 22, 1);
	}
	if (bit_select(cmd, 21, 1)) {
		rs[i].state = SVGA3D_RS_BLENDEQUATIONALPHA;
		rs[i++].uintValue = bit_select(cmd, 16, 3) + 1U;
	}
	if (bit_select(cmd, 11, 1)) {
		rs[i].state = SVGA3D_RS_SRCBLENDALPHA;
		rs[i++].uintValue = bit_select(cmd, 6, 4) + 1U;
	}
	if (bit_select(cmd, 5, 1)) {
		rs[i].state = SVGA3D_RS_DSTBLENDALPHA;
		rs[i++].uintValue = bit_select(cmd, 0, 4) + 1U;
	}
	if (i)
		m_provider->setRenderState(m_context_id, i, &rs[0]);
}

HIDDEN
void CLASS::ip_backface_stencil_ops(uint32_t cmd)
{
	SVGA3dRenderState rs[5];
	uint32_t i = 0U;
#if 0
	GLLog(3, "%s: 3DSTATE_BACKFACE_STENCIL_OPS SREn %u, SR %u, ST %u, SF %u, "
		  "SPZF %u, SPZP %u, STS %u\n", __FUNCTION__,
		  bit_select(cmd, 23, 1),
		  bit_select(cmd, 15, 8),
		  bit_select(cmd, 14, 1),
		  bit_select(cmd, 11, 3),
		  bit_select(cmd,  8, 3),
		  bit_select(cmd,  5, 3),
		  bit_select(cmd,  2, 3),
		  bit_select(cmd,  0, 2));
#endif
	if (bit_select(cmd, 1, 1)) {
		rs[i].state = SVGA3D_RS_STENCILENABLE2SIDED;
		rs[i++].uintValue = bit_select(cmd, 0, 1);
	}
	if (bit_select(cmd, 14, 1)) {
		rs[i].state = SVGA3D_RS_CCWSTENCILFUNC;
		rs[i++].uintValue = xlate_comparefunc(bit_select(cmd, 11, 3));
		rs[i].state = SVGA3D_RS_CCWSTENCILFAIL;
		rs[i++].uintValue = xlate_stencilop(bit_select(cmd, 8, 3));
		rs[i].state = SVGA3D_RS_CCWSTENCILZFAIL;
		rs[i++].uintValue = xlate_stencilop(bit_select(cmd, 5, 3));
		rs[i].state = SVGA3D_RS_CCWSTENCILPASS;
		rs[i++].uintValue = xlate_stencilop(bit_select(cmd, 2, 3));
	}
	if (i)
		m_provider->setRenderState(m_context_id, i, &rs[0]);
}

#ifdef PRINT_PS
HIDDEN
void CLASS::ip_print_ps(uint32_t const* p, uint32_t num_dwords)
{
	uint32_t i, num_inst = num_dwords / 3U;
	int np;
	char ps_mask_buf[6];
	char ps_src_reg_buf[3][32];
	if (!num_inst)
		return;
	GLLog(3, "%s: Pixel Shader Program\n", __FUNCTION__);
	for (i = 0U; i != num_inst; ++i, p += 3) {
		uint8_t opcode = p[0] >> 24;
		if (opcode <= 20U) {
			// Arithmetic
			np = ps_arith_num_params[opcode];
			if (np <= 0) {
				GLLog(3, "%s:   %s\n", __FUNCTION__, ps_arith_ops[opcode]);
				continue;
			}
			xlate_ps_arith_src_reg(&ps_src_reg_buf[0][0],
								   bit_select(p[0],  7,  3),
								   bit_select(p[0],  2,  4),
								   bit_select(p[1], 16, 16));
			if (np >= 2) {
				xlate_ps_arith_src_reg(&ps_src_reg_buf[1][0],
									   bit_select(p[1], 13, 3),
									   bit_select(p[1],  8, 4),
									   (bit_select(p[1], 0, 8) << 8) | bit_select(p[2], 24, 8));
			} else
				ps_src_reg_buf[1][0] = 0;
			if (np >= 3) {
				xlate_ps_arith_src_reg(&ps_src_reg_buf[2][0],
									   bit_select(p[2], 21,  3),
									   bit_select(p[2], 16,  4),
									   bit_select(p[2],  0, 16));
			} else
				ps_src_reg_buf[2][0] = 0;
			GLLog(3, "%s:   %s%s %s%u%s%s%s%s\n", __FUNCTION__,
				  ps_arith_ops[opcode],
				  bit_select(p[0], 22, 1) ? "_sat" : "",
				  xlate_ps_reg_type(bit_select(p[0], 19, 3)),
				  bit_select(p[0], 14, 4),
				  xlate_ps_mask(&ps_mask_buf[0], bit_select(p[0], 10, 4)),
				  &ps_src_reg_buf[0][0],
				  &ps_src_reg_buf[1][0],
				  &ps_src_reg_buf[2][0]);
		} else if (opcode <= 24U) {
			// Texture
			GLLog(3, "%s:   %s %s%u, %s%u, S%u\n", __FUNCTION__,
				  ps_texture_ops[opcode - 21U],
				  xlate_ps_reg_type(bit_select(p[0], 19, 3)),
				  bit_select(p[0], 14, 4),
				  xlate_ps_reg_type(bit_select(p[1], 24, 3)),
				  bit_select(p[1], 17, 4),
				  bit_select(p[0], 0, 4));
		} else if (opcode == 25U) {
			// DCL
			GLLog(3, "%s:   dcl%s %s%u%s\n", __FUNCTION__,
				  ps_sample_type[bit_select(p[0], 22, 2)],
				  xlate_ps_reg_type(bit_select(p[0], 19, 3)),
				  bit_select(p[0], 14, 4),
				  xlate_ps_mask(&ps_mask_buf[0], bit_select(p[0], 10, 4)));
		}
	}
}
#endif

HIDDEN
void CLASS::ip_select_and_load_ps(uint32_t* p, uint32_t cmd)
{
	/*
	 * Simplified shader support
	 */
#if 0
	uint32_t s4 = m_intel_state.imm_s[4];
#else
	uint32_t shader_id = cache_shader(p + 1, (cmd & 0xFFFFU) + 1U);
	if (shader_id == m_active_shid)
		return;
#endif
	SVGA3D* svga3d = m_provider->lock3D();
	if (!svga3d)
		return;
#if 0
	if (s4 & (1U << 11))	/* S4_VFMT_SPEC_FOG */
		svga3d->SetShader(m_context_id, SVGA3D_SHADERTYPE_PS, 3U);
	else if ((m_intel_state.imm_s[2] & 15U) != 15U) /* Tex1 + Diffuse */
		svga3d->SetShader(m_context_id, SVGA3D_SHADERTYPE_PS, 2U);
	else if (s4 & (1U << 10)) /* S4_VFMT_COLOR */
		svga3d->SetShader(m_context_id, SVGA3D_SHADERTYPE_PS, 1U);
	else
		svga3d->SetShader(m_context_id, SVGA3D_SHADERTYPE_PS, SVGA_ID_INVALID);
#else
	svga3d->SetShader(m_context_id, SVGA3D_SHADERTYPE_PS, shader_id);
	m_active_shid = shader_id;
	if (!isIdValid(shader_id)) {
		SVGA3dTextureState* ts;
		/*
		 * Set defaults for the Fixed-Function Pixel Pipeline
		 *
		 * According to Direct3D documentation, the default
		 * operations are
		 *   OC.rgb = Diffuse.rgb * T0.rgb
		 *   If no texture at stage 0,
		 *     OC.a = Diffuse.a
		 *   If texture at stage 0
		 *     OC.a = T0.a
		 * We leave it that way.
		 *
		 * We set the TransformFlags to Projective, since the GLD
		 *   passes down FLOAT4 projective tex coords.
		 */
		if (svga3d->BeginSetTextureState(m_context_id, &ts, 1U)) {
			ts->stage = 0U;
			ts->name = SVGA3D_TS_TEXTURETRANSFORMFLAGS;
			ts->value = SVGA3D_TEX_PROJECTED;
			svga3d->FIFOCommitAll();
		}
	}
#endif
	m_provider->unlock3D();
}

HIDDEN
void CLASS::ip_load_ps_const(uint32_t* p)
{
	uint32_t i, mask = p[1];
	SVGA3D* svga3d;
	if (!(mask & 0xFFFFU))
		return;
	svga3d = m_provider->lock3D();
	if (!svga3d)
		return;
	p += 2;
	for (i = 0U; i != 16U; ++i, mask >>= 1)
		if (mask & 1U) {
#if 0
			GLLog(3, "%s:   const %u == [%#x, %#x, %#x, %#x]\n", __FUNCTION__,
				  i, p[0], p[1], p[2], p[3]);
#endif
			svga3d->SetShaderConst(m_context_id, i, SVGA3D_SHADERTYPE_PS, SVGA3D_CONST_TYPE_FLOAT, p);
			p += 4;
		}
	m_provider->unlock3D();
}

HIDDEN
void CLASS::ip_buf_info(uint32_t* p)
{
	SVGA3dSurfaceImageId hostImage;
	uint8_t kind;
	SVGA3D* svga3d;
	kind = bit_select(p[1], 24, 3);
	if (kind != 3U && kind != 7U)
		return;
	hostImage.sid = p[2];
	hostImage.face = bit_select(p[1], 8, 8);
	hostImage.mipmap = bit_select(p[1], 0, 8);
#ifdef GL_DEV
	GLLog(3, "%s: kind == %u, face == %u, mipmap == %u, sid == %u\n", __FUNCTION__,
		  kind, hostImage.face, hostImage.mipmap, hostImage.sid);
#endif
	svga3d = m_provider->lock3D();
	if (!svga3d)
		return;
	/*
	 * Possibly should set stencil same as depth
	 */
	svga3d->SetRenderTarget(m_context_id, kind == 3U ? SVGA3D_RT_COLOR0 : SVGA3D_RT_DEPTH, &hostImage);
	m_provider->unlock3D();
}

HIDDEN
void CLASS::ip_draw_rect(uint32_t* p)
{
	SVGA3dRect rect;
	SVGA3D* svga3d;
	rect.x = p[1];
	rect.y = p[2];
	rect.w = p[3];
	rect.h = p[4];
#ifdef GL_DEV
	GLLog(3, "%s: [%u, %u, %u, %u]\n", __FUNCTION__, rect.x, rect.y, rect.w, rect.h);
#endif
	svga3d = m_provider->lock3D();
	if (!svga3d)
		return;
	svga3d->SetViewport(m_context_id, &rect);
	svga3d->SetZRange(m_context_id, 0.0F, 1.0F);
	m_provider->unlock3D();
}

HIDDEN
uint32_t CLASS::decode_mi(uint32_t* p, uint32_t cmd)
{
	uint32_t skip = 0U, fence_num;
	SVGA3D* svga3d;
	switch (bit_select(cmd, 23, 6)) {
		case  0U: /* MI_NOOP */
		case  2U: /* MI_USER_INTERRUPT */
		case  3U: /* MI_WAIT_FOR_EVENT */
		case  4U: /* MI_FLUSH */
		case  7U: /* MI_REPORT_HEAD */
		case  8U: /* MI_ARB_ON_OFF */
		case 10U: /* MI_BATCH_BUFFER_END */
			skip = 1U;
			break;
		case 20U: /* MI_DISPLAY_BUFFER_INFO */
		case 34U: /* MI_LOAD_REGISTER_IMM */
		case 36U: /* MI_STORE_REGISTER_MEM */
		case 48U: /* MI_BATCH_BUFFER */
			skip = 3U;
			break;
		case 17U: /* MI_OVERLAY_FLIP */
		case 18U: /* MI_LOAD_SCAN_LINES_INCL */
		case 19U: /* MI_LOAD_SCAN_LINES_EXCL */
		case 24U: /* MI_SET_CONTEXT */
		case 49U: /* MI_BATCH_BUFFER_START */
			skip = 2U;
			break;
		case 32U: /* MI_STORE_DATA_IMM */
			skip = (cmd & 0x3FU) + 2U;
			break;
		case 33U: /* MI_STORE_DATA_INDEX */
			skip = (cmd & 0x3FU) + 2U;
			if (p[1] != 64U)
				break;
			fence_num = p[2];
			if (fence_num * sizeof(GLDFence) >= m_fences_len)
				break;
			svga3d = m_provider->lock3D();
			if (!svga3d)
				break;
			m_fences_ptr[fence_num].u = svga3d->InsertFence();
			m_provider->unlock3D();
#if 0
			GLLog(3, "%s: setting fence %u to %u\n", __FUNCTION__,
				  fence_num, m_fences_ptr[fence_num].u);
#endif
			break;
	}
	return skip;
}

HIDDEN
uint32_t CLASS::decode_2d(uint32_t* p, uint32_t cmd)
{
	uint32_t skip = 0U;
	switch (bit_select(cmd, 22, 7)) {
		case    1U: /* XY_SETUP_BLT */
		case    3U: /* XY_SETUP_CLIP_BLT */
		case 0x11U: /* XY_SETUP_MONO_PATTERN_SL_BLT */
		case 0x24U: /* XY_PIXEL_BLT */
		case 0x25U: /* XY_SCANLINES_BLT */
		case 0x26U: /* Y_TEXT_BLT */
		case 0x31U: /* XY_TEXT_IMMEDIATE_BLT */
		case 0x40U: /* COLOR_BLT */
		case 0x50U: /* XY_COLOR_BLT */
		case 0x51U: /* XY_PAT_BLT */
		case 0x52U: /* XY_MONO_PAT_BLT */
		case 0x53U: /* XY_SRC_COPY_BLT */
		case 0x54U: /* XY_MONO_SRC_COPY_BLT */
		case 0x55U: /* XY_FULL_BLT */
		case 0x56U: /* XY_FULL_MONO_SRC_BLT */
		case 0x57U: /* XY_FULL_MONO_PATTERN_BLT */
		case 0x58U: /* XY_FULL_MONO_PATTERN_MONO_SRC_BLT */
		case 0x59U: /* XY_MONO_PAT_FIXED_BLT */
		case 0x71U: /* XY_MONO_SRC_COPY_IMMEDIATE_BLT */
		case 0x72U: /* XY_PAT_BLT_IMMEDIATE */
		case 0x75U: /* XY_FULL_MONO_SRC_IMMEDIATE_PATTERN_BLT */
		case 0x76U: /* XY_PAT_CHROMA_BLT */
		case 0x77U: /* XY_PAT_CHROMA_BLT_IMMEDIATE */
			skip = (cmd & 0xFFU) + 2U;
			break;
		case 0x43U: /* SRC_COPY_BLT */
#ifdef GL_DEV
			GLLog(3, "%s:   SRC_COPY_BLT\n", __FUNCTION__);
#endif
			skip = (cmd & 0xFFU) + 2U;
			break;
	}
	return skip;
}

HIDDEN
uint32_t CLASS::decode_3d_1d(uint32_t* p, uint32_t cmd)
{
	uint32_t skip = (cmd & 0xFFFFU) + 2U;
	switch (bit_select(cmd, 16, 8)) {
		case 0x00U: /* 3DSTATE_MAP_STATE */
			ip_3d_map_state(p);
			break;
		case 0x01U: /* 3DSTATE_SAMPLER_STATE */
			ip_3d_sampler_state(p);
			break;
		case 0x04U: /* 3DSTATE_LOAD_STATE_IMMEDIATE_1 */
			skip = ip_load_immediate(p, cmd);
			break;
		case 0x05U: /* 3DSTATE_PIXEL_SHADER_PROGRAM */
			ip_select_and_load_ps(p, cmd);
			break;
		case 0x06U: /* 3DSTATE_PIXEL_SHADER_CONSTANTS */
			ip_load_ps_const(p);
			break;
		case 0x80U: /* 3DSTATE_DRAW_RECT_CMD */
			ip_draw_rect(p);
			break;
		case 0x81U: /* 3DSTATE_SCISSOR_RECT_0_CMD */
			ip_misc_render_state(2U, p);
			break;
		case 0x83U: /* 3DSTATE_STIPPLE */
#if 0
			GLLog(3, "%s: 3DSTATE_STIPPLE En %u, %#x\n", __FUNCTION__,
				  bit_select(p[1], 16,  1),
				  bit_select(p[1],  0, 16));
#endif
			break;
		case 0x85U: /* 3DSTATE_DST_BUF_VARS_CMD */
#if 0
			if (cache_misc_reg(5U, p[1])) {
				GLLog(3, "%s: 3DSTATE_DST_BUF_VARS_CMD Upper %#x, DHB %u, DVB %u, "
					  "YUV %u, COLOR %u, DEPTH %u, VLS %u\n", __FUNCTION__,
					  bit_select(p[1], 24, 8),
					  bit_select(p[1], 20, 4),
					  bit_select(p[1], 16, 4),
					  bit_select(p[1], 12, 3),
					  bit_select(p[1],  8, 4),
					  bit_select(p[1],  2, 2),
					  bit_select(p[1],  0, 2));
			}
#endif
			break;
		case 0x88U: /* 3DSTATE_CONST_BLEND_COLOR_CMD */
			ip_misc_render_state(3U, p);
			break;
		case 0x89U: /* 3DSTATE_FOG_MODE_CMD */
#ifdef GL_DEV
			GLLog(3, "%s: 3DSTATE_FOG_MODE_CMD FFMEn %u, FF %u, FIMEn %u, FI %u, "
				  "C1C2MEn %u, DMEn %u, C1 %u, C2 %u, D1 %u\n", __FUNCTION__,
				  bit_select(p[1], 31, 1),
				  bit_select(p[1], 28, 2),
				  bit_select(p[1], 27, 1),
				  bit_select(p[1], 25, 1),
				  bit_select(p[1], 24, 1),
				  bit_select(p[1], 23, 1),
				  bit_select(p[1],  4, 16),
				  bit_select(p[2], 16, 1),
				  bit_select(p[3], 16, 1));
#endif
			break;
		case 0x8EU: /* 3DSTATE_BUF_INFO_CMD */
			ip_buf_info(p);
			break;
		case 0x97U: /* 3DSTATE_DEPTH_OFFSET_SCALE */
			ip_misc_render_state(0U, p);
			break;
		case 0x9CU: /* 3DSTATE_CLEAR_PARAMETERS */
			skip = ip_clear_params(p, cmd);
			break;
		default:
			GLLog(1, "%s:   Unknown cmd %#x\n", __FUNCTION__, cmd);
			break;
	}
	return skip;
}

HIDDEN
uint32_t CLASS::decode_3d(uint32_t* p, uint32_t cmd)
{
	uint32_t skip = 0U;
	switch (bit_select(cmd, 24, 5)) {
		case 0x06U: /* 3DSTATE_AA_CMD */
#ifdef GL_DEV
			if (cache_misc_reg(0U, cmd & 0xFFFFFFU)) {
				GLLog(3, "%s: 3DSTATE_AA_CMD EWEn %u, EW %u, RWEn %u, RW %u\n", __FUNCTION__,
					  bit_select(cmd, 16, 1),
					  bit_select(cmd, 14, 2),
					  bit_select(cmd,  8, 1),
					  bit_select(cmd,  6, 2));
			}
#endif
			skip = 1U;
			break;
		case 0x07U: /* 3DSTATE_RASTER_RULES_CMD */
#ifdef GL_DEV
			GLLog(3, "%s: 3DSTATE_RASTER_RULES_CMD %#x\n", __FUNCTION__, cmd & 0xFFFFFFU);
#endif
			skip = 1U;
			break;
		case 0x08U: /* 3DSTATE_BACKFACE_STENCIL_OPS */
			if (cache_misc_reg(1U, cmd & 0xFFFFFFU))
				ip_backface_stencil_ops(cmd);
			skip = 1U;
			break;
		case 0x09U: /* 3DSTATE_BACKFACE_STENCIL_MASKS */
#if 0
			if (cache_misc_reg(2U, cmd & 0xFFFFFFU)) {
				GLLog(3, "%s: 3DSTATE_BACKFACE_STENCIL_MASKS STEn %u, SWEn %u, STM %u, SWM %u\n", __FUNCTION__,
					  bit_select(cmd, 17, 1),
					  bit_select(cmd, 16, 1),
					  bit_select(cmd,  8, 8),
					  bit_select(cmd,  8, 8));
			}
#endif
			skip = 1U;
			break;
		case 0x0BU: /* 3DSTATE_INDEPENDENT_ALPHA_BLEND_CMD */
			if (cache_misc_reg(3U, cmd & 0xFFFFFFU))
				ip_independent_alpha_blend(cmd);
			skip = 1U;
			break;
		case 0x0CU: /* 3DSTATE_MODES_5_CMD */
#if 0
			GLLog(3, "%s:   modes5 FRC %u, FTC %u\n", __FUNCTION__,
				  bit_select(cmd, 18, 1),
				  bit_select(cmd, 16, 1));
#endif
			skip = 1U;
			break;
		case 0x0DU: /* 3DSTATE_MODES_4_CMD */
			if (cache_misc_reg(4U, cmd & 0xFFFFFFU))
				ip_misc_render_state(4U, p);
			skip = 1U;
			break;
		case 0x15U: /* 3DSTATE_FOG_COLOR_CMD */
#ifdef GL_DEV
			GLLog(3, "%s: 3DSTATE_FOG_COLOR_CMD %#x\n", __FUNCTION__, cmd & 0xFFFFFFU);
#endif
			skip = 1U;
			break;
		case 0x1CU:
			switch (bit_select(cmd, 16, 8)) {
				case 0x80U: /* 3DSTATE_SCISSOR_ENABLE */
					ip_misc_render_state(1U, p);
					skip = 1U;
					break;
				case 0x88U: /* 3DSTATE_DEPTH_SUBRECT_DISABLE */
#ifdef GL_DEV
					GLLog(3, "%s: 3DSTATE_DEPTH_SUBRECT_DISABLE %#x\n", __FUNCTION__, cmd & 0xFFFFFFU);
#endif
					skip = 1U;
					break;
			}
			break;
		case 0x1DU:
			skip = decode_3d_1d(p, cmd);
			break;
		case 0x1FU: /* PRIM3D */
			skip = ip_prim3d(p, cmd);
			break;
	}
	return skip;
}

HIDDEN
uint32_t CLASS::submit_buffer(uint32_t* kernel_buffer_ptr, uint32_t size_dwords)
{
	uint32_t *p, *limit, cmd, skip;
#if 0
	GLLog(3, "%s:   offset %d, size %u [in dwords]\n", __FUNCTION__,
		  static_cast<int>(kernel_buffer_ptr - &m_command_buffer.kernel_ptr->downstream[0]),
		  size_dwords);
#endif
	p = kernel_buffer_ptr;
	limit = p + size_dwords;
	for (; p < limit; p += skip) {
		cmd = *p;
		skip = 0U;
		switch (cmd >> 29) {
			case 0U:
				skip = decode_mi(p, cmd);
				break;
			case 2U:
				skip = decode_2d(p, cmd);
				break;
			case 3U:
				skip = decode_3d(p, cmd);
				break;
		}
		if (!skip) {
			GLLog(1, "%s:   Unknown cmd %#x\n", __FUNCTION__, cmd);
			skip = 1U;
		}
	}
	/*
	 * Note: original inserts a fence and returns the fence
	 *   should probably do the same for finish()
	 */
	return 0U;
}
