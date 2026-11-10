// NSP/XCI/NSZ package extraction for the game files setup (see runtime/nsp_extract.h).
//
// Stage chain (docs/PROGRESS-SIMPLIFIED.md §4): A shallow-extract every package into the staging
// folder (nstool -x <stage> <pkg>) and rebuild its .ncz entries (runtime/ncz.c) - B judge the staged
// NCAs by their decrypted headers (program / public data; an update by the AesCtrEx romfs) -
// C check that title.keys covers every rights id - D extract exefs and fail fast on the game
// version - E extract the RomFS (the update's over the base's, no second 15 GB copy) and the DLC -
// F remove the staging folder. The extracted folders land in game_dir the same way the setup's
// folder flow copies them, so its own checks and the game build continue unchanged.
//
// Key rules (AGENTS.md): key files are read, never logged; the log keeps package and NCA content
// ids, content types and partition layouts only.
#include "nsp_extract.h"
#include "ncz.h"
#include "aes128.h"
#include "dlc_check.h"
#include "platform.h"
#include "game_versions.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define NSPE_PATH_MAX 1100
#define NSPE_SECTOR 0x200
#define NSPE_HEADER_SECTORS 5      // the header block's encrypted range, sectors 1..5 (0xc00 bytes)
#define NSPE_MAX_STAGED 256

#define LOG(...) fprintf(stderr, "[nsp_extract] " __VA_ARGS__)

// The game versions (runtime/game_versions.h), as the launcher's own table.
static const struct NspeVersion {
    const char *name, *id;
    bool supported;
    unsigned romfs_files;
    uint64_t romfs_bytes;
} VERSIONS[] = {
#define GAME_VERSION_ENTRY(v, id, ok, files, bytes) {v, id, ok, files, bytes},
    GAME_VERSIONS(GAME_VERSION_ENTRY)
#undef GAME_VERSION_ENTRY
};

static void set_why(char *why, size_t n, const char *fmt, ...) {
    if (!why || !n) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(why, n, fmt, ap);
    va_end(ap);
}

static bool is_directory(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool is_file_path(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

static bool mkdirs(const char *path) {   // (paths.c's pattern: the parents, then the leaf)
    char p[NSPE_PATH_MAX];
    if (snprintf(p, sizeof p, "%s", path) >= (int)sizeof p) return false;
    for (char *c = p + 1; *c; c++)
        if (*c == '/' || *c == '\\') {
            char s = *c;
            *c = 0;
            mkdir(p, 0755);
            *c = s;
        }
    if (mkdir(p, 0755) != 0 && errno != EEXIST) return is_directory(p);
    return true;
}

static bool ends_with_ci(const char *name, const char *ext) {
    size_t n = strlen(name), e = strlen(ext);
    return n >= e && strcasecmp(name + n - e, ext) == 0;
}

static const char *path_stem(const char *p, char *out, size_t n) {
    const char *slash = strrchr(p, '/'), *base = slash ? slash + 1 : p;
    const char *dot = strrchr(base, '.');
    size_t len = dot && dot > base ? (size_t)(dot - base) : strlen(base);
    if (len >= n) len = n - 1;
    memcpy(out, base, len);
    out[len] = 0;
    return out;
}

static void hex16(char out[33], const uint8_t bytes[16]) {
    for (int b = 0; b < 16; b++) snprintf(out + 2 * b, 3, "%02x", bytes[b]);
}

// ---- staged file lists ----------------------------------------------------------------------

typedef struct {
    char path[NSPE_PATH_MAX];
} NspePath;

typedef struct {
    NspePath v[NSPE_MAX_STAGED];
    int n;
} Paths;

static void paths_push(Paths *list, const char *path) {
    if (list->n >= NSPE_MAX_STAGED) return;
    snprintf(list->v[list->n++].path, NSPE_PATH_MAX, "%s", path);
}

static void scan_dir(const char *dir, int depth, const char *ext, Paths *out) {
    if (depth > 4) return;
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char p[NSPE_PATH_MAX];
        if (snprintf(p, sizeof p, "%s/%s", dir, e->d_name) >= (int)sizeof p) continue;
        if (is_directory(p)) scan_dir(p, depth + 1, ext, out);
        else if (ends_with_ci(e->d_name, ext)) paths_push(out, p);
    }
    closedir(d);
}

// bytes of file data under dir, counted only up to cap (the progress poll walks the tree a lot)
static uint64_t dir_bytes_capped(const char *dir, uint64_t cap, int depth) {
    if (depth > 5 || cap == 0) return 0;
    DIR *d = opendir(dir);
    if (!d) return 0;
    uint64_t sum = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char p[NSPE_PATH_MAX];
        if (snprintf(p, sizeof p, "%s/%s", dir, e->d_name) >= (int)sizeof p) continue;
        struct stat st;
        if (stat(p, &st)) continue;
        if (S_ISDIR(st.st_mode)) sum += dir_bytes_capped(p, cap - sum, depth + 1);
        else sum += (uint64_t)st.st_size;
        if (sum >= cap) break;
    }
    closedir(d);
    return sum;
}

// ---- key files ------------------------------------------------------------------------------
// prod.keys/title.keys lines: "<name> = <hex>" (hactool names; values may carry trailing spaces).

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool read_keys_value(const char *file, const char *name, uint8_t *out, int out_len) {
    FILE *f = fopen(file, "rb");
    if (!f) return false;
    char line[512];
    bool found = false;
    while (!found && fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = line;
        while (*k == ' ' || *k == '\t') k++;
        size_t kn = strlen(k);
        while (kn && (k[kn - 1] == ' ' || k[kn - 1] == '\t' || k[kn - 1] == '\r' || k[kn - 1] == '\n')) k[--kn] = 0;
        if (strcmp(k, name)) continue;
        char *v = eq + 1;
        while (*v == ' ' || *v == '\t') v++;
        if ((int)strlen(v) < out_len * 2) break;
        bool ok = true;
        for (int i = 0; i < out_len && ok; i++) {
            int hi = hex_digit(v[2 * i]), lo = hex_digit(v[2 * i + 1]);
            if (hi < 0 || lo < 0) ok = false;
            else out[i] = (uint8_t)((hi << 4) | lo);
        }
        found = ok;
    }
    fclose(f);
    return found;
}

// title.keys: whether the file has an entry for this rights id ("01007ef00011e8000000000000000003")
static bool keys_file_has_id(const char *file, const char *id_hex) {
    FILE *f = fopen(file, "rb");
    if (!f) return false;
    char line[512];
    bool found = false;
    while (!found && fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = line;
        while (*k == ' ' || *k == '\t') k++;
        found = strncasecmp(k, id_hex, 32) == 0;
    }
    fclose(f);
    return found;
}

// the packages' own tickets (an NSP's .tik files are named after their rights id)
static bool tickets_have_id(const Paths *files, const char *id_hex) {
    for (int i = 0; i < files->n; i++) {
        const char *p = files->v[i].path, *slash = strrchr(p, '/');
        const char *name = slash ? slash + 1 : p;
        if (ends_with_ci(name, ".tik") && strncasecmp(name, id_hex, 32) == 0) return true;
    }
    return false;
}

// ---- XTS (the NCA header decrypt) -----------------------------------------------------------
// nstool's convention (tc::crypto::XtsModeImpl, byte-verified in test/work/nca_probe3.py): the
// tweak starts as 8 zero bytes || BE64(sector), the data key is header_key[0:16] and the tweak key
// header_key[16:32], the tweak advances once per 16-byte block through GF(2^128) x2.

static void gf_mul_tweak(uint8_t t[16]) {
    uint64_t lo, hi;
    memcpy(&lo, t, 8);
    memcpy(&hi, t + 8, 8);           // little-endian words 0 and 1 of the 128-bit value
    uint64_t reduce = hi >> 63;      // falls off the top: the GF(2^128) reduction bit
    uint64_t lo_msb = lo >> 63;      // slides into the bottom of the high word
    lo <<= 1;
    hi = (hi << 1) | lo_msb;
    if (reduce) ((uint8_t *)&lo)[0] ^= 0x87;
    memcpy(t, &lo, 8);
    memcpy(t + 8, &hi, 8);
}

NspeError nspe_xts_decrypt(void *data, size_t size, uint64_t sector_start, const uint8_t header_key[32]) {
    if (!data || !header_key || size == 0 || size % NSPE_SECTOR) return NSPE_ARGS;
    uint8_t *p = data;
    for (uint64_t s = 0; s < size / NSPE_SECTOR; s++) {
        uint8_t *sec = p + (size_t)s * NSPE_SECTOR;
        uint64_t sector = sector_start + s;
        uint8_t tweak[16] = {0};
        for (int i = 0; i < 8; i++) tweak[15 - i] = (uint8_t)(sector >> (8 * i));   // BE64 in bytes 8..16
        uint8_t enc[16];
        aes128_encrypt_block(header_key + 16, tweak, enc);
        for (int off = 0; off < NSPE_SECTOR; off += 16) {
            uint8_t blk[16];
            for (int i = 0; i < 16; i++) blk[i] = sec[off + i] ^ enc[i];
            aes128_decrypt_block(header_key, blk, blk);
            for (int i = 0; i < 16; i++) sec[off + i] = blk[i] ^ enc[i];
            gf_mul_tweak(enc);
        }
    }
    return NSPE_OK;
}

// ---- the header parse -----------------------------------------------------------------------
// Offsets as verified against staged NCAs (test/work/nca_probe3.py, and the CLI matrix in
// docs/PROGRESS-SIMPLIFIED.md §3.2): the fields sit after the two RSA signatures, at 0x200, the
// partition entries at 0x240 and the four partition fs headers at 0x400 (0x200 bytes each).

NspeError nspe_parse_nca_header(const uint8_t block[0xc00], NspeNcaInfo *out) {
    if (!block || !out) return NSPE_ARGS;
    memset(out, 0, sizeof *out);
    if (memcmp(block + 0x200, "NCA3", 4) != 0) return NSPE_NCA;
    out->content_type = block[0x205];
    for (int i = 0; i < 8; i++) out->content_size |= (uint64_t)block[0x208 + i] << (8 * i);
    for (int i = 0; i < 8; i++) out->program_id |= (uint64_t)block[0x210 + i] << (8 * i);
    memcpy(out->rights_id, block + 0x230, 16);
    for (int i = 0; i < 4; i++) {
        const uint8_t *entry = block + 0x240 + 0x10 * i;
        uint32_t start = 0, end = 0;
        for (int b = 0; b < 4; b++) start |= (uint32_t)entry[b] << (8 * b);
        for (int b = 0; b < 4; b++) end |= (uint32_t)entry[4 + b] << (8 * b);
        if (!entry[8] || end <= start) continue;
        const uint8_t *fs = block + 0x400 + NSPE_SECTOR * i;
        out->partition[out->partition_count].offset = (uint64_t)start * NSPE_SECTOR;
        out->partition[out->partition_count].size = (uint64_t)(end - start) * NSPE_SECTOR;
        out->partition[out->partition_count].format_type = fs[2];
        out->partition[out->partition_count].encryption = fs[4];
        for (int b = 0; b < 4; b++) out->partition[out->partition_count].generation |= (uint32_t)fs[0x140 + b] << (8 * b);
        for (int b = 0; b < 4; b++) out->partition[out->partition_count].secure_value |= (uint32_t)fs[0x144 + b] << (8 * b);
        out->partition_count++;
    }
    return NSPE_OK;
}

// ---- nstool ---------------------------------------------------------------------------------

static bool find_nstool(const char *over, char *out, size_t n) {
    if (over && *over) {
        snprintf(out, n, "%s", over);
        return is_file_path(out);
    }
    const char *env = getenv("SAVAGEWIND_NSTOOL");
    if (env && *env) {
        snprintf(out, n, "%s", env);
        if (is_file_path(out)) return true;
    }
    return false;
}

// the bridge between the ncz rebuild's byte progress and the driver's stage fractions
typedef struct {
    const NspeOpts *o;
    double fraction;
} NczBridge;

static bool ncz_bridge_progress(void *user, uint64_t body_bytes) {
    NczBridge *u = user;
    (void)body_bytes;
    if (u->o->cancelled && u->o->cancelled(u->o->user)) return false;
    if (u->o->progress) u->o->progress(u->o->user, NULL, u->fraction);
    return true;
}

// Spawns nstool and waits, polling the output folder's size for the progress bar. frac_to <=
// frac_from: no size polling (the fraction stays put). why gets a short English note on failure
// (the nstool log keeps the details).
static NspeError run_nstool(const NspeOpts *o, const char *const *argv,
                            const char *input, const char *log, const char *watch_dir,
                            uint64_t expected_bytes, double frac_from, double frac_to,
                            char *why, size_t why_size) {
    LOG("nstool:");
    for (const char *const *a = argv; *a; a++) LOG(" %s", *a);
    LOG("\n");
    PlatProc p = plat_spawn(argv, log, false);
    if (!p) {
        set_why(why, why_size, "nstool didn't start for %s (see %s)", input, log);
        return NSPE_NSTOOL;
    }
    uint64_t last = 0;
    for (;;) {
        int rc = plat_proc_wait(p, false);
        if (rc >= 0) {
            if (rc != 0) {
                set_why(why, why_size, "nstool failed on %s (see %s)", input, log);
                return NSPE_PACKAGE;
            }
            if (frac_to > frac_from && o->progress) o->progress(o->user, NULL, frac_to);
            return NSPE_OK;
        }
        if (o->cancelled && o->cancelled(o->user)) {
            plat_proc_kill(p);
            return NSPE_CANCEL;
        }
        if (watch_dir && frac_to > frac_from) {
            uint64_t got = dir_bytes_capped(watch_dir, expected_bytes + 1, 0);
            if (got != last) {
                last = got;
                double f = expected_bytes ? (double)got / (double)expected_bytes : 0;
                if (f > 1) f = 1;
                if (o->progress) o->progress(o->user, NULL, frac_from + (frac_to - frac_from) * f);
            }
        }
        plat_wait_until_ns(plat_now_ns() + 250 * 1000000);
    }
}

// one nstool command line (large enough for the longest run below)
typedef const char *NstArgv[13];

static void argv_keys(NstArgv argv, int *c, const char *nstool, const NspeOpts *o) {
    argv[(*c)++] = nstool;
    argv[(*c)++] = "-k";
    argv[(*c)++] = o->prod_keys;
    if (o->title_keys && *o->title_keys) {
        argv[(*c)++] = "--titlekeyset";
        argv[(*c)++] = o->title_keys;
    }
}

// ---- the run --------------------------------------------------------------------------------

typedef struct {
    NspePath path;
    NspeNcaInfo info;
    char id[64];     // the file name stem: the NCA's content id
} StagedNca;

static int find_partition(const NspeNcaInfo *info, unsigned format_type) {
    for (int i = 0; i < info->partition_count; i++)
        if (info->partition[i].format_type == format_type) return i;
    return -1;
}

NspeError nsp_extract_run(const NspeOpts *o, char *why, size_t why_size) {
    NspeError err = NSPE_OK;
    char nstool[NSPE_PATH_MAX] = "";
    Paths ncas = {0}, tickets = {0}, nczs = {0};
    StagedNca *staged = NULL;
    int staged_n = 0;
    bool wiped_game_files = false;
    uint8_t prod_hdr_key[32];

    if (why && why_size) why[0] = 0;
    if (!o || !o->game_dir || !o->staging_dir || !o->packages || o->package_count < 1 || !o->prod_keys) {
        set_why(why, why_size, "a package, prod.keys, the game dir and the staging dir are all needed");
        return NSPE_ARGS;
    }

    if (!find_nstool(o->nstool_path, nstool, sizeof nstool)) {
        set_why(why, why_size, "the nstool helper program wasn't found");
        return NSPE_NSTOOL;
    }
    if (!read_keys_value(o->prod_keys, "header_key", prod_hdr_key, 32)) {
        set_why(why, why_size, "prod.keys has no readable NCA header key");
        return NSPE_KEYS;
    }

    do {
        // ---- A: shallow-extract every package, then rebuild its .ncz entries
        plat_remove_tree(o->staging_dir);
        if (!mkdirs(o->staging_dir)) {
            set_why(why, why_size, "the staging folder %s can't be created", o->staging_dir);
            err = NSPE_IO;
            break;
        }
        if (o->progress) o->progress(o->user, "Reading the game packages...", 0);
        for (int i = 0; i < o->package_count && err == NSPE_OK; i++) {
            const char *pkg = o->packages[i];
            if (!is_file_path(pkg)) {
                set_why(why, why_size, "the package %s can't be read", pkg);
                err = NSPE_PACKAGE;
                break;
            }
            double f0 = 0.15 * (double)i / (double)o->package_count;
            double f1 = 0.15 * (double)(i + 1) / (double)o->package_count;
            if (o->progress) o->progress(o->user, NULL, f0);
            char pkg_log[NSPE_PATH_MAX];
            snprintf(pkg_log, sizeof pkg_log, "%s/nstool-pkg%d.log", o->staging_dir, i);
            NstArgv argv;
            int c = 0;
            argv_keys(argv, &c, nstool, o);
            argv[c++] = "-x";
            argv[c++] = o->staging_dir;
            argv[c++] = pkg;
            argv[c] = NULL;
            err = run_nstool(o, argv, pkg, pkg_log, NULL, 0, f0, f1, why, why_size);
        }
        if (err != NSPE_OK) break;

        scan_dir(o->staging_dir, 0, ".ncz", &nczs);
        if (nczs.n) {
            if (o->progress) o->progress(o->user, "Rebuilding the compressed entries...", 0.15);
            for (int i = 0; i < nczs.n && err == NSPE_OK; i++) {
                char out_path[NSPE_PATH_MAX], id[64];
                snprintf(out_path, sizeof out_path, "%s", nczs.v[i].path);
                size_t len = strlen(out_path);
                if (len > 4) out_path[len - 4] = 0;   // .ncz -> .nca (same folder, the content id)
                path_stem(nczs.v[i].path, id, sizeof id);
                NczBridge bridge = {o, 0.15 + 0.04 * (double)i / (double)nczs.n};
                uint64_t body_out = 0;
                if (ncz_rebuild(nczs.v[i].path, out_path, ncz_bridge_progress, &bridge, &body_out) != NCZ_OK) {
                    set_why(why, why_size, "the compressed entry %s couldn't be rebuilt (see the log)", id);
                    err = NSPE_NCA;
                    break;
                }
                LOG("ncz: %s.nca rebuilt (%llu bytes)\n", id, (unsigned long long)body_out);
                remove(nczs.v[i].path);
            }
            if (err != NSPE_OK) break;
        }

        // ---- B: judge the staged NCAs by their decrypted headers
        if (o->progress) o->progress(o->user, "Identifying the game content...", 0.2);
        scan_dir(o->staging_dir, 0, ".nca", &ncas);
        staged = calloc((size_t)(ncas.n ? ncas.n : 1), sizeof *staged);
        if (!staged) {
            set_why(why, why_size, "out of memory");
            err = NSPE_IO;
            break;
        }
        for (int i = 0; i < ncas.n; i++) {
            FILE *f = fopen(ncas.v[i].path, "rb");
            if (!f) continue;
            uint8_t block[0xc00];
            size_t got = fread(block, 1, sizeof block, f);
            fclose(f);
            if (got != sizeof block) continue;
            nspe_xts_decrypt(block + NSPE_SECTOR, NSPE_HEADER_SECTORS * NSPE_SECTOR, 1, prod_hdr_key);
            NspeNcaInfo info;
            if (nspe_parse_nca_header(block, &info) != NSPE_OK) {
                LOG("nca: %s has no readable NCA3 header, skipped\n", ncas.v[i].path);
                continue;
            }
            staged[staged_n].path = ncas.v[i];
            staged[staged_n].info = info;
            path_stem(ncas.v[i].path, staged[staged_n].id, sizeof staged[staged_n].id);
            static const char *const CT[] = {"program", "meta", "control", "manual", "data", "public-data"};
            char rid[33];
            hex16(rid, info.rights_id);
            LOG("nca: %s: %s, program id %010llx, rights id %s, %d partition(s)\n", staged[staged_n].id,
                info.content_type < 6 ? CT[info.content_type] : "?", (unsigned long long)info.program_id, rid,
                info.partition_count);
            staged_n++;
        }
        if (!staged_n) {
            set_why(why, why_size, "the packages hold no readable game content");
            err = NSPE_NO_GAME;
            break;
        }

        int update = -1, base = -1;
        int dlc[RT_DLC_MAX];
        for (int i = 0; i < RT_DLC_MAX; i++) dlc[i] = -1;
        for (int i = 0; i < staged_n; i++) {
            const NspeNcaInfo *info = &staged[i].info;
            if (info->content_type == 0) {   // program: the update's romfs says AesCtrEx
                bool patch = false;
                for (int p = 0; p < info->partition_count; p++) patch |= info->partition[p].encryption == 4;
                if (patch && update < 0) update = i;
                else if (!patch && base < 0) base = i;
            } else if (info->content_type == 5) {   // public data: the DLC, numbered after their id
                // BOTW's add-on ids: the application id + 0x1000 + the DLC's number (E000 -> F001, F002)
                int idx = (int)(info->program_id & 0xfff);
                uint64_t family = info->program_id >> 12;
                bool family_ok = true;
                if (update >= 0 || base >= 0) {
                    uint64_t app = (update >= 0 ? staged[update].info.program_id : staged[base].info.program_id) >> 12;
                    family_ok = family == app + 1;
                }
                if (idx >= 1 && idx <= RT_DLC_MAX && dlc[idx - 1] < 0 && family_ok) dlc[idx - 1] = i;
            }   // meta / control / manual / data: the setup doesn't use them
        }
        LOG("judge: update=%s base=%s dlc", update < 0 ? "-" : staged[update].id, base < 0 ? "-" : staged[base].id);
        for (int i = 0; i < RT_DLC_MAX; i++)
            if (dlc[i] >= 0) LOG(" aoc%d=%s", i + 1, staged[dlc[i]].id);
        LOG(" (%d NCAs)\n", staged_n);

        bool game = update >= 0 || base >= 0;
        if (!game && dlc[0] < 0 && dlc[1] < 0) {
            set_why(why, why_size, "the packages hold no game or downloadable content this setup uses");
            err = NSPE_NO_GAME;
            break;
        }
        if (update >= 0 && base < 0) {
            set_why(why, why_size, "the update package needs its base package too (the game data is built from both)");
            err = NSPE_NO_GAME;
            break;
        }

        // ---- C: the titlekeys the chosen NCAs need
        if (o->progress) o->progress(o->user, "Checking the encryption keys...", 0.22);
        scan_dir(o->staging_dir, 0, ".tik", &tickets);
        int chosen[2 + RT_DLC_MAX], chosen_n = 0;
        if (update >= 0) chosen[chosen_n++] = update;
        if (base >= 0) chosen[chosen_n++] = base;
        for (int i = 0; i < RT_DLC_MAX && chosen_n < (int)(sizeof chosen / sizeof *chosen); i++)
            if (dlc[i] >= 0) chosen[chosen_n++] = dlc[i];
        char missing[4][33];
        int nmissing = 0;
        for (int i = 0; i < chosen_n && nmissing < 4; i++) {
            char id[33];
            bool zero = true;
            for (int b = 0; b < 16; b++) zero &= staged[chosen[i]].info.rights_id[b] == 0;
            if (zero) continue;   // no titlekey needed for this NCA
            hex16(id, staged[chosen[i]].info.rights_id);
            if (o->title_keys && *o->title_keys && keys_file_has_id(o->title_keys, id)) continue;
            if (tickets_have_id(&tickets, id)) continue;
            snprintf(missing[nmissing++], 33, "%s", id);
        }
        if (nmissing) {
            char list[200];
            list[0] = 0;
            for (int i = 0; i < nmissing; i++) snprintf(list + strlen(list), sizeof list - strlen(list), "%s%s", i ? " " : "", missing[i]);
            LOG("keys: missing titlekeys for %s\n", list);
            set_why(why, why_size, "%s", list);   // (the rights ids; the setup's failure text wraps them)
            err = NSPE_TITLEKEYS;
            break;
        }

        // ---- D + E: extraction, straight into game_dir (no second copy of anything)
        // a DLC-only run adds to the game files already set up; anything else replaces them
        if (game) {
            if (o->remove_game_files) o->remove_game_files(o->game_dir);   // an earlier or interrupted setup's
            wiped_game_files = true;
            if (!mkdirs(o->game_dir)) {
                set_why(why, why_size, "the game folder %s can't be created", o->game_dir);
                err = NSPE_IO;
                break;
            }
        }

        if (game) {
            // exefs, then fail fast on the game version before the long game data extraction
            if (o->progress) o->progress(o->user, "Extracting the executable...", 0.25);
            const StagedNca *code_nca = staged + (update >= 0 ? update : base);
            int code_part = find_partition(&code_nca->info, 1);   // partitionfs
            if (code_part < 0) {
                set_why(why, why_size, "%s has no executable partition", code_nca->id);
                err = NSPE_NCA;
                break;
            }
            char out[NSPE_PATH_MAX], part[16], code_log[NSPE_PATH_MAX];
            snprintf(out, sizeof out, "%s/exefs", o->game_dir);
            snprintf(part, sizeof part, "/%d/", code_part);
            snprintf(code_log, sizeof code_log, "%s/nstool-exefs.log", o->staging_dir);
            if (!mkdirs(out)) {
                set_why(why, why_size, "the folder %s can't be created", out);
                err = NSPE_IO;
                break;
            }
            NstArgv argv;
            int c = 0;
            argv_keys(argv, &c, nstool, o);
            argv[c++] = "-x";
            argv[c++] = part;
            argv[c++] = out;
            argv[c++] = code_nca->path.path;
            argv[c] = NULL;
            err = run_nstool(o, argv, code_nca->path.path, code_log, out,
                             code_nca->info.partition[code_part].size, 0.25, 0.3, why, why_size);
            if (err != NSPE_OK) break;

            char main_path[NSPE_PATH_MAX];
            snprintf(main_path, sizeof main_path, "%s/exefs/main", o->game_dir);
            FILE *f = fopen(main_path, "rb");
            uint8_t nso[0x48];
            bool is_nso = f && fread(nso, 1, sizeof nso, f) == sizeof nso && !memcmp(nso, "NSO0", 4);
            if (f) fclose(f);
            if (!is_nso) {
                set_why(why, why_size, "exefs/main isn't a game module (the extraction left it out?)");
                err = NSPE_NCA;
                break;
            }
            char build_id[17];
            for (int i = 0; i < 8; i++) snprintf(build_id + 2 * i, 3, "%02x", nso[0x40 + i]);
            LOG("exefs: main build id %s\n", build_id);
            const struct NspeVersion *version = NULL;
            for (size_t i = 0; i < sizeof VERSIONS / sizeof *VERSIONS; i++)
                if (!strcmp(build_id, VERSIONS[i].id)) version = &VERSIONS[i];
            if (!version) {
                set_why(why, why_size, "the game version (build id %s) isn't one this setup knows", build_id);
                err = NSPE_VERSION;
                break;
            }
            if (!version->supported) {
                set_why(why, why_size, "game version %s isn't supported yet", version->name);
                err = NSPE_VERSION;
                break;
            }

            // the merged RomFS: the update's data partition over the base's NCA
            if (o->progress) o->progress(o->user, "Extracting the game data...", 0.3);
            const StagedNca *data_nca = staged + (update >= 0 ? update : base);
            int data_part = find_partition(&data_nca->info, 0);   // romfs
            if (data_part < 0) {
                set_why(why, why_size, "%s has no game data partition", data_nca->id);
                err = NSPE_NCA;
                break;
            }
            snprintf(out, sizeof out, "%s/romfs", o->game_dir);
            snprintf(part, sizeof part, "/%d/", data_part);
            snprintf(code_log, sizeof code_log, "%s/nstool-romfs.log", o->staging_dir);
            if (!mkdirs(out)) {
                set_why(why, why_size, "the folder %s can't be created", out);
                err = NSPE_IO;
                break;
            }
            c = 0;
            argv_keys(argv, &c, nstool, o);
            if (update >= 0) {
                argv[c++] = "--basenca";
                argv[c++] = staged[base].path.path;
            }
            argv[c++] = "-x";
            argv[c++] = part;
            argv[c++] = out;
            argv[c++] = data_nca->path.path;
            argv[c] = NULL;
            uint64_t expected = version->romfs_bytes ? version->romfs_bytes : data_nca->info.partition[data_part].size;
            err = run_nstool(o, argv, data_nca->path.path, code_log, out, expected, 0.3, 0.9, why, why_size);
            if (err != NSPE_OK) break;

            // the game data that goes with the executable (a mixed-up package says its own version)
            char ver_path[NSPE_PATH_MAX];
            snprintf(ver_path, sizeof ver_path, "%s/romfs/System/Version.txt", o->game_dir);
            char found[32] = "";
            if ((f = fopen(ver_path, "rb"))) {
                size_t n = fread(found, 1, sizeof found - 1, f);
                fclose(f);
                found[n] = 0;
                for (char *t = found; *t; t++)
                    if ((unsigned char)*t < 0x20) {
                        *t = 0;
                        break;
                    }
            }
            if (strcmp(found, version->name)) {
                set_why(why, why_size, "the game data says version \"%s\" but the executable is %s", found[0] ? found : "?", version->name);
                err = NSPE_VERSION;
                break;
            }
            LOG("romfs: version \"%s\"\n", found);
        }

        for (int i = 0; i < RT_DLC_MAX; i++) {
            if (dlc[i] < 0) continue;
            double f0 = 0.9 + 0.08 * (double)i / (double)RT_DLC_MAX;
            double f1 = 0.9 + 0.08 * (double)(i + 1) / (double)RT_DLC_MAX;
            if (o->progress) o->progress(o->user, "Extracting the downloadable content...", f0);
            const StagedNca *d = staged + dlc[i];
            int part = find_partition(&d->info, 0);   // a DLC's own romfs
            if (part < 0) part = 0;
            char out[NSPE_PATH_MAX], part_s[16], aoc_log[NSPE_PATH_MAX];
            snprintf(out, sizeof out, "%s/aoc%d", o->game_dir, i + 1);
            snprintf(part_s, sizeof part_s, "/%d/", part);
            snprintf(aoc_log, sizeof aoc_log, "%s/nstool-aoc%d.log", o->staging_dir, i + 1);
            if (!mkdirs(out)) {
                set_why(why, why_size, "the folder %s can't be created", out);
                err = NSPE_IO;
                break;
            }
            NstArgv argv;
            int c = 0;
            argv_keys(argv, &c, nstool, o);
            argv[c++] = "-x";
            argv[c++] = part_s;
            argv[c++] = out;
            argv[c++] = d->path.path;
            argv[c] = NULL;
            err = run_nstool(o, argv, d->path.path, aoc_log, out, d->info.content_size, f0, f1, why, why_size);
            if (err != NSPE_OK) break;
        }
        if (err != NSPE_OK) break;

        if (o->progress) o->progress(o->user, "Cleaning up...", 0.98);
    } while (0);

    // ---- F: the staging goes, whatever the outcome. A cancelled or failed run also takes the half
    // game files it wrote (the folder flow leaves those for its next run to wipe, but a cancelled
    // one is the player changing their mind, and a failed one left the old files deleted already).
    plat_remove_tree(o->staging_dir);
    if (o->remove_game_files && (err == NSPE_CANCEL || (err != NSPE_OK && wiped_game_files)))
        o->remove_game_files(o->game_dir);
    free(staged);
    return err;
}

const char *nspe_error_name(NspeError e) {
    switch (e) {
    case NSPE_OK: return "ok";
    case NSPE_ARGS: return "args";
    case NSPE_NSTOOL: return "nstool";
    case NSPE_KEYS: return "keys";
    case NSPE_TITLEKEYS: return "titlekeys";
    case NSPE_PACKAGE: return "package";
    case NSPE_NO_GAME: return "no-game";
    case NSPE_VERSION: return "version";
    case NSPE_NCA: return "nca";
    case NSPE_IO: return "io";
    case NSPE_CANCEL: return "cancel";
    }
    return "?";
}
