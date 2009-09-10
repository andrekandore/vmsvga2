/*
 *  VMsvga2Accel.cpp
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on July 29th 2009.
 *  Copyright 2009 Zenith432. All rights reserved.
 *
 */

#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/graphics/IOGraphicsInterfaceTypes.h>
#include "vma_options.h"
#include "VMsvga2Accel.h"
#include "VMsvga2Surface.h"
#include "VMsvga22DContext.h"
#include "VMsvga2.h"

#define AUTO_SYNC_UPDATE_COUNT_THRESHOLD 10
#define AUTO_SYNC_PRESENT_COUNT_THRESHOLD 2

#define CLASS VMsvga2Accel
#define super IOAccelerator
OSDefineMetaClassAndStructors(VMsvga2Accel, IOAccelerator);

UInt32 vma_options = 0;

#pragma mark -
#pragma mark Methods from IOService and IOUserClient
#pragma mark -

bool CLASS::init(OSDictionary* dictionary)
{
	if (!super::init(dictionary))
		return false;
	svga3d.Init(0);
	m_provider = 0;
	m_framebuffer = 0;
	m_svga = 0;
	m_auto_sync_update_count = 0;
	m_last_present_fence = 0;
	bHaveSVGA3D = false;
	return true;
}

bool CLASS::start(IOService* provider)
{
	char pathbuf[256];
	UInt32 boot_arg;
	int len;
	OSIterator* it;
	OSObject* obj;
	OSObject* plug;
	VMsvga2* fb = 0;

	m_provider = OSDynamicCast(IOPCIDevice, provider);
	if (!m_provider)
		return false;
	if (!super::start(provider))
		return false;
	vma_options = VMA_OPTION_2D_CONTEXT;
	if (PE_parse_boot_arg("vma_options", &boot_arg))
		vma_options = boot_arg;
	setProperty("VMwareSVGAAccelOptions", static_cast<UInt64>(vma_options), 32U);
	/*
	 * TBD: is there a possible race condition here where VMsvga2Accel::start
	 *   is called before VMsvga2 gets attached to its provider?
	 */
	it = m_provider->getClientIterator();
	if (!it) {
		super::stop(provider);
		return false;
	}
	while ((obj = it->getNextObject()) != 0) {
		fb = OSDynamicCast(VMsvga2, obj);
		if (!fb)
			continue;
		if (!fb->supportsAccel()) {
			fb = 0;
			continue;
		}
		break;
	}
	if (!fb) {
		it->release();
		super::stop(provider);
		return false;
	}
	fb->retain();
	m_framebuffer = fb;
	it->release();
	m_svga = fb->getDevice();
	if (svga3d.Init(m_svga))
		bHaveSVGA3D = true;
	plug = getProperty(kIOCFPlugInTypesKey);
	if (plug)
		fb->setProperty(kIOCFPlugInTypesKey, plug);
	len = sizeof(pathbuf);
	if (getPath(&pathbuf[0], &len, gIOServicePlane)) {
		fb->setProperty(kIOAccelTypesKey, pathbuf);
		fb->setProperty(kIOAccelIndexKey, 0ULL, 32U);
		fb->setProperty(kIOAccelRevisionKey, static_cast<UInt64>(kCurrentGraphicsInterfaceRevision), 32U);
	}
	return true;
}

void CLASS::stop(IOService* provider)
{
	super::stop(provider);
}

void CLASS::free()
{
	if (m_framebuffer) {
		m_framebuffer->release();
		svga3d.Init(0);
		m_svga = 0;
		m_framebuffer = 0;
		bHaveSVGA3D = false;
	}
	super::free();
}

IOReturn CLASS::newUserClient(task_t owningTask, void* securityID, UInt32 type, IOUserClient ** handler)
{
	IOUserClient* client;

	/*
	 * Client Types
	 * 0 - Surface
	 * 1 - GL Context
	 * 2 - 2D Context
	 * 3 - DVD Context
	 * 4 - DVD Context
	 */
#if LOGGING_LEVEL >= 2
	IOLog("%s: newUserClient owningTask==%p, securityID==%p, type==%u\n", getName(), owningTask, securityID, type);
#endif
	if (!handler)
		return kIOReturnBadArgument;
	if (!m_framebuffer)
		return kIOReturnNoDevice;
	switch (type) {
		case kIOAccelSurfaceClientType:
			if (!checkOptionAC(VMA_OPTION_SURFACE_CONNECT))
				return kIOReturnUnsupported;
			client = VMsvga2Surface::withTask(owningTask, securityID, type);
			break;
		case 2:
			if (!checkOptionAC(VMA_OPTION_2D_CONTEXT))
				return kIOReturnUnsupported;
			client = VMsvga22DContext::withTask(owningTask, securityID, type);
			break;
		default:
			return kIOReturnUnsupported;
	}
	if (!client)
		return kIOReturnNoResources;
	if (!client->attach(this)) {
		client->release();
		return kIOReturnInternalError;
	}
	if (!client->start(this)) {
		client->detach(this);
		client->release();
		return kIOReturnInternalError;
	}
	*handler = client;
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark SVGA FIFO Acceleration Methods for 2D Context
#pragma mark -

IOReturn CLASS::useAccelUpdates(UInt32 state)
{
	if (!m_framebuffer)
		return kIOReturnNoDevice;
	m_framebuffer->useAccelUpdates(state != 0);
	return kIOReturnSuccess;
}

IOReturn CLASS::RectCopy(struct IOBlitCopyRectangleStruct const* copyRects, UInt32 copyRectsSize)
{
	UInt32 i, count = copyRectsSize / sizeof(IOBlitCopyRectangle);
	bool rc;

	if (!count || !copyRects)
		return kIOReturnBadArgument;
	if (!m_framebuffer)
		return kIOReturnNoDevice;
	m_framebuffer->lockDevice();
	for (i = 0; i < count; ++i) {
		rc = m_svga->RectCopy(reinterpret_cast<UInt32 const*>(&copyRects[i]));
		if (!rc)
			break;
	}
	m_framebuffer->unlockDevice();
	return rc ? kIOReturnSuccess : kIOReturnNoMemory;
}

IOReturn CLASS::RectFill(UInt32 color, struct IOBlitRectangleStruct const* rects, UInt32 rectsSize)
{
	UInt32 i, count = rectsSize / sizeof(IOBlitRectangle);
	bool rc;

	if (!count || !rects)
		return kIOReturnBadArgument;
	if (!m_framebuffer)
		return kIOReturnNoDevice;
	m_framebuffer->lockDevice();
	for (i = 0; i < count; ++i) {
		rc = m_svga->RectFill(color, reinterpret_cast<UInt32 const*>(&rects[i]));
		if (!rc)
			break;
	}
	m_framebuffer->unlockDevice();
	return rc ? kIOReturnSuccess : kIOReturnNoMemory;
}

#ifdef TIMING
void CLASS::timeSyncs()
{
	static uint32_t first_stamp = 0;
	static uint32_t num_syncs = 0;
	static uint32_t last_stamp;
	uint32_t current_stamp;
	uint32_t dummy;
	float freq;

	if (!first_stamp) {
		clock_get_system_microtime(&first_stamp, &dummy);
		last_stamp = first_stamp;
		return;
	}
	++num_syncs;
	clock_get_system_microtime(&current_stamp, &dummy);
	if (current_stamp >= last_stamp + 60) {
		last_stamp = current_stamp;
		freq = static_cast<float>(num_syncs)/static_cast<float>(current_stamp - first_stamp);
		m_framebuffer->setProperty("VMwareSVGASyncFrequency", static_cast<UInt64>(freq * 1000.0F), 64U);
	}
}
#endif

IOReturn CLASS::UpdateFramebuffer(UInt32 const* rect)
{
	if (!rect)
		return kIOReturnBadArgument;
	if (!m_framebuffer)
		return kIOReturnNoDevice;
	m_framebuffer->lockDevice();
	m_svga->UpdateFramebuffer2(rect);
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

IOReturn CLASS::UpdateFramebufferAutoSync(UInt32 const* rect)
{
	if (!rect)
		return kIOReturnBadArgument;
	if (!m_framebuffer)
		return kIOReturnNoDevice;
	++m_auto_sync_update_count;
	if (m_auto_sync_update_count == AUTO_SYNC_UPDATE_COUNT_THRESHOLD)
		m_auto_sync_update_count = 0;
	m_framebuffer->lockDevice();
	m_svga->UpdateFramebuffer2(rect);
	if (!m_auto_sync_update_count)
		m_svga->SyncToFence(m_svga->InsertFence());
	m_framebuffer->unlockDevice();
#ifdef TIMING
	timeSyncs();
#endif
	return kIOReturnSuccess;
}

IOReturn CLASS::SyncFIFO()
{
	if (!m_framebuffer)
		return kIOReturnNoDevice;
	m_framebuffer->lockDevice();
	m_svga->SyncToFence(m_svga->InsertFence());
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

IOReturn CLASS::RegionCopy(UInt32 destX, UInt32 destY, void /* IOAccelDeviceRegion */ const* region, UInt32 regionSize)
{
	IOAccelDeviceRegion const* rgn = static_cast<IOAccelDeviceRegion const*>(region);
	IOAccelBounds const* rect;
	SInt32 deltaX, deltaY;
	UInt32 i, copyRect[6];
	bool rc;

	if (!rgn || regionSize < IOACCEL_SIZEOF_DEVICE_REGION(rgn))
		return kIOReturnBadArgument;
	if (!m_framebuffer)
		return kIOReturnNoDevice;
	m_framebuffer->lockDevice();
	rect = &rgn->bounds;
	if (checkOptionAC(VMA_OPTION_REGION_BOUNDS_COPY)) {
		copyRect[0] = rect->x;
		copyRect[1] = rect->y;
		copyRect[2] = destX;
		copyRect[3] = destY;
		copyRect[4] = rect->w;
		copyRect[5] = rect->h;
		rc = m_svga->RectCopy(&copyRect[0]);
	} else {
		deltaX = destX - rect->x;
		deltaY = destY - rect->y;
		for (i = 0; i < rgn->num_rects; ++i) {
			rect = &rgn->rect[i];
			copyRect[0] = rect->x;
			copyRect[1] = rect->y;
			copyRect[2] = rect->x + deltaX;
			copyRect[3] = rect->y + deltaY;
			copyRect[4] = rect->w;
			copyRect[5] = rect->h;
			rc = m_svga->RectCopy(&copyRect[0]);
			if (!rc)
				break;
		}
	}
	m_framebuffer->unlockDevice();
	return rc ? kIOReturnSuccess : kIOReturnNoMemory;
}

#pragma mark -
#pragma mark Acceleration Methods for Surfaces
#pragma mark -

bool CLASS::getScreenInfo(IOAccelSurfaceReadData* info)
{
	if (!m_framebuffer || !info)
		return false;
	memset(info, 0, sizeof(IOAccelSurfaceReadData));
	m_framebuffer->lockDevice();
	info->w = m_svga->getCurrentWidth();
	info->h = m_svga->getCurrentHeight();
	info->client_addr = reinterpret_cast<void*>(m_framebuffer->getBar1Ptr());
	info->client_row_bytes = m_svga->getCurrentPitch();
	m_framebuffer->unlockDevice();
	return true;
}

bool CLASS::createSurface(UInt32 sid, SVGA3dSurfaceFormat surfaceFormat, UInt32 width, UInt32 height)
{
	SVGA3dSize* mipSizes;
	SVGA3dSurfaceFace* faces;

	if (!bHaveSVGA3D)
		return false;
	m_framebuffer->lockDevice();
	svga3d.BeginDefineSurface(sid, static_cast<SVGA3dSurfaceFlags>(0), surfaceFormat, &faces, &mipSizes, 1U);
	faces[0].numMipLevels = 1;
	mipSizes[0].width = width;
	mipSizes[0].height = height;
	mipSizes[0].depth = 1;
	m_svga->FIFOCommitAll();
	m_framebuffer->unlockDevice();
	return true;
}

bool CLASS::destroySurface(UInt32 sid)
{
	if (!bHaveSVGA3D)
		return false;
	m_framebuffer->lockDevice();
	svga3d.DestroySurface(sid);
	m_framebuffer->unlockDevice();
	return true;
}

IOMemoryMap* CLASS::allocBackingForTask(task_t task, UInt32 offset_in_bar1, UInt32 size)
{
	IODeviceMemory* bar1;

	if (!m_framebuffer)
		return 0;
	bar1 = m_framebuffer->getBar1();
	if (!bar1)
		return 0;
	return bar1->map(task,
					 0,
					 kIOMapAnywhere,
					 offset_in_bar1,
					 size);
}

bool CLASS::surfaceDMA2D(UInt32 sid, SVGA3dTransferType transfer, void /* IOAccelDeviceRegion */ const* region, ExtraInfo const* extra)
{
	UInt32 i, numCopyBoxes;
	SVGA3dCopyBox* copyBoxes;
	SVGA3dGuestImage guestImage;
	SVGA3dSurfaceImageId hostImage;
	IOAccelDeviceRegion const* rgn;

	if (!extra || !bHaveSVGA3D)
		return false;
	rgn = static_cast<IOAccelDeviceRegion const*>(region);
	numCopyBoxes = rgn ? rgn->num_rects : 0;
	hostImage.sid = sid;
	hostImage.face = 0;
	hostImage.mipmap = 0;
	guestImage.ptr.gmrId = static_cast<UInt32>(-2) /* SVGA_GMR_FRAMEBUFFER */;
	guestImage.ptr.offset = extra->mem_offset_in_bar1;
	guestImage.pitch = extra->mem_pitch;
	m_framebuffer->lockDevice();
	svga3d.BeginSurfaceDMA(&guestImage, &hostImage, transfer, &copyBoxes, numCopyBoxes);
	for (i = 0; i < numCopyBoxes; ++i) {
		IOAccelBounds const* src = &rgn->rect[i];
		SVGA3dCopyBox* dst = &copyBoxes[i];
		dst->srcx = src->x + extra->srcDeltaX;
		dst->srcy = src->y + extra->srcDeltaY;
		dst->x = src->x + extra->dstDeltaX;
		dst->y = src->y + extra->dstDeltaY;
		dst->w = src->w;
		dst->h = src->h;
		dst->d = 1;
	}
	m_svga->FIFOCommitAll();
	m_framebuffer->unlockDevice();
	return true;
}

bool CLASS::surfacePresentAutoSync(UInt32 sid, void /* IOAccelDeviceRegion */ const* region, ExtraInfo const* extra)
{
	UInt32 i, numCopyRects;
	SVGA3dCopyRect* copyRects;
	IOAccelDeviceRegion const* rgn;

	if (!extra || !bHaveSVGA3D)
		return false;
	rgn = static_cast<IOAccelDeviceRegion const*>(region);
	numCopyRects = rgn ? rgn->num_rects : 0;
	m_framebuffer->lockDevice();
	m_svga->SyncToFence(m_last_present_fence);
	svga3d.BeginPresent(sid, &copyRects, numCopyRects);
	for (i = 0; i < numCopyRects; ++i) {
		IOAccelBounds const* src = &rgn->rect[i];
		SVGA3dCopyRect* dst = &copyRects[i];
		dst->srcx = src->x + extra->srcDeltaX;
		dst->srcy = src->y + extra->srcDeltaY;
		dst->x = src->x + extra->dstDeltaX;
		dst->y = src->y + extra->dstDeltaY;
		dst->w = src->w;
		dst->h = src->h;
	}
	m_svga->FIFOCommitAll();
	if (!m_auto_sync_update_count)
		m_last_present_fence = m_svga->InsertFence();
	++m_auto_sync_update_count;
	if (m_auto_sync_update_count == AUTO_SYNC_PRESENT_COUNT_THRESHOLD)
		m_auto_sync_update_count = 0;
	m_framebuffer->unlockDevice();
	return true;
}

bool CLASS::surfacePresentReadback(void /* IOAccelDeviceRegion */ const* region)
{
	UInt32 i, numRects;
	SVGA3dRect* rects;
	IOAccelDeviceRegion const* rgn;

	if (!bHaveSVGA3D)
		return false;
	rgn = static_cast<IOAccelDeviceRegion const*>(region);
	numRects = rgn ? rgn->num_rects : 0;
	m_framebuffer->lockDevice();
	svga3d.BeginPresentReadback(&rects, numRects);
	for (i = 0; i < numRects; ++i) {
		IOAccelBounds const* src = &rgn->rect[i];
		SVGA3dRect* dst = &rects[i];
		dst->x = src->x;
		dst->y = src->y;
		dst->w = src->w;
		dst->h = src->h;
	}
	m_svga->FIFOCommitAll();
	m_framebuffer->unlockDevice();
	return true;
}

#if 0
bool CLASS::setupRenderContext(UInt32 cid, UInt32 color_sid, UInt32 depth_sid, UInt32 width, UInt32 height)
{
	SVGA3dRenderState* rs;
	SVGA3dSurfaceImageId colorImage;
	SVGA3dSurfaceImageId depthImage;
	SVGA3dRect rect;

	if (!bHaveSVGA3D)
		return false;
	memset(&colorImage, 0, sizeof(SVGA3dSurfaceImageId));
	memset(&depthImage, 0, sizeof(SVGA3dSurfaceImageId));
	memset(&rect, 0, sizeof(SVGA3dRect));
	colorImage.sid = color_sid;
	depthImage.sid = depth_sid;
	rect.w = width;
	rect.h = height;
	m_framebuffer->lockDevice();
	svga3d.SetRenderTarget(cid, SVGA3D_RT_COLOR0, &colorImage);
	svga3d.SetRenderTarget(cid, SVGA3D_RT_DEPTH, &depthImage);
	svga3d.SetViewport(cid, &rect);
	svga3d.SetZRange(cid, 0.0F, 1.0F);
	svga3d.BeginSetRenderState(cid, &rs, 1);
	rs->state = SVGA3D_RS_SHADEMODE;
	rs->uintValue = SVGA3D_SHADEMODE_SMOOTH;
	m_svga->FIFOCommitAll();
	m_framebuffer->unlockDevice();
	return true;
}
#else
bool CLASS::setupRenderContext(UInt32 cid, UInt32 color_sid, UInt32 depth_sid, UInt32 width, UInt32 height)
{
	SVGA3dSurfaceImageId colorImage;

	if (!bHaveSVGA3D)
		return false;
	memset(&colorImage, 0, sizeof(SVGA3dSurfaceImageId));
	colorImage.sid = color_sid;
	m_framebuffer->lockDevice();
	svga3d.SetRenderTarget(cid, SVGA3D_RT_COLOR0, &colorImage);
	m_framebuffer->unlockDevice();
	return true;
}
#endif

bool CLASS::clearContext(UInt32 cid, SVGA3dClearFlag flags, UInt32 width, UInt32 height, UInt32 color, float depth, UInt32 stencil)
{
	SVGA3dRect* rect;

	if (!bHaveSVGA3D)
		return false;
	m_framebuffer->lockDevice();
	svga3d.BeginClear(cid, flags, color, depth, stencil, &rect, 1);
	memset(rect, 0, sizeof(SVGA3dRect));
	rect->w = width;
	rect->h = height;
	m_svga->FIFOCommitAll();
	m_framebuffer->unlockDevice();
	return true;
}

bool CLASS::createContext(UInt32 cid)
{
	if (!bHaveSVGA3D)
		return false;
	m_framebuffer->lockDevice();
	svga3d.DefineContext(cid);
	m_framebuffer->unlockDevice();
	return true;
}

bool CLASS::destroyContext(UInt32 cid)
{
	if (!bHaveSVGA3D)
		return false;
	m_framebuffer->lockDevice();
	svga3d.DestroyContext(cid);
	m_framebuffer->unlockDevice();
	return true;
}

