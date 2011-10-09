/*
 *  DevCaps.c
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on October 9th 2011.
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

#include <IOKit/IOLib.h>
#include "DevCaps.h"
#include "VLog.h"

#include "svga_apple_header.h"
#include "svga3d_reg.h"
#include "svga_apple_footer.h"

#define HIDDEN __attribute__((visibility("hidden")))

#if LOGGING_LEVEL >= 1
#define Log(...) VLog("DevCaps: ", ##__VA_ARGS__)
#else
#define Log(...)
#endif

#pragma mark -
#pragma mark GPU Capability Constants
#pragma mark -

static
char const* const cap_names[] =
{
	0, "Max Lights", "Max FFP Texture Stages", "Max Clip Planes",
	"Vertex Shader Version", "Vertex Shaders",
	"Pixel Shader Version", "Pixel Shaders",
	"Max Render Targets", "S23E8 Textures",
	"S10E5 Textures", "Max Fixed VertexBlend",
	0, 0, 0, "Occlusion Query",
	"Texture Gradient Sampling", "Max Point Size",
	"Max Pixel Shader Textures", "Max Texture Width",
	"Max Texture Height", "Max Volume Extent",
	"Max Texture Repeat", "Max Texture Aspect Ratio",
	"Max Texture Anisotropy", "Max Primitive Count",
	"Max Vertex Index", "Max Vertex Shader Instructions",
	"Max Pixel Shader Instructions", "Max Vertex Shader Temps",
	"Max Pixel Shader Temps", "Texture Ops",
	"X8R8G8B8", "A8R8G8B8", "A2R10G10B10", "X1R5G5B5",
	"A1R5G5B5", "A4R4G4B4", "R5G6B5", "L16",
	"L8A8", "A8", "L8", "Z_D16",
	"Z_D24S8", "Z_D24X8", "DXT1", "DXT2",
	"DXT3", "DXT4", "DXT5", "BUMPX8L8V8U8",
	"A2W10V10U10", "BUMPU8V8", "Q8W8V8U8", "CxV8U8",
	"R_S10E5", "R_S23E8", "RG_S10E5", "RG_S23E8",
	"ARGB_S10E5", "ARGB_S23E8", 0, "Max Vertex Shader Textures",
	"Max Simultaneous Render Targets", "V16U16",
	"G16R16", "A16B16G16R16", "UYVY", "YUY2",
	"Nonmaskable Samples", "Maskable Samples",
	"AlphaToCoverage", "SuperSample",
	"AutoGenMipmaps", "NV12", "AYUV", "Max Context IDs",
	"Max Surface IDs", "Z_DF16", "Z_DF24", "Z_D24S8_INT",
	"BC4_UNORM", "BC5_UNORM"
};

static
uint8_t const cap_types[] =
{
	0, 1, 1, 1,
	4, 0, 5, 0,
	1, 0, 0, 1,
	0, 0, 0, 0,
	0, 3, 1, 1,
	1, 1, 1, 1,
	1, 1, 1, 2,
	2, 1, 1, 6,
	7, 7, 7, 7,
	7, 7, 7, 7,
	7, 7, 7, 7,
	7, 7, 7, 7,
	7, 7, 7, 7,
	7, 7, 7, 7,
	7, 7, 7, 7,
	7, 7, 1, 1,
	1, 7, 7, 7,
	7, 7, 1, 1,
	1, 1, 1, 7,
	7, 1, 1, 7,
	7, 7, 7, 7
};

static
char const* const cap_psver[] =
{
	"Enabled", "ps_1_1", "ps_1_2", "ps_1_3", "ps_1_4", "ps_2_0", "ps_3_0", "ps_4_0"
};

static
char const* const cap_vsver[] =
{
	"Enabled", "vs_1_1", "vs_2_0", "vs_3_0", "vs_4_0"
};

static
char const* const cap_sf_names[] =
{
	"Texture", "VolumeTexture", "CubeTexture", "OffscreenRenderTarget",
	"SameFormatRenderTarget", 0, "ZStencil", "ZStencilArbitraryDepth",
	"SameFormatUpToAlpha", 0, "DisplayMode", "3DAcceleration",
	"PixelSize", "ConvertToARGB", "OffscreenPlain", "SRGBRead",
	"BumpMap", "DMap", "NoFilter", "MemberOfGroupARGB",
	"SRGBWrite", "NoAlphaBlend", "AutoGenMipMap", "VertexTexture",
	"NoTexCoordWrapNorMip"
};

#pragma mark -
#pragma mark GPU Capability Helpers
#pragma mark -

static
char const* cap_ps_version(uint8_t num)
{
	if ((num & 1U) && ((num >> 1) <= 7U))
		return cap_psver[num >> 1];
	return "None";
}

static
char const* cap_vs_version(uint8_t num)
{
	if ((num & 1U) && ((num >> 1) <= 4U))
		return cap_vsver[num >> 1];
	return "None";
}

static
void dumpSurfaceCaps(uint32_t caps)
{
	int i;

	for (i = 0; i != 25; ++i)
		if (caps & (1U << i)) {
			if (cap_sf_names[i])
				Log("    %s\n", cap_sf_names[i]);
			else
				Log("    Unknown %#x\n", (1U << i));
		}
}

#pragma mark -
#pragma mark Public Functions
#pragma mark -

HIDDEN
uint32_t* allocGPUCaps(void)
{
	uint32_t* p = (uint32_t*) IOMalloc(SVGA3D_DEVCAP_MAX * sizeof(uint32_t));
	if (p)
		bzero(p, SVGA3D_DEVCAP_MAX * sizeof(uint32_t));
	return p;
}

HIDDEN
void freeGPUCaps(uint32_t* accelBlock)
{
	if (!accelBlock)
		return;
	IOFree(accelBlock, SVGA3D_DEVCAP_MAX * sizeof(uint32_t));
}

HIDDEN
void dumpGPUCaps(uint32_t* const accelBlock)
{
	uint32_t c;
	SVGA3dDevCapResult const* v;

	Log("GPU Feature Detection\n");
	for (c = 1U; c < SVGA3D_DEVCAP_MAX; ++c) {
		switch (c) {
			case 12U:
			case 13U:
			case 14U:
			case 62U:
				continue;
		}
		v = (typeof(v)) &accelBlock[c];
		if (c >= sizeof cap_types) {
			Log("  DevCap %u == %#x\n", c, v->u);
			continue;
		}
		switch (cap_types[c]) {
			case 0U:
				Log("  %s %s\n", cap_names[c], (v->b & 1U) ? "Yes" : "No");
				break;
			case 1U:
				Log("  %s %u\n", cap_names[c], v->u);
				break;
			case 2U:
				Log("  %s %d\n", cap_names[c], v->i);
				break;
			case 3U:
				Log("  %s %d\n", cap_names[c], (int) v->f);
				break;
			case 4U:
				Log("  %s %s\n", cap_names[c], cap_vs_version(v->u));
				break;
			case 5U:
				Log("  %s %s\n", cap_names[c], cap_ps_version(v->u));
				break;
			case 6U:	// Texture Ops TBD
				Log("  %s %#x\n", cap_names[c], v->u);
				break;
			case 7U:
				if (v->u & 0x1FFFDDFU) {
					Log("  %s Caps\n", cap_names[c]);
					dumpSurfaceCaps(v->u);
				}
				break;
		}
	}
}

HIDDEN
void getGPUCaps(uint32_t const* deviceBlock, uint32_t* accelBlock)
{
	uint32_t offset, i, len, c;

	if (!deviceBlock)
		return;
	for (offset = 0U; (len = deviceBlock[offset]); offset += len) {
		if (deviceBlock[offset + 1U] != 0x100U /* SVGA3DCAPS_RECORD_DEVCAPS */)
			continue;
		for (i = 2U; i < len; i += 2U) {
			c = deviceBlock[offset + i];
			if (c < SVGA3D_DEVCAP_MAX)
				accelBlock[c] = deviceBlock[offset + i + 1U];
		}
	}
}
