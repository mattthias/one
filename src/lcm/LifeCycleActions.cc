/* -------------------------------------------------------------------------- */
/* Copyright 2002-2015, OpenNebula Project (OpenNebula.org), C12G Labs        */
/*                                                                            */
/* Licensed under the Apache License, Version 2.0 (the "License"); you may    */
/* not use this file except in compliance with the License. You may obtain    */
/* a copy of the License at                                                   */
/*                                                                            */
/* http://www.apache.org/licenses/LICENSE-2.0                                 */
/*                                                                            */
/* Unless required by applicable law or agreed to in writing, software        */
/* distributed under the License is distributed on an "AS IS" BASIS,          */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   */
/* See the License for the specific language governing permissions and        */
/* limitations under the License.                                             */
/* -------------------------------------------------------------------------- */

#include "LifeCycleManager.h"
#include "TransferManager.h"
#include "DispatchManager.h"
#include "VirtualMachineManager.h"

void  LifeCycleManager::deploy_action(int vid)
{
    VirtualMachine *    vm;
    ostringstream       os;

    vm = vmpool->get(vid,true);

    if ( vm == 0 )
    {
        return;
    }

    if ( vm->get_state() == VirtualMachine::ACTIVE )
    {
        time_t thetime = time(0);
        int    cpu,mem,disk;

        VirtualMachine::LcmState vm_state;
        TransferManager::Actions tm_action;

        //----------------------------------------------------
        //                 PROLOG STATE
        //----------------------------------------------------

        vm->get_requirements(cpu,mem,disk);

        vm_state  = VirtualMachine::PROLOG;
        tm_action = TransferManager::PROLOG;

        if (vm->hasPreviousHistory())
        {
            if (vm->get_previous_action() == History::STOP_ACTION)
            {
                vm_state  = VirtualMachine::PROLOG_RESUME;
                tm_action = TransferManager::PROLOG_RESUME;
            }
            else if (vm->get_previous_action() == History::UNDEPLOY_ACTION ||
                     vm->get_previous_action() == History::UNDEPLOY_HARD_ACTION)
            {
                vm_state  = VirtualMachine::PROLOG_UNDEPLOY;
                tm_action = TransferManager::PROLOG_RESUME;
            }
        }

        vm->set_state(vm_state);

        vmpool->update(vm);

        vm->set_stime(thetime);

        vm->set_prolog_stime(thetime);

        vmpool->update_history(vm);

        //----------------------------------------------------

        if (hpool->add_capacity(vm->get_hid(),vm->get_oid(),cpu,mem,disk) == -1)
        {
            //The host has been deleted, move VM to FAILURE
            this->trigger(LifeCycleManager::PROLOG_FAILURE, vid);
        }
        else
        {
            tm->trigger(tm_action, vid);
        }
    }
    else
    {
        vm->log("LCM", Log::ERROR, "deploy_action, VM in a wrong state.");
    }

    vm->unlock();

    return;
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

void  LifeCycleManager::suspend_action(int vid)
{
    VirtualMachine *    vm;

    vm = vmpool->get(vid,true);

    if ( vm == 0 )
    {
        return;
    }

    if (vm->get_state()     == VirtualMachine::ACTIVE &&
        vm->get_lcm_state() == VirtualMachine::RUNNING)
    {
        //----------------------------------------------------
        //                SAVE_SUSPEND STATE
        //----------------------------------------------------

        vm->set_state(VirtualMachine::SAVE_SUSPEND);

        vm->set_resched(false);

        vmpool->update(vm);

        vm->set_action(History::SUSPEND_ACTION);

        vmpool->update_history(vm);

        //----------------------------------------------------

        vmm->trigger(VirtualMachineManager::SAVE,vid);
    }
    else
    {
        vm->log("LCM", Log::ERROR, "suspend_action, VM in a wrong state.");
    }

    vm->unlock();

    return;
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

void  LifeCycleManager::stop_action(int vid)
{
    VirtualMachine * vm = vmpool->get(vid,true);

    if ( vm == 0 )
    {
        return;
    }

    if (vm->get_state()     == VirtualMachine::ACTIVE &&
        vm->get_lcm_state() == VirtualMachine::RUNNING)
    {
        //----------------------------------------------------
        //                SAVE_STOP STATE
        //----------------------------------------------------

        vm->set_state(VirtualMachine::SAVE_STOP);

        vm->set_resched(false);

        vmpool->update(vm);

        vm->set_action(History::STOP_ACTION);

        vmpool->update_history(vm);

        //----------------------------------------------------

        vmm->trigger(VirtualMachineManager::SAVE,vid);
    }
    else if (vm->get_state() == VirtualMachine::SUSPENDED)
    {
        //----------------------------------------------------
        //   Bypass SAVE_STOP
        //----------------------------------------------------
        vm->set_state(VirtualMachine::ACTIVE);
        vm->set_state(VirtualMachine::EPILOG_STOP);

        vmpool->update(vm);

        vm->set_action(History::STOP_ACTION);

        vm->set_epilog_stime(time(0));

        vmpool->update_history(vm);

        //----------------------------------------------------

        tm->trigger(TransferManager::EPILOG_STOP,vid);
    }
    else
    {
        vm->log("LCM", Log::ERROR, "stop_action, VM in a wrong state.");
    }

    vm->unlock();

    return;
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

void  LifeCycleManager::migrate_action(int vid)
{
    VirtualMachine *    vm;

	int    cpu, mem, disk;
	time_t the_time = time(0);

    vm = vmpool->get(vid,true);

    if ( vm == 0 )
    {
        return;
    }

    if (vm->get_state()     == VirtualMachine::ACTIVE &&
        vm->get_lcm_state() == VirtualMachine::RUNNING)
    {
        //----------------------------------------------------
        //                SAVE_MIGRATE STATE
        //----------------------------------------------------

        vm->set_state(VirtualMachine::SAVE_MIGRATE);

        vm->set_resched(false);

        vmpool->update(vm);

        vm->set_stime(the_time);

        vm->set_previous_action(History::MIGRATE_ACTION);

        vmpool->update_history(vm);

        vm->get_requirements(cpu,mem,disk);

        hpool->add_capacity(vm->get_hid(), vm->get_oid(), cpu, mem, disk);

        //----------------------------------------------------

        vmm->trigger(VirtualMachineManager::SAVE,vid);
    }
    else if (vm->get_state() == VirtualMachine::POWEROFF ||
        vm->get_state() == VirtualMachine::SUSPENDED )
    {
        //------------------------------------------------------
        //   Bypass SAVE_MIGRATE & go to PROLOG_MIGRATE_POWEROFF
        //------------------------------------------------------
        if (vm->get_state() == VirtualMachine::POWEROFF)
		{
			vm->set_state(VirtualMachine::PROLOG_MIGRATE_POWEROFF);
		}
		else // VirtualMachine::SUSPENDED
		{
			vm->set_state(VirtualMachine::PROLOG_MIGRATE_SUSPEND);
		}

        vm->set_state(VirtualMachine::ACTIVE);

        vm->set_resched(false);

        vm->delete_snapshots();

        vm->reset_info();

        vmpool->update(vm);

        vm->set_stime(the_time);

        vm->set_prolog_stime(the_time);

        vmpool->update_history(vm);

        vm->get_requirements(cpu,mem,disk);

        hpool->add_capacity(vm->get_hid(), vm->get_oid(), cpu, mem, disk);

        hpool->del_capacity(vm->get_previous_hid(), vm->get_oid(), cpu, mem, disk);

        //----------------------------------------------------

        tm->trigger(TransferManager::PROLOG_MIGR,vid);
    }
    else if (vm->get_state()     == VirtualMachine::ACTIVE &&
        vm->get_lcm_state() == VirtualMachine::UNKNOWN)
    {
        //----------------------------------------------------
        //   Bypass SAVE_MIGRATE & PROLOG_MIGRATE goto BOOT
        //----------------------------------------------------

        vm->set_resched(false);

        vm->set_state(VirtualMachine::BOOT);

        vm->delete_snapshots();

        vm->reset_info();

        vmpool->update(vm);

        vm->set_stime(the_time);

        vm->set_previous_action(History::MIGRATE_ACTION);

        vm->set_previous_etime(the_time);

        vm->set_previous_vm_info();

        vm->set_previous_running_etime(the_time);

        vm->set_previous_reason(History::USER);

        vmpool->update_previous_history(vm);

        vmpool->update_history(vm);

        vm->get_requirements(cpu,mem,disk);

        hpool->add_capacity(vm->get_hid(), vm->get_oid(), cpu, mem, disk);

        hpool->del_capacity(vm->get_previous_hid(), vm->get_oid(), cpu, mem, disk);

        //----------------------------------------------------

        vmm->trigger(VirtualMachineManager::DEPLOY, vid);
    }
    else
    {
        vm->log("LCM", Log::ERROR, "migrate_action, VM in a wrong state.");
    }

    vm->unlock();

    return;
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

void  LifeCycleManager::live_migrate_action(int vid)
{
    VirtualMachine *    vm;
    ostringstream        os;

    vm = vmpool->get(vid,true);

    if ( vm == 0 )
    {
        return;
    }

    if (vm->get_state()     == VirtualMachine::ACTIVE &&
        vm->get_lcm_state() == VirtualMachine::RUNNING)
    {
        int                     cpu,mem,disk;

        //----------------------------------------------------
        //                   MIGRATE STATE
        //----------------------------------------------------

        vm->set_state(VirtualMachine::MIGRATE);

        vm->set_resched(false);

        vmpool->update(vm);

        vm->set_stime(time(0));

        vm->set_previous_action(History::LIVE_MIGRATE_ACTION);

        vmpool->update_history(vm);

        vm->get_requirements(cpu,mem,disk);

        hpool->add_capacity(vm->get_hid(), vm->get_oid(), cpu, mem, disk);

        //----------------------------------------------------

        vmm->trigger(VirtualMachineManager::MIGRATE,vid);
    }
    else
    {
        vm->log("LCM", Log::ERROR, "live_migrate_action, VM in a wrong state.");
    }

    vm->unlock();

    return;
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

void  LifeCycleManager::shutdown_action(int vid, bool hard)
{
    VirtualMachine * vm = vmpool->get(vid,true);

    if ( vm == 0 )
    {
        return;
    }

    if (vm->get_state()     == VirtualMachine::ACTIVE &&
        (vm->get_lcm_state() == VirtualMachine::RUNNING ||
         vm->get_lcm_state() == VirtualMachine::UNKNOWN))
    {
        //----------------------------------------------------
        //                  SHUTDOWN STATE
        //----------------------------------------------------

        vm->set_state(VirtualMachine::SHUTDOWN);

        if (hard)
        {
            vm->set_action(History::SHUTDOWN_HARD_ACTION);

            //----------------------------------------------------

            vmm->trigger(VirtualMachineManager::CANCEL,vid);
        }
        else
        {
            vm->set_action(History::SHUTDOWN_ACTION);

            //----------------------------------------------------

            vmm->trigger(VirtualMachineManager::SHUTDOWN,vid);
        }

        vm->set_resched(false);

        vmpool->update(vm);
        vmpool->update_history(vm);
    }
    else if (vm->get_state() == VirtualMachine::SUSPENDED ||
             vm->get_state() == VirtualMachine::POWEROFF)
    {
        //----------------------------------------------------
        //   Bypass SHUTDOWN
        //----------------------------------------------------

        vm->set_state(VirtualMachine::ACTIVE);
        vm->set_state(VirtualMachine::EPILOG);

        vmpool->update(vm);

        vm->set_action(History::SHUTDOWN_ACTION);

        vm->set_epilog_stime(time(0));

        vmpool->update_history(vm);

        //----------------------------------------------------

        tm->trigger(TransferManager::EPILOG,vid);
    }
    else
    {
        vm->log("LCM", Log::ERROR, "shutdown_action, VM in a wrong state.");
    }

    vm->unlock();

    return;
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

void  LifeCycleManager::undeploy_action(int vid, bool hard)
{
    VirtualMachine * vm = vmpool->get(vid,true);

    if ( vm == 0 )
    {
        return;
    }

    if (vm->get_state()     == VirtualMachine::ACTIVE &&
        (vm->get_lcm_state() == VirtualMachine::RUNNING ||
         vm->get_lcm_state() == VirtualMachine::UNKNOWN))
    {
        //----------------------------------------------------
        //             SHUTDOWN_UNDEPLOY STATE
        //----------------------------------------------------

        vm->set_state(VirtualMachine::SHUTDOWN_UNDEPLOY);

        vm->set_resched(false);

        vmpool->update(vm);

        //----------------------------------------------------

        if (hard)
        {
            vm->set_action(History::UNDEPLOY_HARD_ACTION);

            vmm->trigger(VirtualMachineManager::CANCEL,vid);
        }
        else
        {
            vm->set_action(History::UNDEPLOY_ACTION);

            vmm->trigger(VirtualMachineManager::SHUTDOWN,vid);
        }

        vmpool->update_history(vm);
    }
    else if (vm->get_state() == VirtualMachine::POWEROFF)
    {
        //----------------------------------------------------
        //   Bypass SHUTDOWN_UNDEPLOY
        //----------------------------------------------------

        vm->set_state(VirtualMachine::ACTIVE);
        vm->set_state(VirtualMachine::EPILOG_UNDEPLOY);

        vmpool->update(vm);

        vm->set_action(History::UNDEPLOY_ACTION);

        vm->set_epilog_stime(time(0));

        vmpool->update_history(vm);

        //----------------------------------------------------

        tm->trigger(TransferManager::EPILOG_STOP,vid);
    }
    else
    {
        vm->log("LCM", Log::ERROR, "undeploy_action, VM in a wrong state.");
    }

    vm->unlock();

    return;
}


/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

void  LifeCycleManager::poweroff_action(int vid)
{
    poweroff_action(vid, false);
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

void  LifeCycleManager::poweroff_hard_action(int vid)
{
    poweroff_action(vid, true);
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

void  LifeCycleManager::poweroff_action(int vid, bool hard)
{
    VirtualMachine *    vm;

    vm = vmpool->get(vid,true);

    if ( vm == 0 )
    {
        return;
    }

    if (vm->get_state()     == VirtualMachine::ACTIVE &&
        (vm->get_lcm_state() == VirtualMachine::RUNNING ||
         vm->get_lcm_state() == VirtualMachine::UNKNOWN))
    {
        //----------------------------------------------------
        //             SHUTDOWN_POWEROFF STATE
        //----------------------------------------------------

        vm->set_state(VirtualMachine::SHUTDOWN_POWEROFF);

        vm->set_resched(false);

        vmpool->update(vm);

        //----------------------------------------------------

        if (hard)
        {
            vm->set_action(History::POWEROFF_HARD_ACTION);

            vmm->trigger(VirtualMachineManager::CANCEL,vid);
        }
        else
        {
            vm->set_action(History::POWEROFF_ACTION);

            vmm->trigger(VirtualMachineManager::SHUTDOWN,vid);
        }

        vmpool->update_history(vm);
    }
    else
    {
        vm->log("LCM", Log::ERROR, "poweroff_action, VM in a wrong state.");
    }

    vm->unlock();

    return;
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

void  LifeCycleManager::restore_action(int vid)
{
    VirtualMachine *    vm;
    ostringstream       os;

    vm = vmpool->get(vid,true);

    if ( vm == 0 )
    {
        return;
    }

    if (vm->get_state() == VirtualMachine::SUSPENDED)
    {
        time_t                  the_time = time(0);

        vm->log("LCM", Log::INFO, "Restoring VM");

        //----------------------------------------------------
        //            BOOT STATE (FROM SUSPEND)
        //----------------------------------------------------
        vm->set_state(VirtualMachine::ACTIVE);

        vm->set_state(VirtualMachine::BOOT_SUSPENDED);

        vm->cp_history();

        vmpool->update(vm); //update last_seq & state

        vm->set_stime(the_time);

        vm->set_last_poll(0);

        vm->set_running_stime(the_time);

        vmpool->update_history(vm);

        //----------------------------------------------------

        vmm->trigger(VirtualMachineManager::RESTORE,vid);
    }
    else
    {
        vm->log("LCM", Log::ERROR, "restore_action, VM in a wrong state.");
    }

    vm->unlock();

    return;
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

void  LifeCycleManager::restart_action(int vid)
{
    VirtualMachine *    vm;

    vm = vmpool->get(vid,true);

    if ( vm == 0 )
    {
        return;
    }

    if (vm->get_state() == VirtualMachine::ACTIVE &&
        vm->get_lcm_state() == VirtualMachine::UNKNOWN)
    {
        vm->set_state(VirtualMachine::BOOT_UNKNOWN);

        vmpool->update(vm);

        vmm->trigger(VirtualMachineManager::DEPLOY, vid);
    }
    else if ( vm->get_state() == VirtualMachine::POWEROFF )
    {
        time_t the_time = time(0);

        vm->set_state(VirtualMachine::ACTIVE); // Only needed by poweroff

        vm->set_state(VirtualMachine::BOOT_POWEROFF);

        vm->cp_history();

        vmpool->update(vm);

        vm->set_stime(the_time);

        vm->set_last_poll(0);

        vm->set_running_stime(the_time);

        vmpool->update_history(vm);

        vmm->trigger(VirtualMachineManager::DEPLOY, vid);
    }
    else
    {
        vm->log("LCM", Log::ERROR, "restart_action, VM in a wrong state.");
    }

    vm->unlock();

    return;
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

void LifeCycleManager::delete_action(int vid)
{
    VirtualMachine * vm;

    int image_id = -1;

    vm = vmpool->get(vid,true);

    if ( vm == 0 )
    {
        return;
    }

    if ( vm->get_state() != VirtualMachine::ACTIVE )
    {
        vm->log("LCM", Log::ERROR, "clean_action, VM in a wrong state.");
        vm->unlock();

        return;
    }

    switch(vm->get_lcm_state())
    {
        case VirtualMachine::CLEANUP_RESUBMIT:
            vm->set_state(VirtualMachine::CLEANUP_DELETE);
            vmpool->update(vm);

        case VirtualMachine::CLEANUP_DELETE:
            dm->trigger(DispatchManager::DONE, vid);
        break;

        default:
            clean_up_vm(vm, true, image_id);
            dm->trigger(DispatchManager::DONE, vid);
        break;
    }

    vm->unlock();

    if ( image_id != -1 )
    {
        Image*     image = ipool->get(image_id, true);

        if ( image != 0 )
        {
            image->set_state(Image::ERROR);

            ipool->update(image);

            image->unlock();
        }
    }
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

void  LifeCycleManager::clean_action(int vid)
{
    VirtualMachine * vm;

    int image_id = -1;

    vm = vmpool->get(vid,true);

    if ( vm == 0 )
    {
        return;
    }

    if ( vm->get_state() != VirtualMachine::ACTIVE )
    {
        vm->log("LCM", Log::ERROR, "clean_action, VM in a wrong state.");
        vm->unlock();

        return;
    }

    switch(vm->get_lcm_state())
    {
        case VirtualMachine::CLEANUP_DELETE:
            vm->log("LCM", Log::ERROR, "clean_action, VM in a wrong state.");
        break;

        case VirtualMachine::CLEANUP_RESUBMIT:
            dm->trigger(DispatchManager::RESUBMIT, vid);
        break;

        default:
            clean_up_vm(vm, false, image_id);
        break;
    }

    vm->unlock();

    if ( image_id != -1 )
    {
        Image*     image = ipool->get(image_id, true);

        if ( image != 0 )
        {
            image->set_state(Image::ERROR);

            ipool->update(image);

            image->unlock();
        }
    }
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

void  LifeCycleManager::clean_up_vm(VirtualMachine * vm, bool dispose, int& image_id)
{
    int    cpu, mem, disk;
    time_t the_time = time(0);

    VirtualMachine::LcmState state = vm->get_lcm_state();
    int                      vid   = vm->get_oid();

    if (dispose)
    {
        vm->set_state(VirtualMachine::CLEANUP_DELETE);
        vm->set_action(History::DELETE_ACTION);
    }
    else
    {
        vm->set_state(VirtualMachine::CLEANUP_RESUBMIT);
        vm->set_action(History::DELETE_RECREATE_ACTION);
    }

    vm->set_resched(false);

    vm->delete_snapshots();

    vm->reset_info();

    vmpool->update(vm);

    vm->set_etime(the_time);
    vm->set_vm_info();
    vm->set_reason(History::USER);

    vm->get_requirements(cpu,mem,disk);
    hpool->del_capacity(vm->get_hid(), vm->get_oid(), cpu, mem, disk);

    switch (state)
    {
        case VirtualMachine::PROLOG:
        case VirtualMachine::PROLOG_RESUME:
        case VirtualMachine::PROLOG_RESUME_FAILURE:
        case VirtualMachine::PROLOG_UNDEPLOY:
        case VirtualMachine::PROLOG_UNDEPLOY_FAILURE:
        case VirtualMachine::PROLOG_FAILURE:
            vm->set_prolog_etime(the_time);
            vmpool->update_history(vm);

            tm->trigger(TransferManager::DRIVER_CANCEL,vid);
            tm->trigger(TransferManager::EPILOG_DELETE,vid);
        break;

        case VirtualMachine::BOOT:
        case VirtualMachine::BOOT_UNKNOWN:
        case VirtualMachine::BOOT_POWEROFF:
        case VirtualMachine::BOOT_SUSPENDED:
        case VirtualMachine::BOOT_STOPPED:
        case VirtualMachine::BOOT_UNDEPLOY:
        case VirtualMachine::BOOT_MIGRATE:
        case VirtualMachine::BOOT_FAILURE:
        case VirtualMachine::BOOT_MIGRATE_FAILURE:
        case VirtualMachine::BOOT_UNDEPLOY_FAILURE:
        case VirtualMachine::BOOT_STOPPED_FAILURE:
        case VirtualMachine::RUNNING:
        case VirtualMachine::UNKNOWN:
        case VirtualMachine::SHUTDOWN:
        case VirtualMachine::SHUTDOWN_POWEROFF:
        case VirtualMachine::SHUTDOWN_UNDEPLOY:
        case VirtualMachine::HOTPLUG_SNAPSHOT:
            vm->set_running_etime(the_time);
            vmpool->update_history(vm);

            vmm->trigger(VirtualMachineManager::DRIVER_CANCEL,vid);
            vmm->trigger(VirtualMachineManager::CLEANUP,vid);
        break;

        case VirtualMachine::HOTPLUG:
            vm->clear_attach_disk();
            vmpool->update(vm);

            vm->set_running_etime(the_time);
            vmpool->update_history(vm);

            vmm->trigger(VirtualMachineManager::DRIVER_CANCEL,vid);
            vmm->trigger(VirtualMachineManager::CLEANUP,vid);
        break;

        case VirtualMachine::HOTPLUG_NIC:
            vm->clear_attach_nic();
            vmpool->update(vm);

            vm->set_running_etime(the_time);
            vmpool->update_history(vm);

            vmm->trigger(VirtualMachineManager::DRIVER_CANCEL,vid);
            vmm->trigger(VirtualMachineManager::CLEANUP,vid);
        break;

        case VirtualMachine::HOTPLUG_SAVEAS:
            image_id = vm->clear_saveas_disk();
            vmpool->update(vm);

            vm->set_running_etime(the_time);
            vmpool->update_history(vm);

            vmm->trigger(VirtualMachineManager::DRIVER_CANCEL,vid);
            vmm->trigger(VirtualMachineManager::CLEANUP,vid);
        break;

        case VirtualMachine::HOTPLUG_SAVEAS_POWEROFF:
        case VirtualMachine::HOTPLUG_SAVEAS_SUSPENDED:
            tm->trigger(TransferManager::DRIVER_CANCEL, vid);

            image_id = vm->clear_saveas_disk();
            vmpool->update(vm);

            vm->set_running_etime(the_time);
            vmpool->update_history(vm);
        break;

        case VirtualMachine::HOTPLUG_PROLOG_POWEROFF:
        case VirtualMachine::HOTPLUG_EPILOG_POWEROFF:
            vm->clear_attach_disk();
            vmpool->update(vm);

            tm->trigger(TransferManager::DRIVER_CANCEL,vid);
            tm->trigger(TransferManager::EPILOG_DELETE,vid);
        break;

        case VirtualMachine::DISK_SNAPSHOT_POWEROFF:
        case VirtualMachine::DISK_SNAPSHOT_REVERT_POWEROFF:
        case VirtualMachine::DISK_SNAPSHOT_DELETE_POWEROFF:
            vm->clear_snapshot_disk();
            vmpool->update(vm);

            tm->trigger(TransferManager::DRIVER_CANCEL, vid);
            tm->trigger(TransferManager::EPILOG_DELETE,vid);
        break;

        case VirtualMachine::MIGRATE:
            vm->set_running_etime(the_time);
            vmpool->update_history(vm);

            vm->set_previous_etime(the_time);
            vm->set_previous_vm_info();
            vm->set_previous_running_etime(the_time);
            vm->set_previous_reason(History::USER);
            vmpool->update_previous_history(vm);

            hpool->del_capacity(vm->get_previous_hid(), vm->get_oid(), cpu,
                    mem, disk);

            vmm->trigger(VirtualMachineManager::DRIVER_CANCEL,vid);
            vmm->trigger(VirtualMachineManager::CLEANUP_BOTH,vid);
        break;

        case VirtualMachine::SAVE_STOP:
        case VirtualMachine::SAVE_SUSPEND:
            vm->set_running_etime(the_time);
            vmpool->update_history(vm);

            vmm->trigger(VirtualMachineManager::DRIVER_CANCEL,vid);
            vmm->trigger(VirtualMachineManager::CLEANUP,vid);
        break;

        case VirtualMachine::SAVE_MIGRATE:
            vm->set_running_etime(the_time);
            vmpool->update_history(vm);

            vm->set_previous_etime(the_time);
            vm->set_previous_vm_info();
            vm->set_previous_running_etime(the_time);
            vm->set_previous_reason(History::USER);
            vmpool->update_previous_history(vm);

            hpool->del_capacity(vm->get_previous_hid(), vm->get_oid(), cpu,
                    mem, disk);

            vmm->trigger(VirtualMachineManager::DRIVER_CANCEL,vid);
            vmm->trigger(VirtualMachineManager::CLEANUP_PREVIOUS,vid);
        break;

        case VirtualMachine::PROLOG_MIGRATE:
        case VirtualMachine::PROLOG_MIGRATE_FAILURE:
        case VirtualMachine::PROLOG_MIGRATE_POWEROFF:
        case VirtualMachine::PROLOG_MIGRATE_POWEROFF_FAILURE:
        case VirtualMachine::PROLOG_MIGRATE_SUSPEND:
        case VirtualMachine::PROLOG_MIGRATE_SUSPEND_FAILURE:
            vm->set_prolog_etime(the_time);
            vmpool->update_history(vm);

            tm->trigger(TransferManager::DRIVER_CANCEL,vid);
            tm->trigger(TransferManager::EPILOG_DELETE_BOTH,vid);
        break;

        case VirtualMachine::EPILOG_STOP:
        case VirtualMachine::EPILOG_UNDEPLOY:
        case VirtualMachine::EPILOG:
        case VirtualMachine::EPILOG_FAILURE:
        case VirtualMachine::EPILOG_STOP_FAILURE:
        case VirtualMachine::EPILOG_UNDEPLOY_FAILURE:
            vm->set_epilog_etime(the_time);
            vmpool->update_history(vm);

            tm->trigger(TransferManager::DRIVER_CANCEL,vid);
            tm->trigger(TransferManager::EPILOG_DELETE,vid);
        break;

        case VirtualMachine::LCM_INIT:
        case VirtualMachine::CLEANUP_RESUBMIT:
        case VirtualMachine::CLEANUP_DELETE:
        break;
    }
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

void  LifeCycleManager::recover(VirtualMachine * vm, bool success)
{
    LifeCycleManager::Actions lcm_action = LifeCycleManager::FINALIZE;

    switch (vm->get_lcm_state())
    {
        //----------------------------------------------------------------------
        // Direct recovery states
        //----------------------------------------------------------------------
        case VirtualMachine::LCM_INIT:
        case VirtualMachine::RUNNING:
            return;

        case VirtualMachine::CLEANUP_DELETE:
            dm->trigger(DispatchManager::DONE, vm->get_oid());
            return;

        case VirtualMachine::CLEANUP_RESUBMIT:
            dm->trigger(DispatchManager::RESUBMIT, vm->get_oid());
            return;

        case VirtualMachine::UNKNOWN:
            vm->set_state(VirtualMachine::RUNNING);
            vmpool->update(vm);
            return;

        //----------------------------------------------------------------------
        // Recover through re-triggering LCM events
        //----------------------------------------------------------------------
        case VirtualMachine::PROLOG:
        case VirtualMachine::PROLOG_MIGRATE:
        case VirtualMachine::PROLOG_MIGRATE_FAILURE:
        case VirtualMachine::PROLOG_RESUME:
        case VirtualMachine::PROLOG_RESUME_FAILURE:
        case VirtualMachine::PROLOG_UNDEPLOY:
        case VirtualMachine::PROLOG_UNDEPLOY_FAILURE:
        case VirtualMachine::PROLOG_FAILURE:
        case VirtualMachine::PROLOG_MIGRATE_POWEROFF:
        case VirtualMachine::PROLOG_MIGRATE_POWEROFF_FAILURE:
        case VirtualMachine::PROLOG_MIGRATE_SUSPEND:
        case VirtualMachine::PROLOG_MIGRATE_SUSPEND_FAILURE:
            if (success)
            {
                lcm_action = LifeCycleManager::PROLOG_SUCCESS;
            }
            else
            {
                lcm_action = LifeCycleManager::PROLOG_FAILURE;
            }
        break;

        case VirtualMachine::EPILOG:
        case VirtualMachine::EPILOG_STOP:
        case VirtualMachine::EPILOG_UNDEPLOY:
        case VirtualMachine::EPILOG_FAILURE:
        case VirtualMachine::EPILOG_STOP_FAILURE:
        case VirtualMachine::EPILOG_UNDEPLOY_FAILURE:
            if (success)
            {
                lcm_action = LifeCycleManager::EPILOG_SUCCESS;
            }
            else
            {
                lcm_action = LifeCycleManager::EPILOG_FAILURE;
            }
        break;

        case VirtualMachine::HOTPLUG_SAVEAS:
        case VirtualMachine::HOTPLUG_SAVEAS_POWEROFF:
        case VirtualMachine::HOTPLUG_SAVEAS_SUSPENDED:
            if (success)
            {
                lcm_action = LifeCycleManager::SAVEAS_SUCCESS;
            }
            else
            {
                lcm_action = LifeCycleManager::SAVEAS_FAILURE;
            }
        break;

        case VirtualMachine::HOTPLUG_PROLOG_POWEROFF:
            if (success)
            {
                lcm_action = LifeCycleManager::ATTACH_SUCCESS;
            }
            else
            {
                lcm_action = LifeCycleManager::ATTACH_FAILURE;
            }
        break;

        case VirtualMachine::HOTPLUG_EPILOG_POWEROFF:
            if (success)
            {
                lcm_action = LifeCycleManager::DETACH_SUCCESS;
            }
            else
            {
                lcm_action = LifeCycleManager::DETACH_FAILURE;
            }
        break;

        case VirtualMachine::BOOT:
        case VirtualMachine::BOOT_UNKNOWN:
        case VirtualMachine::BOOT_POWEROFF:
        case VirtualMachine::BOOT_SUSPENDED:
        case VirtualMachine::BOOT_STOPPED:
        case VirtualMachine::BOOT_UNDEPLOY:
        case VirtualMachine::MIGRATE:
        case VirtualMachine::BOOT_MIGRATE:
        case VirtualMachine::BOOT_MIGRATE_FAILURE:
        case VirtualMachine::BOOT_FAILURE:
        case VirtualMachine::BOOT_STOPPED_FAILURE:
        case VirtualMachine::BOOT_UNDEPLOY_FAILURE:
            if (success)
            {
                //Auto-generate deploy-id it'll work for Xen, KVM and VMware
                if (vm->get_deploy_id().empty())
                {
                    ostringstream oss;

                    oss << "one-" << vm->get_oid();

                    vm->update_info(oss.str());
                }

                lcm_action = LifeCycleManager::DEPLOY_SUCCESS;
            }
            else
            {
                lcm_action = LifeCycleManager::DEPLOY_FAILURE;
            }
        break;

        case VirtualMachine::SHUTDOWN:
        case VirtualMachine::SHUTDOWN_POWEROFF:
        case VirtualMachine::SHUTDOWN_UNDEPLOY:
            if (success)
            {
                lcm_action = LifeCycleManager::SHUTDOWN_SUCCESS;
            }
            else
            {
                lcm_action = LifeCycleManager::SHUTDOWN_FAILURE;
            }
        break;

        case VirtualMachine::SAVE_STOP:
        case VirtualMachine::SAVE_SUSPEND:
        case VirtualMachine::SAVE_MIGRATE:
            if (success)
            {
                lcm_action = LifeCycleManager::SAVE_SUCCESS;
            }
            else
            {
                lcm_action = LifeCycleManager::SAVE_FAILURE;
            }
        break;

        case VirtualMachine::HOTPLUG:
            if (success)
            {
                lcm_action = LifeCycleManager::ATTACH_SUCCESS;
            }
            else
            {
                lcm_action = LifeCycleManager::ATTACH_FAILURE;
            }
        break;

        case VirtualMachine::HOTPLUG_NIC:
            if (success)
            {
                lcm_action = LifeCycleManager::ATTACH_NIC_SUCCESS;
            }
            else
            {
                lcm_action = LifeCycleManager::ATTACH_NIC_FAILURE;
            }
        break;

        //This is for all snapshot actions (create, delete & revert)
        case VirtualMachine::HOTPLUG_SNAPSHOT:
            lcm_action = LifeCycleManager::SNAPSHOT_CREATE_FAILURE;
        break;

        case VirtualMachine::DISK_SNAPSHOT_POWEROFF:
        case VirtualMachine::DISK_SNAPSHOT_REVERT_POWEROFF:
        case VirtualMachine::DISK_SNAPSHOT_DELETE_POWEROFF:
            lcm_action = LifeCycleManager::DISK_SNAPSHOT_FAILURE;
        break;
    }

    if (lcm_action != LifeCycleManager::FINALIZE)
    {
        trigger(lcm_action, vm->get_oid());
    }
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

void LifeCycleManager::retry(VirtualMachine * vm)
{
	int vid = vm->get_oid();

    if ( vm->get_state() != VirtualMachine::ACTIVE )
    {
        return;
    }

    VirtualMachine::LcmState state = vm->get_lcm_state();

    switch (state)
    {
        case VirtualMachine::BOOT_FAILURE:
            vm->set_state(VirtualMachine::BOOT);

            vmpool->update(vm);

            vmm->trigger(VirtualMachineManager::DEPLOY, vid);
            break;

        case VirtualMachine::BOOT_MIGRATE_FAILURE:
            vm->set_state(VirtualMachine::BOOT_MIGRATE);

            vmpool->update(vm);

            vmm->trigger(VirtualMachineManager::RESTORE, vid);
            break;

        case VirtualMachine::BOOT_UNDEPLOY_FAILURE:
            vm->set_state(VirtualMachine::BOOT_UNDEPLOY);

            vmpool->update(vm);

            vmm->trigger(VirtualMachineManager::DEPLOY, vid);
            break;

        case VirtualMachine::BOOT_STOPPED_FAILURE:
            vm->set_state(VirtualMachine::BOOT_STOPPED);

            vmpool->update(vm);

            vmm->trigger(VirtualMachineManager::RESTORE, vid);
            break;

        case VirtualMachine::PROLOG_MIGRATE_FAILURE:
            vm->set_state(VirtualMachine::PROLOG_MIGRATE);

            vmpool->update(vm);

            tm->trigger(TransferManager::PROLOG_MIGR, vid);
            break;

        case VirtualMachine::PROLOG_MIGRATE_POWEROFF_FAILURE:
            vm->set_state(VirtualMachine::PROLOG_MIGRATE_POWEROFF);

            vmpool->update(vm);

            tm->trigger(TransferManager::PROLOG_MIGR, vid);
            break;


        case VirtualMachine::PROLOG_MIGRATE_SUSPEND_FAILURE:
            vm->set_state(VirtualMachine::PROLOG_MIGRATE_SUSPEND);

            vmpool->update(vm);

            tm->trigger(TransferManager::PROLOG_MIGR, vid);
            break;

        case VirtualMachine::PROLOG_RESUME_FAILURE:
            vm->set_state(VirtualMachine::PROLOG_RESUME);

            vmpool->update(vm);

            tm->trigger(TransferManager::PROLOG_RESUME,vid);
            break;

        case VirtualMachine::PROLOG_UNDEPLOY_FAILURE:
            vm->set_state(VirtualMachine::PROLOG_UNDEPLOY);

            vmpool->update(vm);

            tm->trigger(TransferManager::PROLOG_RESUME,vid);
            break;

        case VirtualMachine::PROLOG_FAILURE:
            vm->set_state(VirtualMachine::PROLOG);

            vmpool->update(vm);

            tm->trigger(TransferManager::PROLOG,vid);
            break;

        case VirtualMachine::EPILOG_FAILURE:
            vm->set_state(VirtualMachine::EPILOG);

            vmpool->update(vm);

            tm->trigger(TransferManager::EPILOG,vid);
            break;

        case VirtualMachine::EPILOG_STOP_FAILURE:
            vm->set_state(VirtualMachine::EPILOG_STOP);

            vmpool->update(vm);

            tm->trigger(TransferManager::EPILOG_STOP,vid);
            break;

       case VirtualMachine::EPILOG_UNDEPLOY_FAILURE:
            vm->set_state(VirtualMachine::EPILOG_UNDEPLOY);

            vmpool->update(vm);

            tm->trigger(TransferManager::EPILOG_STOP,vid);
            break;

        case VirtualMachine::BOOT_MIGRATE:
        case VirtualMachine::BOOT_SUSPENDED:
        case VirtualMachine::BOOT_STOPPED:
        case VirtualMachine::BOOT_UNDEPLOY:
            vmm->trigger(VirtualMachineManager::RESTORE, vid);
            break;

        case VirtualMachine::BOOT:
        case VirtualMachine::BOOT_POWEROFF:
        case VirtualMachine::BOOT_UNKNOWN:
            vmm->trigger(VirtualMachineManager::DEPLOY, vid);
            break;

        case VirtualMachine::SHUTDOWN:
        case VirtualMachine::SHUTDOWN_POWEROFF:
        case VirtualMachine::SHUTDOWN_UNDEPLOY:
            if (vm->get_action() == History::SHUTDOWN_HARD_ACTION)
            {
                vmm->trigger(VirtualMachineManager::CANCEL,vid);
            }
            else
            {
                vmm->trigger(VirtualMachineManager::SHUTDOWN,vid);
            }
            break;

        case VirtualMachine::SAVE_STOP:
        case VirtualMachine::SAVE_SUSPEND:
        case VirtualMachine::SAVE_MIGRATE:
            vmm->trigger(VirtualMachineManager::SAVE,vid);
            break;

        case VirtualMachine::MIGRATE:
            vmm->trigger(VirtualMachineManager::MIGRATE,vid);
            break;

        case VirtualMachine::PROLOG:
            tm->trigger(TransferManager::PROLOG,vid);
            break;

        case VirtualMachine::PROLOG_MIGRATE:
        case VirtualMachine::PROLOG_MIGRATE_POWEROFF:
        case VirtualMachine::PROLOG_MIGRATE_SUSPEND:
            tm->trigger(TransferManager::PROLOG_MIGR,vid);
            break;

        case VirtualMachine::PROLOG_RESUME:
        case VirtualMachine::PROLOG_UNDEPLOY:
            tm->trigger(TransferManager::PROLOG_RESUME,vid);
            break;

        case VirtualMachine::EPILOG:
            tm->trigger(TransferManager::EPILOG,vid);
            break;

        case VirtualMachine::EPILOG_STOP:
        case VirtualMachine::EPILOG_UNDEPLOY:
            tm->trigger(TransferManager::EPILOG_STOP,vid);
            break;

        case VirtualMachine::LCM_INIT:
        case VirtualMachine::CLEANUP_RESUBMIT:
        case VirtualMachine::CLEANUP_DELETE:
        case VirtualMachine::HOTPLUG:
        case VirtualMachine::HOTPLUG_NIC:
        case VirtualMachine::HOTPLUG_SNAPSHOT:
        case VirtualMachine::HOTPLUG_SAVEAS:
        case VirtualMachine::HOTPLUG_SAVEAS_POWEROFF:
        case VirtualMachine::HOTPLUG_SAVEAS_SUSPENDED:
        case VirtualMachine::HOTPLUG_PROLOG_POWEROFF:
        case VirtualMachine::HOTPLUG_EPILOG_POWEROFF:
        case VirtualMachine::DISK_SNAPSHOT_POWEROFF:
        case VirtualMachine::DISK_SNAPSHOT_REVERT_POWEROFF:
        case VirtualMachine::DISK_SNAPSHOT_DELETE_POWEROFF:
        case VirtualMachine::RUNNING:
        case VirtualMachine::UNKNOWN:
            break;
    }

    return;
}
