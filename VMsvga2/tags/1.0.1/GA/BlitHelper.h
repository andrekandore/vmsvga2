/*
 *  BlitHelper.h
 *  VMsvga2GA
 *
 *  Created by Zenith432 on August 12th 2009.
 *  Copyright 2009 Zenith432. All rights reserved.
 *
 */

#ifndef __BLITHELPER_H__
#define __BLITHELPER_H__

#ifdef __cplusplus
extern "C" {
#endif

IOReturn useAccelUpdates(io_connect_t context, int state);
IOReturn RectCopy(io_connect_t context, void const* copyRects, UInt32 copyRectsSize);
IOReturn RectFill(io_connect_t context, UInt32 color, void const* rects, UInt32 rectsSize);
IOReturn UpdateFramebuffer(io_connect_t context, UInt32 const* rect);
IOReturn SyncFIFO(io_connect_t context);
IOReturn RegionCopy(io_connect_t context, UInt32 destX, UInt32 destY, void const* region, UInt32 regionSize);

#ifdef __cplusplus
}
#endif

#endif /* __BLITHELPER_H__ */
