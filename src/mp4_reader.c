/* See mp4_reader.h. */
#include "mp4_reader.h"
#include <stdlib.h>
#include <string.h>

#define MOOV_MAX (8u * 1024 * 1024)    /* bigger than any index we'd want to hold */

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint64_t rd64(const uint8_t *p) { return (uint64_t)rd32(p) << 32 | rd32(p + 4); }

/* the boxes of one track that matter */
typedef struct {
    const uint8_t *stsd, *stts, *stsz, *stsc, *stco, *co64, *stss, *mdhd, *hdlr;
    size_t stsd_n, stts_n, stsz_n, stsc_n, stco_n, co64_n, stss_n, mdhd_n, hdlr_n;
} trak_t;

/* Walks the boxes in [p, p+n); containers are entered. Payload pointers go into t. */
static void walk(const uint8_t *p, size_t n, trak_t *t, int depth)
{
    while (n >= 8 && depth < 8) {
        uint64_t sz = rd32(p);
        size_t hdr = 8;
        if (sz == 1) {
            if (n < 16) return;
            sz = rd64(p + 8);
            hdr = 16;
        } else if (sz == 0) {
            sz = n;
        }
        if (sz < hdr || sz > n) return;
        const uint8_t *body = p + hdr;
        size_t bn = (size_t)sz - hdr;
        const char *ty = (const char *)p + 4;
#define IS(s) (memcmp(ty, s, 4) == 0)
        if (IS("mdia") || IS("minf") || IS("stbl")) walk(body, bn, t, depth + 1);
        else if (IS("stsd")) { t->stsd = body; t->stsd_n = bn; }
        else if (IS("stts")) { t->stts = body; t->stts_n = bn; }
        else if (IS("stsz")) { t->stsz = body; t->stsz_n = bn; }
        else if (IS("stsc")) { t->stsc = body; t->stsc_n = bn; }
        else if (IS("stco")) { t->stco = body; t->stco_n = bn; }
        else if (IS("co64")) { t->co64 = body; t->co64_n = bn; }
        else if (IS("stss")) { t->stss = body; t->stss_n = bn; }
        else if (IS("mdhd")) { t->mdhd = body; t->mdhd_n = bn; }
        else if (IS("hdlr")) { t->hdlr = body; t->hdlr_n = bn; }
#undef IS
        p += sz;
        n -= (size_t)sz;
    }
}

#define FAIL(msg) do { snprintf(err, errn, "%s", msg); goto fail; } while (0)

/* avc1/avc3 sample entry -> size, NAL length size, SPS/PPS */
static bool parse_stsd(const trak_t *t, mp4r_t *m, char *err, size_t errn)
{
    const uint8_t *p = t->stsd;
    size_t n = t->stsd_n;
    if (!p || n < 16) FAIL("No video description in it.");
    p += 8; n -= 8;                                 /* version/flags, entry count */
    uint32_t esz = rd32(p);
    if (esz < 86 || esz > n) FAIL("Broken video description.");
    if (memcmp(p + 4, "avc1", 4) && memcmp(p + 4, "avc3", 4)) {
        snprintf(err, errn, "The video is %.4s, not H.264.", (const char *)p + 4);
        return false;
    }
    m->width = rd16(p + 32);
    m->height = rd16(p + 34);
    const uint8_t *c = p + 86, *end = p + esz;     /* the entry's child boxes */
    while (c + 8 <= end) {
        uint32_t csz = rd32(c);
        if (csz < 8 || c + csz > end) break;
        if (!memcmp(c + 4, "avcC", 4) && csz >= 8 + 7) {
            const uint8_t *a = c + 8, *ae = c + csz;
            m->nal_len = (a[4] & 3) + 1;
            if (m->nal_len == 3) FAIL("Unsupported NAL length size.");
            size_t cap = csz * 2;                   /* (start codes: +2 bytes a set at most) */
            m->ps = malloc(cap);
            if (!m->ps) FAIL("Out of memory.");
            const uint8_t *q = a + 5;
            for (int pass = 0; pass < 2; pass++) {  /* SPS, then PPS */
                if (q >= ae) break;
                int cnt = pass == 0 ? (*q & 0x1F) : *q;
                q++;
                for (int i = 0; i < cnt && q + 2 <= ae; i++) {
                    uint16_t l = rd16(q);
                    q += 2;
                    if (q + l > ae || m->ps_len + 4 + l > cap) break;
                    memcpy(m->ps + m->ps_len, "\0\0\0\1", 4);
                    memcpy(m->ps + m->ps_len + 4, q, l);
                    m->ps_len += 4 + l;
                    q += l;
                }
            }
            return true;
        }
        c += csz;
    }
    FAIL("No H.264 settings (avcC) in it.");
fail:
    return false;
}

static bool parse_trak(const trak_t *t, mp4r_t *m, char *err, size_t errn)
{
    if (!t->mdhd || t->mdhd_n < 24) FAIL("Broken track header.");
    m->timescale = t->mdhd[0] == 1 ? rd32(t->mdhd + 20) : rd32(t->mdhd + 12);
    if (!m->timescale) m->timescale = 1000;
    if (!parse_stsd(t, m, err, errn)) return false;
    if (!t->stsz || t->stsz_n < 12 || !t->stsc || t->stsc_n < 8 || !t->stts || t->stts_n < 8 ||
        !((t->stco && t->stco_n >= 8) || (t->co64 && t->co64_n >= 8)))
        FAIL("Its frame index is missing.");

    uint32_t fixed = rd32(t->stsz + 4), n = rd32(t->stsz + 8);
    if (!n) FAIL("The video has no frames.");
    if (n > 2000000 || (!fixed && 12 + (size_t)n * 4 > t->stsz_n)) FAIL("Broken frame index.");
    m->n = n;
    m->off = malloc((size_t)n * sizeof(uint64_t));
    m->size = malloc((size_t)n * sizeof(uint32_t));
    m->time = malloc((size_t)n * sizeof(uint32_t));
    m->key = malloc(n);
    if (!m->off || !m->size || !m->time || !m->key) FAIL("Out of memory.");
    for (uint32_t i = 0; i < n; i++) {
        m->size[i] = fixed ? fixed : rd32(t->stsz + 12 + (size_t)i * 4);
        if (m->size[i] > m->max_size) m->max_size = m->size[i];
    }

    /* offsets: chunks (stco/co64), samples per chunk (stsc) */
    bool big = t->co64 != NULL;
    const uint8_t *co = big ? t->co64 : t->stco;
    size_t co_n = big ? t->co64_n : t->stco_n;
    uint32_t nchunks = rd32(co + 4);
    if (8 + (size_t)nchunks * (big ? 8 : 4) > co_n) FAIL("Broken chunk index.");
    uint32_t nsc = rd32(t->stsc + 4);
    if (!nsc || 8 + (size_t)nsc * 12 > t->stsc_n) FAIL("Broken chunk index.");
    uint32_t s = 0;
    for (uint32_t e = 0; e < nsc && s < n; e++) {
        const uint8_t *r = t->stsc + 8 + (size_t)e * 12;
        uint32_t first = rd32(r), per = rd32(r + 4);
        uint32_t last = e + 1 < nsc ? rd32(r + 12) - 1 : nchunks;   /* 1-based, inclusive */
        if (first < 1) FAIL("Broken chunk index.");
        for (uint32_t c = first; c <= last && c <= nchunks && s < n; c++) {
            uint64_t o = big ? rd64(co + 8 + (size_t)(c - 1) * 8) : rd32(co + 8 + (size_t)(c - 1) * 4);
            for (uint32_t k = 0; k < per && s < n; k++) {
                m->off[s] = o;
                o += m->size[s];
                s++;
            }
        }
    }
    if (s < n) FAIL("Broken chunk index.");

    /* times (stts) */
    uint32_t ne = rd32(t->stts + 4), tt = 0;
    s = 0;
    for (uint32_t e = 0; e < ne && 8 + (size_t)e * 8 + 8 <= t->stts_n && s < n; e++) {
        uint32_t cnt = rd32(t->stts + 8 + (size_t)e * 8), d = rd32(t->stts + 12 + (size_t)e * 8);
        for (uint32_t k = 0; k < cnt && s < n; k++) {
            m->time[s++] = tt;
            tt += d;
        }
    }
    uint32_t last_d = s > 1 ? m->time[s - 1] - m->time[s - 2] : m->timescale / 15;
    while (s < n) { m->time[s++] = tt; tt += last_d ? last_d : 1; }   /* (a short table) */
    m->duration = tt;

    /* key frames (stss); none: every frame is one */
    if (t->stss && t->stss_n >= 8) {
        memset(m->key, 0, n);
        uint32_t nk = rd32(t->stss + 4);
        for (uint32_t k = 0; k < nk && 8 + (size_t)k * 4 + 4 <= t->stss_n; k++) {
            uint32_t idx = rd32(t->stss + 8 + (size_t)k * 4);
            if (idx >= 1 && idx <= n) m->key[idx - 1] = 1;
        }
        m->key[0] = 1;                              /* (start somewhere) */
    } else {
        memset(m->key, 1, n);
    }
    return true;
fail:
    return false;
}

bool mp4r_open(FILE *f, mp4r_t *m, char *err, size_t errn)
{
    memset(m, 0, sizeof(*m));
    uint8_t *moov = NULL;
    size_t moov_n = 0;
    uint8_t h[16];
    uint64_t pos = 0;
    bool any = false;
    /* top-level boxes: find moov */
    for (;;) {
        if (fseeko(f, (off_t)pos, SEEK_SET) != 0 || fread(h, 1, 8, f) != 8) break;
        uint64_t sz = rd32(h);
        size_t hdr = 8;
        if (sz == 1) {
            if (fread(h + 8, 1, 8, f) != 8) break;
            sz = rd64(h + 8);
            hdr = 16;
        }
        if (!any && memcmp(h + 4, "ftyp", 4) && memcmp(h + 4, "moov", 4) && memcmp(h + 4, "mdat", 4) &&
            memcmp(h + 4, "free", 4) && memcmp(h + 4, "skip", 4) && memcmp(h + 4, "wide", 4))
            FAIL("Not an MP4 file.");
        any = true;
        if (!memcmp(h + 4, "moov", 4)) {
            if (sz == 0 || sz < hdr || sz - hdr > MOOV_MAX) FAIL("Its index is too big.");
            moov_n = (size_t)(sz - hdr);
            moov = malloc(moov_n);
            if (!moov) FAIL("Out of memory.");
            if (fread(moov, 1, moov_n, f) != moov_n) FAIL("Couldn't read the file.");
            break;
        }
        if (sz == 0) break;                         /* to the end of the file */
        if (sz < hdr) break;
        pos += sz;
    }
    if (!moov) FAIL(any ? "Unfinished video: it has no index (moov)." : "Not an MP4 file.");

    /* the tracks: the first video one */
    const uint8_t *p = moov;
    size_t n = moov_n;
    bool found = false;
    while (n >= 8) {
        uint32_t sz = rd32(p);
        if (sz < 8 || sz > n) break;
        if (!memcmp(p + 4, "trak", 4)) {
            trak_t t = {0};
            walk(p + 8, sz - 8, &t, 0);
            if (t.hdlr && t.hdlr_n >= 12 && !memcmp(t.hdlr + 8, "vide", 4)) {
                if (!parse_trak(&t, m, err, errn)) goto fail;
                found = true;
                break;
            }
        }
        p += sz;
        n -= sz;
    }
    if (!found) FAIL("There's no video track in it.");
    free(moov);
    return true;
fail:
    free(moov);
    mp4r_free(m);
    return false;
}

void mp4r_free(mp4r_t *m)
{
    free(m->off);
    free(m->size);
    free(m->time);
    free(m->key);
    free(m->ps);
    memset(m, 0, sizeof(*m));
}

uint32_t mp4r_key_before(const mp4r_t *m, uint32_t i)
{
    if (i >= m->n) i = m->n ? m->n - 1 : 0;
    while (i > 0 && !m->key[i]) i--;
    return i;
}

size_t mp4r_annexb_cap(const mp4r_t *m)
{
    int l = m->nal_len ? m->nal_len : 4;
    return (size_t)m->max_size + (size_t)m->max_size * (4 - l) / (l + 1) + 64;
}

size_t mp4r_read_annexb(FILE *f, const mp4r_t *m, uint32_t i, uint8_t *out, size_t cap)
{
    if (i >= m->n) return 0;
    size_t sz = m->size[i];
    if (sz > cap) return 0;
    /* read to the end of out, rewrite from the start: the start codes (4 bytes where
     * the length prefix may be 1 or 2) never catch up with the unread input, as
     * cap leaves room for all of them */
    uint8_t *in = out + cap - sz;
    if (fseeko(f, (off_t)m->off[i], SEEK_SET) != 0 || fread(in, 1, sz, f) != sz) return 0;
    int L = m->nal_len;
    size_t ip = 0, op = 0;
    while (ip + (size_t)L <= sz) {
        uint32_t nl = 0;
        for (int k = 0; k < L; k++) nl = nl << 8 | in[ip + k];
        ip += L;
        if (nl > sz - ip) break;                    /* (broken: keep what's whole) */
        out[op] = 0; out[op + 1] = 0; out[op + 2] = 0; out[op + 3] = 1;
        memmove(out + op + 4, in + ip, nl);         /* (in place when L is 4) */
        op += 4 + nl;
        ip += nl;
    }
    return op;
}
