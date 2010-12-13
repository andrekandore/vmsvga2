/*
 *  UCTypes.h
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on December 11th 2010.
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

#ifndef __UCTYPES_H__
#define __UCTYPES_H__

#ifdef __cplusplus
extern "C" {
#endif

struct VendorNewTextureDataStruc
{
	uint32_t type;		// offset 0x00
	uint8_t  version;	// offset 0x04
	uint8_t  flags[2];	// offset 0x05
	uint8_t  bytespp;	// offset 0x07
	uint32_t width;		// offset 0x08
	uint16_t height;	// offset 0x0C
	uint16_t depth;		// offset 0x0E
	uint32_t f1;		// offset 0x10
	uint32_t pitch;		// offset 0x14
	uint32_t f3[4];		// offset 0x18
	uint32_t size;		// offset 0x28
	uint32_t f4;		// offset 0x2C
	uint64_t pixels[3]; // offset 0x30
						// ends   0x48
};

struct sIONewTextureReturnData
{
	uint32_t pad;
	uint64_t data;
	uint64_t addr;
} __attribute__((packed));

struct sIODevicePageoffTexture
{
	uint32_t data[6];	// Note: only 2 elements are used
};

struct sIODeviceChannelMemoryData
{
	mach_vm_address_t addr;
};

enum eIOGLContextModeBits {
	CGLCMB_Stereoscopic      = 0x00000010U,
	CGLCMB_Windowed          = 0x00000020U,
	CGLCMB_DepthMode0        = 0x00000040U,
	CGLCMB_StencilMode0      = 0x00000080U,
	CGLCMB_AuxBuffersMask    = 0x00000300U,
	CGLCMB_DoubleBuffer      = 0x00000400U,
	CGLCMB_FullScreen        = 0x00000800U,
	CGLCMB_HaveSampleBuffers = 0x00001000U,
	CGLCMB_AuxDepthStencil   = 0x00002000U,
	CGLCMB_BeamSync          = 0x00008000U,
	CGLCMB_BackingStore      = 0x00800000U,
	CGLCMB_DepthMode16       = 0x01000000U,
	CGLCMB_DepthMode32       = 0x02000000U,
	CGLCMB_SampleBuffersMask = 0x0C000000U
};

struct sIOGLContextReadBufferData
{
	uint32_t data[8];		// Note: only uses four
};

struct sIOGLContextSetSurfaceData {
	uint32_t surface_id;
	uint32_t context_mode_bits;
	uint32_t surface_mode;
	uint32_t dr_options_hi;		// high byte of options passed to gldAttachDrawable
	uint32_t dr_options_lo;		// low byte of options passed to gldAttachDrawable
	uint32_t volatile_state;
	uint32_t set_scale;
	uint32_t scale_options;
	uint32_t scale_width;		// lower 16 bits
	uint32_t scale_height;		// lower 16 bits
};

struct sIOGLContextGetConfigStatus {
	uint32_t config[3];
	uint32_t inner_width;
	uint32_t inner_height;
	uint32_t outer_width;
	uint32_t outer_height;
	uint32_t status;		// boolean 0 or 1
	uint32_t surface_mode_bits;
	uint32_t reserved;
};

struct sIOGLContextGetDataBuffer
{
	uint32_t len;
	mach_vm_address_t addr;
} __attribute__((packed));

struct sIOGLGetCommandBuffer {
	uint32_t len[2];
	mach_vm_address_t addr[2];
};

struct NvNotificationRec
{
	uint32_t data[4];
};

struct GLDSysObject {
	uint32_t object_id;		//  0
	uint32_t f0;			//  4
	int32_t volatile stamps[2];
							//  8
	int32_t volatile refcount;
							// 10
	uint8_t volatile in_use;
							// 14
	uint8_t f1;				// 15
	uint8_t type;			// 16
	uint32_t f2;			// 18
	uint16_t f3[12];		// 1C
	uint32_t f4;			// 34
							// 38
};

#ifdef __cplusplus
}
#endif

#endif /* __UCTYPES_H__ */
