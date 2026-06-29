#include <rtems.h>
#include <tmacros.h>
#include <rtems/score/containerfs.h>
#include <rtems/score/container.h>
#ifdef RTEMSCFG_UTS_CONTAINER
#include <rtems/score/utsContainer.h>
#endif
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

const char rtems_test_name[] = "UTS Container FS Control Test";

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

#ifdef RTEMSCFG_UTS_CONTAINER
    /* Register control file */
    rtems_containerfs_register_utsctl();

    /* Ensure root container exists */
    Container *root = rtems_container_get_root();
    if (!root || !root->utsContainer) {
        UtsContainer *tmp = NULL;
        int rc = rtems_uts_container_initialize_root(&tmp);
        printf("init root uts container rc=%d\n", rc);
        (void)tmp;
    }

    /* List current */
    write_cmd("/utsctl", "list\n");

    /* Create one container */
    write_cmd("/utsctl", "create myhost\n");

    /* List again */
    write_cmd("/utsctl", "list\n");

    /* Create again */
    write_cmd("/utsctl", "create testhost\n");

    /* Final list */
    write_cmd("/utsctl", "list\n");
#else
    printf("UTS Container not enabled\n");
#endif

    TEST_END();
    rtems_test_exit(0);

}


#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_SIMPLE_CONSOLE_DRIVER

#define CONFIGURE_USE_IMFS_AS_BASE_FILESYSTEM
#define CONFIGURE_MAXIMUM_FILE_DESCRIPTORS 8
#define CONFIGURE_MAXIMUM_TASKS 5
#define CONFIGURE_RTEMS_INIT_TASKS_TABLE
#define CONFIGURE_INIT_TASK_ATTRIBUTES RTEMS_FLOATING_POINT
#define CONFIGURE_INITIAL_EXTENSIONS RTEMS_TEST_INITIAL_EXTENSION
#define CONFIGURE_INIT
#include <rtems/confdefs.h>


