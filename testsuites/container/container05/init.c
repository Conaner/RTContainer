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

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

const char rtems_test_name[] = "CONTAINER 05";

static void print_step(uint32_t step, const char *message)
{
  printf("[step %02" PRIu32 "] %s\n", step, message);
}

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

static void verify_container_runtime(
  RtemsContainer *container,
  rtems_id root_queue,
  rtems_name queue_name
)
{
  Container *namespaces;
  CORE_cgroup_Control *core;
  IO_Cgroup_Control *io;
  Thread_Control *self;
  ThreadContainerInfo pid_info;
  rtems_status_code sc;
  rtems_id child_queue;
  rtems_id found_queue;
  uint64_t mem_quota_before_alloc;
  uint64_t quota_before;
  uint64_t io_bytes_before;
  IO_Cgroup_Request io_request;
  char hostname[64];

  namespaces = rtems_unified_container_get_namespaces(container);
  core = rtems_unified_container_get_core_cgroup(container);
  io = rtems_unified_container_get_io_cgroup(container);

  rtems_test_assert(namespaces != NULL);
  rtems_test_assert(core != NULL);
  rtems_test_assert(io != NULL);

  self = _Thread_Get_executing();
  rtems_test_assert(self != NULL);
  print_step(12, "Init task control block acquired");

  sc = rtems_unified_container_enter(container, self);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);
  print_step(13, "Init task entered unified container");

  rtems_test_assert(self->container->pidContainer == namespaces->pidContainer);
  rtems_test_assert(self->container->ipcContainer == namespaces->ipcContainer);
  rtems_test_assert(self->container->mntContainer == namespaces->mntContainer);
  rtems_test_assert(self->container->netContainer == namespaces->netContainer);
  rtems_test_assert(self->container->utsContainer == namespaces->utsContainer);
  print_step(14, "PID/IPC/MNT/NET/UTS namespace pointers switched");

  pid_info = rtems_pid_container_get_thread_info(self);
  rtems_test_assert(pid_info.isRoot == 0);
  rtems_test_assert(
    pid_info.containerID == rtems_pid_container_get_id(namespaces->pidContainer)
  );
  rtems_test_assert(pid_info.vid_or_id > 0);
  print_step(15, "PID namespace identity verified");

  sc = rtems_message_queue_create(
    queue_name,
    2,
    sizeof(uint32_t),
    RTEMS_DEFAULT_ATTRIBUTES,
    &child_queue
  );
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);
  rtems_test_assert(child_queue != root_queue);
  print_step(16, "IPC queue with root name created inside container");

  sc = rtems_message_queue_ident(
    queue_name,
    RTEMS_SEARCH_ALL_NODES,
    &found_queue
  );
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);
  rtems_test_assert(found_queue == child_queue);
  print_step(17, "IPC queue lookup resolved to container-local queue");

  sc = rtems_message_queue_delete(child_queue);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);
  print_step(18, "Container-local IPC queue deleted");

  rtems_test_assert(get_current_thread_mnt_container() == namespaces->mntContainer);
  print_step(19, "MNT namespace switched to unified container");

  rtems_test_assert(rtems_net_container_get_ifnet() != NULL);
  rtems_test_assert(self->container->netContainer->group != NULL);
  print_step(20, "NET namespace initialized and active");

  rtems_test_assert(sethostname("ctr-host", 8) == 0);
  rtems_test_assert(gethostname(hostname, sizeof(hostname)) == 0);
  rtems_test_assert(strcmp(hostname, "ctr-host") == 0);
  print_step(21, "UTS hostname set and read as ctr-host");

  rtems_test_assert(self->cgroup == core);
  rtems_test_assert(self->is_added_to_cgroup);
  rtems_test_assert(core->thread_count == 1);
  print_step(22, "Core cgroup binding verified");

  mem_quota_before_alloc = core->mem_quota_available;
  rtems_test_assert(mem_quota_before_alloc <= container->cgroup_config.memory_limit);
  print_step(23, "Memory quota initialized inside core cgroup");

  quota_before = core->cpu_quota_available;
  rtems_test_assert(quota_before > 1);
  _CORE_cgroup_Consume_cpu_quota(core, 1);
  rtems_test_assert(core->cpu_quota_available == quota_before - 1);
  print_step(24, "CPU quota consumption decremented available quota");

  memset(&io_request, 0, sizeof(io_request));
  io_request.size = 1;
  io_request.type = IO_CGROUP_READ;
  io_request.timestamp = rtems_clock_get_ticks_since_boot();
  io_bytes_before = io->stats.read.bytes;
  sc = rtems_io_cgroup_handle_request(io, &io_request);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);
  rtems_test_assert(io->stats.read.bytes == io_bytes_before + io_request.size);
  print_step(25, "I/O cgroup read request accounted");

  sc = rtems_unified_container_leave(container, self);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);
  print_step(26, "Init task left unified container");
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
  rtems_id found_queue;
  rtems_id cgroup_id;
  uint32_t io_id;
  uint32_t cgroup_count;
  rtems_name queue_name = rtems_build_name('I', 'P', 'C', 'Q');
  char hostname[64];

  (void) arg;

  TEST_BEGIN();
  print_step(1, "TEST_BEGIN completed, test program started");

  rtems_test_assert(rtems_bsdnet_initialize_network() == 0);
  print_step(2, "BSD network stack initialized");

  root = rtems_container_get_root();
  rtems_test_assert(root != NULL);
  print_step(3, "Root container pointer acquired");

  rtems_test_assert(root->pidContainer != NULL);
  rtems_test_assert(root->ipcContainer != NULL);
  rtems_test_assert(root->mntContainer != NULL);
  rtems_test_assert(root->netContainer != NULL);
  rtems_test_assert(root->utsContainer != NULL);
  print_step(4, "Root PID/IPC/MNT/NET/UTS namespaces are present");

  rtems_test_assert(sethostname("root-host", 9) == 0);
  print_step(5, "Root hostname set to root-host");

  sc = rtems_message_queue_create(
    queue_name,
    2,
    sizeof(uint32_t),
    RTEMS_DEFAULT_ATTRIBUTES,
    &root_queue
  );
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);
  print_step(6, "Root IPC message queue created");

  rtems_unified_container_config_initialize(&config);
  print_step(7, "Unified container configuration initialized");

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
  print_step(8, "Unified container configuration fields populated");

  sc = rtems_unified_container_create(&config, &container);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);
  rtems_test_assert(container != NULL);
  print_step(9, "Unified container created");

  namespaces = rtems_unified_container_get_namespaces(container);
  core = rtems_unified_container_get_core_cgroup(container);
  io = rtems_unified_container_get_io_cgroup(container);
  rtems_test_assert(namespaces != NULL);
  rtems_test_assert(core != NULL);
  rtems_test_assert(io != NULL);
  print_step(10, "Unified container namespace, core cgroup, and I/O cgroup acquired");

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
  print_step(11, "Child PID/IPC/MNT/NET/UTS namespaces are isolated from root");

  verify_container_runtime(container, root_queue, queue_name);

  self = _Thread_Get_executing();
  rtems_test_assert(self->container->pidContainer == root->pidContainer);
  rtems_test_assert(self->container->ipcContainer == root->ipcContainer);
  rtems_test_assert(self->container->mntContainer == root->mntContainer);
  rtems_test_assert(self->container->netContainer == root->netContainer);
  rtems_test_assert(self->container->utsContainer == root->utsContainer);
  print_step(27, "Task namespace pointers restored to root");

  rtems_test_assert(self->cgroup == NULL);
  rtems_test_assert(!self->is_added_to_cgroup);
  print_step(28, "Task cgroup binding cleared after leaving container");

  rtems_test_assert(gethostname(hostname, sizeof(hostname)) == 0);
  rtems_test_assert(strcmp(hostname, "root-host") == 0);
  print_step(29, "Root hostname still reads root-host");

  sc = rtems_message_queue_ident(
    queue_name,
    RTEMS_SEARCH_ALL_NODES,
    &found_queue
  );
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);
  rtems_test_assert(found_queue == root_queue);
  sc = rtems_message_queue_delete(root_queue);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);
  print_step(30, "Root IPC queue found and deleted");

  sc = rtems_unified_container_delete(container);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);
  print_step(31, "Unified container deleted");

  rtems_test_assert(!rtems_pid_container_exists(pid));
  rtems_test_assert(!rtems_uts_container_exists(uts));
  rtems_test_assert(!mnt_is_listed(mnt));
  rtems_test_assert(!net_is_listed(net));
  rtems_test_assert(!ipc_is_listed(ipc));
  print_step(32, "Namespace resources released");

  rtems_test_assert(rtems_io_cgroup_get_by_id(io_id) == NULL);
  print_step(33, "I/O cgroup released");

  sc = rtems_cgroup_get_task_count(cgroup_id, &cgroup_count);
  rtems_test_assert(sc == RTEMS_INVALID_ID);
  print_step(34, "Core cgroup ID is invalid after deletion");

  print_step(35, "All unified container checks completed");
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
