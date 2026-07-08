#include <rtems.h>
#include <tmacros.h>
#include <rtems/rtems/cgroup.h>
#include <rtems/score/container.h>
#include <rtems/score/containerfs.h>
#include <rtems/score/threadimpl.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

const char rtems_test_name[] = "ContainerFS Lifecycle Pause Resume Test";

#define WORKER_READY_EVENT RTEMS_EVENT_0
#define WORKER_DONE_EVENT RTEMS_EVENT_1

static rtems_id init_task_id;
static rtems_id worker_task_id;
static RtemsContainer *managed_container;
static volatile bool worker_stop;
static volatile uint32_t worker_ticks;

static void write_cmd(const char *path, const char *cmd)
{
  int fd;
  ssize_t n;

  fd = open(path, O_WRONLY);
  if (fd < 0) {
    printf("open %s failed: errno=%d\n", path, errno);
  }
  rtems_test_assert(fd >= 0);

  n = write(fd, cmd, strlen(cmd));
  if (n < 0) {
    printf("write %s failed: errno=%d\n", path, errno);
  }
  rtems_test_assert(n == (ssize_t) strlen(cmd));

  close(fd);
}

static void wait_until_counter_changes(uint32_t previous)
{
  uint32_t i;

  for (i = 0; i < 20; ++i) {
    if (worker_ticks != previous) {
      return;
    }
    rtems_task_wake_after(1);
  }

  rtems_test_assert(worker_ticks != previous);
}

static rtems_task WorkerTask(rtems_task_argument arg)
{
  Thread_Control *self;
  rtems_status_code sc;

  (void) arg;

  self = _Thread_Get_executing();
  sc = rtems_unified_container_enter(managed_container, self);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);
  rtems_test_assert(self->cgroup == rtems_unified_container_get_core_cgroup(managed_container));
  rtems_test_assert(self->is_added_to_cgroup);
  puts("[step 07] Worker entered CPU container");

  sc = rtems_event_send(init_task_id, WORKER_READY_EVENT);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);
  puts("[step 08] Worker ready event sent");

  while (!worker_stop) {
    ++worker_ticks;
    rtems_task_wake_after(1);
  }

  sc = rtems_unified_container_leave(managed_container, self);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);
  puts("[step 15] Worker left CPU container");

  sc = rtems_event_send(init_task_id, WORKER_DONE_EVENT);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);
  rtems_task_exit();
}

static void run_containerfs_lifecycle_probe(void)
{
  rtems_containerfs_register_cpuctl();
  puts("[step 01] /cpuctl registered");

  write_cmd("/cpuctl", "list\n");
  puts("[step 02] /cpuctl opened");
  puts("[step 03] list command accepted");
}

static rtems_task Init(rtems_task_argument arg)
{
  RtemsContainerConfig config;
  CORE_cgroup_Control *core;
  rtems_event_set received;
  rtems_status_code sc;
  uint32_t before_pause;
  uint32_t during_pause;
  char cmd[64];

  (void) arg;

  rtems_print_printer_fprintf_putc(&rtems_test_printer);
  TEST_BEGIN();

#if defined(RTEMSCFG_CONTAINER_FILE) && defined(RTEMS_CGROUP)
  run_containerfs_lifecycle_probe();

  init_task_id = rtems_task_self();
  worker_stop = false;
  worker_ticks = 0;

  rtems_unified_container_config_initialize(&config);
  config.flags = RTEMS_UNIFIED_CONTAINER_CPU;
  config.cgroup_config.cpu_quota = 100;
  config.cgroup_config.cpu_period = 1000;
  config.cgroup_config.cpu_shares = 1;
  config.cgroup_config.memory_limit = 0;
  config.cgroup_config.blkio_limit = 0;

  sc = rtems_unified_container_create(&config, &managed_container);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);
  rtems_test_assert(managed_container != NULL);
  puts("[step 04] CPU container created");

  core = rtems_unified_container_get_core_cgroup(managed_container);
  rtems_test_assert(core != NULL);
  printf(
    "[step 05] core cgroup ready, cgroup id=%" PRIu32 "\n",
    managed_container->cgroup_id
  );

  sc = rtems_task_create(
    rtems_build_name('W', 'R', 'K', 'P'),
    9,
    RTEMS_MINIMUM_STACK_SIZE,
    RTEMS_DEFAULT_MODES,
    RTEMS_DEFAULT_ATTRIBUTES,
    &worker_task_id
  );
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  sc = rtems_task_start(worker_task_id, WorkerTask, 0);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);
  puts("[step 06] Worker task created and started");

  sc = rtems_event_receive(
    WORKER_READY_EVENT,
    RTEMS_EVENT_ALL | RTEMS_WAIT,
    RTEMS_NO_TIMEOUT,
    &received
  );
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  before_pause = worker_ticks;
  wait_until_counter_changes(before_pause);
  printf("[step 09] Worker running, ticks=%" PRIu32 "\n", worker_ticks);

  snprintf(cmd, sizeof(cmd), "pause %" PRIu32 "\n", managed_container->cgroup_id);
  write_cmd("/cpuctl", cmd);
  printf(
    "[step 10] pause command accepted for cgroup id=%" PRIu32 "\n",
    managed_container->cgroup_id
  );

  before_pause = worker_ticks;
  rtems_task_wake_after(5);
  during_pause = worker_ticks;
  printf("[step 11] ticks while paused: before=%" PRIu32 ", after=%" PRIu32 "\n",
    before_pause,
    during_pause
  );
  rtems_test_assert(during_pause == before_pause);

  snprintf(cmd, sizeof(cmd), "resume %" PRIu32 "\n", managed_container->cgroup_id);
  write_cmd("/cpuctl", cmd);
  printf(
    "[step 12] resume command accepted for cgroup id=%" PRIu32 "\n",
    managed_container->cgroup_id
  );

  wait_until_counter_changes(during_pause);
  printf("[step 13] Worker resumed, ticks=%" PRIu32 "\n", worker_ticks);

  worker_stop = true;
  puts("[step 14] Worker stop requested");

  sc = rtems_event_receive(
    WORKER_DONE_EVENT,
    RTEMS_EVENT_ALL | RTEMS_WAIT,
    RTEMS_NO_TIMEOUT,
    &received
  );
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);
  puts("[step 16] Worker done event received");

  sc = rtems_unified_container_delete(managed_container);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);
  managed_container = NULL;
  puts("[step 17] CPU container deleted");
  puts("[container] deleted");
  puts("[step 18] lifecycle pause/resume test completed");
#else
  printf("ContainerFS lifecycle pause/resume requires RTEMSCFG_CONTAINER_FILE and RTEMS_CGROUP\n");
#endif

  TEST_END();
  rtems_test_exit(0);
}

#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_SIMPLE_CONSOLE_DRIVER

#define CONFIGURE_USE_IMFS_AS_BASE_FILESYSTEM
#define CONFIGURE_MAXIMUM_FILE_DESCRIPTORS 8
#define CONFIGURE_MAXIMUM_TASKS 8
#define CONFIGURE_MAXIMUM_CGROUPS 4
#define CONFIGURE_RTEMS_INIT_TASKS_TABLE
#define CONFIGURE_INIT_TASK_ATTRIBUTES RTEMS_FLOATING_POINT
#define CONFIGURE_INITIAL_EXTENSIONS RTEMS_TEST_INITIAL_EXTENSION
#define CONFIGURE_INIT
#include <rtems/confdefs.h>
