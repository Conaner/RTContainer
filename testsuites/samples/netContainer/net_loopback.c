/*
 * 网络容器 Loopback 通信测试 (参考 LiteOS It_net_container_007)
 * 
 * 测试目标：
 *   验证新创建的网络容器内部的 UDP loopback 通信功能
 *   - 在容器内向 127.0.0.1 发送 UDP 数据包
 *   - 接收自己发送的数据包
 *   - 验证数据内容一致性
 */

#ifdef rtems_object_id_get_api
#undef rtems_object_id_get_api
#endif
#ifdef rtems_object_id_get_class  
#undef rtems_object_id_get_class
#endif
#ifdef rtems_object_id_get_index
#undef rtems_object_id_get_index  
#endif
#ifdef rtems_object_id_get_node
#undef rtems_object_id_get_node
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <rtems.h>
#include <rtems/rtems_bsdnet.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/in_var.h>
#include <arpa/inet.h>
#include <net/if.h>

#include <rtems/score/threadimpl.h>
#include <rtems/score/container.h>
#include <rtems/score/netContainer.h>
#include <rtems/score/wkspace.h>

#include <tmacros.h>

/* 外部函数声明 */
extern int rtinit(struct ifaddr *, int, int);
extern struct in_ifaddr **rtems_net_container_get_in_ifaddr(void);

const char rtems_test_name[] = "NET CONTAINER 007 DEBUG";

#define TEST_PORT  8003
#define TEST_MSG   "Hello"

static NetContainer *test_container = NULL;
static rtems_id test_task_id;
static volatile int test_result = -1;

/* 打印容器信息 */
static void print_container_info(const char *stage)
{
    Thread_Control *executing = _Thread_Get_executing();
    printf("\n[%s] 容器信息:\n", stage);
    printf("  Thread: %p\n", (void*)executing);
    printf("  Container: %p\n", (void*)executing->container);
    
    if (executing->container) {
        printf("  NetContainer: %p\n", (void*)executing->container->netContainer);
        if (executing->container->netContainer) {
            printf("  Container ID: %d, rc=%d\n", 
                   executing->container->netContainer->containerID,
                   executing->container->netContainer->rc);
            printf("  net_group: %p\n", 
                   (void*)executing->container->netContainer->group);
        }
    }
}

/* 测试任务 */
static rtems_task udp_test_task(rtems_task_argument arg)
{
    int sock;
    struct sockaddr_in addr;
    char buf[32];
    ssize_t n;
    
    print_container_info("任务启动");
    
    // 创建 socket
    printf("\n[步骤1] 创建 socket\n");
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    printf("  socket() = %d (FD=%d, errno=%d: %s)\n", sock, sock, errno, strerror(errno));
    if (sock < 0) {
        test_result = 1;
        rtems_task_delete(RTEMS_SELF);
    }
    
    // 绑定
    printf("\n[步骤2] 绑定到 127.0.0.1:%d\n", TEST_PORT);
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(TEST_PORT);
    
    int ret = bind(sock, (struct sockaddr*)&addr, sizeof(addr));
    printf("  bind() = %d\n", ret);
    if (ret < 0) {
        printf("  错误: %s\n", strerror(errno));
        close(sock);
        test_result = 2;
        rtems_task_delete(RTEMS_SELF);
    }
    
    // 添加 loopback 路由
    {
        struct in_ifaddr **list_ptr = rtems_net_container_get_in_ifaddr();
        struct in_ifaddr *ia = *list_ptr;
        
        if (ia) {
            /* RTM_ADD=1, RTF_UP=1（网络路由）*/
            rtinit(&(ia->ia_ifa), 1, 1);
        }
    }
    
    // 发送
    printf("\n[步骤3] 发送数据\n");
    strcpy(buf, TEST_MSG);
    n = sendto(sock, buf, strlen(buf) + 1, 0, 
               (struct sockaddr*)&addr, sizeof(addr));
    if (n < 0) {
        printf("  sendto() 失败: %s\n", strerror(errno));
        close(sock);
        test_result = 3;
        rtems_task_delete(RTEMS_SELF);
    }
    printf("  发送 %zd 字节: \"%s\"\n", n, buf);
    
    // 等待数据处理
    rtems_task_wake_after(rtems_clock_get_ticks_per_second() / 2);
    
    // 4. 接收  
    printf("\n[步骤4] 接收数据\n");
    memset(buf, 0, sizeof(buf));
    
    struct timeval tv = {2, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    n = recvfrom(sock, buf, sizeof(buf) - 1, 0, NULL, NULL);
    if (n < 0) {
        printf("  recvfrom() 失败: %s\n", strerror(errno));
        close(sock);
        test_result = 4;
        rtems_task_delete(RTEMS_SELF);
    }
    printf("  接收 %zd 字节: \"%s\"\n", n, buf);
    
    // 验证
    printf("\n[步骤5] 验证数据\n");
    if (strcmp(buf, TEST_MSG) == 0) {
        printf("  数据匹配!\n");
        test_result = 0;
    } else {
        printf("   数据不匹配: 期望=\"%s\", 实际=\"%s\"\n", TEST_MSG, buf);
        test_result = 5;
    }
    
    close(sock);
    rtems_task_delete(RTEMS_SELF);
}

// 主测试
static rtems_task Init(rtems_task_argument ignored)
{
    rtems_status_code sc;
    
    TEST_BEGIN();
    
    // 初始化网络栈
    printf("\n初始化 BSD 网络栈...\n");
    if (rtems_bsdnet_initialize_network() < 0) {
        printf(" 网络栈初始化失败\n");
        TEST_END();
        rtems_test_exit(1);
    }
    printf("网络栈初始化成功\n");
    
    // 创建网络容器
    printf("\n创建网络容器...\n");
    test_container = rtems_net_container_create();
    if (!test_container) {
        printf(" 容器创建失败\n");
        TEST_END();
        rtems_test_exit(1);
    }
    printf("容器创建成功 (ID=%d, rc=%d)\n", test_container->containerID, test_container->rc);
    printf("  NetContainer: %p\n", (void*)test_container);
    printf("  net_group: %p\n", (void*)test_container->group);
    
    // 创建测试任务
    printf("\n创建测试任务...\n");
    sc = rtems_task_create(
        rtems_build_name('U', 'D', 'P', 'T'),
        1,
        RTEMS_MINIMUM_STACK_SIZE * 4,
        RTEMS_DEFAULT_MODES,
        RTEMS_DEFAULT_ATTRIBUTES,
        &test_task_id
    );
    
    if (sc != RTEMS_SUCCESSFUL) {
        printf(" 任务创建失败\n");
        TEST_END();
        rtems_test_exit(1);
    }
    printf("任务创建成功 (ID=0x%08x)\n", (unsigned)test_task_id);
    
    // 关联任务到容器
    printf("\n关联任务到容器...\n");
    ISR_lock_Context lock_context;
    Thread_Control *test_thread = _Thread_Get(test_task_id, &lock_context);
    
    if (!test_thread) {
        printf(" 无法获取任务控制块\n");
        TEST_END();
        rtems_test_exit(1);
    }
    
    printf("  Thread: %p\n", (void*)test_thread);
    printf("  Thread->container: %p\n", (void*)test_thread->container);
    
    // 记录旧容器信息
    NetContainer *old_container = test_thread->container->netContainer;
    printf("  旧 NetContainer: %p (ID=%d, rc=%d)\n", 
           (void*)old_container, 
           old_container ? old_container->containerID : 0,
           old_container ? old_container->rc : 0);
    
    _ISR_lock_ISR_enable(&lock_context);
    
    rtems_net_container_move_task(old_container, test_container, test_thread);
    
    printf("任务已关联到网络容器\n");
    printf("  新 NetContainer: %p (ID=%d, rc=%d)\n", 
           (void*)test_container, test_container->containerID, test_container->rc);
    
    // 启动任务
    printf("\n启动测试任务...\n");
    sc = rtems_task_start(test_task_id, udp_test_task, 0);
    if (sc != RTEMS_SUCCESSFUL) {
        printf(" 任务启动失败\n");
        TEST_END();
        rtems_test_exit(1);
    }
    
    // 等待完成
    rtems_task_wake_after(5 * rtems_clock_get_ticks_per_second());
    
    // 输出结果
    printf("\n");
    printf("═══════════════════════════════════════\n");
    printf("           测试结果\n");
    printf("═══════════════════════════════════════\n");
    if (test_result == 0) {
        printf("测试通过!\n");
    } else {
        printf(" 测试失败 (错误码: %d)\n", test_result);
    }
    printf("═══════════════════════════════════════\n");
    
    // 清理
    if (test_container) {
        rtems_net_container_delete(test_container);
    }
    
    TEST_END();
    rtems_test_exit(test_result == 0 ? 0 : 1);
}

// 网络栈配置
struct rtems_bsdnet_config rtems_bsdnet_config = {
    NULL, NULL, 0, 0, 0, 0, 0, 0, 0,
    {"0.0.0.0"}, {"0.0.0.0"}, 0, 0, 0, 0, 0
};

// RTEMS 配置
#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_SIMPLE_CONSOLE_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_LIBBLOCK
#define CONFIGURE_APPLICATION_NEEDS_LIBNETWORKING

#define CONFIGURE_MAXIMUM_TASKS 5
#define CONFIGURE_MAXIMUM_SEMAPHORES 5
#define CONFIGURE_MAXIMUM_FILE_DESCRIPTORS 20

#define CONFIGURE_EXECUTIVE_RAM_SIZE (2 * 1024 * 1024)

#define CONFIGURE_RTEMS_INIT_TASKS_TABLE
#define CONFIGURE_INIT

#include <rtems/confdefs.h>
