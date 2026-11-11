/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Fred Horch.
 * Plain text daily journals. Micro edits durable drafts; commits are atomic.
 */
#include <tam.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define VERSION "0.1.0"
#define LIMIT (16U * 1024U * 1024U)

static char *join(const char *a, const char *b) {
    tam_str_t s = tam_str_new(a);
    if (s) s = tam_str_cat(s, "/");
    if (s) s = tam_str_cat(s, b);
    if (!s) return NULL;
    char *result = strdup(s);
    tam_str_free(s);
    return result;
}

static int private_dir(const char *path) {
    if (!tam_fs_mkdir_p(path, 0700)) return -1;
    int fd = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    struct stat st;
    if (fd < 0) return -1;
    int ok = fstat(fd, &st) == 0 && st.st_uid == geteuid() && !(st.st_mode & 077);
    if (!ok) { close(fd); errno = EACCES; return -1; }
    return fd;
}

static int valid_date(const char *s) {
    if (strlen(s) != 10 || s[4] != '-' || s[7] != '-') return 0;
    for (int i = 0; i < 10; ++i)
        if (i != 4 && i != 7 && (s[i] < '0' || s[i] > '9')) return 0;
    int y = (s[0]-'0')*1000+(s[1]-'0')*100+(s[2]-'0')*10+s[3]-'0';
    int m = (s[5]-'0')*10+s[6]-'0', d = (s[8]-'0')*10+s[9]-'0';
    const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (y < 1 || m < 1 || m > 12 || d < 1) return 0;
    return d <= days[m-1] + (m == 2 && y%4 == 0 && (y%100 != 0 || y%400 == 0));
}

static int utf8(const unsigned char *s, size_t n) {
    for (size_t i = 0; i < n;) {
        unsigned c = s[i++];
        if (!c) return 0;
        if (c < 128) continue;
        unsigned count, lo = 0x80, hi = 0xbf;
        if (c >= 0xc2 && c <= 0xdf) count = 1;
        else if (c >= 0xe0 && c <= 0xef) {
            count = 2; if (c == 0xe0) lo = 0xa0; if (c == 0xed) hi = 0x9f;
        } else if (c >= 0xf0 && c <= 0xf4) {
            count = 3; if (c == 0xf0) lo = 0x90; if (c == 0xf4) hi = 0x8f;
        } else return 0;
        if (n-i < count || s[i] < lo || s[i] > hi) return 0;
        ++i;
        while (--count) { if (s[i] < 0x80 || s[i] > 0xbf) return 0; ++i; }
    }
    return 1;
}

static char *read_text(const char *path, size_t *length, int missing_ok) {
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        if (errno == ENOENT && missing_ok) { *length = 0; return strdup(""); }
        return NULL;
    }
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
        st.st_size < 0 || st.st_size > LIMIT) {
        close(fd); errno = EINVAL; return NULL;
    }
    size_t n = (size_t)st.st_size, done = 0;
    char *text = malloc(n+1);
    if (!text) { close(fd); return NULL; }
    while (done < n) {
        ssize_t got = read(fd, text+done, n-done);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) { free(text); close(fd); errno = EIO; return NULL; }
        done += (size_t)got;
    }
    char extra;
    ssize_t more = read(fd, &extra, 1);
    close(fd);
    if (more != 0 || !utf8((unsigned char *)text, n)) {
        free(text); errno = EINVAL; return NULL;
    }
    text[n] = 0; *length = n; return text;
}

static int lock_day(const char *root, const char *day) {
    char name[32]; snprintf(name, sizeof name, ".%s.lock", day);
    char *path = join(root, name);
    if (!path) return -1;
    int fd = open(path, O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600);
    free(path);
    if (fd < 0) return -1;
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
        st.st_nlink != 1 || fchmod(fd, 0600) || flock(fd, LOCK_EX | LOCK_NB)) {
        int saved = errno; close(fd); errno = saved; return -1;
    }
    return fd;
}

static int editor(const char *path) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (!pid) { execlp("micro", "micro", path, (char *)NULL); _exit(127); }
    int result;
    while (waitpid(pid, &result, 0) < 0) if (errno != EINTR) return -1;
    return WIFEXITED(result) ? WEXITSTATUS(result) : -1;
}

static int blank(const char *s, size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (s[i] != ' ' && s[i] != '\t' && s[i] != '\r' && s[i] != '\n') return 0;
    return 1;
}

static void usage(FILE *out) {
    fputs("Usage: tam-journal [add] [--date YYYY-MM-DD]\n"
          "Open a daily text journal in Micro, or capture a blank note to append.\n"
          "Files: $TAM_JOURNAL_DIR/YYYY-MM-DD.txt (default: $HOME/Journal).\n", out);
}

int main(int argc, char **argv) {
    int add = 0, have_date = 0;
    char day[11], stamp[40];
    time_t started = time(NULL); struct tm local;
    if (!localtime_r(&started, &local) || !strftime(day, sizeof day, "%Y-%m-%d", &local) ||
        !strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%S%z", &local)) return 1;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--help") && argc == 2) { usage(stdout); return 0; }
        if (!strcmp(argv[i], "--version") && argc == 2) { puts("tam-journal " VERSION); return 0; }
        if (!strcmp(argv[i], "add") && !add) add = 1;
        else if (!strcmp(argv[i], "--date") && !have_date && i+1 < argc && valid_date(argv[i+1])) {
            memcpy(day, argv[++i], sizeof day); have_date = 1;
        } else { usage(stderr); return 2; }
    }
    if (tam_bundle_guard(NULL)) {
        fputs("tam-journal: Tamlinux is switching versions; finish recovery first.\n", stderr); return 1;
    }
    umask(077);
    const char *configured = getenv("TAM_JOURNAL_DIR"), *home = getenv("HOME");
    char *root = configured && *configured ? strdup(configured) : home && *home ? join(home, "Journal") : NULL;
    if (!root || root[0] != '/') { fputs("tam-journal: journal directory must be absolute; set HOME or TAM_JOURNAL_DIR.\n", stderr); free(root); return 2; }
    char name[24]; snprintf(name, sizeof name, "%s.txt", day);
    char *path = join(root, name), *draftdir = join(root, ".drafts"), *draft = NULL;
    char *before = NULL, *after = NULL, *current = NULL;
    int rootfd = -1, draftsfd = -1, lockfd = -1, draftfd = -1, result = 1, keep = 0;
    size_t beforelen = 0, afterlen = 0, currentlen = 0;
    if (!path || !draftdir || (rootfd = private_dir(root)) < 0 ||
        (draftsfd = private_dir(draftdir)) < 0) goto failed;
    if (!add && (lockfd = lock_day(root, day)) < 0) goto busy;
    before = add ? strdup("") : read_text(path, &beforelen, 1);
    if (!before) goto failed;
    char draftname[40]; snprintf(draftname, sizeof draftname, "%s-XXXXXX", day);
    draft = join(draftdir, draftname);
    if (!draft || (draftfd = mkstemp(draft)) < 0) goto failed;
    if (fcntl(draftfd, F_SETFD, FD_CLOEXEC) || fchmod(draftfd, 0600)) goto failed;
    size_t written = 0;
    while (written < beforelen) {
        ssize_t n = write(draftfd, before+written, beforelen-written);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) goto failed;
        written += (size_t)n;
    }
    if (fsync(draftfd) || fsync(draftsfd) || fsync(rootfd)) goto failed;
    close(draftfd); draftfd = -1; keep = 1;
    if (editor(draft) != 0) { fputs("tam-journal: Micro did not finish successfully.\n", stderr); goto out; }
    after = read_text(draft, &afterlen, 0);
    if (!after) goto failed;
    if ((add && blank(after, afterlen)) || (!add && beforelen == afterlen && !memcmp(before, after, afterlen))) {
        result = 0; keep = 0; goto out;
    }
    if (add && (lockfd = lock_day(root, day)) < 0) goto busy;
    current = read_text(path, &currentlen, 1);
    if (!current) goto failed;
    if (!add && (currentlen != beforelen || memcmp(before, current, beforelen))) {
        fputs("tam-journal: the journal changed outside this editor; refusing to overwrite it.\n", stderr); goto out;
    }
    if (add) {
        tam_str_t combined = tam_str_new_len(current, currentlen);
        if (!combined) goto failed;
        if (!currentlen) combined = tam_str_cat_printf(combined, "%s\n", day);
        combined = tam_str_cat_printf(combined, "%s\n[%s]\n", currentlen && current[currentlen-1] != '\n' ? "\n" : "", stamp);
        combined = tam_str_cat_len(combined, after, afterlen);
        if (after[afterlen-1] != '\n') combined = tam_str_cat(combined, "\n");
        size_t length = tam_str_len(combined);
        int ok = length <= LIMIT && tam_fs_write_atomic(path, combined, length, 0600);
        tam_str_free(combined);
        if (!ok) goto failed;
    } else if (!tam_fs_write_atomic(path, after, afterlen, 0600)) goto failed;
    result = 0; keep = 0;
    puts(add ? "Note saved." : "Journal saved.");
    goto out;
busy:
    fputs("tam-journal: this day is already being edited or its lock is unavailable; try again after closing the other editor.\n", stderr);
    result = 3; goto out;
failed:
    fprintf(stderr, "tam-journal: cannot safely read or save the journal: %s.\n", strerror(errno));
out:
    if (draftfd >= 0) close(draftfd);
    if (draft) {
        if (keep) fprintf(stderr, "Draft retained: %s\n", draft);
        else { unlink(draft); if (draftsfd >= 0) fsync(draftsfd); }
    }
    if (lockfd >= 0) close(lockfd);
    if (draftsfd >= 0) close(draftsfd);
    if (rootfd >= 0) close(rootfd);
    free(root); free(path); free(draftdir); free(draft);
    free(before); free(after); free(current);
    return result;
}
