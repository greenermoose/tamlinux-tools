/* Fixed build paths: run a build in its own user and mount namespace, with
 * its work tree and dependencies mounted at paths that never change, so a
 * build directory recorded in a program (debug information, a pkg-config
 * file) is the same wherever the build cache is.
 *
 * Inside, /tmp is a new, small tmpfs holding only the mount points under
 * TB_BUILD_ROOT; the caller's own /tmp is out of sight. Nothing needs root:
 * the namespace maps the caller's own user and group to themselves. */
#define _GNU_SOURCE /* unshare */
#include "tb.h"
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

static int write_file(const char *path, const char *text) {
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    ssize_t n = write(fd, text, strlen(text));
    int saved = errno;
    close(fd);
    errno = saved;
    return n == (ssize_t)strlen(text) ? 0 : -1;
}

static int mkdirs(const char *path) {
    char buf[4096];
    snprintf(buf, sizeof buf, "%s", path);
    for (char *p = buf + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(buf, 0755) != 0 && errno != EEXIST) return -1;
        *p = '/';
    }
    return mkdir(buf, 0755) != 0 && errno != EEXIST ? -1 : 0;
}

int tb_isolate(const tb_bind_t *binds, size_t n, const char **why) {
    uid_t uid = getuid();
    gid_t gid = getgid();
    char map[64];
    *why = "unshare(CLONE_NEWUSER | CLONE_NEWNS) (are unprivileged user namespaces turned off?)";
    if (unshare(CLONE_NEWUSER | CLONE_NEWNS) != 0) return -1;
    *why = "writing the user and group maps";
    if (write_file("/proc/self/setgroups", "deny") != 0) return -1;
    snprintf(map, sizeof map, "%u %u 1\n", (unsigned)uid, (unsigned)uid);
    if (write_file("/proc/self/uid_map", map) != 0) return -1;
    snprintf(map, sizeof map, "%u %u 1\n", (unsigned)gid, (unsigned)gid);
    if (write_file("/proc/self/gid_map", map) != 0) return -1;
    *why = "making the mounts private";
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) != 0) return -1;

    /* Take copies of every source while the caller's paths are still in
     * view; a source may itself be under /tmp. */
    int fds[TB_MAX_BINDS];
    if (n > TB_MAX_BINDS) {
        *why = "too many mounts";
        errno = E2BIG;
        return -1;
    }
    for (size_t i = 0; i < n; i++) {
        *why = "open_tree on a build directory";
        fds[i] = open_tree(AT_FDCWD, binds[i].from, OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC | AT_RECURSIVE);
        if (fds[i] < 0) return -1;
        if (binds[i].read_only) {
            struct mount_attr attr = {.attr_set = MOUNT_ATTR_RDONLY};
            *why = "mount_setattr read-only";
            if (mount_setattr(fds[i], "", AT_EMPTY_PATH | AT_RECURSIVE, &attr, sizeof attr) != 0) return -1;
        }
    }
    *why = "mounting a tmpfs on /tmp";
    if (mount("tmpfs", "/tmp", "tmpfs", MS_NOSUID | MS_NODEV, "mode=1777,size=64m") != 0) return -1;
    for (size_t i = 0; i < n; i++) {
        *why = "placing a build directory at its fixed path";
        if (mkdirs(binds[i].to) != 0) return -1;
        if (move_mount(fds[i], "", AT_FDCWD, binds[i].to, MOVE_MOUNT_F_EMPTY_PATH) != 0) return -1;
        close(fds[i]);
    }
    *why = NULL;
    return 0;
}
