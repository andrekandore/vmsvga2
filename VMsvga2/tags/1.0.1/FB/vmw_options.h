/*
 *  vmw_options.h
 *  VMsvga2
 *
 *  Created by Zenith432 on August 20th 2009.
 *  Copyright 2009 Zenith432. All rights reserved.
 *
 */

#ifndef __VMW_OPTIONS_H__
#define __VMW_OPTIONS_H__

#define VMW_OPTION_FIFO_INIT			0x1
#define VMW_OPTION_REFRESH_TIMER		0x2
#define VMW_OPTION_ACCEL				0x4
#define VMW_OPTION_CURSOR_BYPASS_2		0x8
#define VMW_OPTION_REG_DUMP				0x10
#define VMW_OPTION_LINUX				0x20

#ifdef __cplusplus
extern "C" {
#endif

extern UInt32 vmw_options;

static inline bool checkOptionFB(UInt32 mask)
{
	return (vmw_options & mask) != 0;
}

#ifdef __cplusplus
}
#endif

#endif /* __VMW_OPTIONS_H__ */
