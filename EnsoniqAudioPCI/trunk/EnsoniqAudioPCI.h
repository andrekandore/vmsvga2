/*-
 * EnsoniqAudioPCI.h
 * EnsoniqAudioPCI
 *
 * Created by Zenith432 on July 14th 2009.
 *
 * This supports the ENSONIQ AudioPCI board based on the ES1370.
 *
 * Copyright (c) 2009 Zenith432.
 * Copyright (c) 2005 Maxxuss.
 * Copyright (c) 1998 Joachim Kuebart <joki@kuebart.stuttgart.netsurf.de>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Absolutely no warranty of function or purpose is made by the author
 *    Joachim Kuebart.
 * 4. Modifications may be freely made to this file if the above conditions
 *    are met.
 *
 * Some of the code was adapted from AppleAC97AudioAMDCS5535.h and is subject
 *   to the APSL Version 1.1, see http://www.opensource.apple.com/apsl
 */

#ifndef __ENSONIQ_AUDIOPCI_H__
#define __ENSONIQ_AUDIOPCI_H__

#include <IOAC97Controller.h>

class IOPCIDevice;
class IOWorkLoop;
class IOFilterInterruptEventSource;
class IOAC97CodecDevice;
class IOAudioEngine;
struct DMAEngineState;

class EnsoniqAudioPCI : public IOAC97Controller
{
	OSDeclareDefaultStructors(EnsoniqAudioPCI);

private:
	IOPCIDevice* fPCI;									// offset 0x58 start
	IOWorkLoop* fWorkLoop;								// offset 0x5C
	IOFilterInterruptEventSource* fInterruptSource;		// offset 0x60
	bool fACLinkPowerDown;								// offset 0x64
	DMAEngineState* fDMAState;							// offset 0x68
	UInt16 fIOBase;										// offset 0x6C
	thread_call_t fSetPowerStateThreadCall;				// offset 0x70
	IOAC97CodecDevice* fCodecs[kIOAC97MaxCodecCount];	// offset 0x74, 0x78, 0x7C, 0x80
	IOOptionBits fBusyOutputSlots;						// offset 0x84
	UInt32 ctrl;										// offset 0x88
	UInt32 sctrl;										// offset 0x8C end

	IOByteCount fFrameCountCache[3];
	thread_call_t fEngineThreadCall;
	IOAudioEngine* fEnginePCMOut;

	UInt32 es_rd(int regno, int size);
	void es_wr(int regno, UInt32 data, int size);
	UInt32 es1371_wait_src_ready();
	void es1371_src_write(UInt16 reg, UInt16 data);
	UInt es1371_src_read(UInt16 reg);
	UInt es1371_dac_rate(UInt rate, int channel);
	UInt es1371_adc_rate(UInt rate, int channel);
	int es1371_init();
	int es1371_wrcd(int addr, UInt32 data);
	int es1371_rdcd(int addr);
	UInt eschan_prepare(int channel, UInt32 snd_dbuf, UInt32 bufsz, UInt32 cnt, UInt format, UInt rate);
	int eschan_trigger(int channel, int go);
	IOByteCount eschan_getptr(int channel);
	IOByteCount eschan_getSampleCounter(int channel);
	UInt32 es_intr();

	static void handleSetPowerState(thread_call_param_t param0, thread_call_param_t param1);
	static bool interruptFilter(OSObject* owner, IOFilterInterruptEventSource* source);
	static void interruptOccurred(OSObject* owner, IOInterruptEventSource* source, int count);
	static void engineThreadCall(thread_call_param_t param0, thread_call_param_t param1);
	static IOReturn engineAction(OSObject* target, void* arg0, void* arg1, void* arg2, void* arg3);
	bool configureProvider(IOService* provider);
	void processBootOptions();
	void resetACLink(IOOptionBits type) { es1371_init(); }
	IOItemCount attachCodecDevices();
	void publishCodecDevices();
	IOAC97CodecDevice* createCodecDevice(IOAC97CodecID codecID);
	static bool selectDMAEngineForConfiguration(IOAC97AudioConfig* config);
	bool selectSlotMapsForConfiguration(IOAC97AudioConfig* config);
	bool waitCodecReady(IOAC97CodecID codecID);
	bool hwActivateConfiguration(IOAC97AudioConfig const* config);
	void hwDeactivateConfiguration(IOAC97AudioConfig const* config);

public:
	IOReturn startDMAEngine(IOAC97DMAEngineID engine, IOOptionBits options = 0);
	void stopDMAEngine(IOAC97DMAEngineID engine);
	IOByteCount getDMAEngineHardwarePointer(IOAC97DMAEngineID engine);
	IOReturn codecRead(IOAC97CodecID codec, IOAC97CodecOffset offset, IOAC97CodecWord* word);
	IOReturn codecWrite(IOAC97CodecID codec, IOAC97CodecOffset offset, IOAC97CodecWord word);

	bool init(OSDictionary* dictionary = 0);
	bool start(IOService* provider);
	void stop(IOService* provider);
	void free();
	IOWorkLoop* getWorkLoop() const { return fWorkLoop; }
	IOReturn setPowerState(unsigned long powerStateOrdinal, IOService* whatDevice);

	IOReturn prepareAudioConfiguration(IOAC97AudioConfig* config);
	IOReturn activateAudioConfiguration(IOAC97AudioConfig* config, void* target = 0, IOAC97DMAEngineAction action = 0, void* param = 0);
	void deactivateAudioConfiguration(IOAC97AudioConfig* config);
};

#endif /* __ENSONIQ_AUDIOPCI_H__ */
