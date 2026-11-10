/* Bounded ELF64 little-endian inspection for the first x86_64 rootstock.
 * No ldd, loader invocation or execution of inspected programs. */
#define _GNU_SOURCE
#include "tb.h"
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    int root;
    char *product,*tree,*placed,*rootpath;
    const tb_bundle_t *bundle;
    bool declarations;
    size_t memory;
    struct {dev_t dev; ino_t ino;} seen[1024]; size_t nseen;
} links;
static bool within(const char *path,const char *base) {
    size_t n=strlen(base); return !strncmp(path,base,n) && (!path[n] || path[n]=='/');
}
static bool range(size_t size,uint64_t off,uint64_t n) {return off<=size && n<=size-off;}
static int inspect(links *c,int fd,const char *what,bool payload,unsigned depth);
static int local_open(links *c,const char *path) {
    char *real=realpath(path,NULL);
    int fd=real && within(real,c->product)?open(real,O_RDONLY|O_NONBLOCK|O_CLOEXEC):-1;
    free(real); return fd;
}
static int origin_open(links *c,const char *path,bool *payload) {
    char *real=realpath(path,NULL); int fd=-1;
    if(real && within(real,c->product)) {fd=open(real,O_RDONLY|O_NONBLOCK|O_CLOEXEC); *payload=true;}
    else if(real && (!strcmp(c->rootpath,"/") || within(real,c->rootpath))) {
        const char *rel=!strcmp(c->rootpath,"/")?real:real+strlen(c->rootpath);
        fd=tb_runtime_open(c->root,rel); *payload=false;
    }
    free(real); return fd;
}
static bool declared(links *c,const char *path,int fd) {
    if(!c->declarations)return true;
    struct stat actual;
    if(fstat(fd,&actual))return false;
    const char *base=strrchr(path,'/'); base=base?base+1:path;
    for(size_t i=0;i<c->bundle->nneeds;i++) {
        const tb_need_t *n=&c->bundle->needs[i];
        if(!strcmp(n->kind,"command") && !strcmp(n->target,base))return true;
        if(strcmp(n->kind,"file"))continue;
        if(!strcmp(n->target,path))return true;
        int other=tb_runtime_open(c->root,n->target); struct stat st;
        bool same=other>=0 && !fstat(other,&st) && st.st_dev==actual.st_dev && st.st_ino==actual.st_ino;
        if(other>=0)close(other);
        if(same)return true;
    }
    tb_err("undeclared interpreter or ELF dependency: %s",path); return false;
}
static int external(links *c,const char *path,bool require,unsigned depth) {
    if(!path || *path!='/' || !tam_path_is_safe_rel(path+1))return -1;
    const char *prefix="/usr/lib/tamlinux/current/";
    char *provided=!strncmp(path,prefix,strlen(prefix))?
        tam_path_join(c->tree,path+strlen(prefix)):tam_path_join(c->placed,path+1);
    int own=provided?local_open(c,provided):-1; free(provided);
    if(own>=0) {
        int r=inspect(c,own,path,true,depth+1); close(own); return r;
    }
    int fd=tb_runtime_open(c->root,path);
    if(fd<0){tb_err("missing interpreter or ELF library: %s",path); return -1;}
    int r=require && !declared(c,path,fd)?-1:inspect(c,fd,path,false,depth+1);
    close(fd); return r;
}
static int library(links *c,const char *name,const char *runpath,const char *origin,bool require,unsigned depth) {
    if(!*name || strchr(name,'/') || strlen(name)>255)return -1;
    if(runpath && (*runpath==':' || strstr(runpath,"::") || (*runpath && runpath[strlen(runpath)-1]==':')))return -1;
    char *paths=strdup(runpath?runpath:""); int fd=-1; bool payload=false; char *found=NULL;
    for(char *save=NULL,*p=paths?strtok_r(paths,":",&save):NULL;p && fd<0;p=strtok_r(NULL,":",&save)) {
        char *dir=NULL;
        if(!strncmp(p,"$ORIGIN",7) && (!p[7] || p[7]=='/')) {
            if(asprintf(&dir,"%s%s",origin,p+7)<0)dir=NULL;
        } else if(!strncmp(p,"${ORIGIN}",9) && (!p[9] || p[9]=='/')) {
            if(asprintf(&dir,"%s%s",origin,p+9)<0)dir=NULL;
        } else if(*p=='/' && !strchr(p,'$')) {
            const char *prefix="/usr/lib/tamlinux/current/";
            if(!strncmp(p,prefix,strlen(prefix)))dir=tam_path_join(c->tree,p+strlen(prefix));
            else {
                found=tam_path_join(p,name); fd=found?tb_runtime_open(c->root,found):-1;
                if(fd<0){free(found); found=NULL;} continue;
            }
        } else {tb_err("unsupported relative or expanded ELF search path: %s",p); free(paths); return -1;}
        found=dir?tam_path_join(dir,name):NULL; free(dir);
        fd=found?origin_open(c,found,&payload):-1;
        if(fd<0){free(found); found=NULL;}
    }
    free(paths);
    const char *private_dirs[]={"lib","lib64"};
    for(size_t i=0;i<2 && fd<0;i++) {
        char *dir=tam_path_join(c->tree,private_dirs[i]); found=dir?tam_path_join(dir,name):NULL; free(dir);
        fd=found?local_open(c,found):-1; payload=fd>=0;
        if(fd<0){free(found); found=NULL;}
    }
    const char *dirs[]={"/usr/lib","/lib","/lib64","/usr/lib64"};
    for(size_t i=0;i<4 && fd<0;i++) {
        found=tam_path_join(dirs[i],name); fd=found?tb_runtime_open(c->root,found):-1; payload=false;
        if(fd<0){free(found); found=NULL;}
    }
    if(fd<0){tb_err("unresolved ELF dependency: %s",name); return -1;}
    int result=(!payload && require && !declared(c,found,fd))?-1:inspect(c,fd,found,payload,depth+1);
    close(fd); free(found); return result;
}
static int script(links *c,const unsigned char *data,size_t size,bool require,unsigned depth) {
    size_t len=0; while(len<size && data[len]!='\n' && len<4096)len++;
    if(len==4096)return -1;
    char *line=strndup((const char *)data+2,len>=2?len-2:0);
    if(!line)return -1;
    char *save=NULL,*program=strtok_r(line," \t",&save); int result=-1;
    if(!program || *program!='/')goto done;
    if(external(c,program,require,depth))goto done;
    if(!strcmp(program,"/usr/bin/env") || !strcmp(program,"/bin/env")) {
        program=strtok_r(NULL," \t",&save);
        if(program && !strcmp(program,"-S"))program=strtok_r(NULL," \t",&save);
        if(!program || !tb_valid_name(program))goto done;
        char *path=NULL; if(asprintf(&path,"/usr/bin/%s",program)<0)path=NULL;
        int r=path?external(c,path,require,depth):-1; free(path); if(r)goto done;
    }
    result=0;
done:
    free(line); return result;
}
static int inspect(links *c,int fd,const char *what,bool payload,unsigned depth) {
    struct stat st;
    if(depth>64 || fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_size<0 || (uint64_t)st.st_size>256U*1024U*1024U)return -1;
    for(size_t i=0;i<c->nseen;i++)if(c->seen[i].dev==st.st_dev && c->seen[i].ino==st.st_ino)return 0;
    if(c->nseen>=1024)return -1;
    c->seen[c->nseen].dev=st.st_dev; c->seen[c->nseen++].ino=st.st_ino;
    if(!st.st_size)return 0;
    unsigned char magic[4]={0};
    ssize_t initial=pread(fd,magic,sizeof magic,0);
    bool is_script=initial>=2 && !memcmp(magic,"#!",2);
    if(!is_script && (initial!=4 || memcmp(magic,ELFMAG,4)))return 0;
    size_t size=(size_t)st.st_size;
    if(is_script && size>4096)size=4096;
    if(size>512U*1024U*1024U-c->memory)return -1;
    unsigned char *data=malloc(size);
    if(!data)return -1;
    c->memory+=size;
    size_t off=0;
    while(off<size) {
        ssize_t n=pread(fd,data+off,size-off,(off_t)off);
        if(n<0 && errno==EINTR)continue;
        if(n<=0)break;
        off+=(size_t)n;
    }
    struct stat after;
    int result=-1;
    if(off!=size || fstat(fd,&after) || st.st_size!=after.st_size ||
       st.st_ctim.tv_sec!=after.st_ctim.tv_sec || st.st_ctim.tv_nsec!=after.st_ctim.tv_nsec)goto done;
    if(size>=2 && !memcmp(data,"#!",2)){result=script(c,data,size,payload,depth); goto done;}
    if(size<4 || memcmp(data,ELFMAG,4)){result=0; goto done;}
    Elf64_Ehdr header;
    if(size<sizeof header)goto done;
    memcpy(&header,data,sizeof header);
    if(header.e_ident[EI_CLASS]!=ELFCLASS64 || header.e_ident[EI_DATA]!=ELFDATA2LSB ||
       header.e_ident[EI_VERSION]!=EV_CURRENT || header.e_machine!=EM_X86_64)goto done;
    if(header.e_type==ET_REL && !header.e_phnum){result=0; goto done;}
    if((header.e_type!=ET_EXEC && header.e_type!=ET_DYN) ||
       header.e_phentsize!=sizeof(Elf64_Phdr) || !header.e_phnum || header.e_phnum>4096 ||
       !range(size,header.e_phoff,(uint64_t)header.e_phnum*sizeof(Elf64_Phdr)))goto done;
    Elf64_Phdr *ph=calloc(header.e_phnum?header.e_phnum:1,sizeof *ph);
    if(!ph)goto done;
    memcpy(ph,data+header.e_phoff,header.e_phnum*sizeof *ph);
    uint64_t dynoff=0,dynsize=0,strva=0,strsize=0,stroff=0; bool hasdyn=false,hasstr=false,hasinterp=false;
    for(size_t i=0;i<header.e_phnum;i++) {
        if(!range(size,ph[i].p_offset,ph[i].p_filesz))goto elf_done;
        if(ph[i].p_type==PT_INTERP) {
            if(hasinterp)goto elf_done;
            hasinterp=true;
            const char *p=(const char *)data+ph[i].p_offset;
            if(!ph[i].p_filesz || ph[i].p_filesz>4096 || !memchr(p,0,(size_t)ph[i].p_filesz) ||
               external(c,p,payload,depth))goto elf_done;
        }
        if(ph[i].p_type==PT_DYNAMIC) {
            if(hasdyn)goto elf_done;
            hasdyn=true; dynoff=ph[i].p_offset; dynsize=ph[i].p_filesz;
        }
    }
    if(!hasdyn){result=0; goto elf_done;}
    if(dynsize>16U*1024U*1024U || dynsize%sizeof(Elf64_Dyn))goto elf_done;
    bool terminated=false;
    for(uint64_t off=0;off<dynsize;off+=sizeof(Elf64_Dyn)) {
        Elf64_Dyn d; memcpy(&d,data+dynoff+off,sizeof d);
        if(d.d_tag==DT_NULL){terminated=true; break;}
        if(d.d_tag==DT_STRTAB)strva=d.d_un.d_ptr;
        if(d.d_tag==DT_STRSZ)strsize=d.d_un.d_val;
    }
    if(!terminated || !strsize)goto elf_done;
    for(size_t i=0;i<header.e_phnum;i++)if(ph[i].p_type==PT_LOAD && strva>=ph[i].p_vaddr &&
       strva-ph[i].p_vaddr<=ph[i].p_filesz && strsize<=ph[i].p_filesz-(strva-ph[i].p_vaddr)) {
        stroff=ph[i].p_offset+(strva-ph[i].p_vaddr); hasstr=true; break;
    }
    if(!hasstr || !range(size,stroff,strsize))goto elf_done;
    const char *strings=(const char *)data+stroff,*runpath=NULL;
    for(uint64_t off=0;off<dynsize;off+=sizeof(Elf64_Dyn)) {
        Elf64_Dyn d; memcpy(&d,data+dynoff+off,sizeof d); if(d.d_tag==DT_NULL)break;
        if(d.d_tag!=DT_NEEDED && d.d_tag!=DT_RUNPATH && d.d_tag!=DT_RPATH)continue;
        if(d.d_un.d_val>=strsize || !memchr(strings+d.d_un.d_val,0,(size_t)(strsize-d.d_un.d_val)))goto elf_done;
        if(d.d_tag==DT_RUNPATH || (d.d_tag==DT_RPATH && !runpath))runpath=strings+d.d_un.d_val;
    }
    char proc[64],actual[4096]; snprintf(proc,sizeof proc,"/proc/self/fd/%d",fd);
    ssize_t an=readlink(proc,actual,sizeof actual-1);
    if(an<=0)goto elf_done;
    actual[an]=0; char *slash=strrchr(actual,'/'); if(!slash)goto elf_done; *slash=0;
    for(uint64_t off=0;off<dynsize;off+=sizeof(Elf64_Dyn)) {
        Elf64_Dyn d; memcpy(&d,data+dynoff+off,sizeof d); if(d.d_tag==DT_NULL)break;
        if(d.d_tag==DT_NEEDED && library(c,strings+d.d_un.d_val,runpath,actual,payload,depth))goto elf_done;
    }
    result=0;
elf_done:
    free(ph);
done:
    if(result)tb_err("cannot prove interpreter/ELF dependencies: %s",what);
    free(data); c->memory-=size; return result;
}
static int init(links *c,const char *root,const char *product,const tb_bundle_t *b,bool declarations) {
    memset(c,0,sizeof *c); c->root=-1;
    c->bundle=b; c->declarations=declarations;
    c->product=realpath(product,NULL);
    c->rootpath=realpath(root,NULL);
    c->root=open(root,O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(!c->product || !c->rootpath || c->root<0)return -1;
    char *tree=tam_path_join(c->product,"tree"); struct stat st;
    bool archive=b->format==2 && tree && !lstat(tree,&st) && S_ISDIR(st.st_mode);
    c->tree=archive?tree:strdup(c->product); if(!archive)free(tree);
    c->placed=tam_path_join(c->product,archive?"placed":".placed");
    return c->tree && c->placed?0:-1;
}
static void finish(links *c) {if(c->root>=0)close(c->root); free(c->product); free(c->tree); free(c->placed); free(c->rootpath);}
int tb_link_check(const char *root,const char *product,const tb_bundle_t *b,bool declarations) {
    links c; int result=-1;
    if(init(&c,root,product,b,declarations))goto done;
    tb_bundle_t metadata={0}; tb_manifest_t m={0};
    /* Unsealed build output has no records yet: a manifest walk is enough. */
    if(tb_manifest_build(product,&m))goto done;
    result=0;
    for(size_t i=0;i<m.n;i++)if(m.v[i].type=='f') {
        const char *p=m.v[i].path;
        if(!strcmp(p,"BUNDLE") || !strcmp(p,"MANIFEST") || !strcmp(p,"SETTINGS") || !strncmp(p,"capture/",8))continue;
        char *path=tam_path_join(product,p); int fd=path?local_open(&c,path):-1;
        /* Independent payloads can share an external library; don't let a
         * previous traversal suppress a later payload's declaration check. */
        c.nseen=0;
        if(fd<0 || inspect(&c,fd,p,true,0))result=-1;
        if(fd>=0)close(fd);
        free(path);
    }
    tb_manifest_free(&m); tb_bundle_free(&metadata);
done:
    finish(&c); return result;
}
int tb_elf_probe(const char *root,const char *product,const tb_bundle_t *b,const char *path) {
    links c; int result=-1;
    if(init(&c,root,product,b,false))goto done;
    const char *rel=path;
    const char *base=c.tree;
    if(!strncmp(path,"usr/",4)){base=c.placed; rel=path;}
    else if(!strncmp(path,"tree/",5))rel=path+5;
    else if(!strncmp(path,"placed/",7)){base=c.placed; rel=path+7;}
    char *full=tam_path_join(base,rel); int fd=full?local_open(&c,full):-1;
    if(fd>=0) {
        unsigned char magic[4];
        if(pread(fd,magic,4,0)==4 && !memcmp(magic,ELFMAG,4))result=inspect(&c,fd,path,true,0);
        close(fd);
    }
    free(full);
done:
    finish(&c); return result;
}
