#include <rtems/score/ipcContainer.h>
#include <rtems/score/container.h>
#include <rtems/score/threadimpl.h>
#include <rtems/score/wkspace.h>
#include <rtems/score/objectimpl.h>
#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <rtems/rtems/semdata.h>
#include <rtems/rtems/intr.h>

#include <rtems/score/containerlog.h>

static int g_ipcContainerIdCounter = 1;
static int g_currentIpcContainerNum = 0;

static void _IPC_Container_Initialize_RTEMS_Message_Queue_Info(IpcContainer *container, Objects_Maximum max_objects);
// static void _IPC_Container_Initialize_POSIX_Message_Queue_Info(IpcContainer *container, Objects_Maximum max_objects);
static void _IPC_Container_Initialize_Semaphore_Info(IpcContainer *container, Objects_Maximum max_objects);

int rtems_ipc_container_initialize_root(IpcContainer **ipcContainer)
{
    CONTAINER_LOG_TRACE("Initializing root IPC container");
    if (ipcContainer == NULL)
    {
        CONTAINER_LOG_ERROR("IPC container pointer is NULL");
        return -1;
    }

    *ipcContainer = (IpcContainer *)malloc(sizeof(IpcContainer));
    if (*ipcContainer == NULL)
    {
        CONTAINER_LOG_ERROR("Failed to allocate memory for root IPC container");
        return -1;
    }
    memset(*ipcContainer, 0, sizeof(IpcContainer));
    (*ipcContainer)->rc = 3;
    (*ipcContainer)->containerID = 1;

    _IPC_Container_Initialize_RTEMS_Message_Queue_Info(*ipcContainer, 10);
    _IPC_Container_Initialize_Semaphore_Info(*ipcContainer, 20);
    g_currentIpcContainerNum++;
    CONTAINER_LOG_INFO("Root IPC container initialized successfully: ID=%d", (*ipcContainer)->containerID);

    return 0;
}

IpcContainer *rtems_ipc_container_create(void)
{
    CONTAINER_LOG_TRACE("Creating new IPC container");
    IpcContainer *ipcContainer = (IpcContainer *)malloc(sizeof(IpcContainer));
    if (ipcContainer == NULL)
    {
        CONTAINER_LOG_ERROR("Failed to allocate memory for new IPC container");
        return NULL;
    }

    memset(ipcContainer, 0, sizeof(IpcContainer));
    ipcContainer->rc = 1;
    ipcContainer->containerID = ++g_ipcContainerIdCounter;

    _IPC_Container_Initialize_RTEMS_Message_Queue_Info(ipcContainer, 5);
    // _IPC_Container_Initialize_POSIX_Message_Queue_Info(ipcContainer, 3);
    _IPC_Container_Initialize_Semaphore_Info(ipcContainer, 10);

    g_currentIpcContainerNum++;

    rtems_ipc_container_add_to_list(ipcContainer);
    CONTAINER_LOG_INFO("New IPC container created successfully: ID=%d", ipcContainer->containerID);

    return ipcContainer;
}

void rtems_ipc_container_delete(IpcContainer *ipcContainer)
{
    if (!ipcContainer)
    {
        CONTAINER_LOG_ERROR("Attempting to delete NULL IPC container");
        return;
    }

    Container *container = rtems_container_get_root();
    if (!container || !container->ipcContainer)
    {
        CONTAINER_LOG_ERROR("Root container or IPC container is NULL");
        return;
    }
    IpcContainer *root = container->ipcContainer;
    if (ipcContainer == root) // 不能删除根容器
    {
        CONTAINER_LOG_WARN("Attempting to delete root IPC container, operation denied");
        return;
    }
    rtems_ipc_container_remove_from_list(ipcContainer);

    if (ipcContainer->rtems_message_queue_info.initial_objects)
    {
        free((void *)ipcContainer->rtems_message_queue_info.initial_objects);
    }
    if (ipcContainer->rtems_message_queue_info.local_table)
    {
        free(ipcContainer->rtems_message_queue_info.local_table);
    }

    // if (ipcContainer->posix_message_queue_info.initial_objects)
    // {
    //     free((void *)ipcContainer->posix_message_queue_info.initial_objects);
    // }
    // if (ipcContainer->posix_message_queue_info.local_table)
    // {
    //     free(ipcContainer->posix_message_queue_info.local_table);
    // }

    if (ipcContainer->semaphore_info.initial_objects)
    {
        free((void *)ipcContainer->semaphore_info.initial_objects);
    }
    if (ipcContainer->semaphore_info.local_table)
    {
        free(ipcContainer->semaphore_info.local_table);
    }

    free(ipcContainer);
    g_currentIpcContainerNum--;
    CONTAINER_LOG_INFO("IPC container deleted successfully");
}

void rtems_ipc_container_add_to_list(IpcContainer *ipcContainer)
{
    if (!ipcContainer)
    {
        return;
    }

    Container *container = rtems_container_get_root();
    if (!container)
    {
        return;
    }
    IpcContainerNode **head = (IpcContainerNode **)&container->ipcContainerListHead;
    IpcContainerNode *new_node = (IpcContainerNode *)malloc(sizeof(IpcContainerNode));
    if (!new_node)
    {
        CONTAINER_LOG_ERROR("Failed to allocate memory for IPC container list node");
        return;
    }
    new_node->ipcContainer = ipcContainer;
    new_node->next = *head;
    *head = new_node;
    CONTAINER_LOG_DEBUG("IPC container added to list: ID=%d", ipcContainer->containerID);
}

void rtems_ipc_container_remove_from_list(IpcContainer *ipcContainer)
{
    if (!ipcContainer)
    {
        return;
    }

    Container *container = rtems_container_get_root();
    if (!container)
    {
        return;
    }
    IpcContainerNode **head = (IpcContainerNode **)&container->ipcContainerListHead;
    IpcContainerNode *current = *head;
    IpcContainerNode *prev = NULL;
    while (current)
    {
        if (current->ipcContainer == ipcContainer)
        {
            if (prev)
            {
                prev->next = current->next;
            }
            else
            {
                *head = current->next;
            }
            free(current);
            CONTAINER_LOG_DEBUG("IPC container removed from list: ID=%d", ipcContainer->containerID);
            return;
        }
        prev = current;
        current = current->next;
    }
}

void rtems_ipc_container_move_task(IpcContainer *srcContainer, IpcContainer *destContainer, Thread_Control *thread)
{
    if (!srcContainer || !destContainer || !thread) {
        CONTAINER_LOG_ERROR("Invalid parameters for IPC task move");
        return;
    }
    if (thread->container && thread->container->ipcContainer == srcContainer)
    {
        thread->container->ipcContainer = destContainer;
        srcContainer->rc--;
        if (srcContainer->rc <= 0 && srcContainer->containerID != 1)
        {
            rtems_ipc_container_delete(srcContainer);
        }
        destContainer->rc++;
        CONTAINER_LOG_INFO(
          "Task moved successfully: thread_id=%" PRIu32 " from ipc=%d to ipc=%d",
          thread->Object.id,
          srcContainer->containerID,
          destContainer->containerID
        );
    } else {
        CONTAINER_LOG_WARN(
          "Thread not in source IPC container: thread_id=%" PRIu32 ", src_id=%d",
          thread->Object.id,
          srcContainer->containerID
        );
    }
}

int rtems_ipc_container_get_id(IpcContainer *ipcContainer)
{
    return ipcContainer ? ipcContainer->containerID : -1;
}

int rtems_ipc_container_get_rc(IpcContainer *ipcContainer)
{
    return ipcContainer ? ipcContainer->rc : 0;
}

// Objects_Information *_Message_queue_Get_information_from_container(void)
// {
// #ifdef RTEMSCFG_IPC_CONTAINER
//     Thread_Control *executing = _Thread_Get_executing();
//     if (executing && executing->container && executing->container->ipcContainer)
//     {
//         printf("DEBUG: [Message_queue] 使用容器消息队列信息，容器ID=%d\n",
//                executing->container->ipcContainer->containerID);
//         return &executing->container->ipcContainer->rtems_message_queue_info;
//     }
// #endif
//     return &_Message_queue_Information;
// }

Objects_Information *_Message_queue_Get_information_from_container(void)
{
#ifdef RTEMSCFG_IPC_CONTAINER
    rtems_interrupt_level level;
    rtems_interrupt_disable(level);
    Thread_Control *executing = _Thread_Get_executing();
    rtems_interrupt_enable(level);
    
    if (executing && executing->container && executing->container->ipcContainer)
    {
        IpcContainer *ipc_Container = executing->container->ipcContainer;
        // printf("DEBUG: [Message_queue] 使用容器消息队列信息，容器ID=%d\n",
        //        ipc_Container->containerID);
        return &ipc_Container->rtems_message_queue_info;
    }
#endif
    return &_Message_queue_Information;
}

Objects_Information *_POSIX_Message_queue_Get_information_from_container(void)
{
    // #ifdef RTEMSCFG_IPC_CONTAINER
    //     Thread_Control *executing = _Thread_Get_executing();
    //     if (executing && executing->container && executing->container->ipcContainer)
    //     {
    //         return &executing->container->ipcContainer->posix_message_queue_info;
    //     }
    // #endif
    return &_POSIX_Message_queue_Information;
}

Objects_Information *_Semaphore_Get_information_from_container(void)
{
#ifdef RTEMSCFG_IPC_CONTAINER
    rtems_interrupt_level level;
    rtems_interrupt_disable(level);
    Thread_Control *executing = _Thread_Get_executing();
    rtems_interrupt_enable(level);
    
    if (executing && executing->container && executing->container->ipcContainer)
    {
        IpcContainer *ipc_Container = executing->container->ipcContainer;
        return &ipc_Container->semaphore_info;
    }
#endif
    return &_Semaphore_Information;
}

// rtems queue初始化
static void _IPC_Container_Initialize_RTEMS_Message_Queue_Info(
    IpcContainer *container,
    Objects_Maximum max_objects)
{
    Objects_Information *info = &container->rtems_message_queue_info;
    memset(info, 0, sizeof(Objects_Information));
    size_t object_size = sizeof(Message_queue_Control);
    Message_queue_Control *objects = (Message_queue_Control *)
        malloc(object_size * max_objects);
    if (!objects)
    {
        return;
    }
    memset(objects, 0, object_size * max_objects);

    size_t table_size = sizeof(Objects_Control *) * (max_objects + 1);
    Objects_Control **local_table = (Objects_Control **)
        malloc(table_size);
    if (!local_table)
    {
        free(objects);
        return;
    }
    memset(local_table, 0, table_size);

    info->maximum_id = _Objects_Build_id(
        OBJECTS_CLASSIC_API,
        OBJECTS_RTEMS_MESSAGE_QUEUES,
        container->containerID,     
        max_objects);
    info->local_table = local_table;
    info->allocate = _Objects_Allocate_static;
    info->deallocate = _Objects_Free_static;
    info->inactive = max_objects;
    info->objects_per_block = 0;
    info->object_size = object_size;
    info->name_length = OBJECTS_NO_STRING_NAME;
    info->initial_objects = &objects[0].Object;

    _Chain_Initialize_empty(&info->Inactive);

    for (Objects_Maximum i = 0; i < max_objects; i++)
    {
        Objects_Control *obj = &objects[i].Object;
        obj->id = _Objects_Build_id(
            OBJECTS_CLASSIC_API,
            OBJECTS_RTEMS_MESSAGE_QUEUES,
            container->containerID,     
            i + OBJECTS_INDEX_MINIMUM);
        _Chain_Initialize_node(&obj->Node);
        _Chain_Append_unprotected(&info->Inactive, &obj->Node);
        local_table[i + OBJECTS_INDEX_MINIMUM] = NULL;
    }
    _Objects_Initialize_information(info);
}

static void _IPC_Container_Initialize_POSIX_Message_Queue_Info(
    IpcContainer *container,
    Objects_Maximum max_objects)
{
    Objects_Information *info = &container->posix_message_queue_info;
    memset(info, 0, sizeof(Objects_Information));

    size_t object_size = sizeof(POSIX_Message_queue_Control);
    POSIX_Message_queue_Control *objects = (POSIX_Message_queue_Control *)
        malloc(object_size * max_objects);
    if (!objects)
    {
        return;
    }
    memset(objects, 0, object_size * max_objects);

    size_t table_size = sizeof(Objects_Control *) * (max_objects + 1);
    Objects_Control **local_table = (Objects_Control **)
        malloc(table_size);
    if (!local_table)
    {
        free(objects);
        return;
    }
    memset(local_table, 0, table_size);

    info->maximum_id = _Objects_Build_id(
        OBJECTS_POSIX_API,
        OBJECTS_POSIX_MESSAGE_QUEUES,
        1,
        max_objects);
    info->local_table = local_table;
    info->allocate = _Objects_Allocate_static;
    info->deallocate = _Objects_Free_static;
    info->inactive = max_objects;
    info->objects_per_block = 0;
    info->object_size = object_size;
    info->name_length = 255;
    info->initial_objects = &objects[0].Object;

    _Chain_Initialize_empty(&info->Inactive);

    for (Objects_Maximum i = 0; i < max_objects; i++)
    {
        Objects_Control *obj = &objects[i].Object;
        obj->id = _Objects_Build_id(
            OBJECTS_POSIX_API,
            OBJECTS_POSIX_MESSAGE_QUEUES,
            1,
            i + OBJECTS_INDEX_MINIMUM);
        _Chain_Initialize_node(&obj->Node);
        _Chain_Append_unprotected(&info->Inactive, &obj->Node);
        local_table[i + OBJECTS_INDEX_MINIMUM] = NULL;
    }
    _Objects_Initialize_information(info);
}

static Objects_Control *_IPC_Semaphore_Allocate_static(
    Objects_Information *information
)
{
    Objects_Control *the_object = NULL;
    Objects_Maximum block = 0;
    
    the_object = _Objects_Get_inactive(information);
    
    if (the_object != NULL) {
        Objects_Id id = the_object->id;
        Objects_Maximum index = _Objects_Get_index(id);
        
        _Objects_Set_local_object(information, index, the_object);
        
        information->inactive--;
        
    //     printf("DEBUG: 分配信号量对象成功 - ID: 0x%08x, 索引: %d, 剩余: %d\n",
    //            (unsigned int)id, index, information->inactive);
    // } else {
    //     printf("DEBUG: 没有可用的非活动对象\n");
    }
    
    return the_object;
}

static void _IPC_Semaphore_Free_static(
    Objects_Information *information,
    Objects_Control *the_object
)
{
    if (the_object != NULL) {
        Objects_Maximum index = _Objects_Get_index(the_object->id);
        
        information->local_table[index] = NULL;

        _Chain_Initialize_node(&the_object->Node);
        _Chain_Append_unprotected(&information->Inactive, &the_object->Node);

        information->inactive++;
        
        // printf("DEBUG: 释放信号量对象成功 - ID: 0x%08x, 索引: %d, 当前: %d\n",
        //        (unsigned int)the_object->id, index, information->inactive);
    }
}

static void _IPC_Container_Initialize_Semaphore_Info(
    IpcContainer *container,
    Objects_Maximum max_objects)
{
    Objects_Information *info = &container->semaphore_info;
    memset(info, 0, sizeof(Objects_Information));

    size_t object_size = sizeof(Semaphore_Control);
    Semaphore_Control *objects = (Semaphore_Control *)
        malloc(object_size * max_objects);
    if (!objects)
    {
        return;
    }
    memset(objects, 0, object_size * max_objects);

    size_t table_size = sizeof(Objects_Control *) * (max_objects + 1);
    Objects_Control **local_table = (Objects_Control **)
        malloc(table_size);
    if (!local_table)
    {
        free(objects);
        return;
    }
    memset(local_table, 0, table_size);

    info->maximum_id = _Objects_Build_id(
        OBJECTS_CLASSIC_API,
        OBJECTS_RTEMS_SEMAPHORES,
        container->containerID,    
        max_objects);
    info->local_table = local_table;
    info->allocate = _IPC_Semaphore_Allocate_static;
    info->deallocate = _IPC_Semaphore_Free_static;
    info->inactive = max_objects;
    info->objects_per_block = 0;
    info->object_size = object_size;
    info->name_length = OBJECTS_NO_STRING_NAME;
    info->initial_objects = &objects[0].Object;

    _Chain_Initialize_empty(&info->Inactive);

    // 将所有对象添加到非活跃链表
    for (Objects_Maximum i = 0; i < max_objects; i++)
    {
        Objects_Control *obj = &objects[i].Object;
        
        obj->id = _Objects_Build_id(
            OBJECTS_CLASSIC_API,
            OBJECTS_RTEMS_SEMAPHORES,
            container->containerID,     
            i + OBJECTS_INDEX_MINIMUM);
        _Chain_Initialize_node(&obj->Node);
        _Chain_Append_unprotected(&info->Inactive, &obj->Node);

        local_table[i + OBJECTS_INDEX_MINIMUM] = NULL; 
    }

    _Objects_Initialize_information(info);

    // printf("DEBUG: 信号量信息初始化完成，容器ID: %d，最大对象数: %d\n", 
    //        container->containerID, max_objects);
    // printf("DEBUG: maximum_id: 0x%08x\n", info->maximum_id);
    // printf("DEBUG: local_table地址: %p\n", (void *)info->local_table);
    // printf("DEBUG: inactive对象数: %d\n", info->inactive);
}
