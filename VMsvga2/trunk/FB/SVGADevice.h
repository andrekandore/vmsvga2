/*
 *  SVGADevice.h
 *  VMsvga2
 *
 *  Created by Zenith432 on July 2nd 2009.
 *  Copyright 2009-2010 Zenith432. All rights reserved.
 *
 */

/**********************************************************
 * Portions Copyright 2009 VMware, Inc.  All rights reserved.
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

#ifndef __SVGADEVICE_H__
#define __SVGADEVICE_H__

#include <stdint.h>
#include <sys/types.h>
class IOPCIDevice;
class IODeviceMemory;
class IOMemoryMap;

class SVGADevice
{
private:
	IOPCIDevice* m_provider;	// offset 0
	IOMemoryMap* m_bar0;		// offset 4
	IOMemoryMap* m_bar2;		// offset 8
	uint32_t* m_fifo_ptr;		// offset 12
	void* m_cursor_ptr;			// offset 16
	uint32_t m_fifo_size;		// offset 20
	uint8_t* m_bounce_buffer;	// offset 24
	bool m_using_bounce_buffer;	// offset 28
	size_t m_reserved_size;		// offset 32
	uint32_t m_next_fence;		// offset 36

	/*
	 * Begin Added
	 */
	uint32_t m_capabilities;
	uint32_t m_width;
	uint32_t m_height;
	uint32_t m_pitch;
	uint32_t m_max_width;
	uint32_t m_max_height;
	uint32_t m_max_gmr_ids;
	uint32_t m_max_gmr_descriptor_length;
	uint32_t m_fb_offset;
	uint32_t m_vram_size;
	uint32_t m_fb_size;
	/*
	 * End Added
	 */

	void FIFOFull();

public:
	bool Init();
	uint32_t ReadReg(uint32_t index);
	void WriteReg(uint32_t index, uint32_t value);
	void Cleanup();
	IODeviceMemory* Start(IOPCIDevice* provider);
	void Disable();				// Added

	/*
	 * FIFO Stuff
	 */
	bool IsFIFORegValid(uint32_t reg) const;	// Added
	bool HasFIFOCap(uint32_t mask) const;
	bool FIFOInit();
	void* FIFOReserve(size_t bytes);
	void* FIFOReserveCmd(uint32_t type, size_t bytes);
	void* FIFOReserveEscape(uint32_t nsid, size_t bytes);		// Added
	void FIFOCommit(size_t bytes);
	void FIFOCommitAll();

	/*
	 * Fence Stuff
	 */
	uint32_t InsertFence();
	bool HasFencePassed(uint32_t fence) const;
	void SyncToFence(uint32_t fence);
	void RingDoorBell();		// Added
	void SyncFIFO();			// Added

	/*
	 * Cursor Stuff
	 */
	void setCursorState(uint32_t x, uint32_t y, bool visible);
	void setCursorState(uint32_t screenId, uint32_t x, uint32_t y, bool visible);
	void* BeginDefineAlphaCursor(uint32_t width, uint32_t height, uint32_t bytespp);
	bool EndDefineAlphaCursor(uint32_t width, uint32_t height, uint32_t bytespp, uint32_t hotspot_x, uint32_t hotspot_y);

	void SetMode(uint32_t width, uint32_t height, uint32_t bpp);

	bool UpdateFramebuffer(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
	bool UpdateFullscreen() { return UpdateFramebuffer(0, 0, m_width, m_height); }			// Added

	/*
	 * Video Stuff (Added)
	 */
	bool BeginVideoSetRegs(uint32_t streamId, size_t numItems, struct SVGAEscapeVideoSetRegs **setRegs);
	bool VideoSetRegsInRange(uint32_t streamId, struct SVGAOverlayUnit const* regs, uint32_t minReg, uint32_t maxReg);
	bool VideoSetRegsWithMask(uint32_t streamId, struct SVGAOverlayUnit const* regs, uint32_t regMask);
	bool VideoSetReg(uint32_t streamId, uint32_t registerId, uint32_t value);
	bool VideoFlush(uint32_t streamId);

	/*
	 * New Stuff (Added)
	 */
	bool HasCapability(uint32_t mask) const { return (m_capabilities & mask) != 0; }
	uint32_t getCurrentWidth() const { return m_width; }
	uint32_t getCurrentHeight() const { return m_height; }
	uint32_t getCurrentPitch() const { return m_pitch; }
	uint32_t getMaxWidth() const { return m_max_width; }
	uint32_t getMaxHeight() const { return m_max_height; }
	uint32_t getMaxGMRIDs() const { return m_max_gmr_ids; }
	uint32_t getMaxGMRDescriptorLength() const { return m_max_gmr_descriptor_length; }
	uint32_t getCurrentFBOffset() const { return m_fb_offset; }
	uint32_t getVRAMSize() const { return m_vram_size; }
	uint32_t getCurrentFBSize() const { return m_fb_size; }
	bool get3DHWVersion(UInt32* HWVersion);
	void RegDump();

	bool RectCopy(UInt32 const* copyRect);					// copyRect is an array of 6 UInt32 - same order as SVGAFifoCmdRectCopy
	bool RectFill(UInt32 color, UInt32 const* rect);		// rect is an array of 4 UInt32 - same order as SVGAFifoCmdFrontRopFill
	bool UpdateFramebuffer2(UInt32 const* rect);			// rect is an array of 4 UInt32 - same order as SVGAFifoCmdUpdate

#ifdef TESTING
	static void test_ram_size(char const* name, IOVirtualAddress ptr, IOByteCount count);
#endif
};

#endif /* __SVGADEVICE_H__ */
