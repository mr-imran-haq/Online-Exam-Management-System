#include "common.h"

/* ------------------------------ helpers ---------------------------- */

static void read_date(const char *prompt, char *out, size_t sz)
{
    for (;;) {
        read_required(prompt, out, sz);
        if (valid_date(out)) return;
        printf("  Use the format YYYY-MM-DD (example 2026-12-05).\n");
    }
}

static void read_time(const char *prompt, char *out, size_t sz)
{
    for (;;) {
        read_required(prompt, out, sz);
        if (valid_time(out)) return;
        printf("  Use 24-hour format HH:MM (example 14:30).\n");
    }
}

static void read_abcd(const char *prompt, char *out)
{
    char b[16];
    for (;;) {
        read_required(prompt, b, sizeof b);
        char c = (char)toupper((unsigned char)b[0]);
        if (!b[1] && c >= 'A' && c <= 'D') { out[0] = c; out[1] = 0; return; }
        printf("  Please type A, B, C or D.\n");
    }
}

/* ----------------------------- add questions ----------------------- */

/* Asks for one question and saves it. A blank required answer cancels this one question (GUARD). */
static void add_one_question(const char *exam, char type)
{
    char text[1024], oa[256] = "", ob[256] = "", oc[256] = "", od[256] = "", correct[4] = "";
    char marks[32], num[16], row[LINE_LEN];

    if (type == 'M') printf("\nCREATE MCQ\n");
    printf("  (Leave a required field blank and press Enter to cancel this question.)\n");
    read_required("Enter question", text, sizeof text);
    if (type == 'M') {
        printf("Enter options:\n");
        read_required("  Option A", oa, sizeof oa);
        read_required("  Option B", ob, sizeof ob);
        read_required("  Option C", oc, sizeof oc);
        read_required("  Option D", od, sizeof od);
        read_abcd("Select correct answer (A-D)", correct);
    }
    double m = read_double("Enter question marks", 0.01, 1000);
    fmt_num(m, marks, sizeof marks);
    int qno = exam_question_count(exam) + 1;
    snprintf(num, sizeof num, "%d", qno);
    char typestr[2] = { type, 0 };
    join_fields(row, sizeof row, 10, exam, num, typestr, marks, text, oa, ob, oc, od, correct);
    table_append_line(F_QUESTIONS, row);
    printf("  Question %d saved%s.\n", qno, type == 'M' ? " together with its correct answer" : "");
}

/* Returns 1 when the exam has questions, 0 when the user gave up (the empty exam is deleted). */
static int add_questions(const char *exam, double *total)
{
    for (;;) {
        int qno = exam_question_count(exam);
        double sum = exam_question_marks(exam, 0);
        print_header("ADD QUESTIONS");
        printf("  Questions so far: %d    Marks so far: %.2f / %.2f\n", qno, sum, *total);
        printf("  Select question type:\n    1. Short question\n    2. Long question\n    3. MCQ\n    0. Done\n");
        int t = read_int("Choice: ", 0, 3);

        if (t == 0) {
            if (qno == 0) {
                if (confirm("No question added yet. Cancel and delete this exam")) {
                    delete_exam_cascade(exam);
                    return 0;
                }
                continue;
            }
            if (sum > *total + 1e-9 || sum < *total - 1e-9) {
                printf("  The questions add up to %.2f marks but the exam total is %.2f.\n", sum, *total);
                printf("    1. Add / change questions\n    2. Set exam total marks = %.2f\n", sum);
                if (read_int("Choice: ", 1, 2) == 1) continue;
                *total = sum;
            }
            return 1;
        }
        GUARD(add_one_question(exam, t == 1 ? 'S' : t == 2 ? 'L' : 'M'));
    }
}

/* ----------------------------- create exam ------------------------- */

static void create_exam(const char *course)
{
    char code[64], name[128], sd[16], ed[16], st[8], et[8], row[LINE_LEN];
    print_header("CREATE EXAM");

    Table t;
    table_load(F_EXAMS, &t);
    if (t.n) {
        printf("  EXAM CODES ALREADY IN USE:\n");
        for (int i = 0; i < t.n; i++) printf("    %-10s %s\n", fld(t.row[i], E_CODE), fld(t.row[i], E_NAME));
        printf("\n");
    }
    table_free(&t);
    for (;;) {
        read_required("Exam code: ", code, sizeof code);
        to_upper(code);
        table_load(F_EXAMS, &t);
        int dup = table_find(&t, E_CODE, code) >= 0;
        table_free(&t);
        if (!dup) break;
        printf("  That exam code already exists.\n");
    }
    read_required("Exam name: ", name, sizeof name);
    double total = read_double("Total marks: ", 1, 10000);

    for (;;) {
        read_date("Start date (YYYY-MM-DD): ", sd, sizeof sd);
        read_date("End date   (YYYY-MM-DD): ", ed, sizeof ed);
        read_time("Start time (HH:MM): ", st, sizeof st);
        read_time("End time   (HH:MM): ", et, sizeof et);
        if (parse_datetime(ed, et) > parse_datetime(sd, st)) break;
        printf("  The end must be after the start. Please enter the dates and times again.\n");
    }
    int dur = read_number("Exam duration in minutes", 1, 600);
    int show = confirm("Result publish settings: show MCQ marks to students immediately after they submit");

    char tot[32], d[16];
    fmt_num(total, tot, sizeof tot);
    snprintf(d, sizeof d, "%d", dur);
    join_fields(row, sizeof row, 12, code, course, name, tot, sd, ed, st, et, d, show ? "1" : "0", "0", "0");
    table_append_line(F_EXAMS, row);
    printf("  Exam saved. Now add the questions.\n");

    if (!add_questions(code, &total)) {
        printf("  Exam was not created.\n");
        return;
    }

    /* total may have been adjusted while adding questions */
    table_load(F_EXAMS, &t);
    int i = table_find(&t, E_CODE, code);
    if (i >= 0) {
        fmt_num(total, tot, sizeof tot);
        join_fields(row, sizeof row, 12, code, course, name, tot, sd, ed, st, et, d, show ? "1" : "0", "0", "0");
        table_set(&t, i, row);
        table_save(F_EXAMS, &t);
    }
    table_free(&t);
    audit("EXAM_CREATE", "exam %s in course %s", code, course);
    printf("  Exam %s created successfully.\n", code);
}

/* ------------------------- search / edit / delete ------------------ */

static int search_exam(const char *course, char *code_out)
{
    char q[128] = "";
    for (;;) {
        Table t;
        int idx[MAX_LIST], n = 0;
        table_load(F_EXAMS, &t);
        for (int i = 0; i < t.n && n < MAX_LIST; i++)
            if (strcmp(fld(t.row[i], E_COURSE), course) == 0 &&
                (!q[0] || contains_ci(fld(t.row[i], E_NAME), q) || contains_ci(fld(t.row[i], E_CODE), q)))
                idx[n++] = i;
        if (n == 0) {
            printf("  %s\n", q[0] ? "NOTHING THERE - no exam matches. Showing all exams again." : "No exams in this course yet.");
            table_free(&t);
            if (!q[0]) return 0;
            q[0] = 0;
            continue;
        }
        printf("\n  %-4s %-10s %-28s %-11s %-6s %s\n", "No.", "CODE", "NAME", "DATE", "MINS", "TOTAL");
        for (int k = 0; k < n; k++) {
            const char *r = t.row[idx[k]];
            printf("  %-4d %-10s %-28.28s %-11s %-6s %s\n", k + 1, fld(r, E_CODE), fld(r, E_NAME), fld(r, E_SDATE),
                   fld(r, E_DUR), fld(r, E_TOTAL));
        }
        if (q[0] && n == 1) { strcpy(code_out, fld(t.row[idx[0]], E_CODE)); table_free(&t); return 1; }
        table_free(&t);
        char in[128];
        read_required("Type the list number, or an exam name / code to search", in, sizeof in);
        int all_digits = 1;
        for (const char *p = in; *p; p++) if (!isdigit((unsigned char)*p)) all_digits = 0;
        if (all_digits && atoi(in) >= 1 && atoi(in) <= n) {
            table_load(F_EXAMS, &t);
            int m = 0, ok = 0;
            for (int i = 0; i < t.n && !ok; i++)
                if (strcmp(fld(t.row[i], E_COURSE), course) == 0 &&
                    (!q[0] || contains_ci(fld(t.row[i], E_NAME), q) || contains_ci(fld(t.row[i], E_CODE), q)) &&
                    ++m == atoi(in)) { strcpy(code_out, fld(t.row[i], E_CODE)); ok = 1; }
            table_free(&t);
            return ok;
        }
        snprintf(q, sizeof q, "%s", in);
    }
}

static void edit_exam(const char *course)
{
    char code[64];
    print_header("EDIT EXAM");
    if (!search_exam(course, code)) return;

    Table t;
    table_load(F_EXAMS, &t);
    int i = table_find(&t, E_CODE, code);
    if (i < 0) { table_free(&t); return; }
    char f[12][256];
    for (int k = 0; k < 12; k++) { strncpy(f[k], fld(t.row[i], k), 255); f[k][255] = 0; }

    printf("\n");
    print_exam_info(t.row[i]);
    printf("\n  Select which one you want to change:\n");
    printf("    1. Exam name\n    2. Total marks\n    3. Start date\n    4. End date\n    5. Start time\n");
    printf("    6. End time\n    7. Duration\n    8. Show MCQ marks immediately (yes/no)\n    0. Cancel\n");
    int c = read_choice("Choice: ", 0, 8);
    char buf[256];
    switch (c) {
    case 0: table_free(&t); return;
    case 1: read_required("New exam name: ", f[E_NAME], 256); break;
    case 2: fmt_num(read_double("New total marks: ", 1, 10000), f[E_TOTAL], 256); break;
    case 3: read_date("New start date: ", f[E_SDATE], 256); break;
    case 4: read_date("New end date: ", f[E_EDATE], 256); break;
    case 5: read_time("New start time: ", f[E_STIME], 256); break;
    case 6: read_time("New end time: ", f[E_ETIME], 256); break;
    case 7: snprintf(buf, sizeof buf, "%d", read_number("New duration (minutes)", 1, 600)); strcpy(f[E_DUR], buf); break;
    case 8: strcpy(f[E_SHOWMCQ], confirm("Show MCQ marks immediately after submit") ? "1" : "0"); break;
    }
    if (parse_datetime(f[E_EDATE], f[E_ETIME]) <= parse_datetime(f[E_SDATE], f[E_STIME])) {
        printf("  Not saved: the exam end would be before its start.\n");
        table_free(&t);
        return;
    }
    char row[LINE_LEN];
    join_fields(row, sizeof row, 12, f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], f[10], f[11]);
    table_set(&t, i, row);
    table_save(F_EXAMS, &t);
    table_free(&t);
    audit("EXAM_EDIT", "exam %s changed", code);
    printf("  Exam updated and saved.\n");
    view_exam_history(course);
}

static void delete_exam(const char *course)
{
    char code[64];
    print_header("DELETE EXAM");
    if (!search_exam(course, code)) return;

    Table t;
    table_load(F_EXAMS, &t);
    int i = table_find(&t, E_CODE, code);
    if (i >= 0) {
        printf("\n  EXAM DETAILS & CONFIRMATION\n");
        print_exam_info(t.row[i]);
        printf("  Students who attended: %d\n", count_rows(F_ATTEMPTS, AT_EXAM, code));
    }
    table_free(&t);
    if (confirm("Delete this exam together with its questions and all student answers")) {
        delete_exam_cascade(code);
        audit("EXAM_DELETE", "exam %s deleted", code);
        printf("  Exam %s deleted.\n", code);
        view_exam_history(course);
    } else {
        printf("  Going back - nothing was deleted.\n");
    }
}

/* ------------------------------ public ----------------------------- */

void view_exam_history(const char *course)
{
    Table t;
    int n = 0;
    printf("\n  EXAM HISTORY - %s %s\n", course, course_title(course));
    print_rule();
    printf("  %-10s %-24s %-17s %-17s %4s %5s %-9s\n", "CODE", "NAME", "START", "END", "MINS", "TAKEN", "RESULT");
    table_load(F_EXAMS, &t);
    for (int i = 0; i < t.n; i++) {
        const char *r = t.row[i];
        if (strcmp(fld(r, E_COURSE), course) != 0) continue;
        char s[24], e[24];
        snprintf(s, sizeof s, "%s %s", fld(r, E_SDATE), fld(r, E_STIME));
        snprintf(e, sizeof e, "%s %s", fld(r, E_EDATE), fld(r, E_ETIME));
        printf("  %-10s %-24.24s %-17s %-17s %4s %5d %-9s\n", fld(r, E_CODE), fld(r, E_NAME), s, e,
               fld(r, E_DUR), count_rows(F_ATTEMPTS, AT_EXAM, fld(r, E_CODE)),
               fld(r, E_PUB)[0] == '1' ? "published" : fld(r, E_FINAL)[0] == '1' ? "submitted" : "pending");
        n++;
    }
    if (!n) printf("  (no exams yet)\n");
    table_free(&t);
}

/* ------------------- interrupted attempts (teacher / admin) -------- */

static void purge_exam_student(const char *path, const char *exam, const char *student)
{
    Table t;
    table_load(path, &t);
    for (int i = t.n - 1; i >= 0; i--)
        if (strcmp(fld(t.row[i], A_EXAM), exam) == 0 && strcmp(fld(t.row[i], A_STUDENT), student) == 0)
            table_remove(&t, i);
    table_save(path, &t);
    table_free(&t);
}

/* Lists attempts that were STARTED but never submitted (program closed, power cut ...) and lets a
   teacher (only for one course) or the admin (course_or_null = NULL) give that student a fresh try. */
void reset_attempts_menu(const char *course_or_null)
{
    Table at;
    char exams[MAX_LIST][64], studs[MAX_LIST][64];
    int n = 0;
    print_header("RESET AN INTERRUPTED ATTEMPT");
    printf("  Only attempts that were started but never submitted can be reset.\n\n");
    printf("  %-4s %-10s %-14s %-22s %-17s %s\n", "No.", "EXAM", "STUDENT ID", "NAME", "STARTED", "STATE");
    table_load(F_ATTEMPTS, &at);
    for (int i = 0; i < at.n && n < MAX_LIST; i++) {
        if (strcmp(fld(at.row[i], AT_STATUS), "STARTED") != 0) continue;
        Exam ex;
        Attempt a;
        if (!exam_get(fld(at.row[i], AT_EXAM), &ex)) continue;
        if (course_or_null && strcmp(ex.course, course_or_null) != 0) continue;
        if (!attempt_get(ex.code, fld(at.row[i], AT_STUDENT), &a)) continue;
        char when[24];
        format_epoch(a.start, when, sizeof when);
        printf("  %-4d %-10s %-14s %-22.22s %-17s %s\n", n + 1, ex.code, a.student,
               user_name(ROLE_STUDENT, a.student), when, attempt_state(&ex, &a));
        snprintf(exams[n], sizeof exams[n], "%s", ex.code);
        snprintf(studs[n], sizeof studs[n], "%s", a.student);
        n++;
    }
    table_free(&at);
    if (!n) { printf("  (no interrupted attempts)\n"); pause_enter(); return; }

    int c = read_choice("Select number to reset (0 = back): ", 0, n);
    if (c == 0) return;
    Exam ex;
    exam_get(exams[c - 1], &ex);
    if (exam_window_state_of(&ex) > 0)
        printf("  Note: this exam window is already closed, so the student cannot start again\n"
               "  unless you first move the exam's end date/time (Edit exam).\n");
    if (!confirm("Reset this attempt so the student can start again")) { printf("  Nothing changed.\n"); return; }
    purge_exam_student(F_ATTEMPTS, exams[c - 1], studs[c - 1]);
    purge_exam_student(F_MCQ, exams[c - 1], studs[c - 1]);
    purge_exam_student(F_SHORT, exams[c - 1], studs[c - 1]);
    purge_exam_student(F_LONG, exams[c - 1], studs[c - 1]);
    purge_exam_student(F_ORDER, exams[c - 1], studs[c - 1]);
    audit("ATTEMPT_RESET", "exam %s student %s", exams[c - 1], studs[c - 1]);
    printf("  Attempt reset. %s can start exam %s again.\n", studs[c - 1], exams[c - 1]);
    pause_enter();
}

void exam_management(const char *course)
{
    for (;;) {
        print_header("EXAM MANAGEMENT");
        printf("  Course: %s - %s\n\n", course, course_title(course));
        printf("  1. Create exam\n  2. Edit exam\n  3. Delete exam\n  4. View exam history\n");
        printf("  5. Reset an interrupted attempt\n  0. Back\n");
        int c = read_choice("Choice: ", 0, 5);
        if (c == 0) return;
        GUARD(
            if (c == 1) create_exam(course);
            else if (c == 2) edit_exam(course);
            else if (c == 3) delete_exam(course);
            else if (c == 4) view_exam_history(course);
            else reset_attempts_menu(course)
        );
    }
}
