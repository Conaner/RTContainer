#include <rtems/score/utsContainer.h>
#include <rtems/score/thread.h>
#include <rtems/rtems/tasks.h> 
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <rtems/score/threadimpl.h>

#include <rtems/score/containerlog.h>

static int g_utsContainerId = 0;
/* Static root pointer avoids accidental root deletion after task moves. */
static UtsContainer *g_rootUtsContainer = NULL;

// 后面在线程创建时，直接将它的container指针指向RootUtsContainer，之后在进行其他操作
int rtems_uts_container_initialize_root(UtsContainer **utsContainer)
{
    CONTAINER_LOG_TRACE("Initializing root UTS container");
    if (!utsContainer) {
        CONTAINER_LOG_ERROR("UTS container pointer is NULL");
        return -1;
    }

    *utsContainer = (UtsContainer *)malloc(sizeof(UtsContainer));
    if (!*utsContainer) {
        CONTAINER_LOG_ERROR("Failed to allocate memory for root UTS container");
        return -1;
    }

    (*utsContainer)->rc = 0;
    (*utsContainer)->ID = ++g_utsContainerId;
    strcpy((*utsContainer)->name, "root"); // 默认主机名
    g_rootUtsContainer = *utsContainer;
    CONTAINER_LOG_INFO(
      "Root UTS container initialized successfully: ID=%d, name=%s",
      (*utsContainer)->ID,
      (*utsContainer)->name
    );

    return 0;
}

UtsContainer *rtems_uts_container_create(const char *name)
{
    CONTAINER_LOG_TRACE("Creating new UTS container: name=%s", name ? name : "NULL");
    UtsContainer *container = (UtsContainer *)malloc(sizeof(UtsContainer));
    if (!container) {
        CONTAINER_LOG_ERROR("Failed to allocate memory for new UTS container");
        return NULL;
    }
    container->rc = 0;
    container->ID = ++g_utsContainerId;
    if (name && name[0]) {
        strncpy(container->name, name, sizeof(container->name) - 1);
        container->name[sizeof(container->name) - 1] = '\0';
    } else {
        strcpy(container->name, "uts");
    }      

    rtems_uts_container_add_to_list(container);
    CONTAINER_LOG_INFO(
      "New UTS container created successfully: ID=%d, name=%s",
      container->ID,
      container->name
    );

    return container;
}

// 后续切换容器的时候要使用这个函数，避免容器rc为零之后无法自动删除
void rtems_uts_container_move_task(UtsContainer *srcContainer, UtsContainer *destContainer, Thread_Control *thread)
{
    CONTAINER_LOG_TRACE(
      "Moving task between UTS containers: src_id=%d, dest_id=%d, thread_id=%" PRIu32,
      srcContainer ? srcContainer->ID : -1,
      destContainer ? destContainer->ID : -1,
      thread ? thread->Object.id : 0
    );
    if (!srcContainer || !destContainer || !thread) {
        CONTAINER_LOG_ERROR("Invalid parameters for task move");
        return;
    }
    if (thread->container && thread->container->utsContainer == srcContainer) {
        thread->container->utsContainer = destContainer;
        srcContainer->rc--;
        if (srcContainer->rc <= 0) {
            rtems_uts_container_delete(srcContainer);
        }
        destContainer->rc++;
        CONTAINER_LOG_INFO(
          "Task moved successfully: thread_id=%" PRIu32 " from uts=%d to uts=%d",
          thread->Object.id,
          srcContainer->ID,
          destContainer->ID
        );
    } else {
        CONTAINER_LOG_WARN(
          "Thread not in source UTS container: thread_id=%" PRIu32 ", src_id=%d",
          thread->Object.id,
          srcContainer->ID
        );
    }
}

// 回调函数，供删除UTS容器使用
static bool switch_to_root(Thread_Control *thread, void *arg)
{
    UtsContainer *utsContainer = (UtsContainer *)arg;
    UtsContainer *root = g_rootUtsContainer;
    if (!root) return true;

    if (thread->container && thread->container->utsContainer == utsContainer) {
        thread->container->utsContainer = root;
        root->rc++;
        utsContainer->rc--;
    }
    return true;
}

void rtems_uts_container_delete(UtsContainer *utsContainer)
{
    CONTAINER_LOG_TRACE(
      "Deleting UTS container: container_id=%d",
      utsContainer ? utsContainer->ID : -1
    );
    if (!utsContainer) {
        CONTAINER_LOG_ERROR("Attempting to delete NULL UTS container");
        return;
    }
    if (utsContainer == g_rootUtsContainer) {
        CONTAINER_LOG_WARN("Attempting to delete root UTS container, operation denied");
        return;
    }
    Container *container = rtems_container_get_root();
    if (!container || !g_rootUtsContainer) {
        CONTAINER_LOG_ERROR("Root container or root UTS pointer is NULL");
        return;
    }
    UtsContainer *root = g_rootUtsContainer;

    // 遍历所有线程，将指向该容器的线程迁移到根容器
    rtems_task_iterate(switch_to_root, utsContainer);

    // 主动处理当前正在运行的线程，rtems_task_iterate没遍历到主线程（即正在执行的线程）
    Thread_Control *self = (Thread_Control *)_Thread_Get_executing();
    if (self && self->container && self->container->utsContainer == utsContainer) {
        self->container->utsContainer = root;
        root->rc++;
        utsContainer->rc--;
    }

    rtems_uts_container_remove_from_list(utsContainer);
    free(utsContainer);
    CONTAINER_LOG_INFO("UTS container deleted successfully");
}

UtsContainer *rtems_uts_container_find_by_thread(Thread_Control *thread)
{
    if (!thread) return NULL;
    return thread->container->utsContainer;
}

void rtems_uts_container_add_to_list(UtsContainer *utsContainer) {
    if (!utsContainer) return;
    Container *container = rtems_container_get_root();
    if (!container) return;

    UtsContainerNode **head = (UtsContainerNode **)&container->utsContainerListHead;
    UtsContainerNode *node = (UtsContainerNode *)malloc(sizeof(UtsContainerNode));
    if (!node) {
        CONTAINER_LOG_ERROR("Failed to allocate memory for UTS container list node");
        return;
    }
    node->utsContainer = utsContainer;
    node->next = *head;
    *head = node;
    CONTAINER_LOG_DEBUG("UTS container added to list: ID=%d", utsContainer->ID);
}

void rtems_uts_container_remove_from_list(UtsContainer *utsContainer) {
    if (!utsContainer) return;
    Container *container = rtems_container_get_root();
    if (!container) return;

    UtsContainerNode **head = (UtsContainerNode **)&container->utsContainerListHead;
    UtsContainerNode **cur = head;
    while (*cur) {
        if ((*cur)->utsContainer == utsContainer) {
            UtsContainerNode *toRemove = *cur;
            *cur = toRemove->next;
            free(toRemove);
            CONTAINER_LOG_DEBUG("UTS container removed from list: ID=%d", utsContainer->ID);
            break;
        }
        cur = &(*cur)->next;
    }
}

int rtems_uts_container_exists(UtsContainer *utsContainer)
{
    if (!utsContainer) return 0;
    Container *container = rtems_container_get_root();
    if (!container) return 0;
    if (container->utsContainer == utsContainer) return 1;
    UtsContainerNode *node = (UtsContainerNode *)container->utsContainerListHead;
    while (node) {
        if (node->utsContainer == utsContainer) return 1;
        node = node->next;
    }
    return 0;
}
