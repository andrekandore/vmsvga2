/*
 *  VMsvga2Surface.cpp
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on July 29th 2009.
 *  Copyright 2009 Zenith432. All rights reserved.
 *
 */

#include <IOKit/IOLib.h>
#include <IOKit/graphics/IOGraphicsInterfaceTypes.h>

#include "vma_options.h"
#include "VMsvga2Accel.h"
#include "VMsvga2Surface.h"

#define CLASS VMsvga2Surface
#define super IOUserClient
OSDefineMetaClassAndStructors(VMsvga2Surface, IOUserClient);

#define VRAM_OFFSET SVGA_FB_MAX_TRACEABLE_SIZE
#define VRAM_CHUNK_SIZE (8*1024*1024)

#define VLOG_PREFIX_STR "log IOSF: "
#define VLOG_PREFIX_LEN (sizeof(VLOG_PREFIX_STR) - 1)
#define VLOG_BUF_SIZE 256

extern "C" char VMLog_SendString(char const* str);

#if LOGGING_LEVEL >= 1
#define SLog(fmt, ...) CLASS::VLog(fmt, ##__VA_ARGS__)
#else
#define SLog(fmt, ...)
#endif

static IOExternalMethod iofbFuncsCache[kIOAccelNumSurfaceMethods] =
{
	{0, reinterpret_cast<IOMethod>(&CLASS::surface_read_lock_options), kIOUCScalarIStructO, 1, kIOUCVariableStructureSize},
	{0, reinterpret_cast<IOMethod>(&CLASS::surface_read_unlock_options), kIOUCScalarIScalarO, 1, 0},
	{0, reinterpret_cast<IOMethod>(&CLASS::get_state), kIOUCScalarIScalarO, 0, 1},
	{0, reinterpret_cast<IOMethod>(&CLASS::surface_write_lock_options), kIOUCScalarIStructO, 1, kIOUCVariableStructureSize},
	{0, reinterpret_cast<IOMethod>(&CLASS::surface_write_unlock_options), kIOUCScalarIScalarO, 1, 0},
	{0, reinterpret_cast<IOMethod>(&CLASS::surface_read), kIOUCScalarIStructI, 0, kIOUCVariableStructureSize},
	{0, reinterpret_cast<IOMethod>(&CLASS::set_shape_backing), kIOUCScalarIStructI, 4, kIOUCVariableStructureSize},
	{0, reinterpret_cast<IOMethod>(&CLASS::set_id_mode), kIOUCScalarIScalarO, 2, 0},
	{0, reinterpret_cast<IOMethod>(&CLASS::set_scale), kIOUCScalarIStructI, 1, kIOUCVariableStructureSize},
	{0, reinterpret_cast<IOMethod>(&CLASS::set_shape), kIOUCScalarIStructI, 2, kIOUCVariableStructureSize},
	{0, reinterpret_cast<IOMethod>(&CLASS::surface_flush), kIOUCScalarIScalarO, 2, 0},
	{0, reinterpret_cast<IOMethod>(&CLASS::surface_query_lock), kIOUCScalarIScalarO, 0, 0},
	{0, reinterpret_cast<IOMethod>(&CLASS::surface_read_lock), kIOUCScalarIStructO, 0, kIOUCVariableStructureSize},
	{0, reinterpret_cast<IOMethod>(&CLASS::surface_read_unlock), kIOUCScalarIScalarO, 0, 0},
	{0, reinterpret_cast<IOMethod>(&CLASS::surface_write_lock), kIOUCScalarIStructO, 0, kIOUCVariableStructureSize},
	{0, reinterpret_cast<IOMethod>(&CLASS::surface_write_unlock), kIOUCScalarIScalarO, 0, 0},
	{0, reinterpret_cast<IOMethod>(&CLASS::surface_control), kIOUCScalarIScalarO, 2, 1},
	{0, reinterpret_cast<IOMethod>(&CLASS::set_shape_backing_length), kIOUCScalarIStructI, 5, kIOUCVariableStructureSize}
};

void CLASS::VLog(char const* fmt, ...)
{
	va_list ap;
	char print_buf[VLOG_BUF_SIZE];

	va_start(ap, fmt);
	strlcpy(&print_buf[0], VLOG_PREFIX_STR, sizeof(print_buf));
	vsnprintf(&print_buf[VLOG_PREFIX_LEN], sizeof(print_buf) - VLOG_PREFIX_LEN, fmt, ap);
	va_end(ap);
	VMLog_SendString(&print_buf[0]);
}

#pragma mark -
#pragma mark IOUserClient Methods
#pragma mark -

IOExternalMethod* CLASS::getTargetAndMethodForIndex(IOService** targetP, UInt32 index)
{
	if (!targetP || index >= kIOAccelNumSurfaceMethods)
		return 0;
	*targetP = this;
	return &m_funcs_cache[index];
}

IOReturn CLASS::clientClose()
{
#if LOGGING_LEVEL >= 2
	IOLog("VMsvga2Surface: clientClose\n");
#endif
	if (bContextCreated && m_provider) {
		m_provider->destroySurface(color_sid);
		bContextCreated = false;
	}
	if (!terminate(0))
		IOLog("%s: terminate failed\n", __FUNCTION__);
	m_owning_task = 0;
	m_provider = 0;
	return kIOReturnSuccess;
}

bool CLASS::start(IOService* provider)
{
	m_provider = OSDynamicCast(VMsvga2Accel, provider);
	if (!m_provider)
		return false;
	return super::start(provider);
}

void CLASS::stop(IOService* provider)
{
	if (m_backing) {
		m_backing->release();
		m_backing = 0;
	}
	clearLastShape();
	super::stop(provider);
}

bool CLASS::initWithTask(task_t owningTask, void* securityToken, UInt32 type)
{
	m_owning_task = 0;
	m_provider = 0;
	m_funcs_cache = 0;
	bContextCreated = false;
	m_backing = 0;
	m_last_shape = 0;
	m_last_region = 0;
	m_last_region_pitch = 0;
	if (!super::initWithTask(owningTask, securityToken, type))
		return false;
	m_owning_task = owningTask;
	m_funcs_cache = &iofbFuncsCache[0];
	return true;
}

CLASS* CLASS::withTask(task_t owningTask, void* securityToken, UInt32 type)
{
	CLASS* inst;

	inst = new CLASS;

	if (inst && !inst->initWithTask(owningTask, securityToken, type))
	{
		inst->release();
		inst = 0;
	}

	return (inst);
}

void CLASS::clearLastShape()
{
	if (m_last_shape) {
		m_last_shape->release();
		m_last_shape = 0;
		m_last_region = 0;
		m_last_region_pitch = 0;
	}
}

#pragma mark -
#pragma mark IONVSurface Methods
#pragma mark -

IOReturn CLASS::surface_read_lock_options(IOOptionBits options, IOAccelSurfaceInformation* info, UInt32* infoSize)
{
#if LOGGING_LEVEL >= 2
	SLog("%s: surface_read_lock_options(0x%x, %p, %u)\n", getName(), options, info, infoSize ? *infoSize : 0);
#endif
	return kIOReturnUnsupported;
}

IOReturn CLASS::surface_read_unlock_options(IOOptionBits options)
{
#if LOGGING_LEVEL >= 2
	SLog("%s: surface_read_unlock_options(0x%x)\n", getName(), options);
#endif
	return kIOReturnSuccess;
}

IOReturn CLASS::get_state(eIOAccelSurfaceStateBits* state)
{
#if LOGGING_LEVEL >= 2
	SLog("%s: get_state(%p)\n", getName(), state);
#endif
	return kIOReturnUnsupported;
}

IOReturn CLASS::surface_write_lock_options(IOOptionBits options, IOAccelSurfaceInformation* info, UInt32* infoSize)
{
#if LOGGING_LEVEL >= 3
	SLog("%s: surface_write_lock_options(0x%x, %p, %u)\n", getName(), options, info, infoSize ? *infoSize : 0);
#endif
	if (!info || !infoSize || *infoSize != sizeof(IOAccelSurfaceInformation))
		return kIOReturnBadArgument;
#if 0 && LOGGING_LEVEL >= 5
	for (UInt32 i = 0; i < sizeof(IOAccelSurfaceInformation) / sizeof(UInt32); ++i) {
		UInt32 v = reinterpret_cast<UInt32 const*>(info)[i];
		SLog("%s:   info[%d] == 0x%x\n", getName(), i, v);
	}
#endif
	if (!bContextCreated)
		return kIOReturnNotReady;
	memset(info, 0, sizeof(IOAccelSurfaceInformation));
	m_backing = m_provider->allocBackingForTask(m_owning_task, VRAM_OFFSET, VRAM_CHUNK_SIZE);
	if (!m_backing)
		return kIOReturnNoMemory;
	info->address[0] = m_backing->getVirtualAddress();
	if (m_last_region) {
		if (checkOptionAC(VMA_OPTION_PACKED_BACKING))
			info->rowBytes = m_last_region_pitch;
		else {
			info->rowBytes = screenInfo.client_row_bytes;
			info->address[0] += m_last_region->bounds.y * info->rowBytes + m_last_region->bounds.x * sizeof(UInt32);	// TBD use right pixel size
		}
		info->width = m_last_region->bounds.w;
		info->height = m_last_region->bounds.h;
	} else {
		info->rowBytes = screenInfo.client_row_bytes;
		info->width = screenInfo.w;
		info->height = screenInfo.h;
	}
	info->pixelFormat = kIO24RGBPixelFormat;	// TBD use right pixel format
	return kIOReturnSuccess;
}

IOReturn CLASS::surface_write_unlock_options(IOOptionBits options)
{
	bool rc, packed_backing = checkOptionAC(VMA_OPTION_PACKED_BACKING);
	VMsvga2Accel::ExtraInfo extra;

#if LOGGING_LEVEL >= 3
	SLog("%s: surface_write_unlock_options(0x%x)\n", getName(), options);
#endif
	if (!bContextCreated || !m_backing)
		return kIOReturnNotReady;
	memset(&extra, 0, sizeof(extra));
	extra.mem_offset_in_bar1 = VRAM_OFFSET;
	if (packed_backing) {
		extra.mem_pitch = m_last_region ? m_last_region_pitch : 0;
		if (m_last_region) {
			extra.srcDeltaX = -static_cast<SInt32>(m_last_region->bounds.x);
			extra.srcDeltaY = -static_cast<SInt32>(m_last_region->bounds.y);
		}
	} else
		extra.mem_pitch = screenInfo.client_row_bytes;
	rc = m_provider->surfaceDMA2D(color_sid, SVGA3D_WRITE_HOST_VRAM, m_last_region, &extra);
	if (packed_backing)
		m_provider->SyncFIFO();	// TBD use different mem areas so can do multiple backing at the same time
	m_backing->release();
	m_backing = 0;
	return rc ? kIOReturnSuccess : kIOReturnInternalError;
}

IOReturn CLASS::surface_read(IOAccelSurfaceReadData* parameters, UInt32 parametersSize)
{
#if LOGGING_LEVEL >= 2
	SLog("%s: surface_read(%p, %u)\n", getName(), parameters, parametersSize);
#endif
	return kIOReturnUnsupported;
}

IOReturn CLASS::set_shape_backing(eIOAccelSurfaceShapeBits options,
								  UInt32 framebufferIndex,
								  IOVirtualAddress backing,
								  UInt32 rowbytes,
								  IOAccelDeviceRegion* rgn,
								  UInt32 rgnSize)
{
	return set_shape_backing_length_ext(options, framebufferIndex, backing, rowbytes, rgn, rgnSize, rgn ? (rowbytes * rgn->bounds.h) : 0);
}

IOReturn CLASS::set_id_mode(UInt32 wID, eIOAccelSurfaceModeBits modebits)
{
	UInt32 cid;

#if LOGGING_LEVEL >= 2
	SLog("%s: set_id_mode(%u, 0x%x)\n", getName(), wID, modebits);
#endif
#ifdef TESTING
	if (checkOptionAC(VMA_OPTION_TEST))
		runTest();
#endif
	if (modebits & ~(kIOAccelSurfaceModeColorDepthBits | kIOAccelSurfaceModeWindowedBit))
		return kIOReturnUnsupported;
	if (!(modebits & kIOAccelSurfaceModeWindowedBit))
		return kIOReturnUnsupported;
	switch (modebits & kIOAccelSurfaceModeColorDepthBits) {
		case kIOAccelSurfaceModeColorDepth1555:
			surfaceFormat = SVGA3D_X1R5G5B5;
			break;
		case kIOAccelSurfaceModeColorDepth8888:
			surfaceFormat = SVGA3D_X8R8G8B8;
			break;
		case kIOAccelSurfaceModeColorDepthBGRA32:
			surfaceFormat = SVGA3D_A8R8G8B8;
			break;
		case kIOAccelSurfaceModeColorDepthYUV:
		case kIOAccelSurfaceModeColorDepthYUV9:
		case kIOAccelSurfaceModeColorDepthYUV12:
		case kIOAccelSurfaceModeColorDepthYUV2:
		default:
			// TBD
			return kIOReturnUnsupported;
	}
	cid = 1;
	color_sid = wID & 0xFFFFU;	// TBD: find a consistent way to make wID a "small" integer
	if (!m_provider->getScreenInfo(&screenInfo))
		return kIOReturnNoDevice;
	if (!m_provider->createSurface(color_sid, surfaceFormat, screenInfo.w, screenInfo.h))
		return kIOReturnNoResources;
	if (!m_provider->createContext(cid)) {
		m_provider->destroySurface(color_sid);
		return kIOReturnNoResources;
	}
	m_provider->setupRenderContext(cid, color_sid, 0, screenInfo.w, screenInfo.h);	// note: rc ignored
	m_provider->clearContext(cid, static_cast<SVGA3dClearFlag>(SVGA3D_CLEAR_COLOR), screenInfo.w, screenInfo.h, 0, 1.0F, 0);
	m_provider->SyncFIFO();
	m_provider->destroyContext(cid);
	bContextCreated = true;
	return kIOReturnSuccess;
}

IOReturn CLASS::set_scale(IOOptionBits options, IOAccelSurfaceScaling* scaling, UInt32 scalingSize)
{
#if LOGGING_LEVEL >= 2
	SLog("%s: set_scale(0x%x, %p, %u)\n", getName(), options, scaling, scalingSize);
#endif
	return kIOReturnUnsupported;
}

IOReturn CLASS::set_shape(eIOAccelSurfaceShapeBits options, UInt32 framebufferIndex, IOAccelDeviceRegion* rgn, UInt32 rgnSize)
{
	return set_shape_backing_length_ext(options, framebufferIndex, 0, static_cast<UInt32>(-1), rgn, rgnSize, 0);
}

IOReturn CLASS::surface_flush(UInt32 framebufferMask, IOOptionBits options)
{
	bool rc;
	UInt32 rect[4];
	VMsvga2Accel::ExtraInfo extra;

#if LOGGING_LEVEL >= 3
	SLog("%s: surface_flush(%u, 0x%x)\n", getName(), framebufferMask, options);
#endif
	if (!bContextCreated)
		return kIOReturnNotReady;
	memset(&extra, 0, sizeof(extra));
	if (checkOptionAC(VMA_OPTION_USE_PRESENT)) {
		rc = m_provider->surfacePresentAutoSync(color_sid, m_last_region, &extra);
		if (checkOptionAC(VMA_OPTION_USE_READBACK))
			m_provider->surfacePresentReadback(m_last_region);
	} else {
		extra.mem_offset_in_bar1 = 0;
		extra.mem_pitch = screenInfo.client_row_bytes;
		rc = m_provider->surfaceDMA2D(color_sid, SVGA3D_READ_HOST_VRAM, m_last_region, &extra);
		if (m_last_region) {
			rect[0] = m_last_region->bounds.x;
			rect[1] = m_last_region->bounds.y;
			rect[2] = m_last_region->bounds.w;
			rect[3] = m_last_region->bounds.h;
		} else {
			rect[0] = 0;
			rect[1] = 0;
			rect[2] = screenInfo.w;
			rect[3] = screenInfo.h;
		}
		m_provider->UpdateFramebufferAutoSync(&rect[0]);
	}
	return rc ? kIOReturnSuccess : kIOReturnInternalError;
}

IOReturn CLASS::surface_query_lock()
{
#if LOGGING_LEVEL >= 3
	SLog("%s: surface_query_lock()\n", getName());
#endif
	return kIOReturnSuccess;
}

IOReturn CLASS::surface_read_lock(IOAccelSurfaceInformation* info, UInt32* infoSize)
{
	return surface_read_lock_options(kIOAccelSurfaceLockInDontCare, info, infoSize);
}

IOReturn CLASS::surface_read_unlock()
{
	return surface_read_unlock_options(kIOAccelSurfaceLockInDontCare);
}

IOReturn CLASS::surface_write_lock(IOAccelSurfaceInformation* info, UInt32* infoSize)
{
	return surface_write_lock_options(kIOAccelSurfaceLockInAccel, info, infoSize);
}

IOReturn CLASS::surface_write_unlock()
{
	return surface_write_unlock_options(kIOAccelSurfaceLockInDontCare);
}

IOReturn CLASS::surface_control(UInt32 selector, UInt32 arg, UInt32* result)
{
#if LOGGING_LEVEL >= 2
	SLog("%s: surface_control(%u, %u, %p)\n", getName(), selector, arg, result);
#endif
	return kIOReturnUnsupported;
}

IOReturn CLASS::set_shape_backing_length(eIOAccelSurfaceShapeBits options,
										 UInt32 framebufferIndex,
										 IOVirtualAddress backing,
										 UInt32 rowbytes,
										 UInt32 backingLength,
										 IOAccelDeviceRegion* rgn /* , UInt32 rgnSize */)
{
	return set_shape_backing_length_ext(options, framebufferIndex, backing, rowbytes, rgn,  rgn ? IOACCEL_SIZEOF_DEVICE_REGION(rgn) : 0, backingLength);
}

IOReturn CLASS::set_shape_backing_length_ext(eIOAccelSurfaceShapeBits options,
											 UInt32 framebufferIndex,
											 IOVirtualAddress backing,
											 UInt32 rowbytes,
											 IOAccelDeviceRegion* rgn,
											 UInt32 rgnSize,
											 UInt32 backingLength)
{
	int const expectedOptions = kIOAccelSurfaceShapeIdentityScaleBit | kIOAccelSurfaceShapeFrameSyncBit;

#if LOGGING_LEVEL >= 3
	SLog("%s: set_shape_backing_length_ext(0x%x, %u, 0x%x, %u, %p, %u, %u)\n",
		 getName(),
		 options,
		 framebufferIndex,
		 backing,
		 rowbytes,
		 rgn,
		 rgnSize,
		 backingLength);
#endif

	if (!rgn || rgnSize < IOACCEL_SIZEOF_DEVICE_REGION(rgn))
		return kIOReturnBadArgument;

#if LOGGING_LEVEL >= 3
	SLog("%s:   rgn->num_rects == %u, rgn->bounds == %u, %u, %u, %u\n",
		 getName(),
		 rgn->num_rects,
		 rgn->bounds.x,
		 rgn->bounds.y,
		 rgn->bounds.w,
		 rgn->bounds.h);
	for (UInt32 i = 0; i < rgn->num_rects; ++i) {
		SLog("%s:   rgn->rect[%d] == %u, %u, %u, %u\n",
			 getName(),
			 i,
			 rgn->rect[i].x,
			 rgn->rect[i].y,
			 rgn->rect[i].w,
			 rgn->rect[i].h);
	}
#endif

	clearLastShape();
	if ((options & expectedOptions) != expectedOptions)
		return kIOReturnSuccess;
	m_last_shape = OSData::withBytes(rgn, rgnSize);
	if (!m_last_shape)
		return kIOReturnNoMemory;
	m_last_region = static_cast<IOAccelDeviceRegion const*>(m_last_shape->getBytesNoCopy());
	m_last_region_pitch = rgn->bounds.w * sizeof(UInt32);	// TBD: use right pixel size
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark TEST
#pragma mark -

#ifdef TESTING
void CLASS::runTest()
{
	char buf[20];
	bool rc;
	UInt32* ptr;
	IOAccelDeviceRegion* rgn = reinterpret_cast<IOAccelDeviceRegion*>(&buf[0]);
	UInt32 const cid = 1;
	UInt32 const color_sid = 12345;
	UInt32 const depth_sid = 12346;
	UInt32 const pixval = 0xFF00FF00U;
	UInt32 i, w, h, pixels;
	UInt32 rect[4];
	VMsvga2Accel::ExtraInfo extra;

	rc = m_provider->getScreenInfo(&screenInfo);
	if (!rc) {
		SLog("runTest getScreenInfo failed\n");
		return;
	}
	if (checkOptionAC(VMA_OPTION_TEST_SMALL)) {
		w = h = 512;
	} else {
		w = screenInfo.w;
		h = screenInfo.h;
	}
	pixels = w * h;
	rc = m_provider->createSurface(color_sid, SVGA3D_X8R8G8B8, w, h);
	if (!rc) {
		SLog("runTest createSurface color failed\n");
		return;
	}
	rc = m_provider->createSurface(depth_sid, SVGA3D_Z_D16, w, h);
	if (!rc) {
		SLog("runTest createSurface depth failed\n");
		return;
	}
	rc = m_provider->createContext(cid);
	if (!rc) {
		SLog("runTest createContext failed\n");
		return;
	}
	rc = m_provider->setupRenderContext(cid, color_sid, depth_sid, w, h);
	if (!rc) {
		SLog("runTest setupRenderContext failed\n");
		return;
	}
	rc = m_provider->clearContext(cid, static_cast<SVGA3dClearFlag>(SVGA3D_CLEAR_COLOR | SVGA3D_CLEAR_DEPTH), w, h, 0xFFFF0000U, 1.0F, 0);
	if (!rc) {
		SLog("runTest clearContext failed\n");
		return;
	}
	ptr = static_cast<UInt32*>(screenInfo.client_addr) + VRAM_OFFSET / sizeof(UInt32);
	rgn->num_rects = 1;
	rgn->bounds.x = 0;
	rgn->bounds.y = 0;
	rgn->bounds.w = static_cast<SInt16>(w);
	rgn->bounds.h = static_cast<SInt16>(h);
	memcpy(&rgn->rect[0], &rgn->bounds, sizeof(IOAccelBounds));
	rect[0] = 0;
	rect[1] = 0;
	rect[2] = w;
	rect[3] = h;
	for (i = 0; i < pixels; ++i)
		ptr[i] = pixval;
	memset(&extra, 0, sizeof(extra));
	extra.mem_offset_in_bar1 = VRAM_OFFSET;
	extra.mem_pitch = w * sizeof(UInt32);
	rc = m_provider->surfaceDMA2D(color_sid, SVGA3D_WRITE_HOST_VRAM, rgn, &extra);
	if (!rc) {
		SLog("runTest surfaceDMA2D write failed\n");
		return;
	}
	m_provider->SyncFIFO();
	memset(ptr, 0, pixels * sizeof(UInt32));
	for (i = 0; i < pixels; ++i)
		if (ptr[i] != pixval) {
			SLog("runTest DMA dummy diff ok\n");
			break;
		}
	rc = m_provider->surfaceDMA2D(color_sid, SVGA3D_READ_HOST_VRAM, rgn, &extra);
	if (!rc) {
		SLog("runTest surfaceDMA2D read failed\n");
		return;
	}
	m_provider->SyncFIFO();
	for (i = 0; i < pixels; ++i)
		if (ptr[i] != pixval) {
			SLog("runTest DMA readback differs\n");
			break;
		}
	if (checkOptionAC(VMA_OPTION_TEST_PRESENT)) {
		memset(&extra, 0, sizeof(extra));
		rc = m_provider->surfacePresentAutoSync(color_sid, rgn, &extra);
		if (!rc) {
			SLog("runTest surfacePresent failed\n");
			return;
		}
		if (checkOptionAC(VMA_OPTION_USE_READBACK)) {
			rc = m_provider->surfacePresentReadback(rgn);
			if (!rc) {
				SLog("runTest surfacePresentReadback failed\n");
				return;
			}
		}
		m_provider->SyncFIFO();
	} else {
		extra.mem_offset_in_bar1 = 0;
		extra.mem_pitch = screenInfo.client_row_bytes;
		rc = m_provider->surfaceDMA2D(color_sid, SVGA3D_READ_HOST_VRAM, rgn, &extra);
		if (!rc) {
			SLog("runTest surfaceDMA2D to FB failed\n");
			return;
		}
		m_provider->UpdateFramebuffer(&rect[0]);
		m_provider->SyncFIFO();
	}
	SLog("runTest complete\n");
	IOSleep(10000);
	m_provider->destroySurface(color_sid);
	m_provider->destroySurface(depth_sid);
	m_provider->destroyContext(cid);
	m_provider->SyncFIFO();
}
#endif /* TESTING */
