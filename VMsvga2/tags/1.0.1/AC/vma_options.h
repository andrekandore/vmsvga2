/*
 *  vma_options.h
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on August 18th 2009.
 *  Copyright 2009 Zenith432. All rights reserved.
 *
 */

#ifndef __VMA_OPTIONS_H__
#define __VMA_OPTIONS_H__

#define VMA_OPTION_SURFACE_CONNECT		0x0001
#define VMA_OPTION_2D_CONTEXT			0x0002
#define VMA_OPTION_REGION_BOUNDS_COPY	0x0004

#define VMA_OPTION_PACKED_BACKING		0x0010
#define VMA_OPTION_USE_PRESENT			0x0020
#define VMA_OPTION_USE_READBACK			0x0040
#define VMA_OPTION_TEST					0x0080
#define VMA_OPTION_TEST_SMALL			0x0100
#define VMA_OPTION_TEST_PRESENT			0x0200

#ifdef __cplusplus
extern "C" {
#endif

extern UInt32 vma_options;

static inline bool checkOptionAC(UInt32 mask)
{
	return (vma_options & mask) != 0;
}
	
#ifdef __cplusplus
}
#endif

#endif /* __VMA_OPTIONS_H__ */
