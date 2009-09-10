/*
 *  VMsvga2Surface.h
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on July 29th 2009.
 *  Copyright 2009 Zenith432. All rights reserved.
 *
 */

#ifndef __VMSVGA2SURFACE_H__
#define __VMSVGA2SURFACE_H__

#include <IOKit/IOUserClient.h>
#include <IOKit/graphics/IOAccelSurfaceConnect.h>

class VMsvga2Surface: public IOUserClient
{
	OSDeclareDefaultStructors(VMsvga2Surface);

private:
	task_t m_owning_task;
	class VMsvga2Accel* m_provider;
	IOExternalMethod* m_funcs_cache;

	unsigned bContextCreated:1;
	UInt32 color_sid;
	SVGA3dSurfaceFormat surfaceFormat;
	IOMemoryMap* m_backing;
	OSData* m_last_shape;
	IOAccelDeviceRegion const* m_last_region;
	UInt32 m_last_region_pitch;
	IOAccelSurfaceReadData screenInfo;

	void VLog(char const* fmt, ...);
#ifdef TESTING
	void runTest();
#endif

	IOReturn set_shape_backing_length_ext(eIOAccelSurfaceShapeBits options,
										  UInt32 framebufferIndex,
										  IOVirtualAddress backing,
										  UInt32 rowbytes,
										  IOAccelDeviceRegion* rgn,
										  UInt32 rgnSize,
										  UInt32 backingLength);
	void clearLastShape();

public:
	IOExternalMethod* getTargetAndMethodForIndex(IOService** targetP, UInt32 index);
	IOReturn clientClose();
	bool start(IOService* provider);
	void stop(IOService* provider);
	bool initWithTask(task_t owningTask, void* securityToken, UInt32 type);
	static VMsvga2Surface* withTask(task_t owningTask, void* securityToken, UInt32 type);

	IOReturn surface_read_lock_options(IOOptionBits options, IOAccelSurfaceInformation* info, UInt32* infoSize);
	IOReturn surface_read_unlock_options(IOOptionBits options);
	IOReturn get_state(eIOAccelSurfaceStateBits* state);	// not called
	IOReturn surface_write_lock_options(IOOptionBits options, IOAccelSurfaceInformation* info, UInt32* infoSize);
	IOReturn surface_write_unlock_options(IOOptionBits options);
	IOReturn surface_read(IOAccelSurfaceReadData* parameters, UInt32 parametersSize);
	IOReturn set_shape_backing(eIOAccelSurfaceShapeBits options,
							   UInt32 framebufferIndex,
							   IOVirtualAddress backing,
							   UInt32 rowbytes,
							   IOAccelDeviceRegion* rgn,
							   UInt32 rgnSize);	// not called
	IOReturn set_id_mode(UInt32 wID, eIOAccelSurfaceModeBits modebits);
	IOReturn set_scale(IOOptionBits options, IOAccelSurfaceScaling* scaling, UInt32 scalingSize);
	IOReturn set_shape(eIOAccelSurfaceShapeBits options, UInt32 framebufferIndex, IOAccelDeviceRegion* rgn, UInt32 rgnSize);
	IOReturn surface_flush(UInt32 framebufferMask, IOOptionBits options);
	IOReturn surface_query_lock();
	IOReturn surface_read_lock(IOAccelSurfaceInformation* info, UInt32* infoSize);
	IOReturn surface_read_unlock();
	IOReturn surface_write_lock(IOAccelSurfaceInformation* info, UInt32* infoSize);
	IOReturn surface_write_unlock();
	IOReturn surface_control(UInt32 selector, UInt32 arg, UInt32* result);
	IOReturn set_shape_backing_length(eIOAccelSurfaceShapeBits options,
									  UInt32 framebufferIndex,
									  IOVirtualAddress backing,
									  UInt32 rowbytes,
									  UInt32 backingLength,
									  IOAccelDeviceRegion* rgn /* , UInt32 rgnSize */);
};

#endif /* __VMSVGA2SURFACE_H__ */
