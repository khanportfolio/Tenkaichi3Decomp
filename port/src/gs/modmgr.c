/*
 * The Mod Manager's library (main menu, "Mod Manager": src/menu/port_modmgr.c, port_modmgr2.c, port_mmkit.c draw it):
 * the character packages of `characters` (port/src/plat_charas.c, docs/port/character_packages.md) as the manager's
 * list. Nothing of its own is kept on disk: a package is its folder and its character.txt, which is on or off and in
 * which order is load_order.txt (PortCharas_OrderWrite), and every change is read back from there
 * (PortCharas_Reload). The menu's code calls these with the game's pointers; the records it gets are plain
 * (include/menu/port_mods.h).
 *
 * A "mod" of the manager is a folder directly in `characters`: one that replaces a disc character (replaces=) or adds
 * one (a "new fighter", on the Mod Fighters grid, in the order the added characters take their ids). Its model on
 * the manager's platform is the package's files answering for the disc character it replaces or stands on
 * (PortCharas_Preview), on or not.
 *
 * Online (a session) none of it is there: the package reader answers as if there were no folder.
 */
#include <SDL3/SDL.h>
#include <ctype.h>
#include <math.h>
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define mkdir_one(p) _mkdir(p)
#else
#define mkdir_one(p) mkdir(p, 0755)
#endif

typedef int32_t s32;
typedef uint16_t u16;
typedef uint32_t u32;
#include "../../../include/menu/port_mods.h"
#include "../../include/plat_charas.h"

extern int Port_NetSession(void);
extern int Port_NetModsHidden(void); /* port/src/gs/net.c: online, unless both players have the same mods */                /* gs/net.c */
extern char *PortTbo_FighterName(s32 chara);     /* src/menu/port_tbo.c */
extern const char *Port_CharaName(int index);    /* plat_charas.c: an added character's name */
extern void PortTbo_TextMode(int on);            /* gs_input.c */
extern int PortTbo_TextRead(void);
extern int PortInput_DropTake(char *out, unsigned size);

#define LIB_MAX 256
#define DISC 161

typedef struct {
    char folder[128]; /* "Name", or "Name/Form" for a form (a package inside a package) */
    PortCharaDesc d;
    int on;
    int place;   /* its place in the order of use (load order), 0 first */
    int form;    /* a form: on, off and its place are its package's */
} Pkg;

static Pkg sLib[LIB_MAX];
static int sLibCount;
static int sLibRead;
static long sStamp;
static char sBrowse[700];     /* the folder the browser looked at (Port_DirLook) */
static int sBrowseChara = -1;
static int sChipEdit[3];      /* Port_ModSet 3..5: the chip's square, written with the last of the three */

/* ---- the library ---------------------------------------------------------------------------------------------- */

static int name_cmp(const void *a, const void *b) {
    const Pkg *x = a, *y = b;
    return SDL_strcasecmp(x->folder, y->folder);
}

static long folder_stamp(void) {
    char root[300], path[420];
    struct stat st;
    long stamp = 0;
    DIR *d;
    struct dirent *e;
    if (!PortCharas_Root(root, sizeof(root)) || (d = opendir(root)) == NULL) {
        return 0;
    }
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') {
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s", root, e->d_name);
        if (stat(path, &st) == 0) {
            stamp = stamp * 31 + (long)st.st_mtime + (long)st.st_size;
        }
        stamp++;
    }
    closedir(d);
    return stamp;
}

/* The folders and what each is: every folder directly in `characters` that is a package, by name. */
static void lib_read(void) {
    static char order[LIB_MAX][128];
    static unsigned char on[LIB_MAX];
    int n = PortCharas_OrderGet(order, on, LIB_MAX), i, k;

    sLibCount = 0;
    for (i = 0; i < n && sLibCount < LIB_MAX; i++) {
        Pkg *p = &sLib[sLibCount];
        if (strlen(order[i]) >= sizeof(p->folder) || !PortCharas_Describe(order[i], &p->d)) {
            continue;
        }
        snprintf(p->folder, sizeof(p->folder), "%s", order[i]);
        p->on = on[i];
        p->place = i;
        p->form = 0;
        sLibCount++;
        {   /* its forms (the folders inside it that are packages: added characters of their own, as the game reads
               them), each an entry of its own after it */
            static char forms[64][128];
            int m = PortCharas_Forms(order[i], forms, 64), f;
            for (f = 0; f < m && sLibCount < LIB_MAX; f++) {
                Pkg *q = &sLib[sLibCount];
                if (strlen(order[i]) + 1 + strlen(forms[f]) >= sizeof(q->folder) || !PortCharas_DescribeForm(order[i], forms[f], &q->d)) {
                    continue;
                }
                snprintf(q->folder, sizeof(q->folder), "%s/%s", order[i], forms[f]);
                q->on = on[i];
                q->place = i;
                q->form = 1;
                sLibCount++;
            }
        }
    }
    qsort(sLib, (size_t)sLibCount, sizeof(Pkg), name_cmp);
    for (k = 0; k < 3; k++) {
        sChipEdit[k] = 0;
    }
    sStamp = folder_stamp();
    sLibRead = 1;
}

static void ensure(void) {
    if (!sLibRead) {
        lib_read();
    }
}

static int valid(int i) {
    ensure();
    return i >= 0 && i < sLibCount && !Port_NetModsHidden();
}

/* Writes the order of use with folder `at` moved to `to` (0 first; -1 where it is) and switched `on` (-1 as it is),
   then reads the packages again. */
static void order_put(const char *whole, int to, int on) {
    static char order[LIB_MAX][128];
    static unsigned char ons[LIB_MAX];
    int n = PortCharas_OrderGet(order, ons, LIB_MAX), i, from = -1;
    char keep[128], folder[128];
    unsigned char keepOn;

    snprintf(folder, sizeof(folder), "%s", whole);
    if (strchr(folder, '/') != NULL) {
        *strchr(folder, '/') = '\0'; /* (a form: its package is what is in the order) */
    }

    for (i = 0; i < n; i++) {
        if (strcmp(order[i], folder) == 0) {
            from = i;
        }
    }
    if (from < 0) {
        return;
    }
    if (on >= 0) {
        ons[from] = (unsigned char)(on != 0);
    }
    if (to >= 0 && to < n && to != from) {
        snprintf(keep, sizeof(keep), "%s", order[from]);
        keepOn = ons[from];
        if (to < from) {
            memmove(order[to + 1], order[to], (size_t)(from - to) * sizeof(order[0]));
            memmove(ons + to + 1, ons + to, (size_t)(from - to));
        } else {
            memmove(order[from], order[from + 1], (size_t)(to - from) * sizeof(order[0]));
            memmove(ons + from, ons + from + 1, (size_t)(to - from));
        }
        snprintf(order[to], sizeof(order[0]), "%s", keep);
        ons[to] = keepOn;
    }
    PortCharas_OrderWrite((const char(*)[128])order, ons, n);
}

/* After a change: the packages read again, then the list. */
static void changed(void) {
    PortCharas_Reload();
    lib_read();
}

static int chara_of(const Pkg *p) {
    int c = p->d.replaces >= 0 ? p->d.replaces : p->d.base;
    return c >= 0 && c < DISC ? c : 0;
}

/* Another package that is on and replaces the same disc character (only one of them is used: the first in the order). */
static int rival_of(int i) {
    int j, best = -1;
    if (sLib[i].d.replaces < 0) {
        return -1;
    }
    for (j = 0; j < sLibCount; j++) {
        if (j != i && sLib[j].on && sLib[j].d.replaces == sLib[i].d.replaces && (best < 0 || sLib[j].place < sLib[best].place)) {
            best = j;
        }
    }
    return best;
}

/* The added characters that are on, in the order they take their ids: the Mod Fighters grid. */
static int skins(int *out, int max) {
    int n = 0, i, j;
    for (i = 0; i < sLibCount && n < max; i++) {
        if (sLib[i].on && sLib[i].d.replaces < 0) {
            out[n++] = i;
        }
    }
    for (i = 1; i < n; i++) { /* (by place) */
        int v = out[i];
        for (j = i; j > 0 && sLib[out[j - 1]].place > sLib[v].place; j--) {
            out[j] = out[j - 1];
        }
        out[j] = v;
    }
    return n;
}

static int problem_of(const PortCharaDesc *d) {
    if (d->problem[0] == '\0') {
        return 0;
    }
    if (strstr(d->problem, "more than") != NULL) {
        return 2;
    }
    if (strstr(d->problem, "empty") != NULL) {
        return 3;
    }
    if (strstr(d->problem, "chip") != NULL) {
        return 4;
    }
    return 1;
}

void Port_ModRescan(void) {
    if (!Port_NetSession()) {
        PortCharas_Reload();
    }
    lib_read();
}

s32 Port_ModsChanged(void) {
    return sLibRead && folder_stamp() != sStamp;
}

s32 Port_ModCount(void) {
    ensure();
    return Port_NetModsHidden() ? 0 : sLibCount;
}

void Port_ModGetInfo(s32 i, PortModInfo *out) {
    const Pkg *p;
    int list[LIB_MAX], n, k, onCount = 0, before = 0;
    char v[64];

    memset(out, 0, sizeof(*out));
    out->skin = out->rival = -1;
    out->chara = -1;
    if (!valid(i)) {
        return;
    }
    p = &sLib[i];
    snprintf(out->name, sizeof(out->name), "%s", p->folder);
    out->chara = chara_of(p);
    Port_ModCharaName(out->chara, out->target, sizeof(out->target));
    if (p->d.nameSet) {
        snprintf(out->white, sizeof(out->white), "%s", p->d.name);
    }
    if (p->d.labelSet) {
        snprintf(out->yellow, sizeof(out->yellow), "%s", p->d.label);
    }
    out->named = p->d.nameSet || p->d.labelSet;
    out->on = p->on;
    out->kinds = (p->d.files & 0x3FF) | ((p->d.files >> PORT_DESC_PORTRAIT) & 1) << 10;
    out->costumes = p->d.costumes;
    for (k = 0; k < 11; k++) {
        out->files += (out->kinds >> k) & 1;
    }
    out->large = p->d.large;
    out->isNew = p->d.replaces < 0;
    out->problem = problem_of(&p->d);
    out->charas = 1;
    out->charaList[0] = out->chara;
    out->art[3] = out->artH = 100;
    if (PortCharas_ManifestGet(p->folder, "portrait_frame", v, sizeof(v)) &&
        sscanf(v, "%d,%d,%d,%d", &out->art[1], &out->art[2], &out->art[3], &out->artH) == 4) {
        out->art[0] = 1; /* (a portrait made in the manager: its frame, to open it again) */
    }
    if (PortCharas_ManifestGet(p->folder, "chip_square", v, sizeof(v))) {
        sscanf(v, "%d,%d,%d", &out->chip[0], &out->chip[1], &out->chip[2]);
    }
    for (k = 0; k < sLibCount; k++) { /* (the packages: a form is counted with its package) */
        onCount += sLib[k].on && !sLib[k].form;
        before += sLib[k].on && !sLib[k].form && sLib[k].place < p->place;
    }
    out->onCount = onCount;
    out->priority = p->on ? before + 1 : 0;
    out->rival = rival_of(i);
    out->conflict = p->on && out->rival >= 0;
    out->wins = !out->conflict || sLib[out->rival].place > p->place;
    n = skins(list, LIB_MAX);
    for (k = 0; k < n; k++) {
        if (list[k] == i) {
            out->skin = k;
        }
    }
}

/* Switches a package on or off. One switched on goes first in the order (it wins), and another that is on for the
   same disc character is switched off; that one is returned, -1 none. */
s32 Port_ModToggle(s32 i) {
    int rival, off = -1;
    char folder[128], other[128] = "";
    if (!valid(i)) {
        return -1;
    }
    snprintf(folder, sizeof(folder), "%s", sLib[i].folder);
    if (sLib[i].on) {
        order_put(folder, -1, 0);
    } else {
        rival = rival_of(i);
        if (rival >= 0) {
            snprintf(other, sizeof(other), "%s", sLib[rival].folder);
            order_put(other, -1, 0);
        }
        order_put(folder, 0, 1);
    }
    changed();
    for (i = 0; other[0] != '\0' && i < sLibCount; i++) {
        if (strcmp(sLib[i].folder, other) == 0) {
            off = i;
        }
    }
    return off;
}

/* -1: one place earlier in the order of those that are on (it wins over one more); +1 one later. */
void Port_ModPriority(s32 i, s32 dir) {
    int j, best = -1;
    if (!valid(i) || !sLib[i].on) {
        return;
    }
    for (j = 0; j < sLibCount; j++) { /* the next one that is on, before or after it */
        if (j == i || !sLib[j].on) {
            continue;
        }
        if (dir < 0 && sLib[j].place < sLib[i].place && (best < 0 || sLib[j].place > sLib[best].place)) {
            best = j;
        }
        if (dir > 0 && sLib[j].place > sLib[i].place && (best < 0 || sLib[j].place < sLib[best].place)) {
            best = j;
        }
    }
    if (best >= 0) {
        order_put(sLib[i].folder, sLib[best].place, -1);
        changed();
    }
}

/* The package whose files the platform shows (-1 none, -2 the folder the browser looked at), for the disc character
   it replaces or stands on. */
void Port_ModPreview(s32 i) {
    char root[300], path[400];
    if (i == -2 && sBrowse[0] != '\0') {
        PortCharas_Preview(sBrowse, sBrowseChara >= 0 ? sBrowseChara : 0);
        return;
    }
    if (!valid(i) || !PortCharas_Root(root, sizeof(root))) {
        PortCharas_Preview(NULL, -1);
        return;
    }
    snprintf(path, sizeof(path), "%s/%s", root, sLib[i].folder);
    PortCharas_Preview(path, chara_of(&sLib[i]));
}

/* ---- the icon made from the portrait: the chip's square (Port_ModSet 3..5) -------------------------------------
 * chip.bin is one texture of the game: 8 bits a pixel with 256 colours of 4 bytes (the alpha 0..128), the pixels and
 * the colours in the order the console's video memory keeps them, each after the commands that upload it (0x60
 * bytes). portrait.bin is the same kind of file, packed with the game's byte pair coder (Bpe_Decode). The square
 * (in the portrait's own pixels, as the crop screen shows it) is averaged down to the chip's size and each pixel
 * given the nearest of the portrait's colours, which the chip takes over whole. The chip it is written into is the
 * package's own (kept as chip_original.bin, which nothing reads, and put back by "automatic"), else the base
 * character's from the disc (the character select's pack: menu archive file 0x1C1, section 2 packed, its section
 * 45, entry id + 1). Every offset of the files is checked: they are a mod's, or a damaged disc's. */
enum { CHIP_BYTES = 5504, PIC_HEAD = 0x60, PORTRAIT_MAX = 0x100000, ARCHIVE_MAX = 0x4000000 };
static int exists(const char *path);

static uint32_t rd32(const unsigned char *b) {
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

/* Bpe_Decode (src/sys/bpe.c): the unpacked size and the packed size, then blocks of a pair table and bytes. A new
   buffer of the unpacked size (no more is written, so a table that loops ends there), *size; NULL if it is not one. */
static unsigned char *bpe_unpack(const unsigned char *src, long n, long max, long *size) {
    long want, packed, p = 8, end, got = 0;
    unsigned char *out;
    if (n < 8 || (want = (int32_t)rd32(src)) <= 0 || want > max || (packed = (int32_t)rd32(src + 4)) <= 0 || packed > n - 8 ||
        (out = malloc((size_t)want)) == NULL) {
        return NULL;
    }
    end = 8 + packed;
    while (p < end && got < want) {
        unsigned char left[256], right[256], stack[1024];
        int c = 0, count, k, depth = 0;
        long len;
        for (k = 0; k < 256; k++) {
            left[k] = (unsigned char)k;
            right[k] = 0;
        }
        count = src[p++];
        for (;;) {
            if (count > 127) {
                c += count - 127;
                count = 0;
            }
            if (c >= 256) {
                break;
            }
            for (k = 0; k <= count; k++) {
                if (c >= 256 || p >= end) {
                    free(out);
                    return NULL;
                }
                left[c] = src[p++];
                if (c != left[c]) {
                    if (p >= end) {
                        free(out);
                        return NULL;
                    }
                    right[c] = src[p++];
                }
                c++;
            }
            if (c >= 256) {
                break;
            }
            if (p >= end) {
                free(out);
                return NULL;
            }
            count = src[p++];
        }
        if (p + 2 > end) {
            free(out);
            return NULL;
        }
        len = (long)src[p] << 8 | src[p + 1];
        p += 2;
        if (len > end - p) {
            free(out);
            return NULL;
        }
        while (got < want) {
            if (depth > 0) {
                c = stack[--depth];
            } else if (len > 0) {
                len--;
                c = src[p++];
            } else {
                break;
            }
            if (c == left[c]) {
                out[got++] = (unsigned char)c;
            } else if (depth + 2 <= (int)sizeof(stack)) {
                stack[depth++] = right[c];
                stack[depth++] = left[c];
            } else {
                free(out);
                return NULL;
            }
        }
        p += len; /* (what was left of the block when the size was reached) */
    }
    if (got < want) {
        free(out);
        return NULL;
    }
    *size = want;
    return out;
}

/* A picture file: one texture, 8 bits a pixel, 256 colours. Its size and where its pixels and colours are. */
static int pic_parse(const unsigned char *b, long n, int *w, int *h, long *pix, long *clut) {
    long rec, p, c, ps, cs;
    uint64_t tex0;
    int psm;
    if (n < PIC_HEAD || rd32(b) != 1 || (rec = (long)rd32(b + 4) * 4) < 8 || rec > n - 0x38) {
        return 0;
    }
    p = (long)rd32(b + rec) * 4;
    c = (long)rd32(b + rec + 4) * 4;
    ps = (int32_t)rd32(b + rec + 8);
    cs = (int32_t)rd32(b + rec + 12);
    tex0 = (uint64_t)rd32(b + rec + 0x30) | (uint64_t)rd32(b + rec + 0x34) << 32;
    psm = (int)(tex0 >> 20 & 0x3F);
    *w = 1 << (tex0 >> 26 & 0xF);
    *h = 1 << (tex0 >> 30 & 0xF);
    if (psm != 0x13 || *w < 16 || *h < 16 || *w > 1024 || *h > 1024 || ps != PIC_HEAD + (long)*w * *h + 0x20 ||
        cs != PIC_HEAD + 1024 + 0x20 || p < 0 || c < 0 || p > n - ps || c > n - cs) {
        return 0;
    }
    *pix = p + PIC_HEAD;
    *clut = c + PIC_HEAD;
    return 1;
}

/* where pixel (x, y) of an 8-bit texture `w` wide lies in its stored bytes; where colour i lies among the colours */
static long pic_place(int x, int y, int w) {
    long block = (long)(y & ~0xF) * w + (x & ~0xF) * 2;
    int swap = ((y + 2) >> 2 & 1) * 4, row = (((y & ~3) >> 1) + (y & 1)) & 7;
    return block + (long)row * w * 2 + ((x + swap) & 7) * 4 + (y >> 1 & 1) + (x >> 2 & 2);
}
static int pic_slot(int i) {
    return (i & 0x18) == 8 || (i & 0x18) == 16 ? i ^ 0x18 : i;
}

static unsigned char *file_all(const char *path, long max, long *n) {
    FILE *f = fopen(path, "rb");
    unsigned char *d = NULL;
    long len;
    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) == 0 && (len = ftell(f)) > 0 && len <= max && fseek(f, 0, SEEK_SET) == 0 && (d = malloc((size_t)len)) != NULL &&
        fread(d, 1, (size_t)len, f) == (size_t)len) {
        *n = len;
    } else {
        free(d);
        d = NULL;
    }
    fclose(f);
    return d;
}

/* Section i (from 1) of a pack: word 0 the count, word k the offset of section k (the low two bits are flags). */
static int pack_section(const unsigned char *b, long n, unsigned i, long *at, long *len) {
    unsigned count;
    long a, z;
    if (n < 8 || (count = rd32(b)) == 0 || i < 1 || i > count || (long)(i + 1) * 4 + 4 > n) {
        return 0;
    }
    a = (long)(rd32(b + 4 * i) & ~3u);
    z = i < count ? (long)(rd32(b + 4 * (i + 1)) & ~3u) : n;
    if (a < 0 || z < a || z > n) {
        return 0;
    }
    *at = a;
    *len = z - a;
    return 1;
}

/* The base character's chip of the disc, into out (CHIP_BYTES). */
static int disc_chip(int chara, unsigned char *out) {
    const char *data = getenv("BT3_DATA");
    char path[600];
    unsigned char *arc, *res;
    long n, at, len, size, at2, len2, at3, len3;
    int ok = 0;
    snprintf(path, sizeof(path), "%s/pzs3us1/00448.bin", data != NULL && data[0] != '\0' ? data : "gamedata");
    if ((arc = file_all(path, ARCHIVE_MAX, &n)) == NULL) {
        return 0;
    }
    if (pack_section(arc, n, 2, &at, &len) && (res = bpe_unpack(arc + at, len, ARCHIVE_MAX, &size)) != NULL) {
        if (pack_section(res, size, 45, &at2, &len2) && pack_section(res + at2, len2, (unsigned)chara + 1, &at3, &len3)) {
            memset(out, 0, CHIP_BYTES);
            memcpy(out, res + at2 + at3, (size_t)(len3 < CHIP_BYTES ? len3 : CHIP_BYTES));
            ok = 1;
        }
        free(res);
    }
    free(arc);
    return ok;
}

/* The package's portrait, unpacked: a new buffer, *n its size. */
static unsigned char *portrait_body(int i, long *n) {
    unsigned char *raw = malloc(PORTRAIT_MAX), *body;
    long len;
    if (raw == NULL) {
        return NULL;
    }
    len = Port_ModPortrait(i, raw, PORTRAIT_MAX);
    if (len < 8) {
        free(raw);
        return NULL;
    }
    if (rd32(raw) != 1 && (body = bpe_unpack(raw, len, PORTRAIT_MAX * 4, n)) != NULL) {
        free(raw);
        return body;
    }
    *n = len; /* (a portrait kept unpacked) */
    return raw;
}

static int put_file(const char *path, const unsigned char *d, long n) {
    char tmp[620];
    FILE *f;
    snprintf(tmp, sizeof(tmp), "%s.new", path);
    if ((f = fopen(tmp, "wb")) == NULL) {
        return 0;
    }
    if (fwrite(d, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        remove(tmp);
        return 0;
    }
    fclose(f);
    remove(path); /* (Windows does not rename over a file) */
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return 0;
    }
    return 1;
}

/* Makes package i's chip.bin from the square (x, y, s) of its portrait; 0 (and why) if it could not. `ours`: the
   chip.bin there is one made here before (its square was set), not the package's own. */
static int chip_make(int i, int x, int y, int s, int ours, const char *dir, char *why, size_t whyLen) {
    unsigned char tpl[CHIP_BYTES], *body;
    char path[600], keep[620];
    long n, pix, clut, cpix, cclut, got = 0;
    int w, h, cw, ch, ox, oy, k, clear = 0, ax, ay, aw, ah, sh, top;
    unsigned char *f;

    if ((body = portrait_body(i, &n)) == NULL || !pic_parse(body, n, &w, &h, &pix, &clut)) {
        snprintf(why, whyLen, "its portrait is not a picture file of the game");
        free(body);
        return 0;
    }
    if (s < 1 || x < 0 || y < 0 || x > w - s || y > h - s) {
        snprintf(why, whyLen, "the square is outside the portrait (%dx%d)", w, h);
        free(body);
        return 0;
    }
    /* the chip to write into: the package's own (or kept) one, else the disc's */
    snprintf(keep, sizeof(keep), "%s/chip_original.bin", dir);
    snprintf(path, sizeof(path), "%s/chip.bin", dir);
    if ((f = file_all(keep, CHIP_BYTES, &got)) == NULL && !ours) {
        f = file_all(path, CHIP_BYTES, &got);
    }
    memset(tpl, 0, sizeof(tpl));
    if (f != NULL) {
        memcpy(tpl, f, (size_t)got);
        free(f);
    }
    if ((f == NULL || !pic_parse(tpl, CHIP_BYTES, &cw, &ch, &cpix, &cclut)) && !disc_chip(chara_of(&sLib[i]), tpl)) {
        snprintf(why, whyLen, "no chip to start from (the package's or the disc's)");
        free(body);
        return 0;
    }
    if (!pic_parse(tpl, CHIP_BYTES, &cw, &ch, &cpix, &cclut)) {
        snprintf(why, whyLen, "the chip to start from is not a picture file of the game");
        free(body);
        return 0;
    }
    for (k = 1; k < 256; k++) { /* the most transparent of the colours, for what is clear */
        if (body[clut + pic_slot(k) * 4 + 3] < body[clut + pic_slot(clear) * 4 + 3]) {
            clear = k;
        }
    }
    /* The disc's chips hold their picture in 48 x 41 of the 64 x 64 (x 8..55, y 11..51: measured on all 161),
       clear around it: that is what lines up with the select screen's box. The square goes there, its full width and
       a slice of its height of the same shape (the middle 41/48 of it); the rest is clear. */
    ax = cw * 8 / 64;
    ay = ch * 11 / 64;
    aw = cw * 48 / 64;
    ah = ch * 41 / 64;
    sh = s * 41 / 48;
    top = y + (s - sh) / 2;
    for (oy = 0; oy < ch; oy++) {
        for (ox = 0; ox < cw; ox++) {
            int x0 = x + (ox - ax) * s / aw, x1 = x + (ox - ax + 1) * s / aw, y0 = top + (oy - ay) * sh / ah, y1 = top + (oy - ay + 1) * sh / ah;
            int xx, yy, best = clear;
            long r = 0, g = 0, b = 0, a = 0, cnt = 0, bestD = -1;
            if (ox < ax || ox >= ax + aw || oy < ay || oy >= ay + ah) {
                tpl[cpix + pic_place(ox, oy, cw)] = (unsigned char)clear;
                continue;
            }
            x1 = x1 > x0 ? x1 : x0 + 1;
            y1 = y1 > y0 ? y1 : y0 + 1;
            for (yy = y0; yy < y1; yy++) {
                for (xx = x0; xx < x1; xx++) {
                    const unsigned char *c = body + clut + pic_slot(body[pix + pic_place(xx, yy, w)]) * 4;
                    r += (long)c[0] * c[3];
                    g += (long)c[1] * c[3];
                    b += (long)c[2] * c[3];
                    a += c[3];
                    cnt++;
                }
            }
            if (a > 0) { /* (the average, the alpha weighing the colours; then the nearest colour, alpha first) */
                long ar = r / a, ag = g / a, ab = b / a, aa = a / cnt;
                for (k = 0; k < 256; k++) {
                    const unsigned char *c = body + clut + pic_slot(k) * 4;
                    long da = aa - c[3], d = da * da * 16 + (ar - c[0]) * (ar - c[0]) + (ag - c[1]) * (ag - c[1]) + (ab - c[2]) * (ab - c[2]);
                    if (bestD < 0 || d < bestD) {
                        bestD = d;
                        best = k;
                    }
                }
            }
            tpl[cpix + pic_place(ox, oy, cw)] = (unsigned char)best;
        }
    }
    memcpy(tpl + cclut, body + clut, 1024); /* (the portrait's colours, in their stored order: the indices stay right) */
    free(body);
    /* the package's own chip kept the first time, then the new one in its place */
    if (!ours && !exists(keep) && exists(path) && rename(path, keep) != 0) {
        snprintf(why, whyLen, "could not keep the package's chip.bin as chip_original.bin");
        return 0;
    }
    if (!put_file(path, tpl, CHIP_BYTES)) {
        snprintf(why, whyLen, "could not write %s", path);
        return 0;
    }
    return 1;
}

/* "Automatic": the package's own chip back (or none, if it had none and the one there was made here). */
static void chip_unmake(int ours, const char *dir) {
    char path[600], keep[620];
    snprintf(keep, sizeof(keep), "%s/chip_original.bin", dir);
    snprintf(path, sizeof(path), "%s/chip.bin", dir);
    if (exists(keep)) {
        remove(path);
        rename(keep, path);
    } else if (ours) {
        remove(path);
    }
}

/* 0: a new fighter (value 1) or a replacement (0); 1: the disc character a new one stands on; 2: the one it
   replaces; 3..5: the chip's square (x, y, size; 0 0 0: the usual one), written with 5. */
void Port_ModSet(s32 i, s32 field, s32 value) {
    char num[48];
    Pkg *p;
    if (!valid(i)) {
        return;
    }
    p = &sLib[i];
    switch (field) {
    case 0:
        snprintf(num, sizeof(num), "%d", chara_of(p));
        if (value) {
            PortCharas_ManifestSet(p->folder, "replaces", NULL);
            PortCharas_ManifestSet(p->folder, "base", num);
        } else {
            PortCharas_ManifestSet(p->folder, "replaces", num);
        }
        break;
    case 1:
    case 2:
        if (value < 0 || value >= DISC) {
            return;
        }
        snprintf(num, sizeof(num), "%d", value);
        PortCharas_ManifestSet(p->folder, field == 1 ? "base" : "replaces", num);
        break;
    case 3:
    case 4:
    case 5:
        sChipEdit[field - 3] = value;
        if (field != 5) {
            return;
        }
        {   /* the icon made (or the package's own put back) with the square, then the square kept */
            char root[300], dir[420], was[48], why[200] = "";
            int ours = PortCharas_ManifestGet(p->folder, "chip_square", was, sizeof(was)) && was[0] != '\0';
            if (!PortCharas_Root(root, sizeof(root))) {
                return;
            }
            snprintf(dir, sizeof(dir), "%s/%s", root, p->folder);
            if (sChipEdit[2] <= 0) {
                chip_unmake(ours, dir);
                PortCharas_ManifestSet(p->folder, "chip_square", NULL);
            } else if (chip_make(i, sChipEdit[0], sChipEdit[1], sChipEdit[2], ours, dir, why, sizeof(why))) {
                snprintf(num, sizeof(num), "%d,%d,%d", sChipEdit[0], sChipEdit[1], sChipEdit[2]);
                PortCharas_ManifestSet(p->folder, "chip_square", num);
            } else {
                fprintf(stderr, "bt3: mod manager: the icon of %s was not made: %s\n", p->folder, why);
                return;
            }
        }
        break;
    default:
        return;
    }
    changed();
}

/* 0 the name (white), 1 the form's label (yellow), 2 author, 3 version, 4 the description. */
void Port_ModSetText(s32 i, s32 which, char *str) {
    static const char *const kKey[5] = {"name", "form", "author", "version", "about"};
    if (!valid(i) || which < 0 || which > 4 || str == NULL) {
        return;
    }
    PortCharas_ManifestSet(sLib[i].folder, kKey[which], str[0] != '\0' || which == 1 ? str : NULL);
    changed();
}

/* author (32 bytes), version (16), description (200) */
void Port_ModAbout(s32 i, char *out) {
    memset(out, 0, 248);
    if (!valid(i)) {
        return;
    }
    PortCharas_ManifestGet(sLib[i].folder, "author", out, 32);
    PortCharas_ManifestGet(sLib[i].folder, "version", out + 32, 16);
    PortCharas_ManifestGet(sLib[i].folder, "about", out + 48, 200);
}

/* ---- removed packages: moved to characters_removed beside `characters`, never deleted ------------------------- */

static int removed_dir(char *out, size_t size) {
    char root[300];
    char *slash;
    if (!PortCharas_Root(root, sizeof(root))) {
        return 0;
    }
    slash = strrchr(root, '/');
    if (slash != NULL) {
        snprintf(out, size, "%.*s/characters_removed", (int)(slash - root), root);
    } else {
        snprintf(out, size, "characters_removed");
    }
    return 1;
}

static int exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

s32 Port_ModRemove(s32 i) {
    char root[300], gone[320], from[400], to[420];
    int n;
    if (!valid(i) || sLib[i].form || !PortCharas_Root(root, sizeof(root)) || !removed_dir(gone, sizeof(gone))) {
        return 0; /* (a form is not taken out of its package alone) */
    }
    mkdir_one(gone);
    snprintf(from, sizeof(from), "%s/%s", root, sLib[i].folder);
    snprintf(to, sizeof(to), "%s/%s", gone, sLib[i].folder);
    for (n = 2; exists(to) && n < 100; n++) {
        snprintf(to, sizeof(to), "%s/%s (%d)", gone, sLib[i].folder, n);
    }
    if (exists(to) || rename(from, to) != 0) {
        return 0;
    }
    changed();
    return 1;
}

static char sGone[LIB_MAX][64];
static int sGoneCount;

static int gone_cmp(const void *a, const void *b) {
    return SDL_strcasecmp(a, b);
}

s32 Port_RemovedList(void) {
    char gone[320], path[420];
    DIR *d;
    struct dirent *e;
    struct stat st;
    sGoneCount = 0;
    if (!removed_dir(gone, sizeof(gone)) || (d = opendir(gone)) == NULL) {
        return 0;
    }
    while ((e = readdir(d)) != NULL && sGoneCount < LIB_MAX) {
        snprintf(path, sizeof(path), "%s/%s", gone, e->d_name);
        if (e->d_name[0] != '.' && strlen(e->d_name) < 64 && stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            snprintf(sGone[sGoneCount++], 64, "%s", e->d_name);
        }
    }
    closedir(d);
    qsort(sGone, (size_t)sGoneCount, 64, gone_cmp);
    return sGoneCount;
}

void Port_RemovedName(s32 i, char *out) {
    snprintf(out, 64, "%s", i >= 0 && i < sGoneCount ? sGone[i] : "");
}

/* Back into `characters` (switched off); its place in the list, -1 not done. */
s32 Port_RemovedRestore(s32 i) {
    char root[300], gone[320], from[420], to[400], name[64];
    int k;
    if (i < 0 || i >= sGoneCount || !PortCharas_Root(root, sizeof(root)) || !removed_dir(gone, sizeof(gone))) {
        return -1;
    }
    snprintf(name, sizeof(name), "%s", sGone[i]);
    snprintf(from, sizeof(from), "%s/%s", gone, name);
    snprintf(to, sizeof(to), "%s/%s", root, name);
    if (exists(to) || rename(from, to) != 0) {
        return -1;
    }
    order_put(name, -1, 0); /* (it may not be in the list: then it comes back on, as any new folder) */
    changed();
    Port_RemovedList();
    for (k = 0; k < sLibCount; k++) {
        if (strcmp(sLib[k].folder, name) == 0) {
            return k;
        }
    }
    return -1;
}

/* The package's folder (-1 `characters`) in the file manager of the PC. */
void Port_ModShowFolder(s32 i) {
    char root[300], path[400], full[1100], url[1200];
    if (!PortCharas_Root(root, sizeof(root))) {
        return;
    }
    mkdir_one(root);
    if (i >= 0 && valid(i)) {
        snprintf(path, sizeof(path), "%s/%s", root, sLib[i].folder);
    } else {
        snprintf(path, sizeof(path), "%s", root);
    }
#ifdef _WIN32
    if (_fullpath(full, path, sizeof(full)) == NULL) {
        return;
    }
    for (char *c = full; *c != '\0'; c++) {
        if (*c == '\\') {
            *c = '/';
        }
    }
    snprintf(url, sizeof(url), "file:///%s", full);
#else
    if (realpath(path, full) == NULL) {
        return;
    }
    snprintf(url, sizeof(url), "file://%s", full);
#endif
    SDL_OpenURL(url);
}

/* ---- names, small things ------------------------------------------------------------------------------------- */

void Port_ModCharaName(s32 chara, char *out, s32 size) {
    const char *name = NULL;
    if (size <= 0) {
        return;
    }
    if (chara >= 0 && chara < DISC) {
        name = PortTbo_FighterName(chara);
    } else if (chara >= 0xC6) {
        name = Port_CharaName(chara - 0xC6);
    }
    snprintf(out, (size_t)size, "%s", name != NULL && name[0] != '\0' ? name : "?");
}

s32 Port_ReadFile(char *path, void *buf, s32 max) {
    FILE *f;
    long n;
    if (path == NULL || max <= 0 || (f = fopen(path, "rb")) == NULL) {
        return 0;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > max || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        n = 0;
    }
    fclose(f);
    return (s32)n;
}

s32 Port_ModTestStage(void) {
    const char *v = getenv("BT3_TEST_STAGE");
    return v != NULL ? atoi(v) : 0;
}

void Port_TextMode(s32 on) {
    PortTbo_TextMode(on);
}

s32 Port_TextRead(void) {
    return PortTbo_TextRead();
}

s32 Port_DropTake(char *path) {
    return PortInput_DropTake(path, 700);
}

/* ---- the chip and the portrait (the package's own files; making them: Port_ModArt*) --------------------------- */

static long file_into(const char *path, void *out, long max) {
    FILE *f = fopen(path, "rb");
    long n = 0;
    if (f == NULL) {
        return 0;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > max || fread(out, 1, (size_t)n, f) != (size_t)n) {
        n = 0;
    }
    fclose(f);
    return n;
}

/* A package's chip.bin as an entry of the select's chip pack (5504 bytes; a shorter one, as some tools write them,
   filled out with zeros, as the package reader takes it); 0 without one, or if it is not a picture of the game. */
s32 Port_ModChip(s32 i, void *out, s32 max) {
    char root[300], path[420];
    long n, pix, clut;
    int w, h;
    if (!valid(i) || !(sLib[i].d.files >> PORT_DESC_CHIP & 1) || !PortCharas_Root(root, sizeof(root)) || max < CHIP_BYTES) {
        return 0;
    }
    snprintf(path, sizeof(path), "%s/%s/chip.bin", root, sLib[i].folder);
    memset(out, 0, CHIP_BYTES);
    n = file_into(path, out, CHIP_BYTES);
    return n >= PIC_HEAD && pic_parse(out, CHIP_BYTES, &w, &h, &pix, &clut) ? CHIP_BYTES : 0;
}

/* The package's portrait file (portrait.bin or *_img.cdbt, the game's packed form); 0 without one. */
s32 Port_ModPortrait(s32 i, void *out, s32 max) {
    char root[300], dir[420], path[600];
    DIR *d;
    struct dirent *e;
    long n = 0;
    if (!valid(i) || !(sLib[i].d.files >> PORT_DESC_PORTRAIT & 1) || !PortCharas_Root(root, sizeof(root))) {
        return 0;
    }
    snprintf(dir, sizeof(dir), "%s/%s", root, sLib[i].folder);
    if ((d = opendir(dir)) == NULL) {
        return 0;
    }
    while (n == 0 && (e = readdir(d)) != NULL) {
        size_t l = strlen(e->d_name);
        if (SDL_strcasecmp(e->d_name, "portrait.bin") == 0 || (l > 9 && SDL_strcasecmp(e->d_name + l - 9, "_img.cdbt") == 0)) {
            snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
            n = file_into(path, out, max);
        }
    }
    closedir(d);
    return (s32)n;
}

/* ---- the Mod Fighters grid: the added characters that are on ------------------------------------------------- */

s32 Port_SkinCount(void) {
    int list[LIB_MAX];
    ensure();
    return Port_NetModsHidden() ? 0 : skins(list, LIB_MAX);
}

s32 Port_SkinMod(s32 skin) {
    int list[LIB_MAX], n;
    ensure();
    n = skins(list, LIB_MAX);
    return skin >= 0 && skin < n ? list[skin] : -1;
}

/* The grid's order is the order the added characters take their ids in: two of them change places in it. */
void Port_RosterMove(s32 skin, s32 dir) {
    int list[LIB_MAX], n;
    ensure();
    n = skins(list, LIB_MAX);
    if (skin < 0 || skin >= n || skin + dir < 0 || skin + dir >= n || dir == 0) {
        return;
    }
    order_put(sLib[list[skin]].folder, sLib[list[skin + dir]].place, -1);
    changed();
}

/* An added character's id: the game's own number for it (0xC6 on), -1 if it is not one that is on. */
s32 Port_ModFighterId(s32 i) {
    int k;
    ensure();
    for (k = 0; valid(i) && sLib[i].on && sLib[i].d.replaces < 0 && k < Port_CharaPkgCount(); k++) {
        const PortCharaPkg *p = Port_CharaPkg(k);
        if (p->kind == PORT_PKG_ADDS && strcmp(p->folder, sLib[i].folder) == 0) {
            return p->id;
        }
    }
    return -1;
}

/* (the costume codes of the patch's skins are not used: an added character has an id of its own) */
s32 Port_CostumeOf(s32 code) {
    return code >= 8 ? (code - 8) & 3 : code;
}
s32 Port_SkinFile(s32 chara, s32 code, s32 kind, s32 variant) {
    (void)code;
    (void)variant;
    return chara * 10 + 0x590 + kind;
}
s32 Port_CharaOfFile(s32 id) {
    return (id - 0x590) / 10;
}

/* ---- transformations and fusions (character.txt's links) ----------------------------------------------------- */

static void trans_clear(PortTrans *t) {
    int k, m;
    memset(t, 0, sizeof(*t));
    for (k = 0; k < 4; k++) {
        t->target[k] = t->assist[k] = -1;
        t->type[k] = k == 3 ? 1 : 0;
        t->effect[k] = k == 3 ? 1 : 2;
        t->cost[k] = k == 3 ? 0 : 1;
    }
    t->next = -1;
    for (k = 0; k < 3; k++) {
        t->fuse[k] = t->fuseLead[k] = -1;
        t->fuseType[k] = 1;
        t->fuseCost[k] = 3;
        for (m = 0; m < 4; m++) {
            t->partner[k][m] = -1;
        }
    }
}

static const char *const kSlotKey[4] = {"left", "up", "right", "down"};

static const char *data_dir(void) {
    const char *d = getenv("BT3_DATA");
    return d != NULL ? d : "gamedata";
}

/* A whole file (at most `max` bytes); NULL if it cannot be read or is larger. */
static uint8_t *read_file(const char *path, long *len, long max) {
    FILE *f = fopen(path, "rb");
    uint8_t *d = NULL;
    long n;
    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n > 0 && n <= max && (d = malloc((size_t)n)) != NULL && fread(d, 1, (size_t)n, f) != (size_t)n) {
        free(d);
        d = NULL;
    }
    fclose(f);
    *len = n;
    return d;
}

/* A character id of a model's table as the manager's reference: -1 none, a disc id, or PORT_REF_MOD + the package
   of an added character. */
static int ref_of_id(int id) {
    int i;
    if (id == 0xFF || id < 0) {
        return -1;
    }
    if (id < DISC) {
        return id;
    }
    for (i = 0; i < sLibCount; i++) {
        if (Port_ModFighterId(i) == id) {
            return PORT_REF_MOD + i;
        }
    }
    return -1;
}

/* The forms as a model's table has them (entry 17 of the model file, its bytes 0x98..0xC5). */
static int trans_of_file(const char *path, PortTrans *t) {
    long len = 0;
    uint8_t *d = read_file(path, &len, 0x180000);
    uint32_t n, a, b;
    int k, j, ok = 0;

    if (d == NULL) {
        return 0;
    }
    memcpy(&n, d, 4);
    if (len >= 4 + 19 * 4 && n > 18 && n < 4096) {
        memcpy(&a, d + 4 + 17 * 4, 4);
        memcpy(&b, d + 4 + 18 * 4, 4);
        if (a < b && b <= (uint32_t)len && b - a >= 0xC6) {
            const uint8_t *q = d + a;
            for (k = 0; k < 4; k++) {
                t->target[k] = ref_of_id(q[0x98 + k]);
                t->cost[k] = q[0x9C + k];
                t->type[k] = q[0xA0 + k];
                t->effect[k] = q[0xA4 + k];
                t->assist[k] = q[0xA8 + k] == 0xFF ? -1 : q[0xA8 + k];
                t->fresh[k] = (q[0xAD] >> (4 + k)) & 1;
            }
            t->next = q[0xAC] == 0xFF ? -1 : q[0xAC];
            t->adLow = q[0xAD] & 0x0F;
            for (k = 0; k < 3; k++) {
                t->fuseCost[k] = q[0xAE + k];
                t->fuseType[k] = q[0xB1 + k];
                t->fuse[k] = ref_of_id(q[0xB4 + k]);
                t->fuseLead[k] = q[0xB7 + k] == 0xFF ? -1 : q[0xB7 + k];
                for (j = 0; j < 4; j++) {
                    t->partner[k][j] = ref_of_id(q[0xBA + k * 4 + j]);
                }
            }
            ok = 1;
        }
    }
    free(d);
    return ok;
}

/* The package used for disc character `chara` (the replacement that is on and first in the order), -1 none. */
static int repl_index(int chara) {
    int i, best = -1;
    for (i = 0; i < sLibCount; i++) {
        if (sLib[i].on && sLib[i].d.replaces == chara && (best < 0 || sLib[i].place < sLib[best].place)) {
            best = i;
        }
    }
    return best;
}

/* The package a fighter of the manager is: PORT_REF_MOD + index, or a disc character's replacement; -1 none. */
static int pkg_of(int who) {
    if (who >= PORT_REF_MOD) {
        return who - PORT_REF_MOD < sLibCount ? who - PORT_REF_MOD : -1;
    }
    return who >= 0 && who < DISC ? repl_index(who) : -1;
}

/* The model its fighter is set up from: the package's first costume, else the disc's (the first archive's file
   0x590 + id * 10, stored as pzs3us1/<id - 1>). */
static void model_path(int who, char *out, size_t size) {
    char root[300], dir[420];
    int i = pkg_of(who), chara = who < DISC ? who : (i >= 0 ? chara_of(&sLib[i]) : 0);
    DIR *d;
    struct dirent *e;

    out[0] = '\0';
    if (i >= 0 && (sLib[i].d.files & 1) && PortCharas_Root(root, sizeof(root))) {
        snprintf(dir, sizeof(dir), "%s/%s", root, sLib[i].folder);
        if ((d = opendir(dir)) != NULL) {
            while ((e = readdir(d)) != NULL) {
                size_t l = strlen(e->d_name);
                if (l > 7 && SDL_strcasecmp(e->d_name + l - 7, "_1p.pak") == 0 && (out[0] == '\0' || SDL_strcasecmp(e->d_name, strrchr(out, '/') + 1) < 0)) {
                    snprintf(out, size, "%s/%s", dir, e->d_name);
                }
            }
            closedir(d);
        }
    }
    if (out[0] == '\0') {
        snprintf(out, size, "%s/pzs3us1/%05d.bin", data_dir(), 0x590 + chara * 10 - 1);
    }
}

/* the entry of the library whose folder is `folder`, -1 none */
static int lib_index(const char *folder) {
    int i;
    for (i = 0; i < sLibCount; i++) {
        if (strcmp(sLib[i].folder, folder) == 0) {
            return i;
        }
    }
    return -1;
}

/* A link of package `from`'s character.txt as the manager's reference, read as the package reader reads it (find in
   plat_charas.c): "..", the character whose folder `from` is in; a name, a folder inside the same character's folder
   (from the character itself: one inside its own), else a folder of the top; or "Character/Form". -2 a folder that
   is not there (the model's own stands). (A link was compared with the whole folder only, so a form's name beside it
   was not found and the model's own table was shown: the disc character the mod had taken the place of.) */
static int ref_of_link(const char *from, const char *link) {
    char family[128], path[260];
    int i, x;
    if (strcmp(link, "none") == 0) {
        return -1;
    }
    if (strncmp(link, "disc:", 5) == 0) {
        x = atoi(link + 5);
        return x >= 0 && x < DISC ? x : -2;
    }
    snprintf(family, sizeof(family), "%s", from);
    if (strchr(family, '/') != NULL) {
        *strchr(family, '/') = '\0';
    }
    if (strcmp(link, "..") == 0) {
        return strchr(from, '/') != NULL && (i = lib_index(family)) >= 0 ? PORT_REF_MOD + i : -2;
    }
    if (strchr(link, '/') == NULL) {
        snprintf(path, sizeof(path), "%s/%s", family, link);
        if ((i = lib_index(path)) >= 0) {
            return PORT_REF_MOD + i;
        }
    }
    return (i = lib_index(link)) >= 0 ? PORT_REF_MOD + i : -2;
}

static void link_of_ref(int ref, char *out, size_t size) {
    if (ref >= PORT_REF_MOD && ref - PORT_REF_MOD < sLibCount) {
        snprintf(out, size, "%s", sLib[ref - PORT_REF_MOD].folder);
    } else if (ref >= 0 && ref < DISC) {
        snprintf(out, size, "disc:%d", ref);
    } else {
        snprintf(out, size, "none");
    }
}

static int get_num(const char *folder, const char *key, int *out) {
    char v[64];
    if (!PortCharas_ManifestGet(folder, key, v, sizeof(v)) || v[0] == '\0') {
        return 0;
    }
    if (strcmp(v, "none") == 0) {
        *out = -1;
    } else {
        *out = atoi(v);
    }
    return 1;
}

/* who: a disc character, or PORT_REF_MOD + a package. What its model has, with what its package says over it
   (set / fuseSet: the package says something of that part). */
void Port_TransGet(s32 who, PortTrans *out) {
    char path[700], key[32], v[300];
    const char *folder;
    int i, k, m, x;

    ensure();
    trans_clear(out);
    model_path(who, path, sizeof(path));
    trans_of_file(path, out);
    out->set = out->fuseSet = 0;
    if (Port_NetModsHidden() || (i = pkg_of(who)) < 0) {
        return;
    }
    folder = sLib[i].folder;
    for (k = 0; k < 4; k++) {
        if (PortCharas_ManifestGet(folder, kSlotKey[k], v, sizeof(v)) && (x = ref_of_link(folder, v)) != -2) {
            out->target[k] = x;
            out->set = 1;
        }
        snprintf(key, sizeof(key), "%s_cost", kSlotKey[k]);
        out->set |= get_num(folder, key, &out->cost[k]);
        snprintf(key, sizeof(key), "%s_type", kSlotKey[k]);
        out->set |= get_num(folder, key, &out->type[k]);
        snprintf(key, sizeof(key), "%s_effect", kSlotKey[k]);
        out->set |= get_num(folder, key, &out->effect[k]);
        snprintf(key, sizeof(key), "%s_assist", kSlotKey[k]);
        out->set |= get_num(folder, key, &out->assist[k]);
        snprintf(key, sizeof(key), "%s_fresh", kSlotKey[k]);
        out->set |= get_num(folder, key, &out->fresh[k]);
    }
    if (PortCharas_ManifestGet(folder, "default", v, sizeof(v))) {
        out->next = -1;
        for (k = 0; k < 4; k++) {
            if (strcmp(v, kSlotKey[k]) == 0) {
                out->next = k;
            }
        }
        out->set = 1;
    }
    for (k = 0; k < 3; k++) {
        snprintf(key, sizeof(key), "fusion%d", k + 1);
        if (PortCharas_ManifestGet(folder, key, v, sizeof(v)) && (x = ref_of_link(folder, v)) != -2) {
            out->fuse[k] = x;
            out->fuseSet = 1;
        }
        snprintf(key, sizeof(key), "partner%d", k + 1);
        if (PortCharas_ManifestGet(folder, key, v, sizeof(v))) {
            char *at = v;
            for (m = 0; m < 4; m++) {
                char one[80];
                size_t len = strcspn(at, ",");
                snprintf(one, sizeof(one), "%.*s", (int)(len < sizeof(one) - 1 ? len : sizeof(one) - 1), at);
                x = ref_of_link(folder, one);
                out->partner[k][m] = x == -2 ? -1 : x;
                at += len + (at[len] == ',');
            }
            out->fuseSet = 1;
        }
        snprintf(key, sizeof(key), "fusion%d_cost", k + 1);
        out->fuseSet |= get_num(folder, key, &out->fuseCost[k]);
        snprintf(key, sizeof(key), "fusion%d_type", k + 1);
        if (PortCharas_ManifestGet(folder, key, v, sizeof(v))) {
            out->fuseType[k] = strcmp(v, "potara") == 0 ? 2 : 1;
            out->fuseSet = 1;
        }
        snprintf(key, sizeof(key), "fusion%d_lead", k + 1);
        out->fuseSet |= get_num(folder, key, &out->fuseLead[k]);
        snprintf(key, sizeof(key), "fusion%d_scene", k + 1);
        out->fuseSet |= get_num(folder, key, &out->fuseAnim[k]);
    }
}

/* A disc character's package for its forms, made when its forms are first set here: "<its name> forms", replacing
   it with nothing but the manifest. Its index, -1 not made. */
static int forms_package(int chara) {
    char root[300], name[64], dir[420], path[440];
    FILE *f;
    int k, n;

    if (!PortCharas_Root(root, sizeof(root))) {
        return -1;
    }
    mkdir_one(root);
    Port_ModCharaName(chara, name, 48);
    for (k = 0; name[k] != '\0'; k++) {
        if (strchr("/\\:*?\"<>|", name[k]) != NULL || (unsigned char)name[k] < 0x20) {
            name[k] = '_';
        }
    }
    snprintf(name + strlen(name), sizeof(name) - strlen(name), " forms");
    snprintf(dir, sizeof(dir), "%s/%s", root, name);
    for (n = 2; exists(dir) && n < 100; n++) {
        snprintf(dir, sizeof(dir), "%s/%s %d", root, name, n);
    }
    if (exists(dir) || mkdir_one(dir) != 0) {
        return -1;
    }
    snprintf(path, sizeof(path), "%s/character.txt", dir);
    if ((f = fopen(path, "wb")) == NULL) {
        return -1;
    }
    fprintf(f, "format=1\n# the transformations and fusions of a disc character, set in the Mod Manager\nreplaces=%d\n", chara);
    fclose(f);
    lib_read();
    for (k = 0; k < sLibCount; k++) {
        if (strcmp(sLib[k].folder, strrchr(dir, '/') + 1) == 0) {
            return k;
        }
    }
    return -1;
}

static void put_num(const char *folder, const char *key, int on, int value) {
    char v[16];
    snprintf(v, sizeof(v), "%d", value);
    PortCharas_ManifestSet(folder, key, on ? (value < 0 ? "none" : v) : NULL);
}

/* What the FORMS screen made: written into the package's character.txt (set 0 / fuseSet 0: that part's keys taken
   out, the model's own again). A disc character without a package gets one (forms_package). */
void Port_TransSet(s32 who, PortTrans *in) {
    char folder[128], key[32], v[300], one[80];
    int i, k, m, on;

    ensure();
    if (Port_NetSession() || in == NULL) {
        return;
    }
    i = pkg_of(who);
    if (i < 0 && who >= 0 && who < DISC && (in->set || in->fuseSet)) {
        i = forms_package(who);
    }
    if (i < 0) {
        return;
    }
    snprintf(folder, sizeof(folder), "%s", sLib[i].folder);
    on = in->set != 0;
    for (k = 0; k < 4; k++) {
        link_of_ref(in->target[k], v, sizeof(v));
        PortCharas_ManifestSet(folder, kSlotKey[k], on ? v : NULL);
        snprintf(key, sizeof(key), "%s_cost", kSlotKey[k]);
        put_num(folder, key, on, in->cost[k] < 0 ? 0 : in->cost[k] > 9 ? 9 : in->cost[k]);
        snprintf(key, sizeof(key), "%s_type", kSlotKey[k]);
        put_num(folder, key, on, in->type[k] < 0 || in->type[k] > 4 ? 0 : in->type[k]);
        snprintf(key, sizeof(key), "%s_effect", kSlotKey[k]);
        put_num(folder, key, on, in->effect[k] < 0 || in->effect[k] > 11 ? 1 : in->effect[k]);
        snprintf(key, sizeof(key), "%s_assist", kSlotKey[k]);
        put_num(folder, key, on, in->assist[k] >= 0 && in->assist[k] < DISC ? in->assist[k] : -1);
        snprintf(key, sizeof(key), "%s_fresh", kSlotKey[k]);
        put_num(folder, key, on, in->fresh[k] != 0);
    }
    PortCharas_ManifestSet(folder, "default", on ? (in->next >= 0 && in->next < 4 ? kSlotKey[in->next] : "none") : NULL);
    on = in->fuseSet != 0;
    for (k = 0; k < 3; k++) {
        snprintf(key, sizeof(key), "fusion%d", k + 1);
        link_of_ref(in->fuse[k], v, sizeof(v));
        PortCharas_ManifestSet(folder, key, on ? v : NULL);
        v[0] = '\0';
        for (m = 0; m < 4; m++) {
            link_of_ref(in->partner[k][m], one, sizeof(one));
            snprintf(v + strlen(v), sizeof(v) - strlen(v), "%s%s", m > 0 ? "," : "", one);
        }
        snprintf(key, sizeof(key), "partner%d", k + 1);
        PortCharas_ManifestSet(folder, key, on ? v : NULL);
        snprintf(key, sizeof(key), "fusion%d_cost", k + 1);
        put_num(folder, key, on, in->fuseCost[k] < 0 ? 0 : in->fuseCost[k] > 9 ? 9 : in->fuseCost[k]);
        snprintf(key, sizeof(key), "fusion%d_type", k + 1);
        PortCharas_ManifestSet(folder, key, on ? (in->fuseType[k] == 2 ? "potara" : "dance") : NULL);
        snprintf(key, sizeof(key), "fusion%d_lead", k + 1);
        put_num(folder, key, on, in->fuseLead[k] >= 0 && in->fuseLead[k] < DISC ? in->fuseLead[k] : -1);
        snprintf(key, sizeof(key), "fusion%d_scene", k + 1);
        put_num(folder, key, on, in->fuseAnim[k] < 0 || in->fuseAnim[k] > 5 ? 0 : in->fuseAnim[k]);
    }
    changed();
}

/* (the patch's own way, writing into the loaded model, is Port_CharaParamApply's in the package reader) */
void Port_TransApplyFile(s32 modelFile, void *param) {
    (void)modelFile;
    (void)param;
}

/* ---- the moves of a package (its first costume's move list) -------------------------------------------------- */

/* Its blasts, for the Library's panel: the first page of the skill list its model carries (section 45 of the model
   file, the text the fight's pause menu shows: "$0" the special attacks, each "*" line one, after its three-digit
   cost), from the package's first costume or the disc's. Letters with accents as the plain ones (the panel's font
   has no others); anything else '?', which the panel takes for a name it cannot show. Up to `max` names of 40 bytes. */
s32 Port_ModMoves(s32 i, void *out, s32 max) {
    /* (U+00C0..U+00FF as plain letters) */
    static const char kPlain[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
    char (*names)[40] = out, path[600];
    unsigned char *d;
    long n, at, len, p;
    int count = 0, page = -1;

    if (!valid(i) || max <= 0) {
        return 0;
    }
    model_path(PORT_REF_MOD + i, path, sizeof(path));
    if ((d = file_all(path, 8L << 20, &n)) == NULL) {
        return 0;
    }
    if (pack_section(d, n, 45, &at, &len) && len >= 4 && d[at] == 0xFF && d[at + 1] == 0xFE) {
        for (p = at + 2; p + 1 < at + len && count < max;) {
            unsigned line[80];
            int k = 0;
            while (p + 1 < at + len) { /* (one line, UTF-16) */
                unsigned c = d[p] | (unsigned)d[p + 1] << 8;
                p += 2;
                if (c == '\n' || c == 0) {
                    break;
                }
                if (k < 80) {
                    line[k++] = c;
                }
            }
            if (k >= 3 && line[0] == '!') { /* ("!F" and the like: what shows it, then the line itself) */
                const unsigned *t = line + 2;
                int m = k - 2, c, o = 0;
                if (t[0] == '@') {
                    break;
                }
                if (t[0] == '$' && m >= 2) {
                    if (page == 0) {
                        break; /* (the special attacks' page is over) */
                    }
                    page = (int)t[1] - '0';
                } else if (t[0] == '*' && page == 0 && m > 4) {
                    for (c = 4; c < m && o < 39; c++) {
                        unsigned u = t[c];
                        names[count][o++] = u >= 32 && u < 127 ? (char)u : u >= 0xC0 && u <= 0xFF ? kPlain[u - 0xC0] : '?';
                    }
                    while (o > 0 && names[count][o - 1] == ' ') {
                        o--;
                    }
                    names[count][o] = '\0';
                    count += o > 0;
                }
            }
        }
    }
    free(d);
    return count;
}

/* ---- the portrait made in the manager (Port_ModArt*) -----------------------------------------------------------
 * The picture: a PNG or BMP picked in the browser (kept in the package as portrait_source.png / .bmp when the
 * portrait is saved, so it can be framed again), else the package's own portrait. A picture is put whole into the
 * frame as the select screen shows it: 512 x 512 on the screen, stored 256 x 512 (the portraits are kept squeezed
 * and shown twice as wide). The screen then moves it (x, y: the frame's pixels) and sizes it (width and height in
 * percent, about the frame's middle); saving draws that into a 256 x 512 picture, brings it down to 256 colours
 * (fewer, if packed it would not fit the game's 92,160 bytes), packs it and writes portrait.bin. The package's own
 * portrait is moved aside as portrait_original.bin (its name kept in character.txt as portrait_was) and put back by
 * the reset. The file written into is the package's own portrait, else the base character's of the disc. */
enum { ART_W = 256, ART_H = 512, ART_PACKED_MAX = 0x16800 };
static int sArtMod = -1;                /* the package a picture was picked for this time */
static unsigned char *sArtPick;         /* that picture in the frame (ART_W x ART_H, RGBA) */
static char sArtPickPath[700];

/* A picture file as RGBA put whole into the frame (centred, its shape kept on the screen); 0 if it is not read. */
static int art_fit(const char *path, unsigned char *out) {
    SDL_Surface *s = SDL_LoadPNG(path), *r;
    int x, y, W, H;
    double scale, offx, offy;
    if (s == NULL) {
        s = SDL_LoadBMP(path);
    }
    if (s == NULL) {
        return 0;
    }
    r = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(s);
    if (r == NULL) {
        return 0;
    }
    W = r->w;
    H = r->h;
    if (W < 1 || H < 1 || W > 8192 || H > 8192) {
        SDL_DestroySurface(r);
        return 0;
    }
    scale = (double)ART_H / (W > H ? W : H); /* (screen pixels a picture pixel: it fits 512 x 512) */
    offx = (ART_H - W * scale) / 2.0;
    offy = (ART_H - H * scale) / 2.0;
    for (y = 0; y < ART_H; y++) {
        for (x = 0; x < ART_W; x++) {
            /* the stored pixel covers two screen pixels across: the picture's pixels under them, averaged */
            double sx0 = (x * 2.0 - offx) / scale, sx1 = (x * 2.0 + 2.0 - offx) / scale, sy0 = (y - offy) / scale, sy1 = (y + 1.0 - offy) / scale;
            int ix0 = (int)floor(sx0), ix1 = (int)ceil(sx1), iy0 = (int)floor(sy0), iy1 = (int)ceil(sy1), xx, yy;
            long rr = 0, gg = 0, bb = 0, aa = 0, n = 0;
            unsigned char *o = out + ((long)y * ART_W + x) * 4;
            ix0 = ix0 < 0 ? 0 : ix0;
            iy0 = iy0 < 0 ? 0 : iy0;
            ix1 = ix1 > W ? W : ix1;
            iy1 = iy1 > H ? H : iy1;
            for (yy = iy0; yy < iy1; yy++) {
                const unsigned char *row = (const unsigned char *)r->pixels + (long)yy * r->pitch;
                for (xx = ix0; xx < ix1; xx++) {
                    const unsigned char *p = row + xx * 4;
                    rr += (long)p[0] * p[3];
                    gg += (long)p[1] * p[3];
                    bb += (long)p[2] * p[3];
                    aa += p[3];
                    n++;
                }
            }
            if (aa > 0 && n > 0) {
                o[0] = (unsigned char)(rr / aa);
                o[1] = (unsigned char)(gg / aa);
                o[2] = (unsigned char)(bb / aa);
                o[3] = (unsigned char)(aa / n);
            } else {
                o[0] = o[1] = o[2] = o[3] = 0;
            }
        }
    }
    SDL_DestroySurface(r);
    return 1;
}

/* the package's folder; its portrait as it came (kept aside, else the one there); the file it was kept from */
static int art_dir(int i, char *dir, size_t n) {
    char root[300];
    if (!valid(i) || !PortCharas_Root(root, sizeof(root))) {
        return 0;
    }
    snprintf(dir, n, "%s/%s", root, sLib[i].folder);
    return 1;
}

static unsigned char *art_own_body(int i, long *n) {
    char dir[420], path[600];
    unsigned char *raw, *body;
    long len;
    if (!art_dir(i, dir, sizeof(dir))) {
        return NULL;
    }
    snprintf(path, sizeof(path), "%s/portrait_original.bin", dir);
    if ((raw = file_all(path, PORTRAIT_MAX, &len)) == NULL) {
        return portrait_body(i, n);
    }
    if (rd32(raw) != 1 && (body = bpe_unpack(raw, len, PORTRAIT_MAX * 4, n)) != NULL) {
        free(raw);
        return body;
    }
    *n = len;
    return raw;
}

/* the file to write into: the package's own portrait, else the base character's of the disc (unpacked) */
static unsigned char *art_template(int i, long *n) {
    const char *data = getenv("BT3_DATA");
    char path[600];
    unsigned char *raw, *body;
    long len, pix, clut;
    int w, h;
    if ((body = art_own_body(i, n)) != NULL && pic_parse(body, *n, &w, &h, &pix, &clut) && w == ART_W && h == ART_H) {
        return body;
    }
    free(body);
    snprintf(path, sizeof(path), "%s/pzs3us1/%05d.bin", data != NULL && data[0] != '\0' ? data : "gamedata", 0x2F9 + chara_of(&sLib[i]) - 1);
    if ((raw = file_all(path, PORTRAIT_MAX, &len)) == NULL) {
        return NULL;
    }
    body = rd32(raw) != 1 ? bpe_unpack(raw, len, PORTRAIT_MAX * 4, n) : NULL;
    free(raw);
    if (body != NULL && pic_parse(body, *n, &w, &h, &pix, &clut) && w == ART_W && h == ART_H) {
        return body;
    }
    free(body);
    return NULL;
}

/* a portrait file's picture as RGBA (alpha 0..255) */
static int art_rgba_of(const unsigned char *body, long n, unsigned char *out) {
    long pix, clut;
    int w, h, x, y;
    if (!pic_parse(body, n, &w, &h, &pix, &clut) || w != ART_W || h != ART_H) {
        return 0;
    }
    for (y = 0; y < ART_H; y++) {
        for (x = 0; x < ART_W; x++) {
            const unsigned char *c = body + clut + pic_slot(body[pix + pic_place(x, y, ART_W)]) * 4;
            unsigned char *o = out + ((long)y * ART_W + x) * 4;
            o[0] = c[0];
            o[1] = c[1];
            o[2] = c[2];
            o[3] = (unsigned char)(c[3] * 2 - (c[3] >> 7) > 255 ? 255 : c[3] * 2 - (c[3] >> 7));
        }
    }
    return 1;
}

/* The picture the portrait is made from, in the frame (RGBA): the one picked now, the one kept in the package, or
   the package's own portrait. */
static int art_base(int i, unsigned char *rgba) {
    char dir[420], path[600];
    unsigned char *body;
    long n;
    int ok;
    if (i == sArtMod && sArtPick != NULL) {
        memcpy(rgba, sArtPick, (size_t)ART_W * ART_H * 4);
        return 1;
    }
    if (art_dir(i, dir, sizeof(dir))) {
        snprintf(path, sizeof(path), "%s/portrait_source.png", dir);
        if (exists(path) && art_fit(path, rgba)) {
            return 1;
        }
        snprintf(path, sizeof(path), "%s/portrait_source.bmp", dir);
        if (exists(path) && art_fit(path, rgba)) {
            return 1;
        }
    }
    if ((body = art_own_body(i, &n)) == NULL) {
        return 0;
    }
    ok = art_rgba_of(body, n, rgba);
    free(body);
    return ok;
}

/* Median cut: `colours` colours (index 0 kept for the clear) for an RGBA picture; the indices into idx. */
typedef struct {
    int from, to;
} ArtBox;
static const unsigned char *sCutPx;
static int sCutChan;
static int art_cut_cmp(const void *a, const void *b) {
    return (int)sCutPx[*(const int *)a * 4 + sCutChan] - (int)sCutPx[*(const int *)b * 4 + sCutChan];
}
static int art_quantize(const unsigned char *rgba, int count, int colours, unsigned char pal[256][4], unsigned char *idx) {
    int *order = malloc(sizeof(int) * (size_t)count), n = 0, boxes = 1, k, b;
    ArtBox box[256];
    if (order == NULL) {
        return 0;
    }
    memset(pal, 0, 256 * 4);
    for (k = 0; k < count; k++) {
        if (rgba[k * 4 + 3] < 2) {
            idx[k] = 0;
        } else {
            order[n++] = k;
        }
    }
    box[0].from = 0;
    box[0].to = n;
    sCutPx = rgba;
    while (boxes < colours - 1) { /* (the box with the widest spread, weighed by how many it holds, cut at its median) */
        int best = -1, bestChan = 0;
        double bestW = 0.0;
        for (b = 0; b < boxes; b++) {
            int lo[4] = {255, 255, 255, 255}, hi[4] = {0, 0, 0, 0}, c, j;
            if (box[b].to - box[b].from < 2) {
                continue;
            }
            for (j = box[b].from; j < box[b].to; j++) {
                for (c = 0; c < 4; c++) {
                    int v = rgba[order[j] * 4 + c];
                    lo[c] = v < lo[c] ? v : lo[c];
                    hi[c] = v > hi[c] ? v : hi[c];
                }
            }
            for (c = 0; c < 4; c++) {
                double w = (double)(hi[c] - lo[c]) * sqrt((double)(box[b].to - box[b].from));
                if (w > bestW) {
                    bestW = w;
                    best = b;
                    bestChan = c;
                }
            }
        }
        if (best < 0) {
            break;
        }
        sCutChan = bestChan;
        qsort(order + box[best].from, (size_t)(box[best].to - box[best].from), sizeof(int), art_cut_cmp);
        box[boxes].from = box[best].from + (box[best].to - box[best].from) / 2;
        box[boxes].to = box[best].to;
        box[best].to = box[boxes].from;
        boxes++;
    }
    for (b = 0; b < boxes && n > 0; b++) {
        long s[4] = {0, 0, 0, 0}, m = box[b].to - box[b].from;
        int j, c;
        if (m <= 0) {
            continue;
        }
        for (j = box[b].from; j < box[b].to; j++) {
            for (c = 0; c < 4; c++) {
                s[c] += rgba[order[j] * 4 + c];
            }
            idx[order[j]] = (unsigned char)(b + 1);
        }
        for (c = 0; c < 4; c++) {
            pal[b + 1][c] = (unsigned char)(s[c] / m);
        }
    }
    free(order);
    return 1;
}

/* The game's byte pair coder (bpe_pack of port/tools/import_chara.py): blocks of up to 4096 bytes of at most 200
   kinds, pairs replaced by unused bytes while a pair is there four times or more. */
static unsigned char *bpe_pack(const unsigned char *d, long n, long *outLen) {
    long cap = n + n / 2 + 4096, o = 8, at = 0;
    unsigned char *out = malloc((size_t)cap), *buf = malloc(4096);
    unsigned *pairs = calloc(65536, sizeof(unsigned));
    if (out == NULL || buf == NULL || pairs == NULL) {
        free(out);
        free(buf);
        free(pairs);
        return NULL;
    }
    while (at < n) {
        unsigned char used[256], left[256], right[256], free_[256];
        long end = at, len;
        int kinds = 0, nfree = 0, c;
        memset(used, 0, sizeof(used));
        while (end < n && end - at < 4096 && (kinds < 200 || used[d[end]])) {
            if (!used[d[end]]) {
                used[d[end]] = 1;
                kinds++;
            }
            end++;
        }
        len = end - at;
        memcpy(buf, d + at, (size_t)len);
        at = end;
        for (c = 0; c < 256; c++) {
            left[c] = (unsigned char)c;
            right[c] = 0;
        }
        for (c = 255; c >= 0; c--) { /* (from the highest down: taken from the end, the lowest first) */
            if (!used[c]) {
                free_[nfree++] = (unsigned char)c;
            }
        }
        while (nfree > 0 && len > 2) {
            unsigned best = 0, bestK = 0;
            long j, w;
            unsigned char a, b2, code;
            for (j = 0; j + 1 < len; j++) {
                pairs[buf[j] << 8 | buf[j + 1]]++;
            }
            for (j = 0; j + 1 < len; j++) { /* (the first pair of the greatest count, in order of first appearance) */
                unsigned k = (unsigned)(buf[j] << 8 | buf[j + 1]);
                if (pairs[k] > best) {
                    best = pairs[k];
                    bestK = k;
                }
            }
            for (j = 0; j + 1 < len; j++) {
                pairs[buf[j] << 8 | buf[j + 1]] = 0;
            }
            if (best < 4) {
                break;
            }
            code = free_[--nfree];
            a = (unsigned char)(bestK >> 8);
            b2 = (unsigned char)(bestK & 255);
            left[code] = a;
            right[code] = b2;
            for (j = 0, w = 0; j < len;) {
                if (j + 1 < len && buf[j] == a && buf[j + 1] == b2) {
                    buf[w++] = code;
                    j += 2;
                } else {
                    buf[w++] = buf[j++];
                }
            }
            len = w;
        }
        if (o + 600 + len > cap) {
            free(out);
            free(buf);
            free(pairs);
            return NULL;
        }
        /* the pair table: runs of bytes that stand for themselves skipped (a count above 127, then one entry), the
           others listed (a count, then that many entries and one more) */
        c = 0;
        while (c < 256) {
            int cnt = 0, k;
            if (left[c] == c) {
                cnt = 1;
                c++;
                while (cnt < 127 && c < 256 && left[c] == c) {
                    cnt++;
                    c++;
                }
                out[o++] = (unsigned char)(cnt + 127);
                cnt = 0;
                if (c == 256) {
                    break;
                }
            } else {
                c++;
                while (cnt < 127 && c < 256 && left[c] != c) {
                    cnt++;
                    c++;
                }
                out[o++] = (unsigned char)cnt;
                c -= cnt + 1;
            }
            for (k = 0; k <= cnt; k++) {
                out[o++] = left[c];
                if (left[c] != c) {
                    out[o++] = right[c];
                }
                c++;
            }
        }
        out[o++] = (unsigned char)(len >> 8);
        out[o++] = (unsigned char)(len & 255);
        memcpy(out + o, buf, (size_t)len);
        o += len;
    }
    out[0] = (unsigned char)(n & 255);
    out[1] = (unsigned char)(n >> 8 & 255);
    out[2] = (unsigned char)(n >> 16 & 255);
    out[3] = (unsigned char)(n >> 24 & 255);
    out[4] = (unsigned char)((o - 8) & 255);
    out[5] = (unsigned char)((o - 8) >> 8 & 255);
    out[6] = (unsigned char)((o - 8) >> 16 & 255);
    out[7] = (unsigned char)((o - 8) >> 24 & 255);
    free(buf);
    free(pairs);
    *outLen = o;
    return out;
}

/* An RGBA picture (ART_W x ART_H) written into the template's pixels and colours, in `colours` colours. */
static void art_into(unsigned char *body, long pix, long clut, const unsigned char *rgba, int colours, unsigned char *idx) {
    unsigned char pal[256][4];
    int x, y, k;
    art_quantize(rgba, ART_W * ART_H, colours, pal, idx);
    for (y = 0; y < ART_H; y++) {
        for (x = 0; x < ART_W; x++) {
            body[pix + pic_place(x, y, ART_W)] = idx[y * ART_W + x];
        }
    }
    for (k = 0; k < 256; k++) {
        unsigned char *c = body + clut + pic_slot(k) * 4;
        c[0] = pal[k][0];
        c[1] = pal[k][1];
        c[2] = pal[k][2];
        c[3] = (unsigned char)((pal[k][3] + 1) >> 1);
    }
}

s32 Port_ModArtSource(s32 i, char *path) {
    unsigned char *rgba;
    if (!valid(i) || path == NULL || (rgba = malloc((size_t)ART_W * ART_H * 4)) == NULL) {
        return 0;
    }
    if (!art_fit(path, rgba)) {
        free(rgba);
        return 0;
    }
    free(sArtPick);
    sArtPick = rgba;
    sArtMod = i;
    snprintf(sArtPickPath, sizeof(sArtPickPath), "%s", path);
    return 1;
}

/* The picture whole in the frame, as an unpacked portrait file (the screen shows it and moves it). */
s32 Port_ModArtBase(s32 i, void *out, s32 max) {
    unsigned char *rgba = malloc((size_t)ART_W * ART_H * 4), *idx = malloc((size_t)ART_W * ART_H), *body = NULL;
    long n = 0, pix, clut;
    int w, h;
    s32 got = 0;
    if (rgba != NULL && idx != NULL && art_base(i, rgba) && (body = art_template(i, &n)) != NULL && n <= max &&
        pic_parse(body, n, &w, &h, &pix, &clut)) {
        art_into(body, pix, clut, rgba, 256, idx);
        memcpy(out, body, (size_t)n);
        got = (s32)n;
    }
    free(rgba);
    free(idx);
    free(body);
    return got;
}

/* Saves the portrait: the picture moved by (x, y) and sized zx, zy percent about the frame's middle. */
s32 Port_ModArtSet2(s32 i, s32 x, s32 y, s32 zx, s32 zy) {
    unsigned char *base = malloc((size_t)ART_W * ART_H * 4), *pic = malloc((size_t)ART_W * ART_H * 4), *idx = malloc((size_t)ART_W * ART_H);
    unsigned char *body = NULL, *packed = NULL, *check = NULL;
    char dir[420], path[600], keep[620], frame[64], was[200] = "";
    long n = 0, pix, clut, plen = 0, clen = 0;
    int w, h, u, v, colours, ok = 0, ours;
    double fx = zx / 100.0, fy = zy / 100.0, x0 = 128.0 + x - 128.0 * fx, y0 = 256.0 + y - 256.0 * fy;

    if (base == NULL || pic == NULL || idx == NULL || zx < 1 || zy < 1 || !art_dir(i, dir, sizeof(dir)) || !art_base(i, base) ||
        (body = art_template(i, &n)) == NULL || !pic_parse(body, n, &w, &h, &pix, &clut)) {
        goto done;
    }
    /* the picture where the screen showed it: each pixel of the frame the average of the picture's under it */
    for (v = 0; v < ART_H; v++) {
        for (u = 0; u < ART_W; u++) {
            double a0 = (u - x0) / fx, a1 = (u + 1 - x0) / fx, b0 = (v - y0) / fy, b1 = (v + 1 - y0) / fy;
            int ix0 = (int)floor(a0), ix1 = (int)ceil(a1), iy0 = (int)floor(b0), iy1 = (int)ceil(b1), xx, yy;
            long rr = 0, gg = 0, bb = 0, aa = 0, cnt = 0;
            unsigned char *o = pic + ((long)v * ART_W + u) * 4;
            ix0 = ix0 < 0 ? 0 : ix0;
            iy0 = iy0 < 0 ? 0 : iy0;
            ix1 = ix1 > ART_W ? ART_W : ix1;
            iy1 = iy1 > ART_H ? ART_H : iy1;
            for (yy = iy0; yy < iy1; yy++) {
                for (xx = ix0; xx < ix1; xx++) {
                    const unsigned char *p = base + ((long)yy * ART_W + xx) * 4;
                    rr += (long)p[0] * p[3];
                    gg += (long)p[1] * p[3];
                    bb += (long)p[2] * p[3];
                    aa += p[3];
                    cnt++;
                }
            }
            { /* (the part of the frame pixel the picture covers: its edges come out soft, not stepped) */
                long all = (long)((ceil(a1) - floor(a0)) * (ceil(b1) - floor(b0)));
                cnt = all > cnt ? all : cnt;
            }
            if (aa > 0 && cnt > 0) {
                o[0] = (unsigned char)(rr / aa);
                o[1] = (unsigned char)(gg / aa);
                o[2] = (unsigned char)(bb / aa);
                o[3] = (unsigned char)(aa / cnt);
            } else {
                o[0] = o[1] = o[2] = o[3] = 0;
            }
        }
    }
    /* in as many colours as still pack into the game's room for a portrait */
    for (colours = 256; colours >= 16 && !ok; colours /= 2) {
        free(packed);
        free(check);
        check = NULL;
        art_into(body, pix, clut, pic, colours, idx);
        packed = bpe_pack(body, n, &plen);
        ok = packed != NULL && plen <= ART_PACKED_MAX && (check = bpe_unpack(packed, plen, n, &clen)) != NULL && clen == n &&
             memcmp(check, body, (size_t)n) == 0;
    }
    if (!ok) {
        fprintf(stderr, "bt3: mod manager: the portrait of %s was not made (%s)\n", sLib[i].folder,
                packed == NULL ? "not packed" : plen > ART_PACKED_MAX ? "too large packed, even in 16 colours" : "the packed file did not unpack to it");
        goto done;
    }
    ok = 0;
    /* the package's own portrait kept aside the first time (its name remembered) */
    ours = PortCharas_ManifestGet(sLib[i].folder, "portrait_frame", frame, sizeof(frame)) && frame[0] != '\0';
    snprintf(keep, sizeof(keep), "%s/portrait_original.bin", dir);
    if (!ours && !exists(keep)) {
        DIR *d = opendir(dir);
        struct dirent *e;
        while (d != NULL && was[0] == '\0' && (e = readdir(d)) != NULL) {
            size_t l = strlen(e->d_name);
            if (SDL_strcasecmp(e->d_name, "portrait.bin") == 0 || (l > 9 && SDL_strcasecmp(e->d_name + l - 9, "_img.cdbt") == 0)) {
                snprintf(was, sizeof(was), "%s", e->d_name);
            }
        }
        if (d != NULL) {
            closedir(d);
        }
        if (was[0] != '\0') {
            snprintf(path, sizeof(path), "%s/%s", dir, was);
            if (rename(path, keep) != 0) {
                goto done;
            }
            PortCharas_ManifestSet(sLib[i].folder, "portrait_was", was);
        }
    }
    snprintf(path, sizeof(path), "%s/portrait.bin", dir);
    if (!put_file(path, packed, plen)) {
        goto done;
    }
    if (i == sArtMod && sArtPick != NULL) { /* the picture kept with it, to frame it again later */
        const char *dot = strrchr(sArtPickPath, '.');
        int bmp = dot != NULL && SDL_strcasecmp(dot, ".bmp") == 0;
        unsigned char *src;
        long sl;
        snprintf(path, sizeof(path), "%s/portrait_source.png", dir);
        remove(path);
        snprintf(path, sizeof(path), "%s/portrait_source.bmp", dir);
        remove(path);
        snprintf(path, sizeof(path), "%s/portrait_source.%s", dir, bmp ? "bmp" : "png");
        if ((src = file_all(sArtPickPath, 64L << 20, &sl)) != NULL) {
            put_file(path, src, sl);
            free(src);
        }
    }
    snprintf(frame, sizeof(frame), "%d,%d,%d,%d", x, y, zx, zy);
    PortCharas_ManifestSet(sLib[i].folder, "portrait_frame", frame);
    changed();
    ok = 1;
done:
    free(base);
    free(pic);
    free(idx);
    free(body);
    free(packed);
    free(check);
    return ok;
}

s32 Port_ModArtSet(s32 i, s32 x, s32 y, s32 zoom) {
    return Port_ModArtSet2(i, x, y, zoom, zoom);
}

/* The package's own portrait back, and the picture forgotten. */
void Port_ModArtReset(s32 i) {
    char dir[420], path[600], keep[620], was[200];
    if (i == sArtMod) {
        free(sArtPick);
        sArtPick = NULL;
        sArtMod = -1;
    }
    if (!art_dir(i, dir, sizeof(dir))) {
        return;
    }
    snprintf(keep, sizeof(keep), "%s/portrait_original.bin", dir);
    snprintf(path, sizeof(path), "%s/portrait.bin", dir);
    if (exists(keep)) {
        if (!PortCharas_ManifestGet(sLib[i].folder, "portrait_was", was, sizeof(was)) || was[0] == '\0' || strchr(was, '/') != NULL ||
            strchr(was, '\\') != NULL || strcmp(was, "..") == 0) {
            snprintf(was, sizeof(was), "portrait.bin");
        }
        remove(path);
        snprintf(path, sizeof(path), "%s/%s", dir, was);
        remove(path);
        rename(keep, path);
    } else if (PortCharas_ManifestGet(sLib[i].folder, "portrait_frame", was, sizeof(was)) && was[0] != '\0') {
        remove(path); /* (made here, and it had none of its own) */
    }
    snprintf(path, sizeof(path), "%s/portrait_source.png", dir);
    remove(path);
    snprintf(path, sizeof(path), "%s/portrait_source.bmp", dir);
    remove(path);
    PortCharas_ManifestSet(sLib[i].folder, "portrait_frame", NULL);
    PortCharas_ManifestSet(sLib[i].folder, "portrait_was", NULL);
    changed();
}

/* ---- the browser: the PC's folders ---------------------------------------------------------------------------- */

typedef struct {
    char name[96];
    int isDir;
} DirEnt;

static DirEnt sDir[512], sShown[512];
static int sDirCount, sShownCount, sDirKind;

void Port_DirKind(s32 kind) {
    sDirKind = kind;
}

static int listed_file(const char *name) {
    static const char *const kMods[] = {".pak", ".unk", ".cdbt", ".dbt", ".bin", ".txt", ".zip", NULL};
    static const char *const kPics[] = {".png", ".bmp", ".jpg", ".jpeg", NULL};
    const char *const *ends = sDirKind == 1 ? kPics : kMods;
    size_t l = strlen(name);
    int k;
    for (k = 0; ends[k] != NULL; k++) {
        size_t e = strlen(ends[k]);
        if (l > e && SDL_strcasecmp(name + l - e, ends[k]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int dir_cmp(const void *a, const void *b) {
    const DirEnt *x = a, *y = b;
    if (x->isDir != y->isDir) {
        return y->isDir - x->isDir;
    }
    return SDL_strcasecmp(x->name, y->name);
}

/* Lists a folder: its folders first, then the files of the kind asked for. "" lists the drives (Windows) or "/". */
s32 Port_DirOpen(char *path) {
    char full[1100];
    DIR *d;
    struct dirent *e;
    struct stat st;

    sDirCount = 0;
#ifdef _WIN32
    if (path == NULL || path[0] == '\0') {
        DWORD drives = GetLogicalDrives();
        int k;
        for (k = 0; k < 26 && sDirCount < 512; k++) {
            if (drives & (1u << k)) {
                snprintf(sDir[sDirCount].name, sizeof(sDir[0].name), "%c:", 'A' + k);
                sDir[sDirCount++].isDir = 1;
            }
        }
        memcpy(sShown, sDir, sizeof(DirEnt) * (size_t)sDirCount);
        return sShownCount = sDirCount;
    }
#endif
    if ((d = opendir(path != NULL && path[0] != '\0' ? path : "/")) == NULL) {
        sShownCount = 0;
        return 0;
    }
    while ((e = readdir(d)) != NULL && sDirCount < 512) {
        if (e->d_name[0] == '.' || strlen(e->d_name) >= sizeof(sDir[0].name)) {
            continue;
        }
        snprintf(full, sizeof(full), "%s/%s", path != NULL && path[0] != '\0' ? path : "", e->d_name);
        if (stat(full, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode) || (S_ISREG(st.st_mode) && listed_file(e->d_name))) {
            snprintf(sDir[sDirCount].name, sizeof(sDir[0].name), "%s", e->d_name);
            sDir[sDirCount++].isDir = S_ISDIR(st.st_mode);
        }
    }
    closedir(d);
    qsort(sDir, (size_t)sDirCount, sizeof(DirEnt), dir_cmp);
    memcpy(sShown, sDir, sizeof(DirEnt) * (size_t)sDirCount);
    return sShownCount = sDirCount;
}

/* Keeps the entries with `text` in their name (any case); the count. */
s32 Port_DirFilter(char *text) {
    int i;
    sShownCount = 0;
    for (i = 0; i < sDirCount; i++) {
        if (text == NULL || text[0] == '\0' || SDL_strcasestr(sDir[i].name, text) != NULL) {
            sShown[sShownCount++] = sDir[i];
        }
    }
    return sShownCount;
}

void Port_DirGet(s32 i, PortDirEntry *out) {
    memset(out, 0, sizeof(*out));
    out->chara = -1;
    if (i >= 0 && i < sShownCount) {
        snprintf(out->name, sizeof(out->name), "%s", sShown[i].name);
        out->isDir = sShown[i].isDir;
    }
}

/* What a folder the browser rests on is: 1 a package (its record into out), 0 not one. It can then be previewed
   (Port_ModPreview(-2)). */
s32 Port_DirLook(char *path, PortModInfo *out) {
    PortCharaDesc d;
    memset(out, 0, sizeof(*out));
    out->skin = out->rival = out->chara = -1;
    sBrowse[0] = '\0';
    if (path == NULL || !PortCharas_DescribePath(path, &d)) {
        return 0;
    }
    snprintf(sBrowse, sizeof(sBrowse), "%s", path);
    sBrowseChara = d.replaces >= 0 ? d.replaces : d.base;
    snprintf(out->name, sizeof(out->name), "%s", strrchr(path, '/') != NULL ? strrchr(path, '/') + 1 : path);
    out->chara = sBrowseChara;
    Port_ModCharaName(out->chara, out->target, sizeof(out->target));
    if (d.nameSet) {
        snprintf(out->white, sizeof(out->white), "%s", d.name);
    }
    out->kinds = (d.files & 0x3FF) | ((d.files >> PORT_DESC_PORTRAIT) & 1) << 10;
    out->costumes = d.costumes;
    out->isNew = d.replaces < 0;
    out->problem = problem_of(&d);
    out->charas = 1;
    out->charaList[0] = out->chara;
    return 1;
}

/* ---- adding packages: a folder, a file of one, an archive ---------------------------------------------------- */

/* A drop on the window and the browser's "add" come here. A package is copied (never moved) into `characters`, first
   into characters/.import (a dot folder: no package to the reader, and no change the library's watch sees) and then
   renamed into place, so nothing reads half of one. An archive is unpacked there by a program of the system (tar,
   which on Windows 10 and on reads zip and on Windows 11 7z and rar too; elsewhere bsdtar, unzip, 7z or unrar) and
   every package in it moved in. Links are not followed, in the copy as in the clean up. It runs on a thread of its
   own; the manager touches nothing of the library meanwhile (src/menu/port_modmgr.c, sMm->importing), and the
   library is read again on the menu's thread when it is done (Port_ModImportPoll). */

static SDL_Thread *sImpThread;
static SDL_AtomicInt sImpState;   /* 0 none, 1 runs, 2 done (the menu has not taken it yet) */
static SDL_AtomicInt sImpProg[3]; /* files done, of how many, what (0 looks, 1 unpacks, 2 copies) */
static char sImpPath[700];
static int sImpAll;
static int sImpAdded, sImpSkipped, sImpOwn, sImpUnpackFailed, sImpVisits;
static char sImpFirst[128]; /* the folder (in `characters`) of the first one added, or found there already */
static int sImpFirstAt = -1;

/* The names in a folder (not in the folders in it): an array of `*count` (SDL_free it), NULL if it cannot be read. */
typedef struct {
    char **names;
    int n, cap;
} NameList;

static SDL_EnumerationResult list_one(void *user, const char *dir, const char *name) {
    NameList *l = user;
    char **more;
    (void)dir;
    if (l->n == l->cap) {
        l->cap = l->cap > 0 ? l->cap * 2 : 32;
        if (l->cap > 65536 || (more = SDL_realloc(l->names, (size_t)l->cap * sizeof(char *))) == NULL) {
            return SDL_ENUM_FAILURE;
        }
        l->names = more;
    }
    if ((l->names[l->n] = SDL_strdup(name)) == NULL) {
        return SDL_ENUM_FAILURE;
    }
    l->n++;
    return SDL_ENUM_CONTINUE;
}

static char **list_dir(const char *dir, int *count) {
    NameList l = {NULL, 0, 0};
    *count = 0;
    if (!SDL_EnumerateDirectory(dir, list_one, &l) && l.n == 0) {
        SDL_free(l.names);
        return NULL;
    }
    *count = l.n;
    return l.names;
}

static void list_free(char **names, int n) {
    int k;
    for (k = 0; names != NULL && k < n; k++) {
        SDL_free(names[k]);
    }
    SDL_free(names);
}

static const char *base_of(const char *path) {
    const char *b = path, *c;
    for (c = path; *c != '\0'; c++) {
        if ((*c == '/' || *c == '\\') && c[1] != '\0') {
            b = c + 1;
        }
    }
    return b;
}

/* 0 nothing, 1 a file, 2 a folder; a link (or a junction) is neither */
static int path_kind(const char *path) {
    SDL_PathInfo info;
#ifdef _WIN32
    wchar_t w[1100];
    DWORD a;
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1100) == 0 || (a = GetFileAttributesW(w)) == INVALID_FILE_ATTRIBUTES ||
        (a & FILE_ATTRIBUTE_REPARSE_POINT)) {
        return 0;
    }
#else
    struct stat st;
    if (lstat(path, &st) != 0 || S_ISLNK(st.st_mode)) {
        return 0;
    }
#endif
    if (!SDL_GetPathInfo(path, &info)) {
        return 0;
    }
    return info.type == SDL_PATHTYPE_FILE ? 1 : info.type == SDL_PATHTYPE_DIRECTORY ? 2 : 0;
}

static int is_link(const char *path) {
#ifdef _WIN32
    wchar_t w[1100];
    DWORD a;
    return MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1100) != 0 && (a = GetFileAttributesW(w)) != INVALID_FILE_ATTRIBUTES &&
           (a & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    struct stat st;
    return lstat(path, &st) == 0 && S_ISLNK(st.st_mode);
#endif
}

/* A folder's files and their bytes, all the way down (links left out). */
static void tree_size(const char *dir, long *files, long long *bytes, int depth) {
    char **names;
    char path[1100];
    int n = 0, k;
    SDL_PathInfo info;
    if (depth > 12 || (names = list_dir(dir, &n)) == NULL) {
        return;
    }
    for (k = 0; k < n; k++) {
        if (strchr(names[k], '/') != NULL || snprintf(path, sizeof(path), "%s/%s", dir, names[k]) >= (int)sizeof(path)) {
            continue;
        }
        switch (path_kind(path)) {
        case 1:
            if (SDL_GetPathInfo(path, &info)) {
                (*files)++;
                *bytes += (long long)info.size;
            }
            break;
        case 2:
            tree_size(path, files, bytes, depth + 1);
            break;
        }
    }
    list_free(names, n);
}

static int tree_copy(const char *from, const char *to, int depth) {
    char **names;
    char a[1100], b[1100];
    int n = 0, k, ok = 1;
    if (depth > 12 || !SDL_CreateDirectory(to) || (names = list_dir(from, &n)) == NULL) {
        return 0;
    }
    for (k = 0; k < n && ok; k++) {
        if (strchr(names[k], '/') != NULL || snprintf(a, sizeof(a), "%s/%s", from, names[k]) >= (int)sizeof(a) ||
            snprintf(b, sizeof(b), "%s/%s", to, names[k]) >= (int)sizeof(b)) {
            continue;
        }
        switch (path_kind(a)) {
        case 1:
            ok = SDL_CopyFile(a, b);
            SDL_AddAtomicInt(&sImpProg[0], 1);
            break;
        case 2:
            ok = tree_copy(a, b, depth + 1);
            break;
        }
    }
    list_free(names, n);
    return ok;
}

/* Takes a folder away with everything in it; a link in it is taken away itself, never what it points to. */
static void tree_remove(const char *dir, int depth) {
    char **names;
    char path[1100];
    int n = 0, k;
    if (is_link(dir) || path_kind(dir) != 2) {
        SDL_RemovePath(dir);
        return;
    }
    if (depth <= 12 && (names = list_dir(dir, &n)) != NULL) {
        for (k = 0; k < n; k++) {
            if (strchr(names[k], '/') == NULL && snprintf(path, sizeof(path), "%s/%s", dir, names[k]) < (int)sizeof(path)) {
                tree_remove(path, depth + 1);
            }
        }
        list_free(names, n);
    }
    SDL_RemovePath(dir);
}

/* Takes the links out of a folder unpacked from an archive (before it is moved into `characters`). */
static void links_out(const char *dir, int depth) {
    char **names;
    char path[1100];
    int n = 0, k;
    if (depth > 12 || (names = list_dir(dir, &n)) == NULL) {
        return;
    }
    for (k = 0; k < n; k++) {
        if (strchr(names[k], '/') != NULL || snprintf(path, sizeof(path), "%s/%s", dir, names[k]) >= (int)sizeof(path)) {
            continue;
        }
        if (is_link(path)) {
            SDL_RemovePath(path);
        } else if (path_kind(path) == 2) {
            links_out(path, depth + 1);
        }
    }
    list_free(names, n);
}

/* The name a package gets in `characters`: its folder's, in plain letters (the reader opens files by their bytes,
   which on Windows are not UTF-8), at most 63 of them. */
static void plain_name(const char *from, char *out) {
    int n = 0;
    const unsigned char *c;
    /* (UTF-8's Latin letters with accents, C3 80..BF, as the plain letter: "Végéta" is "Vegeta") */
    static const char kLatin[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo_ouuuuyty";
    for (c = (const unsigned char *)from; *c != '\0' && n < 63; c++) {
        if (c[0] == 0xC3 && c[1] >= 0x80 && c[1] <= 0xBF) {
            out[n++] = kLatin[*++c - 0x80];
            continue;
        }
        if (*c >= 0x80) {
            while ((c[1] & 0xC0) == 0x80) { /* (any other letter: one '_' for all its bytes) */
                c++;
            }
            out[n++] = '_';
            continue;
        }
        out[n++] = *c < 0x20 || *c == 0x7F || strchr("<>:\"|?*\\/", *c) != NULL ? '_' : (char)*c;
    }
    while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '.')) {
        n--;
    }
    out[n] = '\0';
    if (out[0] == '.' || out[0] == '\0') {
        snprintf(out, 64, "%s", "Character");
    }
}

static int in_folder(const char *path, const char *dir) {
    char a[1100], b[1100];
    size_t l;
#ifdef _WIN32
    char *c;
    if (_fullpath(a, path, sizeof(a)) == NULL || _fullpath(b, dir, sizeof(b)) == NULL) {
        return 0;
    }
    for (c = a; *c != '\0'; c++) {
        *c = *c == '\\' ? '/' : *c;
    }
    for (c = b; *c != '\0'; c++) {
        *c = *c == '\\' ? '/' : *c;
    }
    l = strlen(b);
    return SDL_strncasecmp(a, b, l) == 0 && (a[l] == '\0' || a[l] == '/');
#else
    if (realpath(path, a) == NULL || realpath(dir, b) == NULL) {
        return 0;
    }
    l = strlen(b);
    return strncmp(a, b, l) == 0 && (a[l] == '\0' || a[l] == '/');
#endif
}

/* One package (folder `src`) into `characters`. Already there (a folder of that name with the same files): skipped.
   `own`: src lies in characters/.import (unpacked from an archive), so it is moved, not copied. */
static void import_one(const char *root, const char *src, int own) {
    char name[64], dest[420], stage[420], want[64];
    long fa = 0, fb = 0;
    long long ba = 0, bb = 0;
    int n;

    plain_name(base_of(src), want);
    snprintf(dest, sizeof(dest), "%s/%s", root, want);
    snprintf(name, sizeof(name), "%s", want);
    for (n = 2; path_kind(dest) != 0 && n < 100; n++) {
        tree_size(src, &fa, &ba, 0);
        tree_size(dest, &fb, &bb, 0);
        if (fa == fb && ba == bb) { /* (this one is in the library already) */
            sImpSkipped++;
            if (sImpFirst[0] == '\0') {
                snprintf(sImpFirst, sizeof(sImpFirst), "%s", name);
            }
            return;
        }
        fa = fb = 0;
        ba = bb = 0;
        snprintf(name, sizeof(name), "%.56s (%d)", want, n);
        snprintf(dest, sizeof(dest), "%s/%s", root, name);
    }
    if (path_kind(dest) != 0) {
        return;
    }
    if (own) {
        links_out(src, 0);
        if (!SDL_RenamePath(src, dest)) {
            return;
        }
    } else {
        snprintf(stage, sizeof(stage), "%s/.import/%s", root, name);
        tree_remove(stage, 0);
        if (!tree_copy(src, stage, 0) || !SDL_RenamePath(stage, dest)) {
            fprintf(stderr, "bt3: mod manager: could not copy %s: %s\n", src, SDL_GetError());
            tree_remove(stage, 0);
            return;
        }
    }
    fprintf(stderr, "bt3: mod manager: added %s\n", name);
    sImpAdded++;
    if (sImpFirst[0] == '\0') {
        snprintf(sImpFirst, sizeof(sImpFirst), "%s", name);
    }
}

static int is_package(const char *dir) {
    PortCharaDesc d;
    return PortCharas_DescribePath(dir, &d);
}

/* Every package in folder `dir` (it itself, or folders in it, four deep at most; a package's own folders are its
   forms, not packages of their own). */
static void import_search(const char *root, const char *dir, int own, int depth) {
    char **names;
    char path[1100];
    int n = 0, k;
    if (sImpVisits++ > 4000) {
        return;
    }
    if (is_package(dir)) {
        import_one(root, dir, own);
        return;
    }
    if (depth >= 4 || (names = list_dir(dir, &n)) == NULL) {
        return;
    }
    for (k = 0; k < n; k++) {
        if (names[k][0] != '.' && strchr(names[k], '/') == NULL && snprintf(path, sizeof(path), "%s/%s", dir, names[k]) < (int)sizeof(path) &&
            path_kind(path) == 2) {
            import_search(root, path, own, depth + 1);
        }
    }
    list_free(names, n);
}

static int ends_with(const char *s, const char *end) {
    size_t l = strlen(s), e = strlen(end);
    return l > e && SDL_strcasecmp(s + l - e, end) == 0;
}

static int is_archive(const char *path) {
    static const char *const kEnds[] = {".zip", ".7z", ".rar", ".tar", ".tar.gz", ".tgz", ".tar.xz", ".tar.bz2", NULL};
    int k;
    for (k = 0; kEnds[k] != NULL; k++) {
        if (ends_with(path, kEnds[k])) {
            return 1;
        }
    }
    return 0;
}

/* Runs a program of the system and waits for it; 1 if it ran and said it went well. No window of its own. */
static int run_quiet(const char *const *args) {
#ifdef _WIN32
    wchar_t line[3000], arg[1100];
    size_t at = 0;
    int k, ok = 0;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    DWORD code = 1;
    line[0] = L'\0';
    for (k = 0; args[k] != NULL; k++) {
        size_t l;
        if (strchr(args[k], '"') != NULL || MultiByteToWideChar(CP_UTF8, 0, args[k], -1, arg, 1100) == 0) {
            return 0;
        }
        l = wcslen(arg);
        if (at + l + 4 >= sizeof(line) / sizeof(line[0])) {
            return 0;
        }
        line[at++] = k > 0 ? L' ' : L'"';
        if (k > 0) {
            line[at++] = L'"';
        }
        memcpy(line + at, arg, l * sizeof(wchar_t));
        at += l;
        if (l > 0 && arg[l - 1] == L'\\') {
            line[at++] = L'\\'; /* (a backslash before the closing quote would escape it) */
        }
        line[at++] = L'"';
        line[at] = L'\0';
    }
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    if (!CreateProcessW(NULL, line, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        return 0;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    ok = GetExitCodeProcess(pi.hProcess, &code) && code == 0;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return ok;
#else
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_Process *p;
    int code = 1;
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void *)args);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
    p = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (p == NULL) {
        return 0;
    }
    SDL_WaitProcess(p, true, &code);
    SDL_DestroyProcess(p);
    return code == 0;
#endif
}

static int has_entries(const char *dir) {
    int n = 0;
    char **names = list_dir(dir, &n);
    list_free(names, n);
    return n > 0;
}

/* Unpacks archive `arc` into folder `out` (made new); 1 if something came out. */
static int unpack(const char *arc, const char *out) {
#ifdef _WIN32
    static const char *const kTries[][8] = {{"tar", "-xf", "@A", "-C", "@O", NULL}};
#else
    char seven[1200];
    static const char *const kTries[][8] = {
        {"bsdtar", "-xf", "@A", "-C", "@O", NULL},  {"unzip", "-q", "-o", "@A", "-d", "@O", NULL},
        {"7z", "x", "-y", "@7", "@A", NULL},         {"7zz", "x", "-y", "@7", "@A", NULL},
        {"unrar", "x", "-o+", "-y", "@A", "@O/", NULL}, {"tar", "-xf", "@A", "-C", "@O", NULL},
    };
#endif
    char slashed[1100];
    const char *args[8];
    int t, k;

    snprintf(slashed, sizeof(slashed), "%s/", out);
#ifndef _WIN32
    snprintf(seven, sizeof(seven), "-o%s", out);
#endif
    for (t = 0; t < (int)(sizeof(kTries) / sizeof(kTries[0])); t++) {
        tree_remove(out, 0);
        if (!SDL_CreateDirectory(out)) {
            return 0;
        }
        for (k = 0; kTries[t][k] != NULL; k++) {
            const char *a = kTries[t][k];
            args[k] = strcmp(a, "@A") == 0 ? arc : strcmp(a, "@O") == 0 ? out : strcmp(a, "@O/") == 0 ? slashed :
#ifndef _WIN32
                      strcmp(a, "@7") == 0 ? seven :
#endif
                      a;
        }
        args[k] = NULL;
        if (run_quiet(args) && has_entries(out)) {
            return 1;
        }
    }
    tree_remove(out, 0);
    return 0;
}

static int import_work(void *unused) {
    char root[300], work[400], unpacked[420], dir[700];
    char *slash;
    int kind;
    (void)unused;

    sImpAdded = sImpSkipped = sImpOwn = sImpUnpackFailed = sImpVisits = 0;
    sImpFirst[0] = '\0';
    if (!PortCharas_Root(root, sizeof(root))) {
        SDL_SetAtomicInt(&sImpState, 2);
        return 0;
    }
    kind = path_kind(sImpPath);
    if (kind != 0 && in_folder(sImpPath, root)) {
        sImpOwn = 1;
    } else if (kind == 1 && is_archive(sImpPath)) {
        SDL_SetAtomicInt(&sImpProg[2], 1);
        snprintf(work, sizeof(work), "%s/.import", root);
        SDL_CreateDirectory(work);
        snprintf(unpacked, sizeof(unpacked), "%s/.import/unpacked", root);
        if (unpack(sImpPath, unpacked)) {
            SDL_SetAtomicInt(&sImpProg[2], 2);
            import_search(root, unpacked, 1, 0);
        } else {
            sImpUnpackFailed = 1;
            fprintf(stderr, "bt3: mod manager: could not unpack %s (no program for it)\n", sImpPath);
        }
        tree_remove(work, 0);
    } else if (kind != 0) {
        snprintf(dir, sizeof(dir), "%s", sImpPath);
        if (kind == 1) { /* (a file: the folder it is in, if that is a package) */
            slash = strrchr(dir, '/');
#ifdef _WIN32
            if (strrchr(dir, '\\') > slash) {
                slash = strrchr(dir, '\\');
            }
#endif
            if (slash != NULL) {
                *slash = '\0';
            }
        }
        while (strlen(dir) > 1 && (dir[strlen(dir) - 1] == '/' || dir[strlen(dir) - 1] == '\\')) {
            dir[strlen(dir) - 1] = '\0';
        }
        {
            long files = 0;
            long long bytes = 0;
            if (is_package(dir)) {
                tree_size(dir, &files, &bytes, 0);
            }
            SDL_SetAtomicInt(&sImpProg[1], (int)files);
        }
        SDL_SetAtomicInt(&sImpProg[2], 2);
        snprintf(work, sizeof(work), "%s/.import", root);
        SDL_CreateDirectory(work);
        if (!sImpAll || kind == 1) {
            if (is_package(dir)) {
                import_one(root, dir, 0);
            }
        } else {
            import_search(root, dir, 0, 0);
        }
        tree_remove(work, 0);
    }
    SDL_SetAtomicInt(&sImpState, 2);
    return 0;
}

/* Starts adding what is at `path` (all: every package in it; else it as one package); 0 if one runs already. */
s32 Port_ModImportStart(char *path, s32 all) {
    if (SDL_GetAtomicInt(&sImpState) != 0 || path == NULL || Port_NetSession()) {
        return 0;
    }
    if (sImpThread != NULL) {
        SDL_WaitThread(sImpThread, NULL);
        sImpThread = NULL;
    }
    snprintf(sImpPath, sizeof(sImpPath), "%s", path);
    sImpAll = all;
    sImpFirstAt = -1;
    SDL_SetAtomicInt(&sImpProg[0], 0);
    SDL_SetAtomicInt(&sImpProg[1], 0);
    SDL_SetAtomicInt(&sImpProg[2], 0);
    SDL_SetAtomicInt(&sImpState, 1);
    sImpThread = SDL_CreateThread(import_work, "mod import", NULL);
    if (sImpThread == NULL) {
        import_work(NULL);
    }
    return 1;
}

/* 0 none runs, 1 one runs, 2 one just ended (once: the library is read again then, here, on the menu's thread). */
s32 Port_ModImportPoll(s32 *progress) {
    int i;
    if (progress != NULL) {
        for (i = 0; i < 3; i++) {
            progress[i] = SDL_GetAtomicInt(&sImpProg[i]);
        }
    }
    switch (SDL_GetAtomicInt(&sImpState)) {
    case 1:
        return 1;
    case 2:
        if (sImpThread != NULL) {
            SDL_WaitThread(sImpThread, NULL);
            sImpThread = NULL;
        }
        changed();
        sImpFirstAt = -1;
        for (i = 0; sImpFirst[0] != '\0' && i < sLibCount; i++) {
            if (strcmp(sLib[i].folder, sImpFirst) == 0) {
                sImpFirstAt = i;
            }
        }
        SDL_SetAtomicInt(&sImpState, 0);
        return 2;
    }
    return 0;
}

s32 Port_ModImportResult(void) {
    return sImpAdded;
}

s32 Port_ModImportInfo(s32 *skipped) {
    if (skipped != NULL) {
        *skipped = sImpSkipped;
    }
    return sImpOwn ? -2 : sImpUnpackFailed ? -3 : sImpFirstAt;
}

/* The same, waited for (one package; every package in it). */
s32 Port_ModImport(char *path) {
    s32 r = 0;
    if (Port_ModImportStart(path, 0)) {
        while (Port_ModImportPoll(NULL) == 1) {
            SDL_Delay(10);
        }
        r = sImpAdded;
    }
    return r;
}

s32 Port_ModImportAll(char *path) {
    s32 r = 0;
    if (Port_ModImportStart(path, 1)) {
        while (Port_ModImportPoll(NULL) == 1) {
            SDL_Delay(10);
        }
        r = sImpAdded;
    }
    return r;
}

