#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <rtems.h>
#include <rtems/io_cgroup.h>
#include <rtems/rtems/cgroup.h>
#include <rtems/rtems_bsdnet.h>
#include <rtems/score/container.h>
#include <rtems/score/corecgroupimpl.h>
#include <rtems/score/ipcContainer.h>
#include <rtems/score/mntContainer.h>
#include <rtems/score/netContainer.h>
#include <rtems/score/pidContainer.h>
#include <rtems/score/threadimpl.h>
#include <rtems/score/utsContainer.h>
#include <tmacros.h>

#include <string.h>
#include <unistd.h>

const char rtems_test_name[] = "CONTAINER 01";

static bool mnt_is_listed(MntContainer *target)
{
  Container *root = rtems_container_get_root();
  MntContainerNode *node;

  for (node = root->mntContainerListHead; node != NULL; node = node->next) {
    if (node->mntContainer == target) {
      return true;
    }
  }

  return false;
}

static bool net_is_listed(NetContainer *target)
{
  Container *root = rtems_container_get_root();
  NetContainerNode *node;

  for (node = root->netContainerListHead; node != NULL; node = node->next) {
    if (node->netContainer == target) {
      return true;
    }
  }

  return false;
}

static bool ipc_is_listed(IpcContainer *target)
{
  Container *root = rtems_container_get_root();
  IpcContainerNode *node;

  for (node = root->ipcContainerListHead; node != NULL; node = node->next) {
    if (node->ipcContainer == target) {
      return true;
    }
  }

  return false;
}

static void verify_entered_container(
  RtemsContainer *container,
  rtems_id root_queue
)
{
  Thread_Control *self = _Thread_Get_executing();
  Container *namespaces = rtems_unified_container_get_namespaces(container);
  CORE_cgroup_Control *core = rtems_unified_container_get_core_cgroup(container);
  IO_Cgroup_Control *io = rtems_unified_container_get_io_cgroup(container);
  ThreadContainerInfo pid_info;
  rtems_status_code sc;
  rtems_id child_queue;
  rtems_id ignored;
  rtems_name queue_name = rtems_build_name('I', 'P', 'C', 'Q');
  uint64_t quota_before;
  uint64_t mem_quota_before_alloc;
  IO_Cgroup_Request io_request;
  char hostname[64];
  uint64_t io_bytes_before;

  rtems_test_assert(self->container->pidContainer == namespaces->pidContainer);
  rtems_test_assert(self->container->ipcContainer == namespaces->ipcContainer);
  rtems_test_assert(self->container->mntContainer == namespaces->mntContainer);
  rtems_test_assert(self->container->netContainer == namespaces->netContainer);
  rtems_test_assert(self->container->utsContainer == namespaces->utsContainer);
  mem_quota_before_alloc = core->mem_quota_available;
  rtems_test_assert(mem_quota_before_alloc <= container->cgroup_config.memory_limit);

  pid_info = rtems_pid_container_get_thread_info(self);
  rtems_test_assert(pid_info.isRoot == 0);
  rtems_test_assert(pid_info.containerID == rtems_pid_container_get_id(namespaces->pidContainer));
  rtems_test_assert(pid_info.vid_or_id > 0);

  sc = rtems_message_queue_create(
    queue_name,
    2,
    sizeof(uint32_t),
    RTEMS_DEFAULT_ATTRIBUTES,
    &child_queue
  );
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);
  rtems_test_assert(child_queue != root_queue);

  sc = rtems_message_queue_ident(queue_name, RTEMS_SEARCH_ALL_NODES, &ignored);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);
  rtems_test_assert(ignored == child_queue);

  sc = rtems_message_queue_delete(child_queue);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  rtems_test_assert(get_current_thread_mnt_container() == namespaces->mntContainer);
  rtems_test_assert(rtems_net_container_get_ifnet() != NULL);
  rtems_test_assert(self->container->netContainer->group != NULL);

  rtems_test_assert(sethostname("ctr-host", 8) == 0);
  rtems_test_assert(gethostname(hostname, sizeof(hostname)) == 0);
  rtems_test_assert(strcmp(hostname, "ctr-host") == 0);

  rtems_test_assert(self->cgroup == core);
  rtems_test_assert(self->is_added_to_cgroup);
  rtems_test_assert(core->thread_count == 1);
  rtems_test_assert(core->mem_quota_available <= mem_quota_before_alloc);

  quota_before = core->cpu_quota_available;
  rtems_test_assert(quota_before > 1);
  _CORE_cgroup_Consume_cpu_quota(core, 1);
  rtems_test_assert(core->cpu_quota_available == quota_before - 1);

  memset(&io_request, 0, sizeof(io_request));
  io_request.size = 1;
  io_request.type = IO_CGROUP_READ;
  io_request.timestamp = rtems_clock_get_ticks_since_boot();
  io_bytes_before = io->stats.read.bytes;
  sc = rtems_io_cgroup_handle_request(io, &io_request);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);
  rtems_test_assert(io->stats.read.bytes == io_bytes_before + io_request.size);
}

static rtems_task Init(rtems_task_argument arg)
{
  RtemsContainerConfig config;
  RtemsContainer *container;
  Container *root;
  Container *namespaces;
  Thread_Control *self;
  CORE_cgroup_Control *core;
  IO_Cgroup_Control *io;
  PidContainer *pid;
  UtsContainer *uts;
  MntContainer *mnt;
  NetContainer *net;
  IpcContainer *ipc;
  rtems_status_code sc;
  rtems_id root_queue;
  rtems_id cgroup_id;
  uint32_t io_id;
  uint32_t cgroup_count;
  rtems_name queue_name = rtems_build_name('I', 'P', 'C', 'Q');
  char hostname[64];

  (void)arg;

  TEST_BEGIN();

  rtems_test_assert(rtems_bsdnet_initialize_network() == 0);
  root = rtems_container_get_root();
  rtems_test_assert(root != NULL);
  rtems_test_assert(root->pidContainer != NULL);
  rtems_test_assert(root->ipcContainer != NULL);
  rtems_test_assert(root->mntContainer != NULL);
  rtems_test_assert(root->netContainer != NULL);
  rtems_test_assert(root->utsContainer != NULL);

  rtems_test_assert(sethostname("root-host", 9) == 0);
  sc = rtems_message_queue_create(
    queue_name,
    2,
    sizeof(uint32_t),
    RTEMS_DEFAULT_ATTRIBUTES,
    &root_queue
  );
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  rtems_unified_container_config_initialize(&config);
  config.flags = RTEMS_UNIFIED_CONTAINER_ALL;
  config.uts_name = "unified";
  config.cgroup_config.cpu_quota = 10;
  config.cgroup_config.cpu_period = 1000;
  config.cgroup_config.memory_limit = 64 * 1024;
  config.cgroup_config.blkio_limit = 64 * 1024;
  config.io_system_read_bps = 1024 * 1024 * 1024;
  config.io_system_write_bps = 1024 * 1024 * 1024;
  config.io_read_bps_limit = 1024 * 1024 * 1024;
  config.io_write_bps_limit = 1024 * 1024 * 1024;
  config.io_weight = 100;
  config.io_thread_weight = 100;

  sc = rtems_unified_container_create(&config, &container);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);
  rtems_test_assert(container != NULL);

  namespaces = rtems_unified_container_get_namespaces(container);
  core = rtems_unified_container_get_core_cgroup(container);
  io = rtems_unified_container_get_io_cgroup(container);
  pid = namespaces->pidContainer;
  uts = namespaces->utsContainer;
  mnt = namespaces->mntContainer;
  net = namespaces->netContainer;
  ipc = namespaces->ipcContainer;
  cgroup_id = container->cgroup_id;
  io_id = container->io_cgroup_id;

  rtems_test_assert(pid != NULL && pid != root->pidContainer);
  rtems_test_assert(uts != NULL && uts != root->utsContainer);
  rtems_test_assert(mnt != NULL && mnt != root->mntContainer);
  rtems_test_assert(net != NULL && net != root->netContainer);
  rtems_test_assert(ipc != NULL && ipc != root->ipcContainer);
  rtems_test_assert(core != NULL);
  rtems_test_assert(io != NULL);

  self = _Thread_Get_executing();
  sc = rtems_unified_container_enter(container, self);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);
  verify_entered_container(container, root_queue);

  sc = rtems_unified_container_leave(container, self);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);
  rtems_test_assert(self->container->pidContainer == root->pidContainer);
  rtems_test_assert(self->container->ipcContainer == root->ipcContainer);
  rtems_test_assert(self->container->mntContainer == root->mntContainer);
  rtems_test_assert(self->container->netContainer == root->netContainer);
  rtems_test_assert(self->container->utsContainer == root->utsContainer);
  rtems_test_assert(self->cgroup == NULL);
  rtems_test_assert(!self->is_added_to_cgroup);
  rtems_test_assert(gethostname(hostname, sizeof(hostname)) == 0);
  rtems_test_assert(strcmp(hostname, "root-host") == 0);

  sc = rtems_message_queue_ident(queue_name, RTEMS_SEARCH_ALL_NODES, &root_queue);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);
  sc = rtems_message_queue_delete(root_queue);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  sc = rtems_unified_container_delete(container);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);
  rtems_test_assert(!rtems_pid_container_exists(pid));
  rtems_test_assert(!rtems_uts_container_exists(uts));
  rtems_test_assert(!mnt_is_listed(mnt));
  rtems_test_assert(!net_is_listed(net));
  rtems_test_assert(!ipc_is_listed(ipc));
  rtems_test_assert(rtems_io_cgroup_get_by_id(io_id) == NULL);
  sc = rtems_cgroup_get_task_count(cgroup_id, &cgroup_count);
  rtems_test_assert(sc == RTEMS_INVALID_ID);

  TEST_END();
  rtems_test_exit(0);
}

struct rtems_bsdnet_config rtems_bsdnet_config = {
  NULL, NULL, 0, 0, 0, 0, 0, 0, 0,
  {"0.0.0.0"}, {"0.0.0.0"}, 0, 0, 0, 0, 0
};

#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_SIMPLE_CONSOLE_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_LIBBLOCK
#define CONFIGURE_APPLICATION_NEEDS_LIBNETWORKING

#define CONFIGURE_MAXIMUM_TASKS 8
#define CONFIGURE_MAXIMUM_CGROUPS 1
#define CONFIGURE_MAXIMUM_SEMAPHORES 10
#define CONFIGURE_MAXIMUM_MESSAGE_QUEUES 4
#define CONFIGURE_MAXIMUM_FILE_DESCRIPTORS 20

#define CONFIGURE_MESSAGE_BUFFER_MEMORY \
  CONFIGURE_MESSAGE_BUFFERS_FOR_QUEUE(4, sizeof(uint32_t))

#define CONFIGURE_EXECUTIVE_RAM_SIZE (4 * 1024 * 1024)
#define CONFIGURE_RTEMS_INIT_TASKS_TABLE
#define CONFIGURE_INIT_TASK_ATTRIBUTES RTEMS_FLOATING_POINT
#define CONFIGURE_INITIAL_EXTENSIONS RTEMS_TEST_INITIAL_EXTENSION

#define CONFIGURE_INIT
#include <rtems/confdefs.h>
