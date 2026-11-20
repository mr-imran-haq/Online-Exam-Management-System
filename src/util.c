#include "common.h"
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir(p, 0755)
#endif

/* ===================== console input helpers ======================= */

void trim(char *s)
{
    size_t len = strlen(s), start = 0;
    while (len > 0 && isspace((unsigned char)s[len - 1])) s[--len] = 0;
    while (s[start] && isspace((unsigned char)s[start])) start++;
    if (start) memmove(s, s + start, strlen(s + start) + 1);
}

/* Reads one line. '|' is replaced by '/' so it can never break the file format. */
void read_line(const char *prompt, char *buf, size_t size)
{
    if (prompt) { fputs(prompt, stdout); fflush(stdout); }
    if (!fgets(buf, (int)size, stdin)) {
        printf("\n[input closed - exiting]\n");
        exit(0);
    }
    session_touch();            /* idle-timeout check: may log the user out */
    if (!strchr(buf, '\n')) {
        int c;
        while ((c = getchar()) != '\n' && c != EOF) { }
    }
    trim(buf);
    for (char *p = buf; *p; p++)
        if (*p == '|') *p = '/';
}

jmp_buf *cancel_top = NULL;
jmp_buf  home_jmp;
int      home_active = 0;

/* "Full name: " + hint  ->  "Full name [blank = go back]: " */
static void hinted(char *out, size_t sz, const char *prompt, const char *hint)
{
    snprintf(out, sz, "%s", prompt);
    size_t l = strlen(out);
    while (l && (out[l - 1] == ' ' || out[l - 1] == ':')) out[--l] = 0;
    snprintf(out + l, sz - l, " [%s]: ", hint);
}

/* Reads a line; a blank answer means "go back" (see GUARD in common.h). */
static void read_or_cancel(const char *prompt, const char *hint, char *buf, size_t size)
{
    char p[400];
    if (strstr(prompt, "0 =")) snprintf(p, sizeof p, "%s", prompt);   /* prompt already says how to leave */
    else hinted(p, sizeof p, prompt, hint);
    for (;;) {
        read_line(p, buf, size);
        if (buf[0]) return;
        if (cancel_top) longjmp(*cancel_top, 1);
        printf("  This field cannot be empty.\n");
    }
}

void read_required(const char *prompt, char *buf, size_t size)
{
    read_or_cancel(prompt, "blank = go back", buf, size);
}

int read_int(const char *prompt, int lo, int hi)
{
    char b[64];
    for (;;) {
        read_line(prompt, b, sizeof b);
        char *end;
        long v = strtol(b, &end, 10);
        if (b[0] && *end == 0 && v >= lo && v <= hi) return (int)v;
        printf("  Please enter a number between %d and %d.\n", lo, hi);
    }
}

/* Menu choice. While a portal is open, H (or h) jumps straight to the dashboard (Home). */
int read_choice(const char *prompt, int lo, int hi)
{
    char p[400], b[64];
    if (home_active) hinted(p, sizeof p, prompt, "H = Home");
    else snprintf(p, sizeof p, "%s", prompt);
    for (;;) {
        read_line(p, b, sizeof b);
        if (home_active && (b[0] == 'h' || b[0] == 'H') && !b[1]) longjmp(home_jmp, 1);
        char *end;
        long v = strtol(b, &end, 10);
        if (b[0] && *end == 0 && v >= lo && v <= hi) return (int)v;
        printf("  Please enter a number between %d and %d%s.\n", lo, hi, home_active ? " (or H for Home)" : "");
    }
}

int read_number(const char *prompt, int lo, int hi)
{
    char b[64];
    for (;;) {
        read_or_cancel(prompt, "blank = go back", b, sizeof b);
        char *end;
        long v = strtol(b, &end, 10);
        if (*end == 0 && v >= lo && v <= hi) return (int)v;
        printf("  Please enter a number between %d and %d.\n", lo, hi);
    }
}

double read_double(const char *prompt, double lo, double hi)
{
    char b[64];
    for (;;) {
        read_or_cancel(prompt, "blank = go back", b, sizeof b);
        char *end;
        double v = strtod(b, &end);
        if (*end == 0 && v >= lo && v <= hi) return v;
        printf("  Please enter a number between %g and %g.\n", lo, hi);
    }
}

/* Student ID looks like 253-33-666: three groups of digits, the first group has 3 digits. */
int valid_student_id(const char *s)
{
    int g = 0, len = 0, first = 0;
    for (const char *p = s;; p++) {
        if (isdigit((unsigned char)*p)) { len++; continue; }
        if (*p != '-' && *p != 0) return 0;
        if (len == 0) return 0;
        if (g == 0) first = len;
        g++;
        len = 0;
        if (*p == 0) break;
    }
    return g == 3 && first == 3;
}

/* Batch = the first 3 digits of the student ID. */
void batch_of(const char *student_id, char *out)
{
    out[0] = 0;
    if (valid_student_id(student_id)) { memcpy(out, student_id, 3); out[3] = 0; }
}

/* Section looks like 46N: 1-3 digits then 1-2 letters (upper case). */
int valid_section(const char *s)
{
    int d = 0, l = 0;
    while (isdigit((unsigned char)s[d])) d++;
    while (isalpha((unsigned char)s[d + l])) l++;
    return d >= 1 && d <= 3 && l >= 1 && l <= 2 && s[d + l] == 0;
}

void mask_email(const char *e, char *out, size_t sz)
{
    const char *at = strchr(e, '@');
    if (!at || at - e < 3) { snprintf(out, sz, "***%s", at ? at : ""); return; }
    snprintf(out, sz, "%.2s***%s", e, at);
}

/* Multi-line text, ended by an empty line. Newlines are stored as the two characters "\n". */
void read_multiline(const char *prompt, char *buf, size_t size)
{
    char line[512];
    size_t len = 0;
    int first = 1;
    buf[0] = 0;
    printf("%s (finish with an empty line)\n", prompt);
    for (;;) {
        read_line("> ", line, sizeof line);
        if (!line[0]) break;
        size_t ll = strlen(line);
        size_t need = ll + (first ? 0 : 2);
        if (len + need + 1 >= size) {
            printf("  (answer too long - the rest was ignored)\n");
            break;
        }
        if (!first) { buf[len++] = '\\'; buf[len++] = 'n'; }
        memcpy(buf + len, line, ll);
        len += ll;
        buf[len] = 0;
        first = 0;
    }
}

int confirm(const char *prompt)
{
    char full[300], b[32];
    snprintf(full, sizeof full, "%s (y/n): ", prompt);
    for (;;) {
        read_line(full, b, sizeof b);
        if (str_ieq(b, "y") || str_ieq(b, "yes")) return 1;
        if (str_ieq(b, "n") || str_ieq(b, "no")) return 0;
        printf("  Please answer y or n.\n");
    }
}

void pause_enter(void)
{
    char b[16];
    read_line(home_active ? "Press Enter to go back [H = Home]: " : "Press Enter to continue...", b, sizeof b);
    if (home_active && (b[0] == 'h' || b[0] == 'H') && !b[1]) longjmp(home_jmp, 1);
}

void print_rule(void)
{
    printf("------------------------------------------------------------\n");
}

void print_header(const char *title)
{
    printf("\n============================================================\n");
    printf("  %s\n", title);
    printf("============================================================\n");
}

void print_unescaped(const char *s, const char *indent)
{
    printf("%s", indent);
    for (; *s; s++) {
        if (s[0] == '\\' && s[1] == 'n') {
            printf("\n%s", indent);
            s++;
        } else {
            putchar(*s);
        }
    }
    putchar('\n');
}

/* ========================= string helpers ========================== */

int str_ieq(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a == *b;
}

int contains_ci(const char *hay, const char *needle)
{
    size_t nl = strlen(needle);
    if (nl == 0) return 1;
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < nl && hay[i] && tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[i])) i++;
        if (i == nl) return 1;
    }
    return 0;
}

void to_upper(char *s) { for (; *s; s++) *s = (char)toupper((unsigned char)*s); }
void to_lower(char *s) { for (; *s; s++) *s = (char)tolower((unsigned char)*s); }

static void random_from(char *out, int len, const char *set)
{
    size_t n = strlen(set);
    for (int i = 0; i < len; i++) out[i] = set[(size_t)rand() % n];
    out[len] = 0;
}

void random_password(char *out, int len)
{
    random_from(out, len, "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz23456789");
}

void random_code(char *out, int len)
{
    random_from(out, len, "ABCDEFGHJKLMNPQRSTUVWXYZ23456789");
}

void fmt_num(double v, char *out, size_t sz)
{
    snprintf(out, sz, "%.2f", v);
    char *dot = strchr(out, '.');
    if (dot) {
        size_t l = strlen(out);
        while (l > 0 && out[l - 1] == '0') out[--l] = 0;
        if (l > 0 && out[l - 1] == '.') out[--l] = 0;
    }
}

/* ============================ date / time ========================== */

void now_string(char *out, size_t sz)
{
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    strftime(out, sz, "%Y-%m-%d %H:%M", lt);
}

int valid_date(const char *s)
{
    int y, m, d;
    if (strlen(s) != 10 || s[4] != '-' || s[7] != '-') return 0;
    if (sscanf(s, "%d-%d-%d", &y, &m, &d) != 3) return 0;
    return y >= 2000 && y <= 2100 && m >= 1 && m <= 12 && d >= 1 && d <= 31;
}

int valid_time(const char *s)
{
    int h, m;
    if (strlen(s) != 5 || s[2] != ':') return 0;
    if (sscanf(s, "%d:%d", &h, &m) != 2) return 0;
    return h >= 0 && h <= 23 && m >= 0 && m <= 59;
}

time_t parse_datetime(const char *date, const char *tm_s)
{
    struct tm t;
    int y = 0, mo = 0, d = 0, h = 0, mi = 0;
    memset(&t, 0, sizeof t);
    sscanf(date, "%d-%d-%d", &y, &mo, &d);
    sscanf(tm_s, "%d:%d", &h, &mi);
    t.tm_year = y - 1900;
    t.tm_mon = mo - 1;
    t.tm_mday = d;
    t.tm_hour = h;
    t.tm_min = mi;
    t.tm_isdst = -1;
    return mktime(&t);
}

void format_epoch(time_t t, char *out, size_t sz)
{
    struct tm *lt = localtime(&t);
    strftime(out, sz, "%Y-%m-%d %H:%M", lt);
}

/* ========================= record helpers ========================== */

/* Returns field number idx of a '|' separated line (rotating static buffers). */
const char *fld(const char *line, int idx)
{
    static char bufs[16][LINE_LEN];
    static int cur = 0;
    char *out = bufs[cur];
    cur = (cur + 1) % 16;
    const char *p = line;
    for (int i = 0; i < idx; i++) {
        p = strchr(p, '|');
        if (!p) { out[0] = 0; return out; }
        p++;
    }
    size_t n = 0;
    while (p[n] && p[n] != '|' && p[n] != '\n' && p[n] != '\r') n++;
    if (n >= LINE_LEN) n = LINE_LEN - 1;
    memcpy(out, p, n);
    out[n] = 0;
    return out;
}

void join_fields(char *out, size_t size, int n, ...)
{
    va_list ap;
    size_t len = 0;
    out[0] = 0;
    va_start(ap, n);
    for (int i = 0; i < n; i++) {
        const char *s = va_arg(ap, const char *);
        if (!s) s = "";
        int w = snprintf(out + len, size - len, "%s%s", i ? "|" : "", s);
        if (w < 0 || (size_t)w >= size - len) { len = size - 1; break; }
        len += (size_t)w;
    }
    va_end(ap);
}

/* ============================== tables ============================= */

static void table_grow(Table *t)
{
    if (t->n < t->cap) return;
    int ncap = t->cap ? t->cap * 2 : 32;
    char (*nr)[LINE_LEN] = realloc(t->row, (size_t)ncap * LINE_LEN);
    if (!nr) { fprintf(stderr, "Out of memory\n"); exit(1); }
    t->row = nr;
    t->cap = ncap;
}

void table_load(const char *path, Table *t)
{
    t->row = NULL; t->n = 0; t->cap = 0;
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[LINE_LEN];
    while (fgets(line, sizeof line, f)) {
        size_t l = strlen(line);
        while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
        if (!l) continue;
        table_grow(t);
        strcpy(t->row[t->n++], line);
    }
    fclose(f);
}

void table_free(Table *t)
{
    free(t->row);
    t->row = NULL; t->n = 0; t->cap = 0;
}

int table_save(const char *path, const Table *t)
{
    FILE *f = fopen(path, "w");
    if (!f) { printf("  [error] cannot write %s\n", path); return 0; }
    for (int i = 0; i < t->n; i++) fprintf(f, "%s\n", t->row[i]);
    fclose(f);
    integrity_update(path);          /* every legitimate write refreshes the file's seal */
    return 1;
}

void table_add(Table *t, const char *line)
{
    table_grow(t);
    strncpy(t->row[t->n], line, LINE_LEN - 1);
    t->row[t->n][LINE_LEN - 1] = 0;
    t->n++;
}

void table_remove(Table *t, int idx)
{
    if (idx < 0 || idx >= t->n) return;
    memmove(t->row[idx], t->row[idx + 1], (size_t)(t->n - idx - 1) * LINE_LEN);
    t->n--;
}

void table_set(Table *t, int idx, const char *line)
{
    if (idx < 0 || idx >= t->n) return;
    strncpy(t->row[idx], line, LINE_LEN - 1);
    t->row[idx][LINE_LEN - 1] = 0;
}

int table_find(const Table *t, int col, const char *val)
{
    for (int i = 0; i < t->n; i++)
        if (strcmp(fld(t->row[i], col), val) == 0) return i;
    return -1;
}

int table_find2(const Table *t, int c1, const char *v1, int c2, const char *v2)
{
    for (int i = 0; i < t->n; i++)
        if (strcmp(fld(t->row[i], c1), v1) == 0 && strcmp(fld(t->row[i], c2), v2) == 0) return i;
    return -1;
}

int table_find3(const Table *t, int c1, const char *v1, int c2, const char *v2,
                int c3, const char *v3)
{
    for (int i = 0; i < t->n; i++)
        if (strcmp(fld(t->row[i], c1), v1) == 0 && strcmp(fld(t->row[i], c2), v2) == 0 &&
            strcmp(fld(t->row[i], c3), v3) == 0) return i;
    return -1;
}

int table_append_line(const char *path, const char *line)
{
    FILE *f = fopen(path, "a");
    if (!f) { printf("  [error] cannot write %s\n", path); return 0; }
    fprintf(f, "%s\n", line);
    fclose(f);
    integrity_update(path);
    return 1;
}

const char *lookup_field(const char *path, int keycol, const char *key, int valcol)
{
    static char out[8][512];
    static int cur = 0;
    char *o = out[cur];
    cur = (cur + 1) % 8;
    o[0] = 0;
    Table t;
    table_load(path, &t);
    int i = table_find(&t, keycol, key);
    if (i >= 0) {
        strncpy(o, fld(t.row[i], valcol), 511);
        o[511] = 0;
    }
    table_free(&t);
    return o;
}

/* Removes every row whose column `col` equals `val`. Returns how many were removed. */
int purge_rows(const char *path, int col, const char *val)
{
    Table t;
    int removed = 0;
    table_load(path, &t);
    for (int i = t.n - 1; i >= 0; i--)
        if (strcmp(fld(t.row[i], col), val) == 0) { table_remove(&t, i); removed++; }
    if (removed) table_save(path, &t);
    table_free(&t);
    return removed;
}

int count_rows(const char *path, int col, const char *val)
{
    Table t;
    int c = 0;
    table_load(path, &t);
    for (int i = 0; i < t.n; i++)
        if (strcmp(fld(t.row[i], col), val) == 0) c++;
    table_free(&t);
    return c;
}

/* ===================== first-run data directory ==================== */

static int file_exists(const char *p)
{
    FILE *f = fopen(p, "r");
    if (f) { fclose(f); return 1; }
    return 0;
}

static void seed(const char *path, const char *const *lines)
{
    if (file_exists(path)) return;
    FILE *f = fopen(path, "w");
    if (!f) return;
    for (int i = 0; lines[i]; i++) fprintf(f, "%s\n", lines[i]);
    fclose(f);
}

void ensure_data(void)
{
    int fresh = !file_exists(F_UNIVIDS) && !file_exists(F_STUDENTS);   /* brand-new installation? */
    MKDIR(DATA_DIR);

    /* The university registry: who is allowed to register (role|id). S=student T=teacher A=admin.
       Only ONE demo admin ID is shipped. The admin adds every student / teacher ID afterwards
       (Admin dashboard -> University ID registry). */
    static const char *const ids[] = { "A|ADM001", NULL };
    static const char *const depts[] = {
        "CSE|Computer Science and Engineering",
        "EEE|Electrical and Electronic Engineering",
        "BBA|Business Administration", NULL };
    static const char *const courses[] = {
        "CSE101|Programming Fundamentals|CSE||",
        "CSE102|Data Structures|CSE||",
        "CSE201|Database Systems|CSE||",
        "EEE101|Basic Electrical Engineering|EEE||",
        "BBA101|Principles of Management|BBA||", NULL };
    static const char *const none[] = { NULL };

    seed(F_UNIVIDS, ids);
    seed(F_DEPTS, depts);
    seed(F_COURSES, courses);
    seed(F_STUDENTS, none);
    seed(F_TEACHERS, none);
    seed(F_ADMINS, none);
    seed(F_OUTBOX, none);
    seed(F_ENROLL, none);
    seed(F_EXAMS, none);
    seed(F_QUESTIONS, none);
    seed(F_ATTEMPTS, none);
    seed(F_MCQ, none);
    seed(F_SHORT, none);
    seed(F_LONG, none);
    seed(F_ORDER, none);
    seed(F_AUDIT, none);
    /* A new installation is sealed right away. An existing installation whose seal is
       missing is NOT sealed silently: the integrity check reports it instead. */
    if (fresh) integrity_seal_all();
}
