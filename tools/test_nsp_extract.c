// Synthetic tests for the package extraction driver (make test-nsp-extract). No game files and no
// real keys: the XTS vector is tools/test_xts_vector.inc (test/work/xts_vector.py, nstool's exact
// convention), the NCA headers are fabricated here, and the platform layer plus nstool itself are
// stubbed - the fake nstool writes the files a real extraction would, so a full run reaches
// NSPE_OK through the driver's every stage.
#include "nsp_extract.h"
#include "aes128.h"
#include "platform.h"   // (the PlatProc type; the real implementations aren't linked, see below)
#include "test_xts_vector.inc"

#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static unsigned g_checks;
#define CHECK(x) do { assert(x); ++g_checks; } while (0)

static void rm_tree(const char *dir) {
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            char p[1100];
            snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
            struct stat st;
            if (stat(p, &st)) continue;
            if (S_ISDIR(st.st_mode)) rm_tree(p);
            else remove(p);
        }
        closedir(d);
    }
    rmdir(dir);
}

static void write_text(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    assert(f);
    fwrite(text, 1, strlen(text), f);
    fclose(f);
}

static void mkdirs(const char *path) {
    char p[1100];
    snprintf(p, sizeof p, "%s", path);
    for (char *c = p + 1; *c; c++)
        if (*c == '/') {
            char s = *c;
            *c = 0;
            mkdir(p, 0755);
            *c = s;
        }
    mkdir(p, 0755);
}

// (GAME/STAGE are const char *const, so no literal pasting: join through a buffer)
static FILE *open_under(const char *dir, const char *rest, const char *mode) {
    char p[1100];
    snprintf(p, sizeof p, "%s/%s", dir, rest);
    return fopen(p, mode);
}

// ---- the fabricated NCA headers -------------------------------------------------------------
// The same layout the driver parses (fields at 0x200, partition entries at 0x240, fs headers at
// 0x400): a base program, a DLC pack, and a mismatched-rights-id variant for the keys check.

static const uint8_t RIGHTS_E800_3[16] = {0x01, 0x00, 0x7e, 0xf0, 0x00, 0x11, 0xe8, 0x00,
                                          0, 0, 0, 0, 0, 0, 0, 0x03};

static void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static void put64(uint8_t *p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i)); }

// one header: two partitions (the second optional), the driver's judged fields set
static void fabricate_header(uint8_t *b, unsigned content_type, uint64_t progid, const uint8_t rights[16],
                             int p0_fmt, int p0_enc, int p1_fmt, int p1_enc) {
    memset(b, 0, 0xc00);
    memcpy(b + 0x200, "NCA3", 4);
    b[0x205] = (uint8_t)content_type;
    put64(b + 0x208, 0x3ae96c00ull);       // content size (whatever, the driver only logs it)
    put64(b + 0x210, progid);
    memcpy(b + 0x230, rights, 16);
    put32(b + 0x240, 0x10);                // partition 0: blocks 0x10..0x18
    put32(b + 0x244, 0x18);
    b[0x248] = 1;
    b[0x402] = (uint8_t)p0_fmt;            // its fs header
    b[0x403] = 2;
    b[0x404] = (uint8_t)p0_enc;
    put32(b + 0x540, 0x10);                // generation, secure value: the partition counter
    put32(b + 0x544, 1);
    if (p1_enc) {
        put32(b + 0x250, 0x20);            // partition 1: blocks 0x20..0x28
        put32(b + 0x254, 0x28);
        b[0x258] = 1;
        b[0x602] = (uint8_t)p1_fmt;
        b[0x603] = 3;
        b[0x604] = (uint8_t)p1_enc;
        put32(b + 0x740, 0x10);
        put32(b + 0x744, 2);
    }
}

// the test-side XTS encrypt (the exact inverse of the driver's nspe_xts_decrypt): the stub nstool
// stages encrypted NCAs, because the driver decrypts every header before parsing
static void gf_step(uint8_t t[16]) {
    uint64_t lo, hi;
    memcpy(&lo, t, 8);
    memcpy(&hi, t + 8, 8);
    uint64_t reduce = hi >> 63;
    uint64_t lo_msb = lo >> 63;
    lo <<= 1;
    hi = (hi << 1) | lo_msb;
    if (reduce) ((uint8_t *)&lo)[0] ^= 0x87;
    memcpy(t, &lo, 8);
    memcpy(t + 8, &hi, 8);
}

static void xts_encrypt_header(uint8_t *block, size_t size, uint64_t sector_start, const uint8_t key[32]) {
    for (uint64_t s = 0; s < size / 0x200; s++) {
        uint8_t *sec = block + s * 0x200;
        uint64_t sector = sector_start + s;
        uint8_t tweak[16] = {0};
        for (int i = 0; i < 8; i++) tweak[15 - i] = (uint8_t)(sector >> (8 * i));
        uint8_t enc[16];
        aes128_encrypt_block(key + 16, tweak, enc);
        for (int off = 0; off < 0x200; off += 16) {
            uint8_t blk[16];
            for (int i = 0; i < 16; i++) blk[i] = sec[off + i] ^ enc[i];
            aes128_encrypt_block(key, blk, blk);
            for (int i = 0; i < 16; i++) sec[off + i] = blk[i] ^ enc[i];
            gf_step(enc);
        }
    }
}

// a staged NCA as the stub nstool writes it: plain signatures, encrypted header fields
static void write_nca(const char *path, const uint8_t plain[0xc00], const uint8_t header_key[32]) {
    uint8_t block[0xc10];
    memcpy(block, plain, 0xc00);
    memset(block + 0xc00, 0, 0x10);
    xts_encrypt_header(block + 0x200, 5 * 0x200, 1, header_key);
    FILE *f = fopen(path, "wb");
    assert(f);
    fwrite(block, 1, sizeof block, f);
    fclose(f);
}

// ---- platform stubs (the driver under test calls these; the real ones live in runtime/platform/) --

static int g_spawn_fail;        // plat_spawn returns NULL (nstool didn't start)
static int g_proc_exit;         // the fake nstool's exit code
static int g_wait_running;      // waits that pretend the process is still running
static int g_cancelled;         // what the driver's cancelled() callback says
static int g_stage_tik;         // the package also stages its own ticket (a rights-id-named .tik)
static char g_last_removed[1100];
static char g_build_id[17];     // what the fake nstool writes into exefs/main
static char g_version_text[32]; // and into romfs/System/Version.txt

PlatProc plat_spawn(const char *const argv[], const char *log_path, bool append_log) {
    (void)log_path;
    (void)append_log;
    if (g_spawn_fail) return (PlatProc)0;
    const char *flag = NULL, *out = NULL;
    for (int i = 0; argv[i]; i++)
        if (!strcmp(argv[i], "-x")) {
            flag = argv[i + 1];
            out = argv[i + 2];
        }
    if (!flag) return (PlatProc)0;
    bool package_mode = flag[0] != '/';   // "-x <stage>/ <pkg>" vs "-x /0/ <outdir> <nca>"
    if (package_mode) {
        // a package's shallow extraction: stage a base program NCA and a DLC NCA (content ids);
        // the destination is the -x flag itself, the input is the package
        uint8_t plain[0xc00];
        static const uint8_t HDR_KEY[32] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
                                            0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13,
                                            0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d,
                                            0x1e, 0x1f};   // synthetic (the same bytes syn_prod.keys has)
        char p[1100];
        fabricate_header(plain, 0, 0x01007ef00011e000ull, RIGHTS_E800_3, 1, 3, 0, 3);
        snprintf(p, sizeof p, "%s/10000000000000000000000000000000.nca", flag);
        write_nca(p, plain, HDR_KEY);
        fabricate_header(plain, 5, 0x01007ef00011f001ull, RIGHTS_E800_3, 0, 3, 0, 0);
        snprintf(p, sizeof p, "%s/20000000000000000000000000000000.nca", flag);
        write_nca(p, plain, HDR_KEY);
        if (g_stage_tik) {   // a dump's own ticket: named after the rights id, like real NSPs carry them
            snprintf(p, sizeof p, "%s/01007ef00011e8000000000000000003.tik", flag);
            write_text(p, "tik\n");
        }
    } else if (out && strstr(out, "exefs")) {
        // partition extraction: the executable partition's main module, build id at 0x40
        mkdirs(out);
        uint8_t nso[0x48] = {0};
        memcpy(nso, "NSO0", 4);
        for (int i = 0; i < 8; i++) {
            unsigned v;
            sscanf(g_build_id + 2 * i, "%2x", &v);
            nso[0x40 + i] = (uint8_t)v;
        }
        char p[1100];
        snprintf(p, sizeof p, "%s/main", out);
        FILE *f = fopen(p, "wb");
        assert(f);
        fwrite(nso, 1, sizeof nso, f);
        fclose(f);
    } else if (out && strstr(out, "romfs")) {
        mkdirs(out);
        char p[1100];
        snprintf(p, sizeof p, "%s/System", out);
        mkdirs(p);
        snprintf(p, sizeof p, "%s/System/Version.txt", out);
        write_text(p, g_version_text);
    } else if (out && strstr(out, "aoc")) {
        mkdirs(out);
        char p[1100];
        snprintf(p, sizeof p, "%s/extracted.txt", out);
        write_text(p, "dlc\n");
    }
    return (PlatProc)1;
}

int plat_proc_wait(PlatProc p, bool block) {
    (void)p;
    (void)block;
    if (g_wait_running > 0) {
        g_wait_running--;
        return -1;
    }
    return g_proc_exit;
}

void plat_proc_kill(PlatProc p) { (void)p; }

bool plat_remove_tree(const char *dir) {
    snprintf(g_last_removed, sizeof g_last_removed, "%s", dir);
    return true;
}

uint64_t plat_now_ns(void) { return 0; }
void plat_wait_until_ns(uint64_t t) { (void)t; }

// ---- the driver's callbacks, recorded -------------------------------------------------------

static char g_steps[16][64];
static int g_steps_n;

static void progress_cb(void *user, const char *step, double fraction) {
    (void)user;
    (void)fraction;
    if (g_steps_n < 16) snprintf(g_steps[g_steps_n++], 64, "%s", step ? step : "-");
}

static int cancelled_cb(void *user) {
    (void)user;
    return g_cancelled;
}

// ---- the scenarios --------------------------------------------------------------------------

static const char *const STAGE = "build/nsp-test-stage";
static const char *const GAME = "build/nsp-test-game";

static void reset_state(void) {
    g_spawn_fail = 0;
    g_proc_exit = 0;
    g_wait_running = 0;
    g_cancelled = 0;
    g_stage_tik = 0;
    g_last_removed[0] = 0;
    g_steps_n = 0;
    strcpy(g_build_id, "cd57b23fa4bbad65");   // 1.9.0, supported
    strcpy(g_version_text, "1.9.0\n");
    rm_tree(STAGE);
    rm_tree(GAME);
    mkdirs("build");
    write_text("build/fake-game.nsp", "PKG");   // (the driver checks it exists before extracting)
}

static void base_opts(NspeOpts *o, const char *nstool) {
    static const char *pkgs[1];
    pkgs[0] = "build/fake-game.nsp";
    memset(o, 0, sizeof *o);
    o->packages = pkgs;
    o->package_count = 1;
    o->nstool_path = nstool;
    o->game_dir = GAME;
    o->staging_dir = STAGE;
    o->progress = progress_cb;
    o->cancelled = cancelled_cb;
}

static void write_key_files(void) {
    mkdirs("build");
    write_text("build/syn_prod.keys",
               // synthetic bytes, not key material: 00 01 02 ... 1f (the XTS vector's key)
               "header_key = 000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f\n"
               "master_key_00 = 2122232425262728292a2b2c2d2e2f30   \n");   // (trailing spaces, as dumped)
    write_text("build/syn_title.keys",
               "01007ef00011e8000000000000000003 = 4141414141414141414141414141414141414141414141414141414141414141\n");
}

static void test_aes128(void) {
    {   // FIPS-197 appendix C.1, both directions
        const uint8_t key[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
        const uint8_t in[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
        const uint8_t want[16] = {0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30, 0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a};
        uint8_t got[16], back[16];
        aes128_encrypt_block(key, in, got);
        CHECK(memcmp(got, want, 16) == 0);
        aes128_decrypt_block(key, want, back);
        CHECK(memcmp(back, in, 16) == 0);
    }
    {   // NIST SP 800-38A F.1.1, both directions
        const uint8_t key[16] = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};
        const uint8_t in[16] = {0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96, 0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a};
        const uint8_t want[16] = {0x3a, 0xd7, 0x7b, 0xb4, 0x0d, 0x7a, 0x36, 0x60, 0xa8, 0x9e, 0xca, 0xf3, 0x24, 0x66, 0xef, 0x97};
        uint8_t got[16], back[16];
        aes128_encrypt_block(key, in, got);
        CHECK(memcmp(got, want, 16) == 0);
        aes128_decrypt_block(key, want, back);
        CHECK(memcmp(back, in, 16) == 0);
    }
}

static void test_xts(void) {
    uint8_t work[0x200];
    memcpy(work, XTS_CIPHER, sizeof work);
    CHECK(nspe_xts_decrypt(work, sizeof work, 1, XTS_KEY) == NSPE_OK);
    CHECK(memcmp(work, XTS_PLAIN, sizeof work) == 0);
    CHECK(nspe_xts_decrypt(work, 16, 1, XTS_KEY) == NSPE_ARGS);          // not whole sectors
    CHECK(nspe_xts_decrypt(NULL, 0x200, 1, XTS_KEY) == NSPE_ARGS);
}

static void test_parse(void) {
    uint8_t block[0xc00];
    fabricate_header(block, 0, 0x01007ef00011e000ull, RIGHTS_E800_3, 1, 3, 0, 3);
    NspeNcaInfo info;
    CHECK(nspe_parse_nca_header(block, &info) == NSPE_OK);
    CHECK(info.content_type == 0);
    CHECK(info.content_size == 0x3ae96c00ull);
    CHECK(info.program_id == 0x01007ef00011e000ull);
    CHECK(memcmp(info.rights_id, RIGHTS_E800_3, 16) == 0);
    CHECK(info.partition_count == 2);
    CHECK(info.partition[0].offset == 0x2000);
    CHECK(info.partition[0].size == 0x1000);
    CHECK(info.partition[0].format_type == 1);
    CHECK(info.partition[0].encryption == 3);
    CHECK(info.partition[0].generation == 0x10);
    CHECK(info.partition[0].secure_value == 1);
    CHECK(info.partition[1].offset == 0x4000);
    CHECK(info.partition[1].size == 0x1000);
    CHECK(info.partition[1].format_type == 0);
    CHECK(info.partition[1].encryption == 3);
    CHECK(info.partition[1].generation == 0x10);
    CHECK(info.partition[1].secure_value == 2);
    memcpy(block + 0x200, "NCA2", 4);   // the header parse only takes NCA3
    CHECK(nspe_parse_nca_header(block, &info) == NSPE_NCA);
    memset(block, 0, sizeof block);
    CHECK(nspe_parse_nca_header(block, &info) == NSPE_NCA);
}

static void test_run_errors(const char *self) {
    char why[512];
    NspeOpts o;

    reset_state();
    write_key_files();
    CHECK(nsp_extract_run(NULL, why, sizeof why) == NSPE_ARGS);

    base_opts(&o, self);
    o.package_count = 0;
    CHECK(nsp_extract_run(&o, why, sizeof why) == NSPE_ARGS);

    base_opts(&o, self);
    o.prod_keys = NULL;
    CHECK(nsp_extract_run(&o, why, sizeof why) == NSPE_ARGS);

    // the helper program: missing, then the keys
    reset_state();
    base_opts(&o, "build/definitely-missing-nstool.exe");
    o.prod_keys = "build/syn_prod.keys";
    CHECK(nsp_extract_run(&o, why, sizeof why) == NSPE_NSTOOL);

    reset_state();
    base_opts(&o, self);
    o.prod_keys = "build/no.keys";
    CHECK(nsp_extract_run(&o, why, sizeof why) == NSPE_KEYS);

    reset_state();
    base_opts(&o, self);
    o.prod_keys = "build/syn_prod.keys";
    g_spawn_fail = 1;
    CHECK(nsp_extract_run(&o, why, sizeof why) == NSPE_NSTOOL);

    reset_state();
    base_opts(&o, self);
    o.prod_keys = "build/syn_prod.keys";
    g_proc_exit = 1;   // the fake nstool fails on the package
    CHECK(nsp_extract_run(&o, why, sizeof why) == NSPE_PACKAGE);

    reset_state();
    base_opts(&o, self);
    o.prod_keys = "build/syn_prod.keys";
    g_wait_running = 1;
    g_cancelled = 1;
    CHECK(nsp_extract_run(&o, why, sizeof why) == NSPE_CANCEL);
    CHECK(strcmp(g_last_removed, STAGE) == 0);   // the staging is taken down on cancel
}

static void test_titlekeys(const char *self) {
    char why[512];
    NspeOpts o;
    reset_state();
    write_key_files();
    base_opts(&o, self);
    o.prod_keys = "build/syn_prod.keys";
    o.title_keys = "build/syn_title_other.keys";   // without the rights id the staged NCAs need
    CHECK(nsp_extract_run(&o, why, sizeof why) == NSPE_TITLEKEYS);
    CHECK(strstr(why, "01007ef00011e8000000000000000003") != NULL);

    // the package's own ticket doesn't stand in: its titlekey is locked to the console that made it, and
    // nstool is given the keys file alone, so the entry must be in title.keys either way
    reset_state();
    write_key_files();
    g_stage_tik = 1;
    base_opts(&o, self);
    o.prod_keys = "build/syn_prod.keys";
    o.title_keys = "build/syn_title_other.keys";
    CHECK(nsp_extract_run(&o, why, sizeof why) == NSPE_TITLEKEYS);
    CHECK(strstr(why, "01007ef00011e8000000000000000003") != NULL);
    g_stage_tik = 0;
}

static void test_full_run(const char *self) {
    char why[512];
    NspeOpts o;
    reset_state();
    write_key_files();
    base_opts(&o, self);
    o.prod_keys = "build/syn_prod.keys";
    o.title_keys = "build/syn_title.keys";
    CHECK(nsp_extract_run(&o, why, sizeof why) == NSPE_OK);

    // the fake nstool's files are where the setup expects them
    uint8_t nso[0x48];
    FILE *f = open_under(GAME, "exefs/main", "rb");
    CHECK(f && fread(nso, 1, sizeof nso, f) == sizeof nso);
    if (f) fclose(f);
    CHECK(!memcmp(nso, "NSO0", 4) && !memcmp(nso + 0x40, "\xcd\x57\xb2\x3f\xa4\xbb\xad\x65", 8));
    char ver[32] = "";
    f = open_under(GAME, "romfs/System/Version.txt", "rb");
    CHECK(f && fread(ver, 1, sizeof ver - 1, f) > 0);
    if (f) fclose(f);
    CHECK(!strcmp(ver, g_version_text));
    f = open_under(GAME, "aoc1/extracted.txt", "rb");
    CHECK(f != NULL);
    if (f) fclose(f);
    CHECK(strcmp(g_last_removed, STAGE) == 0);   // the staging is taken down after the run
    CHECK(g_steps_n >= 7);
    CHECK(!strcmp(g_steps[0], "Reading the game packages..."));
    CHECK(!strcmp(g_steps[g_steps_n - 1], "Cleaning up..."));

    // a RomFS that says another version than the executable is refused
    reset_state();
    strcpy(g_version_text, "1.8.2\n");
    base_opts(&o, self);
    o.prod_keys = "build/syn_prod.keys";
    o.title_keys = "build/syn_title.keys";
    CHECK(nsp_extract_run(&o, why, sizeof why) == NSPE_VERSION);

    // an unknown build id is refused before the game data extraction
    reset_state();
    strcpy(g_build_id, "0000000000000000");
    base_opts(&o, self);
    o.prod_keys = "build/syn_prod.keys";
    o.title_keys = "build/syn_title.keys";
    CHECK(nsp_extract_run(&o, why, sizeof why) == NSPE_VERSION);
}

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "-k")) return 0;   // (the test binary plays the stub nstool)
    char self[1100];
    snprintf(self, sizeof self, "%s", argv[0]);
    test_aes128();
    test_xts();
    test_parse();
    test_run_errors(self);
    test_titlekeys(self);
    test_full_run(self);
    printf("nsp-extract: %u checks\n", g_checks);
    return 0;
}
