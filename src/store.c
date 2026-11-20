#include "common.h"

const char *user_file(int role)
{
    return role == ROLE_STUDENT ? F_STUDENTS : role == ROLE_TEACHER ? F_TEACHERS : F_ADMINS;
}

const char *role_label(int role)
{
    return role == ROLE_STUDENT ? "STUDENT" : role == ROLE_TEACHER ? "TEACHER" : "ADMIN";
}

/* ----------------------------- departments ------------------------- */

void list_departments(void)
{
    Table t;
    table_load(F_DEPTS, &t);
    printf("  %-8s %s\n", "CODE", "DEPARTMENT");
    for (int i = 0; i < t.n; i++)
        printf("  %-8s %s\n", fld(t.row[i], D_CODE), fld(t.row[i], D_NAME));
    if (t.n == 0) printf("  (no departments yet)\n");
    table_free(&t);
}

int dept_exists(const char *code)
{
    Table t;
    table_load(F_DEPTS, &t);
    int r = table_find(&t, D_CODE, code) >= 0;
    table_free(&t);
    return r;
}

/* Asks until a valid department code is typed. Returns 0 if the user types 0 to cancel. */
int choose_department(char *out)
{
    printf("Departments:\n");
    list_departments();
    for (;;) {
        char b[64];
        read_required("Department code (0 = cancel): ", b, sizeof b);
        if (strcmp(b, "0") == 0) return 0;
        to_upper(b);
        if (dept_exists(b)) { strcpy(out, b); return 1; }
        printf("  Unknown department code.\n");
    }
}

/* ------------------------------ lookups ---------------------------- */

const char *user_name(int role, const char *id)
{
    return lookup_field(user_file(role), U_ID, id, U_NAME);
}

const char *course_title(const char *code)
{
    return lookup_field(F_COURSES, C_CODE, code, C_TITLE);
}

int is_enrolled(const char *course, const char *student)
{
    Table t;
    table_load(F_ENROLL, &t);
    int r = table_find2(&t, EN_COURSE, course, EN_STUDENT, student) >= 0;
    table_free(&t);
    return r;
}

int attempt_exists(const char *exam, const char *student)
{
    Table t;
    table_load(F_ATTEMPTS, &t);
    int r = table_find2(&t, AT_EXAM, exam, AT_STUDENT, student) >= 0;
    table_free(&t);
    return r;
}

/* Prints every course and lets the user pick one. */
int select_course_any(char *out)
{
    Table t;
    table_load(F_COURSES, &t);
    if (t.n == 0) { printf("  No courses exist yet.\n"); table_free(&t); return 0; }
    printf("  %-4s %-9s %-34s %-6s %s\n", "No.", "CODE", "TITLE", "DEPT", "TEACHER");
    for (int i = 0; i < t.n; i++) {
        const char *tid = fld(t.row[i], C_TEACHER);
        printf("  %-4d %-9s %-34s %-6s %s\n", i + 1, fld(t.row[i], C_CODE),
               fld(t.row[i], C_TITLE), fld(t.row[i], C_DEPT), tid[0] ? tid : "(unassigned)");
    }
    int c = read_choice("Select course number (0 = back): ", 0, t.n);
    if (c > 0) strcpy(out, fld(t.row[c - 1], C_CODE));
    table_free(&t);
    return c > 0;
}

/* ------------------------------- exams ----------------------------- */

time_t exam_start_epoch(const char *erow)
{
    return parse_datetime(fld(erow, E_SDATE), fld(erow, E_STIME));
}

time_t exam_end_epoch(const char *erow)
{
    return parse_datetime(fld(erow, E_EDATE), fld(erow, E_ETIME));
}

int exam_window_state(const char *erow)
{
    time_t n = time(NULL);
    if (n < exam_start_epoch(erow)) return -1;
    if (n > exam_end_epoch(erow)) return 1;
    return 0;
}

int select_exam_in_course(const char *course, char *code_out)
{
    Table t;
    int idx[MAX_LIST], n = 0;
    table_load(F_EXAMS, &t);
    for (int i = 0; i < t.n && n < MAX_LIST; i++)
        if (strcmp(fld(t.row[i], E_COURSE), course) == 0) idx[n++] = i;
    if (n == 0) { printf("  No exams found for this course.\n"); table_free(&t); return 0; }
    printf("  %-4s %-10s %-30s %-11s %s\n", "No.", "CODE", "NAME", "DATE", "STATUS");
    for (int k = 0; k < n; k++) {
        const char *r = t.row[idx[k]];
        printf("  %-4d %-10s %-30s %-11s %s\n", k + 1, fld(r, E_CODE), fld(r, E_NAME),
               fld(r, E_SDATE),
               fld(r, E_PUB)[0] == '1' ? "published" : fld(r, E_FINAL)[0] == '1' ? "submitted" : "pending");
    }
    int c = read_choice("Select exam number (0 = back): ", 0, n);
    if (c > 0) strcpy(code_out, fld(t.row[idx[c - 1]], E_CODE));
    table_free(&t);
    return c > 0;
}

int attended_list(const char *exam, char ids[][64], int max)
{
    Table t;
    int n = 0;
    table_load(F_ATTEMPTS, &t);
    for (int i = 0; i < t.n && n < max; i++)
        if (strcmp(fld(t.row[i], AT_EXAM), exam) == 0) {
            strncpy(ids[n], fld(t.row[i], AT_STUDENT), 63);
            ids[n][63] = 0;
            n++;
        }
    table_free(&t);
    return n;
}

int exam_question_count(const char *exam)
{
    return count_rows(F_QUESTIONS, Q_EXAM, exam);
}

double exam_question_marks(const char *exam, char type)
{
    Table t;
    double sum = 0;
    table_load(F_QUESTIONS, &t);
    for (int i = 0; i < t.n; i++)
        if (strcmp(fld(t.row[i], Q_EXAM), exam) == 0 && (type == 0 || fld(t.row[i], Q_TYPE)[0] == type))
            sum += atof(fld(t.row[i], Q_MARKS));
    table_free(&t);
    return sum;
}

int exam_has_type(const char *exam, char type)
{
    Table t;
    int r = 0;
    table_load(F_QUESTIONS, &t);
    for (int i = 0; i < t.n && !r; i++)
        if (strcmp(fld(t.row[i], Q_EXAM), exam) == 0 && fld(t.row[i], Q_TYPE)[0] == type) r = 1;
    table_free(&t);
    return r;
}

void print_exam_info(const char *erow)
{
    printf("  Exam name        : %s\n", fld(erow, E_NAME));
    printf("  Exam code        : %s\n", fld(erow, E_CODE));
    printf("  Course           : %s - %s\n", fld(erow, E_COURSE), course_title(fld(erow, E_COURSE)));
    printf("  Total marks      : %s\n", fld(erow, E_TOTAL));
    printf("  Start date/time  : %s %s\n", fld(erow, E_SDATE), fld(erow, E_STIME));
    printf("  End date/time    : %s %s\n", fld(erow, E_EDATE), fld(erow, E_ETIME));
    printf("  Duration         : %s minutes\n", fld(erow, E_DUR));
    printf("  Result settings  : MCQ marks shown to student right after submit = %s\n",
           fld(erow, E_SHOWMCQ)[0] == '1' ? "YES" : "NO");
    printf("  Result status    : %s\n",
           fld(erow, E_PUB)[0] == '1' ? "PUBLISHED" : fld(erow, E_FINAL)[0] == '1' ? "SUBMITTED TO ADMIN (not published)" : "PENDING");
}

/* ------------------------------ cascades --------------------------- */

void delete_exam_cascade(const char *exam)
{
    purge_rows(F_EXAMS, E_CODE, exam);
    purge_rows(F_QUESTIONS, Q_EXAM, exam);
    purge_rows(F_ATTEMPTS, AT_EXAM, exam);
    purge_rows(F_MCQ, A_EXAM, exam);
    purge_rows(F_SHORT, A_EXAM, exam);
    purge_rows(F_LONG, A_EXAM, exam);
    purge_rows(F_ORDER, A_EXAM, exam);          /* order.txt has exam|student|... like the answer files */
}

void delete_course_cascade(const char *course)
{
    Table t;
    table_load(F_EXAMS, &t);
    for (int i = 0; i < t.n; i++)
        if (strcmp(fld(t.row[i], E_COURSE), course) == 0) {
            char code[64];
            strncpy(code, fld(t.row[i], E_CODE), 63);
            code[63] = 0;
            delete_exam_cascade(code);
        }
    table_free(&t);
    purge_rows(F_ENROLL, EN_COURSE, course);
    purge_rows(F_COURSES, C_CODE, course);
}

void delete_student_cascade(const char *id)
{
    purge_rows(F_ENROLL, EN_STUDENT, id);
    purge_rows(F_ATTEMPTS, AT_STUDENT, id);
    purge_rows(F_MCQ, A_STUDENT, id);
    purge_rows(F_SHORT, A_STUDENT, id);
    purge_rows(F_LONG, A_STUDENT, id);
    purge_rows(F_ORDER, A_STUDENT, id);
    purge_rows(F_STUDENTS, U_ID, id);
}

/* ---------------------- structures loaded from the files ------------ */

static void copy_str(char *dst, size_t n, const char *src)
{
    strncpy(dst, src, n - 1);
    dst[n - 1] = 0;
}

int exam_get(const char *code, Exam *e)
{
    Table t;
    table_load(F_EXAMS, &t);
    int i = table_find(&t, E_CODE, code);
    if (i >= 0) {
        const char *r = t.row[i];
        memset(e, 0, sizeof *e);
        copy_str(e->code, sizeof e->code, fld(r, E_CODE));
        copy_str(e->course, sizeof e->course, fld(r, E_COURSE));
        copy_str(e->name, sizeof e->name, fld(r, E_NAME));
        e->total = atof(fld(r, E_TOTAL));
        copy_str(e->sdate, sizeof e->sdate, fld(r, E_SDATE));
        copy_str(e->edate, sizeof e->edate, fld(r, E_EDATE));
        copy_str(e->stime, sizeof e->stime, fld(r, E_STIME));
        copy_str(e->etime, sizeof e->etime, fld(r, E_ETIME));
        e->duration = atoi(fld(r, E_DUR));
        e->show_mcq = fld(r, E_SHOWMCQ)[0] == '1';
        e->finalized = fld(r, E_FINAL)[0] == '1';
        e->published = fld(r, E_PUB)[0] == '1';
    }
    table_free(&t);
    return i >= 0;
}

/* Fills the array q[] with the questions of `exam` (in question-number order). Returns the count. */
int questions_load(const char *exam, Question *q, int max)
{
    Table t;
    int n = 0;
    table_load(F_QUESTIONS, &t);
    for (int i = 0; i < t.n && n < max; i++) {
        const char *r = t.row[i];
        if (strcmp(fld(r, Q_EXAM), exam) != 0) continue;
        Question *x = &q[n++];
        memset(x, 0, sizeof *x);
        x->no = atoi(fld(r, Q_NO));
        x->type = fld(r, Q_TYPE)[0];
        x->marks = atof(fld(r, Q_MARKS));
        copy_str(x->text, sizeof x->text, fld(r, Q_TEXT));
        for (int k = 0; k < 4; k++) copy_str(x->opt[k], sizeof x->opt[k], fld(r, Q_A + k));
        x->correct = fld(r, Q_CORRECT)[0];
    }
    table_free(&t);
    return n;
}

int attempt_get(const char *exam, const char *student, Attempt *a)
{
    Table t;
    table_load(F_ATTEMPTS, &t);
    int i = table_find2(&t, AT_EXAM, exam, AT_STUDENT, student);
    if (i >= 0 && a) {
        const char *r = t.row[i];
        memset(a, 0, sizeof *a);
        copy_str(a->exam, sizeof a->exam, fld(r, AT_EXAM));
        copy_str(a->student, sizeof a->student, fld(r, AT_STUDENT));
        a->start = (time_t)atoll(fld(r, AT_START));
        a->end = (time_t)atoll(fld(r, AT_END));
        copy_str(a->status, sizeof a->status, fld(r, AT_STATUS));
    }
    table_free(&t);
    return i >= 0;
}

int exam_window_state_of(const Exam *e)
{
    time_t n = time(NULL);
    if (n < parse_datetime(e->sdate, e->stime)) return -1;
    if (n > parse_datetime(e->edate, e->etime)) return 1;
    return 0;
}

/* The moment an attempt had to be over: start + duration, but never later than the exam window. */
static time_t attempt_deadline(const Exam *e, const Attempt *a)
{
    time_t d = a->start + (time_t)e->duration * 60;
    time_t w = parse_datetime(e->edate, e->etime);
    return w < d ? w : d;
}

/* ATTENDED when it was submitted, INTERRUPTED when it was started but never submitted and its time is over. */
const char *attempt_state(const Exam *e, const Attempt *a)
{
    if (strcmp(a->status, "STARTED") != 0) return "ATTENDED";
    return time(NULL) > attempt_deadline(e, a) ? "INTERRUPTED" : "IN PROGRESS?";
}

/* Shows EVERY student / teacher first (so nothing has to be remembered), then lets the user either pick a
   number from the list or type a word (ID, name, batch, section, department) to narrow it down.
   Blank answer = go back (see read_required / GUARD). */
int pick_user_list(int role, char *id_out)
{
    char q[128] = "";
    int is_stu = role == ROLE_STUDENT;
    for (;;) {
        Table t;
        int idx[MAX_LIST], n = 0;
        table_load(user_file(role), &t);
        for (int i = 0; i < t.n && n < MAX_LIST; i++) {
            const char *r = t.row[i];
            if (!q[0] || contains_ci(fld(r, U_ID), q) || contains_ci(fld(r, U_NAME), q) ||
                contains_ci(fld(r, U_DEPT), q) || contains_ci(fld(r, U_UEMAIL), q) ||
                (is_stu && (strcmp(fld(r, U_BATCH), q) == 0 || str_ieq(fld(r, U_SECTION), q))))
                idx[n++] = i;
        }
        if (t.n == 0) { printf("\n  No %s accounts exist yet.\n", is_stu ? "student" : "teacher"); table_free(&t); return 0; }
        if (n == 0) {
            printf("\n  NOTHING FOUND for '%s'. Showing the full list again.\n", q);
            q[0] = 0;
            table_free(&t);
            continue;
        }
        if (q[0]) printf("\n  %s matching '%s' (%d of %d)\n", is_stu ? "STUDENTS" : "TEACHERS", q, n, t.n);
        else printf("\n  ALL %s (%d)\n", is_stu ? "STUDENTS" : "TEACHERS", t.n);
        if (is_stu) printf("  %-4s %-12s %-22s %-5s %-5s %-5s %-12s %s\n", "No.", "ID", "NAME", "BATCH", "SEC", "DEPT", "PHONE", "UNIVERSITY EMAIL");
        else        printf("  %-4s %-10s %-24s %-5s %-12s %s\n", "No.", "ID", "NAME", "DEPT", "PHONE", "UNIVERSITY EMAIL");
        for (int k = 0; k < n; k++) {
            const char *r = t.row[idx[k]];
            if (is_stu)
                printf("  %-4d %-12s %-22.22s %-5s %-5s %-5s %-12.12s %s\n", k + 1, fld(r, U_ID), fld(r, U_NAME),
                       fld(r, U_BATCH), fld(r, U_SECTION), fld(r, U_DEPT), fld(r, U_PHONE), fld(r, U_UEMAIL));
            else
                printf("  %-4d %-10s %-24.24s %-5s %-12.12s %s\n", k + 1, fld(r, U_ID), fld(r, U_NAME),
                       fld(r, U_DEPT), fld(r, U_PHONE), fld(r, U_UEMAIL));
        }
        if (q[0] && n == 1) {                       /* a search with a single hit: take it */
            strcpy(id_out, fld(t.row[idx[0]], U_ID));
            table_free(&t);
            return 1;
        }
        table_free(&t);
        char in[128];
        read_required(is_stu ? "Type the list number, or an ID / name / batch / section to search"
                             : "Type the list number, or an ID / name / department to search", in, sizeof in);
        int all_digits = 1;
        for (const char *p = in; *p; p++) if (!isdigit((unsigned char)*p)) all_digits = 0;
        if (all_digits && atoi(in) >= 1 && atoi(in) <= n) {
            table_load(user_file(role), &t);
            /* rebuild the same index list to map the number back to a row */
            int m = 0, found = -1;
            for (int i = 0; i < t.n; i++) {
                const char *r = t.row[i];
                if (!q[0] || contains_ci(fld(r, U_ID), q) || contains_ci(fld(r, U_NAME), q) ||
                    contains_ci(fld(r, U_DEPT), q) || contains_ci(fld(r, U_UEMAIL), q) ||
                    (is_stu && (strcmp(fld(r, U_BATCH), q) == 0 || str_ieq(fld(r, U_SECTION), q)))) {
                    if (++m == atoi(in)) { found = i; break; }
                }
            }
            if (found >= 0) strcpy(id_out, fld(t.row[found], U_ID));
            table_free(&t);
            return found >= 0;
        }
        snprintf(q, sizeof q, "%s", in);
    }
}
