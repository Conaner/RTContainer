#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <rtems.h>
#include <rtems/score/container.h>
#include <rtems/score/pidContainer.h>
#include <rtems/score/threadimpl.h>
#include <tmacros.h>

#include <stdbool.h>
#include <stdint.h>

const char rtems_test_name[] = "CONTAINER 03";

#define WORKER_DONE_EVENT RTEMS_EVENT_0

typedef struct {
  Objects_Id target_id;
  bool found;
} SearchContext;

static rtems_id init_task_id;
static rtems_id host_task_id;
static volatile bool host_task_stop;
static RtemsContainer *pid_container;

static bool find_thread_by_id(Thread_Control *thread, void *arg)
{
  SearchContext *ctx = (SearchContext *) arg;

  if (thread->Object.id == ctx->target_id) {
    ctx->found = true;
    return false;
  }

  return true;
}

static bool pid_container_contains(PidContainer *container, Objects_Id id)
{
  SearchContext ctx;

  ctx.target_id = id;
  ctx.found = false;
  rtems_pid_container_foreach_thread(container, find_thread_by_id, &ctx);
  return ctx.found;
}

static rtems_task HostTask(rtems_task_argument arg)
{
  (void) arg;

  while (!host_task_stop) {
    rtems_task_wake_after(1);
  }

  rtems_task_exit();
}

static rtems_task WorkerTask(rtems_task_argument arg)
{
  rtems_status_code sc;
  Thread_Control *self;
  PidContainer *root_pid;
  PidContainer *current_pid;
  ThreadContainerInfo info;

  (void) arg;

  self = _Thread_Get_executing();
  sc = rtems_unified_container_enter(pid_container, self);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  root_pid = rtems_container_get_root()->pidContainer;
  current_pid = self->container->pidContainer;

  rtems_test_assert(current_pid != NULL);
  rtems_test_assert(root_pid != NULL);
  rtems_test_assert(current_pid != root_pid);

  info = rtems_pid_container_get_thread_info(self);
  rtems_test_assert(info.isRoot == 0);
  rtems_test_assert(info.containerID == rtems_pid_container_get_id(current_pid));
  rtems_test_assert(info.vid_or_id > 0);

  /*
   * PID namespace isolation check:
   * the task tree visible in current PID container must not include host tasks.
   */
  rtems_test_assert(pid_container_contains(current_pid, self->Object.id));
  rtems_test_assert(!pid_container_contains(current_pid, init_task_id));
  rtems_test_assert(!pid_container_contains(current_pid, host_task_id));

  sc = rtems_unified_container_leave(pid_container, self);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  sc = rtems_event_send(init_task_id, WORKER_DONE_EVENT);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);
  rtems_task_exit();
}

static rtems_task Init(rtems_task_argument arg)
{
  RtemsContainerConfig config;
  rtems_id worker_task_id;
  rtems_event_set received;
  rtems_status_code sc;

  (void) arg;

  TEST_BEGIN();

  init_task_id = rtems_task_self();
  host_task_stop = false;
  pid_container = NULL;

  sc = rtems_task_create(
    rtems_build_name('H', 'O', 'S', 'T'),
    10,
    RTEMS_MINIMUM_STACK_SIZE,
    RTEMS_DEFAULT_MODES,
    RTEMS_DEFAULT_ATTRIBUTES,
    &host_task_id
  );
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  sc = rtems_task_start(host_task_id, HostTask, 0);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  rtems_unified_container_config_initialize(&config);
  config.flags = RTEMS_UNIFIED_CONTAINER_PID;
  sc = rtems_unified_container_create(&config, &pid_container);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);
  rtems_test_assert(pid_container != NULL);

  sc = rtems_task_create(
    rtems_build_name('W', 'K', 'P', 'D'),
    9,
    RTEMS_MINIMUM_STACK_SIZE,
    RTEMS_DEFAULT_MODES,
    RTEMS_DEFAULT_ATTRIBUTES,
    &worker_task_id
  );
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  sc = rtems_task_start(worker_task_id, WorkerTask, 0);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  sc = rtems_event_receive(
    WORKER_DONE_EVENT,
    RTEMS_EVENT_ALL | RTEMS_WAIT,
    RTEMS_NO_TIMEOUT,
    &received
  );
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  host_task_stop = true;
  rtems_task_wake_after(2);
  sc = rtems_task_delete(host_task_id);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL || sc == RTEMS_INVALID_ID);

  sc = rtems_unified_container_delete(pid_container);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  TEST_END();
  rtems_test_exit(0);
}

#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_SIMPLE_CONSOLE_DRIVER

#define CONFIGURE_MAXIMUM_TASKS 5
#define CONFIGURE_MAXIMUM_CGROUPS 1

#define CONFIGURE_RTEMS_INIT_TASKS_TABLE
#define CONFIGURE_INIT_TASK_ATTRIBUTES RTEMS_FLOATING_POINT
#define CONFIGURE_INITIAL_EXTENSIONS RTEMS_TEST_INITIAL_EXTENSION

#define CONFIGURE_INIT
#include <rtems/confdefs.h>
