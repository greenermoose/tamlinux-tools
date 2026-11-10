/* Runtime contracts are read-only. Product executables are never run. */
#define _GNU_SOURCE
#include "tb.h"
#include <ctype.h>
#include <errno.h>
#include <elf.h>
#include <fcntl.h>
#include <linux/openat2.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <unistd.h>

int tb_runtime_open(int root,const char *absolute) {
    if(!absolute || absolute[0]!='/' || !tam_path_is_safe_rel(absolute+1))return -1;
    struct open_how h={.flags=O_RDONLY|O_NONBLOCK|O_CLOEXEC,
        .resolve=RESOLVE_IN_ROOT|RESOLVE_NO_MAGICLINKS};
    return (int)syscall(SYS_openat2,root,absolute,&h,sizeof h);
}
typedef struct {char *copy,*version,*exact; bool qml;} rule;
static bool module(const char *s) {
    bool start=true;
    for(;*s;s++) {
        if(*s=='.'){if(start)return false; start=true; continue;}
        if(!(isalpha((unsigned char)*s) || *s=='_' || (!start && isdigit((unsigned char)*s))))return false;
        start=false;
    }
    return !start;
}
static bool version(const char *s) {
    return *s && strlen(s)<=64 && strspn(s,"0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ._:+-~")==strlen(s) && *s!='-';
}
static int rule_read(const char *kind,const char *target,const char *rest,rule *r) {
    memset(r,0,sizeof *r);
    if(strlen(target)>=4096 || strlen(rest)>4096)return -1;
    bool file=!strcmp(kind,"file"),command=!strcmp(kind,"command");
    r->qml=!strcmp(kind,"qml");
    bool python=!strcmp(kind,"python"),elf=!strcmp(kind,"elf"),self=!strcmp(kind,"tam-bundle");
    if(!(file || command || r->qml || python || elf || self) ||
       (file && (*target!='/' || !tam_path_is_safe_rel(target+1))) ||
       (command && !tb_valid_name(target)) || ((python || r->qml) && !module(target)) ||
       (elf && !tam_path_is_safe_rel(target)) || (self && strcmp(target,">=")))return -1;
    r->copy=strdup(rest); if(!r->copy)return -1;
    char *save=NULL,*tok=strtok_r(r->copy," \t",&save);
    if(self || r->qml) {
        if(!tok || !version(tok))return -1;
        r->version=tok; tok=strtok_r(NULL," \t",&save);
    } else if(tok && (!strcmp(tok,">=") || !strcmp(tok,"exact"))) {
        if(!file && !command)return -1;
        bool exact=!strcmp(tok,"exact"); tok=strtok_r(NULL," \t",&save);
        if(!tok)return -1;
        if(exact) {
            if(strlen(tok)!=TB_HASH_LEN || strncmp(tok,"sha256:",7) || strspn(tok+7,"0123456789abcdef")!=64)return -1;
            r->exact=tok;
        } else {if(!version(tok))return -1; r->version=tok;}
        tok=strtok_r(NULL," \t",&save);
    }
    for(;tok;tok=strtok_r(NULL," \t",&save)) {
        char *eq=strchr(tok,'=');
        if(elf || self || !eq || eq==tok || !eq[1])return -1;
        for(char *p=tok;p<eq;p++)if(!isalnum((unsigned char)*p) && *p!='-')return -1;
    }
    return 0;
}
bool tb_need_valid(const char *kind,const char *target,const char *rest) {
    rule r; int ok=rule_read(kind,target,rest,&r); free(r.copy); return !ok;
}
static char *read_small(int fd,size_t max) {
    struct stat st;
    if(fd<0 || fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_size<0 || (uint64_t)st.st_size>max)return NULL;
    char *text=calloc(1,(size_t)st.st_size+1);
    if(!text)return NULL;
    size_t off=0;
    while(off<(size_t)st.st_size) {
        ssize_t n=read(fd,text+off,(size_t)st.st_size-off);
        if(n<=0){free(text); return NULL;}
        off+=(size_t)n;
    }
    return text;
}
static bool baseline(const char *v) {
#if defined(__x86_64__)
    if(!strcmp(v,"x86-64"))return true;
    if(!strcmp(v,"x86-64-v2"))return __builtin_cpu_supports("x86-64-v2");
    if(!strcmp(v,"x86-64-v3"))return __builtin_cpu_supports("x86-64-v3");
    if(!strcmp(v,"x86-64-v4"))return __builtin_cpu_supports("x86-64-v4");
#endif
    return false;
}
static int targets(int root,const tb_bundle_t *b,const tb_native_t *native) {
    struct utsname u; if(uname(&u))return -1;
    int errors=0;
    for(size_t i=0;i<b->ntargets;i++) {
        char *copy=strdup(b->targets[i]),*f[6]={0},*save=NULL; size_t n=0;
        for(char *t=copy?strtok_r(copy," \t",&save):NULL;t && n<6;t=strtok_r(NULL," \t",&save))f[n++]=t;
        bool ok=false;
        if(n==4 && !strcmp(f[0],"arch") && !strcmp(f[2],"baseline"))
            ok=!strcmp(f[1],u.machine) && baseline(f[3]);
        if(n==4 && !strcmp(f[0],"libc") && !strcmp(f[1],"glibc") && !strcmp(f[2],"loader")) {
            char *glibc=native?tb_native_version(native,"glibc"):strdup("deferred"); int fd=tb_runtime_open(root,f[3]);
            Elf64_Ehdr elf;
            ok=glibc && fd>=0 && read(fd,&elf,sizeof elf)==sizeof elf && !memcmp(elf.e_ident,ELFMAG,4) &&
                elf.e_ident[EI_CLASS]==ELFCLASS64 && elf.e_ident[EI_DATA]==ELFDATA2LSB && elf.e_machine==EM_X86_64;
            if(fd>=0)close(fd);
            free(glibc);
        }
        if(n==2 && !strcmp(f[0],"profile")) {
            int fd=tb_runtime_open(root,"/usr/share/tamlinux/rootstock/id");
            char *id=read_small(fd,128); if(fd>=0)close(fd);
            if(id){size_t len=strlen(id); if(len && id[len-1]=='\n')id[len-1]=0; ok=!strcmp(id,f[1]); free(id);}
        }
        if(!ok){tb_err("incompatible or unsupported target: %s",b->targets[i]); errors++;}
        free(copy);
    }
    return errors?-1:0;
}
static bool qml_number(const char *text,unsigned long *major,unsigned long *minor) {
    if(!isdigit((unsigned char)*text))return false;
    char *end; errno=0; *major=strtoul(text,&end,10);
    if(errno || *end!='.' || !isdigit((unsigned char)end[1]))return false;
    *minor=strtoul(end+1,&end,10);
    return !errno && !*end && *major<=1000000 && *minor<=1000000;
}
static bool qml_version(const char *actual,const char *want) {
    unsigned long major,minor,a,b;
    return qml_number(want,&major,&minor) && qml_number(actual,&a,&b) &&
        (a>major || (a==major && b>=minor));
}
/* Only literal exports arrays establish qmltypes versions. Skip all
 * comments and other strings so a note or numeric property cannot pass. */
typedef struct {int kind; const char *start; size_t len;} qtoken;
static qtoken qnext(const char **source) {
    const char *p=*source;
    for(;;) {
        while(isspace((unsigned char)*p))p++;
        if(!strncmp(p,"//",2)){while(*p && *p!='\n')p++; continue;}
        if(!strncmp(p,"/*",2)){const char *end=strstr(p+2,"*/"); if(!end){*source=p+strlen(p); return (qtoken){-1,0,0};} p=end+2; continue;}
        break;
    }
    qtoken t={.kind=(unsigned char)*p,.start=p,.len=1};
    if(!*p){t.len=0; *source=p; return t;}
    if(*p=='\"' || *p=='\'') {
        char quote=*p++; t.start=p; t.kind='s';
        while(*p && *p!=quote){if(*p=='\\' && p[1])p++; p++;}
        if(!*p){*source=p; return (qtoken){-1,0,0};}
        t.len=(size_t)(p-t.start); p++;
    } else if(isalpha((unsigned char)*p) || *p=='_') {
        t.kind='i'; while(isalnum((unsigned char)*p) || *p=='_')p++;
        t.len=(size_t)(p-t.start);
    } else p++;
    *source=p; return t;
}
static bool qml_exports(const char *text,const char *name,const char *want) {
    qtoken t; const char *p=text; size_t len=strlen(name);
    while((t=qnext(&p)).kind>0) {
        if(t.kind!='i' || t.len!=7 || strncmp(t.start,"exports",7))continue;
        if(qnext(&p).kind!=':' || qnext(&p).kind!='[')continue;
        while((t=qnext(&p)).kind=='s') {
            if(t.len>len+3 && !strncmp(t.start,name,len) && t.start[len]=='/') {
                const char *space=memchr(t.start,' ',t.len);
                if(space && t.len-(size_t)(space+1-t.start)<80) {
                    char v[80]; size_t n=t.len-(size_t)(space+1-t.start);
                    memcpy(v,space+1,n); v[n]=0;
                    if(qml_version(v,want))return true;
                }
            }
            if(qnext(&p).kind!=',')break;
        }
    }
    return false;
}
static bool qml_probe(int root,const char *name,const char *want) {
    char *mod=strdup(name); if(!mod)return false;
    for(char *p=mod;*p;p++)if(*p=='.')*p='/';
    char *path=NULL; bool ok=false;
    if(asprintf(&path,"/usr/lib/qt6/qml/%s/qmldir",mod)<0)path=NULL;
    int fd=path?tb_runtime_open(root,path):-1;
    char *text=read_small(fd,1024U*1024U); if(fd>=0)close(fd);
    if(text) {
        bool identity=false;
        for(char *save=NULL,*line=strtok_r(text,"\n",&save);line;line=strtok_r(NULL,"\n",&save)) {
            char type[128],ver[80],file[4096];
            if(sscanf(line,"%127s %79s",type,ver)==2 && !strcmp(type,"module") && !strcmp(ver,name))identity=true;
            if(sscanf(line,"%127s %79s %4095s",type,ver,file)==3 && module(type) && qml_version(ver,want))ok=true;
        }
        if(!identity){free(text); free(path); free(mod); return false;}
        if(!ok) {
            free(path); path=NULL;
            if(asprintf(&path,"/usr/lib/qt6/qml/%s/plugins.qmltypes",mod)<0)path=NULL;
            fd=path?tb_runtime_open(root,path):-1;
            char *types=read_small(fd,16U*1024U*1024U); if(fd>=0)close(fd);
            if(types)ok=qml_exports(types,name,want);
            free(types);
        }
    }
    free(text); free(path); free(mod); return ok;
}
int tb_contract_check(const char *rootpath,const char *product,const tb_bundle_t *b) {
    int root=open(rootpath,O_RDONLY|O_DIRECTORY|O_CLOEXEC),errors=0;
    if(root<0)return -1;
    /* Reject hardware/profile/loader mismatches before a native DB query. */
    if(targets(root,b,NULL)){close(root); return -1;}
    tb_native_t native;
    if(tb_native_read(rootpath,true,&native)){close(root); return -1;}
    struct stat opened,path;
    if(fstat(root,&opened) || stat(native.root,&path) || opened.st_dev!=path.st_dev || opened.st_ino!=path.st_ino ||
       targets(root,b,&native)){close(root); tb_native_free(&native); return -1;}
    for(size_t i=0;i<b->nneeds;i++) {
        const tb_need_t *need=&b->needs[i]; rule r;
        if(rule_read(need->kind,need->target,need->pkgs,&r)){free(r.copy); errors++; continue;}
        bool ok=false;
        if(!strcmp(need->kind,"tam-bundle"))ok=tb_version_compare(TB_VERSION,r.version)>=0;
        else if(!strcmp(need->kind,"qml"))ok=qml_probe(root,need->target,r.version);
        else if(!strcmp(need->kind,"python")) {
            bool running_root=!strcmp(native.root,"/");
#ifdef TAM_BUNDLE_TESTING
            running_root=true; /* isolated fixture executables, never a host fallback */
#endif
            if(!running_root) {
                tb_err("Python import probes require the running rootstock; an offline foreign root cannot prove imports");
                free(r.copy); errors++; continue;
            }
            char *python=tam_path_join(native.root,"usr/bin/python3");
            const char *args[]={python,"-I","-c","import importlib,sys; importlib.import_module(sys.argv[1])",need->target,NULL};
            tam_exec_result_t run=tam_exec(args,10000);
            ok=!run.exit_code && !run.timed_out && !run.truncated;
            tam_exec_result_free(&run); free(python);
        } else if(!strcmp(need->kind,"elf")) {
            ok=!tb_elf_probe(rootpath,product,b,need->target);
        } else {
            char *path=NULL;
            if(!strcmp(need->kind,"command")) {if(asprintf(&path,"/usr/bin/%s",need->target)<0)path=NULL;}
            else path=strdup(need->target);
            int fd=path?tb_runtime_open(root,path):-1; struct stat st;
            ok=fd>=0 && !fstat(fd,&st) && S_ISREG(st.st_mode) &&
                (strcmp(need->kind,"command") || (st.st_mode&0111));
            if(ok && r.exact) {
                uint8_t d[TAM_SHA256_LEN]; char hash[TB_HASH_LEN+1]="sha256:";
                ok=!tam_sha256_fd(fd,d); if(ok){tam_sha256_hex(d,hash+7); ok=!strcmp(hash,r.exact);}
            }
            if(ok && r.version) {
                const char *owner=tb_native_owner(&native,path);
                char proc[64],canonical[4096]; snprintf(proc,sizeof proc,"/proc/self/fd/%d",fd);
                ssize_t n=readlink(proc,canonical,sizeof canonical-1);
                if(!owner && n>0) {
                    canonical[n]=0; size_t len=strlen(native.root);
                    if(!strcmp(native.root,"/"))owner=tb_native_owner(&native,canonical);
                    else if(!strncmp(canonical,native.root,len) && canonical[len]=='/')owner=tb_native_owner(&native,canonical+len);
                }
                if(!owner)tb_note("present, version unknown: %s",path);
                else {
                    char *ver=tb_native_version(&native,owner);
                    ok=ver && tb_version_compare(ver,r.version)>=0; free(ver);
                }
            }
            if(fd>=0)close(fd);
            free(path);
        }
        if(!ok){tb_err("unmet runtime need: %s %s %s",need->kind,need->target,need->pkgs); errors++;}
        free(r.copy);
    }
    close(root); tb_native_free(&native); return errors?-1:0;
}
