#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define CONFIGURE_INIT
#include "system.h"

#include <inttypes.h>
#include <stdlib.h>

#include <rtems/test.h>
#include <rtems/libcsupport.h>
#include <rtems/score/heapinfo.h>
#include <rtems/score/watchdogimpl.h>
#include <rtems/score/corecgroup.h>
#include <rtems/score/isrlock.h>
#include <rtems/score/thread.h>
#include <rtems/score/threadimpl.h>

#include <rtems/rtems/cgroupimpl.h>
#include <rtems/rtems/event.h>
#include <rtems/rtems/tasks.h>

const char rtems_test_name[] = "CGTEST6-6 MEM CALC";

/*
 * Boundary case: physical out-of-memory (the C heap has no block large enough)
 * versus cgroup quota overflow.
 *
 * The cgroup memory limit is set far above the physical heap so the quota path
 * never binds; every failure exercised here is a *physical* shortage.  The test
 * verifies that the heap allocator now:
 *
 *   Phase A: on physical OOM, invokes the cgroup shrink callback to reclaim
 *            memory and retries the allocation, which then succeeds.
 *   Phase B: when even shrinking cannot free enough contiguous space, reports a
 *            genuine physical OOM (malloc returns NULL) after attempting shrink.
 *
 * Physical OOM is distinguished from quota overflow by Heap_Statistics:
 * a physical failure increments failed_allocs, whereas a quota rollback does
 * not (it allocated then freed the block).
 */

#define COMPLETION_EVENT RTEMS_EVENT_0

/* 1 TiB: deliberately larger than any physical heap so the quota never binds. */
#define HUGE_MEMORY_LIMIT ( (uint64_t) 1024 * 1024 * 1024 * 1024 )

static rtems_id Init_task_id;
static void    *Reclaimable_buffer;
static size_t   Reclaimable_size;
static bool     Shrink_called;

/*
 * Report system heap usage through the malloc_info() query interface, so no
 * printing has to live in the score/heap maintenance code.  "occupied" is the
 * used part of the allocatable area and "system free" is what remains; both are
 * derived from the heap statistics, which are kept current on allocate/free.
 */
static void report_memory( const char *tag )
{
  Heap_Information_block info;

  rtems_test_assert( malloc_info( &info ) == 0 );
  printf(
    "\033[36m[cgroup-mem] %s: occupied=%" PRIuPTR " bytes, system free=%" PRIuPTR " bytes\033[0m\n",
    tag,
    (uintptr_t) ( info.Stats.size - info.Stats.free_size ),
    (uintptr_t) info.Stats.free_size
  );
}

static void reclaim_shrinker( void *arg, uintptr_t target )
{
  (void) arg;

  Shrink_called = true;

  printf(
    "\033[33m[Shrink Callback] triggered, allocator needs %lu bytes\033[0m\n",
    (unsigned long) target
  );
  report_memory( "shrink triggered, before reclaim" );

  if ( Reclaimable_buffer != NULL ) {
    size_t freed = Reclaimable_size;

    free( Reclaimable_buffer );
    Reclaimable_buffer = NULL;
    Reclaimable_size = 0;
    printf(
      "\033[33m[Shrink Callback] freed reclaimable buffer of %lu bytes\033[0m\n",
      (unsigned long) freed
    );
  } else {
    printf( "\033[33m[Shrink Callback] no memory to free\033[0m\n" );
  }

  report_memory( "shrink finished, after reclaim" );
}

static uintptr_t heap_largest_free( void )
{
  Heap_Information_block info;

  rtems_test_assert( malloc_info( &info ) == 0 );
  return info.Free.largest;
}

static uintptr_t heap_total_size( void )
{
  Heap_Information_block info;

  rtems_test_assert( malloc_info( &info ) == 0 );
  return info.Stats.size;
}

static uint32_t heap_failed_allocs( void )
{
  Heap_Information_block info;

  rtems_test_assert( malloc_info( &info ) == 0 );
  return info.Stats.failed_allocs;
}

static void phase_a_shrink_rescues_physical_oom( void )
{
  uintptr_t largest;
  uintptr_t reclaim_size;
  uintptr_t probe_size;
  void     *probe;

  printf( "[Phase A] shrink should reclaim physical memory and let retry succeed\n" );

  largest = heap_largest_free();

  printf( "[Phase A] largest free block = %lu bytes\n", (unsigned long) largest );

  /* Occupy ~75% of the largest free block and mark it reclaimable. */
  reclaim_size = largest / 2 + largest / 4;
  Reclaimable_buffer = malloc( reclaim_size );
  rtems_test_assert( Reclaimable_buffer != NULL );
  Reclaimable_size = reclaim_size;
  printf(
    "[Phase A] Allocated reclaimable (shared) buffer of %lu bytes\n",
    (unsigned long) reclaim_size
  );

  /*
   * Request ~50% of the original largest block.  Only ~25% remains free, so the
   * physical allocation cannot be satisfied and must trigger the shrink
   * callback; after the reclaimable buffer is freed, the retry succeeds.
   */
  probe_size = largest / 2;
  rtems_test_assert( probe_size > heap_largest_free() );

  Shrink_called = false;
  printf(
    "[Phase A] Requesting %lu bytes (exceeds remaining physical memory)\n",
    (unsigned long) probe_size
  );
  report_memory( "Phase A request, before malloc" );
  probe = malloc( probe_size );
  report_memory( "Phase A request, after malloc" );

  rtems_test_assert( Shrink_called );
  rtems_test_assert( probe != NULL );
  printf( "\033[32m[Phase A] Allocation succeeded after shrink reclaimed physical memory\033[0m\n" );

  free( probe );
}

static void phase_b_unrecoverable_physical_oom( void )
{
  uintptr_t total;
  uintptr_t impossible;
  uintptr_t reclaim_size;
  uint32_t  failed_before;
  uint32_t  failed_after;
  void     *huge;

  printf( "[Phase B] shrink cannot help; allocator must report physical OOM\n" );

  /* Provide something for the shrink callback to free, to prove it still ran. */
  reclaim_size = heap_largest_free() / 2;
  Reclaimable_buffer = malloc( reclaim_size );
  rtems_test_assert( Reclaimable_buffer != NULL );
  Reclaimable_size = reclaim_size;
  printf(
    "[Phase B] Allocated reclaimable (shared) buffer of %lu bytes\n",
    (unsigned long) reclaim_size
  );

  /* Larger than the entire heap: no amount of shrinking can satisfy it. */
  total = heap_total_size();
  impossible = total + total;

  Shrink_called = false;
  printf(
    "[Phase B] Requesting %lu bytes (larger than entire heap)\n",
    (unsigned long) impossible
  );
  report_memory( "Phase B request, before malloc" );
  failed_before = heap_failed_allocs();
  huge = malloc( impossible );
  report_memory( "Phase B request, after malloc" );

  failed_after = heap_failed_allocs();

  rtems_test_assert( Shrink_called );
  rtems_test_assert( huge == NULL );
  printf( "\033[32m[Phase B] Allocation failed: genuine physical OOM after shrink\033[0m\n" );

  rtems_test_assert( failed_after == failed_before + 1 );
  printf( "[Phase B] failed_allocs incremented -> reported as physical OOM\n" );

  /* The shrink callback already freed the reclaimable buffer. */
  rtems_test_assert( Reclaimable_buffer == NULL );
}

static rtems_task worker_task( rtems_task_argument arg )
{
  const Thread_Control *self = _Thread_Get_executing();
  CORE_cgroup_Control  *cg =
    ( self != NULL && self->is_added_to_cgroup ) ? self->cgroup : NULL;

  (void) arg;
  rtems_test_assert( cg != NULL );
  printf(
    "Worker task started in CG1, mem quota=%" PRIu64 "\n",
    (uint64_t) cg->mem_quota_available
  );

  phase_a_shrink_rescues_physical_oom();
  phase_b_unrecoverable_physical_oom();

  rtems_event_send( Init_task_id, COMPLETION_EVENT );
  rtems_task_exit();
}

rtems_task Init( rtems_task_argument ignored )
{
  (void) ignored;

  rtems_print_printer_fprintf_putc( &rtems_test_printer );
  TEST_BEGIN();

  CORE_cgroup_config config = {
    .cpu_shares = 1,
    .cpu_quota = _Watchdog_Ticks_per_second * 60,
    .cpu_period = _Watchdog_Ticks_per_second * 60,
    .memory_limit = HUGE_MEMORY_LIMIT,
    .blkio_limit = 0
  };
  rtems_id cg_id;
  rtems_status_code status;

  status = rtems_cgroup_create( rtems_build_name( 'C', 'G', '1', ' ' ), &cg_id, &config );
  rtems_test_assert( status == RTEMS_SUCCESSFUL );
  printf(
    "Config: 1 cgroup, memory_limit=%" PRIu64 " bytes (far above physical heap)\n",
    config.memory_limit
  );

  ISR_lock_Context lock_context;
  Cgroup_Control *the_cgroup = _Cgroup_Get( cg_id, &lock_context );
  rtems_test_assert( the_cgroup != NULL );
  the_cgroup->cgroup.shrink_callback = reclaim_shrinker;
  the_cgroup->cgroup.shrink_arg = NULL;
  _ISR_lock_ISR_enable( &lock_context );

  Init_task_id = rtems_task_self();

  /*
   * The worker must belong to the cgroup before it allocates, otherwise the
   * heap allocator would not see a cgroup shrink callback.  Create, add, then
   * start (the worker has higher priority than Init and would preempt it).
   */
  status = rtems_task_create(
    rtems_build_name( '6', '6', 'T', '0' ),
    8,
    RTEMS_MINIMUM_STACK_SIZE,
    RTEMS_DEFAULT_MODES,
    RTEMS_DEFAULT_ATTRIBUTES,
    &Task_id[0]
  );
  rtems_test_assert( status == RTEMS_SUCCESSFUL );

  status = rtems_cgroup_add_task( cg_id, Task_id[0] );
  rtems_test_assert( status == RTEMS_SUCCESSFUL );

  status = rtems_task_start( Task_id[0], worker_task, 0 );
  rtems_test_assert( status == RTEMS_SUCCESSFUL );

  printf( "Init waiting for worker completion\n" );
  rtems_event_set received;
  status = rtems_event_receive(
    COMPLETION_EVENT,
    RTEMS_EVENT_ALL | RTEMS_WAIT,
    RTEMS_NO_TIMEOUT,
    &received
  );
  rtems_test_assert( status == RTEMS_SUCCESSFUL );

  printf(
    "CGTEST6-6 completed: physical OOM triggers shrink+retry; unrecoverable OOM returns NULL\n"
  );

  TEST_END();
  rtems_test_exit( 0 );
}
