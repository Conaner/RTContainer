#include <rtems.h>
#include <tmacros.h>
#include <rtems/score/containerfs.h>
#include <rtems/score/container.h>
#ifdef RTEMSCFG_PID_CONTAINER
#include <rtems/score/pidContainer.h>
#endif
#ifdef RTEMSCFG_UTS_CONTAINER
#include <rtems/score/utsContainer.h>
#endif
#ifdef RTEMSCFG_MNT_CONTAINER
#include <rtems/score/mntContainer.h>
#endif
#ifdef RTEMSCFG_NET_CONTAINER
#include <rtems/score/netContainer.h>
#endif
#ifdef RTEMSCFG_IPC_CONTAINER
#include <rtems/score/ipcContainer.h>
#endif
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

const char rtems_test_name[] = "CONTAINER FS ALL CONTROL TEST";

/*
 * 测试步骤:
 * 步骤 1: 测试 PID 容器控制文件 /pidctl
 * 步骤 2: 测试 UTS 容器控制文件 /utsctl
 * 步骤 3: 测试 MNT 容器控制文件 /mntctl
 * 步骤 4: 测试 NET 容器控制文件 /netctl
 * 步骤 5: 测试 IPC 容器控制文件 /ipcctl
 * 步骤 6: 测试 CPU cgroup 控制文件 /cpuctl
 * 步骤 7: 测试 MEM cgroup 控制文件 /memctl
 * 步骤 8: 测试 IO cgroup 控制文件 /ioctl
 */

static void write_cmd(const char *path, const char *cmd)
{
  int fd = open(path, O_WRONLY);
  if (fd < 0) {
    printf("open %s failed: errno=%d\n", path, errno);
    return;
  }

  ssize_t n = write(fd, cmd, strlen(cmd));
  if (n < 0) {
    printf("write %s failed: errno=%d\n", path, errno);
  }

  close(fd);
}

static void test_pidctl(void)
{
  printf("\n===== 步骤 1: PID 容器控制文件测试 /pidctl =====\n");

#ifdef RTEMSCFG_PID_CONTAINER
  rtems_containerfs_register_pidctl();

  /* 确保根容器存在 */
  Container *root = rtems_container_get_root();
  if (!root || !root->pidContainer) {
    PidContainer *tmp = NULL;
    int rc = rtems_pid_container_initialize_root(&tmp);
    printf("init root pid container rc=%d\n", rc);
    (void)tmp;
  }

  printf("--- list 当前 PID 容器 ---\n");
  write_cmd("/pidctl", "list\n");

  printf("--- create 创建 PID 容器 ---\n");
  write_cmd("/pidctl", "create\n");

  printf("--- list 查看创建后的 PID 容器 ---\n");
  write_cmd("/pidctl", "list\n");

  printf("--- create 再次创建 PID 容器 ---\n");
  write_cmd("/pidctl", "create\n");

  printf("--- list 最终 PID 容器列表 ---\n");
  write_cmd("/pidctl", "list\n");
#else
  printf("PID Container not enabled\n");
#endif
}

static void test_utsctl(void)
{
  printf("\n===== 步骤 2: UTS 容器控制文件测试 /utsctl =====\n");

#ifdef RTEMSCFG_UTS_CONTAINER
  rtems_containerfs_register_utsctl();

  /* 确保根容器存在 */
  Container *root = rtems_container_get_root();
  if (!root || !root->utsContainer) {
    UtsContainer *tmp = NULL;
    int rc = rtems_uts_container_initialize_root(&tmp);
    printf("init root uts container rc=%d\n", rc);
    (void)tmp;
  }

  printf("--- list 当前 UTS 容器 ---\n");
  write_cmd("/utsctl", "list\n");

  printf("--- create 创建 UTS 容器 (hostname: myhost) ---\n");
  write_cmd("/utsctl", "create myhost\n");

  printf("--- list 查看创建后的 UTS 容器 ---\n");
  write_cmd("/utsctl", "list\n");

  printf("--- create 再次创建 UTS 容器 (hostname: testhost) ---\n");
  write_cmd("/utsctl", "create testhost\n");

  printf("--- list 最终 UTS 容器列表 ---\n");
  write_cmd("/utsctl", "list\n");
#else
  printf("UTS Container not enabled\n");
#endif
}

static void test_mntctl(void)
{
  printf("\n===== 步骤 3: MNT 容器控制文件测试 /mntctl =====\n");

#ifdef RTEMSCFG_MNT_CONTAINER
  rtems_containerfs_register_mntctl();

  /* 确保根容器存在 */
  Container *root = rtems_container_get_root();
  if (!root || !root->mntContainer) {
    MntContainer *tmp = NULL;
    int rc = rtems_mnt_container_initialize_root(&tmp);
    printf("init root mnt container rc=%d\n", rc);
    (void)tmp;
  }

  printf("--- list 当前 MNT 容器 ---\n");
  write_cmd("/mntctl", "list\n");

  printf("--- create 创建 MNT 容器 ---\n");
  write_cmd("/mntctl", "create\n");

  printf("--- list 查看创建后的 MNT 容器 ---\n");
  write_cmd("/mntctl", "list\n");

  printf("--- create_inherit 继承创建 MNT 容器 (从容器 1) ---\n");
  write_cmd("/mntctl", "create_inherit 1\n");

  printf("--- list 最终 MNT 容器列表 ---\n");
  write_cmd("/mntctl", "list\n");
#else
  printf("MNT Container not enabled\n");
#endif
}

static void test_netctl(void)
{
  printf("\n===== 步骤 4: NET 容器控制文件测试 /netctl =====\n");

#ifdef RTEMSCFG_NET_CONTAINER
  rtems_containerfs_register_netctl();

  /* 确保根容器存在 */
  Container *root = rtems_container_get_root();
  if (!root || !root->netContainer) {
    NetContainer *tmp = NULL;
    int rc = rtems_net_container_initialize_root(&tmp);
    printf("init root net container rc=%d\n", rc);
    (void)tmp;
  }

  printf("--- list 当前 NET 容器 ---\n");
  write_cmd("/netctl", "list\n");

  printf("--- create 创建 NET 容器 ---\n");
  write_cmd("/netctl", "create\n");

  printf("--- list 查看创建后的 NET 容器 ---\n");
  write_cmd("/netctl", "list\n");

  printf("--- create 再次创建 NET 容器 ---\n");
  write_cmd("/netctl", "create\n");

  printf("--- list 最终 NET 容器列表 ---\n");
  write_cmd("/netctl", "list\n");
#else
  printf("NET Container not enabled\n");
#endif
}

static void test_ipcctl(void)
{
  printf("\n===== 步骤 5: IPC 容器控制文件测试 /ipcctl =====\n");

#ifdef RTEMSCFG_IPC_CONTAINER
  rtems_containerfs_register_ipcctl();

  /* 确保根容器存在 */
  Container *root = rtems_container_get_root();
  if (!root || !root->ipcContainer) {
    IpcContainer *tmp = NULL;
    int rc = rtems_ipc_container_initialize_root(&tmp);
    printf("init root ipc container rc=%d\n", rc);
    (void)tmp;
  }

  printf("--- list 当前 IPC 容器 ---\n");
  write_cmd("/ipcctl", "list\n");

  printf("--- create 创建 IPC 容器 ---\n");
  write_cmd("/ipcctl", "create\n");

  printf("--- list 查看创建后的 IPC 容器 ---\n");
  write_cmd("/ipcctl", "list\n");

  printf("--- create 再次创建 IPC 容器 ---\n");
  write_cmd("/ipcctl", "create\n");

  printf("--- list 最终 IPC 容器列表 ---\n");
  write_cmd("/ipcctl", "list\n");
#else
  printf("IPC Container not enabled\n");
#endif
}

static void test_cpuctl(void)
{
  printf("\n===== 步骤 6: CPU cgroup 控制文件测试 /cpuctl =====\n");

#ifdef RTEMS_CGROUP
  rtems_containerfs_register_cpuctl();

  printf("--- list 当前 CPU cgroup ---\n");
  write_cmd("/cpuctl", "list\n");

  printf("--- create 创建 CPU cgroup (quota=200, period=1000, max_threads=2) ---\n");
  write_cmd("/cpuctl", "create 200 1000 2\n");

  printf("--- create 创建 CPU cgroup (quota=300, period=1000, max_threads=3) ---\n");
  write_cmd("/cpuctl", "create 300 1000 3\n");

  printf("--- list 查看创建后的 CPU cgroup ---\n");
  write_cmd("/cpuctl", "list\n");

  printf("--- set 修改 cgroup 1 (quota=400, period=1000) ---\n");
  write_cmd("/cpuctl", "set 1 400 1000\n");

  printf("--- list 最终 CPU cgroup 列表 ---\n");
  write_cmd("/cpuctl", "list\n");
#else
  printf("CPU cgroup not enabled\n");
#endif
}

static void test_memctl(void)
{
  printf("\n===== 步骤 7: MEM cgroup 控制文件测试 /memctl =====\n");

#ifdef RTEMS_CGROUP
  rtems_containerfs_register_memctl();

  printf("--- list 当前 MEM cgroup ---\n");
  write_cmd("/memctl", "list\n");

  printf("--- create 创建 MEM cgroup (limit=2097152 bytes=2MB) ---\n");
  write_cmd("/memctl", "create 2097152\n");

  printf("--- create 创建 MEM cgroup (limit=4194304 bytes=4MB) ---\n");
  write_cmd("/memctl", "create 4194304\n");

  printf("--- list 查看创建后的 MEM cgroup ---\n");
  write_cmd("/memctl", "list\n");

  printf("--- set 修改 cgroup 1 (limit=8388608 bytes=8MB) ---\n");
  write_cmd("/memctl", "set 1 8388608\n");

  printf("--- list 最终 MEM cgroup 列表 ---\n");
  write_cmd("/memctl", "list\n");
#else
  printf("MEM cgroup not enabled\n");
#endif
}

static void test_ioctl(void)
{
  printf("\n===== 步骤 8: IO cgroup 控制文件测试 /ioctl =====\n");

#ifdef RTEMSCFG_IO_CGROUP
  rtems_containerfs_register_ioctl();

  printf("--- list 当前 IO cgroup ---\n");
  write_cmd("/ioctl", "list\n");

  printf("--- create 创建 IO cgroup (weight=70) ---\n");
  write_cmd("/ioctl", "create 70\n");

  printf("--- create 创建 IO cgroup (weight=30) ---\n");
  write_cmd("/ioctl", "create 30\n");

  printf("--- list 查看创建后的 IO cgroup ---\n");
  write_cmd("/ioctl", "list\n");

  printf("--- set 修改 cgroup 1 (read_bps=524288, write_bps=262144, window_ms=1000) ---\n");
  write_cmd("/ioctl", "set 1 524288 262144 1000\n");

  printf("--- list 最终 IO cgroup 列表 ---\n");
  write_cmd("/ioctl", "list\n");
#else
  printf("IO cgroup not enabled\n");
#endif
}

static rtems_task Init(rtems_task_argument arg)
{
  (void) arg;
  rtems_print_printer_fprintf_putc(&rtems_test_printer);
  TEST_BEGIN();

  printf("========== ContainerFS 综合控制文件测试 ==========\n");

  /* 步骤 1: PID 容器控制 */
  test_pidctl();

  /* 步骤 2: UTS 容器控制 */
  test_utsctl();

  /* 步骤 3: MNT 容器控制 */
  test_mntctl();

  /* 步骤 4: NET 容器控制 */
  test_netctl();

  /* 步骤 5: IPC 容器控制 */
  test_ipcctl();

  /* 步骤 6: CPU cgroup 控制 */
  test_cpuctl();

  /* 步骤 7: MEM cgroup 控制 */
  test_memctl();

  /* 步骤 8: IO cgroup 控制 */
  test_ioctl();

  printf("\n========== ContainerFS 综合控制文件测试完成 ==========\n");

  TEST_END();
  rtems_test_exit(0);
}

#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_SIMPLE_CONSOLE_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_LIBBLOCK

#define CONFIGURE_USE_IMFS_AS_BASE_FILESYSTEM
#define CONFIGURE_MAXIMUM_FILE_DESCRIPTORS 20
#define CONFIGURE_MAXIMUM_TASKS 12
#define CONFIGURE_MAXIMUM_TASK_VARIABLES 10
#define CONFIGURE_MAXIMUM_SEMAPHORES 25
#define CONFIGURE_MAXIMUM_CGROUPS 10
#define CONFIGURE_MAXIMUM_POSIX_MESSAGE_QUEUES 15
#define CONFIGURE_MAXIMUM_POSIX_MESSAGE_QUEUE_DESCRIPTORS 30
#define CONFIGURE_RTEMS_INIT_TASKS_TABLE
#define CONFIGURE_INIT_TASK_ATTRIBUTES RTEMS_FLOATING_POINT
#define CONFIGURE_INIT_TASK_STACK_SIZE (RTEMS_MINIMUM_STACK_SIZE * 4)
#define CONFIGURE_INITIAL_EXTENSIONS RTEMS_TEST_INITIAL_EXTENSION
#define CONFIGURE_INIT
#include <rtems/confdefs.h>
