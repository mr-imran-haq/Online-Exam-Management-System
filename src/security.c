#include "common.h"
#include <stdarg.h>
#include <stdint.h>

/* ====================================================================== */
/*  SHA-256 (FIPS 180-4). Used for password hashes and for sealing files.  */
/* ====================================================================== */

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static void sha256_block(uint32_t st[8], const unsigned char *p)
{
    uint32_t w[64];
    for (int t = 0; t < 16; t++)
        w[t] = ((uint32_t)p[4 * t] << 24) | ((uint32_t)p[4 * t + 1] << 16) | ((uint32_t)p[4 * t + 2] << 8) |
               (uint32_t)p[4 * t + 3];
    for (int t = 16; t < 64; t++) {
        uint32_t s0 = ROR(w[t - 15], 7) ^ ROR(w[t - 15], 18) ^ (w[t - 15] >> 3);
        uint32_t s1 = ROR(w[t - 2], 17) ^ ROR(w[t - 2], 19) ^ (w[t - 2] >> 10);
        w[t] = w[t - 16] + s0 + w[t - 7] + s1;
    }
    uint32_t a = st[0], b = st[1], c = st[2], d = st[3], e = st[4], f = st[5], g = st[6], h = st[7];
    for (int t = 0; t < 64; t++) {
        uint32_t S1 = ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + S1 + ch + K256[t] + w[t];
        uint32_t S0 = ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22);
        uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + mj;
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    st[0] += a; st[1] += b; st[2] += c; st[3] += d; st[4] += e; st[5] += f; st[6] += g; st[7] += h;
}

static void sha256_raw(const void *data, size_t len, unsigned char out[32])
{
    uint32_t st[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    const unsigned char *p = (const unsigned char *)data;
    size_t i = 0;
    for (; i + 64 <= len; i += 64) sha256_block(st, p + i);

    unsigned char tail[128];
    size_t rem = len - i;
    if (rem) memcpy(tail, p + i, rem);
    tail[rem++] = 0x80;
    size_t padlen = rem <= 56 ? 64 : 128;
    memset(tail + rem, 0, padlen - rem);
    uint64_t bits = (uint64_t)len * 8;
    for (int k = 0; k < 8; k++) tail[padlen - 1 - k] = (unsigned char)(bits >> (8 * k));
    sha256_block(st, tail);
    if (padlen == 128) sha256_block(st, tail + 64);

    for (int k = 0; k < 8; k++) {
        out[4 * k] = (unsigned char)(st[k] >> 24);
        out[4 * k + 1] = (unsigned char)(st[k] >> 16);
        out[4 * k + 2] = (unsigned char)(st[k] >> 8);
        out[4 * k + 3] = (unsigned char)st[k];
    }
}

static void to_hex(const unsigned char *d, char *out)
{
    static const char *hx = "0123456789abcdef";
    for (int i = 0; i < 32; i++) { out[2 * i] = hx[d[i] >> 4]; out[2 * i + 1] = hx[d[i] & 15]; }
    out[64] = 0;
}

void sha256_hex(const void *data, size_t len, char *out_hex)
{
    unsigned char d[32];
    sha256_raw(data, len, d);
    to_hex(d, out_hex);
}

/* Salted, stretched password hash: SHA-256 repeated 2000 times over (previous digest + salt + password). */
void hash_password(const char *pass, const char *salt, char *out)
{
    unsigned char d[32], buf[32 + 512];
    char first[600];
    snprintf(first, sizeof first, "%s:%s", salt, pass);
    sha256_raw(first, strlen(first), d);
    for (int i = 0; i < 2000; i++) {
        memcpy(buf, d, 32);
        int l = snprintf((char *)buf + 32, 512, "%s%s", salt, pass);
        sha256_raw(buf, 32 + (size_t)(l < 0 ? 0 : l > 511 ? 511 : l), d);
    }
    to_hex(d, out);
}

/* ====================================================================== */
/*  Integrity seal: a keyed SHA-256 of every data file, kept in integrity.txt.
    Every write made by this program refreshes the seal. If someone edits a
    data file by hand (Notepad, etc.) the seal no longer matches and the
    program reports it.  The key lives inside the program, so this protects
    against ordinary editing, not against someone who rebuilds the program.  */
/* ====================================================================== */

#define SEAL_KEY "UEMS-seal-key-2026"

static const char *const PROTECTED[] = {
    F_UNIVIDS, F_STUDENTS, F_TEACHERS, F_ADMINS, F_DEPTS, F_COURSES, F_ENROLL, F_EXAMS,
    F_QUESTIONS, F_ATTEMPTS, F_MCQ, F_SHORT, F_LONG, F_FINAL, F_ORDER, F_AUDIT, NULL
};

static int is_protected(const char *path)
{
    for (int i = 0; PROTECTED[i]; i++)
        if (strcmp(PROTECTED[i], path) == 0) return 1;
    return 0;
}

/* Keyed digest of a file. Returns 0 if the file does not exist; *size gets its length. */
static int file_digest(const char *path, char *hex, long *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    size_t kl = strlen(SEAL_KEY);
    unsigned char *buf = malloc(kl + (size_t)len + 1);
    if (!buf) { fclose(f); return 0; }
    memcpy(buf, SEAL_KEY, kl);
    size_t got = len > 0 ? fread(buf + kl, 1, (size_t)len, f) : 0;
    fclose(f);
    sha256_hex(buf, kl + got, hex);
    free(buf);
    if (size) *size = (long)got;
    return 1;
}

void integrity_update(const char *path)
{
    if (!is_protected(path)) return;
    char hex[65], row[LINE_LEN];
    if (!file_digest(path, hex, NULL)) return;
    Table t;
    table_load(F_INTEG, &t);
    join_fields(row, sizeof row, 2, path, hex);
    int i = table_find(&t, 0, path);
    if (i >= 0) table_set(&t, i, row); else table_add(&t, row);
    table_save(F_INTEG, &t);
    table_free(&t);
}

void integrity_seal_all(void)
{
    for (int i = 0; PROTECTED[i]; i++) integrity_update(PROTECTED[i]);
}

int integrity_check(char bad[][96], int max)
{
    Table seal;
    int nb = 0;
    table_load(F_INTEG, &seal);
    for (int i = 0; PROTECTED[i]; i++) {
        const char *p = PROTECTED[i];
        const char *name = strrchr(p, '/') ? strrchr(p, '/') + 1 : p;
        char hex[65];
        long size = 0;
        int exists = file_digest(p, hex, &size);
        int si = table_find(&seal, 0, p);
        const char *msg = NULL;
        if (!exists) { if (si >= 0) msg = "file is missing"; }
        else if (si < 0) { if (size > 0) msg = "has no seal (seal record deleted?)"; }
        else if (strcmp(hex, fld(seal.row[si], 1)) != 0) msg = "was changed outside the program";
        if (msg && nb < max) snprintf(bad[nb++], 96, "%s: %s", name, msg);
    }
    table_free(&seal);
    return nb;
}

int integrity_problems(void)
{
    char bad[24][96];
    return integrity_check(bad, 24);
}

void print_integrity_banner(void)
{
    char bad[24][96];
    int n = integrity_check(bad, 24);
    if (!n) return;
    printf("\n  !!! SECURITY WARNING: %d data file(s) were changed outside the program !!!\n", n);
    for (int i = 0; i < n; i++) printf("      - %s\n", bad[i]);
    printf("  Results cannot be submitted until an admin reviews this (Admin -> Security).\n");
}

/* ====================================================================== */
/*  Activity (audit) log                                                    */
/* ====================================================================== */

Session g_session;

void audit(const char *action, const char *fmt, ...)
{
    char detail[600], stamp[32], line[LINE_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(detail, sizeof detail, fmt, ap);
    va_end(ap);
    for (char *p = detail; *p; p++) if (*p == '|' || *p == '\n' || *p == '\r') *p = ' ';

    time_t t = time(NULL);
    strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", localtime(&t));
    const char *actor = g_session.active && g_session.user ? g_session.user->id : "-";
    const char *role = g_session.active && g_session.user ? role_label(g_session.user->role) : "-";
    join_fields(line, sizeof line, 5, stamp, actor, role, action, detail);
    table_append_line(F_AUDIT, line);
}

/* ====================================================================== */
/*  Session with idle timeout                                               */
/* ====================================================================== */

void session_start(const User *u)
{
    memset(&g_session, 0, sizeof g_session);
    g_session.active = 1;
    g_session.user = u;
    g_session.login_at = g_session.last_activity = time(NULL);
    g_session.idle_limit = IDLE_SECONDS;
    const char *env = getenv("EXAM_IDLE_SECONDS");          /* lets the timeout be tested quickly */
    if (env && atoi(env) > 0) g_session.idle_limit = atoi(env);
    audit("LOGIN", "%s logged in", u->uemail);
}

void session_end(void)
{
    if (g_session.active && !g_session.expired) audit("LOGOUT", "%s logged out", g_session.user->uemail);
    g_session.active = 0;
    g_session.user = NULL;
}

/* Called for every line the user types. If they were away longer than the limit the line is
   dropped and the session is closed. (Like the exam timer, it is checked when Enter is pressed.) */
void session_touch(void)
{
    if (!g_session.active || g_session.exam_mode) return;
    time_t now = time(NULL);
    if (g_session.idle_limit > 0 && now - g_session.last_activity > g_session.idle_limit) {
        printf("\n  Your session expired after %d minute(s) without activity. You were logged out.\n",
               (g_session.idle_limit + 59) / 60);
        audit("SESSION_EXPIRED", "%s was idle for %ld s", g_session.user->uemail, (long)(now - g_session.last_activity));
        g_session.expired = 1;
        longjmp(home_jmp, 1);
    }
    g_session.last_activity = now;
}

/* ====================================================================== */
/*  Admin: Security menu                                                    */
/* ====================================================================== */

static void show_integrity(void)
{
    char bad[24][96];
    int n = integrity_check(bad, 24);
    printf("\n  DATA INTEGRITY CHECK\n");
    print_rule();
    if (!n) { printf("  OK - every data file matches its seal.\n"); return; }
    printf("  PROBLEMS FOUND: %d\n", n);
    for (int i = 0; i < n; i++) printf("    - %s\n", bad[i]);
    printf("\n  Someone changed these files without using the program. Look at them (compare with a backup);\n"
           "  if the changes are fine, choose 'Accept current data' to re-seal.\n");
}

static void reseal(void)
{
    if (integrity_problems() == 0) { printf("  Nothing to accept - all files already match their seals.\n"); return; }
    show_integrity();
    if (!confirm("\nI checked the changes and accept the current files as correct")) { printf("  Not changed.\n"); return; }
    integrity_seal_all();
    audit("INTEGRITY_RESEAL", "admin accepted the current data files");
    printf("  Data re-sealed. This action was written to the activity log.\n");
}

static void view_log(const char *keyword, int limit)
{
    Table t;
    table_load(F_AUDIT, &t);
    int shown = 0;
    for (int i = t.n - 1; i >= 0 && shown < limit; i--)
        if (!keyword || contains_ci(t.row[i], keyword)) shown++;
    printf("\n  ACTIVITY LOG%s%s (newest last)\n", keyword ? " - matching " : "", keyword ? keyword : "");
    print_rule();
    printf("  %-19s %-14s %-8s %-16s %s\n", "TIME", "USER", "ROLE", "ACTION", "DETAIL");
    int skip = shown;
    for (int i = 0; i < t.n; i++) {
        if (keyword && !contains_ci(t.row[i], keyword)) continue;
        if (skip-- > shown) continue;
        const char *r = t.row[i];
        printf("  %-19s %-14.14s %-8s %-16s %s\n", fld(r, 0), fld(r, 1), fld(r, 2), fld(r, 3), fld(r, 4));
    }
    if (!shown) printf("  (nothing)\n");
    table_free(&t);
}

void secure_menu(void)
{
    for (;;) {
        print_header("SECURITY");
        print_integrity_banner();
        printf("\n  1. Check data integrity now\n  2. Accept current data (re-seal after reviewing a warning)\n");
        printf("  3. View activity log (last 25)\n  4. Search activity log\n");
        printf("  5. Reset an interrupted exam attempt\n  0. Back\n");
        int c = read_choice("Choice: ", 0, 5);
        if (c == 0) return;
        GUARD(
            if (c == 1) { show_integrity(); pause_enter(); }
            else if (c == 2) { reseal(); pause_enter(); }
            else if (c == 3) { view_log(NULL, 25); pause_enter(); }
            else if (c == 4) {
                char kw[64];
                read_required("Search word (user, action, exam code ...)", kw, sizeof kw);
                view_log(kw, 40);
                pause_enter();
            } else reset_attempts_menu(NULL)
        );
    }
}
