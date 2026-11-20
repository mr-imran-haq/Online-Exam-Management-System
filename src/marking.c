#include "common.h"

typedef struct {
    char id[64];
    int pending;   /* short/long answers still waiting for a mark */
} Att;

/* Every student who attended `exam`, with the number of answers still unmarked. */
static int collect(const char *exam, Att *arr, int max)
{
    static char ids[MAX_LIST][64];
    Table sh, lg;
    int n = attended_list(exam, ids, max > MAX_LIST ? MAX_LIST : max);
    table_load(F_SHORT, &sh);
    table_load(F_LONG, &lg);
    for (int i = 0; i < n; i++) {
        strcpy(arr[i].id, ids[i]);
        arr[i].pending = 0;
        const Table *tabs[2] = { &sh, &lg };
        for (int k = 0; k < 2; k++)
            for (int r = 0; r < tabs[k]->n; r++) {
                const char *row = tabs[k]->row[r];
                if (strcmp(fld(row, A_EXAM), exam) == 0 && strcmp(fld(row, A_STUDENT), ids[i]) == 0 &&
                    atof(fld(row, A_MARKS)) < 0)
                    arr[i].pending++;
            }
    }
    table_free(&sh);
    table_free(&lg);
    return n;
}

static void set_exam_flag(const char *exam, int col, const char *val)
{
    Table t;
    table_load(F_EXAMS, &t);
    int i = table_find(&t, E_CODE, exam);
    if (i >= 0) {
        char f[12][256], row[LINE_LEN];
        for (int k = 0; k < 12; k++) { strncpy(f[k], fld(t.row[i], k), 255); f[k][255] = 0; }
        strncpy(f[col], val, 255);
        join_fields(row, sizeof row, 12, f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], f[10], f[11]);
        table_set(&t, i, row);
        table_save(F_EXAMS, &t);
    }
    table_free(&t);
}

/* Shows one question + the student's answer and stores the mark the teacher types. */
static void show_and_mark(const char *exam, const char *sid, const char *q)
{
    char type = fld(q, Q_TYPE)[0];
    const char *path = type == 'S' ? F_SHORT : F_LONG;
    char qno[16], maxm[32];
    strncpy(qno, fld(q, Q_NO), sizeof qno - 1); qno[sizeof qno - 1] = 0;
    strncpy(maxm, fld(q, Q_MARKS), sizeof maxm - 1); maxm[sizeof maxm - 1] = 0;

    Table at;
    table_load(path, &at);
    int ai = table_find3(&at, A_EXAM, exam, A_STUDENT, sid, A_QNO, qno);
    if (ai < 0) { printf("  No answer found.\n"); table_free(&at); return; }

    printf("\n");
    print_rule();
    printf("Question %s  [%s, max %s mark(s)]\n", qno, type == 'S' ? "Short" : "Long", maxm);
    print_unescaped(fld(q, Q_TEXT), "  ");
    printf("Answer of %s (%s):\n", sid, user_name(ROLE_STUDENT, sid));
    const char *ans = fld(at.row[ai], A_ANS);
    if (ans[0]) print_unescaped(ans, "  | "); else printf("  | (no answer)\n");
    double cur = atof(fld(at.row[ai], A_MARKS));
    if (cur >= 0) printf("Current mark: %g\n", cur);

    double m = read_double("Enter mark: ", 0, atof(maxm));
    char ms[32], row[LINE_LEN];
    fmt_num(m, ms, sizeof ms);
    join_fields(row, sizeof row, 5, exam, sid, qno, fld(at.row[ai], A_ANS), ms);
    table_set(&at, ai, row);
    table_save(path, &at);
    table_free(&at);
    audit("MARK_SET", "exam %s student %s question %s = %s", exam, sid, qno, ms);
    printf("  Mark stored.\n");
}

/* Indexes of the short/long questions of `exam` whose answer for `sid` still needs a mark. */
static int pending_questions(const Table *qt, const char *exam, const char *sid, int *out)
{
    Table sh, lg;
    int n = 0;
    table_load(F_SHORT, &sh);
    table_load(F_LONG, &lg);
    for (int i = 0; i < qt->n && n < MAX_Q; i++) {
        const char *q = qt->row[i];
        char type = fld(q, Q_TYPE)[0];
        if (strcmp(fld(q, Q_EXAM), exam) != 0 || type == 'M') continue;
        const Table *t = type == 'S' ? &sh : &lg;
        int ai = table_find3(t, A_EXAM, exam, A_STUDENT, sid, A_QNO, fld(q, Q_NO));
        if (ai >= 0 && atof(fld(t->row[ai], A_MARKS)) < 0) out[n++] = i;
    }
    table_free(&sh);
    table_free(&lg);
    return n;
}

/* Returns 1 if the teacher chose to leave the marking screens completely. */
static int mark_student(const char *exam, const char *sid)
{
    Table qt;
    int todo[MAX_Q];
    table_load(F_QUESTIONS, &qt);
    int n = pending_questions(&qt, exam, sid, todo);
    for (int k = 0; k < n; k++) {
        char q[LINE_LEN];
        volatile int cancelled = 1;
        strcpy(q, qt.row[todo[k]]);
        GUARD(show_and_mark(exam, sid, q); cancelled = 0);
        if (cancelled) { table_free(&qt); return 0; }          /* blank mark = back to the student list */
        if (k < n - 1 && confirm("Exit marking now")) { table_free(&qt); return 1; }
    }
    printf("\n  All answers of %s are marked - the student is removed from the review list.\n", sid);
    table_free(&qt);
    return 0;
}

static void review_answers(const char *exam)
{
    if (!exam_has_type(exam, 'S') && !exam_has_type(exam, 'L')) {
        printf("  This exam has only MCQ questions - nothing to review. (MCQs are marked automatically.)\n");
        return;
    }
    for (;;) {
        static Att a[MAX_LIST];
        int map[MAX_LIST], m = 0;
        int n = collect(exam, a, MAX_LIST);
        for (int i = 0; i < n; i++) if (a[i].pending > 0) map[m++] = i;
        if (m == 0) {
            printf("\n  Every attended student has been reviewed (%d attended).\n", n);
            return;
        }
        printf("\n  STUDENTS WAITING FOR REVIEW (short & long questions)\n");
        printf("  %-6s %-12s %-24s %s\n", "INDEX", "STUDENT ID", "NAME", "UNMARKED");
        for (int k = 0; k < m; k++)
            printf("  %-6d %-12s %-24.24s %d\n", k + 1, a[map[k]].id, user_name(ROLE_STUDENT, a[map[k]].id),
                   a[map[k]].pending);
        int idx = read_choice("Enter a student index (0 = back): ", 0, m);
        if (idx == 0) return;
        if (mark_student(exam, a[map[idx - 1]].id)) return;
    }
}

static void correct_marks(const char *exam)
{
    for (;;) {
        static Att a[MAX_LIST];
        int n = collect(exam, a, MAX_LIST);
        if (n == 0) { printf("  Nobody attended this exam.\n"); return; }
        printf("\n  STUDENTS WHO ATTENDED\n  %-6s %-12s %s\n", "INDEX", "STUDENT ID", "NAME");
        for (int i = 0; i < n; i++)
            printf("  %-6d %-12s %s\n", i + 1, a[i].id, user_name(ROLE_STUDENT, a[i].id));
        int idx = read_choice("Enter a student index (0 = done): ", 0, n);
        if (idx == 0) return;
        const char *sid = a[idx - 1].id;

        Table qt, sh, lg;
        table_load(F_QUESTIONS, &qt);
        table_load(F_SHORT, &sh);
        table_load(F_LONG, &lg);
        printf("\n  Short/long answers of %s:\n  %-4s %-6s %-8s %s\n", sid, "Q.", "TYPE", "MAX", "MARK");
        for (int i = 0; i < qt.n; i++) {
            const char *q = qt.row[i];
            char type = fld(q, Q_TYPE)[0];
            if (strcmp(fld(q, Q_EXAM), exam) != 0 || type == 'M') continue;
            const Table *t = type == 'S' ? &sh : &lg;
            int ai = table_find3(t, A_EXAM, exam, A_STUDENT, sid, A_QNO, fld(q, Q_NO));
            if (ai < 0) continue;
            double m = atof(fld(t->row[ai], A_MARKS));
            char ms[32];
            if (m < 0) strcpy(ms, "not marked"); else fmt_num(m, ms, sizeof ms);
            printf("  %-4s %-6s %-8s %s\n", fld(q, Q_NO), type == 'S' ? "Short" : "Long", fld(q, Q_MARKS), ms);
        }
        table_free(&sh);
        table_free(&lg);

        GUARD(
            char want[16];
            read_required("Enter the question number", want, sizeof want);
            int qi = table_find2(&qt, Q_EXAM, exam, Q_NO, want);
            if (qi < 0 || fld(qt.row[qi], Q_TYPE)[0] == 'M') {
                printf("  That is not a short/long question of this exam.\n");
            } else {
                char q[LINE_LEN];
                strcpy(q, qt.row[qi]);
                show_and_mark(exam, sid, q);
            }
        );
        table_free(&qt);
    }
}

static void mark_submission(const char *exam)
{
    char erow[LINE_LEN];
    Table t;
    table_load(F_EXAMS, &t);
    int ei = table_find(&t, E_CODE, exam);
    if (ei < 0) { table_free(&t); return; }
    strcpy(erow, t.row[ei]);
    table_free(&t);

    if (fld(erow, E_PUB)[0] == '1') {
        printf("  This result is already PUBLISHED.\n");
        teacher_final_list(exam);
        return;
    }
    if (integrity_problems()) {      /* data was edited by hand: do not let results be submitted on top of it */
        print_integrity_banner();
        audit("RESULT_BLOCKED", "exam %s: result submission refused, integrity problem", exam);
        return;
    }
    if ((exam_has_type(exam, 'S') || exam_has_type(exam, 'L')) && confirm("Did you find any marking mistake"))
        correct_marks(exam);

    static Att a[MAX_LIST];
    int n = collect(exam, a, MAX_LIST), pend = 0;
    for (int i = 0; i < n; i++) pend += a[i].pending;
    if (pend > 0) {
        printf("\n  %d answer(s) are still unmarked. Finish 'Review answers' first.\n", pend);
        return;
    }
    printf("\n  The system calculated the total marks of all %d student(s).\n", n);

    if (fld(erow, E_FINAL)[0] != '1') {
        if (!confirm("Make the final submission to admin")) { printf("  Not submitted.\n"); return; }
        set_exam_flag(exam, E_FINAL, "1");
        audit("RESULT_SUBMIT", "exam %s submitted to admin", exam);
        printf("  Final result submitted to admin.\n");
    }
    teacher_final_list(exam);
    if (confirm("\nPublish result (students and admin can then view it)")) {
        set_exam_flag(exam, E_PUB, "1");
        audit("RESULT_PUBLISH", "exam %s published", exam);
        printf("  Result PUBLISHED.\n");
    } else {
        printf("  Result kept PENDING. You can publish it later from this screen.\n");
    }
}

void mark_distribution(const char *course)
{
    for (;;) {
        print_header("MARK DISTRIBUTION");
        printf("  Course: %s - %s\n\n  1. Review answers\n  2. Mark submission\n  0. Back\n", course, course_title(course));
        int c = read_choice("Choice: ", 0, 2);
        if (c == 0) return;
        char exam[64];
        printf("\n");
        if (!select_exam_in_course(course, exam)) continue;
        if (c == 1) review_answers(exam);
        else mark_submission(exam);
    }
}
