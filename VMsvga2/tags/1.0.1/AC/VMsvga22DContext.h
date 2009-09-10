/*
 *  VMsvga22DContext.h
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on August 10th 2009.
 *  Copyright 2009 Zenith432. All rights reserved.
 *
 */

#ifndef __VMSVGA22DCONTEXT_H__
#define __VMSVGA22DCONTEXT_H__

#include <IOKit/IOUserClient.h>

typedef unsigned long eIOContextModeBits;
struct IOSurfacePagingControlInfoStruct;
struct IOSurfaceVsyncControlInfoStruct;

class VMsvga22DContext: public IOUserClient
{
	OSDeclareDefaultStructors(VMsvga22DContext);

private:
	task_t m_owning_task;
	class VMsvga2Accel* m_provider;
	IOExternalMethod* m_funcs_cache;

public:
	IOExternalMethod* getTargetAndMethodForIndex(IOService** targetP, UInt32 index);
	IOReturn clientClose();
	bool start(IOService* provider);
	void stop(IOService* provider);
	bool initWithTask(task_t owningTask, void* securityToken, UInt32 type);
	static VMsvga22DContext* withTask(task_t owningTask, void* securityToken, UInt32 type);

	/*
	 * Methods corresponding to Apple's GeForce 2D Context User Client
	 */
	IOReturn set_surface(unsigned long, eIOContextModeBits, void *, unsigned long *);
	IOReturn get_config(unsigned long *, unsigned long *);
	IOReturn get_surface_info1(unsigned long, eIOContextModeBits, void *, unsigned long *);
	IOReturn swap_surface(unsigned long, unsigned long *);
	IOReturn scale_surface(unsigned long, unsigned long, unsigned long);
	IOReturn lock_memory(unsigned long, unsigned int *, unsigned long *);
	IOReturn unlock_memory(unsigned long, unsigned long *);
	IOReturn finish(unsigned long);
	IOReturn declare_image(unsigned long, unsigned int, unsigned long, unsigned int *);
	IOReturn create_image(unsigned long, unsigned long, unsigned int *, unsigned int *);
	IOReturn create_transfer(unsigned long, unsigned long, unsigned int *, unsigned int *);
	IOReturn delete_image(unsigned long);
	IOReturn wait_image(unsigned long);
	IOReturn set_surface_paging_options(IOSurfacePagingControlInfoStruct *, IOSurfacePagingControlInfoStruct *, unsigned long, unsigned long *);
	IOReturn set_surface_vsync_options(IOSurfaceVsyncControlInfoStruct *, IOSurfaceVsyncControlInfoStruct *, unsigned long, unsigned long *);
	IOReturn set_macrovision(unsigned long);

	IOReturn read_configs(unsigned long *, unsigned long *, unsigned long, unsigned long *);
	IOReturn read_config_Ex(unsigned long *, unsigned long *, unsigned long, unsigned long *);
	IOReturn write_configs(unsigned long *, unsigned long);
	IOReturn write_config_Ex(unsigned long *, unsigned long);
	IOReturn get_surface_info2(unsigned long *, unsigned long *, unsigned long, unsigned long *);
	IOReturn kernel_printf(char *, unsigned long);
};

#endif /* __VMSVGA22DCONTEXT_H__ */
