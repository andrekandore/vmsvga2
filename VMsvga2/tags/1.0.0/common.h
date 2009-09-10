/*
 *  common.h
 *  VMsvga2
 *
 *  Created by Zenith432 on July 4th 2009.
 *  Copyright 2009 Zenith432. All rights reserved.
 *
 */

/**********************************************************
 * Portions Copyright 2009 VMware, Inc.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **********************************************************/

#ifndef _VMSVGA2_COMMON_H_
#define _VMSVGA2_COMMON_H_

#define NUM_DISPLAY_MODES 14
#define CUSTOM_MODE_ID 1

#define GUEST_OS_LINUX 0x5008U

#define VMW_OPTION_FIFO_INIT			0x1
#define VMW_OPTION_TIMER				0x2
#define VMW_OPTION_REG_DUMP				0x4
#define VMW_OPTION_LINUX				0x8
#define VMW_OPTION_CURSOR_BYPASS_2		0x10

__BEGIN_DECLS

typedef SInt32 VMFBIOLog;

struct DisplayModeEntry
{
	SInt32 mode_id;
	UInt32 width;
	UInt32 height;
	UInt32 flags;
};

struct CustomModeData
{
	UInt32 flags;
	UInt32 width;
	UInt32 height;
};

extern DisplayModeEntry const modeList[NUM_DISPLAY_MODES];

extern DisplayModeEntry customMode;

extern UInt32 vmw_options;

static inline bool checkOption(UInt32 mask)
{
	return (vmw_options & mask) != 0;
}

SInt8 VMLog_SendString(char const* str);

__END_DECLS

#endif /* _VMSVGA2_COMMON_H_ */
