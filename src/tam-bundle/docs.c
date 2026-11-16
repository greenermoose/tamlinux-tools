/* Optional documentation is data under three fixed /usr/share/doc roots. */
#include "tb.h"
#include <stdlib.h>
#include <string.h>

unsigned tb_doc_bit(const char *name) {
    return !strcmp(name,"info")?1:!strcmp(name,"tldr")?2:!strcmp(name,"archwiki")?4:0;
}

int tb_docs_parse(const char *selection,unsigned *mask) {
    *mask=0;
    if(!strcmp(selection,"none"))return 0;
    if(!*selection || selection[0]==',' || selection[strlen(selection)-1]==',' || strstr(selection,",,"))return -1;
    char *copy=strdup(selection);
    if(!copy)return -1;
    int result=0;
    for(char *save=NULL,*name=strtok_r(copy,",",&save);name;name=strtok_r(NULL,",",&save)) {
        unsigned bit=tb_doc_bit(name);
        if(!bit || (*mask & bit)){result=-1;break;}
        *mask|=bit;
    }
    free(copy);return result;
}

unsigned tb_doc_path(const char *path) {
    const char *roots[]={"placed/usr/share/doc/info","placed/usr/share/doc/tldr","placed/usr/share/doc/arch-wiki"};
    for(unsigned i=0;i<3;i++) {
        size_t n=strlen(roots[i]);
        if(!strncmp(path,roots[i],n) && (!path[n] || path[n]=='/'))return 1U<<i;
    }
    return 0;
}

void tb_docs_filter(tb_manifest_t *manifest,unsigned declared,unsigned selected) {
    size_t count=0;
    for(size_t i=0;i<manifest->n;i++) {
        tb_entry_t *e=&manifest->v[i];
        unsigned group=tb_doc_path(e->path) & declared;
        if(group && !(group & selected)){free(e->path);free(e->value);continue;}
        manifest->v[count++]=*e;
    }
    manifest->n=count;
}
