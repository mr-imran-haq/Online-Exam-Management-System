#include "common.h"

/* ------------------------------ helpers ---------------------------- */

static void attempt_time(const char *exam, const char *sid, char *out, size_t sz)
{
    Table t;
    out[0] = 0;
    table_load(F_ATTEMPTS, &t);
    int i = table_find2(&t, AT_EXAM, exam, AT_STUDENT, sid);
    if (i >= 0) format_epoch((time_t)atoll(fld(t.row[i], AT_START)), out, sz);
    table_free(&t);
}

/* Lists the courses the student has joined and lets them pick one. */
static int pick_joined_course(const User *s, char *course_out)
{
    Table en;
    char codes[MAX_LIST][64];
    int n = 0;
    table_load(F_ENROLL, &en);
    for (int i = 0; i < en.n && n < MAX_LIST; i++)
        if (strcmp(fld(en.row[i], EN_STUDENT), s->id) == 0) {
            strncpy(codes[n], fld(en.row[i], EN_COURSE), 63);
            codes[n][63] = 0;
            n++;
        }
    table_free(&en);
    if (n == 0) { printf("  You have not joined any course yet. Use 'Join new course' first.\n"); return 0; }
    printf("\n  YOUR JOINED COURSES\n  %-4s %-9s %-32s %s\n", "No.", "CODE", "TITLE", "TEACHER");
    for (int i = 0; i < n; i++) {
        const char *tid = lookup_field(F_COURSES, C_CODE, codes[i], C_TEACHER);
        printf("  %-4d %-9s %-32.32s %s\n", i + 1, codes[i], course_title(codes[i]),
               tid[0] ? user_name(ROLE_TEACHER, tid) : "(not assigned)");
    }
    int c = read_choice("Select course number (0 = back): ", 0, n);
    if (c > 0) strcpy(course_out, codes[c - 1]);
    return c > 0;
}

/* ----------------------------- join course ------------------------- */

static void join_course(const User *s)
{
    char code[64];
    print_header("JOIN NEW COURSE");
    read_required("Enter course access code: ", code, sizeof code);
    printf("Verifying course code...\n");

    Table co;
    table_load(F_COURSES, &co);
    int ci = -1;
    for (int i = 0; i < co.n; i++)
        if (fld(co.row[i], C_ACCESS)[0] && str_ieq(fld(co.row[i], C_ACCESS), code)) { ci = i; break; }
    if (ci < 0) {
        printf("\n  INVALID - ACCESS DENIED.\n");
        table_free(&co);
        printf("  Going back to the My Courses page...\n");
        return;
    }
    char course[64];
    strncpy(course, fld(co.row[ci], C_CODE), 63);
    course[63] = 0;
    char title[128];
    strncpy(title, fld(co.row[ci], C_TITLE), 127);
    title[127] = 0;
    table_free(&co);

    if (is_enrolled(course, s->id)) {
        printf("\n  You have already joined %s - %s.\n", course, title);
        return;
    }
    char row[256];
    join_fields(row, sizeof row, 2, course, s->id);
    table_append_line(F_ENROLL, row);
    printf("\n  VALID - %s - %s has been added to MY COURSES.\n", course, title);
}

/* ---------------------------- course exams ------------------------- */

static void course_exams(const User *s, const char *course)
{
    for (;;) {
        Table t;
        int idx[MAX_LIST], n = 0;
        print_header("AVAILABLE EXAMS");
        printf("  Course: %s - %s\n", course, course_title(course));
        table_load(F_EXAMS, &t);
        for (int i = 0; i < t.n && n < MAX_LIST; i++)
            if (strcmp(fld(t.row[i], E_COURSE), course) == 0) idx[n++] = i;
        if (n == 0) {
            printf("\n  NO EXAM AVAILABLE for this course.\n");
            table_free(&t);
            return;
        }
        printf("\n  %-4s %-10s %-24s %-17s %-17s %4s  %s\n", "No.", "CODE", "NAME", "OPENS", "CLOSES", "MINS", "STATUS");
        for (int k = 0; k < n; k++) {
            const char *r = t.row[idx[k]];
            char o[24], c[24];
            snprintf(o, sizeof o, "%s %s", fld(r, E_SDATE), fld(r, E_STIME));
            snprintf(c, sizeof c, "%s %s", fld(r, E_EDATE), fld(r, E_ETIME));
            int w = exam_window_state(r);
            Exam ex;
            Attempt at;
            const char *st = w == 0 ? "OPEN NOW" : w < 0 ? "upcoming" : "closed";
            if (exam_get(fld(r, E_CODE), &ex) && attempt_get(ex.code, s->id, &at)) st = attempt_state(&ex, &at);
            printf("  %-4d %-10s %-24.24s %-17s %-17s %4s  %s\n", k + 1, fld(r, E_CODE), fld(r, E_NAME), o, c,
                   fld(r, E_DUR), st);
        }
        int c = read_choice("Select exam number (0 = back): ", 0, n);
        if (c == 0) { table_free(&t); return; }
        char erow[LINE_LEN], code[64];
        strcpy(erow, t.row[idx[c - 1]]);
        strcpy(code, fld(erow, E_CODE));
        table_free(&t);

        printf("\n  EXAM INFORMATION\n");
        print_exam_info(erow);
        printf("  Questions        : %d\n", exam_question_count(code));
        printf("\n  1. Start exam\n  0. Exit (back to course page)\n");
        if (read_choice("Choice: ", 0, 1) == 0) return;

        Exam ex2;
        Attempt at2;
        if (exam_get(code, &ex2) && attempt_get(code, s->id, &at2)) {
            if (strcmp(at2.status, "STARTED") == 0)
                printf("\n  ATTEMPT FAILED - your previous attempt was started but never submitted (%s).\n"
                       "  Ask your teacher or the admin to reset it if you should get another try.\n",
                       attempt_state(&ex2, &at2));
            else
                printf("\n  ATTEMPT FAILED - you have already attended this exam. Only one attempt is allowed.\n");
            continue;
        }
        int w = exam_window_state(erow);
        if (w < 0) { printf("\n  This exam has not started yet (opens %s %s).\n", fld(erow, E_SDATE), fld(erow, E_STIME)); continue; }
        if (w > 0) { printf("\n  This exam is closed.\n"); continue; }
        if (exam_question_count(code) == 0) { printf("\n  This exam has no questions yet.\n"); continue; }
        take_exam(s, code);
    }
}

static void my_courses(const User *s)
{
    for (;;) {
        print_header("COURSE DASHBOARD");
        printf("  1. Join new course\n  2. My joined courses (select a course to see its exams)\n  0. Back\n");
        int c = read_choice("Choice: ", 0, 2);
        if (c == 0) return;
        if (c == 1) { GUARD(join_course(s)); continue; }
        char course[64];
        if (pick_joined_course(s, course)) course_exams(s, course);
    }
}

/* ----------------------------- exam history ------------------------ */

static void exam_history(const User *s)
{
    Table at;
    int n = 0;
    print_header("EXAM HISTORY");
    printf("  %-10s %-24s %-9s %-17s %-15s %s\n", "EXAM", "NAME", "COURSE", "STARTED", "STATUS", "MCQ MARKS");
    table_load(F_ATTEMPTS, &at);
    for (int i = 0; i < at.n; i++) {
        if (strcmp(fld(at.row[i], AT_STUDENT), s->id) != 0) continue;
        char code[64], when[24], mcq[32];
        strncpy(code, fld(at.row[i], AT_EXAM), sizeof code - 1);
        code[sizeof code - 1] = 0;
        format_epoch((time_t)atoll(fld(at.row[i], AT_START)), when, sizeof when);
        Score sc;
        exam_score(code, s->id, &sc);
        if (exam_has_type(code, 'M')) snprintf(mcq, sizeof mcq, "%.2f", sc.mcq); else strcpy(mcq, "-");
        printf("  %-10s %-24.24s %-9s %-17s %-15s %s\n", code, lookup_field(F_EXAMS, E_CODE, code, E_NAME),
               lookup_field(F_EXAMS, E_CODE, code, E_COURSE), when, fld(at.row[i], AT_STATUS), mcq);
        n++;
    }
    if (!n) printf("  (you have not attended any exam yet)\n");
    table_free(&at);
}

/* ------------------------------ my result -------------------------- */

static void each_attended_exam(const User *s, const char *course, void (*fn)(const User *, const char *))
{
    Table t;
    int any = 0;
    table_load(F_EXAMS, &t);
    for (int i = 0; i < t.n; i++) {
        if (strcmp(fld(t.row[i], E_COURSE), course) != 0) continue;
        char code[64];
        strncpy(code, fld(t.row[i], E_CODE), 63);
        code[63] = 0;
        if (!attempt_exists(code, s->id)) continue;
        fn(s, code);
        any = 1;
    }
    table_free(&t);
    if (!any) printf("  You have not attended any exam in this course.\n");
}

static void quiz_result_one(const User *s, const char *exam)
{
    if (!exam_has_type(exam, 'M')) return;
    char when[24];
    attempt_time(exam, s->id, when, sizeof when);
    printf("\n  QUIZ: %s (%s)    attended: %s\n", lookup_field(F_EXAMS, E_CODE, exam, E_NAME), exam, when);
    printf("  (Options were shuffled for you, so answers are shown as text, not as letters.)\n");
    printf("  %-4s %-30s %-30s %s\n", "Q.", "YOUR ANSWER", "CORRECT ANSWER", "MARKS");
    Table mc, qt;
    table_load(F_MCQ, &mc);
    table_load(F_QUESTIONS, &qt);
    double got = 0;
    for (int i = 0; i < qt.n; i++) {
        const char *q = qt.row[i];
        if (strcmp(fld(q, Q_EXAM), exam) != 0 || fld(q, Q_TYPE)[0] != 'M') continue;
        int ai = table_find3(&mc, A_EXAM, exam, A_STUDENT, s->id, A_QNO, fld(q, Q_NO));
        char ml = ai >= 0 ? fld(mc.row[ai], A_ANS)[0] : '-';       /* original letter the student chose */
        double m = ai >= 0 ? atof(fld(mc.row[ai], A_MARKS)) : 0;
        char mine_txt[64] = "(no answer)", cor_txt[64];
        if (ml >= 'A' && ml <= 'D') snprintf(mine_txt, sizeof mine_txt, "%.28s", fld(q, Q_A + (ml - 'A')));
        snprintf(cor_txt, sizeof cor_txt, "%.28s", fld(q, Q_A + (fld(q, Q_CORRECT)[0] - 'A')));
        got += m;
        printf("  %-4s %-30s %-30s %g / %s\n", fld(q, Q_NO), mine_txt, cor_txt, m, fld(q, Q_MARKS));
    }
    printf("  QUIZ TOTAL: %g / %g\n", got, exam_question_marks(exam, 'M'));
    table_free(&mc);
    table_free(&qt);
}

static void typed_result(const User *s, const char *exam, char type)
{
    if (!exam_has_type(exam, type)) return;
    char when[24];
    attempt_time(exam, s->id, when, sizeof when);
    printf("\n  %s QUESTION EXAM: %s (%s)    attended: %s\n", type == 'S' ? "SHORT" : "LONG",
           lookup_field(F_EXAMS, E_CODE, exam, E_NAME), exam, when);
    printf("  %-4s %-40s %s\n", "Q.", "QUESTION", "MARKS");
    Table an, qt;
    table_load(type == 'S' ? F_SHORT : F_LONG, &an);
    table_load(F_QUESTIONS, &qt);
    double got = 0;
    int unmarked = 0;
    for (int i = 0; i < qt.n; i++) {
        const char *q = qt.row[i];
        if (strcmp(fld(q, Q_EXAM), exam) != 0 || fld(q, Q_TYPE)[0] != type) continue;
        int ai = table_find3(&an, A_EXAM, exam, A_STUDENT, s->id, A_QNO, fld(q, Q_NO));
        double m = ai >= 0 ? atof(fld(an.row[ai], A_MARKS)) : 0;
        char ms[40];
        if (m < 0) { strcpy(ms, "not marked yet"); unmarked++; }
        else { snprintf(ms, sizeof ms, "%g / %s", m, fld(q, Q_MARKS)); got += m; }
        printf("  %-4s %-40.40s %s\n", fld(q, Q_NO), fld(q, Q_TEXT), ms);
    }
    printf("  TOTAL SO FAR: %g / %g%s\n", got, exam_question_marks(exam, type), unmarked ? "  (marking in progress)" : "");
    table_free(&an);
    table_free(&qt);
}

static void short_result_one(const User *s, const char *exam) { typed_result(s, exam, 'S'); }
static void long_result_one(const User *s, const char *exam) { typed_result(s, exam, 'L'); }

static void final_result_one(const User *s, const char *exam)
{
    char when[24];
    attempt_time(exam, s->id, when, sizeof when);
    int pub = lookup_field(F_EXAMS, E_CODE, exam, E_PUB)[0] == '1';
    printf("\n  %s (%s)    attended: %s    STATUS: %s\n", lookup_field(F_EXAMS, E_CODE, exam, E_NAME), exam, when,
           pub ? "PUBLISHED" : "PENDING");
    if (pub) print_result_card(exam, s->id);
    else printf("  RESULT NOT PUBLISHED yet.\n");
}

static void my_result(const User *s)
{
    char course[64];
    print_header("MY RESULT");
    if (!pick_joined_course(s, course)) return;
    for (;;) {
        printf("\n  Results of %s - %s\n", course, course_title(course));
        printf("  1. Quiz (MCQ) result\n  2. Short question result\n  3. Long question result\n");
        printf("  4. Final result status\n  0. Exit (back to select-course page)\n");
        int c = read_choice("Choice: ", 0, 4);
        if (c == 0) return;
        if (c == 1) each_attended_exam(s, course, quiz_result_one);
        else if (c == 2) each_attended_exam(s, course, short_result_one);
        else if (c == 3) each_attended_exam(s, course, long_result_one);
        else each_attended_exam(s, course, final_result_one);
    }
}

/* ----------------------------- dashboard --------------------------- */

void student_dashboard(User *u)
{
    for (;;) {
        print_header("STUDENT DASHBOARD");
        printf("  Welcome, %s (%s)\n\n", u->name, u->id);
        printf("  1. Profile / Account\n  2. My courses\n  3. Exam history\n  4. My result\n  0. Logout\n");
        int c = read_int("Choice: ", 0, 4);
        if (c == 0) { printf("  Logged out.\n"); return; }
        if (c == 1) profile_menu(u);
        else if (c == 2) my_courses(u);
        else if (c == 3) { exam_history(u); pause_enter(); }
        else my_result(u);
    }
}
