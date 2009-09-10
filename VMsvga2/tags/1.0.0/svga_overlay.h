/**********************************************************
 * Copyright 2007-2009 VMware, Inc.  All rights reserved.
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

/*
 * svga_overlay.h --
 *
 *    Definitions for video-overlay support.
 */

#ifndef _SVGA_OVERLAY_H_
#define _SVGA_OVERLAY_H_

/*
 * Video formats we support
 */

#define VMWARE_FOURCC_YV12 0x32315659U // 'Y' 'V' '1' '2'
#define VMWARE_FOURCC_YUY2 0x32595559U // 'Y' 'U' 'Y' '2'
#define VMWARE_FOURCC_UYVY 0x59565955U // 'U' 'Y' 'V' 'Y'

#define SVGA_VIDEO_COLORKEY_MASK             0x00ffffffU

#define SVGA_ESCAPE_NSID_VMWARE				 0x00000000U

#define SVGA_ESCAPE_VMWARE_VIDEO             0x00020000U

#define SVGA_ESCAPE_VMWARE_VIDEO_SET_REGS    0x00020001U
        /* FIFO escape layout:
         * Type, Stream Id, (Register Id, Value) pairs */

#define SVGA_ESCAPE_VMWARE_VIDEO_FLUSH       0x00020002U
        /* FIFO escape layout:
         * Type, Stream Id */

struct SVGAEscapeVideoSetRegs {
   struct {
      UInt32 cmdType;
      UInt32 streamId;
   } header;

   // May include zero or more items.
   struct {
      UInt32 registerId;
      UInt32 value;
   } items[1];
};

struct SVGAEscapeVideoFlush {
   UInt32 cmdType;
   UInt32 streamId;
};

#endif // _SVGA_OVERLAY_H_
