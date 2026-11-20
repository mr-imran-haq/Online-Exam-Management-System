#include "common.h"

/* ============================ score maths ========================== */

static double sum_marks(const char *path, const char *exam, const char *student, int *pending)
{
    Table t;
    double s = 0;
    table_load(path, &t);
    for (int i = 0; i < t.n; i++)
        if (strcmp(fld(t.row[i], A_EXAM), exam) == 0 && strcmp(fld(t.row[i], A_STUDENT), student) == 0) {
            double m = atof(fld(t.row[i], A_MARKS));
            if (m < 0) { if (pending) (*pending)++; }
            else s += m;
        }
    table_free(&t);
    return s;
}

void exam_score(const char *exam, const char *student, Score *s)
{
    memset(s, 0, sizeof *s);
    s->mcq = sum_marks(F_MCQ, exam, student, NULL);
    s->shortm = sum_marks(F_SHORT, exam, student, &s->pending);
    s->longm = sum_marks(F_LONG, exam, student, &s->pending);
    s->total = s->mcq + s->shortm + s->longm;
    s->attended = attempt_exists(exam, student);
}

double exam_declared_total(const char *exam)
{
    return atof(lookup_field(F_EXAMS, E_CODE, exam, E_TOTAL));
}

/* Grade scale: >=80 A+, 75 A, 70 A-, 65 B+, 60 B, 55 B-, 50 C+, 45 C, 40 D, else F */
const char *grade_for(double pct)
{
    if (pct >= 80) return "A+";
    if (pct >= 75) return "A";
    if (pct >= 70) return "A-";
    if (pct >= 65) return "B+";
    if (pct >= 60) return "B";
    if (pct >= 55) return "B-";
    if (pct >= 50) return "C+";
    if (pct >= 45) return "C";
    if (pct >= 40) return "D";
    return "F";
}

int is_pass(double pct) { return pct >= 40.0; }

/* ============================ result views ========================= */

void print_result_card(const char *exam, const char *student)
{
    Score s;
    char a[32], b[32], c[32], d[32], e[32];
    exam_score(exam, student, &s);
    double tot = exam_declared_total(exam);
    double pct = tot > 0 ? s.total / tot * 100.0 : 0.0;
    fmt_num(s.mcq, a, sizeof a);
    fmt_num(s.shortm, b, sizeof b);
    fmt_num(s.longm, c, sizeof c);
    fmt_num(s.total, d, sizeof d);
    fmt_num(tot, e, sizeof e);
    printf("  Exam        : %s (%s)\n", lookup_field(F_EXAMS, E_CODE, exam, E_NAME), exam);
    printf("  MCQ marks   : %s\n  Short marks : %s\n  Long marks  : %s\n", a, b, c);
    printf("  Total       : %s / %s\n", d, e);
    printf("  Percentage  : %.2f%%\n", pct);
    printf("  Grade       : %s\n", grade_for(pct));
    printf("  Result      : %s\n", is_pass(pct) ? "PASS" : "FAIL");
}

static void exam_table(const char *exam)
{
    char ids[MAX_LIST][64];
    int n = attended_list(exam, ids, MAX_LIST);
    double tot = exam_declared_total(exam);
    printf("  %-4s %-12s %-20s %-5s %7s %7s %7s %8s %7s %-5s %s\n", "No.", "STUDENT ID", "NAME", "SEC",
           "MCQ", "SHORT", "LONG", "TOTAL", "%", "GRADE", "RESULT");
    for (int i = 0; i < n; i++) {
        Score s;
        exam_score(exam, ids[i], &s);
        double pct = tot > 0 ? s.total / tot * 100.0 : 0.0;
        printf("  %-4d %-12s %-20.20s %-5s %7.2f %7.2f %7.2f %8.2f %6.1f%% %-5s %s\n", i + 1, ids[i],
               user_name(ROLE_STUDENT, ids[i]), lookup_field(F_STUDENTS, U_ID, ids[i], U_SECTION),
               s.mcq, s.shortm, s.longm, s.total, pct, grade_for(pct), is_pass(pct) ? "PASS" : "FAIL");
    }
    if (n == 0) printf("  (no student attended this exam)\n");
}

void teacher_final_list(const char *exam)
{
    printf("\n  FINAL RESULT LIST - %s (%s)\n", lookup_field(F_EXAMS, E_CODE, exam, E_NAME), exam);
    print_rule();
    exam_table(exam);
}

/* Marks of one student in one course, over all PUBLISHED exams. Absent = 0. */
static void course_totals(const char *course, const char *student, double *obt, double *poss)
{
    Table t;
    *obt = 0;
    *poss = 0;
    table_load(F_EXAMS, &t);
    for (int i = 0; i < t.n; i++) {
        const char *r = t.row[i];
        if (strcmp(fld(r, E_COURSE), course) != 0 || fld(r, E_PUB)[0] != '1') continue;
        char code[64];
        strncpy(code, fld(r, E_CODE), 63);
        code[63] = 0;
        Score s;
        exam_score(code, student, &s);
        *poss += atof(fld(r, E_TOTAL));
        *obt += s.total;
    }
    table_free(&t);
}

void student_wise_results(const char *student)
{
    Table t;
    double all_obt = 0, all_poss = 0;
    int shown = 0;
    printf("\n  RESULTS OF %s (%s)\n", user_name(ROLE_STUDENT, student), student);
    print_rule();
    printf("  %-8s %-10s %-26s %14s %7s %-5s %s\n", "COURSE", "EXAM", "NAME", "MARKS", "%", "GRADE", "RESULT");
    table_load(F_EXAMS, &t);
    for (int i = 0; i < t.n; i++) {
        const char *r = t.row[i];
        if (fld(r, E_PUB)[0] != '1') continue;
        char code[64], course[64];
        strncpy(code, fld(r, E_CODE), 63); code[63] = 0;
        strncpy(course, fld(r, E_COURSE), 63); course[63] = 0;
        if (!is_enrolled(course, student) && !attempt_exists(code, student)) continue;
        Score s;
        exam_score(code, student, &s);
        double tot = atof(fld(r, E_TOTAL));
        double pct = tot > 0 ? s.total / tot * 100.0 : 0.0;
        char marks[40];
        snprintf(marks, sizeof marks, "%.2f / %.2f", s.total, tot);
        printf("  %-8s %-10s %-26.26s %14s %6.1f%% %-5s %s%s\n", course, code, fld(r, E_NAME), marks, pct,
               grade_for(pct), is_pass(pct) ? "PASS" : "FAIL", s.attended ? "" : " (absent)");
        all_obt += s.total;
        all_poss += tot;
        shown++;
    }
    table_free(&t);
    if (!shown) { printf("  No published results yet.\n"); return; }
    double pct = all_poss > 0 ? all_obt / all_poss * 100.0 : 0.0;
    print_rule();
    printf("  OVERALL: %.2f / %.2f  (%.2f%%)  Grade %s  %s\n", all_obt, all_poss, pct, grade_for(pct),
           is_pass(pct) ? "PASS" : "FAIL");
}

/* ======================= admin: result management ================== */

typedef struct {
    char id[64], name[128], dept[32];
    double obt, poss, pct;
} FinalRow;

static int cmp_final(const void *a, const void *b)
{
    double x = ((const FinalRow *)a)->pct, y = ((const FinalRow *)b)->pct;
    return (x < y) - (x > y);
}

/* Builds the overall result of every student that has at least one published exam. */
static int build_final(FinalRow *rows, int max)
{
    Table st, en, co;
    int n = 0;
    table_load(F_STUDENTS, &st);
    table_load(F_ENROLL, &en);
    table_load(F_COURSES, &co);
    for (int i = 0; i < st.n && n < max; i++) {
        char sid[64];
        strncpy(sid, fld(st.row[i], U_ID), sizeof sid - 1);
        sid[sizeof sid - 1] = 0;
        double obt = 0, poss = 0;
        for (int k = 0; k < en.n; k++) {
            if (strcmp(fld(en.row[k], EN_STUDENT), sid) != 0) continue;
            double o, p;
            char ccode[64];
            strncpy(ccode, fld(en.row[k], EN_COURSE), sizeof ccode - 1);
            ccode[sizeof ccode - 1] = 0;
            course_totals(ccode, sid, &o, &p);
            obt += o;
            poss += p;
        }
        if (poss <= 0) continue;
        FinalRow *r = &rows[n++];
        strncpy(r->id, sid, 63); r->id[63] = 0;
        strncpy(r->name, fld(st.row[i], U_NAME), 127); r->name[127] = 0;
        strncpy(r->dept, fld(st.row[i], U_DEPT), 31); r->dept[31] = 0;
        r->obt = obt;
        r->poss = poss;
        r->pct = obt / poss * 100.0;
    }
    table_free(&st);
    table_free(&en);
    table_free(&co);
    qsort(rows, (size_t)n, sizeof(FinalRow), cmp_final);
    return n;
}

static void process_results(void)
{
    static FinalRow rows[MAX_LIST];
    int n = build_final(rows, MAX_LIST);
    FILE *f = fopen(F_FINAL, "w");
    if (!f) { printf("  [error] cannot write %s\n", F_FINAL); return; }
    for (int i = 0; i < n; i++)
        fprintf(f, "%d|%s|%s|%s|%.2f|%.2f|%.2f|%s|%s\n", i + 1, rows[i].id, rows[i].name, rows[i].dept,
                rows[i].obt, rows[i].poss, rows[i].pct, grade_for(rows[i].pct),
                is_pass(rows[i].pct) ? "PASS" : "FAIL");
    fclose(f);
    integrity_update(F_FINAL);
    audit("RESULT_PROCESS", "final ranking built for %d student(s)", n);
    printf("  Processed results of %d student(s) and saved them to %s\n", n, F_FINAL);
}

static void view_final(void)
{
    Table t;
    table_load(F_FINAL, &t);
    if (t.n == 0) {
        printf("  No processed results yet. Use 'Process results' first.\n");
        table_free(&t);
        return;
    }
    printf("\n  FINAL RESULTS (ranked)\n");
    print_rule();
    printf("  %-5s %-12s %-22s %-6s %9s %9s %7s %-5s %s\n", "RANK", "STUDENT ID", "NAME", "DEPT",
           "OBTAINED", "OUT OF", "%", "GRADE", "RESULT");
    for (int i = 0; i < t.n; i++) {
        const char *r = t.row[i];
        printf("  %-5s %-12s %-22.22s %-6s %9s %9s %6s%% %-5s %s\n", fld(r, 0), fld(r, 1), fld(r, 2),
               fld(r, 3), fld(r, 4), fld(r, 5), fld(r, 6), fld(r, 7), fld(r, 8));
    }
    table_free(&t);
}

static void course_wise(void)
{
    char course[64];
    printf("\n");
    if (!select_course_any(course)) return;
    Table t;
    int any = 0;
    table_load(F_EXAMS, &t);
    for (int i = 0; i < t.n; i++) {
        const char *r = t.row[i];
        if (strcmp(fld(r, E_COURSE), course) != 0 || fld(r, E_PUB)[0] != '1') continue;
        char code[64];
        strncpy(code, fld(r, E_CODE), 63);
        code[63] = 0;
        printf("\n  EXAM %s - %s (total %s)\n", code, fld(r, E_NAME), fld(r, E_TOTAL));
        exam_table(code);
        any = 1;
    }
    table_free(&t);
    if (!any) printf("  No published exam results for %s.\n", course);
}

static void semester_results(void)
{
    Table st, en;
    int any = 0;
    table_load(F_STUDENTS, &st);
    table_load(F_ENROLL, &en);
    printf("\n  SEMESTER RESULTS (per student, per course; published exams only)\n");
    print_rule();
    printf("  %-12s %-20s %-9s %9s %9s %7s %-5s\n", "STUDENT ID", "NAME", "COURSE", "OBTAINED", "OUT OF", "%", "GRADE");
    for (int i = 0; i < st.n; i++) {
        char sid[64];
        strncpy(sid, fld(st.row[i], U_ID), sizeof sid - 1);
        sid[sizeof sid - 1] = 0;
        for (int k = 0; k < en.n; k++) {
            if (strcmp(fld(en.row[k], EN_STUDENT), sid) != 0) continue;
            double o, p;
            char course[64];
            strncpy(course, fld(en.row[k], EN_COURSE), sizeof course - 1);
            course[sizeof course - 1] = 0;
            course_totals(course, sid, &o, &p);
            if (p <= 0) continue;
            double pct = o / p * 100.0;
            printf("  %-12s %-20.20s %-9s %9.2f %9.2f %6.1f%% %-5s\n", sid, fld(st.row[i], U_NAME), course,
                   o, p, pct, grade_for(pct));
            any = 1;
        }
    }
    if (!any) printf("  No published results yet.\n");
    table_free(&st);
    table_free(&en);
}

static int pick_student(char *id_out)
{
    return pick_user_list(ROLE_STUDENT, id_out);
}

void admin_result_management(void)
{
    for (;;) {
        print_header("RESULT MANAGEMENT");
        printf("  1. View results\n  2. Process results (build final ranking)\n  3. Search student result\n  0. Back\n");
        int c = read_choice("Choice: ", 0, 3);
        if (c == 0) return;
        if (c == 2) { process_results(); pause_enter(); continue; }
        if (c == 3) {
            GUARD(
                char id[64];
                if (pick_student(id)) student_wise_results(id);
                pause_enter()
            );
            continue;
        }
        for (;;) {
            print_header("VIEW RESULTS");
            printf("  1. Course-wise results\n  2. Student-wise results\n  3. Semester results\n  4. Final results\n  0. Back\n");
            int v = read_choice("Choice: ", 0, 4);
            if (v == 0) break;
            GUARD(
                if (v == 1) course_wise();
                else if (v == 2) {
                    char id[64];
                    if (pick_student(id)) student_wise_results(id);
                } else if (v == 3) semester_results();
                else view_final();
                pause_enter()
            );
        }
    }
}
