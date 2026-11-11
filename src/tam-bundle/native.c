/* Native package queries are read-only, rooted explicitly, and fail closed.
 * -Ql reads the database's file list, including files absent on disk. */
#define _GNU_SOURCE
#include "tb.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool stable(const struct stat *a,const struct stat *b) {
    return a->st_dev==b->st_dev && a->st_ino==b->st_ino &&
        a->st_ctim.tv_sec==b->st_ctim.tv_sec && a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
static char *query(const char *root,const char *db,const char *op) {
    const char *argv[]={"/usr/bin/pacman","--config","/dev/null","--root",root,"--dbpath",db,op,NULL};
    tam_exec_opts_t opts={.timeout_ms=30000,.max_output=64U*1024U*1024U};
    tam_exec_result_t r=tam_exec_opts(argv,&opts);
    char *out=NULL;
    if(!r.timed_out && !r.truncated && (r.exit_code==0 ||
       (r.exit_code==1 && !r.stdout_len && !r.stderr_len)) && !r.stderr_len) {
        out=r.stdout_buf; r.stdout_buf=NULL;
    }
    if(!out)tb_err("cannot establish native ownership with pacman %s",op);
    tam_exec_result_free(&r); return out;
}
static int cmp(const void *a,const void *b) {
    return strcmp(((const tb_native_file *)a)->path,((const tb_native_file *)b)->path);
}
void tb_native_free(tb_native_t *n) {
    free(n->root); free(n->versions);
    for(size_t i=0;i<n->nfiles;i++){free(n->files[i].path); free(n->files[i].package);}
    free(n->files); memset(n,0,sizeof *n);
}
static int read_db(const char *root,bool files,bool hook,tb_native_t *n) {
    memset(n,0,sizeof *n);
    n->root=realpath(root,NULL);
    char *db=n->root?tam_path_join(n->root,"var/lib/pacman"):NULL;
    char *local=db?tam_path_join(db,"local"):NULL,*lock=db?tam_path_join(db,"db.lck"):NULL;
    struct stat before,after,st;
    char *list=NULL; int result=-1;
    if(!db || !local || !lock || lstat(local,&before) || !S_ISDIR(before.st_mode) ||
       (!hook && (!lstat(lock,&st) || errno!=ENOENT)))goto done;
    n->versions=query(n->root,db,"-Q");
    if(!n->versions)goto done;
    if(files) {
        list=query(n->root,db,"-Ql");
        if(!list)goto done;
        size_t count=0;
        for(const char *p=list;*p;p++)if(*p=='\n')count++;
        if(count>1000000)goto done;
        n->files=calloc(count?count:1,sizeof *n->files);
        if(!n->files)goto done;
        for(char *save=NULL,*line=strtok_r(list,"\n",&save);line;line=strtok_r(NULL,"\n",&save)) {
            char *path=strchr(line,' ');
            if(!path)goto done;
            *path++=0;
            size_t len=strlen(n->root);
            if(strcmp(n->root,"/")) {
                if(strncmp(path,n->root,len) || path[len]!='/')goto done;
                path+=len;
            }
            if(*path!='/' || strlen(path)>=4096 || !*line)goto done;
            size_t pn=strlen(path);
            if(pn>1 && path[pn-1]=='/')continue; /* shared structural directories */
            if(!tam_path_is_safe_rel(path+1))goto done;
            tb_native_file *e=&n->files[n->nfiles++];
            e->path=strdup(path); e->package=strdup(line);
            if(!e->path || !e->package)goto done;
        }
        qsort(n->files,n->nfiles,sizeof *n->files,cmp);
    }
    if(lstat(local,&after) || !stable(&before,&after) ||
       (!hook && (!lstat(lock,&st) || errno!=ENOENT)))goto done;
    result=0;
done:
    free(list); free(db); free(local); free(lock);
    if(result){tb_err("native package database is unavailable, changing or locked"); tb_native_free(n);}
    return result;
}
int tb_native_read(const char *root,bool files,tb_native_t *n) {
    return read_db(root,files,false,n);
}
int tb_native_read_hook(const char *root,tb_native_t *n) {
    /* Pacman holds its lock for the duration of a post-transaction hook. */
    return read_db(root,true,true,n);
}
const char *tb_native_owner(const tb_native_t *n,const char *path) {
    tb_native_file key={.path=(char *)path};
    tb_native_file *e=bsearch(&key,n->files,n->nfiles,sizeof *n->files,cmp);
    return e?e->package:NULL;
}
char *tb_native_version(const tb_native_t *n,const char *package) {
    size_t len=strlen(package);
    for(const char *p=n->versions;*p;) {
        const char *nl=strchr(p,'\n'); if(!nl)return NULL;
        if((size_t)(nl-p)>len+1 && !strncmp(p,package,len) && p[len]==' ')
            return strndup(p+len+1,(size_t)(nl-p)-len-1);
        p=nl+1;
    }
    return NULL;
}
int tb_native_conflicts(const char *root,const tb_manifest_t *a,const tb_manifest_t *b) {
    tb_native_t n;
    if(tb_native_read(root,true,&n))return -1;
    int result=0;
    const tb_manifest_t *sets[]={a,b};
    for(size_t k=0;k<2;k++)for(size_t i=0;i<sets[k]->n;i++) {
        const tb_entry_t *e=&sets[k]->v[i];
        if(e->type!='f' || strncmp(e->path,"placed/",7))continue;
        char *absolute=NULL;
        if(asprintf(&absolute,"/%s",e->path+7)<0){result=-1; continue;}
        const char *owner=tb_native_owner(&n,absolute);
        if(owner){tb_err("native package %s owns %s; refusing Tamlinux ownership",owner,absolute); result=-1;}
        free(absolute);
    }
    tb_native_free(&n); return result;
}
int tb_version_compare(const char *actual,const char *required) {
    const char *argv[]={"/usr/bin/vercmp",actual,required,NULL};
    tam_exec_result_t r=tam_exec(argv,5000); int result=-2;
    if(!r.exit_code && r.stdout_buf && !r.truncated && !r.timed_out) {
        char *end=NULL; long value=strtol(r.stdout_buf,&end,10);
        if(end && *end=='\n' && !end[1])result=value<0?-1:value>0?1:0;
    }
    tam_exec_result_free(&r); return result;
}
