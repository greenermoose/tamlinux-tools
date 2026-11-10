/* tam-bundle: install a named, versioned set of real files into a slot,
 * record it, and switch between versions with rollback.
 * Design: docs/plans/tam-bundle.md. */
#ifndef TB_H
#define TB_H

#include <tam.h>
#include <stdio.h>

#define TB_VERSION "0.1.0"
#define TB_FORMAT 1               /* newest record format this program reads */
#define TB_DEFAULT_SLOT "/usr/lib/tamlinux"
#define TB_SYSTEM_STATE "/var/lib/tam-bundle"
#define TB_HASH_LEN (7 + TAM_SHA256_HEX_LEN) /* "sha256:" + hex */

/* Exit codes. */
enum {
    TB_OK = 0,
    TB_FAIL = 1,        /* refused, or a check found problems */
    TB_USAGE = 2,
    TB_AFTER_FAILED = 3, /* switched, but a post-install step failed */
    TB_INTERRUPTED = 4,  /* an interrupted switch needs `recover` */
};

/* ---- Records (meta.c) -------------------------------------------------- */

typedef struct {
    char     type;  /* 'd', 'f' or 'l' */
    unsigned mode;  /* permission bits; 0 for links */
    char    *value; /* "sha256:<hex>" for files, the target for links, "-" */
    char    *path;
} tb_entry_t;

typedef struct {
    tb_entry_t *v;
    size_t      n, cap;
    char       *text; /* the MANIFEST bytes as read or written */
    size_t      len;
} tb_manifest_t;

typedef struct {
    char *kind;   /* "command" or "file" */
    char *target;
    char *pkgs;   /* "arch=x void=y", or "" */
} tb_need_t;

typedef struct {
    char      *name, *version, *hash, *host, *built;
    char     **sources; size_t nsources; /* "<component> <commit>" */
    char     **tools;   size_t ntools;
    tb_need_t *needs;   size_t nneeds;
    char     **after;   size_t nafter;
    char      *text;    /* the BUNDLE bytes */
    size_t     len;
    char       id[256]; /* <name>-<version>-<hash 12> */
} tb_bundle_t;

void tb_manifest_free(tb_manifest_t *m);
void tb_bundle_free(tb_bundle_t *b);
const tb_entry_t *tb_manifest_find(const tb_manifest_t *m, const char *path);

/* Parse files already read; on failure print why (prefixed by `what`). */
int  tb_manifest_parse(const char *text, size_t len, const char *what, tb_manifest_t *m);
int  tb_bundle_parse(const char *text, size_t len, const char *what, tb_bundle_t *b);
/* Read and parse BUNDLE and MANIFEST in directory `dir` (relative to dirfd). */
int  tb_tree_read(int dirfd, const char *dir, tb_bundle_t *b, tb_manifest_t *m);
/* The bundle hash: SHA-256 over BUNDLE without its hash and built lines,
 * then MANIFEST. */
void tb_bundle_hash(const char *btext, size_t blen, const char *mtext, size_t mlen,
                    char out[TB_HASH_LEN + 1]);
bool tb_valid_name(const char *s);
bool tb_valid_step(const char *s);
extern const char *const tb_steps[];

/* ---- Trees (tree.c) ---------------------------------------------------- */

typedef struct {
    char    *path;
    long long size, ino, mtime_s, mtime_ns, ctime_s, ctime_ns;
} tb_stat_t;

typedef struct {
    tb_stat_t *v;
    size_t     n, cap;
} tb_stats_t;

typedef struct {
    size_t   edited, missing, extra;
    bool     quiet;          /* do not print each problem */
    char   **paths;          /* edited or extra regular files, for saving */
    size_t   npaths;
} tb_check_t;

/* Build MANIFEST for the tree at `root` (BUNDLE and MANIFEST excluded). */
int  tb_manifest_build(const char *root, tb_manifest_t *m);
/* Compare the tree at `root` with `m`. With `stats` (may be NULL) and not
 * `full`, a file whose size, times and inode match is not hashed. Returns 0
 * when the walk completed (problems are counted in `out`), -1 on error. */
int  tb_tree_check(const char *root, const tb_manifest_t *m, const tb_stats_t *stats,
                   bool full, tb_check_t *out);
void tb_check_free(tb_check_t *c);
int  tb_stats_build(const char *root, const tb_manifest_t *m, tb_stats_t *s);
int  tb_stats_write(const char *path, const tb_stats_t *s);
int  tb_stats_read(const char *path, tb_stats_t *s); /* missing file: empty, 0 */
void tb_stats_free(tb_stats_t *s);
long long tb_tree_bytes(const char *root);

/* ---- Slots and state (slot.c) ------------------------------------------ */

typedef struct {
    char *dir;      /* slot directory, absolute */
    int   fd;
    char *name;     /* slot name: the directory's last component */
    char *state;    /* state directory */
    int   lock_fd;
    dev_t dev;
} tb_slot_t;

typedef struct {
    char op[16];      /* "switch" or "back" */
    char from[256];   /* bundle id, or "-" for an empty slot */
    char to[256];
    char source[300]; /* slot-relative path exchanged with current */
} tb_intent_t;

typedef struct {
    char  **ids;
    char  **reasons;
    size_t  n;
} tb_keep_t;

/* Open a slot. `state` NULL picks /var/lib/tam-bundle/<name> for a slot
 * owned by root and $XDG_STATE_HOME/tamlinux/tam-bundle/<name> otherwise. With `create`, make
 * the slot, kept/ and state directories where missing. */
int  tb_slot_open(tb_slot_t *s, const char *dir, const char *state, bool create);
void tb_slot_close(tb_slot_t *s);
int  tb_slot_lock(tb_slot_t *s);
char *tb_state_path(const tb_slot_t *s, const char *leaf); /* caller frees */
/* Remove a slot-relative tree ("kept/<id>", "staging.<id>"), making its
 * directories writable first. Missing is success. */
int  tb_remove_tree(const tb_slot_t *s, const char *rel);

int  tb_intent_read(const tb_slot_t *s, tb_intent_t *it); /* 1 present, 0 none, -1 error */
int  tb_intent_write(const tb_slot_t *s, const tb_intent_t *it);
int  tb_intent_clear(const tb_slot_t *s);

int  tb_keep_read(const tb_slot_t *s, tb_keep_t *k);
int  tb_keep_write(const tb_slot_t *s, const tb_keep_t *k);
void tb_keep_set(tb_keep_t *k, const char *id, const char *reason);
void tb_keep_drop(tb_keep_t *k, const char *id);
const char *tb_keep_reason(const tb_keep_t *k, const char *id);
const char *tb_keep_previous(const tb_keep_t *k);
void tb_keep_free(tb_keep_t *k);

/* Append one line to the log: `<UTC time> <text>`. */
int  tb_log(const tb_slot_t *s, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
/* The live bundle id in `out`; "-" when the slot has no current/. */
int  tb_current_id(const tb_slot_t *s, char out[256]);

/* ---- Operations (switch.c) --------------------------------------------- */

typedef struct {
    bool save_edits;
    bool dry_run;
    bool full;
} tb_switch_opts_t;

int  tb_switch(tb_slot_t *s, const char *bundle_dir, const tb_switch_opts_t *o);
int  tb_back(tb_slot_t *s, const char *id, const tb_switch_opts_t *o);
/* Finish or undo an interrupted switch. Returns TB_OK when the slot is clean
 * afterwards; sets *ran_after_due when post-install steps are still due. */
int  tb_recover(tb_slot_t *s, bool dry_run, bool *steps_due);

/* ---- Steps and needs (after.c) ----------------------------------------- */

int  tb_run_after(const tb_slot_t *s, const tb_bundle_t *b);
/* Print missing needs; returns the number missing. */
size_t tb_needs_check(const tb_bundle_t *b, bool print);

/* ---- Test fault points (fault.c) --------------------------------------- */

/* In the test build, exit at once when $TAM_BUNDLE_FAULT names `point`;
 * elsewhere it does nothing. */
void tb_fault(const char *point);
void tb_fault_file(size_t index);

/* ---- Helpers (util.c) -------------------------------------------------- */

char *tb_read_at(int dirfd, const char *rel, size_t *len); /* NUL-terminated */
/* Open a file for reading strictly beneath dirfd: no symbolic link in any
 * component (openat2 RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS). */
int   tb_open_beneath(int dirfd, const char *rel);
void  tb_err(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void  tb_note(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
extern bool tb_quiet;

#endif
