/*
 *  VMsvga2GA.cpp
 *  VMsvga2GA
 *
 *  Created by Zenith432 on August 6th 2009.
 *  Copyright 2009 Zenith432. All rights reserved.
 *
 */

#include <CoreFoundation/CFNumber.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <IOKit/graphics/IOGraphicsInterface.h>

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#include <cstdlib>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#else
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#endif
#include "VMsvga2GA.h"
#include "BlitHelper.h"

#define kVMGAFactoryID \
	(CFUUIDGetConstantUUIDWithBytes(NULL, 0x03, 0x46, 0x3B, 0x45, 0x6F, 0xDD, 0x47, 0x49, 0xB6, 0xB7, 0x15, 0xEB, 0x76, 0xBA, 0xA2, 0x2F))

#define VLOG_PREFIX_STR "log IOGA: "
#define VLOG_PREFIX_LEN (sizeof(VLOG_PREFIX_STR) - 1)
#define VLOG_BUF_SIZE 256

#if LOGGING_LEVEL >= 1
#define GALog(fmt, ...) VLog(fmt, ##__VA_ARGS__)
#else
#define GALog(fmt, ...)
#endif

static IOGraphicsAcceleratorInterface ga;
static int ga_initialized = 0;
static io_connect_t this_ga_ctx;
static struct _GAType* this_ga;

typedef struct _GAType {	// GeForce size 176
	IOGraphicsAcceleratorInterface* _interface;
	CFUUIDRef _factoryID;
	UInt32 _refCount;
	io_connect_t _context;			// offset 0xC
	io_service_t _accelerator;		// offset 0x10
	UInt32 _framebufferIndex;		// offset 0x7C
	UInt32 _config_val_1;			// offset 0x80
	UInt32 _config_val_2;			// offset 0x94
} GAType;

#ifdef __cplusplus
extern "C" {
#endif

char VMLog_SendString(char const* str);
static void VLog(char const* fmt, ...);
static GAType* _allocGAType(CFUUIDRef factoryID);
static void _deallocGAType(GAType *myInstance);
static void _buildGAFTbl();
void* VMsvga2GAFactory(CFAllocatorRef allocator, CFUUIDRef typeID);

#ifdef __cplusplus
}
#endif

static void VLog(char const* fmt, ...)
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
#pragma mark IUnknown
#pragma mark -

static HRESULT vmQueryInterface(void *myInstance, REFIID iid, LPVOID *ppv)
{
	GAType* me = static_cast<GAType*>(myInstance);

	CFUUIDRef interfaceID = CFUUIDCreateFromUUIDBytes(NULL, iid);
	if(CFEqual(interfaceID, kIOGraphicsAcceleratorInterfaceID) ||
	   CFEqual(interfaceID, kIOCFPlugInInterfaceID) ||
	   CFEqual(interfaceID, IUnknownUUID)) {
		if (me && me->_interface)
			me->_interface->AddRef(myInstance);
		if (ppv)
			*ppv = myInstance;
		CFRelease(interfaceID);
		return S_OK;
    }
	if (ppv)
	   *ppv = NULL;
	CFRelease(interfaceID);
	return E_NOINTERFACE;
}

static ULONG vmAddRef(void *myInstance)
{
	GAType* me = static_cast<GAType*>(myInstance);

	if (!me)
		return 0;
	++me->_refCount;
	return me->_refCount;
}

static ULONG vmRelease(void *myInstance)
{
	GAType* me = static_cast<GAType*>(myInstance);

	if (!me)
		return 0;
	--me->_refCount;
	if (!me->_refCount) {
		_deallocGAType(me);
		return 0;
    }
	return me->_refCount;
}

#pragma mark -
#pragma mark IOCFPlugin
#pragma mark -

static IOReturn vmProbe(void* myInstance, CFDictionaryRef propertyTable, io_service_t service, SInt32* order)
{
	if (order)
	   *order = 2000;
	return kIOReturnSuccess;
}

static IOReturn vmStart(void* myInstance, CFDictionaryRef propertyTable, io_service_t service)
{
	GAType* me = static_cast<GAType*>(myInstance);
	IOReturn rc;
	io_service_t accelerator = 0;
	io_connect_t context = 0;
	uint32_t output_cnt;
	uint64_t output[2];
	UInt32 input_struct;
	UInt32 output_struct;
	size_t output_struct_cnt;

	if (!me || !service)
		return kIOReturnBadArgument;
	rc = IOAccelFindAccelerator(service, &accelerator, &me->_framebufferIndex);
	if (rc != kIOReturnSuccess)
		return rc;
	rc = IOServiceOpen(accelerator, mach_task_self(), 2 /* 2D Context */, &context);
	if (rc != kIOReturnSuccess)
		goto cleanup;
	me->_accelerator = accelerator;
	me->_context = context;
	output_cnt = 2;
	rc = IOConnectCallMethod(context,
							 1 /* get_config */,
							 0, 0,
							 0, 0,
							 &output[0], &output_cnt,
							 0, 0);
	me->_config_val_1 = static_cast<UInt32>(output[0]);
	if (rc != kIOReturnSuccess)
		goto cleanup;
	input_struct = 2;
	output_struct_cnt = sizeof(output_struct);
	rc = IOConnectCallMethod(context,
							 16 /* read_configs */,
							 0, 0,
							 &input_struct, sizeof(input_struct),
							 0, 0,
							 &output_struct, &output_struct_cnt);
	if (rc != kIOReturnSuccess)
		goto cleanup;
	me->_config_val_2 = output_struct;
	if (vmReset(me, 0) == kIOReturnSuccess)
		goto good_exit;

cleanup:
	if (context)
		IOServiceClose(context);
	if (accelerator)
		IOObjectRelease(accelerator);
good_exit:
	this_ga = me;
	this_ga_ctx = me->_context;
	return rc;
}

static IOReturn vmStop(void* myInstance)
{
	GAType* me = static_cast<GAType*>(myInstance);

	if (!me)
		return kIOReturnBadArgument;
	vmFlush(me, 0);
	this_ga = 0;
	this_ga_ctx = 0;
	if (!me->_accelerator)
		return kIOReturnSuccess;
	if (me->_context)
		IOServiceClose(me->_context);
	IOObjectRelease(me->_accelerator);
	me->_accelerator = 0;
	me->_context = 0;
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark IOGraphicsAccelerator
#pragma mark -

static IOReturn vmReset(void* myInstance, IOOptionBits options)
{
	GAType* me = static_cast<GAType*>(myInstance);

#if LOGGING_LEVEL >= 2
	if (options)
		GALog("VMsvga2GA: Reset(%p, 0x%x)\n", myInstance, options);
#endif

	if (!me || !me->_context)
		return kIOReturnBadArgument;
	useAccelUpdates(me->_context, 1);
	/*
	 * TBD complete the rest
	 */
	return kIOReturnSuccess;
}

static IOReturn vmGetCapabilities(void* myInstance, FourCharCode select, CFTypeRef* capabilities)
{
#if 0
	GAType* me = static_cast<GAType*>(myInstance);
	uint64_t input;
#endif

#if LOGGING_LEVEL >= 2
	GALog("VMsvga2GA: GetCapabilities(%p, 0x%x, %p)\n", myInstance, select, capabilities);
#endif
#if 0
	if (select == IO_FOUR_CHAR_CODE('cgls') ||
		select == kIO32BGRAPixelFormat) {
		*capabilities = kCFBooleanTrue;
		return kIOReturnSuccess;
	}
	if (select == IO_FOUR_CHAR_CODE('smvl')) {
		if (!me || !me->_context)
			return kIOReturnNotReady;
		input = reinterpret_cast<uint64_t>(*capabilities);
		return IOConnectCallMethod(me->_context, 15 /* set_macrovision */,
								   &input, 1,
								   0, 0,
								   0, 0,
								   0, 0);
	}
#endif
	return kIOReturnUnsupported;
}

static IOReturn vmFlush(void* myInstance, IOOptionBits options)
{
#if LOGGING_LEVEL >= 2
	if (options != kIOBlitWaitContext)
		GALog("VMsvga2GA: Flush(%p, 0x%x)\n", myInstance, options);
#endif

	/*
	 * TBD call useAccelUpdates(false) ? (only when called from vmStop())
	 * TBD: complete
	 */
	return kIOReturnSuccess;
}

static IOReturn vmSynchronize(void* myInstance, UInt32 options, UInt32 x, UInt32 y, UInt32 w, UInt32 h)
{
	GAType* me = static_cast<GAType*>(myInstance);
	UInt32 rect[4];

	/*
	 * Note: GeForce code does nothing
	 */

#if LOGGING_LEVEL >= 3
	GALog("VMsvga2GA: Synchronize(%p, 0x%x, %u, %u, %u, %u)\n", myInstance, options, x, y, w, h);
#endif

	if (!(options & kIOBlitSynchronizeFlushHostWrites)) {
#if LOGGING_LEVEL == 2
		GALog("VMsvga2GA: Synchronize(%p, 0x%x, %u, %u, %u, %u)\n", myInstance, options, x, y, w, h);
#endif
		return kIOReturnSuccess;
	}
	if (!me || !me->_context)
		return kIOReturnBadArgument;
	if (w <= x || h <= y)
		return kIOReturnBadArgument;
	rect[0] = x;
	rect[1] = y;
	rect[2] = w - x;
	rect[3] = h - y;
	return UpdateFramebuffer(me->_context, &rect[0]);
}

static IOReturn vmGetBeamPosition(void* myInstance, IOOptionBits options, SInt32* position)
{
#if LOGGING_LEVEL >= 2
	if (options)
		GALog("VMsvga2GA: GetBeamPosition(%p, 0x%x, %p)\n", myInstance, options, position);
#endif

	if (position)
		*position = 0;
	return kIOReturnSuccess;

#if 0
	/*
	 * GeForce code
	 */
	GAType* me = static_cast<GAType*>(myInstance);
	IOReturn rc;
	UInt32 struct_in[2];
	UInt32 struct_out[2];
	size_t struct_out_cnt;

	if (!me || !me->_context)
		return kIOReturnBadArgument;
	struct_out_cnt = sizeof(struct_out);
	struct_in[0] = 144;
	struct_in[1] = me->_framebufferIndex;
	rc = IOConnectCallMethod(me->_context,
						17 /* read_config_Ex */,
						0, 0,
						&struct_in[0], sizeof(struct_in),
						0, 0,
						&struct_out[0], &struct_out_cnt);
	if (position)
		*position = struct_out[1];
	return rc;
#endif
}

static IOReturn vmAllocateSurface(void* myInstance, IOOptionBits options, IOBlitSurface* surface, void* cgsSurfaceID)
{
#if LOGGING_LEVEL >= 2
	GALog("VMsvga2GA: AllocateSurface(%p, 0x%x, %p, %p)\n", myInstance, options, surface, cgsSurfaceID);
#endif
#if LOGGING_LEVEL >= 3
	if (surface)
		for (UInt32 i = 0; i < sizeof(IOBlitSurface) / sizeof(UInt32); ++i) {
			UInt32 v = reinterpret_cast<UInt32 const*>(surface)[i];
			GALog("VMsvga2GA:   surface[%d] == 0x%x\n", i, v);
		}
#endif
	/*
	 * TBD: support
	 */
#if 0
	if (options != kIOBlitHasCGSSurface)
		return kIOReturnUnsupported;
#endif
	return kIOReturnUnsupported;
}

static IOReturn vmFreeSurface(void* myInstance, IOOptionBits options, IOBlitSurface* surface)
{
#if LOGGING_LEVEL >= 2
	GALog("VMsvga2GA: FreeSurface(%p, 0x%x, %p)\n", myInstance, options, surface);
#endif
	/*
	 * TBD: support
	 */
	return kIOReturnUnsupported;
}

static IOReturn vmLockSurface(void* myInstance, IOOptionBits options, IOBlitSurface* surface, vm_address_t* address)
{
#if LOGGING_LEVEL >= 2
	GALog("VMsvga2GA: LockSurface(%p, 0x%x, %p, %p)\n", myInstance, options, surface, address);
#endif
	/*
	 * TBD: support
	 */
	return kIOReturnUnsupported;
}

static IOReturn vmUnlockSurface(void* myInstance, IOOptionBits options, IOBlitSurface* surface, IOOptionBits* swapFlags)
{
#if LOGGING_LEVEL >= 2
	GALog("VMsvga2GA: UnlockSurface(%p, 0x%x, %p, %p)\n", myInstance, options, surface, swapFlags);
#endif
	/*
	 * TBD: support
	 */
	return kIOReturnUnsupported;
}

static IOReturn vmSwapSurface(void* myInstance, IOOptionBits options, IOBlitSurface* surface, IOOptionBits* swapFlags)
{
#if LOGGING_LEVEL >= 2
	GALog("VMsvga2GA: SwapSurface(%p, 0x%x, %p, %p)\n", myInstance, options, surface, swapFlags);
#endif
	/*
	 * TBD: support
	 */
	return kIOReturnUnsupported;
}

static IOReturn vmSetDestination(void* myInstance, IOOptionBits options, IOBlitSurface* surface)
{
	if (options == kIOBlitFramebufferDestination)
		return kIOReturnSuccess;
#if LOGGING_LEVEL >= 2
	GALog("VMsvga2GA: SetDestination(%p, 0x%x, %p)\n", myInstance, options, surface);
#endif
	/*
	 * TBD: support surface destination ??
	 */
	return kIOReturnUnsupported;
}

static IOReturn vmGetBlitter(void* myInstance, IOOptionBits options, IOBlitType type, IOBlitSourceType sourceType, IOBlitterPtr* blitter)
{
	IOReturn rc;

	if (!blitter)
		return kIOReturnBadArgument;
	type &= kIOBlitTypeVerbMask;
	sourceType &= 0x7FFFF000U;
	rc = kIOReturnUnsupported;
	switch (type) {
		case kIOBlitTypeRects:
			switch (sourceType) {
				case kIOBlitSourceSolid:
					*blitter = &vmFill;
					rc = kIOReturnSuccess;
					break;
			}
			break;
		case kIOBlitTypeCopyRects:
			switch (sourceType) {
				case kIOBlitSourceDefault:
				case kIOBlitSourceFramebuffer:
					*blitter = &vmCopy;
					rc = kIOReturnSuccess;
					break;
				case kIOBlitSourceMemory:
					*blitter = &vmMemCopy;
					rc = kIOReturnSuccess;
					break;
			}
			break;
		case kIOBlitTypeCopyRegion:
			switch (sourceType) {
				case kIOBlitSourceDefault:
				case kIOBlitSourceFramebuffer:
					*blitter = &vmCopyRegion;
					rc = kIOReturnSuccess;
					break;
			}
			break;
	}
	/*
	 * Note: GeForce supports a lot of other blitters,
	 *   including some whose type doesn't appear in the header file
	 */
#if LOGGING_LEVEL >= 2
	if (rc != kIOReturnSuccess)
		GALog("VMsvga2GA: GetBlitter(%p, 0x%x, 0x%x, 0x%x, %p)\n", myInstance, options, type, sourceType, blitter);
#endif
	return rc;
}

static IOReturn vmWaitComplete(void* myInstance, IOOptionBits options)
{
	GAType* me = static_cast<GAType*>(myInstance);

	/*
	 * TBD: check GeForce code
	 */
	if (!(options & (kIOBlitWaitAll2D | kIOBlitWaitAll))) {
#if LOGGING_LEVEL >= 2
		GALog("VMsvga2GA: WaitComplete(%p, 0x%x)\n", myInstance, options);
#endif
		return kIOReturnUnsupported;
	}
	if (!me || !me->_context)
		return kIOReturnBadArgument;
	return SyncFIFO(me->_context);
}

static IOReturn vmWaitSurface(void* myInstance, IOOptionBits options, IOBlitSurface* surface)
{
#if LOGGING_LEVEL >= 2
	GALog("VMsvga2GA: WaitSurface(%p, 0x%x, %p)\n", myInstance, options, surface);
#endif
	return kIOReturnUnsupported;
}

static IOReturn vmSetSurface(void* myInstance, IOOptionBits options, IOBlitSurface* surface)
{
#if LOGGING_LEVEL >= 2
	GALog("VMsvga2GA: vmSetSurface(%p, 0x%x, %p)\n", myInstance, options, surface);
#endif
	return kIOReturnUnsupported;
}

#pragma mark -
#pragma mark Blitters
#pragma mark -

static IOReturn vmCopy(void* myInstance, IOOptionBits options, IOBlitType type, IOBlitSourceType sourceType, IOBlitOperation* operation, void* source)
{
	GAType* me = static_cast<GAType*>(myInstance);
	IOBlitCopyRectangles* copy_rects;
	IOReturn rc;

#if LOGGING_LEVEL >= 2
	GALog("VMsvga2GA: Copy(%p, 0x%x, 0x%x, 0x%x, %p, %p)\n", myInstance, options, type, sourceType, operation, source);
#endif

	if (!me || !me->_context)
		return kIOReturnBadArgument;

	if ((type & kIOBlitTypeOperationMask) != kIOBlitCopyOperation)
		return kIOReturnUnsupported;

	copy_rects = reinterpret_cast<IOBlitCopyRectangles*>(operation);
	if (!copy_rects || !copy_rects->count)
		return kIOReturnBadArgument;

#if LOGGING_LEVEL >= 3
	for (IOItemCount i = 0; i < copy_rects->count; ++i) {
		IOBlitCopyRectangle* copy_rect = &copy_rects->rects[i];
		GALog("VMsvga2GA:   Copy Rect %u == [%u, %u, %u, %u, %u, %u]\n",
			  i,
			  copy_rect->sourceX,
			  copy_rect->sourceY,
			  copy_rect->x,
			  copy_rect->y,
			  copy_rect->width,
			  copy_rect->height);
	}
#endif

	rc = RectCopy(me->_context, &copy_rects->rects[0], copy_rects->count * sizeof(IOBlitCopyRectangle));
#if LOGGING_LEVEL >= 2
	GALog("VMsvga2GA:   Copy returns 0x%x\n", rc);
#endif
	return rc;
}

static IOReturn vmFill(void* myInstance, IOOptionBits options, IOBlitType type, IOBlitSourceType sourceType, IOBlitOperation* operation, void* source)
{
	GAType* me = static_cast<GAType*>(myInstance);
	IOBlitRectangles* rects;
	IOReturn rc;

#if LOGGING_LEVEL >= 2
	GALog("VMsvga2GA: Fill(%p, 0x%x, 0x%x, 0x%x, %p, %p)\n", myInstance, options, type, sourceType, operation, source);
#endif

	if (!me || !me->_context)
		return kIOReturnBadArgument;

	if ((type & kIOBlitTypeOperationMask) != kIOBlitCopyOperation)
		return kIOReturnUnsupported;

	/*
	 * Note: source is a 32 bit num
	 *   with source = 0xFFFFFFFF indicates invert
	 *   others apparently solid color fill ???
	 */
	rects = reinterpret_cast<IOBlitRectangles*>(operation);
	if (!rects || !rects->count)
		return kIOReturnBadArgument;

#if LOGGING_LEVEL >= 3
	for (IOItemCount i = 0; i < rects->count; ++i) {
		IOBlitRectangle* rect = &rects->rects[i];
		GALog("VMsvga2GA:   Fill Rect %u == [%u, %u, %u, %u]\n",
			  i,
			  rect->x,
			  rect->y,
			  rect->width,
			  rect->height);
	}
#endif

	rc = RectFill(me->_context, reinterpret_cast<UInt32>(source), &rects->rects[0], rects->count * sizeof(IOBlitRectangle));
#if LOGGING_LEVEL >= 2
	GALog("VMsvga2GA:   Fill returns 0x%x\n", rc);
#endif
	return rc;
}

static IOReturn vmCopyRegion(void* myInstance, IOOptionBits options, IOBlitType type, IOBlitSourceType sourceType, IOBlitOperation* operation, void* source)
{
	GAType* me = static_cast<GAType*>(myInstance);
	IOBlitCopyRegion* copy_region;
	IOAccelDeviceRegion* rgn;
	IOReturn rc;

#if LOGGING_LEVEL >= 3
	IOAccelBounds* rect;
	GALog("VMsvga2GA: CopyRegion(%p, 0x%x, 0x%x, 0x%x, %p, %p)\n", myInstance, options, type, sourceType, operation, source);
#endif

	if (source)		// Note: doesn't support surface source
		return kIOReturnUnsupported;
	if (!me || !me->_context)
		return kIOReturnBadArgument;
	if ((type & kIOBlitTypeOperationMask) != kIOBlitTypeOperationType0)
		return kIOReturnUnsupported;
	copy_region = reinterpret_cast<IOBlitCopyRegion*>(operation);
	if (!copy_region)
		return kIOReturnBadArgument;
	rgn = copy_region->region;
#if LOGGING_LEVEL >= 3
	GALog("VMsvga2GA:   CopyRegion deltaX == %d, deltaY == %d, region->num_rects == %d\n",
		  copy_region->deltaX,
		  copy_region->deltaY,
		  rgn ? rgn->num_rects : -1);
#endif
	if (!rgn)
		return kIOReturnBadArgument;
#if LOGGING_LEVEL >= 3
	rect = &rgn->bounds;
	GALog("VMsvga2GA:   CopyRegion bounds == [%d, %d, %d, %d]\n", rect->x, rect->y, rect->w, rect->h);
	for (UInt32 i = 0; i < rgn->num_rects; ++i) {
		rect = &rgn->rect[i];
		GALog("VMsvga2GA:   CopyRegion rect %d == [%d, %d, %d, %d]\n", i, rect->x, rect->y, rect->w, rect->h);
	}
#endif

	rc = RegionCopy(me->_context, copy_region->deltaX, copy_region->deltaY, rgn, IOACCEL_SIZEOF_DEVICE_REGION(rgn));

#if LOGGING_LEVEL >= 3
	GALog("VMsvga2GA:   CopyRegion returns 0x%x\n", rc);
#endif

	return rc;
}

static IOReturn vmMemCopy(void* myInstance, IOOptionBits options, IOBlitType type, IOBlitSourceType sourceType, IOBlitOperation* operation, void* source)
{
#if LOGGING_LEVEL >= 2
	GALog("VMsvga2GA: MemCopy(%p, 0x%x, 0x%x, 0x%x, %p, %p)\n", myInstance, options, type, sourceType, operation, source);
#endif
	/*
	 * TBD: memory to framebuffer copy
	 */
	return kIOReturnUnsupported;
}

#pragma mark -
#pragma mark misc
#pragma mark -

static GAType* _allocGAType(CFUUIDRef factoryID)
{
	GAType *newOne = static_cast<GAType*>(malloc(sizeof(GAType)));
	if (!newOne)
		return 0;

	memset(newOne, 0, sizeof(GAType));
	_buildGAFTbl();
    newOne->_interface = &ga;

	if (factoryID) {
		newOne->_factoryID = static_cast<CFUUIDRef>(CFRetain(factoryID));
		CFPlugInAddInstanceForFactory(factoryID);
	}
	newOne->_refCount = 1;
	return newOne;
}

static void _deallocGAType(GAType *myInstance)
{
	CFUUIDRef factoryID;

	if (!myInstance)
		return;
	factoryID = myInstance->_factoryID;
	free(myInstance);
	if (factoryID) {
		CFPlugInRemoveInstanceForFactory(factoryID);
		CFRelease(factoryID);
	}
}

static void _buildGAFTbl()
{
	if (ga_initialized)
		return;
	memset(&ga, 0, sizeof(ga));
	ga.QueryInterface = vmQueryInterface;
	ga.AddRef = vmAddRef;
	ga.Release = vmRelease;
	ga.version = kCurrentGraphicsInterfaceVersion;
	ga.revision = kCurrentGraphicsInterfaceRevision;
	ga.Probe = vmProbe;
	ga.Start = vmStart;
	ga.Stop = vmStop;
	ga.Reset = vmReset;
	ga.CopyCapabilities = vmGetCapabilities;
	ga.Flush = vmFlush;
	ga.Synchronize = vmSynchronize;
	ga.GetBeamPosition = vmGetBeamPosition;
	ga.AllocateSurface = vmAllocateSurface;
	ga.FreeSurface = vmFreeSurface;
	ga.LockSurface = vmLockSurface;
	ga.UnlockSurface = vmUnlockSurface;
	ga.SwapSurface = vmSwapSurface;
	ga.SetDestination = vmSetDestination;
	ga.GetBlitter = vmGetBlitter;
	ga.WaitComplete = vmWaitComplete;
	ga.__gaInterfaceReserved[0] = reinterpret_cast<void*>(vmWaitSurface);
	ga.__gaInterfaceReserved[1] = reinterpret_cast<void*>(vmSetSurface);
	/*
	 * 22 slots reserved from here.
	 */
	ga_initialized = 1;
}

void* VMsvga2GAFactory(CFAllocatorRef allocator, CFUUIDRef typeID)
{
	if (CFEqual(typeID, kIOGraphicsAcceleratorTypeID))
		return _allocGAType(kVMGAFactoryID);
	return NULL;
}
