/*
 *  BlitHelper.cpp
 *  VMsvga2GA
 *
 *  Created by Zenith432 on August 12th 2009.
 *  Copyright 2009 Zenith432. All rights reserved.
 *
 */

#include <IOKit/IOKitLib.h>
#include "BlitHelper.h"

IOReturn useAccelUpdates(io_connect_t context, int state)
{
	uint64_t input;

	input = state ? 1 : 0;
	return IOConnectCallMethod(context,
							   22 /* useAccelUpdates */,
							   &input, 1,
							   0, 0,
							   0, 0,
							   0, 0);
}

IOReturn RectCopy(io_connect_t context, void const* copyRects, UInt32 copyRectsSize)
{
	return IOConnectCallMethod(context,
							   23 /* RectCopy */,
							   0, 0,
							   copyRects, copyRectsSize,
							   0, 0,
							   0, 0);
}

IOReturn RectFill(io_connect_t context, UInt32 color, void const* rects, UInt32 rectsSize)
{
	uint64_t input;

	input = color;
	return IOConnectCallMethod(context,
							   24 /* RectFill */,
							   &input, 1,
							   rects, rectsSize,
							   0, 0,
							   0, 0);
}

IOReturn UpdateFramebuffer(io_connect_t context, UInt32 const* rect)
{
	return IOConnectCallMethod(context,
							   25 /* UpdateFramebuffer */,
							   0, 0,
							   rect, 4 * sizeof(UInt32),
							   0, 0,
							   0, 0);
}

IOReturn SyncFIFO(io_connect_t context)
{
	return IOConnectCallMethod(context,
							   26 /* SyncFIFO */,
							   0, 0,
							   0, 0,
							   0, 0,
							   0, 0);
}

IOReturn RegionCopy(io_connect_t context, UInt32 destX, UInt32 destY, void const* region, UInt32 regionSize)
{
	uint64_t input[2];

	input[0] = destX;
	input[1] = destY;
	return IOConnectCallMethod(context,
							   27 /* RegionCopy */,
							   &input[0], 2,
							   region, regionSize,
							   0, 0,
							   0, 0);
}
