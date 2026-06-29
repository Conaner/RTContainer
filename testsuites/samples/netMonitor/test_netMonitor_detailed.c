/*
 * 网络监控详细测试
 *
 * 测试网络监控功能在不同网络活动下的表现
 */

#include <rtems.h>
#include <rtems/rtems_bsdnet.h>
#include <stdbool.h>
#include <rtems/score/netContainer.h>
#include <rtems/score/monitor.h>
#include <rtems/score/threadimpl.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <unistd.h>

#define PACKET_SENDER_PORT 19030
#define PACKET_SENDER_INTERVAL_TICKS 10

rtems_task Init(rtems_task_argument argument);
rtems_task NetworkTask(rtems_task_argument argument);
rtems_task PacketSenderTask(rtems_task_argument argument);

static NetContainer *monitor_net_container;
static volatile bool packet_sender_running;
static volatile uint32_t packet_sender_count;

static NetContainer *enter_monitor_net_container(const char *role)
{
    Thread_Control *self;
    NetContainer *src;

    self = _Thread_Get_executing();
    if (self == NULL || self->container == NULL || self->container->netContainer == NULL ||
        monitor_net_container == NULL) {
        printf("%s could not enter monitor net container\n", role);
        exit(1);
    }

    src = self->container->netContainer;
    if (src != monitor_net_container) {
        rtems_net_container_move_task(src, monitor_net_container, self);
    }

    return src;
}

static void leave_monitor_net_container(NetContainer *dst)
{
    Thread_Control *self;
    NetContainer *src;

    self = _Thread_Get_executing();
    if (self == NULL || self->container == NULL || self->container->netContainer == NULL ||
        dst == NULL) {
        return;
    }

    src = self->container->netContainer;
    if (src != dst) {
        rtems_net_container_move_task(src, dst, self);
    }
}

static void print_network_snapshot(const char *phase)
{
    RtemsMonitor snapshot;

    /* 先采样数据，然后获取快照 */
    rtems_monitor_sample();
    rtems_monitor_get_snapshot(&snapshot);

    printf("\n=== Network Snapshot: %s ===\n", phase);

#ifdef RTEMSCFG_MONITOR_NET
    printf("Network Statistics:\n");
    printf("  RX Bytes: %llu\n", (unsigned long long)snapshot.net.rx_bytes);
    printf("  TX Bytes: %llu\n", (unsigned long long)snapshot.net.tx_bytes);
    printf("  RX Packets: %llu\n", (unsigned long long)snapshot.net.rx_packets);
    printf("  TX Packets: %llu\n", (unsigned long long)snapshot.net.tx_packets);
    printf("  RX Errors: %llu\n", (unsigned long long)snapshot.net.rx_errors);
    printf("  TX Errors: %llu\n", (unsigned long long)snapshot.net.tx_errors);
#endif
}

rtems_task PacketSenderTask(rtems_task_argument argument)
{
    NetContainer *original_net_container;
    int sock;
    struct sockaddr_in addr;
    struct timeval timeout;
    const char payload[] = "net-monitor-background-packet";
    rtems_interval interval;

    (void) argument;

    original_net_container = enter_monitor_net_container("Packet sender task");
    printf("Packet sender task started\n");

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        printf("Packet sender socket() failed, errno=%d\n", errno);
        packet_sender_running = false;
        leave_monitor_net_container(original_net_container);
        rtems_task_delete(RTEMS_SELF);
        return;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(PACKET_SENDER_PORT);

    if (bind(sock, (struct sockaddr *) &addr, sizeof(addr)) != 0) {
        printf("Packet sender bind() failed, errno=%d\n", errno);
        close(sock);
        packet_sender_running = false;
        leave_monitor_net_container(original_net_container);
        rtems_task_delete(RTEMS_SELF);
        return;
    }

    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
        printf("Packet sender setsockopt() failed, errno=%d\n", errno);
        close(sock);
        packet_sender_running = false;
        leave_monitor_net_container(original_net_container);
        rtems_task_delete(RTEMS_SELF);
        return;
    }

    interval = rtems_clock_get_ticks_per_second() / PACKET_SENDER_INTERVAL_TICKS;
    if (interval == 0) {
        interval = 1;
    }

    while (packet_sender_running) {
        char rx_buffer[sizeof(payload)];
        ssize_t n;

        n = sendto(
            sock,
            payload,
            sizeof(payload),
            0,
            (struct sockaddr *) &addr,
            sizeof(addr)
        );
        if (n < 0) {
            printf("Packet sender sendto() failed, errno=%d\n", errno);
            break;
        }

        n = recvfrom(sock, rx_buffer, sizeof(rx_buffer), 0, NULL, NULL);
        if (n < 0) {
            printf("Packet sender recvfrom() failed, errno=%d\n", errno);
            break;
        }

        ++packet_sender_count;
        rtems_task_wake_after(interval);
    }

    close(sock);
    printf("Packet sender task stopped after %lu packets\n",
           (unsigned long)packet_sender_count);
    leave_monitor_net_container(original_net_container);
    rtems_task_delete(RTEMS_SELF);
}

rtems_task NetworkTask(rtems_task_argument argument)
{
    NetContainer *original_net_container;

    (void) argument;

    original_net_container = enter_monitor_net_container("Network task");
    printf("Network task started\n");

    /* 模拟一些网络活动 */
    for (int i = 0; i < 5; i++) {
        printf("Network activity cycle %d\n", i + 1);

        /* 等待一段时间 */
        rtems_task_wake_after(rtems_clock_get_ticks_per_second());

        /* 打印网络快照 */
        char phase[32];
        snprintf(phase, sizeof(phase), "Cycle %d", i + 1);
        print_network_snapshot(phase);
    }

    printf("Network task completed\n");
    leave_monitor_net_container(original_net_container);
    rtems_task_delete(RTEMS_SELF);
}

rtems_task Init(rtems_task_argument argument)
{
    rtems_status_code status;
    rtems_id network_task_id;
    rtems_id sender_task_id;
    NetContainer *original_net_container;

    printf("\n=== Network Monitor Detailed Test ===\n");

#ifdef RTEMSCFG_MONITOR_NET
    printf("Network monitoring is enabled\n");

    if (rtems_bsdnet_initialize_network() < 0) {
        printf("Network stack initialization failed\n");
        exit(1);
    }
    printf("Network stack initialized\n");

    monitor_net_container = rtems_net_container_create();
    if (monitor_net_container == NULL) {
        printf("Failed to create monitor net container\n");
        exit(1);
    }
    original_net_container = enter_monitor_net_container("Init task");
    printf("Monitor net container created: id=%d\n", monitor_net_container->containerID);

    /* 初始化监控系统 */
    rtems_monitor_initialize();
    printf("Monitor system initialized\n");

    /* 初始网络快照 */
    print_network_snapshot("Initial");

    packet_sender_running = true;

    /* 创建后台发包任务 */
    status = rtems_task_create(
        rtems_build_name('S', 'N', 'D', '1'),
        2,
        RTEMS_MINIMUM_STACK_SIZE * 4,
        RTEMS_DEFAULT_MODES,
        RTEMS_DEFAULT_ATTRIBUTES,
        &sender_task_id
    );

    if (status != RTEMS_SUCCESSFUL) {
        printf("Failed to create packet sender task: %s\n", rtems_status_text(status));
        exit(1);
    }

    status = rtems_task_start(sender_task_id, PacketSenderTask, 0);
    if (status != RTEMS_SUCCESSFUL) {
        printf("Failed to start packet sender task: %s\n", rtems_status_text(status));
        exit(1);
    }

    /* 创建网络采样任务 */
    status = rtems_task_create(
        rtems_build_name('N', 'E', 'T', '1'),
        1,
        RTEMS_MINIMUM_STACK_SIZE,
        RTEMS_DEFAULT_MODES,
        RTEMS_DEFAULT_ATTRIBUTES,
        &network_task_id
    );

    if (status != RTEMS_SUCCESSFUL) {
        printf("Failed to create network task: %s\n", rtems_status_text(status));
        exit(1);
    }

    status = rtems_task_start(network_task_id, NetworkTask, 0);
    if (status != RTEMS_SUCCESSFUL) {
        printf("Failed to start network task: %s\n", rtems_status_text(status));
        exit(1);
    }

    /* 等待网络任务完成 */
    rtems_task_wake_after(rtems_clock_get_ticks_per_second() * 6);
    packet_sender_running = false;
    rtems_task_wake_after(rtems_clock_get_ticks_per_second());

    /* 最终网络快照 */
    print_network_snapshot("Final");
    printf("Background packets sent: %lu\n", (unsigned long)packet_sender_count);

    leave_monitor_net_container(original_net_container);
    rtems_net_container_delete(monitor_net_container);
    monitor_net_container = NULL;

    printf("\nNetwork monitor detailed test completed successfully!\n");
#else
    printf("Network monitoring is not enabled\n");
#endif

    printf("\n*** END OF NETWORK MONITOR DETAILED TEST ***\n");
    exit(0);
}

struct rtems_bsdnet_config rtems_bsdnet_config = {
    NULL, NULL, 0, 0, 0, 0, 0, 0, 0,
    {"0.0.0.0"}, {"0.0.0.0"}, 0, 0, 0, 0, 0
};

#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_CONSOLE_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_LIBBLOCK
#define CONFIGURE_APPLICATION_NEEDS_LIBNETWORKING

#define CONFIGURE_MAXIMUM_TASKS 16
#define CONFIGURE_MAXIMUM_FILE_DESCRIPTORS 20
#define CONFIGURE_RTEMS_INIT_TASKS_TABLE

#define CONFIGURE_INIT

#include <rtems/confdefs.h>
