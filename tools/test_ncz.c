// Synthetic NCZ rebuild tests; no game files needed. make test-ncz
// A golden check against a real dump runs when invoked with a path (rebuild + the nsz tool's own
// acceptance: sha256(image) must start with the .ncz file name, the NCA's content id):
//     test_ncz <real.ncz>            (rebuild and verify the hash against the file name)
//     test_ncz <real.ncz> <want.nca> (rebuild and compare bytes with a known-good NCA)
#include "ncz.h"
#include "zstd.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned g_checks;
#define CHECK(x) do { assert(x); ++g_checks; } while (0)

// --- sha256 (test-only; used for the content-id golden check) ----------------

typedef struct {
    uint32_t h[8];
    uint64_t total;
    uint8_t block[64];
    size_t fill;
} Sha256;

static const uint32_t g_k256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static uint32_t ror32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

static void sha256_block(Sha256 *s, const uint8_t *p) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[4 * i] << 24) | ((uint32_t)p[4 * i + 1] << 16) | ((uint32_t)p[4 * i + 2] << 8) | p[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ror32(w[i - 15], 7) ^ ror32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ror32(w[i - 2], 17) ^ ror32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3];
    uint32_t e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t s1 = ror32(e, 6) ^ ror32(e, 11) ^ ror32(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + s1 + ch + g_k256[i] + w[i];
        uint32_t s0 = ror32(a, 2) ^ ror32(a, 13) ^ ror32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d;
    s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}

static void sha256_init(Sha256 *s) {
    static const uint32_t iv[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    memcpy(s->h, iv, sizeof iv);
    s->total = 0;
    s->fill = 0;
}

static void sha256_update(Sha256 *s, const void *data, size_t n) {
    const uint8_t *p = data;
    s->total += n;
    while (n) {
        size_t take = 64 - s->fill;
        if (take > n) take = n;
        memcpy(s->block + s->fill, p, take);
        s->fill += take;
        p += take;
        n -= take;
        if (s->fill == 64) { sha256_block(s, s->block); s->fill = 0; }
    }
}

static void sha256_final(Sha256 *s, uint8_t out[32]) {
    uint64_t bits = s->total * 8;
    uint8_t pad = 0x80;
    sha256_update(s, &pad, 1);
    uint8_t zero = 0;
    while (s->fill != 56) sha256_update(s, &zero, 1);
    uint8_t len[8];
    for (int i = 0; i < 8; i++) len[i] = (uint8_t)(bits >> (56 - 8 * i));
    sha256_update(s, len, 8);
    for (int i = 0; i < 8; i++) {
        out[4 * i] = (uint8_t)(s->h[i] >> 24);
        out[4 * i + 1] = (uint8_t)(s->h[i] >> 16);
        out[4 * i + 2] = (uint8_t)(s->h[i] >> 8);
        out[4 * i + 3] = (uint8_t)s->h[i];
    }
}

// --- tiny growable buffer ---------------------------------------------------

typedef struct {
    uint8_t *data;
    size_t len, cap;
} Buf;

static void buf_put(Buf *b, const void *src, size_t n) {
    if (b->len + n > b->cap) {
        b->cap = b->cap ? b->cap * 2 : 4096;
        while (b->len + n > b->cap) b->cap *= 2;
        b->data = realloc(b->data, b->cap);
        assert(b->data);
    }
    memcpy(b->data + b->len, src, n);
    b->len += n;
}

static void buf_u64le(Buf *b, uint64_t v) {
    for (int i = 0; i < 8; i++) {
        uint8_t x = (uint8_t)(v >> (8 * i));
        buf_put(b, &x, 1);
    }
}

// --- synthetic NCZ fixtures -------------------------------------------------

// A minimal valid zstd frame storing <body> in Raw blocks (no compression): frame magic, a
// single-segment header with an 8-byte content size, then 64 KiB raw blocks. Our decoder accepts
// it; the golden run below exercises real compressed frames.
static void buf_zstd_raw_frame(Buf *b, const uint8_t *body, size_t len) {
    const uint8_t magic[4] = { 0x28, 0xb5, 0x2f, 0xfd };
    buf_put(b, magic, 4);
    const uint8_t fhd = 0xe0;   // FCS field size code 3 (8 bytes) + single segment
    buf_put(b, &fhd, 1);
    buf_u64le(b, (uint64_t)len);   // Frame_Content_Size: 8 bytes little-endian (matches ZSTD_decompressStream)
    size_t off = 0;
    do {
        size_t chunk = (len - off > 65536) ? 65536 : len - off;
        int last = (off + chunk == len);
        uint32_t hdr = (uint32_t)last | (0u << 1) | ((uint32_t)chunk << 3);  // type 0 = Raw
        uint8_t hb[3] = { (uint8_t)hdr, (uint8_t)(hdr >> 8), (uint8_t)(hdr >> 16) };
        buf_put(b, hb, 3);
        buf_put(b, body + off, chunk);
        off += chunk;
    } while (off < len);
}

static void section_entry(Buf *b, uint64_t offset, uint64_t size, uint64_t crypto, const uint8_t key[16], const uint8_t counter[16]) {
    buf_u64le(b, offset);
    buf_u64le(b, size);
    buf_u64le(b, crypto);
    buf_u64le(b, 0);            // padding
    buf_put(b, key ? key : (const uint8_t[16]) { 0 }, 16);
    buf_put(b, counter ? counter : (const uint8_t[16]) { 0 }, 16);
}

// header (0x4000) + NCZSECTN + <count_override or 2> entries + raw zstd frame of <body>
static void build_ncz(Buf *out, const uint8_t *body, size_t body_len, const uint8_t magic[8], uint64_t count_override, int n_entries, int include_frame) {
    uint8_t header[0x4000];
    memcpy(header, "NCA3", 4);
    for (size_t i = 4; i < sizeof header; i++) header[i] = (uint8_t)(i * 7);
    buf_put(out, header, sizeof header);
    buf_put(out, magic ? magic : (const uint8_t *)"NCZSECTN", 8);
    uint64_t count = count_override ? count_override : 2;
    buf_u64le(out, count);
    if (n_entries >= 1) {
        uint64_t half = body_len / 2;
        section_entry(out, 0x4000, half, 1, NULL, NULL);                 // first half plain
    }
    if (n_entries >= 2) {
        uint64_t half = body_len / 2;
        const uint8_t key[16] = { 0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c };
        const uint8_t counter[16] = { 0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0, 0, 0, 0, 0, 0, 0, 0 };  // nonce || zero base, as real files store it
        section_entry(out, 0x4000 + half, body_len - half, 3, key, counter);
    }
    if (include_frame) buf_zstd_raw_frame(out, body, body_len);
}

static void make_body(uint8_t *body, size_t len) {
    uint32_t st = 12345;
    for (size_t i = 0; i < len; i++) {
        st = st * 1103515245u + 12345u;
        body[i] = (uint8_t)(st >> 16);
    }
}

static void write_all(const char *path, const Buf *b) {
    FILE *f = fopen(path, "wb");
    assert(f);
    assert(fwrite(b->data, 1, b->len, f) == b->len);
    fclose(f);
}

static bool read_all(const char *path, Buf *b) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint8_t tmp[65536];
    size_t r;
    while ((r = fread(tmp, 1, sizeof tmp, f)) > 0) buf_put(b, tmp, r);
    fclose(f);
    return true;
}

// expected rebuilt image: the 0x4000 header, then the body with CTR sections re-encrypted
static void expected_image(Buf *out, const uint8_t *body, size_t body_len) {
    uint8_t header[0x4000];
    memcpy(header, "NCA3", 4);
    for (size_t i = 4; i < sizeof header; i++) header[i] = (uint8_t)(i * 7);
    buf_put(out, header, sizeof header);
    uint64_t half = body_len / 2;
    buf_put(out, body, half);
    const uint8_t key[16] = { 0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c };
    const uint8_t counter[16] = { 0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0, 0, 0, 0, 0, 0, 0, 0 };
    uint8_t *enc = malloc(body_len - half);
    assert(enc);
    memcpy(enc, body + half, body_len - half);
    uint64_t block_index = (0x4000 + half) >> 4;   // absolute file offset >> 4, as NCZ decryptors seek
    for (size_t i = 0; i < body_len - half; i += 16) {
        uint8_t ks[16];
        memcpy(ks, counter, 8);
        uint64_t v = block_index++;
        for (int b = 15; b >= 8; b--) {
            ks[b] = (uint8_t)(v & 0xff);
            v >>= 8;
        }
        ncz_aes128_block(key, ks, ks);
        size_t n = (body_len - half - i < 16) ? (body_len - half - i) : 16;
        for (size_t x = 0; x < n; x++) enc[i + x] ^= ks[x];
    }
    buf_put(out, enc, body_len - half);
    free(enc);
}

static bool cancel_cb(void *user, uint64_t body_bytes) {
    (void)body_bytes;
    return *(unsigned *)user == 0;
}

// --- the tests ---------------------------------------------------------------

static void test_aes_known_answer(void) {
    {   // FIPS-197 appendix C.1
        const uint8_t key[16] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f };
        const uint8_t in[16] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff };
        const uint8_t want[16] = { 0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30, 0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a };
        uint8_t got[16];
        ncz_aes128_block(key, in, got);
        CHECK(memcmp(got, want, 16) == 0);
    }
    {   // NIST SP 800-38A F.1.1 (a second vector: the FIPS one alone skipped an S-box entry once)
        const uint8_t key[16] = { 0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c };
        const uint8_t in[16] = { 0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96, 0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a };
        const uint8_t want[16] = { 0x3a, 0xd7, 0x7b, 0xb4, 0x0d, 0x7a, 0x36, 0x60, 0xa8, 0x9e, 0xca, 0xf3, 0x24, 0x66, 0xef, 0x97 };
        uint8_t got[16];
        ncz_aes128_block(key, in, got);
        CHECK(memcmp(got, want, 16) == 0);
    }
}

static void test_rebuild_ok(void) {
    const size_t body_len = 1024 * 1024;
    uint8_t *body = malloc(body_len);
    make_body(body, body_len);
    Buf ncz = { 0 }, want = { 0 }, got = { 0 };
    build_ncz(&ncz, body, body_len, NULL, 0, 2, 1);
    expected_image(&want, body, body_len);
    write_all("build/test_ok.ncz", &ncz);
    {   // probe: is the hand-built frame itself valid?
        uint8_t *dst = malloc(body_len);
        size_t r = ZSTD_decompress(dst, body_len, ncz.data + 0x4000 + 144, ncz.len - 0x4000 - 144);
        if (ZSTD_isError(r)) {
            fprintf(stderr, "frame probe: %s\n", ZSTD_getErrorName(r));
            fflush(stderr);
        }
        CHECK(r == body_len);
        CHECK(memcmp(dst, body, body_len) == 0);
        free(dst);
    }
    uint64_t body_out = 0;
    NczError e = ncz_rebuild("build/test_ok.ncz", "build/test_ok.nca", NULL, NULL, &body_out);
    if (e != NCZ_OK) {
        fprintf(stderr, "rebuild error: %s\n", ncz_error_name(e));
        fflush(stderr);
    }
    CHECK(e == NCZ_OK);
    CHECK(body_out == body_len);
    CHECK(read_all("build/test_ok.nca", &got));
    CHECK(got.len == want.len);
    CHECK(memcmp(got.data, want.data, want.len) == 0);
    free(body);
    free(ncz.data);
    free(want.data);
    free(got.data);
}

static void test_errors(void) {
    const size_t body_len = 64 * 1024;
    uint8_t *body = malloc(body_len);
    make_body(body, body_len);

    {   // wrong table magic
        Buf ncz = { 0 };
        const uint8_t bad[8] = { 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X' };
        build_ncz(&ncz, body, body_len, bad, 0, 2, 1);
        write_all("build/test_magic.ncz", &ncz);
        CHECK(ncz_rebuild("build/test_magic.ncz", "build/x.nca", NULL, NULL, NULL) == NCZ_E_MAGIC);
        free(ncz.data);
    }
    {   // section count of zero
        Buf ncz = { 0 };
        build_ncz(&ncz, body, body_len, NULL, 0, 2, 1);
        memset(ncz.data + 0x4008, 0, 8);
        write_all("build/test_count.ncz", &ncz);
        CHECK(ncz_rebuild("build/test_count.ncz", "build/x.nca", NULL, NULL, NULL) == NCZ_E_TABLE);
        free(ncz.data);
    }
    {   // entries claim 3 but the file has room for none
        Buf ncz = { 0 };
        build_ncz(&ncz, body, body_len, NULL, 3, 1, 0);   // count says 3, one entry, then EOF
        write_all("build/test_short.ncz", &ncz);
        CHECK(ncz_rebuild("build/test_short.ncz", "build/x.nca", NULL, NULL, NULL) == NCZ_E_SHORT);
        free(ncz.data);
    }
    {   // a cipher we do not implement
        Buf ncz = { 0 };
        build_ncz(&ncz, body, body_len, NULL, 0, 2, 1);
        size_t entry = 0x4000 + 16 + 64 + 16;   // second entry's crypto field
        memset(ncz.data + entry, 2, 8);
        write_all("build/test_crypto.ncz", &ncz);
        CHECK(ncz_rebuild("build/test_crypto.ncz", "build/x.nca", NULL, NULL, NULL) == NCZ_E_CRYPTO);
        free(ncz.data);
    }
    {   // truncated zstd body
        Buf ncz = { 0 };
        build_ncz(&ncz, body, body_len, NULL, 0, 2, 1);
        ncz.len -= 32 * 1024;
        write_all("build/test_trunc.ncz", &ncz);
        CHECK(ncz_rebuild("build/test_trunc.ncz", "build/x.nca", NULL, NULL, NULL) == NCZ_E_ZSTD);
        free(ncz.data);
    }
    {   // the progress callback cancels
        Buf ncz = { 0 };
        build_ncz(&ncz, body, body_len, NULL, 0, 2, 1);
        write_all("build/test_cancel.ncz", &ncz);
        unsigned cancel = 1;
        CHECK(ncz_rebuild("build/test_cancel.ncz", "build/x.nca", cancel_cb, &cancel, NULL) == NCZ_E_CANCEL);
        free(ncz.data);
    }
    {   // the block-table variant is rejected, not decoded as garbage
        Buf ncz = { 0 };
        build_ncz(&ncz, body, body_len, NULL, 0, 2, 0);
        buf_put(&ncz, "NCZBLOCK", 8);
        buf_put(&ncz, "0123456789abcdef", 16);
        write_all("build/test_block.ncz", &ncz);
        CHECK(ncz_rebuild("build/test_block.ncz", "build/x.nca", NULL, NULL, NULL) == NCZ_E_TABLE);
        free(ncz.data);
    }
    {   // missing input
        CHECK(ncz_rebuild("build/does_not_exist.ncz", "build/x.nca", NULL, NULL, NULL) == NCZ_E_OPEN);
    }
    free(body);
}

// stream a file through sha256; returns false when the file cannot be read
static bool sha256_file(const char *path, uint8_t out[32]) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    Sha256 s;
    sha256_init(&s);
    static uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) sha256_update(&s, buf, n);
    bool ok = !ferror(f);
    fclose(f);
    sha256_final(&s, out);
    return ok;
}

static void test_golden(const char *ncz_path, const char *expected_path) {
    printf("golden: rebuilding %s\n", ncz_path);
    fflush(stdout);
    NczError e = ncz_rebuild(ncz_path, "build/test_golden.nca", NULL, NULL, NULL);
    if (e != NCZ_OK) {
        printf("golden FAILED: %s\n", ncz_error_name(e));
        exit(1);
    }
    uint8_t hash[32];
    if (!sha256_file("build/test_golden.nca", hash)) {
        printf("golden FAILED: cannot hash build/test_golden.nca\n");
        exit(1);
    }
    if (expected_path == NULL) {
        // the nsz tool's own acceptance: sha256(image) starts with the .ncz file name (first 32 hex)
        const char *base = ncz_path;
        for (const char *p = ncz_path; *p; p++) if (*p == '/' || *p == '\\') base = p + 1;
        char want[33];
        size_t n = 0;
        for (const char *p = base; *p && *p != '.' && n < 32; p++, n++) want[n] = (char)((*p >= 'A' && *p <= 'F') ? *p + 32 : *p);
        want[n] = 0;
        char got[33];
        for (int i = 0; i < 16; i++) snprintf(got + 2 * i, 3, "%02x", hash[i]);
        printf("golden: sha256(image)[0:32] = %s , .ncz name = %s\n", got, want);
        if (n != 32 || strncmp(got, want, 32) != 0) {
            printf("golden FAILED: content id mismatch\n");
            exit(1);
        }
        printf("golden PASSED: rebuilt NCA is byte-identical to the original (content id matches)\n");
        ++g_checks;
        return;
    }
    FILE *a = fopen("build/test_golden.nca", "rb");
    FILE *b = fopen(expected_path, "rb");
    if (!a || !b) {
        printf("golden FAILED: cannot open outputs (%s / %s)\n", "build/test_golden.nca", expected_path);
        exit(1);
    }
    static uint8_t ba[65536], bb[65536];
    size_t total = 0;
    for (;;) {
        size_t ra = fread(ba, 1, sizeof ba, a);
        size_t rb = fread(bb, 1, sizeof bb, b);
        if (ra != rb || memcmp(ba, bb, ra) != 0) {
            printf("golden FAILED: files differ at byte %zu\n", total);
            exit(1);
        }
        total += ra;
        if (ra == 0) break;
    }
    fclose(a);
    fclose(b);
    printf("golden PASSED: %zu bytes identical\n", total);
    ++g_checks;
}

int main(int argc, char **argv) {
    if (argc == 2 || argc == 3) {
        test_golden(argv[1], argc == 3 ? argv[2] : NULL);
        printf("ncz golden: %u checks\n", g_checks);
        return 0;
    }
    test_aes_known_answer();
    test_rebuild_ok();
    test_errors();
    printf("ncz: %u checks\n", g_checks);
    return 0;
}
