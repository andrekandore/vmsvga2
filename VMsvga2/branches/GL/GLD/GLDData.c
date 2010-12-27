/*
 *  GLDData.c
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

#include <IOKit/IOKitLib.h>
#include "EntryPointNames.h"
#include "GLDTypes.h"
#include "GLDData.h"

static __attribute__((used)) char const copyright[] = "Copyright 2009-2010 Zenith432";

#pragma GCC visibility push(hidden)

char const BNDL1[] = "/System/Library/Extensions/AppleIntelGMA950GLDriver.bundle/Contents/MacOS/AppleIntelGMA950GLDriver";
char const BNDL2[] = "/System/Library/Frameworks/OpenGL.framework/Resources/GLRendererFloat.bundle/GLRendererFloat";
char const LIBGLIMAGE[] = "/System/Library/Frameworks/OpenGL.framework/Versions/A/Libraries/libGLImage.dylib";
char const LIBGLPROGRAMMABILITY[] = "/System/Library/Frameworks/OpenGL.framework/Versions/A/Libraries/libGLProgrammability.dylib";

void* bndl_handle[2];

GLD_GENERIC_FUNC bndl_ptrs[2][NUM_ENTRIES];

GLD_GENERIC_DISPATCH remote_dispatch[33];

#if LOGGING_LEVEL >= 1
int logLevel = LOGGING_LEVEL;
#endif

int bndl_index;

glr_io_data_t glr_io_data;

libglimage_t libglimage;

libglprogrammability_t libglprogrammability;

#pragma GCC visibility pop
