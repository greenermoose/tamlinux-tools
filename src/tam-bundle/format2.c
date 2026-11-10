/* Format 2 layout and bounded POSIX pax transport. No archive extraction
 * command is trusted with filesystem writes: every parent is opened beneath
 * the destination without following links. */
#include "tb.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int tb_v2_manifest(const char *dir, tb_manifest_t *m) {
    tb_manifest_t raw = {0};
    if (tb_manifest_build(dir, &raw)) return -1;
    tam_str_t text = tam_str_new("format 2\n");
    bool tree = false, placed = false;
    for (size_t i = 0; i < raw.n; i++) {
        tb_entry_t *e = &raw.v[i];
        if (!strcmp(e->path, "tree") && e->type == 'd' && e->mode == 0755) { tree = true;
     continue;
     }
        if (!strcmp(e->path, "placed") && e->type == 'd' && e->mode == 0755) { placed = true;
     continue;
     }
        bool p = !strncmp(e->path, "placed/", 7), t = !strncmp(e->path, "tree/", 5);
        if ((!p && !t) || (p && e->type == 'l')) { tb_err("format 2: invalid payload path %s", e->path);
     goto fail;
     }
        char type[2] = {e->type, 0}, mode[8];
        if (e->type == 'l') strcpy(mode, "-");
     else snprintf(mode, sizeof mode, "%04o", e->mode);
        const char *f[] = {p ? "p" : "t", type, mode, e->value, e->path + (p ? 7 : 5)};
        text = tam_rec_cat(text, f, 5);
        if (!text) goto fail;
    }
    if (!tree || !placed) { tb_err("format 2 needs tree/ and placed/, both mode 0755");
     goto fail;
     }
    if (tb_manifest_parse(text, tam_str_len(text), "format 2 MANIFEST", m)) goto fail;
    m->text = strdup(text);
     m->len = tam_str_len(text);
    tam_str_free(text);
     tb_manifest_free(&raw);
    return m->text ? 0 : -1;
fail:
    tam_str_free(text);
     tb_manifest_free(&raw);
     return -1;
}

int tb_v2_verify(const char *dir) {
    tb_bundle_t b = {0};
     tb_manifest_t m = {0};
     tb_check_t c = {0};
    if (tb_tree_read(AT_FDCWD, dir, &b, &m)) return TB_FAIL;
    int r = tb_tree_check(dir, &m, NULL, true, &c);
    /* Empty sections still have their two structural directories. */
    if (b.format == 2) {
        size_t capture_entries=0;
        if(tb_settings_archive_check(dir,&capture_entries))r=-1;
        const char *parts[]={"tree","placed"};
        for(size_t i=0;i<2;i++) {
            char *p=tam_path_join(dir,parts[i]);
            struct stat st;
            if(lstat(p,&st) || !S_ISDIR(st.st_mode) || (st.st_mode&07777)!=0755)r=-1;
            else if(!tb_manifest_find(&m,parts[i]) && c.extra)c.extra--;
            free(p);
        }
    }
    bool ok = !r && !c.edited && !c.extra && !c.missing;
    tb_check_free(&c);
     tb_bundle_free(&b);
     tb_manifest_free(&m);
    return ok ? TB_OK : TB_FAIL;
}

static int exact(int fd, void *p, size_t n) {
    while (n) { ssize_t r = read(fd, p, n);
     if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return -1;
     p = (char *)p+r;
     n -= (size_t)r;
     }
    return 0;
}
static int number(const unsigned char *s, size_t n, unsigned long long *v) {
    *v = 0;
     size_t i = 0;
    while (i < n && s[i] == ' ') i++;
    for (; i < n && s[i] >= '0' && s[i] <= '7'; i++) {
        if (*v > (1ULL<<55)) return -1;
        *v = *v * 8 + s[i]-'0';
    }
    for (; i < n; i++) if (s[i] && s[i] != ' ') return -1;
    return 0;
}
static bool layout(const char *p) {
    return !strcmp(p,"BUNDLE") || !strcmp(p,"MANIFEST") || !strcmp(p,"SETTINGS") ||
        !strcmp(p,"tree") || !strcmp(p,"placed") || !strncmp(p,"tree/",5) || !strncmp(p,"placed/",7) ||
        !strcmp(p,"capture") || !strcmp(p,"capture/CAPTURE") || !strcmp(p,"capture/FILES") ||
        !strcmp(p,"capture/files") || !strncmp(p,"capture/files/",14);
}
static int pax(char *data, size_t size, char **path, char **link) {
    size_t off = 0;
    while (off < size) {
        char *end;
     unsigned long n = strtoul(data+off, &end, 10);
        if (end == data+off || *end != ' ' || n > size-off || n < (size_t)(end-data-off)+3 || data[off+n-1] != '\n') return -1;
        char *v = end+1, *eq = memchr(v, '=', (size_t)(data+off+n-1-v));
        if (!eq) return -1;
        data[off+n-1] = 0;
     *eq = 0;
        char **dst = !strcmp(v,"path") ? path : !strcmp(v,"linkpath") ? link : NULL;
        if (dst) { free(*dst);
     *dst = strdup(eq+1);
     if (!*dst) return -1;
     }
        else if (strcmp(v,"mtime") && strcmp(v,"atime") && strcmp(v,"ctime")) return -1;
        off += n;
    }
    return 0;
}
int tb_archive_unpack(const char *archive, const char *dest) {
    int in = open(archive,O_RDONLY|O_NOFOLLOW|O_CLOEXEC), root = open(dest,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    int result = -1;
     unsigned char h[512];
     char *xp = NULL, *xl = NULL;
     size_t count = 0;
    if (in < 0 || root < 0) goto done;
    while (!exact(in,h,sizeof h)) {
        if (!h[0]) { if (exact(in,h,sizeof h)) goto done;
            for (size_t i=0;i<sizeof h;i++) if (h[i]) goto done;
            if (xp || xl) goto done;
            result = 0;
     break;
     }
        unsigned long long sum=0, check=0, size=0, mode=0;
        for (size_t i=0;i<sizeof h;i++) sum += i>=148 && i<156 ? ' ' : h[i];
        if (number(h+148,8,&check) || sum!=check || number(h+124,12,&size) || number(h+100,8,&mode) || size > (1ULL<<34)) goto done;
        if (h[156] == 'x') {
            if (size > 65536) goto done;
            char *data = calloc(1,(size_t)size+1);
     if (!data) goto done;
            int r = exact(in,data,(size_t)size) || pax(data,(size_t)size,&xp,&xl);
     free(data);
            if (r) goto done;
        } else {
            char name[257], target[101];
            if (h[345]) snprintf(name,sizeof name,"%.*s/%.*s",155,h+345,100,h);
            else snprintf(name,sizeof name,"%.*s",100,h);
            snprintf(target,sizeof target,"%.*s",100,h+157);
            char *path = xp ? xp : name, *link = xl ? xl : target;
            size_t n = strlen(path);
     if (n && path[n-1]=='/') path[n-1]=0;
            if (!tam_path_is_safe_rel(path) || !layout(path) || mode > 0777) goto done;
            /* Apply metadata and snapshot bounds before extracting bytes. */
            if((!strcmp(path,"capture/CAPTURE") && size>4096) ||
               ((!strcmp(path,"capture/FILES") || !strcmp(path,"SETTINGS") ||
                 !strncmp(path,"capture/files/",14)) && size>16U*1024U*1024U) ||
               (!strncmp(path,"capture/",8) && strlen(path)>=4096))goto done;
            if (count==0 && strcmp(path,"BUNDLE")) goto done;
            if (count==1 && strcmp(path,"MANIFEST")) goto done;
            char *slash = strrchr(path,'/'), *base = slash ? slash+1 : path;
            int parent = root;
            if (slash) { *slash=0;
     parent=tb_open_beneath(root,path);
     *slash='/';
     if(parent<0) goto done;
     }
            int r = 0;
            if (h[156]=='5' && !size) r = mkdirat(parent,base,(mode_t)mode);
            else if ((h[156]=='0' || !h[156])) {
                int out = openat(parent,base,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
                if (out<0) r=-1;
                unsigned long long left=size;
     char buf[65536];
                while (!r && left) { size_t k=left>sizeof buf?sizeof buf:(size_t)left;
                    if (exact(in,buf,k)) {r=-1;
    break;
    }
                    size_t off=0;
     while(off<k) {ssize_t w=write(out,buf+off,k-off);
    if(w<0&&errno==EINTR)continue;
    if(w<=0){r=-1;
    break;
    }off+=(size_t)w;
    }
                    left-=k;
                }
                if(out>=0) {if(fchmod(out,(mode_t)mode))r=-1;
    close(out);
    }
            } else if (h[156]=='2' && !size && !strncmp(path,"tree/",5)) {
                /* Manifest validation checks the lexical link target before use. */
                r=symlinkat(link,parent,base);
            } else r=-1;
            if(parent!=root)close(parent);
            if(r)goto done;
            free(xp);
    free(xl);
    xp=xl=NULL;
    count++;
        }
        size_t pad=(size_t)((512-size%512)%512);
     if(pad && exact(in,h,pad))goto done;
    }
done:
    free(xp);
     free(xl);
     if(in>=0)close(in);
    if(root>=0)close(root);
    if(result)tb_err("invalid or unsafe bundle archive: %s",archive);
    return result;
}
int tb_archive_pack(const char *dir, const char *archive) {
    char *capture=tam_path_join(dir,"capture"); struct stat st;
    bool private=capture && !lstat(capture,&st); free(capture);
    int out=-1, parent=-1; char fdpath[80]; char *temp=NULL,*leaf=NULL;
    if(private) {
        char *copy=strdup(archive),*slash=copy?strrchr(copy,'/'):NULL;
        if(!copy)return -1;
        leaf=strdup(slash?slash+1:copy);
        if(slash)*slash=0;
        parent=open(slash?(*copy?copy:"/"):".",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
        free(copy);
        if(parent<0 || !leaf || !*leaf){if(parent>=0)close(parent); free(leaf); return -1;}
        if(asprintf(&temp,"/proc/self/fd/%d/.tam-bundle-export-XXXXXX",parent)<0)temp=NULL;
        out=temp?mkostemp(temp,O_CLOEXEC):-1;
        if(out<0){close(parent); free(temp); free(leaf); return -1;}
        snprintf(fdpath,sizeof fdpath,"/proc/%ld/fd/%d",(long)getpid(),out);
    }
    const char *argv[]={"/usr/bin/tar","--format=pax","--pax-option=delete=atime,delete=ctime","-cf",private?fdpath:archive,"-C",dir,"BUNDLE","MANIFEST","SETTINGS","placed","tree",private?"capture":NULL,NULL};
    tam_exec_result_t r=tam_exec(argv,120000);
    bool ok=r.exit_code==0;
     if(!ok)tb_err("cannot write bundle archive: %s",r.stderr_buf?r.stderr_buf:"");
    tam_exec_result_free(&r);
    if(out>=0) {
        if(ok && fsync(out))ok=false;
        if(ok)tb_fault("settings-export-file-sync");
        close(out);
        /* Publish complete bytes without replacing an existing archive. */
        if(ok && linkat(parent,strrchr(temp,'/')+1,parent,leaf,0))ok=false;
        if(ok)tb_fault("settings-export-publish");
        unlinkat(parent,strrchr(temp,'/')+1,0);
        if(ok && fsync(parent))ok=false;
        if(ok)tb_fault("settings-export-directory-sync");
        close(parent); free(temp); free(leaf);
    }
     return ok?0:-1;
}
