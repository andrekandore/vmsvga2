/*
 *  GLDData.h
 *  VMsvga2GLDriver
 *
 *  Created by Zenith432 on December 5th 2010.
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

#ifndef __GLDDATA_H__
#define __GLDDATA_H__

#ifdef __cplusplus
extern "C" {
#endif

extern char const BNDL1[];
extern char const BNDL2[];
extern char const LIBGLIMAGE[];

extern void* bndl_handle[2];

extern GLD_GENERIC_FUNC bndl_ptrs[2][NUM_ENTRIES];

extern int logLevel;

extern int bndl_index;

extern glr_io_data_t glr_io_data;

extern libglimage_t libglimage;

#ifdef __cplusplus
}
#endif

#endif /* __GLDDATA_H__ */
