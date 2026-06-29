#ifndef _RTEMS_SCORE_CONTAINER_H
#define _RTEMS_SCORE_CONTAINER_H

#include <rtems/sysinit.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <rtems/rtems/status.h>
#include <rtems/rtems/types.h>
// #include <stdlib.h>
#ifdef RTEMS_CGROUP
#include <rtems/score/corecgroup.h>
#endif
#ifdef RTEMSCFG_PID_CONTAINER
#include "pidContainer.h"
#endif
#ifdef RTEMSCFG_UTS_CONTAINER
#include "utsContainer.h"
#endif
#ifdef RTEMSCFG_MNT_CONTAINER
#include "mntContainer.h"
#endif
#ifdef RTEMSCFG_NET_CONTAINER
#include "netContainer.h"
#endif
#ifdef RTEMSCFG_IPC_CONTAINER
#include "ipcContainer.h"
#endif

typedef signed int INT32;
typedef volatile INT32 Atomic;

typedef struct _Thread_Control Thread_Control;
#ifdef RTEMSCFG_IO_CGROUP
typedef struct IO_Cgroup_Control IO_Cgroup_Control;
#endif

#ifdef RTEMSCFG_PID_CONTAINER
typedef struct PidContainerNode
{
    struct PidContainer *pidContainer;
    struct PidContainerNode *next;
} PidContainerNode;
#endif

#ifdef RTEMSCFG_UTS_CONTAINER
typedef struct UtsContainerNode
{
    UtsContainer *utsContainer;
    struct UtsContainerNode *next;
} UtsContainerNode;
#endif

#ifdef RTEMSCFG_MNT_CONTAINER
typedef struct MntContainerNode
{
    MntContainer *mntContainer;
    struct MntContainerNode *next;
} MntContainerNode;
#endif

// typedef struct IpcContainerNode
// {
//     IpcContainer *ipcContainer;
//     struct IpcContainerNode *next;
// } IpcContainerNode;

#ifdef RTEMSCFG_NET_CONTAINER
typedef struct NetContainerNode {
    NetContainer *netContainer;
    struct NetContainerNode *next;
} NetContainerNode;
#endif

#ifdef RTEMSCFG_IPC_CONTAINER
typedef struct IpcContainerNode {
    IpcContainer *ipcContainer;
    struct IpcContainerNode *next;
} IpcContainerNode;
#endif

typedef struct Container
{
    Atomic rc;
#ifdef RTEMSCFG_PID_CONTAINER
    struct PidContainer *pidContainer;      // 根pid容器
    PidContainerNode *pidContainerListHead; // 非根pid容器链表头
    // struct PidContainer *pidForChildContainer;
#endif
#ifdef RTEMSCFG_UTS_CONTAINER
    struct UtsContainer *utsContainer;      // 容器对应的uts容器
    UtsContainerNode *utsContainerListHead; // 非根uts容器链表头，只有根uts容器存有
#endif
#ifdef RTEMSCFG_MNT_CONTAINER
    struct MntContainer *mntContainer;      // 容器对应的mnt容器
    MntContainerNode *mntContainerListHead; // 非根mnt容器链表头，只有根mnt容器存有
#endif
#ifdef RTEMSCFG_NET_CONTAINER
    struct NetContainer *netContainer;      // 容器对应的net容器
    NetContainerNode *netContainerListHead; // 非根net容器链表头，只有根net容器存有
#endif
#ifdef RTEMSCFG_IPC_CONTAINER
    struct IpcContainer *ipcContainer;      // 容器对应的ipc容器
    IpcContainerNode *ipcContainerListHead; // 非根ipc容器链表头，只有根ipc容器存有
#endif
} Container;

typedef enum {
    RTEMS_UNIFIED_CONTAINER_PID = 1u << 0,
    RTEMS_UNIFIED_CONTAINER_IPC = 1u << 1,
    RTEMS_UNIFIED_CONTAINER_MNT = 1u << 2,
    RTEMS_UNIFIED_CONTAINER_NET = 1u << 3,
    RTEMS_UNIFIED_CONTAINER_UTS = 1u << 4,
    RTEMS_UNIFIED_CONTAINER_CPU = 1u << 5,
    RTEMS_UNIFIED_CONTAINER_MEM = 1u << 6,
    RTEMS_UNIFIED_CONTAINER_IO  = 1u << 7,
    RTEMS_UNIFIED_CONTAINER_ALL =
        RTEMS_UNIFIED_CONTAINER_PID |
        RTEMS_UNIFIED_CONTAINER_IPC |
        RTEMS_UNIFIED_CONTAINER_MNT |
        RTEMS_UNIFIED_CONTAINER_NET |
        RTEMS_UNIFIED_CONTAINER_UTS |
        RTEMS_UNIFIED_CONTAINER_CPU |
        RTEMS_UNIFIED_CONTAINER_MEM |
        RTEMS_UNIFIED_CONTAINER_IO
} RtemsUnifiedContainerFlags;

typedef struct RtemsContainerConfig
{
    uint32_t flags;
    const char *uts_name;
#ifdef RTEMS_CGROUP
    CORE_cgroup_config cgroup_config;
#endif
#ifdef RTEMSCFG_IO_CGROUP
    uint64_t io_system_read_bps;
    uint64_t io_system_write_bps;
    uint64_t io_read_bps_limit;
    uint64_t io_write_bps_limit;
    uint32_t io_window_size_ms;
    uint16_t io_weight;
    uint32_t io_thread_weight;
#endif
} RtemsContainerConfig;

typedef struct RtemsContainer
{
    uint32_t flags;
    bool attached;
    Thread_Control *attached_thread;
    Container namespaces;
#ifdef RTEMS_CGROUP
    rtems_id cgroup_id;
    CORE_cgroup_config cgroup_config;
    CORE_cgroup_Control *core_cgroup;
#endif
#ifdef RTEMSCFG_IO_CGROUP
    uint32_t io_cgroup_id;
    IO_Cgroup_Control *io_cgroup;
    uint32_t io_thread_weight;
#endif
} RtemsContainer;

void rtems_container_initialize_root(void);
Container *rtems_container_get_root(void);

void rtems_unified_container_config_initialize(
    RtemsContainerConfig *config
);

rtems_status_code rtems_unified_container_create(
    const RtemsContainerConfig *config,
    RtemsContainer **container
);

rtems_status_code rtems_unified_container_enter(
    RtemsContainer *container,
    Thread_Control *thread
);

rtems_status_code rtems_unified_container_leave(
    RtemsContainer *container,
    Thread_Control *thread
);

rtems_status_code rtems_unified_container_delete(
    RtemsContainer *container
);

Container *rtems_unified_container_get_namespaces(
    RtemsContainer *container
);

#ifdef RTEMS_CGROUP
CORE_cgroup_Control *rtems_unified_container_get_core_cgroup(
    RtemsContainer *container
);
#endif

#ifdef RTEMSCFG_IO_CGROUP
IO_Cgroup_Control *rtems_unified_container_get_io_cgroup(
    RtemsContainer *container
);
#endif

#endif /* _RTEMS_SCORE_CONTAINER_H */
