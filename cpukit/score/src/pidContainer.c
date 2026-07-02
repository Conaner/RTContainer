// #ifdef RTEMSCFG_PID_CONTAINER
#include <stdlib.h>
#include <stdio.h>
#include <rtems/score/pidContainer.h>
#include <rtems/score/thread.h>
#include <rtems/score/threadimpl.h>

typedef struct ThreadNode {
    void *thread;
    struct ThreadNode *prev;
    struct ThreadNode *next;
} ThreadNode;

// 单链表形式
typedef struct VidNode {
    INT32 vid;
    struct VidNode *next;
} VidNode;

typedef struct {
    INT32            vid;  /* Virtual ID */
    // RtemsProcessCB    *cb;   /* Control block */
    Objects_Id      id;   /* Object ID */
} ProcessVid;

struct PidContainer {
    Atomic              rc;
    INT32               containerID;
    VidNode             *freeVidListHead;    
    ProcessVid          pidArray[RTEMS_MAX_PROCESS_LIMIT];
    ThreadNode          *threadListHead;
    INT32               pidCount;
};

// 全局容器ID计数器
static INT32 g_pidContainerId = 0;

// 初始化根PID容器
int rtems_pid_container_initialize_root(PidContainer **pidContainer)
{
    if (!pidContainer) return -1;

    *pidContainer = (PidContainer *)malloc(sizeof(PidContainer));
    if (!*pidContainer) return -1;

    (*pidContainer)->rc = 0;
    (*pidContainer)->pidCount = 0;
    (*pidContainer)->threadListHead = NULL;
    (*pidContainer)->freeVidListHead = NULL;
    (*pidContainer)->containerID = ++g_pidContainerId;

    for (int i = 0; i < RTEMS_MAX_PROCESS_LIMIT; i++) {
        (*pidContainer)->pidArray[i].vid = 0;
        (*pidContainer)->pidArray[i].id = 0;
    }

    return 0;
}

// 将线程加入容器
void rtems_pid_container_add_task(PidContainer *container, Thread_Control *thread)
{
    if (!container || !thread) return;
    if (container->pidCount >= RTEMS_MAX_PROCESS_LIMIT - 1) return;

    //分配vid
    INT32 vid;
    if (container->freeVidListHead) {
        VidNode *node = container->freeVidListHead;
        vid = node->vid;
        container->freeVidListHead = node->next;
        free(node);
    } else {
        vid = container->pidCount + 1;    // pidCount一定是小于RTEMS_MAX_PROCESS_LIMIT-1的
        // if (vid >= RTEMS_MAX_PROCESS_LIMIT) return;
        container->pidCount = vid;
    }
    container->rc += 1; 

    //更新pidArray，vid未分配代表pidArray[vid]空闲
    container->pidArray[vid].vid = vid;
    container->pidArray[vid].id = thread->Object.id;

    //插入线程链表
    ThreadNode *node = (ThreadNode *)malloc(sizeof(ThreadNode));
    if (!node) return;

    node->thread = thread;
    node->prev = NULL;
    node->next = container->threadListHead;
    if (container->threadListHead)
        container->threadListHead->prev = node;
    container->threadListHead = node;
} 

// 创建新的PID容器
PidContainer *rtems_pid_container_create(void)
{
    PidContainer *container = (PidContainer *)malloc(sizeof(PidContainer));
    if (!container) return NULL;
    container->rc = 0;
    container->pidCount = 0;
    container->threadListHead = NULL;
    container->freeVidListHead = NULL;
    container->containerID = ++g_pidContainerId;

    for (int i = 0; i < RTEMS_MAX_PROCESS_LIMIT; i++) {
        container->pidArray[i].vid = 0;
        container->pidArray[i].id = 0;
    }

    rtems_pid_container_add_to_list(container);

    return container;
}

// 从容器中移除线程
void rtems_pid_container_remove_task(PidContainer *container, Thread_Control *thread)
{
    if (!container || !thread) return;
    // 防止重复删除
    if (container->rc <= 0) return; 

    //从pidArray中移除并回收vid
    for (int i = 0; i <  RTEMS_MAX_PROCESS_LIMIT; ++i) {   //这个遍历的次数可以优化一下，之后再优化
        if (container->pidArray[i].id == thread->Object.id) {
            INT32 vid = container->pidArray[i].vid;
           //将vid加入到空闲列表中
            VidNode *node = (VidNode *)malloc(sizeof(VidNode));
            if (!node) return; // 内存分配失败

            node->vid = vid;
            node->next = NULL;

            VidNode **pp = &container->freeVidListHead;
            while (*pp && (*pp)->vid < vid) {
                pp = &((*pp)->next);
            }
            node->next = *pp;
            *pp = node;

            
            container->pidArray[i].vid = 0;
            container->pidArray[i].id = 0;
            break;
        }
    }

    //从链表中移除
    ThreadNode *current = container->threadListHead;
    while (current) {
        if (current->thread == thread) {
            if (current->prev)
                current->prev->next = current->next;
            else
                container->threadListHead = current->next;
            if (current->next)
                current->next->prev = current->prev;
            free(current);
            break;
        }
        current = current->next;
    }

    container->rc -= 1;   

    // 如果容器中没有线程了，删除容器
    if (container->rc == 0) {
        printf("[PID Container] rc=0, 自动删除容器 ID=%d\n", container->containerID);
        rtems_pid_container_delete(container);
    }
}

// 将线程从一个PID容器中移到另一个PID容器中
void rtems_pid_container_move_task(PidContainer *srcContainer, PidContainer *destContainer, Thread_Control *thread)
{
    if (!srcContainer || !destContainer || !thread) return;
    rtems_pid_container_remove_task(srcContainer, thread);
    rtems_pid_container_add_task(destContainer, thread);
}


void rtems_pid_container_add_to_list(PidContainer *pidContainer) {
    if (!pidContainer) return;

    Container *container = rtems_container_get_root();
    if (!container) return;

    PidContainerNode *node = (PidContainerNode *)malloc(sizeof(PidContainerNode));
    if (!node) return;

    node->pidContainer = pidContainer;
    node->next = container->pidContainerListHead;
    container->pidContainerListHead = node;
}

// 后续调用这个函数的时候先把容器里面的线程清空（转移到根容器）
void rtems_pid_container_remove_from_list(PidContainer *pidContainer) {
    if (!pidContainer) return;

    Container *container = rtems_container_get_root();
    if (!container) return;

    PidContainerNode **current = &container->pidContainerListHead;
    while (*current) {
        if ((*current)->pidContainer == pidContainer) {
            PidContainerNode *toRemove = *current;
            *current = toRemove->next;
            free(toRemove);
            break;
        }
        current = &(*current)->next;
    }
}

void rtems_pid_container_delete(PidContainer *pidContainer)
{
    if (!pidContainer) return;

    Container *container = rtems_container_get_root();
    if (!container || !container->pidContainer) return;

    PidContainer *root = container->pidContainer;
    // 如果是根容器，直接返回
    if (pidContainer == root) return;

    // 从链表中移除该容器
    rtems_pid_container_remove_from_list(pidContainer);

    // 防止递归多次删除
    pidContainer->rc = -1;  

    // 迁移所有线程到根容器
    ThreadNode *node = pidContainer->threadListHead;
    while (node) {
        ThreadNode *next = node->next;
        Thread_Control *thread = (Thread_Control *)node->thread;
        // 先从当前容器移除，再加到根容器
        rtems_pid_container_remove_task(pidContainer, thread);
        rtems_pid_container_add_task(root, thread);
        node = next;
    }

    // 释放自身内存
    free(pidContainer);
}

PidContainer *rtems_pid_container_find_by_thread(Thread_Control *thread)
{
    if (!thread) return NULL;

    Container *root = rtems_container_get_root();
    if (!root) return NULL;

    // 先查根容器
    PidContainer *container = root->pidContainer;
    if (container) {
        ThreadNode *node = container->threadListHead;
        while (node) {
            if (node->thread == thread) {
                return container;
            }
            node = node->next;
        }
    }

    // 再查链表中的其它容器
    PidContainerNode *pnode = root->pidContainerListHead;
    while (pnode) {
        PidContainer *c = pnode->pidContainer;
        if (c) {
            ThreadNode *node = c->threadListHead;
            while (node) {
                if (node->thread == thread) {
                    return c;
                }
                node = node->next;
            }
        }
        pnode = pnode->next;
    }

    return NULL;
}

ThreadContainerInfo rtems_pid_container_get_thread_info(Thread_Control *thread) 
{
      ThreadContainerInfo info = { .containerID = -1, .vid_or_id = -1, .isRoot = -1 };

    if (!thread)
        return info;

    Container *root = rtems_container_get_root();
    if (!root || !root->pidContainer)
        return info;

    PidContainer *found = rtems_pid_container_find_by_thread(thread);
    if (!found)
        return info;

    info.containerID = found->containerID;

    if (found == root->pidContainer) {
        info.vid_or_id = thread->Object.id;
        info.isRoot = 1;
    } else {
        // 先找thread，再查vid
        ThreadNode *node = found->threadListHead;
        while (node) {
            if (node->thread == thread) {
                for (int i = 0; i < RTEMS_MAX_PROCESS_LIMIT; ++i) {
                    if (found->pidArray[i].id == thread->Object.id && found->pidArray[i].vid != 0) {
                        info.vid_or_id = found->pidArray[i].vid;
                        info.isRoot = 0;
                        return info;
                    }
                }
            }
            node = node->next;
        }
        info.vid_or_id = -1;
        info.isRoot = 0;
    }
    return info;  
}

int rtems_pid_container_exists(PidContainer *pidContainer)
{
    if (!pidContainer)
        return 0;

    Container *root = rtems_container_get_root();
    if (!root)
        return 0;

    // 判断是否为根容器
    if (root->pidContainer == pidContainer)
        return 1;

    // 遍历链表查找
    PidContainerNode *node = root->pidContainerListHead;
    while (node) {
        if (node->pidContainer == pidContainer)
            return 1;
        node = node->next;
    }
    return 0;
}

void rtems_pid_container_inc_rc(PidContainer *container) {
    if (container) container->rc++;
}

int rtems_pid_container_get_id(PidContainer *container) {
    return container ? container->containerID : -1;
}

int rtems_pid_container_get_rc(PidContainer *container) {
    return container ? container->rc : -1;
}

void rtems_pid_container_foreach_thread(PidContainer *container, bool (*visitor)(Thread_Control *, void *), void *arg) {
    if (!container || !visitor) return;
    ThreadNode *node = container->threadListHead;
    while (node) {
        if (!visitor((Thread_Control *)node->thread, arg)) break;
        node = node->next;
    }
}