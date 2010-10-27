
#include <CoreAudio/AudioDriverPlugIn.h>
#include <IOKit/IOKitLib.h>

static __attribute__((used)) char const copyright[] = "Copyright 2009-2010 Zenith432";

OSStatus AudioDriverPlugInOpen(AudioDriverPlugInHostInfo* inHostInfo)
{
	AudioObjectPropertyAddress addr;
	CFNumberRef val;
	UInt32 num = 4096U;

	addr.mSelector = kAudioDevicePropertyBufferFrameSize;
	addr.mScope = kAudioDevicePropertyScopeOutput;
	addr.mElement = 0;

	val = IORegistryEntryCreateCFProperty(inHostInfo->mIOAudioEngine,
										  CFSTR("IOAudioEngineCoreAudioBufferFrameSize"),
										  kCFAllocatorDefault,
										  kNilOptions);
	if (val) {
		CFNumberGetValue(val, kCFNumberSInt32Type, &num);
		CFRelease(val);
	}
	AudioObjectSetPropertyData((AudioObjectID) inHostInfo->mDeviceID,
							   &addr,
							   0,
							   NULL,
							   sizeof num,
							   &num);

	return kAudioHardwareNoError;
}

OSStatus AudioDriverPlugInClose(AudioDeviceID inDevice)
{
	return kAudioHardwareNoError;
}

OSStatus AudioDriverPlugInDeviceGetPropertyInfo(AudioDeviceID           inDevice,
												UInt32                  inChannel,
												Boolean                 isInput,
												AudioDevicePropertyID   inPropertyID,
												UInt32*                 outSize,
												Boolean*                outWritable)
{
	return kAudioHardwareUnknownPropertyError;
}

OSStatus AudioDriverPlugInDeviceGetProperty(AudioDeviceID           inDevice,
											UInt32                  inChannel,
											Boolean                 isInput,
											AudioDevicePropertyID   inPropertyID,
											UInt32*                 ioPropertyDataSize,
											void*                   outPropertyData)
{
	return kAudioHardwareUnknownPropertyError;
}

OSStatus AudioDriverPlugInDeviceSetProperty(AudioDeviceID           inDevice,
											const AudioTimeStamp*   inWhen,
											UInt32                  inChannel,
											Boolean                 isInput,
											AudioDevicePropertyID   inPropertyID,
											UInt32                  inPropertyDataSize,
											const void*             inPropertyData)
{
	return kAudioHardwareUnknownPropertyError;
}

OSStatus AudioDriverPlugInStreamGetPropertyInfo(AudioDeviceID           inDevice,
												io_object_t             inIOAudioStream,
												UInt32                  inChannel,
												AudioDevicePropertyID   inPropertyID,
												UInt32*                 outSize,
												Boolean*                outWritable)
{
	return kAudioHardwareUnknownPropertyError;
}

OSStatus AudioDriverPlugInStreamGetProperty(AudioDeviceID           inDevice,
											io_object_t             inIOAudioStream,
											UInt32                  inChannel,
											AudioDevicePropertyID   inPropertyID,
											UInt32*                 ioPropertyDataSize,
											void*                   outPropertyData)
{
	return kAudioHardwareUnknownPropertyError;
}

OSStatus AudioDriverPlugInStreamSetProperty(AudioDeviceID           inDevice,
											io_object_t             inIOAudioStream,
											const AudioTimeStamp*   inWhen,
											UInt32                  inChannel,
											AudioDevicePropertyID   inPropertyID,
											UInt32                  inPropertyDataSize,
											const void*             inPropertyData)
{
	return kAudioHardwareUnknownPropertyError;
}
