#include "common.h"

/* Lists the courses the admin assigned to this teacher and lets them pick one. */
static int pick_my_course(const User *t, char *out)
{
    Table co;
    int idx[MAX_LIST], n = 0;
    table_load(F_COURSES, &co);
    for (int i = 0; i < co.n && n < MAX_LIST; i++)
        if (strcmp(fld(co.row[i], C_TEACHER), t->id) == 0) idx[n++] = i;
    if (n == 0) {
        printf("\n  No course has been assigned to you yet. Ask the admin to assign one.\n");
        table_free(&co);
        return 0;
    }
    printf("\n  COURSES ASSIGNED BY ADMIN\n  %-4s %-9s %-34s %-6s %s\n", "No.", "CODE", "TITLE", "DEPT", "STUDENTS");
    for (int k = 0; k < n; k++) {
        const char *r = co.row[idx[k]];
        printf("  %-4d %-9s %-34.34s %-6s %d\n", k + 1, fld(r, C_CODE), fld(r, C_TITLE), fld(r, C_DEPT),
               count_rows(F_ENROLL, EN_COURSE, fld(r, C_CODE)));
    }
    int c = read_choice("Select a course (0 = back): ", 0, n);
    if (c > 0) strcpy(out, fld(co.row[idx[c - 1]], C_CODE));
    table_free(&co);
    return c > 0;
}

/* ----------------------------- access code ------------------------- */

static int code_in_use(const char *code)
{
    Table co;
    int used = 0;
    table_load(F_COURSES, &co);
    for (int i = 0; i < co.n && !used; i++)
        if (fld(co.row[i], C_ACCESS)[0] && str_ieq(fld(co.row[i], C_ACCESS), code)) used = 1;
    table_free(&co);
    return used;
}

static void save_access_code(const char *course, const char *code)
{
    Table co;
    table_load(F_COURSES, &co);
    int i = table_find(&co, C_CODE, course);
    if (i >= 0) {
        char row[LINE_LEN];
        join_fields(row, sizeof row, 5, course, fld(co.row[i], C_TITLE), fld(co.row[i], C_DEPT),
                    fld(co.row[i], C_TEACHER), code);
        table_set(&co, i, row);
        table_save(F_COURSES, &co);
    }
    table_free(&co);
}

static void new_unique_code(char *out)
{
    do { random_code(out, 6); } while (code_in_use(out));
}

/* Asks for a custom code. A blank answer cancels (see GUARD). */
static void ask_custom_code(char *code)
{
    for (;;) {
        read_required("New access code (4-12 letters/digits, no spaces)", code, 64);
        to_upper(code);
        int ok = strlen(code) >= 4 && strlen(code) <= 12;
        for (char *p = code; *p && ok; p++) if (!isalnum((unsigned char)*p)) ok = 0;
        if (!ok) { printf("  Invalid format.\n"); continue; }
        if (code_in_use(code)) { printf("  That code is used by another course.\n"); continue; }
        return;
    }
}

static void manage_access_code(const char *course)
{
    for (;;) {
        char cur[64], code[64];
        volatile int got = 1;
        strncpy(cur, lookup_field(F_COURSES, C_CODE, course, C_ACCESS), 63);
        cur[63] = 0;
        print_header("MANAGE COURSE ACCESS CODE");
        printf("  Course: %s - %s\n  Current access code: %s\n\n", course, course_title(course),
               cur[0] ? cur : "(none yet)");
        printf("  1. Create\n  2. Regenerate\n  3. Edit / change (type your own)\n  0. Back\n");
        int c = read_choice("Choice: ", 0, 3);
        if (c == 0) return;
        if (c == 1) {
            if (cur[0]) { printf("  A code already exists. Use Regenerate or Edit.\n"); continue; }
            new_unique_code(code);
        } else if (c == 2) {
            new_unique_code(code);
        } else {
            got = 0;
            GUARD(ask_custom_code(code); got = 1);
            if (!got) continue;
        }
        save_access_code(course, code);
        audit("ACCESS_CODE", "course %s access code changed", course);
        printf("\n  Access code saved: %s\n  Give this code to your students so they can join the course.\n", code);
        printf("  (Old code, if any, no longer works.)\n");
    }
}

/* ----------------------------- student list ------------------------ */

static void student_list(const char *course)
{
    Table st, en;
    int joined = 0;
    const char *dept = lookup_field(F_COURSES, C_CODE, course, C_DEPT);
    char dcopy[32];
    strncpy(dcopy, dept, 31);
    dcopy[31] = 0;
    table_load(F_STUDENTS, &st);
    table_load(F_ENROLL, &en);
    print_header("STUDENT LIST");
    printf("  Course: %s - %s\n\n", course, course_title(course));
    printf("  %-13s %-24s %-6s %-6s %-6s %s\n", "STUDENT ID", "NAME", "BATCH", "SEC", "DEPT", "JOIN STATUS");
    for (int i = 0; i < st.n; i++) {
        char sid[64];
        strncpy(sid, fld(st.row[i], U_ID), sizeof sid - 1);
        sid[sizeof sid - 1] = 0;
        int j = table_find2(&en, EN_COURSE, course, EN_STUDENT, sid) >= 0;
        if (!j && strcmp(fld(st.row[i], U_DEPT), dcopy) != 0) continue;
        printf("  %-13s %-24.24s %-6s %-6s %-6s %s\n", sid, fld(st.row[i], U_NAME), fld(st.row[i], U_BATCH),
               fld(st.row[i], U_SECTION), fld(st.row[i], U_DEPT), j ? "Joined" : "Not joined");
        if (j) joined++;
    }
    printf("\n  TOTAL STUDENTS JOINED = %d\n", joined);
    table_free(&st);
    table_free(&en);
}

/* ------------------------------ menus ------------------------------ */

static void course_menu(const User *t, const char *course)
{
    (void)t;
    for (;;) {
        print_header("COURSE");
        printf("  %s - %s\n\n", course, course_title(course));
        printf("  1. Manage course access code\n  2. Student list\n  3. Exam management\n  4. Mark distribution\n  0. Back\n");
        int c = read_choice("Choice: ", 0, 4);
        if (c == 0) return;
        if (c == 1) manage_access_code(course);
        else if (c == 2) { student_list(course); pause_enter(); }
        else if (c == 3) exam_management(course);
        else mark_distribution(course);
    }
}

static void my_courses(const User *t)
{
    for (;;) {
        char course[64];
        print_header("MY COURSES");
        if (!pick_my_course(t, course)) return;
        course_menu(t, course);
    }
}

static void all_exam_history(const User *t)
{
    Table co;
    int any = 0;
    print_header("EXAM HISTORY (all my courses)");
    table_load(F_COURSES, &co);
    for (int i = 0; i < co.n; i++)
        if (strcmp(fld(co.row[i], C_TEACHER), t->id) == 0) {
            view_exam_history(fld(co.row[i], C_CODE));
            any = 1;
        }
    table_free(&co);
    if (!any) printf("  No course has been assigned to you yet.\n");
}

void teacher_dashboard(User *u)
{
    for (;;) {
        print_header("TEACHER DASHBOARD");
        printf("  Welcome, %s (%s)\n\n", u->name, u->id);
        printf("  1. Profile / Account\n  2. My courses\n  3. Exam history\n  0. Logout\n");
        int c = read_int("Choice: ", 0, 3);
        if (c == 0) { printf("  Logged out.\n"); return; }
        if (c == 1) profile_menu(u);
        else if (c == 2) my_courses(u);
        else { all_exam_history(u); pause_enter(); }
    }
}
