#include "tb.h"
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>

bool tb_quiet;
char *tb_read_limit(int dirfd,const char *rel,size_t *len,size_t limit) {
    int fd=openat(dirfd,rel,O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0)return NULL;
    struct stat a,b; char *data=NULL; size_t off=0;
    if(fstat(fd,&a) || !S_ISREG(a.st_mode) || a.st_size<0 || (uint64_t)a.st_size>limit)goto done;
    size_t n=(size_t)a.st_size;
    data=malloc(n+1); if(!data)goto done;
    while(off<n) {
        ssize_t r=read(fd,data+off,n-off);
        if(r<0 && errno==EINTR)continue;
        if(r<=0)break;
        off+=(size_t)r;
    }
    if(off!=n || fstat(fd,&b) || a.st_size!=b.st_size ||
       a.st_ctim.tv_sec!=b.st_ctim.tv_sec || a.st_ctim.tv_nsec!=b.st_ctim.tv_nsec) {
        free(data); data=NULL; goto done;
    }
    data[n]=0; if(len)*len=n;
done:
    close(fd); if(!data)errno=EINVAL; return data;
}

char *tb_read_at(int dirfd, const char *rel, size_t *len) {
    int fd = openat(dirfd, rel, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return NULL;
    size_t cap = 4096, n = 0;
    char *buf = malloc(cap + 1);
    while (buf) {
        if (n == cap) {
            char *grown = realloc(buf, cap * 2 + 1);
            if (!grown) break;
            buf = grown;
            cap *= 2;
        }
        ssize_t r = read(fd, buf + n, cap - n);
        if (r < 0 && errno == EINTR) continue;
        if (r < 0) break;
        if (r == 0) {
            close(fd);
            buf[n] = '\0';
            if (len) *len = n;
            return buf;
        }
        n += (size_t)r;
    }
    int saved = errno;
    free(buf);
    close(fd);
    errno = saved ? saved : ENOMEM;
    return NULL;
}

void tb_err(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fputs("tam-bundle: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

void tb_note(const char *fmt, ...) {
    if (tb_quiet) return;
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    putchar('\n');
    va_end(ap);
}

#include <linux/openat2.h>
#include <sys/syscall.h>

int tb_open_beneath(int dirfd, const char *rel) {
    struct open_how how = {
        .flags = O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC,
        .resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS,
    };
    long fd = syscall(SYS_openat2, dirfd, rel, &how, sizeof how);
    /* A fallback openat would follow links in parent components. */
    return (int)fd;
}
