/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise real syscalls as an authorized shell, or an explicitly denied UID. */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <linux/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

static const char su_path[] = "/system/bin/su";
static int failures;

static void check_lookup(const char *name, long result, int allowed)
{
    int error = result < 0 ? errno : 0;
    printf("%s result=%ld errno=%d\n", name, result, error);
    if (allowed ? result != 0 : (result != -1 || error != ENOENT))
        failures++;
}

int main(int argc, char **argv)
{
    if (argc != 2 && argc != 3) {
        fprintf(stderr, "usage: %s allow | deny <uid>\n", argv[0]);
        return 2;
    }
    int allowed = !strcmp(argv[1], "allow");
    if ((allowed && argc != 2) || (!allowed && (strcmp(argv[1], "deny") || argc != 3)))
        return 2;
    if (!allowed) {
        char *end;
        unsigned long uid = strtoul(argv[2], &end, 10);
        if (*end || uid < 10000 || uid > 2147483647UL)
            return 2;
        if (setgroups(0, NULL) || setgid((gid_t)uid) || setuid((uid_t)uid)) {
            perror("set identity");
            return 2;
        }
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("uid=%u allowed=%d\n", getuid(), allowed);
    struct stat st;
    struct statx stx;
    check_lookup("faccessat", syscall(__NR_faccessat, AT_FDCWD, su_path, F_OK), allowed);
    check_lookup("faccessat2", syscall(__NR_faccessat2, AT_FDCWD, su_path, F_OK, AT_EACCESS), allowed);
    check_lookup("newfstatat", syscall(__NR_newfstatat, AT_FDCWD, su_path, &st, 0), allowed);
    check_lookup("statx", syscall(__NR_statx, AT_FDCWD, su_path, 0, STATX_BASIC_STATS, &stx), allowed);

    int fd = open(su_path, O_RDONLY | O_CLOEXEC);
    int error = fd < 0 ? errno : 0;
    printf("open real su result=%d errno=%d\n", fd, error);
    if (fd >= 0 || error != ENOENT)
        failures++;
    if (fd >= 0)
        close(fd);

    DIR *dir = opendir("/system/bin");
    if (!dir) {
        error = errno;
        if (allowed || (error != EACCES && error != EPERM)) {
            perror("opendir");
            return 2;
        }
        /* This firmware restricts directory listing for ordinary app UIDs.
         * Keep testing path lookups and exec without relaxing permissions. */
        printf("su directory listing unavailable errno=%d\n", error);
    } else {
        struct dirent *entry;
        int found = 0;
        while ((entry = readdir(dir)))
            found |= !strcmp(entry->d_name, "su");
        closedir(dir);
        printf("su directory entry=%d\n", found);
        failures += found;
    }

    FILE *mounts = fopen("/proc/self/mountinfo", "re");
    if (!mounts) {
        perror("mountinfo");
        return 2;
    }
    char line[8192], mountpoint[4096];
    int mounted = 0;
    while (fgets(line, sizeof(line), mounts)) {
        if (sscanf(line, "%*s %*s %*s %*s %4095s", mountpoint) == 1)
            mounted |= !strcmp(mountpoint, "/system/bin");
    }
    fclose(mounts);
    printf("system/bin mount=%d\n", mounted);
    failures += mounted;

    pid_t child = fork();
    if (child == 0) {
        /* An unexpected unauthorized exec must not look like a passing test. */
        execl(su_path, "su", "-c", allowed ? "id" : "exit 42", (char *)NULL);
        error = errno;
        printf("execve su errno=%d\n", error);
        _exit(!allowed && error == ENOENT ? 0 : 1);
    }
    int status;
    if (child < 0 || waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status))
        failures++;
    printf("failures=%d\n", failures);
    return failures ? 1 : 0;
}
