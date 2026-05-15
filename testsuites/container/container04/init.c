#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <rtems.h>
#include <rtems/counter.h>
#include <rtems/rtems_bsdnet.h>
#include <rtems/score/container.h>
#include <rtems/score/netContainer.h>
#include <rtems/score/threadimpl.h>
#include <tmacros.h>

#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

const char rtems_test_name[] = "CONTAINER 04";

#define ITERATIONS 200u
#define LOG_INTERVAL 50u

#define EVT_SOCKET_RECEIVER_DONE RTEMS_EVENT_0
#define EVT_SOCKET_SENDER_DONE RTEMS_EVENT_1
#define EVT_SHM_RECEIVER_DONE RTEMS_EVENT_2
#define EVT_SHM_SENDER_DONE RTEMS_EVENT_3

#define SOCKET_PORT 19004

typedef struct {
  uint32_t seq;
  uint64_t stamp;
} LatencyPacket;

typedef struct {
  volatile uint32_t seq;
  volatile uint32_t ack;
  volatile rtems_counter_ticks stamp;
} ShmChannel;

static rtems_id init_task_id;
static RtemsContainer *container_a;
static RtemsContainer *container_b;
static NetContainer *socket_net_container;

static volatile bool socket_receiver_ready;
static volatile bool shm_receiver_ready;

static volatile rtems_counter_ticks socket_total_ticks;
static volatile rtems_counter_ticks shm_total_ticks;

static ShmChannel shm_channel;

static void enter_shared_socket_net(Thread_Control *self, const char *role)
{
  NetContainer *src;

  rtems_test_assert(self != NULL);
  rtems_test_assert(self->container != NULL);
  rtems_test_assert(self->container->netContainer != NULL);
  rtems_test_assert(socket_net_container != NULL);

  src = self->container->netContainer;
  if (src != socket_net_container) {
    rtems_net_container_move_task(src, socket_net_container, self);
  }

  rtems_test_assert(self->container->netContainer == socket_net_container);
  printf("[socket][%s] switched to shared net container id=%d\n", role, socket_net_container->containerID);
}

static rtems_task socket_receiver_task(rtems_task_argument arg)
{
  rtems_status_code sc;
  Thread_Control *self;
  int sock;
  struct sockaddr_in bind_addr;

  (void) arg;

  self = _Thread_Get_executing();
  sc = rtems_unified_container_enter(container_b, self);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  printf("[socket][receiver] entered container B\n");
  enter_shared_socket_net(self, "receiver");

  sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    printf("[socket][receiver] socket() failed, errno=%d\n", errno);
  }
  rtems_test_assert(sock >= 0);

  memset(&bind_addr, 0, sizeof(bind_addr));
  bind_addr.sin_family = AF_INET;
  bind_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
  bind_addr.sin_port = htons(SOCKET_PORT);

  if (bind(sock, (struct sockaddr *) &bind_addr, sizeof(bind_addr)) != 0) {
    printf("[socket][receiver] bind(127.0.0.1) failed, errno=%d\n", errno);
    rtems_test_assert(false);
  }

  socket_receiver_ready = true;
  printf("[socket][receiver] ready at 127.0.0.1:%d\n", SOCKET_PORT);

  for (uint32_t i = 1; i <= ITERATIONS; ++i) {
    LatencyPacket packet;
    struct sockaddr_in peer_addr;
    socklen_t peer_len = sizeof(peer_addr);
    ssize_t n;

    n = recvfrom(sock, &packet, sizeof(packet), 0, (struct sockaddr *) &peer_addr, &peer_len);
    rtems_test_assert(n == (ssize_t) sizeof(packet));

    n = sendto(sock, &packet, sizeof(packet), 0, (struct sockaddr *) &peer_addr, peer_len);
    rtems_test_assert(n == (ssize_t) sizeof(packet));

    if ((i % LOG_INTERVAL) == 0u) {
      printf("[socket][receiver] progress: %" PRIu32 "/%" PRIu32 "\n", i, ITERATIONS);
    }
  }

  close(sock);

  sc = rtems_unified_container_leave(container_b, self);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  sc = rtems_event_send(init_task_id, EVT_SOCKET_RECEIVER_DONE);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  rtems_task_exit();
}

static rtems_task socket_sender_task(rtems_task_argument arg)
{
  rtems_status_code sc;
  Thread_Control *self;
  int sock;
  struct sockaddr_in peer_addr;

  (void) arg;

  self = _Thread_Get_executing();
  sc = rtems_unified_container_enter(container_a, self);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  printf("[socket][sender] entered container A\n");
  enter_shared_socket_net(self, "sender");

  while (!socket_receiver_ready) {
    rtems_task_wake_after(1);
  }

  sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    printf("[socket][sender] socket() failed, errno=%d\n", errno);
  }
  rtems_test_assert(sock >= 0);

  memset(&peer_addr, 0, sizeof(peer_addr));
  peer_addr.sin_family = AF_INET;
  peer_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
  peer_addr.sin_port = htons(SOCKET_PORT);

  for (uint32_t i = 1; i <= ITERATIONS; ++i) {
    LatencyPacket tx;
    LatencyPacket rx;
    ssize_t n;

    tx.seq = i;
    tx.stamp = (uint64_t) rtems_counter_read();

    n = sendto(sock, &tx, sizeof(tx), 0, (struct sockaddr *) &peer_addr, sizeof(peer_addr));
    rtems_test_assert(n == (ssize_t) sizeof(tx));

    n = recvfrom(sock, &rx, sizeof(rx), 0, NULL, NULL);
    rtems_test_assert(n == (ssize_t) sizeof(rx));
    rtems_test_assert(rx.seq == i);

    socket_total_ticks += rtems_counter_difference(rtems_counter_read(), (rtems_counter_ticks) tx.stamp);

    if ((i % LOG_INTERVAL) == 0u) {
      printf("[socket][sender] progress: %" PRIu32 "/%" PRIu32 "\n", i, ITERATIONS);
    }
  }

  close(sock);

  sc = rtems_unified_container_leave(container_a, self);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  sc = rtems_event_send(init_task_id, EVT_SOCKET_SENDER_DONE);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  rtems_task_exit();
}

static rtems_task shm_receiver_task(rtems_task_argument arg)
{
  rtems_status_code sc;
  Thread_Control *self;
  uint32_t last_seq;

  (void) arg;

  self = _Thread_Get_executing();
  sc = rtems_unified_container_enter(container_b, self);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  printf("[shm][receiver] entered container B\n");

  shm_channel.seq = 0;
  shm_channel.ack = 0;
  shm_channel.stamp = 0;
  last_seq = 0;
  shm_receiver_ready = true;

  for (uint32_t i = 1; i <= ITERATIONS; ++i) {
    while (shm_channel.seq == last_seq) {
      rtems_task_wake_after(1);
    }

    last_seq = shm_channel.seq;
    shm_channel.ack = last_seq;

    if ((i % LOG_INTERVAL) == 0u) {
      printf("[shm][receiver] progress: %" PRIu32 "/%" PRIu32 "\n", i, ITERATIONS);
    }
  }

  sc = rtems_unified_container_leave(container_b, self);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  sc = rtems_event_send(init_task_id, EVT_SHM_RECEIVER_DONE);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  rtems_task_exit();
}

static rtems_task shm_sender_task(rtems_task_argument arg)
{
  rtems_status_code sc;
  Thread_Control *self;

  (void) arg;

  self = _Thread_Get_executing();
  sc = rtems_unified_container_enter(container_a, self);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  printf("[shm][sender] entered container A\n");

  while (!shm_receiver_ready) {
    rtems_task_wake_after(1);
  }

  for (uint32_t i = 1; i <= ITERATIONS; ++i) {
    rtems_counter_ticks begin = rtems_counter_read();

    shm_channel.stamp = begin;
    shm_channel.seq = i;

    while (shm_channel.ack != i) {
      rtems_task_wake_after(1);
    }

    shm_total_ticks += rtems_counter_difference(rtems_counter_read(), begin);

    if ((i % LOG_INTERVAL) == 0u) {
      printf("[shm][sender] progress: %" PRIu32 "/%" PRIu32 "\n", i, ITERATIONS);
    }
  }

  sc = rtems_unified_container_leave(container_a, self);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  sc = rtems_event_send(init_task_id, EVT_SHM_SENDER_DONE);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  rtems_task_exit();
}

static void run_socket_phase(void)
{
  rtems_id sender_id;
  rtems_id receiver_id;
  rtems_event_set received;
  rtems_status_code sc;

  printf("[init] ===== Socket latency phase start =====\n");

  socket_receiver_ready = false;
  socket_total_ticks = 0;

  sc = rtems_task_create(
    rtems_build_name('S', 'R', 'X', '1'),
    10,
    RTEMS_MINIMUM_STACK_SIZE * 4,
    RTEMS_DEFAULT_MODES,
    RTEMS_DEFAULT_ATTRIBUTES,
    &receiver_id
  );
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  sc = rtems_task_create(
    rtems_build_name('S', 'T', 'X', '1'),
    10,
    RTEMS_MINIMUM_STACK_SIZE * 4,
    RTEMS_DEFAULT_MODES,
    RTEMS_DEFAULT_ATTRIBUTES,
    &sender_id
  );
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  sc = rtems_task_start(receiver_id, socket_receiver_task, 0);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  sc = rtems_task_start(sender_id, socket_sender_task, 0);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  sc = rtems_event_receive(
    EVT_SOCKET_RECEIVER_DONE | EVT_SOCKET_SENDER_DONE,
    RTEMS_EVENT_ALL | RTEMS_WAIT,
    RTEMS_NO_TIMEOUT,
    &received
  );
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  printf("[init] ===== Socket latency phase done =====\n");
}

static void run_shm_phase(void)
{
  rtems_id sender_id;
  rtems_id receiver_id;
  rtems_event_set received;
  rtems_status_code sc;

  printf("[init] ===== Shared-memory latency phase start =====\n");

  shm_receiver_ready = false;
  shm_total_ticks = 0;

  sc = rtems_task_create(
    rtems_build_name('H', 'R', 'X', '1'),
    9,
    RTEMS_MINIMUM_STACK_SIZE * 4,
    RTEMS_DEFAULT_MODES,
    RTEMS_DEFAULT_ATTRIBUTES,
    &receiver_id
  );
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  sc = rtems_task_create(
    rtems_build_name('H', 'T', 'X', '1'),
    10,
    RTEMS_MINIMUM_STACK_SIZE * 4,
    RTEMS_DEFAULT_MODES,
    RTEMS_DEFAULT_ATTRIBUTES,
    &sender_id
  );
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  sc = rtems_task_start(receiver_id, shm_receiver_task, 0);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  sc = rtems_task_start(sender_id, shm_sender_task, 0);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  sc = rtems_event_receive(
    EVT_SHM_RECEIVER_DONE | EVT_SHM_SENDER_DONE,
    RTEMS_EVENT_ALL | RTEMS_WAIT,
    RTEMS_NO_TIMEOUT,
    &received
  );
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  printf("[init] ===== Shared-memory latency phase done =====\n");
}

static rtems_task Init(rtems_task_argument arg)
{
  RtemsContainerConfig config;
  rtems_status_code sc;
  uint64_t socket_avg_ns;
  uint64_t shm_avg_ns;

  (void) arg;

  TEST_BEGIN();
  printf("[init] container04 start: compare socket vs shared-memory latency\n");

  init_task_id = rtems_task_self();

  rtems_test_assert(rtems_bsdnet_initialize_network() == 0);
  printf("[init] network stack initialized\n");

  rtems_unified_container_config_initialize(&config);
  config.flags = RTEMS_UNIFIED_CONTAINER_PID;

  sc = rtems_unified_container_create(&config, &container_a);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  sc = rtems_unified_container_create(&config, &container_b);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  socket_net_container = rtems_net_container_create();
  rtems_test_assert(socket_net_container != NULL);
  printf("[init] shared socket net container created: id=%d\n", socket_net_container->containerID);

  printf("[init] containers created: A=%p, B=%p\n", (void *) container_a, (void *) container_b);

  run_socket_phase();
  run_shm_phase();

  socket_avg_ns = rtems_counter_ticks_to_nanoseconds(socket_total_ticks) / ITERATIONS;
  shm_avg_ns = rtems_counter_ticks_to_nanoseconds(shm_total_ticks) / ITERATIONS;

  printf("[result] socket avg round-trip latency: %" PRIu64 " ns\n", socket_avg_ns);
  printf("[result] shm    avg round-trip latency: %" PRIu64 " ns\n", shm_avg_ns);

  if (shm_avg_ns > 0u) {
    printf("[result] socket/shm ratio: %" PRIu64 "\n", socket_avg_ns / shm_avg_ns);
  }

  sc = rtems_unified_container_delete(container_a);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);
  sc = rtems_unified_container_delete(container_b);
  rtems_test_assert(sc == RTEMS_SUCCESSFUL);

  if (socket_net_container != NULL) {
    rtems_net_container_delete(socket_net_container);
    socket_net_container = NULL;
  }

  printf("[init] container04 done\n");
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

#define CONFIGURE_MAXIMUM_TASKS 16
#define CONFIGURE_MAXIMUM_CGROUPS 2
#define CONFIGURE_MAXIMUM_FILE_DESCRIPTORS 64

#define CONFIGURE_RTEMS_INIT_TASKS_TABLE
#define CONFIGURE_INIT_TASK_ATTRIBUTES RTEMS_FLOATING_POINT
#define CONFIGURE_INITIAL_EXTENSIONS RTEMS_TEST_INITIAL_EXTENSION

#define CONFIGURE_INIT
#include <rtems/confdefs.h>
