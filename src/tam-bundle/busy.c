/* /proc diagnostics catch interpreted programs as well as ELF mappings.
 * Only this invocation's ancestor chain is exempt, never a process name. */
#define _GNU_SOURCE
#include "tb.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

static pid_t ancestor(pid_t pid) {
    char path[64];
    snprintf(path,sizeof path,"/proc/%ld/stat",(long)pid);
    size_t len;
    char *s=tb_read_at(AT_FDCWD,path,&len);
    if(!s)return 0;
    char *end=strrchr(s,')');
    long p=0;
    char state;
    if(end)sscanf(end+1," %c %ld",&state,&p);
    free(s);
    return (pid_t)p;
}
static bool exempt(pid_t p) {
    for(pid_t mine=getpid();mine>1;) {if(mine==p)return true;
    pid_t next=ancestor(mine);
    if(next==mine)break;
    mine=next;
    }
    return p==1;
}
/* Match complete paths. Only version trees cover descendants; a placed
 * file must match exactly, including when the kernel marks it deleted. */
static bool path_matches(const char *path,const char *expected,bool tree) {
    size_t n=strlen(expected);
    if(strncmp(path,expected,n))return false;
    return !path[n] || !strcmp(path+n," (deleted)") || (tree && path[n]=='/');
}
static bool uses(const char *path,const char *root,const char *old,const tb_manifest_t *m) {
    if(!strcmp(old,"-"))return false; /* no previous product files */
    char *slot=tam_path_join(root,"usr/lib/tamlinux/current");
    bool match=path_matches(path,slot,true);
    free(slot);
    if(m->format==1) {
        const char *legacy[]={"usr/lib/tamlinux/kept","usr/lib/tamlinux/legacy-v1"};
        for(size_t i=0;i<2;i++) {
            slot=tam_path_join(root,legacy[i]);
            match=match || path_matches(path,slot,true);
            free(slot);
        }
    }
    char rel[300];
    snprintf(rel,sizeof rel,"usr/lib/tamlinux/version/%s",old);
    slot=tam_path_join(root,rel);
    match=match || path_matches(path,slot,true);
    free(slot);
    for(size_t i=0;i<m->n && !match;i++)if(!strncmp(m->v[i].path,"placed/",7) && m->v[i].type=='f') {
        char *p=tam_path_join(root,m->v[i].path+7);
        match=path_matches(path,p,false);
        free(p);
    }
    return match;
}
static bool link_uses(int fd,const char *name,const char *root,const char *old,const tb_manifest_t *m) {
    char buf[8192];
    ssize_t n=readlinkat(fd,name,buf,sizeof buf-1);
    if(n<0 || (size_t)n==sizeof buf-1)return false;
    buf[n]=0;
    return uses(buf,root,old,m);
}
static bool cmd_uses(char *cmd,size_t len,const char *root,const char *old,const tb_manifest_t *m) {
    /* /proc cmdline is NUL-separated. Flattening it to text loses the
     * difference between a path and a substring, and splits spaced paths. */
    for(size_t off=0;off<len;) {
        size_t n=strnlen(cmd+off,len-off);
        if(n==len-off)break;
        if(uses(cmd+off,root,old,m))return true;
        off+=n+1;
    }
    return false;
}
static bool maps_use(char *maps,const char *root,const char *old,const tb_manifest_t *m) {
    char *save;
    for(char *line=strtok_r(maps,"\n",&save);line;line=strtok_r(NULL,"\n",&save)) {
        int off=0;
        /* address, permissions, offset, device, inode, optional pathname */
        sscanf(line,"%*s %*s %*s %*s %*s %n",&off);
        if(off && line[off]=='/' && uses(line+off,root,old,m))return true;
    }
    return false;
}
static int scan(const char *root,const char *old,const tb_manifest_t *m,bool stop,bool *graphical_running) {
    DIR *proc=opendir("/proc");
    if(!proc)return -1;
    struct dirent *de;
    int found=0;
    while((de=readdir(proc))) {
        char *end;
    long v=strtol(de->d_name,&end,10);
    if(*end || v<=1)continue;
        bool launcher=exempt((pid_t)v);
        /* Ancestors are exempt as old-file users, but an apply launched
         * from a graphical terminal must still see its compositor. */
        if(launcher && strcmp(root,"/"))continue;
        int fd=openat(dirfd(proc),de->d_name,O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(fd<0)continue;
        int pidfd=(int)syscall(SYS_pidfd_open,(pid_t)v,0);
        bool match=link_uses(fd,"exe",root,old,m) || link_uses(fd,"cwd",root,old,m);
        char comm[256]="";
    size_t len=0;
    char *cmd=tb_read_at(fd,"cmdline",&len);
        if(cmd) {match=match || cmd_uses(cmd,len,root,old,m);
    for(size_t i=0;i<len;i++)if(!cmd[i])cmd[i]=' ';
    snprintf(comm,sizeof comm,"%.250s",cmd);
    free(cmd);
    }
        char *maps=tb_read_at(fd,"maps",&len);
    if(maps){match=match || maps_use(maps,root,old,m);
    free(maps);
    }
        /* A compositor on the live root blocks apply, including --yes.
         * This guard never authorizes a signal and ignores temporary roots. */
        bool graphical=false;
        char exe[4096];
    ssize_t en=readlinkat(fd,"exe",exe,sizeof exe-1);
        if(!strcmp(root,"/") && en>=0 && (size_t)en<sizeof exe-1){exe[en]=0;
    const char *base=strrchr(exe,'/');
    if(base && !strcmp(base+1,"Hyprland"))graphical=true;
    }
        if(graphical && graphical_running)*graphical_running=true;
        int fds=openat(fd,"fd",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    DIR *list=fds<0?NULL:fdopendir(fds);
    struct dirent *f;
        if(fds>=0 && !list)close(fds);
        if(list){while(!match && (f=readdir(list)))if(f->d_name[0]!='.')match=link_uses(dirfd(list),f->d_name,root,old,m);
    closedir(list);
    }
        if(launcher)match=false;
        if(match || graphical) {
            found++;
    tb_note("%s: pid %ld %s",graphical ? "graphical session still running" : "still using old Tamlinux",v,comm);
            if(stop && match && !graphical && pidfd>=0 &&
               !syscall(SYS_pidfd_send_signal,pidfd,SIGTERM,NULL,0)) {
                struct pollfd p={.fd=pidfd,.events=POLLIN};
                if(poll(&p,1,5000)==0)syscall(SYS_pidfd_send_signal,pidfd,SIGKILL,NULL,0);
            }
        }
        if(pidfd>=0)close(pidfd);
    close(fd);
    }
    closedir(proc);
    return found;
}
int tb_busy(const char *root,const char *old,const tb_manifest_t *m,bool stop) {
    char *canonical=realpath(root,NULL);
    if(!canonical)return -1;
    bool graphical=false;
    /* Discover blockers before sending any signal. A live graphical
     * session leaves all its programs alone, not just the compositor. */
    int n=scan(canonical,old,m,false,&graphical);
    if(stop && n>0 && !graphical) {
        n=scan(canonical,old,m,true,NULL);
        if(n>0) {
            usleep(20000);
            n=scan(canonical,old,m,false,NULL);
        }
    }
    free(canonical);
    return n;
}
