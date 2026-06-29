#include <rtems.h>
#include <tmacros.h>
#include <rtems/score/containerfs.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

const char rtems_test_name[] = "MEM Cgroup FS Control Test";

static void write_cmd(const char *path, const char *cmd)
{
  int fd = open(path, O_WRONLY);
  if (fd < 0) {
    printf("open %s failed: errno=%d\n", path, errno);
    return;
  }

  if (write(fd, cmd, strlen(cmd)) < 0) {
    printf("write %s failed: errno=%d\n", path, errno);
  }

  close(fd);
}

static rtems_task Init(rtems_task_argument arg)
{
  (void) arg;
  rtems_print_printer_fprintf_putc(&rtems_test_printer);
  TEST_BEGIN();

#ifdef RTEMS_CGROUP
  rtems_containerfs_register_memctl();
  write_cmd("/memctl", "list\n");
  write_cmd("/memctl", "create 2097152\n");
  write_cmd("/memctl", "create 4194304\n");
  write_cmd("/memctl", "list\n");
  write_cmd("/memctl", "set 1 8388608\n");
  write_cmd("/memctl", "list\n");
#else
  printf("MEM cgroup not enabled\n");
#endif

  TEST_END();
  rtems_test_exit(0);
}

#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_SIMPLE_CONSOLE_DRIVER
#define CONFIGURE_USE_IMFS_AS_BASE_FILESYSTEM
#define CONFIGURE_MAXIMUM_FILE_DESCRIPTORS 8
#define CONFIGURE_MAXIMUM_TASKS 8
#define CONFIGURE_RTEMS_INIT_TASKS_TABLE
#define CONFIGURE_INIT_TASK_ATTRIBUTES RTEMS_FLOATING_POINT
#define CONFIGURE_INITIAL_EXTENSIONS RTEMS_TEST_INITIAL_EXTENSION
#define CONFIGURE_INIT
#include <rtems/confdefs.h>
