/* Conventional product installation. An applying record is a durable barrier:
 * errors leave it present and retry only moves forward. No init-system logic. */
#define _GNU_SOURCE
#include "tb.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/stat.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <unistd.h>

typedef struct {
    char id[65], from[256], to[256], state[16];
    char activation[TB_HASH_LEN+1];
    unsigned long uid;
    size_t done;
    unsigned fields;
} transaction;
typedef struct {
    const char *root;
    int rootfd, slot, state, lock;
    transaction tx;
    char **allowed;
     size_t nallowed;
    char **steps;
     char **commands;
     bool *required;
     size_t nsteps;
    char *services;
} context;

static int syncdir(int fd);
static bool identity(const char *s) {
    if (!s || !*s || strlen(s)>240 || !strcmp(s,".") || !strcmp(s,"..")) return false;
    for (;*s;s++) if (!strchr("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._+-",*s))return false;
    return true;
}
static bool activation_hash(const char *s) {
    return !strcmp(s,"-") ||
        (strlen(s)==TB_HASH_LEN && !strncmp(s,"sha256:",7) &&
         strspn(s+7,"0123456789abcdef")==64);
}
static bool uid_number(const char *text,unsigned long *uid) {
    if(!*text || strspn(text,"0123456789")!=strlen(text))return false;
    errno=0;
    unsigned long parsed=strtoul(text,NULL,10);
    if(errno || parsed>=UINT_MAX)return false;
    *uid=parsed;
    return true;
}
static int prepare_uid(unsigned long *uid) {
    *uid=getuid();
    const char *caller=*uid==0?getenv("PKEXEC_UID"):NULL;
    if(caller && !uid_number(caller,uid)) {
        tb_err("invalid invoking user in PKEXEC_UID");
        return -1;
    }
    return 0;
}
static int parent(int base, const char *path, bool create, char **leaf) {
    if (!tam_path_is_safe_rel(path)) return -1;
    char *copy=strdup(path), *last=strrchr(copy,'/');
    *leaf=strdup(last?last+1:copy);
    int fd=fcntl(base,F_DUPFD_CLOEXEC,0);
    if(last) {
        *last=0;
     char *save=NULL;
        for(char *p=strtok_r(copy,"/",&save);p && fd>=0;p=strtok_r(NULL,"/",&save)) {
            int made=create?mkdirat(fd,p,0755):-1;
            if(create && ((made && errno!=EEXIST) || (!made && syncdir(fd)))) {close(fd);
    fd=-1;
    break;
    }
            int next=openat(fd,p,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    close(fd);
    fd=next;
        }
    }
    free(copy);
    return fd;
}
static int directory(int base,const char *path,bool create) {
    char *leaf=NULL;
    int fd=parent(base,path,create,&leaf),out=-1;
    if(fd>=0) {
        int made=create?mkdirat(fd,leaf,0755):-1;
        if(!create || (made && errno==EEXIST) || (!made && !syncdir(fd))) out=openat(fd,leaf,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        close(fd);
    }
    free(leaf);
    return out;
}
static int syncdir(int fd) {if(fsync(fd))return -1;
    tb_fault("v2-directory-sync");
    return 0;
    }
static int syncall(int fd) {if(syncfs(fd))return -1;
    tb_fault("v2-filesystem-sync");
    return 0;
    }
static int atomic(int fd,const char *name,const char *data,size_t len) {
    char tmp[300];
    snprintf(tmp,sizeof tmp,".%s.new",name);
    int out=openat(fd,tmp,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW|O_CLOEXEC,0644);
    if(out<0)return -1;
    size_t off=0;
    int r=0;
    while(off<len) {ssize_t n=write(out,data+off,len-off);
    if(n<0&&errno==EINTR)continue;
    if(n<=0){r=-1;
    break;
    }off+=(size_t)n;
    }
    if(!r && fsync(out))r=-1;
    close(out);
    tb_fault("v2-record-file-sync");
    if(!r && renameat(fd,tmp,fd,name))r=-1;
    if(!r) {tb_fault("v2-record-rename");
    r=syncdir(fd);
    }
    return r;
}
static int txline(void *u,size_t line,int fmt,char **f,size_t n) {
    transaction *t=u;
    (void)line;
    (void)fmt;
    if(n!=2)return 1;
    const char *keys[]={"id","from","to","state","uid","done","capture","kind","activation"};
    unsigned bit=0;
    for(size_t i=0;i<sizeof keys/sizeof *keys;i++)
        if(!strcmp(f[0],keys[i]))bit=1U<<i;
    if(!bit || (t->fields&bit))return 1;
    t->fields|=bit;
    char *dst=NULL;
    size_t cap=0;
    if(!strcmp(f[0],"id")){dst=t->id;
    cap=sizeof t->id;
    }
    else if(!strcmp(f[0],"from")){dst=t->from;
    cap=sizeof t->from;
    }
    else if(!strcmp(f[0],"to")){dst=t->to;
    cap=sizeof t->to;
    }
    else if(!strcmp(f[0],"state")){dst=t->state;
    cap=sizeof t->state;
    }
    else if(!strcmp(f[0],"activation")) {
        if(!activation_hash(f[1]))return 1;
        dst=t->activation;
        cap=sizeof t->activation;
    }
    else if(!strcmp(f[0],"uid"))return !uid_number(f[1],&t->uid);
    else if(!strcmp(f[0],"done")) {
        char *end;
    errno=0;
    unsigned long v=strtoul(f[1],&end,10);
    if(errno || !*f[1] || strspn(f[1],"0123456789")!=strlen(f[1]) || *end)return 1;
        t->done=v;
        return 0;
    } else if(!strcmp(f[0],"capture"))return strcmp(f[1],"-")!=0;
    else if(!strcmp(f[0],"kind"))return strcmp(f[1],"product-only")!=0;
    else return 1;
    if(*dst || strlen(f[1])>=cap)return 1;
    strcpy(dst,f[1]);
    return 0;
}
static int readtx(context *c) {
    memset(&c->tx,0,sizeof c->tx);
    size_t len=0;
    char *text=tb_read_limit(c->state,"transaction",&len,4096);
    if(!text)return errno==ENOENT?0:-1;
    int r=tam_rec_parse(text,len,2,1,txline,&c->tx,NULL);
    free(text);
    transaction *t=&c->tx;
    if(r || (t->fields!=255 && t->fields!=511) || strlen(t->id)!=32 || strspn(t->id,"0123456789abcdef")!=32 ||
       !identity(t->from) || !identity(t->to) || !strcmp(t->to,"-") || !strcmp(t->from,t->to) ||
       (strcmp(t->state,"staging") && strcmp(t->state,"ready") && strcmp(t->state,"applying") && strcmp(t->state,"activating") && strcmp(t->state,"complete"))) {
        tb_err("invalid transaction; desktop must remain blocked");
    return -1;
    }
    return 1;
}
static int state(context *c,const char *s) {
    snprintf(c->tx.state,sizeof c->tx.state,"%s",s);
    char text[1024];
    int n=snprintf(text,sizeof text,"format 1\nid %s\nfrom %s\nto %s\ncapture -\nuid %lu\nkind product-only\nstate %s\ndone %zu\nactivation %s\n",
        c->tx.id,c->tx.from,c->tx.to,c->tx.uid,s,c->tx.done,*c->tx.activation?c->tx.activation:"-");
    if(atomic(c->state,"transaction",text,(size_t)n))return -1;
    char fault[64];
    snprintf(fault,sizeof fault,"v2-state-%s",s);
    tb_fault(fault);
    return 0;
}
static int current(context *c,char id[256]) {
    char link[300];
    ssize_t n=readlinkat(c->slot,"current",link,sizeof link-1);
    if(n<0 && errno==ENOENT){strcpy(id,"-");
    return 0;
    }
    if(n<0)return -1;
    link[n]=0;
    if(strncmp(link,"version/",8) || !identity(link+8))return -1;
    strcpy(id,link+8);
    return 0;
}
static int load(context *c,const char *id,tb_bundle_t *b,tb_manifest_t *m) {
    char path[300];
    snprintf(path,sizeof path,"version/%s",id);
    return tb_tree_read(c->slot,path,b,m);
}
static int previousline(void *u,size_t line,int fmt,char **f,size_t n) {
    (void)line; (void)fmt;
    char *id=u;
    if(n!=2 || strcmp(f[0],"previous") || *id || !identity(f[1]))return 1;
    strcpy(id,f[1]);
    return 0;
}
static int print_status(context *c,const char *cur) {
    char previous[256]="";
    size_t len=0;
    char *text=tb_read_at(c->state,"keep",&len);
    if(text) {
        int r=tam_rec_parse(text,len,2,1,previousline,previous,NULL);
        free(text);
        if(r || !*previous)return -1;
    } else if(errno!=ENOENT)return -1;
    printf("live: %s\nprevious: %s\n",cur,*previous?previous:"-");
    if(strcmp(cur,"-")) {
        tb_bundle_t b={0}; tb_manifest_t m={0};
        if(load(c,cur,&b,&m))return -1;
        printf("bundle-hash: %s\n",b.hash);
        tb_bundle_free(&b); tb_manifest_free(&m);
    }
    return 0;
}
bool tb_placement_allowed(const char *p,bool dir,char *const *roots,size_t count) {
    /* No profile can widen the product's fundamental placement boundary. */
    if(((strcmp(p,"usr") || !dir) && strncmp(p,"usr/",4)) || !strncmp(p,"usr/local",9) || !strncmp(p,"usr/lib/tamlinux",16))return false;
    if(!strncmp(p,"usr/bin/",8) && !dir) {
        const char *name=p+8;
        if(strchr(name,'/') || (strcmp(name,"tam") && strcmp(name,"hi") && strcmp(name,"start") && strncmp(name,"tam-",4)) ||
           !strcmp(name,"tam-bundle") || !strcmp(name,"tam-nut"))return false;
    }
    for(size_t i=0;i<count;i++) {
        size_t n=strlen(roots[i]);
        if(!strncmp(p,roots[i],n) && (p[n]=='/' || !p[n]))return true;
        if(dir && !strncmp(roots[i],p,strlen(p)) && roots[i][strlen(p)]=='/')return true;
    }
    return false;
}
static bool allowed(context *c,const char *path,bool dir) {
    return tb_placement_allowed(path,dir,c->allowed,c->nallowed);
}
static int profileline(void *u,size_t l,int fmt,char **f,size_t n) {
    context *c=u;
    (void)l;
    (void)fmt;
    if(n==2 && !strcmp(f[0],"allow") && tam_path_is_safe_rel(f[1])) {
        char **v=realloc(c->allowed,(c->nallowed+1)*sizeof *v);
    if(!v)return 1;
        c->allowed=v;
    c->allowed[c->nallowed++]=strdup(f[1]);
    return 0;
    }
    if(n==2 && !strcmp(f[0],"services") && tam_path_is_safe_rel(f[1]) && !c->services) {c->services=strdup(f[1]);
    return 0;
    }
    if(n==4 && !strcmp(f[0],"step") && tb_valid_name(f[1]) && (!strcmp(f[2],"required") || !strcmp(f[2],"optional")) && tam_path_is_safe_rel(f[3])) {
        size_t k=c->nsteps++;
        c->steps=realloc(c->steps,c->nsteps*sizeof *c->steps);
    c->commands=realloc(c->commands,c->nsteps*sizeof *c->commands);
    c->required=realloc(c->required,c->nsteps*sizeof *c->required);
        if(!c->steps || !c->commands || !c->required)return 1;
        c->steps[k]=strdup(f[1]);
    c->commands[k]=strdup(f[3]);
    c->required[k]=!strcmp(f[2],"required");
    return 0;
    }
    if(n==4 && !strcmp(f[0],"user-step") && tb_valid_name(f[1]) &&
       (!strcmp(f[2],"required") || !strcmp(f[2],"optional")) && tam_path_is_safe_rel(f[3]))return 0;
    return 1;
}
static int profile(context *c) {
    int fd=tb_open_beneath(c->rootfd,"usr/share/tamlinux/rootstock/profile");
    if(fd<0){tb_err("a rootstock profile is required");
    return -1;
    }
    struct stat st;
    int r=fstat(fd,&st);
    close(fd);
    if(r || !S_ISREG(st.st_mode) || (st.st_mode&0022) || st.st_uid!=geteuid())return -1;
    size_t len;
    char *text=tb_read_at(c->rootfd,"usr/share/tamlinux/rootstock/profile",&len);
    r=text?tam_rec_parse(text,len,4,1,profileline,c,NULL):-1;
    free(text);
    return r;
}
static int command(context *c,const char *rel,const char *op) {
    char *path=tam_path_join(c->root,rel);
    const char *args[]={path,op,c->tx.id,NULL};
    tam_exec_result_t r=tam_exec(args,30000);
    bool ok=r.exit_code==0;
    if(!ok)tb_err("rootstock action %s failed: %s",rel,r.stderr_buf?r.stderr_buf:"");
    tam_exec_result_free(&r);
    free(path);
    return ok?0:-1;
}
static int hashfile(int base,const char *p,char out[TB_HASH_LEN+1],unsigned *mode) {
    int fd=tb_open_beneath(base,p);
    struct stat st;
    uint8_t digest[TAM_SHA256_LEN];
    if(fd<0)return -1;
    int r=fstat(fd,&st);
    if(!r && !S_ISREG(st.st_mode))r=-1;
    if(!r)r=tam_sha256_fd(fd,digest);
    close(fd);
    if(r)return -1;
    memcpy(out,"sha256:",7);
    tam_sha256_hex(digest,out+7);
    if(mode)*mode=(unsigned)(st.st_mode&07777);
    return 0;
}
static bool matches(int fd,const char *p,const tb_entry_t *e) {
    char h[TB_HASH_LEN+1];
    unsigned mode=0;
    return !hashfile(fd,p,h,&mode) && mode==e->mode && !strcmp(h,e->value);
}
static int copyfile(int from,const char *src,int to,const char *dst,unsigned mode) {
    int in=tb_open_beneath(from,src);
    char *leaf=NULL;
    int p=parent(to,dst,true,&leaf);
    int out=p<0?-1:openat(p,leaf,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW|O_CLOEXEC,0600),r=-1;
    if(in>=0 && out>=0 && !tam_fs_clone_or_copy(in,out,NULL) && !fchmod(out,mode) && !fsync(out)) {
        tb_fault("v2-copy-sync");
    r=syncdir(p);
    }
    if(in>=0)close(in);
    if(out>=0)close(out);
    if(p>=0)close(p);
    free(leaf);
    return r;
}
static void location(char out[4096],const tb_entry_t *e) {
    snprintf(out,4096,"%s%s",!strncmp(e->path,"placed/",7)?".placed/":"",e->path+(!strncmp(e->path,"placed/",7)?7:5));
}
static int directory_modes(int fd,const tb_manifest_t *m,bool installed) {
    /* Populate children before making a private directory read-only. */
    for(size_t i=m->n;i>0;i--) {
        const tb_entry_t *e=&m->v[i-1];
        if(e->type!='d' || (installed && !strchr(e->path,'/')))continue;
        char path[4096];
        if(installed)location(path,e);
        else snprintf(path,sizeof path,"%s",e->path);
        int d=directory(fd,path,false);
        if(d<0)return -1;
        int r=fchmod(d,e->mode) || syncdir(d);
        close(d);
        if(r)return -1;
    }
    return 0;
}
static int version_check(context *c,const char *id,const tb_manifest_t *m) {
    char rel[300];
    snprintf(rel,sizeof rel,"version/%s",id);
    int fd=directory(c->slot,rel,false);
    if(fd<0)return -1;
    int r=0;
    for(size_t i=0;i<m->n;i++) {
        const tb_entry_t *e=&m->v[i];
    if(!strchr(e->path,'/'))continue;
        char path[4096];
    location(path,e);
        if(e->type=='f' && !matches(fd,path,e)){tb_err("staged or retained file differs: %s/%s",id,path);
    r=-1;
    }
        if(e->type=='l') {char link[4096];
    ssize_t n=readlinkat(fd,path,link,sizeof link-1);
    if(n<0)r=-1;
    else{link[n]=0;
    if(strcmp(link,e->value))r=-1;
    }}
        if(e->type=='d') {
            int d=directory(fd,path,false);
            struct stat st;
            if(d<0 || fstat(d,&st) || (unsigned)(st.st_mode&07777)!=e->mode)r=-1;
            if(d>=0)close(d);
        }
    }
    close(fd);
    return r;
}
static int mountcheck(context *c,const tb_manifest_t *m) {
    struct statx a,b;
    if(statx(c->slot,"",AT_EMPTY_PATH,STATX_MNT_ID,&a))return -1;
    for(size_t i=0;i<m->n;i++) {
        const tb_entry_t *e=&m->v[i];
    if(strncmp(e->path,"placed/",7))continue;
        const char *path=e->path+7;
        if(!allowed(c,path,e->type=='d')){tb_err("placement refused: /%s",path);
    return -1;
    }
        char *copy=strdup(path);
    int fd=-1;
        while(copy && (fd=tb_open_beneath(c->rootfd,copy))<0 && errno==ENOENT) {char *s=strrchr(copy,'/');
    if(!s)break;
    *s=0;
    }
        free(copy);
    if(fd<0)return -1;
        int r=statx(fd,"",AT_EMPTY_PATH,STATX_MNT_ID,&b);
    close(fd);
        if(r || !(a.stx_mask&STATX_MNT_ID) || !(b.stx_mask&STATX_MNT_ID) || a.stx_mnt_id!=b.stx_mnt_id || a.stx_dev_major!=b.stx_dev_major || a.stx_dev_minor!=b.stx_dev_minor) {tb_err("placed path is on another mount: /%s",path);
    return -1;
    }
    }
    return 0;
}
/* Recheck native ownership even for database-listed files absent on disk. */
static int secure(context *c,const tb_manifest_t *newm,const tb_manifest_t *oldm) {
    if(mountcheck(c,newm) || mountcheck(c,oldm) || tb_native_conflicts(c->root,newm,oldm))return -1;
    for(size_t i=0;i<newm->n;i++) {
        const tb_entry_t *e=&newm->v[i];
    if(strncmp(e->path,"placed/",7))continue;
        const tb_entry_t *old=tb_manifest_find(oldm,e->path);
        char *leaf=NULL;
    int p=parent(c->rootfd,e->path+7,false,&leaf);
    struct stat st;
        int exists=p>=0?fstatat(p,leaf,&st,AT_SYMLINK_NOFOLLOW):-1;
    int err=errno;
        if(p>=0)close(p);
    free(leaf);
        if(!exists && (e->type=='d' ? !S_ISDIR(st.st_mode) : !old || old->type!='f' || !S_ISREG(st.st_mode))) {tb_err("unowned or conflicting placed file: /%s",e->path+7);
    return -1;
    }
        if(exists && err!=ENOENT){tb_err("cannot inspect /%s",e->path+7);
    return -1;
    }
    }
    if(!strcmp(c->tx.from,"-"))return 0;
    char rel[300];
    snprintf(rel,sizeof rel,"version/%s",c->tx.from);
    int oldfd=directory(c->slot,rel,false);
    if(oldfd<0)return -1;
    int r=0;
    for(size_t i=0;i<oldm->n;i++) {
        const tb_entry_t *e=&oldm->v[i];
    if(strncmp(e->path,"placed/",7) || e->type!='f')continue;
        char backup[4096];
    location(backup,e);
        if(!matches(c->rootfd,e->path+7,e)) {tb_err("live file changed or missing: /%s",e->path+7);
    r=-1;
    break;
    }
        if(!matches(oldfd,backup,e) && copyfile(c->rootfd,e->path+7,oldfd,backup,e->mode)){r=-1;
    break;
    }
    }
    close(oldfd);
    return r;
}
static int copybundle(context *c,const char *source,const tb_bundle_t *b,const tb_manifest_t *m) {
    char stage[340];
    snprintf(stage,sizeof stage,"staging/%s",c->tx.id);
    int dest=directory(c->slot,stage,true),src=open(source,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(dest<0 || src<0){if(dest>=0)close(dest);
    if(src>=0)close(src);
    return -1;
    }
    int r=0;
    const char *parts[]={"tree","placed"};
    for(size_t i=0;i<2;i++) {
        int fd=directory(dest,parts[i],true);
        if(fd<0)r=-1;
        else close(fd);
    }
    for(size_t i=0;i<m->n && !r;i++) {
        const tb_entry_t *e=&m->v[i];
        if(e->type=='d'){int fd=directory(dest,e->path,true);
    if(fd<0)r=-1;
    else close(fd);
    }
        else if(e->type=='f')r=copyfile(src,e->path,dest,e->path,e->mode);
        else {char *leaf=NULL;
    int fd=parent(dest,e->path,true,&leaf);
    if(fd<0 || symlinkat(e->value,fd,leaf))r=-1;
    if(fd>=0)close(fd);
    free(leaf);
    }
        tb_fault_file(i+1);
    }
    const char *records[]={"BUNDLE","MANIFEST","SETTINGS"};
    for(size_t i=0;i<3 && !r;i++)r=copyfile(src,records[i],dest,records[i],0644);
    if(!r)r=directory_modes(dest,m,false);
    close(src);
    close(dest);
    char *slot=tam_path_join(c->root,"usr/lib/tamlinux"),*full=tam_path_join(slot,stage);
    free(slot);
    if(!r)r=tb_v2_verify(full);
    free(full);
    if(r)return -1;
    /* Build an immutable rollback source before arming. */
    char ver[300];
    snprintf(ver,sizeof ver,"version/%s",b->id);
    int versionsfd=directory(c->slot,"version",true);
    if(versionsfd<0)return -1;
    close(versionsfd);
    int existing=directory(c->slot,ver,false);
    if(existing>=0) {
        close(existing);
        return version_check(c,b->id,m);
    }
    if(errno!=ENOENT)return -1;
    char preparing[380];
    snprintf(preparing,sizeof preparing,"%s/version",stage);
    int v=directory(c->slot,preparing,true);
    if(v<0)return -1;
    dest=directory(c->slot,stage,false);
    r=0;
    for(size_t i=0;i<m->n && !r;i++) {
        const tb_entry_t *e=&m->v[i];
    if(!strchr(e->path,'/'))continue;
        char path[4096];
    location(path,e);
        if(e->type=='d'){int fd=directory(v,path,true);
    if(fd<0)r=-1;
    else close(fd);
    }
        else if(e->type=='f')r=copyfile(dest,e->path,v,path,e->mode);
        else {char *leaf=NULL;
    int fd=parent(v,path,true,&leaf);
    if(fd<0 || symlinkat(e->value,fd,leaf))r=-1;
    if(fd>=0)close(fd);
    free(leaf);
    }
    }
    for(size_t i=0;i<3 && !r;i++)r=copyfile(dest,records[i],v,records[i],0644);
    if(!r)r=directory_modes(v,m,true);
    close(v);
    close(dest);
    if(!r && syncall(c->slot))r=-1;
    if(!r && renameat(c->slot,preparing,c->slot,ver))r=-1;
    if(!r){tb_fault("v2-version-rename");
    int versions=directory(c->slot,"version",false);
    r=versions<0?-1:syncdir(versions);
    if(versions>=0)close(versions);
    }
    return r;
}
static int archive_complete(context *c) {
    if(renameat(c->state,"transaction",c->state,"last-transaction"))return -1;
    tb_fault("v2-complete-rename");
    return syncdir(c->state);
}
static int cancel(context *c) {
    if(strcmp(c->tx.state,"staging") && strcmp(c->tx.state,"ready")){tb_err("applying transactions must finish forward");
    return TB_INTERRUPTED;
    }
    /* Clear the arm before removing scratch. A crash may leave unreferenced
     * staging, but can never arm missing sources. */
    if(unlinkat(c->state,"transaction",0) || syncdir(c->state))return TB_FAIL;
    tb_fault("v2-cancel-unlink");
    char stage[340];
    snprintf(stage,sizeof stage,"staging/%s",c->tx.id);
    char *slot=tam_path_join(c->root,"usr/lib/tamlinux");
    int r=tb_remove_at(c->slot,slot,stage);
    free(slot);
    return r?TB_FAIL:TB_OK;
}
static int prepare(context *c,const char *source) {
    unsigned long uid;
    if(prepare_uid(&uid))return TB_FAIL;
    int have=readtx(c);
    if(have<0)return TB_INTERRUPTED;
    if(have && !strcmp(c->tx.state,"complete")){if(archive_complete(c))return TB_FAIL;
    have=0;
    }
    if(have && strcmp(c->tx.state,"ready") && strcmp(c->tx.state,"staging")){tb_err("finish the pending apply first");
    return TB_INTERRUPTED;
    }
    tb_bundle_t b={0},oldb={0};
    tb_manifest_t m={0},oldm={0};
    int r=TB_FAIL;
    if(tb_tree_read(AT_FDCWD,source,&b,&m) || b.format!=2 || tb_v2_verify(source))goto done;
    if(tb_contract_check(c->root,source,&b,false))goto done;
    if(have && cancel(c))goto done;
    memset(&c->tx,0,sizeof c->tx);
    if(current(c,c->tx.from))goto done;
    if(!strcmp(c->tx.from,b.id)){tb_err("bundle already current");
    goto done;
    }
    strcpy(c->tx.to,b.id);
    c->tx.uid=uid;
    unsigned char random[16];
    if(getrandom(random,sizeof random,0)!=sizeof random)goto done;
    for(size_t i=0;i<sizeof random;i++)snprintf(c->tx.id+i*2,3,"%02x",random[i]);
    if(strcmp(c->tx.from,"-") && load(c,c->tx.from,&oldb,&oldm))goto done;
    if(secure(c,&m,&oldm))goto done;
    if(tb_settings_host(c->root,c->state))goto done;
    if(state(c,"staging"))goto done;
    if(copybundle(c,source,&b,&m) || syncall(c->slot))goto done;
    if(state(c,"ready"))goto done;
    tb_note("Tamlinux %s is ready. Leave the desktop and start it again to apply it.",b.version);
    r=TB_OK;
done:
    tb_bundle_free(&b);
    tb_bundle_free(&oldb);
    tb_manifest_free(&m);
    tb_manifest_free(&oldm);
    return r;
}
static int replace(context *c,const tb_entry_t *e,int scratch) {
    const char *dst=e->path+7;
    if(e->type=='d'){int fd=directory(c->rootfd,dst,true);
    if(fd<0)return -1;
    int r=syncdir(fd);
    close(fd);
    return r;
    }
    if(matches(c->rootfd,dst,e))return 0;
    char *source=NULL;
    int sp=parent(scratch,e->path,false,&source);
    char *leaf=NULL;
    int p=parent(c->rootfd,dst,true,&leaf);
    int r=p<0 || sp<0?-1:renameat(sp,source,p,leaf);
    if(!r){tb_fault("v2-placed-rename");
    r=syncdir(sp) || syncdir(p);
    }
    if(sp>=0)close(sp);
    free(source);
    if(p>=0)close(p);
    free(leaf);
    return r;
}
static int staged_check(context *c,const tb_manifest_t *m,int scratch) {
    for(size_t i=0;i<m->n;i++) {
        const tb_entry_t *e=&m->v[i];
        if(e->type!='f' || strncmp(e->path,"placed/",7))continue;
        if(!matches(c->rootfd,e->path+7,e) && !matches(scratch,e->path,e)) {
            tb_err("staged file changed or missing: %s",e->path);
            return -1;
        }
    }
    return 0;
}
/* Before resuming any mutation, prove that every destination is a state this
 * transaction could have produced. Do not overwrite an administrator's edit
 * merely because a durable applying record exists. */
static int resume_check(context *c,const tb_manifest_t *m,const tb_manifest_t *oldm,bool settled) {
    char cur[256];
    if(current(c,cur) || (strcmp(cur,c->tx.to) && (settled || strcmp(cur,c->tx.from)))) {
        tb_err("current version differs from both transaction identities");
        return -1;
    }
    const tb_manifest_t *sets[]={m,oldm};
    for(size_t k=0;k<2;k++)for(size_t i=0;i<sets[k]->n;i++) {
        const tb_entry_t *e=&sets[k]->v[i];
        if(strncmp(e->path,"placed/",7))continue;
        if(k && tb_manifest_find(m,e->path))continue;
        const tb_entry_t *old=tb_manifest_find(oldm,e->path);
        const tb_entry_t *new=tb_manifest_find(m,e->path);
        char *leaf=NULL;
        int p=parent(c->rootfd,e->path+7,false,&leaf);
        struct stat st;
        int exists=p<0?-1:fstatat(p,leaf,&st,AT_SYMLINK_NOFOLLOW);
        int err=errno;
        if(p>=0)close(p);
        free(leaf);
        bool ok;
        if(exists)ok=err==ENOENT && (!new || (!settled && (!old || e->type=='d')));
        else if(e->type=='d')ok=S_ISDIR(st.st_mode);
        else ok=(new && matches(c->rootfd,e->path+7,new)) ||
                (!settled && old && matches(c->rootfd,e->path+7,old));
        if(!ok) {
            tb_err("destination differs from transaction: /%s; repair before retrying apply",e->path+7);
            return -1;
        }
    }
    return 0;
}
static bool activation_plan(const context *c,char hash[TB_HASH_LEN+1]) {
    tam_str_t plan=tam_str_new("format 1\n");
    if(c->services && plan) {
        const char *fields[]={"services",c->services};
        plan=tam_rec_cat(plan,fields,2);
    }
    for(size_t i=0;i<c->nsteps && plan;i++) {
        const char *fields[]={"step",c->steps[i],c->required[i]?"required":"optional",c->commands[i]};
        plan=tam_rec_cat(plan,fields,4);
    }
    if(!plan)return false;
    tam_sha256_t digest;
    uint8_t bytes[TAM_SHA256_LEN];
    tam_sha256_init(&digest);
    tam_sha256_update(&digest,plan,tam_str_len(plan));
    tam_sha256_final(&digest,bytes);
    tam_str_free(plan);
    memcpy(hash,"sha256:",7);
    tam_sha256_hex(bytes,hash+7);
    return true;
}
static int apply(context *c,bool yes) {
    int have=readtx(c);
    if(have<0)return TB_INTERRUPTED;
    if(!have)return TB_OK;
    if(!strcmp(c->tx.state,"staging")){tb_err("unfinished prepare; prepare again or cancel");
    return TB_INTERRUPTED;
    }
    if(!strcmp(c->tx.state,"complete"))return archive_complete(c)?TB_FAIL:TB_OK;
    tb_bundle_t b={0},oldb={0};
    tb_manifest_t m={0},oldm={0};
    int r=TB_INTERRUPTED;
    char activation[TB_HASH_LEN+1];
    if(!activation_plan(c,activation))goto done;
    if(load(c,c->tx.to,&b,&m))goto done;
    if(strcmp(c->tx.from,"-") && load(c,c->tx.from,&oldb,&oldm))goto done;
    char *versions=tam_path_join(c->root,"usr/lib/tamlinux/version");
    char *product=versions?tam_path_join(versions,c->tx.to):NULL; free(versions);
    int compatible=product?tb_contract_check(c->root,product,&b,false):-1; free(product);
    if(compatible || tb_native_conflicts(c->root,&m,&oldm))goto done;
    if(!strcmp(c->tx.state,"ready")) {
        bool stopped=false;
        if(c->services){if(command(c,c->services,"stop")){command(c,c->services,"restart-old");
    goto done;
    }stopped=true;
    }
        int busy=tb_busy(c->root,c->tx.from,&oldm,false);
        if(busy>0 && yes)busy=tb_busy(c->root,c->tx.from,&oldm,true);
        if(busy || secure(c,&m,&oldm) || version_check(c,c->tx.to,&m)) {
            if(stopped && command(c,c->services,"restart-old"))goto done;
            r=busy>0?TB_OK:TB_FAIL;
    goto done;
        }
        if(state(c,"applying"))goto done;
    }
    if(!strcmp(c->tx.state,"applying")) {
        if(version_check(c,c->tx.to,&m) || mountcheck(c,&m) || mountcheck(c,&oldm) || resume_check(c,&m,&oldm,false))goto done;
        char stage[340];
    snprintf(stage,sizeof stage,"staging/%s",c->tx.id);
        int scratch=directory(c->slot,stage,false);
        if(scratch<0 || staged_check(c,&m,scratch)){
    if(scratch>=0)close(scratch);
    goto done;
    }
        int failed=0;
        for(size_t i=0;i<m.n && !failed;i++)if(!strncmp(m.v[i].path,"placed/",7)) {
            failed=replace(c,&m.v[i],scratch);
    tb_fault_file(i+1);
        }
        close(scratch);
    if(failed)goto done;
        for(size_t i=oldm.n;i>0;i--) {
            const tb_entry_t *e=&oldm.v[i-1];
    if(strncmp(e->path,"placed/",7) || tb_manifest_find(&m,e->path))continue;
            char *leaf=NULL;
    int p=parent(c->rootfd,e->path+7,false,&leaf);
            if(p<0){free(leaf);
    if(errno==ENOENT)continue;
    goto done;
    }
            int rr=0;
            if(e->type=='d'){if(unlinkat(p,leaf,AT_REMOVEDIR) && errno!=ENOENT && errno!=ENOTEMPTY)rr=-1;
    }
            else {
                char old[300];
    snprintf(old,sizeof old,"version/%s",c->tx.from);
    int fd=directory(c->slot,old,false);
    char backup[4096];
    location(backup,e);
                char *bl=NULL;
    int bp=fd<0?-1:parent(fd,backup,true,&bl);
                if(bp<0)rr=-1;
                else if(renameat(p,leaf,bp,bl) && errno!=ENOENT)rr=-1;
                else {tb_fault("v2-obsolete-rename");
    if(syncdir(bp))rr=-1;
    }
                if(bp>=0)close(bp);
    if(fd>=0)close(fd);
    free(bl);
            }
            if(!rr)rr=syncdir(p);
    close(p);
    free(leaf);
    if(rr)goto done;
        }
        char link[300];
    snprintf(link,sizeof link,"version/%s",c->tx.to);
        if(unlinkat(c->slot,".current.new",0) && errno!=ENOENT)goto done;
        if(symlinkat(link,c->slot,".current.new") || renameat(c->slot,".current.new",c->slot,"current"))goto done;
        tb_fault("v2-current-rename");
    if(syncdir(c->slot))goto done;
        if(atomic(c->state,"record",m.text,m.len))goto done;
        char keep[300];
    int n=snprintf(keep,sizeof keep,"format 1\nprevious %s\n",c->tx.from);
        strcpy(c->tx.activation,activation);
        if(atomic(c->state,"keep",keep,(size_t)n) || state(c,"activating") || syncall(c->slot))goto done;
    }
    if(!strcmp(c->tx.state,"activating")) {
        if(version_check(c,c->tx.to,&m) || resume_check(c,&m,&oldm,true))goto done;
        if(strcmp(c->tx.activation,activation)) {
            strcpy(c->tx.activation,activation);
            c->tx.done=0;
            if(state(c,"activating"))goto done;
        }
        if(c->tx.done>c->nsteps)goto done;
        for(size_t i=c->tx.done;i<c->nsteps;i++) {
            if(command(c,c->commands[i],"apply") && c->required[i]){r=TB_AFTER_FAILED;
    goto done;
    }
            c->tx.done=i+1;
    if(state(c,"activating"))goto done;
        }
        if(c->services && command(c,c->services,"restart-new")){r=TB_AFTER_FAILED;
    goto done;
    }
        if(state(c,"complete"))goto done;
    }
    r=TB_OK;
done:
    tb_bundle_free(&b);
    tb_bundle_free(&oldb);
    tb_manifest_free(&m);
    tb_manifest_free(&oldm);
    return r;
}
static int retained_contracts(context *c,bool native_hook) {
    int fd=directory(c->slot,"version",false);
    if(fd<0)return errno==ENOENT?0:-1;
    DIR *dir=fdopendir(fd);
    if(!dir){close(fd); return -1;}
    struct dirent *de; int result=0;
    while((de=readdir(dir))) {
        if(!identity(de->d_name) || de->d_name[0]=='.')continue;
        tb_bundle_t b={0}; tb_manifest_t m={0};
        char *versions=tam_path_join(c->root,"usr/lib/tamlinux/version");
        char *path=versions?tam_path_join(versions,de->d_name):NULL; free(versions);
        int r=!path || load(c,de->d_name,&b,&m) || tb_contract_check(c->root,path,&b,native_hook);
        printf("runtime: %s %s\n",de->d_name,r?"incompatible":"compatible");
        if(r)result=-1;
        free(path); tb_bundle_free(&b); tb_manifest_free(&m);
    }
    closedir(dir); return result;
}
int tb_v2_main(const char *root,const char *cmd,const char *arg,bool dry,bool if_pending,bool yes) {
    (void)if_pending;
    context c={.root=root,.rootfd=-1,.slot=-1,.state=-1,.lock=-1};
    int r=TB_FAIL;
    bool prepare_cmd=!strcmp(cmd,"install") || !strcmp(cmd,"prepare") || !strcmp(cmd,"restore");
    bool reading=!strcmp(cmd,"status") || !strcmp(cmd,"check");
    c.rootfd=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(c.rootfd<0)goto done;
    c.slot=directory(c.rootfd,"usr/lib/tamlinux",prepare_cmd && !dry);
    c.state=directory(c.rootfd,"var/lib/tamlinux",prepare_cmd && !dry);
    if(c.slot<0 || c.state<0){if(!prepare_cmd && errno==ENOENT)r=TB_OK;
    goto done;
    }
    if(!strcmp(cmd,"native-status")) {
        r=arg?TB_USAGE:retained_contracts(&c,true)?TB_FAIL:TB_OK;
        goto done;
    }
    if(reading || dry) {
        char cur[256];
    if(current(&c,cur))goto done;
        int have=readtx(&c);
    if(have<0){r=TB_INTERRUPTED;
    goto done;
    }
        if(print_status(&c,cur))goto done;
        if(have)printf("transaction: %s\nstate: %s\nfrom: %s\nto: %s\ncapture: -\n",c.tx.id,c.tx.state,c.tx.from,c.tx.to);
        r=have && strcmp(c.tx.state,"complete")?TB_INTERRUPTED:TB_OK;
        if(retained_contracts(&c,false) && r==TB_OK)r=TB_FAIL;
        if(!strcmp(cmd,"check") && strcmp(cur,"-")) {
            tb_bundle_t b={0};
    tb_manifest_t m={0};
            if(load(&c,cur,&b,&m) || version_check(&c,cur,&m))r=TB_FAIL;
            for(size_t i=0;i<m.n;i++)if(!strncmp(m.v[i].path,"placed/",7) && m.v[i].type=='f' && !matches(c.rootfd,m.v[i].path+7,&m.v[i]))r=TB_FAIL;
            tb_bundle_free(&b);
    tb_manifest_free(&m);
        }
        goto done;
    }
    if(!strcmp(cmd,"apply") || !strcmp(cmd,"recover")) {
        struct stat st;
        if(fstatat(c.state,"transaction",&st,AT_SYMLINK_NOFOLLOW) && errno==ENOENT) {
            r=TB_OK;
            goto done;
        }
    }
    char *statepath=tam_path_join(root,"var/lib/tamlinux/lock");
    c.lock=tam_fs_lock(statepath,5000);
    free(statepath);
    if(c.lock<0)goto done;
    if(profile(&c))goto done;
    if(prepare_cmd) {
        if(!arg){r=TB_USAGE;
    goto done;
    }
        struct stat st;
    if(lstat(arg,&st))goto done;
        if(S_ISDIR(st.st_mode))r=prepare(&c,arg);
        else {
            /* A workstation archive can hold private settings. Keep the
             * trusted copy outside public product state; only the manifest's
             * tree/placed payload is copied into system staging. */
            char temp[]="/tmp/tam-bundle-import-XXXXXX";
            if(!mkdtemp(temp))goto done;
            int private=open(temp,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
            int in=open(arg,O_RDONLY|O_NOFOLLOW|O_CLOEXEC),out=private<0?-1:openat(private,"incoming.tbundle",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
            int ok=in>=0 && out>=0 && !tam_fs_clone_or_copy(in,out,NULL) && !fsync(out);
            if(in>=0)close(in);
    if(out>=0)close(out);
            char *incoming=tam_path_join(temp,"incoming.tbundle");
    int fd=private<0?-1:directory(private,"incoming",true);
    if(fd>=0)fchmod(fd,0700);
    if(fd>=0)close(fd);
            char *dest=tam_path_join(temp,"incoming");
            if(ok && fd>=0 && !tb_archive_unpack(incoming,dest))r=prepare(&c,dest);
            if(private>=0)close(private);
            tb_remove_at(AT_FDCWD,"/",temp+1);
    free(incoming);
    free(dest);
        }
    } else if(!strcmp(cmd,"apply") || !strcmp(cmd,"recover")) {
        int have=arg?readtx(&c):0;
        if(arg && (have!=1 || strcmp(c.tx.id,arg))) {
            tb_err("prepared transaction changed; refusing to apply a different transaction");
            r=TB_INTERRUPTED;
        } else r=apply(&c,yes);
    }
    else if(!strcmp(cmd,"cancel")){int have=readtx(&c);
    r=have<0?TB_INTERRUPTED:have?cancel(&c):TB_OK;
    }
    else r=TB_USAGE;
done:
    if(r==TB_FAIL)tb_err("%s failed: %s",cmd,strerror(errno));
    if(c.lock>=0)tam_fs_unlock(c.lock);
    if(c.rootfd>=0)close(c.rootfd);
    if(c.slot>=0)close(c.slot);
    if(c.state>=0)close(c.state);
    for(size_t i=0;i<c.nallowed;i++)free(c.allowed[i]);
    free(c.allowed);
    for(size_t i=0;i<c.nsteps;i++){free(c.steps[i]);
    free(c.commands[i]);
    }free(c.steps);
    free(c.commands);
    free(c.required);
    free(c.services);
    return r;
}
