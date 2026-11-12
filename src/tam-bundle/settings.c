/* Private user settings. System files are read here, never written.
 * All home traversal is descriptor-relative and refuses symbolic links. */
#define _GNU_SOURCE
#include "tb.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <time.h>
#include <unistd.h>
#ifdef TAM_BUNDLE_TESTING
#include <signal.h>
#endif

#define SETTINGS_LIMIT (16U*1024U*1024U)
#define SETTINGS_FILES 16384U
#define METADATA_LIMIT (16U*1024U*1024U)
/* Metadata must be a bounded, stable regular file, even for local retry
 * records. Never allocate based on an archive's unbounded input length. */
static char *metadata_fd(int fd,size_t *len,size_t limit) {
    struct stat a,b;
    char *data=NULL;
    if(fstat(fd,&a) || !S_ISREG(a.st_mode) || a.st_size<0 || (uint64_t)a.st_size>limit)goto done;
    size_t n=(size_t)a.st_size,off=0;
    data=malloc(n+1);
    if(!data)goto done;
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
    data[n]=0; *len=n;
done:
    if(!data)errno=EINVAL;
    return data;
}
static char *metadata(int base,const char *path,size_t *len,size_t limit) {
    int fd=openat(base,path,O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0)return NULL;
    char *data=metadata_fd(fd,len,limit);
    int saved=errno; close(fd); errno=saved; return data;
}
static bool number_valid(const char *s,unsigned long max) {
    if(!*s || strspn(s,"0123456789")!=strlen(s))return false;
    errno=0; unsigned long n=strtoul(s,NULL,10);
    return !errno && n<=max;
}
static bool sid(const char *s) {
    return *s && strlen(s)<=240 && tam_path_is_safe_rel(s) &&
        strspn(s,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._+@-")==strlen(s);
}
static bool spath(const char *s) {
    return strlen(s)<4096 && tam_path_is_safe_rel(s) && !strchr(s,'\t') &&
        s[0]!=' ' && s[strlen(s)-1]!=' ';
}
typedef struct {
    char *path, *mark;
    char *presence, *origin, *base, *reason;
    unsigned mode;
    size_t line;
    char hash[TB_HASH_LEN+1];
    bool cleanup;
} setting;
typedef struct {
    setting *v;
    size_t n;
} settings;
typedef struct {
    int home, state, product;
    char *homepath, *productpath;
    char productid[256], host[TB_HASH_LEN+1];
    settings files;
    bool first_install;
} user_settings;

static int first_product(user_settings *u,const char *root);

static int ssync(int fd) {
    if(fsync(fd))return -1;
    tb_fault("settings-directory-sync");
    return 0;
}
static int sparent(int base,const char *path,bool create,char **leaf) {
    if(!tam_path_is_safe_rel(path)){errno=EINVAL; return -1;}
    char *copy=strdup(path), *last=copy?strrchr(copy,'/'):NULL;
    if(!copy)return -1;
    *leaf=strdup(last?last+1:copy);
    int fd=fcntl(base,F_DUPFD_CLOEXEC,0);
    if(last) {
        *last=0;
        char *save=NULL;
        for(char *p=strtok_r(copy,"/",&save);p && fd>=0;p=strtok_r(NULL,"/",&save)) {
            if(create) {
                int made=mkdirat(fd,p,0700);
                if((made && errno!=EEXIST) || (!made && ssync(fd))) {close(fd); fd=-1; break;}
            }
            int next=openat(fd,p,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
            close(fd); fd=next;
        }
    }
    free(copy);
    return fd;
}
static int sdir(int base,const char *path,bool create) {
    char *leaf=NULL;
    int p=sparent(base,path,create,&leaf), out=-1;
    if(p>=0) {
        int made=create?mkdirat(p,leaf,0700):-1;
        if(!create || (made && errno==EEXIST) || (!made && !ssync(p)))
            out=openat(p,leaf,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        close(p);
    }
    free(leaf);
    return out;
}
static int swrite(int base,const char *path,const void *data,size_t n,unsigned mode) {
    if(n>METADATA_LIMIT || ((!strcmp(path,"CAPTURE") || !strcmp(path,"transaction")) && n>4096)) {
        errno=EFBIG; return -1;
    }
    char *leaf=NULL;
    int p=sparent(base,path,true,&leaf);
    if(p<0){free(leaf); return -1;}
    char *temp=NULL;
    unsigned char random[16]; char suffix[33];
    if(getrandom(random,sizeof random,0)!=sizeof random){close(p); free(leaf); return -1;}
    for(size_t i=0;i<16;i++)snprintf(suffix+i*2,3,"%02x",random[i]);
    if(asprintf(&temp,".%s.%s",leaf,suffix)<0)temp=NULL;
    int fd=temp?openat(p,temp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600):-1;
    int r=fd<0?-1:0;
    for(size_t off=0;!r && off<n;) {
        ssize_t k=write(fd,(const char *)data+off,n-off);
        if(k<0 && errno==EINTR)continue;
        if(k<=0)r=-1; else off+=(size_t)k;
    }
    if(!r && (fchmod(fd,mode) || fsync(fd)))r=-1;
    if(fd>=0)close(fd);
    if(!r)tb_fault("settings-file-sync");
    if(!r && renameat(p,temp,p,leaf))r=-1;
    if(!r){tb_fault("settings-record-rename"); r=ssync(p);}
    if(r && temp)unlinkat(p,temp,0);
    close(p); free(leaf); free(temp);
    return r;
}
static void shash(const void *data,size_t n,char out[TB_HASH_LEN+1]) {
    tam_sha256_t ctx; uint8_t d[TAM_SHA256_LEN];
    tam_sha256_init(&ctx); tam_sha256_update(&ctx,data,n); tam_sha256_final(&ctx,d);
    memcpy(out,"sha256:",7); tam_sha256_hex(d,out+7);
}
static bool secret_path(const char *path) {
    const char *paths[]={".ssh",".gnupg",".codex",".gemini",".local/share/keyrings",
                         ".config/gh/hosts.yml",".claude/.credentials.json",".claude/credentials.json",
                         ".local/state/tamlinux"};
    for(size_t i=0;i<sizeof paths/sizeof *paths;i++) {
        size_t n=strlen(paths[i]);
        if(!strncmp(path,paths[i],n) && (!path[n] || path[n]=='/'))return true;
    }
    return false;
}
static size_t secret_line(const char *data,size_t n) {
    const char *patterns[]={"-----BEGIN PRIVATE KEY-----","-----BEGIN RSA PRIVATE KEY-----",
        "-----BEGIN OPENSSH PRIVATE KEY-----","-----BEGIN EC PRIVATE KEY-----",
        "ghp_","gho_","ghu_","ghs_","ghr_","github_pat_","sk-","xoxb-","xoxp-","xapp-","AKIA","ASIA"};
    size_t first=n;
    for(size_t i=0;i<sizeof patterns/sizeof *patterns;i++) {
        const char *p=memmem(data,n,patterns[i],strlen(patterns[i]));
        if(p && (size_t)(p-data)<first)first=(size_t)(p-data);
    }
    for(size_t off=0;off<n;) {
        const char *nl=memchr(data+off,'\n',n-off);
        size_t len=nl?(size_t)(nl-data-off):n-off;
        char *line=strndup(data+off,len);
        if(!line)return 1;
        for(char *p=line;*p;p++)*p=(char)tolower((unsigned char)*p);
        const char *words[]={"password","token"};
        for(size_t i=0;i<2;i++) {
            char *p=strstr(line,words[i]);
            if(p) {
                p+=strlen(words[i]);
                while(*p==' ' || *p=='\t' || *p=='\"' || *p=='\'')p++;
                if(*p=='=' || *p==':')if(off<first)first=off;
            }
        }
        free(line); off+=len+(nl?1:0);
    }
    if(first==n)return 0;
    size_t line=1;
    for(size_t i=0;i<first;i++)if(data[i]=='\n')line++;
    return line;
}
/* 0 present, 1 absent, 2 excluded. Recheck the opened file and its path so
 * a concurrent atomic save cannot silently capture an obsolete inode. */
static int sread(int base,const char *path,char **data,size_t *len,unsigned *mode,const char **reason) {
    *data=NULL; *len=0; *mode=0;
    if(secret_path(path)){*reason="secret-path"; return 2;}
    for(int attempt=0;attempt<3;attempt++) {
        int fd=tb_open_beneath(base,path);
        if(fd<0){if(errno==ENOENT)return 1; *reason="unreadable-or-link"; return 2;}
        struct stat a,b,c;
        if(fstat(fd,&a) || !S_ISREG(a.st_mode) || a.st_size<0 || (uint64_t)a.st_size>SETTINGS_LIMIT) {
            close(fd); *reason="type-or-size"; return 2;
        }
        size_t size=(size_t)a.st_size, off=0;
        char *buf=malloc(size+1);
        if(!buf){close(fd); return -1;}
        while(off<size) {
            ssize_t k=read(fd,buf+off,size-off);
            if(k<0 && errno==EINTR)continue;
            if(k<=0)break;
            off+=(size_t)k;
        }
#ifdef TAM_BUNDLE_TESTING
        const char *barrier=getenv("TAM_BUNDLE_READ_BARRIER");
        if(barrier && !strcmp(path,barrier))raise(SIGSTOP);
#endif
        int check=tb_open_beneath(base,path);
        bool stable=off==size && !fstat(fd,&b) && check>=0 && !fstat(check,&c) &&
            a.st_dev==c.st_dev && a.st_ino==c.st_ino && a.st_size==b.st_size &&
            a.st_ctim.tv_sec==b.st_ctim.tv_sec && a.st_ctim.tv_nsec==b.st_ctim.tv_nsec &&
            a.st_ctim.tv_sec==c.st_ctim.tv_sec && a.st_ctim.tv_nsec==c.st_ctim.tv_nsec;
        close(fd); if(check>=0)close(check);
        if(stable){buf[size]=0; *data=buf; *len=size; *mode=(unsigned)(a.st_mode&0777); return 0;}
        free(buf);
    }
    *reason="changed-during-read"; return 2;
}
static void sfree(settings *s) {
    for(size_t i=0;i<s->n;i++) {
        setting *e=&s->v[i];
        free(e->path); free(e->mark); free(e->presence); free(e->origin); free(e->base); free(e->reason);
    }
    free(s->v); memset(s,0,sizeof *s);
}
static setting *sadd(settings *s,const char *path,const char *mark) {
    if(!spath(path))return NULL;
    for(size_t i=0;i<s->n;i++)if(!strcmp(s->v[i].path,path)) {
        free(s->v[i].mark); s->v[i].mark=strdup(mark); return &s->v[i];
    }
    if(s->n>=SETTINGS_FILES)return NULL;
    setting *v=realloc(s->v,(s->n+1)*sizeof *v);
    if(!v)return NULL;
    s->v=v;
    setting *e=&v[s->n++]; memset(e,0,sizeof *e);
    e->path=strdup(path); e->mark=strdup(mark);
    return e->path && e->mark?e:NULL;
}
static int gather(int base,const char *path,const char *mark,settings *s) {
    size_t depth=0;
    for(const char *p=path;*p;p++)if(*p=='/')depth++;
    if(depth>64 || !spath(path))return -1;
    if(secret_path(path))return sadd(s,path,mark)?0:-1;
    int fd=tb_open_beneath(base,path);
    if(fd<0)return errno==ENOENT?0:(sadd(s,path,mark)?0:-1);
    struct stat st;
    if(fstat(fd,&st)){close(fd); return -1;}
    if(!S_ISDIR(st.st_mode)){close(fd); return sadd(s,path,mark)?0:-1;}
    DIR *d=fdopendir(fd);
    if(!d){close(fd); return -1;}
    int r=0; struct dirent *de;
    while(!r && (de=readdir(d))) {
        if(!strcmp(de->d_name,".") || !strcmp(de->d_name,".."))continue;
        char *p=tam_path_join(path,de->d_name);
        r=p?gather(base,p,mark,s):-1; free(p);
    }
    closedir(d); return r;
}
typedef struct { user_settings *u; int defaults; } list_reader;
static int listline(void *arg,size_t line,int fmt,char **f,size_t n) {
    (void)line; (void)fmt;
    list_reader *r=arg;
    if(n!=2 || (strcmp(f[0],"common") && strcmp(f[0],"host")))return 1;
    char *path=strdup(f[1]);
    if(!path)return 1;
    size_t len=strlen(path); bool directory=len && path[len-1]=='/';
    if(directory)path[--len]=0;
    int result=!tam_path_is_safe_rel(path);
    if(!result && directory) {
        result=gather(r->u->home,path,f[0],&r->u->files);
        if(!result && r->defaults>=0)result=gather(r->defaults,path,f[0],&r->u->files);
    } else if(!result)result=sadd(&r->u->files,path,f[0])?0:-1;
    free(path); return result?1:0;
}
static int read_list(user_settings *u,int product) {
    size_t n=0;
    char *text=metadata(product,"SETTINGS",&n,METADATA_LIMIT);
    int defaults=tb_open_beneath(product,"defaults/home");
    list_reader r={u,defaults};
    int result=text?tam_rec_parse(text,n,2,1,listline,&r,NULL):-1;
    free(text);
    if(!result) {
        text=metadata(u->state,"settings.list",&n,METADATA_LIMIT);
        if(text){result=tam_rec_parse(text,n,2,1,listline,&r,NULL); free(text);}
        else if(errno!=ENOENT)result=-1;
    }
    if(defaults>=0)close(defaults);
    return result;
}
static int ecmp(const void *a,const void *b) {return strcmp(((const setting *)a)->path,((const setting *)b)->path);}
static void user_close(user_settings *u) {
    if(u->home>=0)close(u->home);
    if(u->state>=0)close(u->state);
    if(u->product>=0)close(u->product);
    free(u->homepath); free(u->productpath); sfree(&u->files);
}
static int user_lock(user_settings *u) {
    char *path=tam_path_join(u->homepath,".local/state/tamlinux/lock");
    int fd=path?tam_fs_lock(path,5000):-1; free(path); return fd;
}
static int user_open(user_settings *u,const char *root,bool allow_first) {
    memset(u,0,sizeof *u); u->home=u->state=u->product=-1;
    const char *home=getenv("HOME");
    if(!home || *home!='/' || (!strcmp(root,"/") && !geteuid())) {
        tb_err("settings must run as the selected user with an absolute HOME"); return -1;
    }
    u->home=open(home,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    struct stat st;
    if(u->home<0 || fstat(u->home,&st) || st.st_uid!=geteuid())return -1;
    u->homepath=realpath(home,NULL);
    char *public=tam_path_join(root,"var/lib/tamlinux");
    char *canonical=public?realpath(public,NULL):NULL;
    free(public);
    bool bad=canonical && u->homepath && !strncmp(u->homepath,canonical,strlen(canonical)) &&
        (!u->homepath[strlen(canonical)] || u->homepath[strlen(canonical)]=='/');
    free(canonical);
    if(bad){tb_err("private settings cannot live under public system state"); return -1;}
    u->state=sdir(u->home,".local/state/tamlinux",true);
    if(u->state<0 || fstat(u->state,&st) || st.st_uid!=geteuid() || fchmod(u->state,0700))return -1;
    char *current=tam_path_join(root,"usr/lib/tamlinux/current");
    if(!current)return -1;
    u->productpath=realpath(current,NULL);
    if(!u->productpath) {
        bool absent=lstat(current,&st)<0 && errno==ENOENT;
        if(!allow_first || !absent || first_product(u,root)){free(current); return -1;}
    }
    free(current);
    if(u->product>=0)close(u->product);
    u->product=open(u->productpath,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    tb_bundle_t b={0}; tb_manifest_t m={0};
    if(u->product<0 || tb_tree_read(AT_FDCWD,u->productpath,&b,&m))return -1;
    snprintf(u->productid,sizeof u->productid,"%s",u->first_install?"-":b.id);
    tb_bundle_free(&b); tb_manifest_free(&m);
    char *hostpath=tam_path_join(root,"var/lib/tamlinux/host-id");
    size_t n=0; char *host=hostpath?metadata(AT_FDCWD,hostpath,&n,128):NULL; free(hostpath);
    if(host && n && host[n-1]=='\n')host[--n]=0;
    bool ok=host && n==TB_HASH_LEN && !strncmp(host,"sha256:",7) && strspn(host+7,"0123456789abcdef")==64;
    if(ok)snprintf(u->host,sizeof u->host,"%s",host);
    free(host);
    if(!ok){tb_err("a recorded system host-id is required before capturing settings"); return -1;}
    return 0;
}
static tam_str_t files_text(const settings *s) {
    tam_str_t text=tam_str_new("format 1\n");
    for(size_t i=0;i<s->n && text;i++) {
        const setting *e=&s->v[i];
        char mode[8],line[32]; snprintf(mode,sizeof mode,"%04o",e->mode); snprintf(line,sizeof line,"%zu",e->line);
        const char *f[]={e->mark,e->presence,mode,*e->hash?e->hash:"-",e->origin,e->base,e->reason,line,e->path};
        text=tam_rec_cat(text,f,9);
    }
    return text;
}
static int snapshot(user_settings *u,char id[256]) {
    if(!u->files.n && read_list(u,u->product))return -1;
    if(u->files.n>1)qsort(u->files.v,u->files.n,sizeof *u->files.v,ecmp);
    unsigned char random[16];
    if(getrandom(random,sizeof random,0)!=sizeof random)return -1;
    char stage[80]=".capture-";
    for(size_t i=0;i<sizeof random;i++)snprintf(stage+9+i*2,3,"%02x",random[i]);
    int fd=sdir(u->state,stage,true), defaults=tb_open_beneath(u->product,"defaults/home");
    if(fd<0){if(defaults>=0)close(defaults); return -1;}
    bool complete=true; int result=-1;
    for(size_t i=0;i<u->files.n;i++) {
        setting *e=&u->files.v[i];
        char *data=NULL; size_t len=0; const char *reason="-";
        int presence=sread(u->home,e->path,&data,&len,&e->mode,&reason);
        if(presence<0)goto done;
        e->line=presence==0?secret_line(data,len):0;
        if(e->line){presence=2; reason="secret-pattern";}
        e->presence=strdup(presence==0?"present":presence==1?"absent":"excluded");
        e->base=strdup(u->productid); e->reason=strdup(reason);
        e->origin=strdup("-");
        if(presence==0) {
            if(!strcmp(e->mark,"common") &&
               (memmem(data,len,"DP-",3) || memmem(data,len,"HDMI-A-",7)))
                tb_note("settings look host-specific: %s (marked common; consider splitting the file)",e->path);
            shash(data,len,e->hash);
            char *baseline=NULL; size_t bn=0; unsigned bm=0; const char *br="-";
            int p=defaults<0?1:sread(defaults,e->path,&baseline,&bn,&bm,&br);
            bool unchanged=p==0 && bn==len && bm==e->mode && !memcmp(data,baseline,len);
            free(baseline); free(e->origin); e->origin=strdup(unchanged?"default":"edited");
            char *path=tam_path_join("files",e->path);
            int r=path?swrite(fd,path,data,len,0600):-1; free(path); free(data); data=NULL;
            if(r)goto done;
        }
        free(data);
        if(presence==2){complete=false; tb_note("settings excluded: %s (%s, line %zu)",e->path,reason,e->line);}
        tb_fault_file(i+1);
    }
    tam_str_t files=files_text(&u->files);
    struct timespec now; struct tm tm; char date[32],hostname[256],user[32];
    clock_gettime(CLOCK_REALTIME,&now); gmtime_r(&now.tv_sec,&tm);
    strftime(date,sizeof date,"%Y%m%dT%H%M%SZ",&tm);
    if(gethostname(hostname,sizeof hostname))strcpy(hostname,"unknown");
    hostname[100]=0;
    for(char *p=hostname;*p;p++)if(!isalnum((unsigned char)*p) && *p!='-' && *p!='.')*p='_';
    snprintf(user,sizeof user,"%lu",(unsigned long)getuid());
    tam_str_t header=tam_str_new("format 1\n");
    header=tam_str_cat_printf(header,"user %s\nhost %s\nhost-id %s\nproduct %s\ncreated %s.%09ld\ncomplete %s\n",
        user,hostname,u->host,u->productid,date,now.tv_nsec,complete?"yes":"no");
    char hash[TB_HASH_LEN+1];
    if(!files || !header){tam_str_free(files); tam_str_free(header); goto done;}
    tam_str_t combined=tam_str_dup(header); combined=tam_str_cat(combined,files);
    shash(combined,tam_str_len(combined),hash); tam_str_free(combined);
    header=tam_str_cat_printf(header,"hash %s\n",hash);
    snprintf(id,256,"%s@%.100s-%s-%.12s",user,hostname,date,hash+7);
    int r=swrite(fd,"FILES",files,tam_str_len(files),0600) || swrite(fd,"CAPTURE",header,tam_str_len(header),0600) || ssync(fd);
    tam_str_free(files); tam_str_free(header);
    if(r)goto done;
    int captures=sdir(u->state,"captures",true);
    if(captures<0)goto done;
    r=renameat(u->state,stage,captures,id);
    if(!r){tb_fault("settings-capture-rename"); r=ssync(captures) || ssync(u->state);}
    close(captures);
    if(!r){tb_note("private settings capture: %s%s",id,complete?"":" (incomplete)"); result=0;}
done:
    if(defaults>=0)close(defaults);
    close(fd);
    return result;
}
int tb_settings_capture(const char *root) {
    user_settings u;
    int r=user_open(&u,root,false);
    char id[256];
    if(!r)r=snapshot(&u,id);
    user_close(&u);
    return r?TB_FAIL:TB_OK;
}
int tb_settings_host(const char *root,int state) {
    struct stat st;
    if(!fstatat(state,"host-id",&st,AT_SYMLINK_NOFOLLOW))return S_ISREG(st.st_mode)?0:-1;
    if(errno!=ENOENT)return -1;
    char *path=tam_path_join(root,"sys/class/dmi/id/product_uuid");
    size_t n=0; char *uuid=path?tb_read_at(AT_FDCWD,path,&n):NULL; free(path);
    if(uuid)while(n && isspace((unsigned char)uuid[n-1]))uuid[--n]=0;
    bool valid=uuid && n==36, nonzero=false, nonf=false;
    for(size_t i=0;valid && i<n;i++) {
        uuid[i]=(char)tolower((unsigned char)uuid[i]);
        if(i==8 || i==13 || i==18 || i==23){if(uuid[i]!='-')valid=false; continue;}
        if(!isxdigit((unsigned char)uuid[i]))valid=false;
        if(uuid[i]!='0')nonzero=true;
        if(uuid[i]!='f')nonf=true;
    }
    const char *placeholders[]={"03000200-0400-0500-0006-000700080009","00020003-0004-0005-0006-000700080009",
                                "12345678-1234-1234-1234-123456789abc"};
    for(size_t i=0;valid && i<sizeof placeholders/sizeof *placeholders;i++)
        if(!strcmp(uuid,placeholders[i]))valid=false;
    char hash[TB_HASH_LEN+2];
    if(valid && nonzero && nonf) {
        char input[80]; int len=snprintf(input,sizeof input,"tamlinux:host:v1\n%s",uuid);
        shash(input,(size_t)len,hash);
    } else {
        unsigned char random[32];
        if(getrandom(random,sizeof random,0)!=sizeof random){free(uuid); return -1;}
        shash(random,sizeof random,hash);
        tb_note("host-id: using a persistent random id; no usable DMI UUID");
    }
    free(uuid);
    strcat(hash,"\n");
    return swrite(state,"host-id",hash,strlen(hash),0644);
}

typedef struct {
    char *user,*hostname,*host,*product,*created,*complete,*hash;
} capture_header;
static void capture_free(capture_header *h) {
    free(h->user); free(h->hostname); free(h->host); free(h->product);
    free(h->created); free(h->complete); free(h->hash); memset(h,0,sizeof *h);
}
static bool shash_valid(const char *hash) {
    return strlen(hash)==TB_HASH_LEN && !strncmp(hash,"sha256:",7) && strspn(hash+7,"0123456789abcdef")==64;
}
static int captureline(void *arg,size_t line,int fmt,char **f,size_t n) {
    (void)line; (void)fmt;
    capture_header *h=arg;
    if(n!=2)return 1;
    const char *keys[]={"user","host","host-id","product","created","complete","hash"};
    char **slots[]={&h->user,&h->hostname,&h->host,&h->product,&h->created,&h->complete,&h->hash};
    for(size_t i=0;i<7;i++)if(!strcmp(f[0],keys[i])) {
        if(*slots[i])return 1;
        *slots[i]=strdup(f[1]); return *slots[i]?0:1;
    }
    return 1;
}
static int filesline(void *arg,size_t line,int fmt,char **f,size_t n) {
    (void)line; (void)fmt;
    settings *s=arg;
    if(n!=9 || (strcmp(f[0],"common") && strcmp(f[0],"host")) ||
       (strcmp(f[1],"present") && strcmp(f[1],"absent") && strcmp(f[1],"excluded")) ||
       (strcmp(f[4],"default") && strcmp(f[4],"edited") && strcmp(f[4],"-")) ||
       !spath(f[8]) || !sid(f[5]) || strlen(f[2])!=4 || strspn(f[2],"01234567")!=4 ||
       !number_valid(f[7],SETTINGS_LIMIT+1))return 1;
    unsigned mode=(unsigned)strtoul(f[2],NULL,8);
    if(mode>0777 || (!strcmp(f[1],"present") && !shash_valid(f[3])) ||
       (strcmp(f[1],"excluded") && secret_path(f[8])))return 1;
    bool present=!strcmp(f[1],"present"), absent=!strcmp(f[1],"absent");
    if(present) {
        if(!strcmp(f[4],"-") || strcmp(f[6],"-") || strcmp(f[7],"0"))return 1;
    } else {
        if(strcmp(f[3],"-") || strcmp(f[4],"-"))return 1;
        if(absent && (mode || strcmp(f[6],"-") || strcmp(f[7],"0")))return 1;
        if(!absent) {
            const char *reasons[]={"secret-pattern","secret-path","unreadable-or-link","type-or-size","changed-during-read"};
            bool known=false;
            for(size_t i=0;i<5;i++)if(!strcmp(f[6],reasons[i]))known=true;
            if(!known || (!strcmp(f[6],"secret-pattern")? !strtoul(f[7],NULL,10):strcmp(f[7],"0")))return 1;
        }
    }
    if(s->n && strcmp(s->v[s->n-1].path,f[8])>=0)return 1;
    setting *e=sadd(s,f[8],f[0]);
    if(!e)return 1;
    e->presence=strdup(f[1]); e->mode=mode;
    snprintf(e->hash,sizeof e->hash,"%s",f[3]);
    e->origin=strdup(f[4]); e->base=strdup(f[5]); e->reason=strdup(f[6]);
    errno=0; unsigned long number=strtoul(f[7],NULL,10); e->line=number;
    return errno || !e->presence || !e->origin || !e->base || !e->reason;
}
static int capture_read(int fd,settings *s,capture_header *h) {
    size_t hn=0,fn=0;
    char *header=metadata(fd,"CAPTURE",&hn,4096), *files=metadata(fd,"FILES",&fn,METADATA_LIMIT);
    int r=-1;
    if(!header || !files || tam_rec_parse(header,hn,2,1,captureline,h,NULL) ||
       !h->user || !h->hostname || !h->host || !h->product || !h->created || !h->complete || !h->hash ||
       !number_valid(h->user,UINT_MAX) || !*h->hostname || strlen(h->hostname)>100 ||
       strspn(h->hostname,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-")!=strlen(h->hostname) ||
       !sid(h->product) || !strcmp(h->product,"-") || strlen(h->created)!=26 ||
       strspn(h->created,"0123456789")!=8 || h->created[8]!='T' ||
       strspn(h->created+9,"0123456789")!=6 || h->created[15]!='Z' || h->created[16]!='.' ||
       strspn(h->created+17,"0123456789")!=9 ||
       !shash_valid(h->host) || !shash_valid(h->hash) ||
       (strcmp(h->complete,"yes") && strcmp(h->complete,"no")) ||
       tam_rec_parse(files,fn,9,1,filesline,s,NULL))goto done;
    /* The hash line is omitted wherever it appears, preserving all other
     * original bytes, including record escaping. */
    tam_str_t body=tam_str_new(NULL);
    for(char *p=header,*end=header+hn;p<end;) {
        char *nl=memchr(p,'\n',(size_t)(end-p)),*next=nl?nl+1:end;
        if(strncmp(p,"hash ",5)) {
            char *part=strndup(p,(size_t)(next-p));
            body=tam_str_cat(body,part); free(part);
        }
        p=next;
    }
    body=tam_str_cat(body,files);
    char hash[TB_HASH_LEN+1]; shash(body,tam_str_len(body),hash); tam_str_free(body);
    if(strcmp(hash,h->hash))goto done;
    bool complete=true;
    for(size_t i=0;i<s->n;i++) {
        setting *e=&s->v[i];
        if(strcmp(e->base,h->product))goto done;
        if(!strcmp(e->presence,"excluded"))complete=false;
        if(strcmp(e->presence,"present"))continue;
        char *path=tam_path_join("files",e->path),*data=NULL;
        size_t len=0; unsigned mode=0; const char *reason="-";
        int ok=path?sread(fd,path,&data,&len,&mode,&reason):-1; free(path);
        if(!ok)shash(data,len,hash);
        free(data);
        if(ok || strcmp(hash,e->hash))goto done;
    }
    if(strcmp(h->complete,complete?"yes":"no"))goto done;
    r=0;
done:
    free(header); free(files);
    if(r)tb_err("invalid or corrupt private settings capture");
    return r;
}
typedef struct {
    char id[65], system[65], from[256], to[256], capture[256], automatic[256], state[16], kind[32], activation[TB_HASH_LEN+1];
    char changes[TB_HASH_LEN+1];
    size_t done;
    unsigned fields;
} settings_tx;
static int stxline(void *arg,size_t line,int fmt,char **f,size_t n) {
    (void)line; (void)fmt;
    settings_tx *t=arg;
    if(n!=2)return 1;
    const char *keys[]={"id","from","to","capture","automatic","state","kind","done","system","activation","changes"};
    char *slots[]={t->id,t->from,t->to,t->capture,t->automatic,t->state,t->kind,NULL,t->system,t->activation,t->changes};
    size_t caps[]={sizeof t->id,sizeof t->from,sizeof t->to,sizeof t->capture,sizeof t->automatic,sizeof t->state,sizeof t->kind,0,sizeof t->system,sizeof t->activation,sizeof t->changes};
    for(size_t i=0;i<11;i++)if(!strcmp(f[0],keys[i])) {
        if(t->fields&(1U<<i))return 1;
        t->fields|=1U<<i;
        if(i==7) {
            if(!number_valid(f[1],128))return 1;
            errno=0; t->done=strtoul(f[1],NULL,10); return errno!=0;
        }
        if(strlen(f[1])>=caps[i] || (i>=9? (strcmp(f[1],"-") && !shash_valid(f[1])):!sid(f[1])))return 1;
        strcpy(slots[i],f[1]); return 0;
    }
    return 1;
}
static int tx_read_record(int fd,const char *record,settings_tx *t) {
    memset(t,0,sizeof *t);
    size_t n; char *text=metadata(fd,record,&n,4096);
    if(!text)return errno==ENOENT?0:-1;
    int r=tam_rec_parse(text,n,2,1,stxline,t,NULL); free(text);
    if(r || t->fields!=2047 || strlen(t->id)!=32 || strspn(t->id,"0123456789abcdef")!=32 ||
       strlen(t->system)!=32 || strspn(t->system,"0123456789abcdef")!=32 ||
       (strcmp(t->kind,"upgrade") && strcmp(t->kind,"restore") && strcmp(t->kind,"settings-only")) ||
       (strcmp(t->state,"ready") && strcmp(t->state,"staging") && strcmp(t->state,"applying") &&
        strcmp(t->state,"activating") && strcmp(t->state,"complete")))return -1;
    bool only=!strcmp(t->kind,"settings-only"), upgrade=!strcmp(t->kind,"upgrade");
    bool early=!strcmp(t->state,"ready") || !strcmp(t->state,"staging") || !strcmp(t->state,"applying");
    if(!strcmp(t->to,"-") || (only?strcmp(t->from,t->to):(!strcmp(t->from,t->to) || strcmp(t->id,t->system))) ||
       (upgrade?strcmp(t->capture,"-"):!strcmp(t->capture,"-")) ||
       (early && (t->done || strcmp(t->activation,"-"))) ||
       (!strcmp(t->state,"ready") && strcmp(t->automatic,"-")) ||
       ((!strcmp(t->state,"applying") || !strcmp(t->state,"activating") || !strcmp(t->state,"complete")) && !strcmp(t->automatic,"-")) ||
       ((!strcmp(t->state,"complete") || t->done) && !shash_valid(t->activation)))return -1;
    bool planning=!strcmp(t->state,"ready") || !strcmp(t->state,"staging");
    if(planning?strcmp(t->changes,"-"):!shash_valid(t->changes))return -1;
    return 1;
}
static int tx_read(int fd,settings_tx *t) {
    return tx_read_record(fd,"transaction",t);
}
static int tx_write(int fd,settings_tx *t,const char *state) {
    snprintf(t->state,sizeof t->state,"%s",state);
    tam_str_t text=tam_str_new("format 1\n");
    text=tam_str_cat_printf(text,"id %s\nsystem %s\nfrom %s\nto %s\ncapture %s\nautomatic %s\nkind %s\nstate %s\ndone %zu\nactivation %s\nchanges %s\n",
        t->id,t->system,t->from,t->to,t->capture,t->automatic,t->kind,t->state,t->done,t->activation,t->changes);
    int r=text?swrite(fd,"transaction",text,tam_str_len(text),0600):-1; tam_str_free(text);
    if(!r){char point[64]; snprintf(point,sizeof point,"settings-state-%s",state); tb_fault(point);}
    return r;
}
typedef struct {char id[65],from[256],to[256],state[16]; unsigned fields; unsigned long uid;} system_tx;
static int systemline(void *arg,size_t line,int fmt,char **f,size_t n) {
    (void)line; (void)fmt;
    system_tx *t=arg;
    if(n!=2)return 1;
    const char *keys[]={"id","from","to","state","capture","uid","kind","done","activation"};
    char *slots[]={t->id,t->from,t->to,t->state};
    size_t caps[]={sizeof t->id,sizeof t->from,sizeof t->to,sizeof t->state};
    for(size_t i=0;i<9;i++)if(!strcmp(f[0],keys[i])) {
        if(t->fields&(1U<<i))return 1;
        t->fields|=1U<<i;
        if(i<4) {
            if(strlen(f[1])>=caps[i] || !sid(f[1]))return 1;
            strcpy(slots[i],f[1]); return 0;
        }
        if(i==4)return strcmp(f[1],"-")!=0;
        if(i==6)return strcmp(f[1],"product-only")!=0;
        if(i==8)return strcmp(f[1],"-") && !shash_valid(f[1]);
        if(!number_valid(f[1],i==5?UINT_MAX:ULONG_MAX))return 1;
        if(i==5)t->uid=strtoul(f[1],NULL,10);
        return 0;
    }
    return 1;
}
static int system_read_record(const char *root,bool previous,system_tx *t) {
    memset(t,0,sizeof *t);
    char *dir=tam_path_join(root,"var/lib/tamlinux");
    int fd=dir?open(dir,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC):-1; free(dir);
    if(fd<0)return -1;
    size_t n=0; char *text=metadata(fd,previous?"last-transaction":"transaction",&n,4096);
    if(!previous && !text && errno==ENOENT)text=metadata(fd,"last-transaction",&n,4096);
    int r=text?tam_rec_parse(text,n,2,1,systemline,t,NULL):-1;
    free(text); close(fd);
    if(r || (t->fields!=255 && t->fields!=511) || strlen(t->id)!=32 || strspn(t->id,"0123456789abcdef")!=32 ||
       !strcmp(t->to,"-") || !strcmp(t->from,t->to) ||
       (strcmp(t->state,"ready") && strcmp(t->state,"staging") && strcmp(t->state,"applying") &&
        strcmp(t->state,"activating") && strcmp(t->state,"complete")))return -1;
    return 0;
}
static int system_read(const char *root,system_tx *t) {
    return system_read_record(root,false,t);
}
static int set_product(user_settings *u,const char *root,const char *id) {
    if(!tam_path_is_safe_rel(id) || strchr(id,'/'))return -1;
    char *slot=tam_path_join(root,"usr/lib/tamlinux/version");
    char *path=slot?tam_path_join(slot,id):NULL; free(slot);
    tb_bundle_t b={0}; tb_manifest_t m={0};
    if(!path || tb_tree_read(AT_FDCWD,path,&b,&m)){free(path); return -1;}
    bool ok=!strcmp(b.id,id);
    tb_bundle_free(&b); tb_manifest_free(&m);
    int fd=ok?open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC):-1;
    if(fd<0){free(path); return -1;}
    if(u->product>=0)close(u->product);
    free(u->productpath); u->product=fd; u->productpath=path;
    snprintf(u->productid,sizeof u->productid,"%s",id); return 0;
}
static int first_product(user_settings *u,const char *root) {
    system_tx sys;
    /* A first install has no current product to read. Only a fully prepared
     * root-owned transaction for this user may select the retained source.
     * A dangling current link or an incomplete staging record never qualifies. */
    if(system_read(root,&sys) || strcmp(sys.from,"-") || sys.uid!=getuid() ||
       (strcmp(sys.state,"ready") && strcmp(sys.state,"applying") && strcmp(sys.state,"activating")) ||
       set_product(u,root,sys.to))return -1;
    u->first_install=true;
    return 0;
}
static int settings_plan(user_settings *u,const char *root,const char *capture,bool only) {
    settings_tx t,previous; int have=tx_read(u->state,&previous);
    if(have<0 || (only && have && strcmp(previous.state,"ready") && strcmp(previous.state,"complete")))return TB_INTERRUPTED;
    bool retain=have && !strcmp(previous.state,"complete");
    if(capture && (!tam_path_is_safe_rel(capture) || strchr(capture,'/') || strlen(capture)>=sizeof t.capture))return TB_USAGE;
    memset(&t,0,sizeof t); strcpy(t.capture,capture?capture:"-"); strcpy(t.automatic,"-"); strcpy(t.activation,"-");
    strcpy(t.changes,"-");
    if(only) {
        system_tx sys;
        if(system_read(root,&sys) || strcmp(sys.state,"complete") || strcmp(sys.to,u->productid) || sys.uid!=getuid())return TB_INTERRUPTED;
        strcpy(t.system,sys.id);
        unsigned char random[16]; if(getrandom(random,sizeof random,0)!=sizeof random)return TB_FAIL;
        for(size_t i=0;i<16;i++)snprintf(t.id+i*2,3,"%02x",random[i]);
        strcpy(t.from,u->productid); strcpy(t.to,u->productid); strcpy(t.kind,"settings-only");
    } else {
        system_tx sys;
        if(system_read(root,&sys) || sys.uid!=getuid() ||
           (strcmp(sys.state,"ready") && strcmp(sys.state,"applying") &&
            strcmp(sys.state,"activating") && strcmp(sys.state,"complete")) ||
           (strcmp(sys.from,u->productid) && strcmp(sys.to,u->productid)))return TB_INTERRUPTED;
        /* Retrying delivery must not discard an in-flight CHANGES plan or
         * the switch-time capture. The system id is the authority here. */
        if(have && !strcmp(previous.system,sys.id) && !strcmp(previous.id,sys.id) &&
           !strcmp(previous.from,sys.from) && !strcmp(previous.to,sys.to) &&
           !strcmp(previous.capture,capture?capture:"-"))return TB_OK;
        if(have && strcmp(previous.state,"ready") && strcmp(previous.state,"complete"))return TB_INTERRUPTED;
        strcpy(t.id,sys.id); strcpy(t.system,sys.id); strcpy(t.from,sys.from); strcpy(t.to,sys.to); strcpy(t.kind,capture?"restore":"upgrade");
    }
    /* Planning reads lists and identities but never captures or stages user
     * bytes. Everything is recomputed when apply-settings actually runs. */
    if(capture) {
        char *p=tam_path_join("captures",capture);
        int fd=p?sdir(u->state,p,false):-1; free(p);
        capture_header h={0}; settings s={0};
        int r=fd<0?-1:capture_read(fd,&s,&h);
        if(fd>=0)close(fd);
        if(!r && strcmp(h.product,t.to))tb_note("settings and software differ: capture %s, target %s",h.product,t.to);
        capture_free(&h); sfree(&s);
        if(r)return TB_FAIL;
    }
    if(retain) {
        size_t n=0;
        char *previous=metadata(u->state,"transaction",&n,4096);
        int saved=previous?swrite(u->state,"last-transaction",previous,n,0600):-1;
        free(previous);
        if(saved)return TB_FAIL;
    }
    if(tx_write(u->state,&t,"ready"))return TB_FAIL;
    tb_note("settings plan ready: %s; home files will be read at the switch",t.id);
    return TB_OK;
}

typedef struct {char *path,*before,*after; unsigned mode,beforemode; bool remove;} settings_change;
typedef struct {settings_change *v; size_t n;} settings_changes;
static void changes_free(settings_changes *c) {
    for(size_t i=0;i<c->n;i++){free(c->v[i].path); free(c->v[i].before); free(c->v[i].after);}
    free(c->v); memset(c,0,sizeof *c);
}
static int change_add(settings_changes *c,int stage,const setting *e,const char *path,const char *data,size_t n,unsigned mode,bool remove) {
    for(size_t i=0;i<c->n;i++)if(!strcmp(c->v[i].path,path))return -1;
    if(secret_path(path) || !spath(path) || c->n>=SETTINGS_FILES*2U)return -1;
    settings_change *v=realloc(c->v,(c->n+1)*sizeof *v);
    if(!v)return -1;
    c->v=v;
    settings_change *ch=&v[c->n++]; memset(ch,0,sizeof *ch);
    ch->path=strdup(path); ch->mode=remove?0:mode; ch->beforemode=e->mode; ch->remove=remove;
    ch->before=strdup(!strcmp(e->presence,"present")?e->hash:"-");
    char hash[TB_HASH_LEN+1]; if(remove)strcpy(hash,"-"); else shash(data,n,hash);
    ch->after=strdup(hash);
    if(!ch->path || !ch->before || !ch->after)return -1;
    if(!remove) {
        char *p=tam_path_join("files",path);
        int r=p?swrite(stage,p,data,n,0600):-1; free(p);
        if(r)return -1;
    }
    return 0;
}
static tam_str_t changes_text(const settings_changes *c) {
    tam_str_t text=tam_str_new("format 1\n");
    for(size_t i=0;i<c->n && text;i++) {
        const settings_change *ch=&c->v[i]; char mode[8],beforemode[8];
        snprintf(mode,sizeof mode,"%04o",ch->mode); snprintf(beforemode,sizeof beforemode,"%04o",ch->beforemode);
        const char *f[]={ch->remove?"remove":"write",mode,beforemode,ch->before,ch->after,ch->path};
        text=tam_rec_cat(text,f,6);
    }
    return text;
}
static int changesline(void *arg,size_t line,int fmt,char **f,size_t n) {
    (void)line; (void)fmt;
    settings_changes *c=arg;
    if(n!=6 || (strcmp(f[0],"write") && strcmp(f[0],"remove")) || strlen(f[1])!=4 ||
       strspn(f[1],"01234567")!=4 || strtoul(f[1],NULL,8)>0777 ||
       strlen(f[2])!=4 || strspn(f[2],"01234567")!=4 || strtoul(f[2],NULL,8)>0777 ||
       (strcmp(f[3],"-") && !shash_valid(f[3])) ||
       (!strcmp(f[0],"write")?!shash_valid(f[4]):strcmp(f[4],"-")) ||
       !spath(f[5]) || secret_path(f[5]) || c->n>=SETTINGS_FILES*2U ||
       (!strcmp(f[3],"-") && strcmp(f[2],"0000")) ||
       (!strcmp(f[0],"remove") && (!strcmp(f[3],"-") || strcmp(f[1],"0000"))))return 1;
    for(size_t i=0;i<c->n;i++)if(!strcmp(c->v[i].path,f[5]))return 1;
    settings_change *v=realloc(c->v,(c->n+1)*sizeof *v); if(!v)return 1; c->v=v;
    settings_change *ch=&v[c->n++]; memset(ch,0,sizeof *ch);
    ch->path=strdup(f[5]); ch->before=strdup(f[3]); ch->after=strdup(f[4]);
    ch->mode=(unsigned)strtoul(f[1],NULL,8); ch->beforemode=(unsigned)strtoul(f[2],NULL,8); ch->remove=!strcmp(f[0],"remove");
    return !ch->path || !ch->before || !ch->after;
}
static const setting *find_setting(const settings *s,const char *path) {
    for(size_t i=0;i<s->n;i++)if(!strcmp(s->v[i].path,path))return &s->v[i];
    return NULL;
}
static bool same_bytes(const char *a,size_t an,unsigned am,const char *b,size_t bn,unsigned bm) {
    return a && b && an==bn && am==bm && !memcmp(a,b,an);
}
static int compute_changes(user_settings *u,const char *root,settings_tx *t,int stage,settings_changes *changes) {
    settings saved={0}; capture_header header={0}; int capture=-1, old=-1, next=-1;
    int result=-1;
    if(set_product(u,root,t->to))goto done;
    next=tb_open_beneath(u->product,"defaults/home");
    if(read_list(u,u->product))goto done;
    if(strcmp(t->from,"-")) {
        if(set_product(u,root,t->from) || read_list(u,u->product))goto done;
        old=tb_open_beneath(u->product,"defaults/home");
    }
    if(strcmp(t->capture,"-")) {
        char *p=tam_path_join("captures",t->capture); capture=p?sdir(u->state,p,false):-1; free(p);
        if(capture<0 || capture_read(capture,&saved,&header))goto done;
        for(size_t i=0;i<saved.n;i++)if(!sadd(&u->files,saved.v[i].path,saved.v[i].mark))goto done;
    }
    if(capture<0) {
        size_t initial=u->files.n;
        for(size_t i=0;i<initial;i++) {
            char *path=NULL;
            if(asprintf(&path,"%s.tamlinux-new",u->files.v[i].path)<0)path=NULL;
            char *current=NULL,*side=NULL; size_t cn=0,sn=0; unsigned cm=0,sm=0; const char *why="-";
            int cp=sread(u->home,u->files.v[i].path,&current,&cn,&cm,&why);
            int sp=path?sread(u->home,path,&side,&sn,&sm,&why):-1;
            if(cp==0 && sp==0 && !secret_line(side,sn) && same_bytes(current,cn,cm,side,sn,sm) &&
               !find_setting(&u->files,path)) {
                char *mark=strdup(u->files.v[i].mark);
                setting *e=mark?sadd(&u->files,path,mark):NULL; free(mark);
                if(!e){free(current); free(side); free(path); goto done;}
                e->cleanup=true;
            }
            free(current); free(side); free(path);
        }
    }
    /* This is the automatic, switch-time capture, after all affected paths
     * are known and before staging or changing any home file. */
    if(snapshot(u,t->automatic) || tx_write(u->state,t,"staging"))goto done;
    for(size_t i=0;i<u->files.n;i++) {
        const setting *e=&u->files.v[i],*restore=find_setting(&saved,e->path);
        if(!strcmp(e->presence,"excluded")){tb_note("settings left alone: %s (%s)",e->path,e->reason); continue;}
        if(e->cleanup) {
            bool planned=false;
            for(size_t j=0;j<changes->n;j++)if(!strcmp(changes->v[j].path,e->path))planned=true;
            if(!planned && change_add(changes,stage,e,e->path,NULL,0,0,true))goto done;
            continue;
        }
        char *newdata=NULL,*olddata=NULL; size_t nn=0,on=0; unsigned nm=0,om=0; const char *why="-";
        int np=next<0?1:sread(next,e->path,&newdata,&nn,&nm,&why);
        int op=old<0?1:sread(old,e->path,&olddata,&on,&om,&why);
        if(np<0 || op<0){free(newdata); free(olddata); goto done;}
        bool remove=false, write=false, side=false;
        if(restore) {
            if(!strcmp(restore->presence,"excluded")) {
                tb_note("capture excluded; left alone: %s (%s)",e->path,restore->reason);
            } else if(!strcmp(restore->mark,"host") && strcmp(header.host,u->host)) {
                tb_note("different host; skipped host settings: %s",e->path);
                write=!strcmp(e->presence,"absent") && np==0;
            } else if(!strcmp(restore->presence,"absent"))remove=!strcmp(e->presence,"present");
            else {
                char *p=tam_path_join("files",restore->path); free(newdata); newdata=NULL;
                int rp=p?sread(capture,p,&newdata,&nn,&nm,&why):-1; free(p);
                if(rp){free(newdata); free(olddata); goto done;}
                nm=restore->mode; write=true;
            }
        } else if(capture>=0) {
            /* A restore affects only paths its capture explicitly records. */
        } else if(!strcmp(e->presence,"absent"))write=np==0;
        else {
            char oldhash[TB_HASH_LEN+1]="";
            if(op==0)shash(olddata,on,oldhash);
            bool unedited=op==0 && !strcmp(oldhash,e->hash) && om==e->mode;
            bool changed=op!=np || (op==0 && !same_bytes(olddata,on,om,newdata,nn,nm));
            if(unedited){write=np==0 && changed; remove=np==1;}
            else if(changed && np==0)side=true;
            else if(np==1)tb_note("edited settings kept after default removal: %s",e->path);
        }
        if((write || side) && secret_line(newdata,nn)) {
            tb_note("new settings excluded: %s (secret-pattern)",e->path); write=side=false;
        }
        int r=0;
        if(side) {
            char *path=NULL; if(asprintf(&path,"%s.tamlinux-new",e->path)<0)path=NULL;
            char *existing=NULL; size_t en=0; unsigned em=0; const char *reason="-";
            int ep=path?sread(u->home,path,&existing,&en,&em,&reason):-1;
            const setting *managed=path?find_setting(&u->files,path):NULL;
            if(ep==1) {
                setting missing={.presence="absent"};
                r=change_add(changes,stage,&missing,path,newdata,nn,nm,false);
            } else if(ep==0 && managed && managed->cleanup) {
                r=change_add(changes,stage,managed,path,newdata,nn,nm,false);
            } else if(ep!=0 || !same_bytes(existing,en,em,newdata,nn,nm)) {
                tb_err("existing .tamlinux-new differs; resolve it before retrying: %s",path?path:e->path); r=-1;
            }
            free(existing);
            if(!r)tb_note("new default beside edited settings: %s",path);
            free(path);
        } else if(write || remove)r=change_add(changes,stage,e,e->path,newdata,nn,nm,remove);
        free(newdata); free(olddata);
        if(r)goto done;
    }
    tam_str_t text=changes_text(changes);
    result=text?swrite(stage,"CHANGES",text,tam_str_len(text),0600):-1;
    if(!result)shash(text,tam_str_len(text),t->changes);
    tam_str_free(text);
done:
    if(capture>=0)close(capture);
    if(old>=0)close(old);
    if(next>=0)close(next);
    sfree(&saved); capture_free(&header);
    return result;
}
static int live_hash(int home,const char *path,char hash[TB_HASH_LEN+1],unsigned *mode) {
    char *data=NULL; size_t n=0; const char *reason="-";
    int p=sread(home,path,&data,&n,mode,&reason);
    if(p==0)shash(data,n,hash);
    free(data);
    if(p==1){strcpy(hash,"-"); return 0;}
    return p==0?0:-1;
}
static int check_changes(user_settings *u,int stage,const settings_changes *changes) {
    struct statx fs, dest;
    if(statx(stage,"",AT_EMPTY_PATH,STATX_MNT_ID,&fs) || !(fs.stx_mask&STATX_MNT_ID))return -1;
    for(size_t i=0;i<changes->n;i++) {
        const settings_change *ch=&changes->v[i]; char hash[TB_HASH_LEN+1]; unsigned mode=0;
        if(live_hash(u->home,ch->path,hash,&mode) ||
           !((!strcmp(hash,ch->before) && (!strcmp(hash,"-") || mode==ch->beforemode)) ||
             (!strcmp(hash,ch->after) && (!strcmp(hash,"-") || mode==ch->mode)))) {
            tb_err("settings changed outside transaction: %s",ch->path); return -1;
        }
        char *path=strdup(ch->path); int fd=-1;
        while(path && (fd=tb_open_beneath(u->home,path))<0 && errno==ENOENT) {
            char *slash=strrchr(path,'/'); if(!slash)break; *slash=0;
        }
        if(fd<0 && path && !strchr(path,'/') && errno==ENOENT)fd=fcntl(u->home,F_DUPFD_CLOEXEC,0);
        free(path);
        int r=fd<0?-1:statx(fd,"",AT_EMPTY_PATH,STATX_MNT_ID,&dest); if(fd>=0)close(fd);
        if(r || !(dest.stx_mask&STATX_MNT_ID) || fs.stx_mnt_id!=dest.stx_mnt_id) {
            tb_err("settings path is on another mount: %s",ch->path); return -1;
        }
        if(!ch->remove && strcmp(hash,ch->after)) {
            char *p=tam_path_join("files",ch->path),*data=NULL; size_t n=0; unsigned m=0; const char *why="-";
            r=p?sread(stage,p,&data,&n,&m,&why):-1; free(p);
            if(!r)shash(data,n,hash);
            free(data);
            if(r || strcmp(hash,ch->after)){tb_err("staged settings changed: %s",ch->path); return -1;}
        }
    }
    return 0;
}
static int commit_changes(user_settings *u,int stage,const settings_changes *changes) {
    if(check_changes(u,stage,changes))return -1;
    for(size_t i=0;i<changes->n;i++) {
        const settings_change *ch=&changes->v[i]; char hash[TB_HASH_LEN+1]; unsigned mode=0;
        if(live_hash(u->home,ch->path,hash,&mode))return -1;
        if(!strcmp(hash,ch->after) && (ch->remove || mode==ch->mode))continue;
        char *leaf=NULL; int p=sparent(u->home,ch->path,!ch->remove,&leaf);
        if(p<0){free(leaf); return -1;}
        int r=0;
        if(ch->remove) {
            r=unlinkat(p,leaf,0);
            if(!r)tb_fault("settings-absent-unlink");
        } else {
            char *path=tam_path_join("files",ch->path),*source=NULL;
            int sp=path?sparent(stage,path,false,&source):-1; free(path);
            int fd=sp<0?-1:openat(sp,source,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
            r=fd<0?-1:(fchmod(fd,ch->mode) || fsync(fd));
            if(fd>=0)close(fd);
            if(!r)tb_fault("settings-file-sync");
            if(!r)r=renameat(sp,source,p,leaf);
            if(!r){tb_fault("settings-file-rename"); r=ssync(sp);}
            if(sp>=0)close(sp);
            free(source);
        }
        if(!r)r=ssync(p);
        close(p); free(leaf);
        if(r)return -1;
        tb_fault_file(i+1);
    }
    return 0;
}
typedef struct {char **names,**commands; bool *required; size_t n;} user_steps;
static int userstepline(void *arg,size_t line,int fmt,char **f,size_t n) {
    (void)line; (void)fmt;
    user_steps *s=arg;
    if(n && strcmp(f[0],"user-step"))
        return strcmp(f[0],"allow") && strcmp(f[0],"services") && strcmp(f[0],"step");
    if(n!=4 || !tb_valid_name(f[1]) || (strcmp(f[2],"required") && strcmp(f[2],"optional")) || !spath(f[3]) || s->n>=128)return 1;
    for(size_t i=0;i<s->n;i++)if(!strcmp(s->names[i],f[1]))return 1;
    char **names=realloc(s->names,(s->n+1)*sizeof *s->names);
    if(!names)return 1;
    s->names=names;
    char **commands=realloc(s->commands,(s->n+1)*sizeof *s->commands);
    if(!commands)return 1;
    s->commands=commands;
    bool *required=realloc(s->required,(s->n+1)*sizeof *s->required);
    if(!required)return 1;
    s->required=required;
    size_t k=s->n++;
    s->names[k]=strdup(f[1]); s->commands[k]=strdup(f[3]); s->required[k]=!strcmp(f[2],"required");
    return !s->names[k] || !s->commands[k];
}
static int user_profile(const char *root,user_steps *steps) {
    size_t n=0;
    char *profile=tam_path_join(root,"usr/share/tamlinux/rootstock/profile");
    struct stat st;
    int fd=profile?open(profile,O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC):-1;
    bool trusted=fd>=0 && !fstat(fd,&st) && S_ISREG(st.st_mode) && !(st.st_mode&0022) &&
        (st.st_uid==0 || (strcmp(root,"/") && st.st_uid==geteuid()));
    char *text=trusted?metadata_fd(fd,&n,METADATA_LIMIT):NULL; free(profile);
    if(fd>=0)close(fd);
    int parsed=text?tam_rec_parse(text,n,4,1,userstepline,steps,NULL):-1;
    free(text);
    bool reload=false;
    for(size_t i=0;i<steps->n;i++)if(!strcmp(steps->names[i],"user-units-reload") && steps->required[i])reload=true;
    if(parsed || !reload) {
        tb_err("a trusted required user-units-reload step is needed before settings complete");
        return -1;
    }
    return 0;
}
static void user_steps_free(user_steps *steps) {
    for(size_t i=0;i<steps->n;i++){free(steps->names[i]); free(steps->commands[i]);}
    free(steps->names); free(steps->commands); free(steps->required);
}
static int user_activate(user_settings *u,const char *root,settings_tx *t,const settings_changes *changes) {
    /* Activating can be retried after an external writer ran: never turn a
     * no-longer-complete file set into a successful transaction. */
    for(size_t i=0;i<changes->n;i++) {
        const settings_change *ch=&changes->v[i]; char hash[TB_HASH_LEN+1]; unsigned mode=0;
        if(live_hash(u->home,ch->path,hash,&mode) || strcmp(hash,ch->after) ||
           (!ch->remove && mode!=ch->mode))return TB_INTERRUPTED;
    }
    user_steps steps={0};
    int result=TB_AFTER_FAILED;
    if(user_profile(root,&steps))goto done;
    tam_str_t plan=tam_str_new("format 1\n");
    for(size_t i=0;i<steps.n && plan;i++) {
        const char *f[]={steps.names[i],steps.required[i]?"required":"optional",steps.commands[i]};
        plan=tam_rec_cat(plan,f,3);
    }
    if(!plan)goto done;
    char hash[TB_HASH_LEN+1]; shash(plan,tam_str_len(plan),hash); tam_str_free(plan);
    /* A count is meaningful only for this exact ordered activation plan.
     * Idempotent actions restart from zero after a profile change. */
    if(strcmp(t->activation,hash)) {
        strcpy(t->activation,hash); t->done=0;
        if(tx_write(u->state,t,"activating")){result=TB_INTERRUPTED; goto done;}
    }
    if(t->done>steps.n)goto done;
    for(size_t i=t->done;i<steps.n;i++) {
        char *command=tam_path_join(root,steps.commands[i]);
        const char *args[]={command,"apply",t->id,NULL};
        tam_exec_result_t r=tam_exec(args,30000); free(command);
        bool ok=r.exit_code==0; tam_exec_result_free(&r);
        if(!ok && steps.required[i]){tb_err("required user step failed: %s",steps.names[i]); goto done;}
        t->done=i+1;
        if(tx_write(u->state,t,"activating")){result=TB_INTERRUPTED; goto done;}
    }
    result=tx_write(u->state,t,"complete")?TB_INTERRUPTED:TB_OK;
done:
    user_steps_free(&steps);
    return result;
}
static bool agrees(const user_settings *u,const system_tx *sys,const settings_tx *t) {
    return !strcmp(sys->state,"complete") && !strcmp(sys->id,t->system) &&
        !strcmp(sys->to,t->to) && !strcmp(u->productid,t->to) && sys->uid==getuid() &&
        (!strcmp(t->kind,"settings-only")? !strcmp(t->from,t->to):
         (!strcmp(sys->id,t->id) && !strcmp(sys->from,t->from)));
}
static int session_ready(user_settings *u,const char *root) {
    settings_tx t;
    system_tx sys;
    int have=tx_read(u->state,&t);
    if(have<=0 || system_read(root,&sys))return TB_INTERRUPTED;
    if(!strcmp(t.state,"complete") && agrees(u,&sys,&t))return TB_OK;
    /* Ready/staging leaves the old product usable. Its completed system
     * and user records must still agree before a launcher may continue. */
    if((strcmp(sys.state,"ready") && strcmp(sys.state,"staging")) ||
       strcmp(u->productid,sys.from) ||
       (strcmp(t.state,"ready") && strcmp(t.state,"complete")))return TB_INTERRUPTED;
    if(!strcmp(t.state,"ready")) {
        if(strcmp(t.system,sys.id) || strcmp(t.from,sys.from) || strcmp(t.to,sys.to) ||
           tx_read_record(u->state,"last-transaction",&t)!=1)return TB_INTERRUPTED;
    }
    if(system_read_record(root,true,&sys) || strcmp(t.state,"complete"))return TB_INTERRUPTED;
    return agrees(u,&sys,&t)?TB_OK:TB_INTERRUPTED;
}
static int settings_apply(user_settings *u,const char *root) {
    settings_tx t;
    int have=tx_read(u->state,&t);
    if(have<0)return TB_INTERRUPTED;
    if(!have)return TB_INTERRUPTED;
    system_tx sys;
    if(system_read(root,&sys) || !agrees(u,&sys,&t)) {
        tb_err("system and settings transaction do not agree; session remains blocked"); return TB_INTERRUPTED;
    }
    if(!strcmp(t.state,"complete"))return TB_OK;
    char *path=tam_path_join("staging",t.id);
    if(!path)return TB_FAIL;
    settings_changes changes={0}; int result=TB_INTERRUPTED;
    if(!strcmp(t.state,"ready") || !strcmp(t.state,"staging")) {
        char *base=tam_path_join(u->homepath,".local/state/tamlinux");
        int cleanup=base?tb_remove_at(u->state,base,path):-1;
        free(base);
        if(cleanup){free(path); return TB_FAIL;}
        int stage=sdir(u->state,path,true);
        if(stage<0 || tx_write(u->state,&t,"staging") || compute_changes(u,root,&t,stage,&changes) || ssync(stage) ||
           tx_write(u->state,&t,"applying")) {
            if(stage>=0)close(stage);
            free(path); changes_free(&changes); return TB_FAIL;
        }
        close(stage);
    }
    int stage=sdir(u->state,path,false); free(path);
    if(stage<0)goto done;
    if(!changes.n) {
        size_t n=0; char *text=metadata(stage,"CHANGES",&n,METADATA_LIMIT);
        char hash[TB_HASH_LEN+1]="";
        if(text)shash(text,n,hash);
        int r=text && !strcmp(hash,t.changes)?tam_rec_parse(text,n,6,1,changesline,&changes,NULL):-1; free(text);
        if(r){close(stage); goto done;}
    }
    if(!strcmp(t.state,"applying")) {
        if(commit_changes(u,stage,&changes) || tx_write(u->state,&t,"activating")){close(stage); goto done;}
    }
    close(stage);
    if(!strcmp(t.state,"activating"))result=user_activate(u,root,&t,&changes);
done:
    changes_free(&changes); return result;
}
static int session_units(const char *root,const char *operation,const char *id) {
    user_steps steps={0};
    int result=-1;
    if(user_profile(root,&steps))goto done;
    for(size_t i=0;i<steps.n;i++)if(!strcmp(steps.names[i],"user-units-reload")) {
        char *helper=tam_path_join(root,steps.commands[i]);
        const char *args[]={helper,operation,id,NULL};
        tam_exec_result_t run=tam_exec(args,30000);
        free(helper);
        result=run.exit_code || run.timed_out || run.truncated?-1:0;
        if(result)tb_err("user services action %s failed: %s",operation,run.stderr_buf?run.stderr_buf:"");
        tam_exec_result_free(&run);
        break;
    }
done:
    user_steps_free(&steps);
    return result;
}
static int session_activate(user_settings *u,const char *root) {
    const char *pkexec="/usr/bin/pkexec";
#ifdef TAM_BUNDLE_TESTING
    const char *fake=getenv("TAM_BUNDLE_PKEXEC");
    if(fake)pkexec=fake;
#endif
    if(strcmp(root,"/")
#ifdef TAM_BUNDLE_TESTING
       && !getenv("TAM_BUNDLE_PKEXEC")
#endif
    ) {
        tb_err("session activation requires the running rootstock");
        return TB_FAIL;
    }
    system_tx sys;
    settings_tx user;
    if(system_read(root,&sys) || sys.uid!=getuid() || tx_read(u->state,&user)!=1)return TB_INTERRUPTED;
    if(!strcmp(sys.state,"staging")) {
        tb_note("Tamlinux preparation is unfinished; continuing requires the completed old installation.");
        return session_ready(u,root);
    }
    bool pending=strcmp(sys.state,"complete")!=0;
    bool ready=!strcmp(sys.state,"ready");
    if(pending && (strcmp(user.system,sys.id) || strcmp(user.from,sys.from) || strcmp(user.to,sys.to))) {
        tb_err("the pending system and settings plans do not agree");
        return TB_INTERRUPTED;
    }
    if(ready) {
        if(session_units(root,"stop",sys.id)) {
            session_units(root,"restart-old",sys.id);
            return TB_AFTER_FAILED;
        }
        tb_bundle_t old={0};
        tb_manifest_t manifest={0};
        int busy=-1;
        if(!strcmp(sys.from,"-"))busy=0;
        else {
            char rel[300]; snprintf(rel,sizeof rel,"usr/lib/tamlinux/version/%s",sys.from);
            char *product=tam_path_join(root,rel);
            if(product && !tb_tree_read(AT_FDCWD,product,&old,&manifest)) {
                bool quiet=tb_quiet;
                for(unsigned attempt=0;attempt<21;attempt++) {
                    tb_quiet=attempt<20 || quiet;
                    busy=tb_busy(root,sys.from,&manifest,false);
                    if(busy<=0 || attempt==20)break;
                    usleep(250000);
                }
                tb_quiet=quiet;
            }
            free(product);
        }
        if(busy>0) {
            fputs("Stop the programs using old Tamlinux and continue? [y/N] ",stdout);
            fflush(stdout);
            char answer[16];
            bool consent=fgets(answer,sizeof answer,stdin) &&
                (!strcmp(answer,"y\n") || !strcmp(answer,"Y\n"));
            if(consent)busy=tb_busy(root,sys.from,&manifest,true);
        }
        tb_bundle_free(&old); tb_manifest_free(&manifest);
        if(busy) {
            if(session_units(root,"restart-old",sys.id))return TB_AFTER_FAILED;
            return busy<0?TB_FAIL:session_ready(u,root);
        }
    }
    if(pending) {
        char expected[65]; strcpy(expected,sys.id);
        const char *args[]={pkexec,"/usr/lib/tam-nut/apply-prepared",expected,NULL};
        tam_exec_result_t run=tam_exec(args,120000);
        bool applied=!run.exit_code && !run.timed_out && !run.truncated;
        if(!applied)tb_err("prepared system apply failed: %s",run.stderr_buf?run.stderr_buf:"");
        tam_exec_result_free(&run);
        if(system_read(root,&sys))return TB_INTERRUPTED;
        if(strcmp(sys.id,expected))return TB_INTERRUPTED;
        if(!strcmp(sys.state,"ready")) {
            if(session_units(root,"restart-old",sys.id))return TB_AFTER_FAILED;
            return session_ready(u,root);
        }
        if(!applied || strcmp(sys.state,"complete"))return TB_INTERRUPTED;
    }
    if(strcmp(u->productid,sys.to) && set_product(u,root,sys.to))return TB_INTERRUPTED;
    int result=settings_apply(u,root);
    if(!result && strcmp(u->productid,sys.to) && set_product(u,root,sys.to))return TB_INTERRUPTED;
    return result?result:session_ready(u,root);
}
int tb_settings_main(const char *root,const char *command,const char *arg) {
    user_settings u; int result=TB_FAIL;
    bool initial=!strcmp(command,"prepare-settings") || !strcmp(command,"session-activate") ||
        !strcmp(command,"session-ready");
    if(user_open(&u,root,initial)){user_close(&u); return TB_FAIL;}
    char *lockpath=tam_path_join(u.homepath,".local/state/tamlinux/lock");
    int lock=lockpath?tam_fs_lock(lockpath,5000):-1; free(lockpath);
    if(lock<0){user_close(&u); return TB_FAIL;}
    if(!strcmp(command,"capture")) {
        char id[256]; result=snapshot(&u,id)?TB_FAIL:TB_OK;
    } else if(!strcmp(command,"prepare-settings"))result=settings_plan(&u,root,arg,false);
    else if(!strcmp(command,"restore-settings"))result=arg?settings_plan(&u,root,arg,true):TB_USAGE;
    else if(!strcmp(command,"apply-settings"))result=settings_apply(&u,root);
    else if(!strcmp(command,"session-activate"))result=session_activate(&u,root);
    else if(!strcmp(command,"session-ready"))result=session_ready(&u,root);
    tam_fs_unlock(lock); user_close(&u); return result;
}

int tb_settings_archive_check(const char *dir,size_t *entries) {
    *entries=0;
    char *path=tam_path_join(dir,"capture"); struct stat st;
    if(!path)return -1;
    if(lstat(path,&st)){int r=errno==ENOENT?0:-1; free(path); return r;}
    if(!S_ISDIR(st.st_mode) || (st.st_mode&07777)!=0700){free(path); return -1;}
    int fd=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    settings s={0}; capture_header h={0}; tb_manifest_t actual={0}; int r=-1;
    if(fd<0 || capture_read(fd,&s,&h) || tb_manifest_build(path,&actual))goto done;
    const char *forbidden[]={"BUNDLE","MANIFEST","SETTINGS"};
    for(size_t i=0;i<3;i++)if(!fstatat(fd,forbidden[i],&st,AT_SYMLINK_NOFOLLOW))goto done;
    for(size_t i=0;i<actual.n;i++) {
        const tb_entry_t *e=&actual.v[i]; bool ok=false;
        if((!strcmp(e->path,"CAPTURE") || !strcmp(e->path,"FILES")) && e->type=='f')ok=true;
        if(!strcmp(e->path,"files") && e->type=='d')ok=true;
        if(!strncmp(e->path,"files/",6))for(size_t j=0;j<s.n;j++) {
            if(strcmp(s.v[j].presence,"present"))continue;
            size_t n=strlen(e->path+6);
            if((e->type=='f' && !strcmp(e->path+6,s.v[j].path)) ||
               (e->type=='d' && !strncmp(e->path+6,s.v[j].path,n) && s.v[j].path[n]=='/'))ok=true;
        }
        if(!ok || e->mode!=(e->type=='d'?0700:0600))goto done;
    }
    tb_bundle_t b={0}; tb_manifest_t m={0};
    if(tb_tree_read(AT_FDCWD,dir,&b,&m))goto done;
    bool matches=!strcmp(h.product,b.id);
    tb_bundle_free(&b); tb_manifest_free(&m);
    if(!matches)goto done;
    *entries=actual.n+1; r=0;
done:
    if(fd>=0)close(fd);
    free(path); sfree(&s); capture_free(&h); tb_manifest_free(&actual);
    return r;
}
/* Copy a product file without the settings-size cap. The destination is in
 * a private export directory, and the resulting manifest hashes its bytes. */
static int product_copy(int source,const char *src,int dest,const char *dst,unsigned mode) {
    int in=tb_open_beneath(source,src);
    if(in<0)return -1;
    char *leaf=NULL;
    int p=sparent(dest,dst,true,&leaf);
    int out=p<0?-1:openat(p,leaf,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    int r=in<0 || out<0?-1:tam_fs_clone_or_copy(in,out,NULL);
    if(!r && (fchmod(out,mode) || fsync(out)))r=-1;
    if(!r)tb_fault("settings-copy-file-sync");
    if(!r)r=ssync(p);
    if(in>=0)close(in);
    if(out>=0)close(out);
    if(p>=0)close(p);
    free(leaf); return r;
}
static int product_entry(int source,const tb_entry_t *e,int dest,const char *dst) {
    if(e->type=='d') {
        int fd=sdir(dest,dst,true); if(fd<0)return -1; close(fd); return 0;
    }
    if(e->type=='f')return product_copy(source,e->path,dest,dst,e->mode);
    char *leaf=NULL; int p=sparent(dest,dst,true,&leaf);
    int r=p<0?-1:symlinkat(e->value,p,leaf);
    if(!r)r=ssync(p);
    if(p>=0)close(p);
    free(leaf); return r;
}
static int restore_directory_modes(int dest,const tb_manifest_t *m,const char *prefix,bool skip_placed) {
    for(size_t i=m->n;i>0;i--) {
        const tb_entry_t *e=&m->v[i-1];
        if(e->type!='d' || (skip_placed && (!strcmp(e->path,".placed") || !strncmp(e->path,".placed/",8))))continue;
        char *path=prefix?tam_path_join(prefix,e->path):strdup(e->path);
        int fd=path?sdir(dest,path,false):-1; free(path);
        if(fd<0)return -1;
        int r=fchmod(fd,e->mode) || ssync(fd); close(fd);
        if(r)return -1;
    }
    return 0;
}
static bool entry_equal(const tb_entry_t *a,const tb_entry_t *b) {
    return a && b && a->type==b->type && a->mode==b->mode && !strcmp(a->value,b->value);
}
static int export_product(user_settings *u,const char *root,int dest,const char *destpath,bool save_edits) {
    tb_bundle_t b={0}; tb_manifest_t old={0},live={0},now={0}; int r=-1;
    if(tb_tree_read(AT_FDCWD,u->productpath,&b,&old) || tb_manifest_build(u->productpath,&live))goto done;
    const char *parts[]={"tree","placed"};
    for(size_t i=0;i<2;i++) {
        int fd=sdir(dest,parts[i],true);
        if(fd<0)goto done;
        int mode=fchmod(fd,0755); close(fd); if(mode)goto done;
    }
    for(size_t i=0;i<live.n;i++) {
        const tb_entry_t *e=&live.v[i];
        if(!strcmp(e->path,".placed") || !strncmp(e->path,".placed/",8))continue;
        char *path=tam_path_join("tree",e->path);
        int copied=path?product_entry(u->product,e,dest,path):-1; free(path);
        if(copied)goto done;
    }
    int rootfd=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(rootfd<0)goto done;
    for(size_t i=0;i<old.n;i++) {
        const tb_entry_t *e=&old.v[i];
        if(strncmp(e->path,"placed/",7))continue;
        tb_entry_t placed=*e; placed.path=e->path+7;
        if(e->type=='f') {
            int livefd=tb_open_beneath(rootfd,placed.path); struct stat st;
            if(livefd<0 && errno==ENOENT)continue;
            bool regular=livefd>=0 && !fstat(livefd,&st) && S_ISREG(st.st_mode) && !(st.st_mode&07000);
            if(livefd>=0)close(livefd);
            if(!regular){close(rootfd); goto done;}
            placed.mode=(unsigned)(st.st_mode&0777);
        }
        int copied=product_entry(rootfd,&placed,dest,e->path);
        if(copied && errno!=ENOENT){close(rootfd); goto done;}
    }
    close(rootfd);
    if(restore_directory_modes(dest,&live,"tree",true))goto done;
    for(size_t i=old.n;i>0;i--) {
        const tb_entry_t *e=&old.v[i-1];
        if(e->type!='d' || strncmp(e->path,"placed/",7))continue;
        int fd=sdir(dest,e->path,false);
        if(fd<0)goto done;
        int mode=fchmod(fd,e->mode); close(fd); if(mode)goto done;
    }
    if(product_copy(u->product,"SETTINGS",dest,"SETTINGS",0644) || tb_v2_manifest(destpath,&now))goto done;
    bool changed=old.len!=now.len || memcmp(old.text,now.text,old.len);
    if(changed) {
        size_t count=0; const char **edits=calloc(old.n+now.n,sizeof *edits);
        if(!edits)goto done;
        for(size_t i=0;i<old.n;i++)if(!entry_equal(&old.v[i],tb_manifest_find(&now,old.v[i].path)))edits[count++]=old.v[i].path;
        for(size_t i=0;i<now.n;i++)if(!tb_manifest_find(&old,now.v[i].path))edits[count++]=now.v[i].path;
        if(!save_edits) {
            for(size_t i=0;i<count;i++)tb_err("edited product file: %s",edits[i]);
            tb_err("capture export needs --save-edits to record a derived bundle"); free(edits); goto done;
        }
        const char **needs=calloc(b.nneeds,sizeof *needs);
        for(size_t i=0;i<b.nneeds;i++) {
            char *line=NULL;
            if(asprintf(&line,"%s %s %s",b.needs[i].kind,b.needs[i].target,b.needs[i].pkgs)<0)line=NULL;
            needs[i]=line;
        }
        tb_seal_t options={.format=2,.buildenv=b.buildenv,.derived=b.id,.edits=edits,.nedits=count,
            .name=b.name,.version=b.version,.host=b.host,.targets=(const char *const *)b.targets,.ntargets=b.ntargets,
            .sources=(const char *const *)b.sources,.nsources=b.nsources,.tools=(const char *const *)b.tools,.ntools=b.ntools,
            .needs=needs,.nneeds=b.nneeds,.after=(const char *const *)b.after,.nafter=b.nafter};
        int sealed=tb_seal(destpath,&options,u->productid);
        for(size_t i=0;i<b.nneeds;i++)free((void *)needs[i]);
        free(needs); free(edits);
        if(sealed)goto done;
    } else {
        if(product_copy(u->product,"BUNDLE",dest,"BUNDLE",0644) || product_copy(u->product,"MANIFEST",dest,"MANIFEST",0644))goto done;
    }
    r=0;
done:
    tb_bundle_free(&b); tb_manifest_free(&old); tb_manifest_free(&live); tb_manifest_free(&now); return r;
}
static int copy_capture(user_settings *u,const char *id,int dest) {
    char *path=tam_path_join("captures",id),*full=path?tam_path_join(u->homepath,".local/state/tamlinux"):NULL;
    char *source=full?tam_path_join(full,path):NULL; free(full);
    int fd=path?sdir(u->state,path,false):-1; free(path);
    tb_manifest_t m={0}; int result=-1;
    if(fd<0 || !source || tb_manifest_build(source,&m))goto done;
    int cap=sdir(dest,"capture",true); if(cap<0)goto done; close(cap);
    for(size_t i=0;i<m.n;i++) {
        tb_entry_t e=m.v[i]; e.mode=e.type=='d'?0700:0600;
        char *p=tam_path_join("capture",e.path);
        int r=p?product_entry(fd,&e,dest,p):-1; free(p);
        if(r)goto done;
    }
    result=0;
done:
    if(fd>=0)close(fd);
    free(source); tb_manifest_free(&m); return result;
}
bool tb_settings_private_output(const char *root,const char *output) {
    char *copy=strdup(output),*slash=copy?strrchr(copy,'/'):NULL;
    if(!copy)return false;
    if(slash){if(slash==copy)slash[1]=0; else *slash=0;}
    else {free(copy); copy=strdup(".");}
    char *parent=realpath(copy,NULL); free(copy);
    char *state=tam_path_join(root,"var/lib/tamlinux"),*public=state?realpath(state,NULL):NULL; free(state);
    bool safe=parent && (!public || strncmp(parent,public,strlen(public)) ||
        (parent[strlen(public)] && parent[strlen(public)]!='/'));
    free(parent); free(public); return safe;
}
int tb_settings_export(const char *root,const char *output,bool save_edits) {
    if(!tb_settings_private_output(root,output)){tb_err("workstation bundles cannot be written under public product state"); return TB_FAIL;}
    user_settings u; int result=TB_FAIL;
    if(user_open(&u,root,false)){user_close(&u); return TB_FAIL;}
    int lock=user_lock(&u);
    if(lock<0){user_close(&u); return TB_FAIL;}
    char *temp=tam_path_join(u.homepath,".local/state/tamlinux/.export-XXXXXX");
    if(!temp || !mkdtemp(temp)){free(temp); tam_fs_unlock(lock); user_close(&u); return TB_FAIL;}
    int dest=open(temp,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    char id[256];
    if(dest<0 || export_product(&u,root,dest,temp,save_edits) || snapshot(&u,id) || copy_capture(&u,id,dest) || tb_v2_verify(temp))goto done;
    if(tb_archive_pack(temp,output))goto done;
    tb_note("private workstation bundle: %s; contains personal settings",output); result=TB_OK;
done:
    if(dest>=0)close(dest);
    tb_remove_at(AT_FDCWD,"/",temp+1); free(temp); tam_fs_unlock(lock); user_close(&u); return result;
}
int tb_settings_import(const char *root,const char *archive) {
    user_settings u; int result=TB_FAIL;
    if(user_open(&u,root,false)){user_close(&u); return TB_FAIL;}
    int lock=user_lock(&u);
    if(lock<0){user_close(&u); return TB_FAIL;}
    char *temp=tam_path_join(u.homepath,".local/state/tamlinux/.import-XXXXXX");
    if(!temp || !mkdtemp(temp)){free(temp); tam_fs_unlock(lock); user_close(&u); return TB_FAIL;}
    settings s={0}; capture_header h={0}; tb_manifest_t m={0}; int cap=-1;
    char *capture=tam_path_join(temp,"capture");
    if(tb_archive_unpack(archive,temp) || tb_v2_verify(temp) || !capture)goto done;
    cap=open(capture,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(cap<0 || capture_read(cap,&s,&h) || tb_manifest_build(capture,&m))goto done;
    if(!*h.user || strspn(h.user,"0123456789")!=strlen(h.user) ||
       !*h.hostname || strlen(h.hostname)>100 ||
       strspn(h.hostname,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-")!=strlen(h.hostname) ||
       strlen(h.created)<16 || strspn(h.created,"0123456789TZ.")!=strlen(h.created))goto done;
    char date[32]; snprintf(date,sizeof date,"%.16s",h.created);
    char id[256]; snprintf(id,sizeof id,"%s@%s-%s-%.12s",h.user,h.hostname,date,h.hash+7);
    if(!tam_path_is_safe_rel(id) || strchr(id,'/'))goto done;
    int captures=sdir(u.state,"captures",true);
    if(captures<0)goto done;
    int existing=sdir(captures,id,false);
    if(existing>=0) {
        settings prior={0}; capture_header old={0};
        bool same=!capture_read(existing,&prior,&old) && !strcmp(old.hash,h.hash);
        close(existing); close(captures); sfree(&prior); capture_free(&old);
        if(!same)goto done;
        tb_note("private settings imported: %s",id); result=TB_OK; goto done;
    }
    unsigned char random[16];
    if(getrandom(random,sizeof random,0)!=sizeof random){close(captures); goto done;}
    char stage[48]=".import-";
    for(size_t i=0;i<16;i++)snprintf(stage+8+i*2,3,"%02x",random[i]);
    if(mkdirat(u.state,stage,0700)){close(captures); goto done;}
    int dest=sdir(u.state,stage,false);
    int r=dest<0?-1:0;
    for(size_t i=0;i<m.n && !r;i++)r=product_entry(cap,&m.v[i],dest,m.v[i].path);
    if(!r)r=ssync(dest);
    if(dest>=0)close(dest);
    if(!r)r=renameat(u.state,stage,captures,id);
    if(!r){tb_fault("settings-import-rename"); r=ssync(captures) || ssync(u.state);}
    close(captures);
    if(!r){tb_note("private settings imported: %s",id); result=TB_OK;}
done:
    if(cap>=0)close(cap);
    free(capture); sfree(&s); capture_free(&h); tb_manifest_free(&m);
    tb_remove_at(AT_FDCWD,"/",temp+1); free(temp); tam_fs_unlock(lock); user_close(&u); return result;
}
int tb_settings_diff(const char *root,const char *path) {
    if(!tam_path_is_safe_rel(path) || secret_path(path))return TB_USAGE;
    user_settings u; int result=TB_FAIL;
    if(user_open(&u,root,false)){user_close(&u); return TB_FAIL;}
    settings_tx tx; system_tx sys;
    char from[256],to[256];
    if(!system_read(root,&sys) && !strcmp(sys.state,"ready")) {
        strcpy(from,sys.from); strcpy(to,sys.to);
    } else if(tx_read(u.state,&tx)>0) {
        strcpy(from,tx.from); strcpy(to,tx.to);
    } else {strcpy(from,u.productid); strcpy(to,u.productid);}
    char *contents[3]={NULL,NULL,NULL}; size_t lengths[3]={0,0,0};
    unsigned mode=0; const char *reason="-";
    int present[3]; present[0]=sread(u.home,path,&contents[0],&lengths[0],&mode,&reason);
    const char *ids[]={from,to};
    for(size_t i=0;i<2;i++) {
        int fd=-1;
        if(strcmp(ids[i],"-") && !set_product(&u,root,ids[i]))fd=tb_open_beneath(u.product,"defaults/home");
        present[i+1]=fd<0?1:sread(fd,path,&contents[i+1],&lengths[i+1],&mode,&reason);
        if(fd>=0)close(fd);
    }
    for(size_t i=0;i<3;i++)if(present[i]<0 || present[i]==2 ||
        (present[i]==0 && (secret_line(contents[i],lengths[i]) || memchr(contents[i],0,lengths[i]))))goto done;
    printf("user: %s\n%s\nbase: %s\n%s\nnew default: %s\n%s\n",path,
        present[0]==1?"(absent)":contents[0],from,present[1]==1?"(absent)":contents[1],
        to,present[2]==1?"(absent)":contents[2]); result=TB_OK;
done:
    for(size_t i=0;i<3;i++)free(contents[i]);
    user_close(&u); return result;
}
