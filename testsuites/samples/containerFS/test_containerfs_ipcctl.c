#include <rtems.h>
#include <tmacros.h>
#include <rtems/score/containerfs.h>
#include <rtems/score/container.h>
#ifdef RTEMSCFG_IPC_CONTAINER
#include <rtems/score/ipcContainer.h>
#endif
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

const char rtems_test_name[] = "IPC Container FS Control Test";

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

static rtems_task Init(rtems_task_argument arg)
{
    (void)arg;
    rtems_print_printer_fprintf_putc(&rtems_test_printer);
    TEST_BEGIN();

#ifdef RTEMSCFG_IPC_CONTAINER
    /* Register control file */
    rtems_containerfs_register_ipcctl();

    /* Ensure root container exists */
    Container *root = rtems_container_get_root();
    if (!root || !root->ipcContainer) {
        IpcContainer *tmp = NULL;
        int rc = rtems_ipc_container_initialize_root(&tmp);
        printf("init root ipc container rc=%d\n", rc);
        (void)tmp;
    }

    /* List current */
    write_cmd("/ipcctl", "list\n");

    /* Create one container */
    write_cmd("/ipcctl", "create\n");

    /* List again */
    write_cmd("/ipcctl", "list\n");

    /* Create again */
    write_cmd("/ipcctl", "create\n");

    /* Final list */
    write_cmd("/ipcctl", "list\n");
#else
    printf("IPC Container not enabled\n");
#endif

    TEST_END();
    rtems_test_exit(0);

}


#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_SIMPLE_CONSOLE_DRIVER

#define CONFIGURE_USE_IMFS_AS_BASE_FILESYSTEM
#define CONFIGURE_MAXIMUM_FILE_DESCRIPTORS 8
#define CONFIGURE_MAXIMUM_TASKS 12
#define CONFIGURE_MAXIMUM_TASK_VARIABLES 10
#define CONFIGURE_MAXIMUM_SEMAPHORES 25
#define CONFIGURE_MAXIMUM_POSIX_MESSAGE_QUEUES 15
#define CONFIGURE_MAXIMUM_POSIX_MESSAGE_QUEUE_DESCRIPTORS 30
#define CONFIGURE_RTEMS_INIT_TASKS_TABLE
#define CONFIGURE_INIT_TASK_ATTRIBUTES RTEMS_FLOATING_POINT
#define CONFIGURE_INIT_TASK_STACK_SIZE (RTEMS_MINIMUM_STACK_SIZE * 4)
#define CONFIGURE_INITIAL_EXTENSIONS RTEMS_TEST_INITIAL_EXTENSION
#define CONFIGURE_INIT
#include <rtems/confdefs.h>


