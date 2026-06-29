#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <rtems.h>
#include <rtems/counter.h>
#include <rtems/score/container.h>
#include <rtems/score/threadimpl.h>
#include <tmacros.h>

#include <inttypes.h>
#include <sched.h>
#include <stdbool.h>

const char rtems_test_name[] = "CONTAINER 02";

#define WORKER_COUNT 2
#define DONE_EVENT_0 RTEMS_EVENT_0
#define DONE_EVENT_1 RTEMS_EVENT_1
#define TARGET_SWITCHES 1000u
#define PROGRESS_PRINT_INTERVAL 100u
#define MEASUREMENT_TIMEOUT_SECONDS 30u

typedef struct {
  uint32_t id;
  rtems_id task_id;
  RtemsContainer *container;
} WorkerContext;

static WorkerContext workers[WORKER_COUNT];
static rtems_id init_task_id;

static volatile bool start_measurement;
static volatile bool stop_measurement;
static volatile uint32_t current_runner;
static volatile uint32_t switch_count;
static volatile rtems_counter_ticks total_switch_ticks;
static volatile rtems_counter_ticks last_switch_tick;

static void pin_task_to_cpu_0(rtems_id task_id)
{
#if defined(RTEMS_SMP)
  cpu_set_t set;
  rtems_status_code sc;

  CPU_ZERO(&set);
  CPU_SET(0, &set);
  sc = rtems_task_set_affinity(task_id, sizeof(set), &set);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);
#else
  (void) task_id;
#endif
}

static rtems_task worker_task(rtems_task_argument arg)
{
  uint32_t worker_index = (uint32_t) arg;
  WorkerContext *ctx = &workers[worker_index];
  Thread_Control *self = _Thread_Get_executing();
  rtems_event_set done_event = (worker_index == 0) ? DONE_EVENT_0 : DONE_EVENT_1;
  rtems_status_code sc;

  sc = rtems_unified_container_enter(ctx->container, self);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  while (!start_measurement) {
    /* Busy wait until Init starts the measurement window. */
  }

  while (!stop_measurement) {
    rtems_counter_ticks now = rtems_counter_read();
    uint32_t previous_runner = current_runner;

    if (previous_runner != ctx->id) {
      current_runner = ctx->id;

      if (previous_runner != 0u) {
        total_switch_ticks += rtems_counter_difference(now, last_switch_tick);
        ++switch_count;
        if ((switch_count % PROGRESS_PRINT_INTERVAL) == 0u) {
          printf(
            "container switch progress: %" PRIu32 "/%" PRIu32 "\n",
            switch_count,
            TARGET_SWITCHES
          );
        }
        if (switch_count >= TARGET_SWITCHES) {
          stop_measurement = true;
        }
      }

      last_switch_tick = now;
    }
  }

  sc = rtems_unified_container_leave(ctx->container, self);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  sc = rtems_event_send(init_task_id, done_event);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);
  rtems_task_exit();
}

static rtems_task Init(rtems_task_argument arg)
{
  RtemsContainerConfig config;
  RtemsContainer *container_a;
  RtemsContainer *container_b;
  rtems_event_set received;
  rtems_status_code sc;
  uint64_t avg_us;
  uint32_t elapsed_seconds;

  (void) arg;

  TEST_BEGIN();

  init_task_id = rtems_task_self();
  start_measurement = false;
  stop_measurement = false;
  current_runner = 0;
  switch_count = 0;
  total_switch_ticks = 0;
  last_switch_tick = 0;

  rtems_unified_container_config_initialize(&config);
  config.flags = RTEMS_UNIFIED_CONTAINER_CPU;
  config.cgroup_config.cpu_quota = 1;
  config.cgroup_config.cpu_period = 2;
  config.cgroup_config.memory_limit = 0;
  config.cgroup_config.blkio_limit = 0;

  sc = rtems_unified_container_create(&config, &container_a);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);
  sc = rtems_unified_container_create(&config, &container_b);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  workers[0].id = 1;
  workers[0].container = container_a;
  workers[1].id = 2;
  workers[1].container = container_b;

  for (uint32_t i = 0; i < WORKER_COUNT; ++i) {
    sc = rtems_task_create(
      rtems_build_name('W', 'K', '0', '0' + i),
      20,
      RTEMS_MINIMUM_STACK_SIZE,
      RTEMS_DEFAULT_MODES,
      RTEMS_DEFAULT_ATTRIBUTES,
      &workers[i].task_id
    );
    rtems_test_assert(sc == RTEMS_SUCCESSFUL);

    pin_task_to_cpu_0(workers[i].task_id);

    sc = rtems_task_start(workers[i].task_id, worker_task, i);
    rtems_test_assert(sc == RTEMS_SUCCESSFUL);
  }

  rtems_task_wake_after(1);
  start_measurement = true;

  for (elapsed_seconds = 0; elapsed_seconds < MEASUREMENT_TIMEOUT_SECONDS; ++elapsed_seconds) {
    sc = rtems_event_receive(
      DONE_EVENT_0 | DONE_EVENT_1,
      RTEMS_EVENT_ALL | RTEMS_WAIT,
      rtems_clock_get_ticks_per_second(),
      &received
    );

    if (sc == RTEMS_SUCCESSFUL) {
      break;
    }

    if (sc == RTEMS_TIMEOUT) {
      printf(
        "waiting... elapsed=%" PRIu32 "s switches=%" PRIu32 " total_ticks=%" PRIu64 "\n",
        elapsed_seconds + 1u,
        switch_count,
        (uint64_t) total_switch_ticks
      );
      continue;
    }

    rtems_test_assert(false);
  }

  if (sc != RTEMS_SUCCESSFUL) {
    stop_measurement = true;
    printf(
      "measurement timeout after %" PRIu32 "s, switches=%" PRIu32 "\n",
      MEASUREMENT_TIMEOUT_SECONDS,
      switch_count
    );
    rtems_test_assert(false);
  }
  rtems_test_assert(switch_count >= TARGET_SWITCHES);

  avg_us = rtems_counter_ticks_to_nanoseconds(total_switch_ticks) / (switch_count * 1000u);

  printf(
    "container switch statistics: switches=%" PRIu32 ", total_ticks=%" PRIu64 "\n",
    switch_count,
    (uint64_t) total_switch_ticks
  );
  printf("container switch average time: %" PRIu64 " us\n", avg_us);

  sc = rtems_unified_container_delete(container_a);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);
  sc = rtems_unified_container_delete(container_b);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  TEST_END();
  rtems_test_exit(0);
}

#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_SIMPLE_CONSOLE_DRIVER

#define CONFIGURE_MAXIMUM_TASKS 4
#define CONFIGURE_MAXIMUM_CGROUPS 2

#define CONFIGURE_RTEMS_INIT_TASKS_TABLE
#define CONFIGURE_INIT_TASK_ATTRIBUTES RTEMS_FLOATING_POINT
#define CONFIGURE_INITIAL_EXTENSIONS RTEMS_TEST_INITIAL_EXTENSION

#define CONFIGURE_INIT
#include <rtems/confdefs.h>
