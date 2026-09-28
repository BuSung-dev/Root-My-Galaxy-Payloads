/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * On-device regression probe. Run from an existing root shell with arguments
 * <uid> <expected-hide:0|1> [app-context]. Drops UID/GID and, by default,
 * retains the caller's domain to test UID filtering independently of app
 * permissions. An optional context tests queries in a real app domain.
 * This does not grant root or change global SELinux policy/enforcement.
 */
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures;

static void transaction(const char *path, const char *request, int invalid)
{
    char response[4096] = {0};
    int fd = open(path, O_RDWR | O_CLOEXEC);
    ssize_t result;
    int saved;

    if (fd < 0) {
        perror(path);
        failures++;
        return;
    }
    result = write(fd, request, strlen(request));
    saved = errno;
    if (invalid ? (result != -1 || saved != EINVAL) : result != (ssize_t)strlen(request))
        failures++;
    if (result >= 0) {
        ssize_t size = read(fd, response, sizeof(response) - 1);
        if (size < 0) {
            perror("transaction read");
            failures++;
        }
    }
    printf("%s request=%s result=%zd errno=%d response=%s\n",
           path, request, result, result < 0 ? saved : 0, response);
    close(fd);
}

static void status_page(void)
{
    uint32_t status[5];
    int fd = open("/sys/fs/selinux/status", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        perror("status open");
        failures++;
        return;
    }
    if (read(fd, status, sizeof(status)) != sizeof(status)) {
        perror("status read");
        failures++;
        close(fd);
        return;
    }
    void *mapping = mmap(NULL, (size_t)sysconf(_SC_PAGESIZE), PROT_READ, MAP_SHARED, fd, 0);
    if (mapping == MAP_FAILED) {
        perror("status mmap");
        failures++;
    } else {
        if (memcmp(status, mapping, sizeof(status)) != 0)
            failures++;
        munmap(mapping, (size_t)sysconf(_SC_PAGESIZE));
    }
    if (status[2] != 1)
        failures++;
    printf("status version=%u sequence=%u enforcing=%u policyload=%u deny_unknown=%u\n",
           status[0], status[1], status[2], status[3], status[4]);
    close(fd);
}

static void procattr(const char *context, int hidden)
{
    /* Use a child: a successful write must not change the probe parent's SID. */
    pid_t child = fork();
    if (child == 0) {
        int fd = open("/proc/self/attr/current", O_WRONLY | O_CLOEXEC);
        ssize_t result = fd < 0 ? -1 : write(fd, context, strlen(context));
        int saved = errno;
        int failed = hidden ? (result != -1 || saved != EINVAL) : result != (ssize_t)strlen(context);
        printf("procattr context=%s result=%zd errno=%d\n", context, result, result < 0 ? saved : 0);
        if (fd >= 0)
            close(fd);
        fflush(stdout);
        _exit(failed);
    }
    int status;
    if (child < 0 || waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status))
        failures++;
}

int main(int argc, char **argv)
{
    if ((argc != 3 && argc != 4) || (strcmp(argv[2], "0") && strcmp(argv[2], "1"))) {
        fprintf(stderr, "usage: %s <uid> <expected-hide:0|1> [app-context]\n", argv[0]);
        return 2;
    }
    char *end;
    unsigned long uid = strtoul(argv[1], &end, 10);
    if (*end || uid > 2147483647UL)
        return 2;
    int hidden = atoi(argv[2]) && uid >= 10000;
    setvbuf(stdout, NULL, _IONBF, 0);
    if (setgroups(0, NULL) || setgid((gid_t)uid) || setuid((uid_t)uid)) {
        perror("drop uid/gid");
        return 2;
    }
    if (argc == 4) {
        int fd = open("/proc/self/attr/current", O_WRONLY | O_CLOEXEC);
        if (fd < 0 || write(fd, argv[3], strlen(argv[3])) != (ssize_t)strlen(argv[3])) {
            perror("enter app context");
            return 2;
        }
        close(fd);
        printf("app_context=%s\n", argv[3]);
    }
    printf("uid=%u expected_hidden=%d\n", getuid(), hidden);
    transaction("/sys/fs/selinux/context", "u:r:shell:s0", 0);
    transaction("/sys/fs/selinux/context", "u:r:ksu:s0", hidden);
    transaction("/sys/fs/selinux/context", "u:object_r:ksu_file:s0", hidden);
    transaction("/sys/fs/selinux/context", "u:r:ksu_nonexistent_test_type:s0", 1);
    transaction("/sys/fs/selinux/access", "u:r:shell:s0 u:r:shell:s0 2", 0);
    transaction("/sys/fs/selinux/access", "u:r:shell:s0 u:r:ksu:s0 2", hidden);
    transaction("/sys/fs/selinux/access", "malformed", 1);
    status_page();
    if (argc == 3) {
        procattr("u:r:ksu:s0", hidden);
        /* A valid stock context exercises the call-through recursion guard. */
        procattr("u:r:shell:s0", 0);
    }
    printf("failures=%d\n", failures);
    return failures ? 1 : 0;
}
