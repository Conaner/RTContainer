#include <stdio.h>
#include <stddef.h>
#include <rtems.h>
#include <rtems/score/monitor.h>
#include <rtems/cpuuse.h>
#include <tmacros.h>

const char rtems_test_name[] = "CPU MONITOR DETAILED TEST";

#define RELEASE_TASK_EVENT RTEMS_EVENT_0
#define DONE_HIGH_1        RTEMS_EVENT_1
#define DONE_MEDIUM_1      RTEMS_EVENT_2
#define DONE_LOW_1         RTEMS_EVENT_3
#define DONE_HIGH_2        RTEMS_EVENT_4
#define DONE_LOW_2         RTEMS_EVENT_5

typedef struct {
    const char *name;
    rtems_id id;
} monitored_task;

static rtems_id init_task_id;

static void notify_ready_and_wait_for_release(
    const char *name,
    rtems_task_argument arg,
    rtems_event_set done_event
)
{
    rtems_status_code sc;
    rtems_event_set events;

    printf(
        "%s task %lu completed workload; waiting for monitor sampling\n",
        name,
        (unsigned long) arg
    );

    sc = rtems_event_send(init_task_id, done_event);
    rtems_test_assert(sc == RTEMS_SUCCESSFUL);

    sc = rtems_event_receive(
        RELEASE_TASK_EVENT,
        RTEMS_EVENT_ALL | RTEMS_WAIT,
        RTEMS_NO_TIMEOUT,
        &events
    );
    rtems_test_assert(sc == RTEMS_SUCCESSFUL);

    printf(
        "%s task %lu released after monitor sampling\n",
        name,
        (unsigned long) arg
    );
}

static void wait_for_task_set(const char *phase, rtems_event_set expected_events)
{
    rtems_status_code sc;
    rtems_event_set received_events;

    printf("\nWaiting for %s tasks to become sample-ready...\n", phase);

    sc = rtems_event_receive(
        expected_events,
        RTEMS_EVENT_ALL | RTEMS_WAIT,
        RTEMS_NO_TIMEOUT,
        &received_events
    );
    rtems_test_assert(sc == RTEMS_SUCCESSFUL);

    printf(
        "%s tasks are alive and blocked for sampling (events=0x%08lx)\n",
        phase,
        (unsigned long) received_events
    );
}

static void print_sample_ready_tasks(
    const char *phase,
    const monitored_task *tasks,
    size_t count
)
{
    printf("\nSample-ready worker tasks (%s): %lu\n",
           phase,
           (unsigned long) count);

    for (size_t i = 0; i < count; ++i) {
        printf("  %s id=0x%08lx\n",
               tasks[i].name,
               (unsigned long) tasks[i].id);
    }
}

static void release_monitored_tasks(const monitored_task *tasks, size_t count)
{
    rtems_status_code sc;

    printf("\nReleasing sampled worker tasks...\n");

    for (size_t i = 0; i < count; ++i) {
        sc = rtems_event_send(tasks[i].id, RELEASE_TASK_EVENT);
        rtems_test_assert(sc == RTEMS_SUCCESSFUL);
    }
}

static rtems_task high_priority_task(rtems_task_argument arg)
{
    printf("High priority task %lu started!\n", (unsigned long) arg);
    
    // 高优先级任务做更多工作
    volatile int counter = 0;
    for (int i = 0; i < 2000000; ++i) {
        counter += i * i;
    }

    notify_ready_and_wait_for_release(
        "High priority",
        arg,
        arg == 1 ? DONE_HIGH_1 : DONE_HIGH_2
    );
    rtems_task_exit();
}

static rtems_task medium_priority_task(rtems_task_argument arg)
{
    printf("Medium priority task %lu started!\n", (unsigned long) arg);
    
    // 中等优先级任务做中等工作
    volatile int counter = 0;
    for (int i = 0; i < 1000000; ++i) {
        counter += i;
    }

    notify_ready_and_wait_for_release("Medium priority", arg, DONE_MEDIUM_1);
    rtems_task_exit();
}

static rtems_task low_priority_task(rtems_task_argument arg)
{
    printf("Low priority task %lu started!\n", (unsigned long) arg);
    
    // 低优先级任务做少量工作
    volatile int counter = 0;
    for (int i = 0; i < 500000; ++i) {
        counter += 1;
    }

    notify_ready_and_wait_for_release(
        "Low priority",
        arg,
        arg == 1 ? DONE_LOW_1 : DONE_LOW_2
    );
    rtems_task_exit();
}

static void print_monitor_snapshot(const char *phase)
{
    RtemsMonitor snapshot;
    
    // 先采样数据，然后获取快照
    rtems_monitor_sample();
    rtems_monitor_get_snapshot(&snapshot);
    
    printf("\n=== Monitor Snapshot: %s ===\n", phase);
    
#ifdef RTEMSCFG_MONITOR_CPU
    printf("CPU Statistics:\n");
    printf("  Total runtime: %u.%06u seconds\n", 
           snapshot.cpu.total_seconds, snapshot.cpu.total_microseconds);
    printf("  Uptime since reset: %u.%06u seconds\n", 
           snapshot.cpu.uptime_seconds, snapshot.cpu.uptime_microseconds);
#endif
}

static rtems_task Init(rtems_task_argument ignored)
{
    rtems_id tid1, tid2, tid3, tid4, tid5;
    rtems_status_code sc;
    monitored_task first_batch[3];
    monitored_task all_tasks[5];

    rtems_print_printer_fprintf_putc(&rtems_test_printer);
    TEST_BEGIN();

#ifdef RTEMSCFG_MONITOR_CPU
    init_task_id = rtems_task_self();

    printf("CPU Monitor Detailed Test started!\n");
    
    // 初始化监控系统
    rtems_monitor_initialize();
    printf("Monitor system initialized\n");
    
    // 重置 CPU 使用率统计
    rtems_cpu_usage_reset();
    printf("CPU usage statistics reset\n");
    
    // 初始快照
    rtems_monitor_sample();
    print_monitor_snapshot("Initial");
    
    // 创建高优先级任务
    sc = rtems_task_create(
        rtems_build_name('H','I','G','H'),
        1,  // 最高优先级
        RTEMS_MINIMUM_STACK_SIZE,
        RTEMS_DEFAULT_MODES,
        RTEMS_DEFAULT_ATTRIBUTES,
        &tid1
    );
    rtems_test_assert(sc == RTEMS_SUCCESSFUL);
    sc = rtems_task_start(tid1, high_priority_task, 1);
    rtems_test_assert(sc == RTEMS_SUCCESSFUL);
    printf("High priority task created and started\n");
    
    // 创建中等优先级任务
    sc = rtems_task_create(
        rtems_build_name('M','E','D','I'),
        2,  // 中等优先级
        RTEMS_MINIMUM_STACK_SIZE,
        RTEMS_DEFAULT_MODES,
        RTEMS_DEFAULT_ATTRIBUTES,
        &tid2
    );
    rtems_test_assert(sc == RTEMS_SUCCESSFUL);
    sc = rtems_task_start(tid2, medium_priority_task, 1);
    rtems_test_assert(sc == RTEMS_SUCCESSFUL);
    printf("Medium priority task created and started\n");
    
    // 创建低优先级任务
    sc = rtems_task_create(
        rtems_build_name('L','O','W','1'),
        3,  // 低优先级
        RTEMS_MINIMUM_STACK_SIZE,
        RTEMS_DEFAULT_MODES,
        RTEMS_DEFAULT_ATTRIBUTES,
        &tid3
    );
    rtems_test_assert(sc == RTEMS_SUCCESSFUL);
    sc = rtems_task_start(tid3, low_priority_task, 1);
    rtems_test_assert(sc == RTEMS_SUCCESSFUL);
    printf("Low priority task 1 created and started\n");

    first_batch[0] = (monitored_task) { "HIGH", tid1 };
    first_batch[1] = (monitored_task) { "MEDI", tid2 };
    first_batch[2] = (monitored_task) { "LOW1", tid3 };

    wait_for_task_set(
        "first batch",
        DONE_HIGH_1 | DONE_MEDIUM_1 | DONE_LOW_1
    );
    
    // 采样并打印快照
    print_sample_ready_tasks("After First Batch", first_batch, 3);
    print_monitor_snapshot("After First Batch");
    printf("After First Batch sampling completed with 3 worker tasks alive\n");
    
    // 创建更多任务
    sc = rtems_task_create(
        rtems_build_name('H','I','G','2'),
        1,  // 最高优先级
        RTEMS_MINIMUM_STACK_SIZE,
        RTEMS_DEFAULT_MODES,
        RTEMS_DEFAULT_ATTRIBUTES,
        &tid4
    );
    rtems_test_assert(sc == RTEMS_SUCCESSFUL);
    sc = rtems_task_start(tid4, high_priority_task, 2);
    rtems_test_assert(sc == RTEMS_SUCCESSFUL);
    printf("High priority task 2 created and started\n");
    
    sc = rtems_task_create(
        rtems_build_name('L','O','W','2'),
        3,  // 低优先级
        RTEMS_MINIMUM_STACK_SIZE,
        RTEMS_DEFAULT_MODES,
        RTEMS_DEFAULT_ATTRIBUTES,
        &tid5
    );
    rtems_test_assert(sc == RTEMS_SUCCESSFUL);
    sc = rtems_task_start(tid5, low_priority_task, 2);
    rtems_test_assert(sc == RTEMS_SUCCESSFUL);
    printf("Low priority task 2 created and started\n");

    all_tasks[0] = (monitored_task) { "HIGH", tid1 };
    all_tasks[1] = (monitored_task) { "MEDI", tid2 };
    all_tasks[2] = (monitored_task) { "LOW1", tid3 };
    all_tasks[3] = (monitored_task) { "HIGH2", tid4 };
    all_tasks[4] = (monitored_task) { "LOW2", tid5 };

    wait_for_task_set("second batch", DONE_HIGH_2 | DONE_LOW_2);
    
    // 最终采样
    print_sample_ready_tasks("Final", all_tasks, 5);
    print_monitor_snapshot("Final");
    printf("Final sampling completed with all 5 worker tasks alive\n");
    
    // 打印简单监控信息
    printf("\n=== Simple Monitor Output ===\n");
    rtems_monitor_print_line();
    
    // 打印详细 CPU 使用率报告
    printf("\n=== Detailed CPU Usage Report ===\n");
    rtems_cpu_usage_report();
    
    // 打印详细监控报告
    printf("\n=== Detailed Monitor Report ===\n");
    rtems_monitor_print_detailed_report(&rtems_test_printer);

    release_monitored_tasks(all_tasks, 5);
    rtems_task_wake_after(RTEMS_MILLISECONDS_TO_TICKS(50));
    
#else
    printf("RTEMSCFG_MONITOR_CPU not defined\n");
#endif

    TEST_END();
    rtems_test_exit(0);
}

#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_SIMPLE_CONSOLE_DRIVER
#define CONFIGURE_MAXIMUM_TASKS            6
#define CONFIGURE_RTEMS_INIT_TASKS_TABLE
#define CONFIGURE_INIT_TASK_ATTRIBUTES RTEMS_FLOATING_POINT
#define CONFIGURE_INITIAL_EXTENSIONS RTEMS_TEST_INITIAL_EXTENSION
#define CONFIGURE_INIT
#include <rtems/confdefs.h>
