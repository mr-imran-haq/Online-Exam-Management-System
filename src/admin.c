#include "common.h"

/* ============================ user helpers ========================= */

static int pick_user(int role, char *id_out)
{
    return pick_user_list(role, id_out);
}
/* Never prints the password: only the person themselves can see it (in their own profile). */
static void print_user_row(int role, const char *row)
{
    printf("  %s INFO\n", role_label(role));
    printf("    ID          : %s\n    Name        : %s\n", fld(row, U_ID), fld(row, U_NAME));
    if (role == ROLE_STUDENT)
        printf("    Date of birth: %s\n    Batch       : %s\n    Section     : %s\n", fld(row, U_DOB),
               fld(row, U_BATCH), fld(row, U_SECTION));
    printf("    Email       : %s\n    Phone       : %s\n    Department  : %s\n    University email: %s\n",
           fld(row, U_EMAIL), fld(row, U_PHONE), fld(row, U_DEPT), fld(row, U_UEMAIL));
    if (fld(row, U_OTHER)[0]) printf("    Other info  : %s\n", fld(row, U_OTHER));
}

static void add_user(int role)
{
    User u;
    char title[64];
    memset(&u, 0, sizeof u);
    snprintf(title, sizeof title, "ADD %s", role_label(role));
    print_header(title);
    printf("  (Leave a required field blank and press Enter to go back.)\n\n");
    read_required("  Full name", u.name, sizeof u.name);
    if (role == ROLE_STUDENT)
        for (;;) {
            read_required("  Date of birth (YYYY-MM-DD)", u.dob, sizeof u.dob);
            if (valid_date(u.dob)) break;
            printf("  Invalid date.\n");
        }
    read_required("  Email", u.email, sizeof u.email);
    read_required("  Phone", u.phone, sizeof u.phone);
    if (!choose_department(u.dept)) { printf("  Cancelled.\n"); return; }
    if (role == ROLE_STUDENT) read_section(u.section);
    read_line("  Other info [optional - Enter to skip]: ", u.other, sizeof u.other);
    for (;;) {
        read_required(role == ROLE_STUDENT ? "  University ID (like 253-33-666)" : "  University ID", u.id, sizeof u.id);
        to_upper(u.id);
        if (role != ROLE_STUDENT || valid_student_id(u.id)) break;
        printf("  Student ID must look like 253-33-666.\n");
    }
    char uemail[128], plain[16];
    if (!user_create(role, &u, uemail, plain)) {
        printf("  An account with this ID already exists.\n");
        return;
    }
    printf("\n  %s added.\n", role_label(role));
    audit("USER_ADD", "%s %s added by admin", role_label(role), u.id);
    show_credentials(role, uemail, plain);
}

static void edit_user(int role)
{
    char id[64];
    print_header(role == ROLE_STUDENT ? "EDIT STUDENT" : "EDIT TEACHER");
    if (!pick_user(role, id)) return;

    Table t;
    table_load(user_file(role), &t);
    int i = table_find(&t, U_ID, id);
    if (i < 0) { table_free(&t); return; }
    printf("\n");
    print_user_row(role, t.row[i]);

    char f[U_COLS][256];
    for (int k = 0; k < U_COLS; k++) { strncpy(f[k], fld(t.row[i], k), 255); f[k][255] = 0; }

    printf("\n  What do you want to change?\n    1. Name\n    2. Date of birth (students)\n    3. Email\n");
    printf("    4. Phone\n    5. Department\n    6. Other info\n    7. Section (students)\n    0. Cancel\n");
    int c = read_choice("Choice: ", 0, 7);
    if (c == 0) { table_free(&t); return; }
    if ((c == 2 || c == 7) && role != ROLE_STUDENT) {
        printf("  Only students have this.\n");
        table_free(&t);
        return;
    }
    switch (c) {
    case 1: read_required("New name", f[U_NAME], 256); break;
    case 2:
        for (;;) {
            read_required("New date of birth (YYYY-MM-DD)", f[U_DOB], 256);
            if (valid_date(f[U_DOB])) break;
            printf("  Invalid date.\n");
        }
        break;
    case 3: read_required("New email", f[U_EMAIL], 256); break;
    case 4: read_required("New phone", f[U_PHONE], 256); break;
    case 5: if (!choose_department(f[U_DEPT])) { table_free(&t); return; } break;
    case 6: read_line("New other info [Enter = clear]: ", f[U_OTHER], 256); break;
    case 7: read_section(f[U_SECTION]); break;
    }
    char row[LINE_LEN];
    join_fields(row, sizeof row, U_COLS, f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], f[10], f[11]);
    table_set(&t, i, row);
    table_save(user_file(role), &t);
    table_free(&t);
    printf("  Changes saved.\n");
    audit("USER_EDIT", "%s %s edited", role_label(role), id);
}

static void unassign_teacher_courses(const char *tid)
{
    Table co;
    table_load(F_COURSES, &co);
    for (int i = 0; i < co.n; i++)
        if (strcmp(fld(co.row[i], C_TEACHER), tid) == 0) {
            char row[LINE_LEN];
            join_fields(row, sizeof row, 5, fld(co.row[i], C_CODE), fld(co.row[i], C_TITLE),
                        fld(co.row[i], C_DEPT), "", fld(co.row[i], C_ACCESS));
            table_set(&co, i, row);
        }
    table_save(F_COURSES, &co);
    table_free(&co);
}

static void delete_user(int role)
{
    char id[64];
    print_header(role == ROLE_STUDENT ? "DELETE STUDENT" : "DELETE TEACHER");
    if (!pick_user(role, id)) return;
    printf("  You are about to delete %s (%s).\n", user_name(role, id), id);
    if (role == ROLE_STUDENT) printf("  Their enrollments, attempts and answers will be removed too.\n");
    else printf("  Their courses will become unassigned.\n");
    if (!confirm("Delete permanently")) { printf("  Cancelled.\n"); return; }
    if (role == ROLE_STUDENT) delete_student_cascade(id);
    else {
        unassign_teacher_courses(id);
        purge_rows(F_TEACHERS, U_ID, id);
    }
    printf("  Deleted.\n");
    audit("USER_DELETE", "%s %s deleted", role_label(role), id);
}

static void teacher_statistics(const char *tid)
{
    Table co, ex;
    int courses = 0, students = 0, exams = 0, finals = 0, pubs = 0, pending = 0;
    printf("\n  TEACHER STATISTICS - %s (%s)\n", user_name(ROLE_TEACHER, tid), tid);
    print_rule();
    table_load(F_COURSES, &co);
    table_load(F_EXAMS, &ex);
    for (int i = 0; i < co.n; i++) {
        if (strcmp(fld(co.row[i], C_TEACHER), tid) != 0) continue;
        char cc[64];
        strncpy(cc, fld(co.row[i], C_CODE), sizeof cc - 1);
        cc[sizeof cc - 1] = 0;
        int enrolled = count_rows(F_ENROLL, EN_COURSE, cc);
        int nex = 0;
        for (int k = 0; k < ex.n; k++) {
            if (strcmp(fld(ex.row[k], E_COURSE), cc) != 0) continue;
            nex++;
            if (fld(ex.row[k], E_FINAL)[0] == '1') finals++;
            if (fld(ex.row[k], E_PUB)[0] == '1') pubs++;
            char ec[64];
            strncpy(ec, fld(ex.row[k], E_CODE), sizeof ec - 1);
            ec[sizeof ec - 1] = 0;
            const char *paths[2] = { F_SHORT, F_LONG };
            for (int p = 0; p < 2; p++) {
                Table an;
                table_load(paths[p], &an);
                for (int r = 0; r < an.n; r++)
                    if (strcmp(fld(an.row[r], A_EXAM), ec) == 0 && atof(fld(an.row[r], A_MARKS)) < 0) pending++;
                table_free(&an);
            }
        }
        printf("  %-9s %-30.30s students: %-4d exams: %d\n", cc, fld(co.row[i], C_TITLE), enrolled, nex);
        courses++;
        students += enrolled;
        exams += nex;
    }
    table_free(&co);
    table_free(&ex);
    if (!courses) { printf("  No courses assigned.\n"); return; }
    print_rule();
    printf("  Courses assigned          : %d\n  Students enrolled (total) : %d\n  Exams created             : %d\n",
           courses, students, exams);
    printf("  Results submitted to admin: %d\n  Results published         : %d\n  Answers waiting for marks : %d\n",
           finals, pubs, pending);
}

static void search_view_user(int role)
{
    char id[64];
    print_header(role == ROLE_STUDENT ? "SEARCH / VIEW STUDENT" : "SEARCH / VIEW TEACHER");
    if (!pick_user(role, id)) return;
    Table t;
    table_load(user_file(role), &t);
    int i = table_find(&t, U_ID, id);
    if (i >= 0) { printf("\n"); print_user_row(role, t.row[i]); }
    table_free(&t);
    if (role == ROLE_STUDENT) {
        printf("\n  1. Student result\n  0. Back\n");
        if (read_choice("Choice: ", 0, 1) == 1) student_wise_results(id);
    } else {
        printf("\n  1. Teacher statistics\n  0. Back\n");
        if (read_choice("Choice: ", 0, 1) == 1) teacher_statistics(id);
    }
}

static void user_management(int role)
{
    for (;;) {
        print_header(role == ROLE_STUDENT ? "STUDENT MANAGEMENT" : "TEACHER MANAGEMENT");
        printf("  1. Add\n  2. Edit\n  3. Delete\n  4. Search / view\n  0. Back\n");
        int c = read_choice("Choice: ", 0, 4);
        if (c == 0) return;
        GUARD(
            if (c == 1) add_user(role);
            else if (c == 2) edit_user(role);
            else if (c == 3) delete_user(role);
            else search_view_user(role)
        );
    }
}

/* ======================== university ID registry =================== */

static int account_exists(char letter, const char *id)
{
    int role = letter == 'S' ? ROLE_STUDENT : letter == 'T' ? ROLE_TEACHER : ROLE_ADMIN;
    return user_find_by_id(role, id, NULL);
}

static void registry_add(void)
{
    printf("\n  Which kind of ID do you want to add?\n    1. Student   2. Teacher   3. Admin   0. Back\n");
    int t = read_choice("Choice: ", 0, 3);
    if (t == 0) return;
    char letter = "STA"[t - 1];
    char line[1024];
    if (letter == 'S') printf("  Student IDs look like 253-33-666.\n");
    read_required("Enter one or more IDs (separate with space or comma)", line, sizeof line);

    Table reg;
    int added = 0, dup = 0, bad = 0;
    char ls[2] = { letter, 0 };
    table_load(F_UNIVIDS, &reg);
    for (char *tok = strtok(line, " ,;"); tok; tok = strtok(NULL, " ,;")) {
        to_upper(tok);
        if (letter == 'S' && !valid_student_id(tok)) { printf("  '%s' is not a valid student ID - skipped.\n", tok); bad++; continue; }
        if (table_find2(&reg, 0, ls, 1, tok) >= 0) { dup++; continue; }
        char row[128];
        join_fields(row, sizeof row, 2, ls, tok);
        table_add(&reg, row);
        added++;
    }
    table_save(F_UNIVIDS, &reg);
    table_free(&reg);
    audit("REGISTRY_ADD", "%s IDs: %d added", ls, added);
    printf("  Added %d ID(s)%s%s.\n", added, dup ? ", already present: " : "", dup ? (dup > 1 ? "several" : "1") : "");
    if (bad) printf("  %d ID(s) had a wrong format.\n", bad);
}

static void registry_view(void)
{
    Table reg;
    table_load(F_UNIVIDS, &reg);
    printf("\n  UNIVERSITY ID REGISTRY (IDs that are allowed to register)\n");
    print_rule();
    printf("  %-8s %-16s %s\n", "TYPE", "ID", "STATUS");
    for (int i = 0; i < reg.n; i++) {
        char letter = fld(reg.row[i], 0)[0];
        char id[64];
        strncpy(id, fld(reg.row[i], 1), sizeof id - 1);
        id[sizeof id - 1] = 0;
        printf("  %-8s %-16s %s\n", letter == 'S' ? "Student" : letter == 'T' ? "Teacher" : "Admin", id,
               account_exists(letter, id) ? "registered" : "not registered yet");
    }
    if (!reg.n) printf("  (empty)\n");
    table_free(&reg);
}

static void registry_remove(void)
{
    char id[64];
    registry_view();
    read_required("ID to remove from the registry", id, sizeof id);
    to_upper(id);
    Table reg;
    table_load(F_UNIVIDS, &reg);
    int i = table_find(&reg, 1, id);
    if (i < 0) { printf("  That ID is not in the registry.\n"); table_free(&reg); return; }
    if (account_exists(fld(reg.row[i], 0)[0], id)) {
        printf("  That ID already has an account. Delete the account first (Student / Teacher management).\n");
    } else if (confirm("Remove this ID")) {
        table_remove(&reg, i);
        table_save(F_UNIVIDS, &reg);
        printf("  Removed.\n");
        audit("REGISTRY_REMOVE", "ID %s removed from the registry", id);
    }
    table_free(&reg);
}

static void registry_management(void)
{
    for (;;) {
        print_header("UNIVERSITY ID REGISTRY");
        printf("  Only IDs listed here can register as student / teacher / admin.\n\n");
        printf("  1. Add ID(s)\n  2. View all IDs\n  3. Remove an ID\n  0. Back\n");
        int c = read_choice("Choice: ", 0, 3);
        if (c == 0) return;
        GUARD(
            if (c == 1) registry_add();
            else if (c == 2) { registry_view(); pause_enter(); }
            else registry_remove()
        );
    }
}

/* ========================== departments ============================ */

static int dept_in_use(const char *code)
{
    return count_rows(F_COURSES, C_DEPT, code) + count_rows(F_STUDENTS, U_DEPT, code) +
           count_rows(F_TEACHERS, U_DEPT, code) + count_rows(F_ADMINS, U_DEPT, code);
}

static void department_action(int c)
{
    char code[64], name[128], row[LINE_LEN];
    Table t;
    if (c == 4) { printf("\n  DEPARTMENT LIST\n"); list_departments(); pause_enter(); return; }
    if (c == 1) {
        printf("\n  EXISTING DEPARTMENTS\n");
        list_departments();
        read_required("Department code (e.g. CSE)", code, sizeof code);
        to_upper(code);
        if (dept_exists(code)) { printf("  That code already exists.\n"); return; }
        read_required("Department name", name, sizeof name);
        join_fields(row, sizeof row, 2, code, name);
        table_append_line(F_DEPTS, row);
        printf("  Department added.\n");
        audit("DEPT_ADD", "department %s", code);
        return;
    }
    list_departments();
    read_required("Department code (0 = back)", code, sizeof code);
    if (strcmp(code, "0") == 0) return;
    to_upper(code);
    table_load(F_DEPTS, &t);
    int i = table_find(&t, D_CODE, code);
    if (i < 0) { printf("  No such department.\n"); table_free(&t); return; }
    if (c == 2) {
        table_free(&t);
        read_required("New department name", name, sizeof name);
        table_load(F_DEPTS, &t);
        i = table_find(&t, D_CODE, code);
        join_fields(row, sizeof row, 2, code, name);
        table_set(&t, i, row);
        table_save(F_DEPTS, &t);
        printf("  Department updated.\n");
        audit("DEPT_EDIT", "department %s", code);
    } else {
        int used = dept_in_use(code);
        if (used) printf("  Cannot delete: %d course/user record(s) still use this department.\n", used);
        else if (confirm("Delete this department")) {
            table_remove(&t, i);
            table_save(F_DEPTS, &t);
            printf("  Department deleted.\n");
            audit("DEPT_DELETE", "department %s", code);
        }
    }
    table_free(&t);
}

static void department_management(void)
{
    for (;;) {
        print_header("DEPARTMENT MANAGEMENT");
        printf("  1. Add\n  2. Edit\n  3. Delete\n  4. View department list\n  0. Back\n");
        int c = read_choice("Choice: ", 0, 4);
        if (c == 0) return;
        GUARD(department_action(c));
    }
}

/* ============================= courses ============================= */

static void list_courses(void)
{
    Table t;
    table_load(F_COURSES, &t);
    printf("\n  %-9s %-34s %-6s %-12s %-8s %s\n", "CODE", "TITLE", "DEPT", "TEACHER", "CODE", "STUDENTS");
    for (int i = 0; i < t.n; i++) {
        const char *tid = fld(t.row[i], C_TEACHER);
        printf("  %-9s %-34.34s %-6s %-12s %-8s %d\n", fld(t.row[i], C_CODE), fld(t.row[i], C_TITLE),
               fld(t.row[i], C_DEPT), tid[0] ? tid : "-", fld(t.row[i], C_ACCESS)[0] ? fld(t.row[i], C_ACCESS) : "-",
               count_rows(F_ENROLL, EN_COURSE, fld(t.row[i], C_CODE)));
    }
    if (!t.n) printf("  (no courses)\n");
    table_free(&t);
}

static void save_course(const char *code, const char *title, const char *dept, const char *teacher,
                        const char *access)
{
    Table t;
    char row[LINE_LEN];
    join_fields(row, sizeof row, 5, code, title, dept, teacher, access);
    table_load(F_COURSES, &t);
    int i = table_find(&t, C_CODE, code);
    if (i >= 0) { table_set(&t, i, row); table_save(F_COURSES, &t); }
    else table_append_line(F_COURSES, row);
    table_free(&t);
}

static void assign_course(void)
{
    char course[64];
    print_header("ASSIGN COURSE TO TEACHER");
    if (!select_course_any(course)) return;

    Table te;
    table_load(F_TEACHERS, &te);
    if (te.n == 0) { printf("  No teachers are registered yet.\n"); table_free(&te); return; }
    printf("\n  %-4s %-10s %-26s %s\n", "No.", "ID", "NAME", "DEPT");
    for (int i = 0; i < te.n; i++)
        printf("  %-4d %-10s %-26.26s %s\n", i + 1, fld(te.row[i], U_ID), fld(te.row[i], U_NAME), fld(te.row[i], U_DEPT));
    int c = read_choice("Select teacher number (0 = back): ", 0, te.n);
    if (c == 0) { table_free(&te); return; }
    char tid[64];
    strcpy(tid, fld(te.row[c - 1], U_ID));
    table_free(&te);

    Table co;
    table_load(F_COURSES, &co);
    int i = table_find(&co, C_CODE, course);
    char row[LINE_LEN];
    strcpy(row, co.row[i]);
    table_free(&co);
    if (fld(row, C_TEACHER)[0] && strcmp(fld(row, C_TEACHER), tid) != 0)
        printf("  Note: this course is currently assigned to %s and will be reassigned.\n", fld(row, C_TEACHER));
    if (!confirm("Save this assignment")) return;
    save_course(course, fld(row, C_TITLE), fld(row, C_DEPT), tid, fld(row, C_ACCESS));
    audit("COURSE_ASSIGN", "course %s assigned to teacher %s", course, tid);
    printf("  Assignment saved. %s's MY COURSES is updated.\n", user_name(ROLE_TEACHER, tid));
}

static void course_action(int c)
{
    if (c == 5) { list_courses(); pause_enter(); return; }
    if (c == 4) { assign_course(); return; }

    if (c == 1) {
        char code[64], title[128], dept[32];
        printf("\n  EXISTING COURSES");
        list_courses();
        read_required("Course code", code, sizeof code);
        to_upper(code);
        if (*lookup_field(F_COURSES, C_CODE, code, C_CODE)) { printf("  That course code already exists.\n"); return; }
        read_required("Course title", title, sizeof title);
        if (!choose_department(dept)) return;
        save_course(code, title, dept, "", "");
        printf("  Course added. Assign it to a teacher from this menu.\n");
        audit("COURSE_ADD", "course %s", code);
        return;
    }
    char course[64];
    if (!select_course_any(course)) return;
    if (c == 2) {
        char title[128], dept[32];
        Table co;
        table_load(F_COURSES, &co);
        char row[LINE_LEN];
        strcpy(row, co.row[table_find(&co, C_CODE, course)]);
        table_free(&co);
        printf("  1. Change title\n  2. Change department\n  0. Cancel\n");
        int e = read_choice("Choice: ", 0, 2);
        if (e == 0) return;
        strcpy(title, fld(row, C_TITLE));
        strcpy(dept, fld(row, C_DEPT));
        if (e == 1) read_required("New title", title, sizeof title);
        else if (!choose_department(dept)) return;
        save_course(course, title, dept, fld(row, C_TEACHER), fld(row, C_ACCESS));
        printf("  Course updated.\n");
        audit("COURSE_EDIT", "course %s", course);
    } else {
        printf("  Deleting %s removes its enrollments, exams, questions and all answers.\n", course);
        if (confirm("Delete this course permanently")) {
            delete_course_cascade(course);
            printf("  Course deleted.\n");
            audit("COURSE_DELETE", "course %s", course);
        }
    }
}

static void course_management(void)
{
    for (;;) {
        print_header("COURSE MANAGEMENT");
        printf("  1. Add course\n  2. Edit course\n  3. Delete course\n  4. Assign course to teacher\n");
        printf("  5. View all courses\n  0. Back\n");
        int c = read_choice("Choice: ", 0, 5);
        if (c == 0) return;
        GUARD(course_action(c));
    }
}

/* ============================= dashboard =========================== */

void admin_dashboard(User *u)
{
    for (;;) {
        print_header("ADMIN DASHBOARD");
        print_integrity_banner();
        printf("  Welcome, %s (%s)\n\n", u->name, u->id);
        printf("  1. Profile / Account\n  2. Student management\n  3. Teacher management\n");
        printf("  4. Department management\n  5. Course management\n  6. Result management\n");
        printf("  7. University ID registry\n  8. Security (integrity check, activity log, reset attempt)\n  0. Logout\n");
        int c = read_int("Choice: ", 0, 8);
        if (c == 0) { printf("  Logged out.\n"); return; }
        if (c == 1) profile_menu(u);
        else if (c == 2) user_management(ROLE_STUDENT);
        else if (c == 3) user_management(ROLE_TEACHER);
        else if (c == 4) department_management();
        else if (c == 5) course_management();
        else if (c == 6) admin_result_management();
        else if (c == 7) registry_management();
        else secure_menu();
    }
}
