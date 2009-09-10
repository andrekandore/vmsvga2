/*
 *  VMsvga2Accel.h
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on July 29th 2009.
 *  Copyright 2009 Zenith432. All rights reserved.
 *
 */

#ifndef __VMSVGA2ACCEL_H__
#define __VMSVGA2ACCEL_H__

#include <IOKit/graphics/IOAccelerator.h>
#include "SVGA3D.h"

class VMsvga2Accel : public IOAccelerator
{
	OSDeclareDefaultStructors(VMsvga2Accel);

private:
	SVGA3D svga3d;
	class IOPCIDevice* m_provider;
	class VMsvga2* m_framebuffer;
	class SVGADevice* m_svga;
	UInt32 m_auto_sync_update_count;
	UInt32 m_last_present_fence;
	unsigned bHaveSVGA3D:1;

#ifdef TIMING
	void timeSyncs();
#endif

public:
	bool init(OSDictionary* dictionary = 0);
	bool start(IOService* provider);
	void stop(IOService* provider);
	void free();
	IOReturn newUserClient(task_t owningTask, void* securityID, UInt32 type, IOUserClient ** handler);

	IOReturn useAccelUpdates(UInt32 state);
	IOReturn RectCopy(struct IOBlitCopyRectangleStruct const* copyRects, UInt32 copyRectsSize);
	IOReturn RectFill(UInt32 color, struct IOBlitRectangleStruct const* rects, UInt32 rectsSize);
	IOReturn UpdateFramebuffer(UInt32 const* rect);	// rect is an array of 4 UInt32 - x, y, width, height
	IOReturn UpdateFramebufferAutoSync(UInt32 const* rect);	// rect same as above
	IOReturn SyncFIFO();
	IOReturn RegionCopy(UInt32 destX, UInt32 destY, void /* IOAccelDeviceRegion */ const* region, UInt32 regionSize);

	struct ExtraInfo {
		UInt32 mem_offset_in_bar1;
		UInt32 mem_pitch;
		SInt32 srcDeltaX;
		SInt32 srcDeltaY;
		SInt32 dstDeltaX;
		SInt32 dstDeltaY;
	};
	bool getScreenInfo(IOAccelSurfaceReadData* info);
	bool createSurface(UInt32 sid, SVGA3dSurfaceFormat surfaceFormat, UInt32 width, UInt32 height);
	bool destroySurface(UInt32 sid);
	IOMemoryMap* allocBackingForTask(task_t task, UInt32 offset_in_bar1, UInt32 size);
	bool surfaceDMA2D(UInt32 sid, SVGA3dTransferType transfer, void /* IOAccelDeviceRegion */ const* region, ExtraInfo const* extra);
	bool surfacePresentAutoSync(UInt32 sid, void /* IOAccelDeviceRegion */ const* region, ExtraInfo const* extra);
	bool surfacePresentReadback(void /* IOAccelDeviceRegion */ const* region);
	bool setupRenderContext(UInt32 cid, UInt32 color_sid, UInt32 depth_sid, UInt32 width, UInt32 height);
	bool clearContext(UInt32 cid, SVGA3dClearFlag flags, UInt32 width, UInt32 height, UInt32 color, float depth, UInt32 stencil);
	bool createContext(UInt32 cid);
	bool destroyContext(UInt32 cid);
};

#endif /* __VMSVGA2ACCEL_H__ */
