#include <rtems/score/container.h>
#include <rtems/score/smp.h>
#include <rtems/score/percpu.h>
#include <rtems/score/thread.h>
#include <rtems/score/threadimpl.h>
#ifdef RTEMS_CGROUP
#include <rtems/rtems/cgroupimpl.h>
#include <rtems/rtems/support.h>
#endif
#ifdef RTEMSCFG_IO_CGROUP
#include <rtems/io_cgroup.h>
#endif
#include <stdlib.h>
#include <string.h>

static Container rootContainerObj;      // 用来避免野指针和内存泄漏
static Container *rootContainer = NULL;

void rtems_container_initialize_root(void){

    if (rootContainer != NULL) {
        return; 
    }
    // rootContainer = (Container *)malloc(sizeof(Container));
    rootContainer = &rootContainerObj;

    rootContainer->rc = 1; 
#ifdef RTEMSCFG_PID_CONTAINER
    rootContainer->pidContainer = NULL; // 初始化pidContainer为NULL 
    rootContainer->pidContainerListHead = NULL;
    (void)rtems_pid_container_initialize_root(&rootContainer->pidContainer);
#endif

#ifdef RTEMSCFG_UTS_CONTAINER
    rootContainer->utsContainer = NULL; // 初始化utsContainer为NULL 
    rootContainer->utsContainerListHead = NULL;
    (void)rtems_uts_container_initialize_root(&rootContainer->utsContainer);
#endif

#ifdef RTEMSCFG_MNT_CONTAINER
    rootContainer->mntContainer = NULL; // 初始化mntContainer为NULL 
    rootContainer->mntContainerListHead = NULL;
    (void)rtems_mnt_container_initialize_root(&rootContainer->mntContainer);
#endif

#ifdef RTEMSCFG_NET_CONTAINER
    rootContainer->netContainer = NULL; // 初始化netContainer为NULL
    rootContainer->netContainerListHead = NULL;
    (void)rtems_net_container_initialize_root(&rootContainer->netContainer);
#endif

#ifdef RTEMSCFG_IPC_CONTAINER
    rootContainer->ipcContainer = NULL; // 初始化ipcContainer为NULL
    rootContainer->ipcContainerListHead = NULL;
    (void)rtems_ipc_container_initialize_root(&rootContainer->ipcContainer);
#endif

//         if (idle) {
//             idle->container = rootContainer;
//         }
//     }
// #else
//     Thread_Control *idle = _Per_CPU_Get_by_index(0)->heir;
//     if (idle) {
//         idle->container = rootContainer;
//     }
// #endif

}

Container *rtems_container_get_root(void) {
    return rootContainer;
}

static rtems_status_code rtems_unified_container_check_flags(uint32_t flags)
{
#ifndef RTEMSCFG_PID_CONTAINER
    if ((flags & RTEMS_UNIFIED_CONTAINER_PID) != 0) {
        return RTEMS_NOT_CONFIGURED;
    }
#endif
#ifndef RTEMSCFG_IPC_CONTAINER
    if ((flags & RTEMS_UNIFIED_CONTAINER_IPC) != 0) {
        return RTEMS_NOT_CONFIGURED;
    }
#endif
#ifndef RTEMSCFG_MNT_CONTAINER
    if ((flags & RTEMS_UNIFIED_CONTAINER_MNT) != 0) {
        return RTEMS_NOT_CONFIGURED;
    }
#endif
#ifndef RTEMSCFG_NET_CONTAINER
    if ((flags & RTEMS_UNIFIED_CONTAINER_NET) != 0) {
        return RTEMS_NOT_CONFIGURED;
    }
#endif
#ifndef RTEMSCFG_UTS_CONTAINER
    if ((flags & RTEMS_UNIFIED_CONTAINER_UTS) != 0) {
        return RTEMS_NOT_CONFIGURED;
    }
#endif
#ifndef RTEMS_CGROUP
    if ((flags & (RTEMS_UNIFIED_CONTAINER_CPU | RTEMS_UNIFIED_CONTAINER_MEM)) != 0) {
        return RTEMS_NOT_CONFIGURED;
    }
#endif
#ifndef RTEMSCFG_IO_CGROUP
    if ((flags & RTEMS_UNIFIED_CONTAINER_IO) != 0) {
        return RTEMS_NOT_CONFIGURED;
    }
#endif

    return RTEMS_SUCCESSFUL;
}

static rtems_status_code rtems_unified_container_ensure_thread_container(
    Thread_Control *thread
)
{
    Container *root;
    Container *thread_container;

    if (thread == NULL) {
        return RTEMS_INVALID_ADDRESS;
    }

    if (thread->container != NULL) {
        return RTEMS_SUCCESSFUL;
    }

    root = rtems_container_get_root();
    if (root == NULL) {
        return RTEMS_NOT_CONFIGURED;
    }

    thread_container = calloc(1, sizeof(*thread_container));
    if (thread_container == NULL) {
        return RTEMS_NO_MEMORY;
    }

    thread_container->rc = 1;

#ifdef RTEMSCFG_PID_CONTAINER
    thread_container->pidContainer = root->pidContainer;
    if (thread_container->pidContainer != NULL) {
        rtems_pid_container_add_task(thread_container->pidContainer, thread);
    }
#endif
#ifdef RTEMSCFG_UTS_CONTAINER
    thread_container->utsContainer = root->utsContainer;
    if (thread_container->utsContainer != NULL) {
        thread_container->utsContainer->rc++;
    }
#endif
#ifdef RTEMSCFG_MNT_CONTAINER
    thread_container->mntContainer = root->mntContainer;
    if (thread_container->mntContainer != NULL) {
        thread_container->mntContainer->rc++;
    }
#endif
#ifdef RTEMSCFG_NET_CONTAINER
    thread_container->netContainer = root->netContainer;
    if (thread_container->netContainer != NULL) {
        thread_container->netContainer->rc++;
    }
#endif
#ifdef RTEMSCFG_IPC_CONTAINER
    thread_container->ipcContainer = root->ipcContainer;
    if (thread_container->ipcContainer != NULL) {
        thread_container->ipcContainer->rc++;
    }
#endif

    thread->container = thread_container;
    return RTEMS_SUCCESSFUL;
}

void rtems_unified_container_config_initialize(
    RtemsContainerConfig *config
)
{
    if (config == NULL) {
        return;
    }

    memset(config, 0, sizeof(*config));
    config->flags = RTEMS_UNIFIED_CONTAINER_ALL;
    config->uts_name = "container";

#ifdef RTEMS_CGROUP
    config->cgroup_config.cpu_shares = 1;
    config->cgroup_config.cpu_quota = 100;
    config->cgroup_config.cpu_period = 1000;
    config->cgroup_config.memory_limit = 1024 * 1024;
    config->cgroup_config.blkio_limit = 1024 * 1024;
#endif

#ifdef RTEMSCFG_IO_CGROUP
    config->io_system_read_bps = 1024 * 1024;
    config->io_system_write_bps = 1024 * 1024;
    config->io_read_bps_limit = 1024 * 1024;
    config->io_write_bps_limit = 1024 * 1024;
    config->io_window_size_ms = 1000;
    config->io_weight = 100;
    config->io_thread_weight = 100;
#endif
}

rtems_status_code rtems_unified_container_create(
    const RtemsContainerConfig *config,
    RtemsContainer **container
)
{
    RtemsContainerConfig default_config;
    const RtemsContainerConfig *effective_config;
    RtemsContainer *created;
    rtems_status_code sc;
    Container *root;

    if (container == NULL) {
        return RTEMS_INVALID_ADDRESS;
    }

    *container = NULL;

    if (rtems_container_get_root() == NULL) {
        rtems_container_initialize_root();
    }

    root = rtems_container_get_root();
    if (root == NULL) {
        return RTEMS_NOT_CONFIGURED;
    }

    if (config == NULL) {
        rtems_unified_container_config_initialize(&default_config);
        effective_config = &default_config;
    } else {
        effective_config = config;
    }

    sc = rtems_unified_container_check_flags(effective_config->flags);
    if (sc != RTEMS_SUCCESSFUL) {
        return sc;
    }

    created = calloc(1, sizeof(*created));
    if (created == NULL) {
        return RTEMS_NO_MEMORY;
    }

    created->flags = effective_config->flags;
    created->namespaces.rc = 1;

#ifdef RTEMSCFG_PID_CONTAINER
    if ((created->flags & RTEMS_UNIFIED_CONTAINER_PID) != 0) {
        created->namespaces.pidContainer = rtems_pid_container_create();
        if (created->namespaces.pidContainer == NULL) {
            sc = RTEMS_NO_MEMORY;
            goto fail;
        }
        rtems_pid_container_inc_rc(created->namespaces.pidContainer);
    }
#endif

#ifdef RTEMSCFG_UTS_CONTAINER
    if ((created->flags & RTEMS_UNIFIED_CONTAINER_UTS) != 0) {
        created->namespaces.utsContainer =
            rtems_uts_container_create(effective_config->uts_name);
        if (created->namespaces.utsContainer == NULL) {
            sc = RTEMS_NO_MEMORY;
            goto fail;
        }
        created->namespaces.utsContainer->rc++;
    }
#endif

#ifdef RTEMSCFG_MNT_CONTAINER
    if ((created->flags & RTEMS_UNIFIED_CONTAINER_MNT) != 0) {
        created->namespaces.mntContainer =
            rtems_mnt_container_create_with_inheritance(root->mntContainer);
        if (created->namespaces.mntContainer == NULL) {
            sc = RTEMS_NO_MEMORY;
            goto fail;
        }
        rtems_mnt_container_add_to_list(created->namespaces.mntContainer);
    }
#endif

#ifdef RTEMSCFG_NET_CONTAINER
    if ((created->flags & RTEMS_UNIFIED_CONTAINER_NET) != 0) {
        created->namespaces.netContainer = rtems_net_container_create();
        if (created->namespaces.netContainer == NULL) {
            sc = RTEMS_NO_MEMORY;
            goto fail;
        }
    }
#endif

#ifdef RTEMSCFG_IPC_CONTAINER
    if ((created->flags & RTEMS_UNIFIED_CONTAINER_IPC) != 0) {
        created->namespaces.ipcContainer = rtems_ipc_container_create();
        if (created->namespaces.ipcContainer == NULL) {
            sc = RTEMS_NO_MEMORY;
            goto fail;
        }
    }
#endif

#ifdef RTEMS_CGROUP
    if ((created->flags & (RTEMS_UNIFIED_CONTAINER_CPU | RTEMS_UNIFIED_CONTAINER_MEM)) != 0) {
        Cgroup_Control *the_cgroup;
        ISR_lock_Context lock_context;

        created->cgroup_config = effective_config->cgroup_config;
        sc = rtems_cgroup_create(
            rtems_build_name('C', 'T', 'N', 'R'),
            &created->cgroup_id,
            &created->cgroup_config
        );
        if (sc != RTEMS_SUCCESSFUL) {
            goto fail;
        }

        the_cgroup = _Cgroup_Get(created->cgroup_id, &lock_context);
        _ISR_lock_ISR_enable(&lock_context);
        if (the_cgroup == NULL) {
            sc = RTEMS_UNSATISFIED;
            goto fail;
        }
        created->core_cgroup = &the_cgroup->cgroup;
    }
#endif

#ifdef RTEMSCFG_IO_CGROUP
    if ((created->flags & RTEMS_UNIFIED_CONTAINER_IO) != 0) {
        IO_Cgroup_Limit limits;

        limits.read_bps_limit = effective_config->io_read_bps_limit;
        limits.write_bps_limit = effective_config->io_write_bps_limit;
        limits.window_size_ms = effective_config->io_window_size_ms;

        sc = rtems_io_cgroup_manager_create(
            effective_config->io_system_read_bps,
            effective_config->io_system_write_bps
        );
        if (sc != RTEMS_SUCCESSFUL) {
            goto fail;
        }

        sc = rtems_io_cgroup_create(
            effective_config->io_weight,
            &limits,
            &created->io_cgroup_id
        );
        if (sc != RTEMS_SUCCESSFUL) {
            goto fail;
        }

        created->io_cgroup = rtems_io_cgroup_get_by_id(created->io_cgroup_id);
        if (created->io_cgroup == NULL) {
            sc = RTEMS_UNSATISFIED;
            goto fail;
        }

        created->io_thread_weight = effective_config->io_thread_weight;
    }
#endif

    *container = created;
    return RTEMS_SUCCESSFUL;

fail:
    rtems_unified_container_delete(created);
    return sc;
}

rtems_status_code rtems_unified_container_enter(
    RtemsContainer *container,
    Thread_Control *thread
)
{
    rtems_status_code sc;

    if (container == NULL) {
        return RTEMS_INVALID_ADDRESS;
    }

    if (thread == NULL) {
        thread = _Thread_Get_executing();
    }

    sc = rtems_unified_container_ensure_thread_container(thread);
    if (sc != RTEMS_SUCCESSFUL) {
        return sc;
    }

    if (container->attached && container->attached_thread != thread) {
        return RTEMS_RESOURCE_IN_USE;
    }

#ifdef RTEMSCFG_PID_CONTAINER
    if ((container->flags & RTEMS_UNIFIED_CONTAINER_PID) != 0) {
        PidContainer *src = thread->container->pidContainer;
        PidContainer *dst = container->namespaces.pidContainer;

        if (src != dst) {
            rtems_pid_container_move_task(src, dst, thread);
        }
        thread->container->pidContainer = dst;
        if (thread->container->pidContainer != dst) {
            return RTEMS_UNSATISFIED;
        }
    }
#endif

#ifdef RTEMSCFG_UTS_CONTAINER
    if ((container->flags & RTEMS_UNIFIED_CONTAINER_UTS) != 0) {
        UtsContainer *src = thread->container->utsContainer;
        UtsContainer *dst = container->namespaces.utsContainer;

        if (src != dst) {
            rtems_uts_container_move_task(src, dst, thread);
        }
        if (thread->container->utsContainer != dst) {
            return RTEMS_UNSATISFIED;
        }
    }
#endif

#ifdef RTEMSCFG_MNT_CONTAINER
    if ((container->flags & RTEMS_UNIFIED_CONTAINER_MNT) != 0) {
        MntContainer *src = thread->container->mntContainer;
        MntContainer *dst = container->namespaces.mntContainer;

        if (src != dst) {
            rtems_mnt_container_move_task(src, dst, thread);
        }
        if (thread->container->mntContainer != dst) {
            return RTEMS_UNSATISFIED;
        }
    }
#endif

#ifdef RTEMSCFG_NET_CONTAINER
    if ((container->flags & RTEMS_UNIFIED_CONTAINER_NET) != 0) {
        NetContainer *src = thread->container->netContainer;
        NetContainer *dst = container->namespaces.netContainer;

        if (src != dst) {
            rtems_net_container_move_task(src, dst, thread);
        }
        if (thread->container->netContainer != dst) {
            return RTEMS_UNSATISFIED;
        }
    }
#endif

#ifdef RTEMSCFG_IPC_CONTAINER
    if ((container->flags & RTEMS_UNIFIED_CONTAINER_IPC) != 0) {
        IpcContainer *src = thread->container->ipcContainer;
        IpcContainer *dst = container->namespaces.ipcContainer;

        if (src != dst) {
            rtems_ipc_container_move_task(src, dst, thread);
        }
        if (thread->container->ipcContainer != dst) {
            return RTEMS_UNSATISFIED;
        }
    }
#endif

#ifdef RTEMS_CGROUP
    if ((container->flags & (RTEMS_UNIFIED_CONTAINER_CPU | RTEMS_UNIFIED_CONTAINER_MEM)) != 0) {
        if (thread->is_added_to_cgroup && thread->cgroup != container->core_cgroup) {
            return RTEMS_RESOURCE_IN_USE;
        }

        if (thread->cgroup != container->core_cgroup) {
            sc = rtems_cgroup_add_task(container->cgroup_id, thread->Object.id);
            if (sc != RTEMS_SUCCESSFUL) {
                return sc;
            }
        }
    }
#endif

#ifdef RTEMSCFG_IO_CGROUP
    if ((container->flags & RTEMS_UNIFIED_CONTAINER_IO) != 0) {
        sc = rtems_io_cgroup_add_thread(
            container->io_cgroup,
            thread,
            container->io_thread_weight
        );
        if (sc != RTEMS_SUCCESSFUL) {
            return sc;
        }
    }
#endif

    container->attached = true;
    container->attached_thread = thread;
    return RTEMS_SUCCESSFUL;
}

rtems_status_code rtems_unified_container_leave(
    RtemsContainer *container,
    Thread_Control *thread
)
{
    rtems_status_code first_error = RTEMS_SUCCESSFUL;
    rtems_status_code sc;
    Container *root;

    if (container == NULL) {
        return RTEMS_INVALID_ADDRESS;
    }

    if (thread == NULL) {
        thread = container->attached_thread != NULL ?
            container->attached_thread : _Thread_Get_executing();
    }

    if (thread == NULL || thread->container == NULL) {
        return RTEMS_INVALID_ADDRESS;
    }

    root = rtems_container_get_root();
    if (root == NULL) {
        return RTEMS_NOT_CONFIGURED;
    }

#ifdef RTEMSCFG_PID_CONTAINER
    if ((container->flags & RTEMS_UNIFIED_CONTAINER_PID) != 0 &&
        thread->container->pidContainer == container->namespaces.pidContainer) {
        rtems_pid_container_move_task(
            container->namespaces.pidContainer,
            root->pidContainer,
            thread
        );
        thread->container->pidContainer = root->pidContainer;
    }
#endif

#ifdef RTEMSCFG_UTS_CONTAINER
    if ((container->flags & RTEMS_UNIFIED_CONTAINER_UTS) != 0 &&
        thread->container->utsContainer == container->namespaces.utsContainer) {
        rtems_uts_container_move_task(
            container->namespaces.utsContainer,
            root->utsContainer,
            thread
        );
    }
#endif

#ifdef RTEMSCFG_MNT_CONTAINER
    if ((container->flags & RTEMS_UNIFIED_CONTAINER_MNT) != 0 &&
        thread->container->mntContainer == container->namespaces.mntContainer) {
        rtems_mnt_container_move_task(
            container->namespaces.mntContainer,
            root->mntContainer,
            thread
        );
    }
#endif

#ifdef RTEMSCFG_NET_CONTAINER
    if ((container->flags & RTEMS_UNIFIED_CONTAINER_NET) != 0 &&
        thread->container->netContainer == container->namespaces.netContainer) {
        rtems_net_container_move_task(
            container->namespaces.netContainer,
            root->netContainer,
            thread
        );
    }
#endif

#ifdef RTEMSCFG_IPC_CONTAINER
    if ((container->flags & RTEMS_UNIFIED_CONTAINER_IPC) != 0 &&
        thread->container->ipcContainer == container->namespaces.ipcContainer) {
        rtems_ipc_container_move_task(
            container->namespaces.ipcContainer,
            root->ipcContainer,
            thread
        );
    }
#endif

#ifdef RTEMS_CGROUP
    if ((container->flags & (RTEMS_UNIFIED_CONTAINER_CPU | RTEMS_UNIFIED_CONTAINER_MEM)) != 0 &&
        thread->cgroup == container->core_cgroup) {
        sc = rtems_cgroup_remove_task(container->cgroup_id, thread->Object.id);
        if (sc != RTEMS_SUCCESSFUL && first_error == RTEMS_SUCCESSFUL) {
            first_error = sc;
        }
        thread->cgroup = NULL;
        thread->is_added_to_cgroup = false;
    }
#endif

#ifdef RTEMSCFG_IO_CGROUP
    if ((container->flags & RTEMS_UNIFIED_CONTAINER_IO) != 0) {
        sc = rtems_io_cgroup_remove_thread(container->io_cgroup, thread);
        if (sc != RTEMS_SUCCESSFUL &&
            sc != RTEMS_INVALID_ID &&
            first_error == RTEMS_SUCCESSFUL) {
            first_error = sc;
        }
    }
#endif

    if (container->attached_thread == thread) {
        container->attached = false;
        container->attached_thread = NULL;
    }

    return first_error;
}

rtems_status_code rtems_unified_container_delete(
    RtemsContainer *container
)
{
    rtems_status_code first_error = RTEMS_SUCCESSFUL;
    rtems_status_code sc;

    if (container == NULL) {
        return RTEMS_INVALID_ADDRESS;
    }

    if (container->attached && container->attached_thread != NULL) {
        first_error = rtems_unified_container_leave(
            container,
            container->attached_thread
        );
    }

#ifdef RTEMS_CGROUP
    if (container->cgroup_id != 0) {
        sc = rtems_cgroup_delete(container->cgroup_id);
        if (sc != RTEMS_SUCCESSFUL && first_error == RTEMS_SUCCESSFUL) {
            first_error = sc;
        }
        container->cgroup_id = 0;
        container->core_cgroup = NULL;
    }
#endif

#ifdef RTEMSCFG_IO_CGROUP
    if (container->io_cgroup == NULL && container->io_cgroup_id != 0) {
        container->io_cgroup = rtems_io_cgroup_get_by_id(container->io_cgroup_id);
    }

    if (container->io_cgroup != NULL) {
        rtems_io_cgroup_delete(container->io_cgroup);
        container->io_cgroup = NULL;
        container->io_cgroup_id = 0;
    }
#endif

#ifdef RTEMSCFG_PID_CONTAINER
    if (container->namespaces.pidContainer != NULL) {
        rtems_pid_container_delete(container->namespaces.pidContainer);
        container->namespaces.pidContainer = NULL;
    }
#endif

#ifdef RTEMSCFG_UTS_CONTAINER
    if (container->namespaces.utsContainer != NULL) {
        rtems_uts_container_delete(container->namespaces.utsContainer);
        container->namespaces.utsContainer = NULL;
    }
#endif

#ifdef RTEMSCFG_MNT_CONTAINER
    if (container->namespaces.mntContainer != NULL) {
        rtems_mnt_container_delete(container->namespaces.mntContainer);
        container->namespaces.mntContainer = NULL;
    }
#endif

#ifdef RTEMSCFG_NET_CONTAINER
    if (container->namespaces.netContainer != NULL) {
        rtems_net_container_delete(container->namespaces.netContainer);
        container->namespaces.netContainer = NULL;
    }
#endif

#ifdef RTEMSCFG_IPC_CONTAINER
    if (container->namespaces.ipcContainer != NULL) {
        rtems_ipc_container_delete(container->namespaces.ipcContainer);
        container->namespaces.ipcContainer = NULL;
    }
#endif

    free(container);
    return first_error;
}

Container *rtems_unified_container_get_namespaces(
    RtemsContainer *container
)
{
    return container != NULL ? &container->namespaces : NULL;
}

#ifdef RTEMS_CGROUP
CORE_cgroup_Control *rtems_unified_container_get_core_cgroup(
    RtemsContainer *container
)
{
    return container != NULL ? container->core_cgroup : NULL;
}
#endif

#ifdef RTEMSCFG_IO_CGROUP
IO_Cgroup_Control *rtems_unified_container_get_io_cgroup(
    RtemsContainer *container
)
{
    return container != NULL ? container->io_cgroup : NULL;
}
#endif

RTEMS_SYSINIT_ITEM(
  rtems_container_initialize_root,           // 初始化函数名
  RTEMS_SYSINIT_BSP_START,
  RTEMS_SYSINIT_ORDER_FIRST
);

// #ifdef RTEMSCFG_PID_CONTAINER
//     struct PidContainer *CurrentPidContainer = NULL;
// #endif

// 之前RTEMS_SYSINIT_ITEM参数
//   RTEMS_SYSINIT_LAST,            // 初始化阶段
//   RTEMS_SYSINIT_ORDER_MIDDLE     // 顺序

// RTEMS_SYSINIT_BSP_START
