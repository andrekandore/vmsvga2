/*
 *  VMsvga22DContext.cpp
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on August 10th 2009.
 *  Copyright 2009 Zenith432. All rights reserved.
 *
 */

#include <IOKit/IOLib.h>
#include "VMsvga2Accel.h"
#include "VMsvga22DContext.h"

#define CLASS VMsvga22DContext
#define super IOUserClient
OSDefineMetaClassAndStructors(VMsvga22DContext, IOUserClient);

#define NUM_2D_METHODS 28
#define VM_METHODS_START 22

static IOExternalMethod iofbFuncsCache[NUM_2D_METHODS] =
{
// Note: methods from IONV2DContext
{0, reinterpret_cast<IOMethod>(&CLASS::set_surface), kIOUCScalarIStructO, 2, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::get_config), kIOUCScalarIScalarO, 0, 2},
{0, reinterpret_cast<IOMethod>(&CLASS::get_surface_info1), kIOUCScalarIStructO, 2, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::swap_surface), kIOUCScalarIScalarO, 1, 1},
{0, reinterpret_cast<IOMethod>(&CLASS::scale_surface), kIOUCScalarIScalarO, 3, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::lock_memory), kIOUCScalarIScalarO, 1, 2},
{0, reinterpret_cast<IOMethod>(&CLASS::unlock_memory), kIOUCScalarIScalarO, 1, 1},
{0, reinterpret_cast<IOMethod>(&CLASS::finish), kIOUCScalarIScalarO, 1, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::declare_image), kIOUCScalarIScalarO, 3, 1},
{0, reinterpret_cast<IOMethod>(&CLASS::create_image), kIOUCScalarIScalarO, 2, 2},
{0, reinterpret_cast<IOMethod>(&CLASS::create_transfer), kIOUCScalarIScalarO, 2, 2},
{0, reinterpret_cast<IOMethod>(&CLASS::delete_image), kIOUCScalarIScalarO, 1, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::wait_image), kIOUCScalarIScalarO, 1, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::set_surface_paging_options), kIOUCStructIStructO, 12, 12},
{0, reinterpret_cast<IOMethod>(&CLASS::set_surface_vsync_options), kIOUCStructIStructO, 12, 12},
{0, reinterpret_cast<IOMethod>(&CLASS::set_macrovision), kIOUCScalarIScalarO, 1, 0},
// Note: Methods from NV2DContext
{0, reinterpret_cast<IOMethod>(&CLASS::read_configs), kIOUCStructIStructO, kIOUCVariableStructureSize, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::read_config_Ex), kIOUCStructIStructO, kIOUCVariableStructureSize, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::write_configs), kIOUCScalarIStructI, 0, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::write_config_Ex), kIOUCScalarIStructI, 0, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::get_surface_info2), kIOUCStructIStructO, kIOUCVariableStructureSize, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::kernel_printf), kIOUCScalarIStructI, 0, kIOUCVariableStructureSize},
// Note: VM Methods
{0, reinterpret_cast<IOMethod>(&VMsvga2Accel::useAccelUpdates), kIOUCScalarIScalarO, 1, 0},
{0, reinterpret_cast<IOMethod>(&VMsvga2Accel::RectCopy), kIOUCScalarIStructI, 0, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&VMsvga2Accel::RectFill), kIOUCScalarIStructI, 1, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&VMsvga2Accel::UpdateFramebuffer), kIOUCScalarIStructI, 0, 4 * sizeof(UInt32)},
{0, reinterpret_cast<IOMethod>(&VMsvga2Accel::SyncFIFO), kIOUCScalarIScalarO, 0, 0},
{0, reinterpret_cast<IOMethod>(&VMsvga2Accel::RegionCopy), kIOUCScalarIStructI, 2, kIOUCVariableStructureSize}
};

#pragma mark -
#pragma mark IOUserClient Methods
#pragma mark -

IOExternalMethod* CLASS::getTargetAndMethodForIndex(IOService** targetP, UInt32 index)
{
	if (!targetP || index >= NUM_2D_METHODS)
		return 0;
	if (index >= VM_METHODS_START) {
		if (m_provider)
			*targetP = m_provider;
		else
			return 0;
	} else
		*targetP = this;
	return &m_funcs_cache[index];
}

IOReturn CLASS::clientClose()
{
#if LOGGING_LEVEL >= 2
	IOLog("VMsvga22DContext: clientClose\n");
#endif
	if (!terminate(0))
		IOLog("%s: terminate failed\n", __FUNCTION__);
	m_owning_task = 0;
	m_provider = 0;
	return kIOReturnSuccess;
}

bool CLASS::start(IOService* provider)
{
	m_provider = OSDynamicCast(VMsvga2Accel, provider);
	if (!m_provider) {
		return false;
	}
	return super::start(provider);
}

void CLASS::stop(IOService* provider)
{
	return super::stop(provider);
}

bool CLASS::initWithTask(task_t owningTask, void* securityToken, UInt32 type)
{
	m_owning_task = 0;
	m_provider = 0;
	m_funcs_cache = 0;
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

#pragma mark -
#pragma mark IONV2DContext Methods
#pragma mark -

IOReturn CLASS::set_surface(unsigned long, eIOContextModeBits, void *, unsigned long *)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::get_config(unsigned long * p1, unsigned long * p2)
{
	if (!p1 || !p2)
		return kIOReturnBadArgument;
	*p1 = 0;
	*p2 = 0;
	return kIOReturnSuccess;
}

IOReturn CLASS::get_surface_info1(unsigned long, eIOContextModeBits, void *, unsigned long *)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::swap_surface(unsigned long, unsigned long *)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::scale_surface(unsigned long, unsigned long, unsigned long)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::lock_memory(unsigned long, unsigned int *, unsigned long *)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::unlock_memory(unsigned long, unsigned long *)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::finish(unsigned long)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::declare_image(unsigned long, unsigned int, unsigned long, unsigned int *)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::create_image(unsigned long, unsigned long, unsigned int *, unsigned int *)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::create_transfer(unsigned long, unsigned long, unsigned int *, unsigned int *)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::delete_image(unsigned long)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::wait_image(unsigned long)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::set_surface_paging_options(IOSurfacePagingControlInfoStruct *, IOSurfacePagingControlInfoStruct *, unsigned long, unsigned long *)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::set_surface_vsync_options(IOSurfaceVsyncControlInfoStruct *, IOSurfaceVsyncControlInfoStruct *, unsigned long, unsigned long *)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::set_macrovision(unsigned long)
{
	return kIOReturnUnsupported;
}

#pragma mark -
#pragma mark NV2DContext Methods
#pragma mark -

IOReturn CLASS::read_configs(unsigned long* p1, unsigned long *p2, unsigned long p3, unsigned long* p4)
{
	if (!p1 || !p2 || !p4)
		return kIOReturnBadArgument;
	if (p3 != sizeof(UInt32) || *p4 != sizeof(UInt32))
		return kIOReturnBadArgument;
	if (*p1 == 2)
		*p2 = 2;
	else
		*p2 = 0;
	return kIOReturnSuccess;
}

IOReturn CLASS::read_config_Ex(unsigned long *, unsigned long *, unsigned long, unsigned long *)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::write_configs(unsigned long *, unsigned long)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::write_config_Ex(unsigned long *, unsigned long)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::get_surface_info2(unsigned long *, unsigned long *, unsigned long, unsigned long *)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::kernel_printf(char *, unsigned long)
{
	return kIOReturnUnsupported;
}
