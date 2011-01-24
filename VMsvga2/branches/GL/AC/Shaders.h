/*
 *  Shaders.h
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on January 11th 2011.
 *  Copyright 2011 Zenith432. All rights reserved.
 *
 */

#ifndef __SHADERS_H__
#define __SHADERS_H__

#define NUM_FIXED_SHADERS 17U

#ifdef __cplusplus
extern "C" {
#endif

struct FixedShaderDescriptor
{
	unsigned long long hash[2];
	unsigned const* bytecode;
	unsigned length;
	unsigned tc2s_map;
	unsigned char tc2s_map_valids;
};

extern struct FixedShaderDescriptor const g_fixed_shaders[];

#ifdef __cplusplus
}
#endif

#endif /* __SHADERS_H__ */
