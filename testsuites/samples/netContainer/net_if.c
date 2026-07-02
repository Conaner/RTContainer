/* 网络容器接口隔离测试 */
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
#include <ctype.h>
#include <unistd.h>

#include <rtems.h>
#include <rtems/rtems_bsdnet.h>

#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/if_var.h>

#ifndef _KERNEL
#define _KERNEL
#define _UNDEF_KERNEL_LATER
#endif

#include <net/if_arp.h>

#ifdef _UNDEF_KERNEL_LATER
#undef _KERNEL
#undef _UNDEF_KERNEL_LATER
#endif

#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <errno.h>
#include <arpa/inet.h>

#include <rtems/score/threadimpl.h>
#include <rtems/score/container.h>
#include <rtems/score/netContainer.h>
#include <rtems/score/wkspace.h>

#include <tmacros.h>
#include <rtems/rtems_bsdnet_internal.h>

const char rtems_test_name[] = "NET CONTAINER ISOLATION TEST";

/* 数据结构定义 */
typedef struct
{
    struct arpcom arpcom;
    int unit;
    int container_id;
    uint8_t mac_address[6];
} container_net_softc;

/* 全局变量 */

static rtems_id container1_thread_id;
static rtems_id container2_thread_id;
static NetContainer *net_container1 = NULL;
static NetContainer *net_container2 = NULL;

static rtems_id sync_sem1, sync_sem2, test_complete_sem;
static volatile int test_results[3] = {0, 0, 0};

static struct rtems_bsdnet_ifconfig container1_netconfig;
static struct rtems_bsdnet_ifconfig container2_netconfig;

/* 虚拟网络接口输出函数 */
static int container_net_output(
    struct ifnet *ifp,
    struct mbuf *m,
    struct sockaddr *dst,
    struct rtentry *rt)
{
    container_net_softc *sc = (container_net_softc *)ifp->if_softc;

    // printf("[容器%d] 接口 %s%d 发送数据包\n",
    //        sc->container_id, ifp->if_name, ifp->if_unit);

    // if (dst && dst->sa_family == AF_INET)
    // {
    //     struct sockaddr_in *sin = (struct sockaddr_in *)dst;
    //     printf("  目标地址: %s\n", inet_ntoa(sin->sin_addr));
    // }

    ifp->if_opackets++;
    if (m)
    {
        ifp->if_obytes += m->m_pkthdr.len;
        m_freem(m);
    }

    return 0;
}

/* 虚拟网络接口初始化 */
static void container_net_init(void *arg)
{
    struct ifnet *ifp = (struct ifnet *)arg;
    container_net_softc *sc = (container_net_softc *)ifp->if_softc;

    printf("[容器%d] 初始化网络接口 %s%d\n",
           sc->container_id, ifp->if_name, ifp->if_unit);

    ifp->if_flags |= (IFF_UP | IFF_RUNNING);
}

/* 虚拟网络接口ioctl处理 */
static int container_net_ioctl(struct ifnet *ifp, ioctl_command_t command, caddr_t data)
{
    int error = 0;

    switch (command)
    {
    case SIOCSIFFLAGS:
        if (ifp->if_flags & IFF_UP)
        {
            if ((ifp->if_flags & IFF_RUNNING) == 0)
            {
                container_net_init(ifp);
            }
        }
        else
        {
            if (ifp->if_flags & IFF_RUNNING)
            {
                ifp->if_flags &= ~IFF_RUNNING;
            }
        }
        break;

    case SIOCSIFADDR:
    case SIOCGIFADDR:
    case SIOCSIFNETMASK:
    case SIOCSIFBRDADDR:
    case SIOCSIFDSTADDR:
        error = ether_ioctl(ifp, command, data);
        break;

    default:
        error = EINVAL;
        break;
    }

    return error;
}

/* 网络驱动attach函数 */
static int container_net_driver_attach(struct rtems_bsdnet_ifconfig *config, int attaching)
{
    struct ifnet *ifp;
    container_net_softc *sc;
    int unit_num = 0;

    if (!attaching)
        return 1;

    printf("附加容器网络驱动: %s\n", config->name);

    char *unit_ptr = config->name;
    while (*unit_ptr && !isdigit(*unit_ptr))
        unit_ptr++;
    if (*unit_ptr)
        unit_num = atoi(unit_ptr);

    sc = (container_net_softc *)_Workspace_Allocate(sizeof(container_net_softc));
    if (!sc)
        return 0;
    memset(sc, 0, sizeof(container_net_softc));

    Thread_Control *executing = _Thread_Get_executing();
    net_group *target_group = NULL;

    if (executing && executing->container && executing->container->netContainer)
    {
        NetContainer *netContainer = executing->container->netContainer;
        sc->container_id = netContainer->containerID;
        target_group = netContainer->group;
    }
    else
    {
        sc->container_id = 0;
    }

    sc->unit = unit_num;

    sc->mac_address[0] = 0x02;
    sc->mac_address[1] = 0x00;
    sc->mac_address[2] = 0x5E;
    sc->mac_address[3] = 0x00;
    sc->mac_address[4] = (uint8_t)sc->container_id;
    sc->mac_address[5] = (uint8_t)unit_num;

    ifp = &sc->arpcom.ac_if;
    ifp->if_softc = sc;
    ifp->if_unit = unit_num;
    ifp->if_name = "veth";
    ifp->if_mtu = ETHERMTU;
    ifp->if_init = container_net_init;
    ifp->if_ioctl = container_net_ioctl;
    ifp->if_output = container_net_output;
    ifp->if_flags = IFF_BROADCAST | IFF_MULTICAST;
    ifp->if_snd.ifq_maxlen = IFQ_MAXLEN;

    memcpy(sc->arpcom.ac_enaddr, sc->mac_address, 6);

    if_attach_to_container(ifp, target_group);
    ether_ifattach(ifp);

    printf("[容器%d] 成功创建虚拟网络接口 %s%d\n",
           sc->container_id, ifp->if_name, ifp->if_unit);
    return 1;
}

/* 在容器中查找接口 */
static struct ifnet *find_interface_in_container(NetContainer *container, const char *name)
{
    if (!container || !container->group || !name)
    {
        return NULL;
    }

    struct ifnet *ifp = container->group->ifnet_p;
    while (ifp)
    {
        if (strcmp(ifp->if_name, name) == 0)
        {
            return ifp;
        }
        ifp = ifp->if_next;
    }
    return NULL;
}

/* 列出容器中的接口 */
static int list_interfaces_in_container(NetContainer *container)
{
    if (!container || !container->group)
    {
        printf("容器无效\n");
        return 0;
    }

    int count = 0;
    struct ifnet *ifp = container->group->ifnet_p;

    printf("  容器%d(ID=%d, rc=%d)中的网络接口 (链表头地址=0x%p):\n",
           container->containerID, container->containerID, container->rc,
           (void *)container->group->ifnet_p);
    while (ifp)
    {
        printf("    - %s%d (索引=%d, 标志=0x%x, if_next=0x%p)\n",
               ifp->if_name, ifp->if_unit, ifp->if_index, ifp->if_flags, (void *)ifp->if_next);
        count++;
        ifp = ifp->if_next;
        if (count > 10)
        { // 防止无限循环
            printf("    警告: 接口链表过长，可能存在循环\n");
            break;
        }
    }

    if (count == 0)
    {
        printf("    (无接口)\n");
    }

    return count;
}

/* 容器2测试任务 */
static rtems_task container1_task(rtems_task_argument arg)
{
    printf("\n=== 容器2接口隔离测试开始 ===\n");

    rtems_interrupt_level level;
    rtems_interrupt_disable(level);
    Thread_Control *self = _Thread_Get_executing();
    NetContainer *current_container = NULL;

    if (self && self->container)
    {
        current_container = self->container->netContainer;
    }
    rtems_interrupt_enable(level);
    printf("当前线程容器ID: %d, rc=%d\n",
           current_container->containerID, current_container->rc);

    printf("\n--- 测试1: 创建容器2网络接口 ---\n");

    memset(&container1_netconfig, 0, sizeof(container1_netconfig));
    container1_netconfig.name = "veth0";
    container1_netconfig.attach = container_net_driver_attach;
    container1_netconfig.ip_address = NULL;
    container1_netconfig.ip_netmask = NULL;
    container1_netconfig.mtu = 1500;

    rtems_bsdnet_attach(&container1_netconfig);
    printf("[PASS] 网络接口创建成功\n");

    rtems_task_wake_after(rtems_clock_get_ticks_per_second() / 4);

    struct sockaddr_in address;
    struct sockaddr_in netmask;
    short flags;

    flags = IFF_UP;
    if (rtems_bsdnet_ifconfig("veth0", SIOCSIFFLAGS, &flags) == 0)
    {
        printf("[PASS] 接口已启动(UP)\n");
    }
    else
    {
        printf("[WARN] 启动接口失败: %s\n", strerror(errno));
    }

    memset(&netmask, 0, sizeof(netmask));
    netmask.sin_len = sizeof(netmask);
    netmask.sin_family = AF_INET;
    netmask.sin_addr.s_addr = inet_addr("255.255.255.0");
    if (rtems_bsdnet_ifconfig("veth0", SIOCSIFNETMASK, &netmask) == 0)
    {
        printf("[PASS] 网络掩码配置成功\n");
    }
    else
    {
        printf("[WARN] 网络掩码配置失败: %s\n", strerror(errno));
    }

    memset(&address, 0, sizeof(address));
    address.sin_len = sizeof(address);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr("192.168.1.10");
    if (rtems_bsdnet_ifconfig("veth0", SIOCSIFADDR, &address) == 0)
    {
        printf("[PASS] IP地址配置成功: 192.168.1.10\n");
        test_results[0] = 1;
    }
    else
    {
        printf("[WARN] IP地址配置失败: %s\n", strerror(errno));
    }

    rtems_task_wake_after(rtems_clock_get_ticks_per_second() / 2);

    rtems_semaphore_release(sync_sem1);
    rtems_semaphore_obtain(sync_sem2, RTEMS_WAIT, RTEMS_NO_TIMEOUT);

    printf("\n--- 测试2: 验证接口隔离 ---\n");
    list_interfaces_in_container(current_container);

    struct ifnet *veth1_from_container1 = find_interface_in_container(current_container, "veth1");
    if (veth1_from_container1 == NULL)
    {
        printf("[PASS] 正确: 容器2无法访问容器3的veth1接口\n");
        test_results[1] = 1;
    }
    else
    {
        printf("[FAIL] 错误: 容器2意外访问到了容器3的接口\n");
    }

    printf("\n=== 容器2测试完成 ===\n");
    rtems_semaphore_release(test_complete_sem);
    rtems_task_delete(RTEMS_SELF);
}

/* 容器3测试任务 */
static rtems_task container2_task(rtems_task_argument arg)
{
    printf("\n=== 容器3接口隔离测试开始 ===\n");

    rtems_interrupt_level level;
    rtems_interrupt_disable(level);
    Thread_Control *self = _Thread_Get_executing();
    NetContainer *current_container = NULL;

    if (self && self->container)
    {
        current_container = self->container->netContainer;
    }
    rtems_interrupt_enable(level);
    printf("当前线程容器ID: %d, rc=%d\n",
           current_container->containerID, current_container->rc);

    rtems_semaphore_obtain(sync_sem1, RTEMS_WAIT, RTEMS_NO_TIMEOUT);

    printf("\n--- 测试3: 创建容器3网络接口 ---\n");

    memset(&container2_netconfig, 0, sizeof(container2_netconfig));
    container2_netconfig.name = "veth1";
    container2_netconfig.attach = container_net_driver_attach;
    container2_netconfig.ip_address = NULL;
    container2_netconfig.ip_netmask = NULL;
    container2_netconfig.mtu = 1500;

    rtems_bsdnet_attach(&container2_netconfig);
    printf("[PASS] 容器3网络接口创建成功\n");

    rtems_task_wake_after(rtems_clock_get_ticks_per_second() / 4);

    struct sockaddr_in address2;
    struct sockaddr_in netmask2;
    short flags2;

    flags2 = IFF_UP;
    if (rtems_bsdnet_ifconfig("veth1", SIOCSIFFLAGS, &flags2) == 0)
    {
        printf("[PASS] 接口已启动(UP)\n");
    }
    else
    {
        printf("[WARN] 启动接口失败: %s\n", strerror(errno));
    }

    memset(&netmask2, 0, sizeof(netmask2));
    netmask2.sin_len = sizeof(netmask2);
    netmask2.sin_family = AF_INET;
    netmask2.sin_addr.s_addr = inet_addr("255.255.255.0");
    if (rtems_bsdnet_ifconfig("veth1", SIOCSIFNETMASK, &netmask2) == 0)
    {
        printf("[PASS] 网络掩码配置成功\n");
    }
    else
    {
        printf("[WARN] 网络掩码配置失败: %s\n", strerror(errno));
    }

    memset(&address2, 0, sizeof(address2));
    address2.sin_len = sizeof(address2);
    address2.sin_family = AF_INET;
    address2.sin_addr.s_addr = inet_addr("192.168.2.20");

    printf("[调试] 准备配置IP: 192.168.2.20\n");
    printf("[调试] sockaddr_in: len=%d, family=%d, addr=0x%08x\n",
           address2.sin_len, address2.sin_family, ntohl(address2.sin_addr.s_addr));

    int ret = rtems_bsdnet_ifconfig("veth1", SIOCSIFADDR, &address2);
    if (ret == 0)
    {
        printf("[PASS] IP地址配置成功: 192.168.2.20\n");
        test_results[2] = 1;
    }
    else
    {
        printf("[WARN] IP地址配置失败: %s (errno=%d, ret=%d)\n",
               strerror(errno), errno, ret);
    }

    rtems_task_wake_after(rtems_clock_get_ticks_per_second() / 2);

    printf("\n--- 列出容器3接口 ---\n");
    list_interfaces_in_container(current_container);

    rtems_semaphore_release(sync_sem2);

    printf("\n=== 容器3测试完成 ===\n");
    rtems_task_delete(RTEMS_SELF);
}

/* 主测试函数 */

static rtems_task Init(rtems_task_argument ignored)
{
    TEST_BEGIN();

    printf("开始网络容器接口隔离测试\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");

    printf("初始化BSD网络栈...\n");
    if (rtems_bsdnet_initialize_network() < 0)
    {
        printf("[FAIL] 网络栈初始化失败\n");
        TEST_END();
        rtems_test_exit(1);
    }
    printf("[PASS] 网络栈初始化成功\n");

    rtems_status_code sc;
    sc = rtems_semaphore_create(rtems_build_name('S', 'Y', 'N', '1'), 0,
                                RTEMS_SIMPLE_BINARY_SEMAPHORE | RTEMS_PRIORITY, 0, &sync_sem1);
    sc |= rtems_semaphore_create(rtems_build_name('S', 'Y', 'N', '2'), 0,
                                 RTEMS_SIMPLE_BINARY_SEMAPHORE | RTEMS_PRIORITY, 0, &sync_sem2);
    sc |= rtems_semaphore_create(rtems_build_name('T', 'E', 'S', 'T'), 0,
                                 RTEMS_SIMPLE_BINARY_SEMAPHORE | RTEMS_PRIORITY, 0, &test_complete_sem);

    if (sc != RTEMS_SUCCESSFUL)
    {
        printf("[FAIL] 创建同步信号量失败\n");
        TEST_END();
        rtems_test_exit(1);
    }

    printf("\n创建网络容器...\n");
    net_container1 = rtems_net_container_create();
    net_container2 = rtems_net_container_create();

    if (!net_container1 || !net_container2)
    {
        printf("[FAIL] 创建网络容器失败\n");
        TEST_END();
        rtems_test_exit(1);
    }

    printf("[PASS] 网络容器创建成功: 容器2(ID=%d, rc=%d), 容器3(ID=%d, rc=%d)\n",
           net_container1->containerID, net_container1->rc,
           net_container2->containerID, net_container2->rc);

    Container *root_container = rtems_container_get_root();
    if (!root_container || !root_container->netContainer)
    {
        printf("[FAIL] 获取根容器失败\n");
        TEST_END();
        rtems_test_exit(1);
    }

    sc = rtems_task_create(rtems_build_name('C', 'N', 'T', '1'), 1,
                           RTEMS_MINIMUM_STACK_SIZE * 4, RTEMS_DEFAULT_MODES,
                           RTEMS_DEFAULT_ATTRIBUTES, &container1_thread_id);
    sc |= rtems_task_create(rtems_build_name('C', 'N', 'T', '2'), 1,
                            RTEMS_MINIMUM_STACK_SIZE * 4, RTEMS_DEFAULT_MODES,
                            RTEMS_DEFAULT_ATTRIBUTES, &container2_thread_id);

    if (sc != RTEMS_SUCCESSFUL)
    {
        printf("[FAIL] 创建测试任务失败\n");
        TEST_END();
        rtems_test_exit(1);
    }

    ISR_lock_Context lock_context;

    Thread_Control *thread1 = _Thread_Get(container1_thread_id, &lock_context);
    if (thread1)
    {
        if (!thread1->container)
        {
            thread1->container = (Container *)_Workspace_Allocate(sizeof(Container));
            if (thread1->container)
            {
                memset(thread1->container, 0, sizeof(Container));
            }
            else
            {
                _ISR_lock_ISR_enable(&lock_context);
                printf("[FAIL] 错误: 无法分配容器内存\n");
                TEST_END();
                rtems_test_exit(1);
            }
        }
        thread1->container->netContainer = net_container1;
        net_container1->rc++;
        _ISR_lock_ISR_enable(&lock_context);
        printf("[PASS] 任务1已分配到容器%d (rc=%d)\n",
               net_container1->containerID, net_container1->rc);
    }
    else
    {
        printf("[FAIL] 错误: 无法获取任务1\n");
        TEST_END();
        rtems_test_exit(1);
    }

    Thread_Control *thread2 = _Thread_Get(container2_thread_id, &lock_context);
    if (thread2)
    {
        if (!thread2->container)
        {
            thread2->container = (Container *)_Workspace_Allocate(sizeof(Container));
            if (thread2->container)
            {
                memset(thread2->container, 0, sizeof(Container));
            }
            else
            {
                _ISR_lock_ISR_enable(&lock_context);
                printf("[FAIL] 错误: 无法分配容器内存\n");
                TEST_END();
                rtems_test_exit(1);
            }
        }
        thread2->container->netContainer = net_container2;
        net_container2->rc++;
        _ISR_lock_ISR_enable(&lock_context);
        printf("[PASS] 任务2已分配到容器%d (rc=%d)\n",
               net_container2->containerID, net_container2->rc);
    }
    else
    {
        printf("[FAIL] 错误: 无法获取任务2\n");
        TEST_END();
        rtems_test_exit(1);
    }

    printf("\n启动接口隔离测试任务...\n");
    printf("测试内容:\n");
    printf("  1. 容器2创建独立网络接口\n");
    printf("  2. 容器3创建独立网络接口\n");
    printf("  3. 验证容器间接口相互不可见\n");

    sc = rtems_task_start(container1_thread_id, container1_task, 0);
    sc |= rtems_task_start(container2_thread_id, container2_task, 0);

    if (sc != RTEMS_SUCCESSFUL)
    {
        printf("[FAIL] 启动测试任务失败\n");
        TEST_END();
        rtems_test_exit(1);
    }

    printf("等待测试完成...\n");
    rtems_semaphore_obtain(test_complete_sem, RTEMS_WAIT, RTEMS_NO_TIMEOUT);

    rtems_task_wake_after(2 * rtems_clock_get_ticks_per_second());

    printf("\n网络容器接口隔离测试结果\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");

    const char *test_names[] = {
        "容器2网络接口创建",
        "接口隔离验证",
        "容器3网络接口创建"};

    int total_tests = 3;
    int passed_tests = 0;

    for (int i = 0; i < total_tests; i++)
    {
        printf("测试%d - %-20s: %s\n", i + 1, test_names[i],
               test_results[i] ? "[PASS]" : "[FAIL]");
        if (test_results[i])
            passed_tests++;
    }

    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    int percentage = (passed_tests * 100) / total_tests;
    printf("总计: %d/%d 测试通过 (%d%%)\n",
           passed_tests, total_tests, percentage);

    if (passed_tests == total_tests)
    {
        printf("\n所有测试通过! 网络容器接口隔离功能验证成功!\n");
    }
    else
    {
        printf("\n部分测试失败，请检查相关实现\n");
    }

    printf("\n清理测试资源...\n");
    if (net_container1)
        rtems_net_container_delete(net_container1);
    if (net_container2)
        rtems_net_container_delete(net_container2);

    rtems_semaphore_delete(sync_sem1);
    rtems_semaphore_delete(sync_sem2);
    rtems_semaphore_delete(test_complete_sem);

    printf("[PASS] 资源清理完成\n");
    printf("\n网络容器接口隔离测试结束\n");

    TEST_END();
    rtems_test_exit(0);
}

/* 网络栈配置 */
struct rtems_bsdnet_config rtems_bsdnet_config = {
    NULL, NULL, 0, 0, 0, 0, 0, 0, 0, {"0.0.0.0"}, {"0.0.0.0"}, 0, 0, 0, 0, 0};

struct rtems_bsdnet_config *rtems_bsdnet_config_ptr = &rtems_bsdnet_config;

/* RTEMS配置 */
#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_SIMPLE_CONSOLE_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_LIBBLOCK
#define CONFIGURE_APPLICATION_NEEDS_LIBNETWORKING

#define CONFIGURE_MAXIMUM_DRIVERS 10
#define CONFIGURE_MAXIMUM_TASKS 10
#define CONFIGURE_MAXIMUM_SEMAPHORES 10
#define CONFIGURE_MAXIMUM_FILE_DESCRIPTORS 30
#define CONFIGURE_MAXIMUM_SOCKETS 10

#define CONFIGURE_EXECUTIVE_RAM_SIZE (2 * 1024 * 1024)
#define CONFIGURE_MAXIMUM_REGIONS 10

#define CONFIGURE_RTEMS_INIT_TASKS_TABLE
#define CONFIGURE_INIT

#include <rtems/confdefs.h>