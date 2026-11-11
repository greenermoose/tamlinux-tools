/* Sealing: write BUNDLE and MANIFEST for a built tree. `seal` and the last
 * step of `build` both come here. */
#include "tb.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

int tb_seal(const char *dir, const tb_seal_t *o, char id[256]) {
    int dfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd < 0) {
        tb_err("cannot open %s: %s", dir, strerror(errno));
        return TB_FAIL;
    }
    struct stat st;
    bool present = fstatat(dfd, "BUNDLE", &st, AT_SYMLINK_NOFOLLOW) == 0 ||
                   fstatat(dfd, "MANIFEST", &st, AT_SYMLINK_NOFOLLOW) == 0;
    close(dfd);
    if (present) {
        tb_err("%s already has a BUNDLE or MANIFEST; remove them to seal it again", dir);
        return TB_FAIL;
    }
    tb_manifest_t m;
    if ((o->format == 2 ? tb_v2_manifest(dir, &m) : tb_manifest_build(dir, &m)) != 0) return TB_FAIL;
    if(!tb_buildenv_matches(o->buildenv,&m)){tb_err("buildenv does not match the carried recipe/BUILDENV record"); tb_manifest_free(&m); return TB_FAIL;}

    char built[64];
    if (o->built) {
        snprintf(built, sizeof built, "%s", o->built);
    } else {
        time_t now = time(NULL);
        struct tm tm;
        gmtime_r(&now, &tm);
        strftime(built, sizeof built, "%Y-%m-%dT%H:%M:%SZ", &tm);
    }
    /* Everything but the hash, in a fixed order; the hash covers it. */
    tam_str_t head = tam_str_new(o->format == 2 ? "format 2\n" : "format 1\n");
    head = tam_str_cat_printf(head, "name %s\nversion %s\n", o->name, o->version);
    tam_str_t tail = tam_str_new(NULL);
    if(o->buildenv)tail=tam_str_cat_printf(tail,"buildenv %s\n",o->buildenv);
    if(o->derived)tail=tam_str_cat_printf(tail,"derived-from %s\n",o->derived);
    for(size_t i=0;i<o->nedits;i++) {
        const char *fields[]={"edit",o->edits[i]};
        tail=tam_rec_cat(tail,fields,2);
    }
    for (size_t i = 0; i < o->nsources; i++) tail = tam_str_cat_printf(tail, "source %s\n", o->sources[i]);
    for (size_t i = 0; i < o->ntools; i++) tail = tam_str_cat_printf(tail, "tool %s\n", o->tools[i]);
    tail = tam_str_cat_printf(tail, "tool tam-bundle %s\n", TB_VERSION);
    tail = tam_str_cat_printf(tail, "host %s\n", o->host ? o->host : "any");
    for (size_t i = 0; i < o->ntargets; i++) tail = tam_str_cat_printf(tail, "target %s\n", o->targets[i]);
    for (size_t i = 0; i < o->nneeds; i++) tail = tam_str_cat_printf(tail, "needs %s\n", o->needs[i]);
    for (size_t i = 0; i < o->nafter; i++) tail = tam_str_cat_printf(tail, "after %s\n", o->after[i]);
    tail = tam_str_cat_printf(tail, "built %s\n", built);

    tam_str_t unhashed = tam_str_dup(head);
    unhashed = tam_str_cat(unhashed, tail);
    char hash[TB_HASH_LEN + 1];
    size_t slen = 0;
    char *settings = NULL, *spath = tam_path_join(dir, "SETTINGS");
    if (o->format == 2) {
        settings = tb_read_at(AT_FDCWD, spath, &slen);
        if (!settings) {
            settings = strdup("format 1\n"); slen = strlen(settings);
            if (!tam_fs_write_atomic(spath, settings, slen, 0644)) { free(settings); settings = NULL; }
        }
    }
    tb_hash_settings(unhashed, tam_str_len(unhashed), m.text, m.len, settings, slen, hash);
    free(settings); free(spath);
    tam_str_t text = tam_str_dup(head);
    text = tam_str_cat_printf(text, "hash %s\n", hash);
    text = tam_str_cat(text, tail);
    tam_str_free(head);
    tam_str_free(tail);
    tam_str_free(unhashed);

    int result = TB_FAIL;
    tb_bundle_t b;
    if (text && tb_bundle_parse(text, tam_str_len(text), "the new BUNDLE", &b) == 0) {
        char *mpath = tam_path_join(dir, "MANIFEST"), *bpath = tam_path_join(dir, "BUNDLE");
        /* MANIFEST first: a tree with a BUNDLE always has its MANIFEST. */
        if (tam_fs_write_atomic(mpath, m.text, m.len, 0644) &&
            tam_fs_write_atomic(bpath, text, tam_str_len(text), 0644)) {
            tb_note("sealed %s as %s (%zu entries)", dir, b.id, m.n);
            if (id) snprintf(id, 256, "%s", b.id);
            result = TB_OK;
        } else {
            tb_err("cannot write the records into %s: %s", dir, strerror(errno));
        }
        free(mpath);
        free(bpath);
        tb_bundle_free(&b);
    }
    tam_str_free(text);
    tb_manifest_free(&m);
    return result;
}
