#include "common.h"

/* ------------------------- row <-> User helpers -------------------- */

static void row_to_user(int role, const char *row, User *u)
{
    memset(u, 0, sizeof *u);
    u->role = role;
    strncpy(u->id, fld(row, U_ID), sizeof u->id - 1);
    strncpy(u->name, fld(row, U_NAME), sizeof u->name - 1);
    strncpy(u->dob, fld(row, U_DOB), sizeof u->dob - 1);
    strncpy(u->email, fld(row, U_EMAIL), sizeof u->email - 1);
    strncpy(u->phone, fld(row, U_PHONE), sizeof u->phone - 1);
    strncpy(u->dept, fld(row, U_DEPT), sizeof u->dept - 1);
    strncpy(u->uemail, fld(row, U_UEMAIL), sizeof u->uemail - 1);
    strncpy(u->hash, fld(row, U_PASS), sizeof u->hash - 1);
    strncpy(u->other, fld(row, U_OTHER), sizeof u->other - 1);
    strncpy(u->saved, fld(row, U_SAVED), sizeof u->saved - 1);
    strncpy(u->batch, fld(row, U_BATCH), sizeof u->batch - 1);
    strncpy(u->section, fld(row, U_SECTION), sizeof u->section - 1);
}

static void user_to_row(const User *u, char *out, size_t sz)
{
    join_fields(out, sz, U_COLS, u->id, u->name, u->dob, u->email, u->phone, u->dept, u->uemail,
                u->hash, u->other, u->saved, u->batch, u->section);
}

int user_find_by_id(int role, const char *id, User *u)
{
    Table t;
    table_load(user_file(role), &t);
    int i = table_find(&t, U_ID, id);
    if (i >= 0 && u) row_to_user(role, t.row[i], u);
    table_free(&t);
    return i >= 0;
}

static const char *domain_for(int role)
{
    return role == ROLE_STUDENT ? "student.uni.edu" : role == ROLE_TEACHER ? "teacher.uni.edu" : "admin.uni.edu";
}

/* Generates the university e-mail + password and stores the new account.
   The generated password is stored so the user can look at it again in their profile.
   Returns 0 if the ID already has an account. */
int user_create(int role, const User *base, char *uemail_out, char *plain_out)
{
    if (user_find_by_id(role, base->id, NULL)) return 0;
    User u = *base;
    u.role = role;
    char lid[64];
    strncpy(lid, u.id, sizeof lid - 1);
    lid[sizeof lid - 1] = 0;
    to_lower(lid);
    snprintf(u.uemail, sizeof u.uemail, "%s@%s", lid, domain_for(role));
    random_password(plain_out, 8);
    hash_password(plain_out, u.uemail, u.hash);
    snprintf(u.saved, sizeof u.saved, "%s", plain_out);
    if (role == ROLE_STUDENT) batch_of(u.id, u.batch);
    else { u.batch[0] = 0; u.section[0] = 0; }
    char row[LINE_LEN];
    user_to_row(&u, row, sizeof row);
    table_append_line(user_file(role), row);
    strcpy(uemail_out, u.uemail);
    return 1;
}

void show_credentials(int role, const char *uemail, const char *plain)
{
    printf("\n  GIVE %s:\n", role_label(role));
    printf("    %s EMAIL : %s\n", role_label(role), uemail);
    printf("    PASSWORD      : %s\n", plain);
    printf("\n  ==> TO LOG IN: choose '1. Login' and use exactly this email and password.\n");
    printf("  ==> Later you can see or change your password: log in -> Profile / Account.\n");
}

/* ------------------------------ sections --------------------------- */

int section_count(const char *section)
{
    return count_rows(F_STUDENTS, U_SECTION, section);
}

/* Asks for a section such as 46N until it is valid and not full. A blank answer goes back. */
void read_section(char *out)
{
    char b[32];
    for (;;) {
        read_required("  Section (example 46N)", b, sizeof b);
        to_upper(b);
        if (!valid_section(b)) {
            printf("  Use batch number + section letter, like 46N.\n");
            continue;
        }
        int n = section_count(b);
        if (n >= SECTION_CAP) {
            printf("  Section %s is full (%d/%d students). Enter another section.\n", b, n, SECTION_CAP);
            continue;
        }
        strcpy(out, b);
        return;
    }
}

/* ------------------------------- login ----------------------------- */

static int verify_login(int role, const char *email, const char *pass, User *out)
{
    Table t;
    int ok = 0;
    table_load(user_file(role), &t);
    for (int i = 0; i < t.n && !ok; i++) {
        if (!str_ieq(fld(t.row[i], U_UEMAIL), email)) continue;
        char h[HASHLEN];
        hash_password(pass, fld(t.row[i], U_UEMAIL), h);
        if (strcmp(h, fld(t.row[i], U_PASS)) == 0) {
            row_to_user(role, t.row[i], out);
            ok = 1;
        }
    }
    table_free(&t);
    return ok;
}

int user_login(int role, User *out)
{
    char title[64];
    snprintf(title, sizeof title, "%s LOGIN", role_label(role));
    print_header(title);
    for (;;) {
        char em[128], pw[128];
        read_line("Enter email [blank or 0 = back]: ", em, sizeof em);
        if (!em[0] || strcmp(em, "0") == 0) return 0;
        read_line("Enter password: ", pw, sizeof pw);
        if (verify_login(role, em, pw, out)) {
            printf("\n  Login successful. Welcome, %s!\n", out->name);
            return 1;
        }
        printf("\n  LOGIN FAILED - wrong email or password.\n");
        audit("LOGIN_FAIL", "%s login failed for %s", role_label(role), em);
        printf("  Forgot your email or password? You can reset it with your university ID + phone number.\n");
        if (confirm("Reset password now using ID + phone")) {
            GUARD(reset_by_id_phone(role));
        }
        printf("\n");
    }
}

/* ----------------------------- registration ------------------------ */

static int verify_university_id(int role, const char *id)
{
    Table t;
    const char *letter = role == ROLE_STUDENT ? "S" : role == ROLE_TEACHER ? "T" : "A";
    table_load(F_UNIVIDS, &t);
    int ok = table_find2(&t, 0, letter, 1, id) >= 0;
    table_free(&t);
    return ok;
}

int user_register(int role, User *out)
{
    char title[64];
    User u;
    memset(&u, 0, sizeof u);
    u.role = role;
    snprintf(title, sizeof title, "%s REGISTRATION", role_label(role));
    print_header(title);
    printf("  (Leave a required field blank and press Enter to go back.)\n\n");

    printf("Enter %s details\n", role == ROLE_STUDENT ? "student" : role == ROLE_TEACHER ? "teacher" : "admin");
    read_required("  Full name", u.name, sizeof u.name);
    if (role == ROLE_STUDENT) {
        for (;;) {
            read_required("  Date of birth (YYYY-MM-DD)", u.dob, sizeof u.dob);
            if (valid_date(u.dob)) break;
            printf("  Invalid date format.\n");
        }
    }
    read_required(role == ROLE_ADMIN ? "  Official email" : "  Email", u.email, sizeof u.email);
    read_required("  Phone", u.phone, sizeof u.phone);
    if (!choose_department(u.dept)) { printf("  Registration cancelled.\n"); return 0; }
    if (role == ROLE_STUDENT) read_section(u.section);
    read_line("  Other info [optional - Enter to skip]: ", u.other, sizeof u.other);

    for (;;) {
        read_required(role == ROLE_STUDENT ? "\nEnter university ID (like 253-33-666)" : "\nEnter university ID",
                      u.id, sizeof u.id);
        to_upper(u.id);
        if (role != ROLE_STUDENT || valid_student_id(u.id)) break;
        printf("  Student ID must look like 253-33-666.\n");
    }
    printf(role == ROLE_ADMIN ? "Verifying admin authorization...\n" : "Verifying university ID...\n");

    if (!verify_university_id(role, u.id)) {
        printf("\n  INVALID - this ID is not in the university registry.\n"
               "  (The admin must add it first: Admin dashboard -> University ID registry.)\n"
               "  REGISTRATION REJECTED.\n");
        pause_enter();
        return 0;
    }
    if (role == ROLE_STUDENT && section_count(u.section) >= SECTION_CAP) {   /* filled up while typing */
        printf("\n  Section %s just became full. REGISTRATION REJECTED.\n", u.section);
        pause_enter();
        return 0;
    }
    char uemail[128], plain[16];
    if (!user_create(role, &u, uemail, plain)) {
        printf("\n  INVALID - this ID already has an account.\n  REGISTRATION REJECTED.\n");
        pause_enter();
        return 0;
    }
    printf("\n  VALID - registration successful.\n");
    audit("REGISTER", "%s %s registered", role_label(role), u.id);
    if (role == ROLE_STUDENT) {
        char bt[8];
        batch_of(u.id, bt);
        printf("  Batch %s, section %s.\n", bt, u.section);
    }
    show_credentials(role, uemail, plain);
    printf("\n  Registration is complete. Press Enter, then choose '1. Login' with the email and password above.\n");
    pause_enter();
    (void)out;
    return 1;
}

/* --------------------------- password changes ---------------------- */

/* keep = 1 -> also store the plain password so it can be shown in the profile. */
static void store_password(int role, const char *id, const char *uemail, const char *plain, int keep)
{
    Table t;
    table_load(user_file(role), &t);
    int i = table_find(&t, U_ID, id);
    if (i >= 0) {
        User u;
        char row[LINE_LEN];
        row_to_user(role, t.row[i], &u);
        hash_password(plain, uemail, u.hash);
        snprintf(u.saved, sizeof u.saved, "%s", keep ? plain : "");
        user_to_row(&u, row, sizeof row);
        table_set(&t, i, row);
        table_save(user_file(role), &t);
    }
    table_free(&t);
}

/* Asks for a new password twice and whether it may be kept for the profile page. */
static void ask_new_password(int role, const char *id, const char *uemail, char *keep_text)
{
    char n1[128], n2[128];
    for (;;) {
        read_required("New password (at least 6 characters)", n1, sizeof n1);
        if (strlen(n1) < 6) { printf("  Too short.\n"); continue; }
        read_required("Repeat new password", n2, sizeof n2);
        if (strcmp(n1, n2) != 0) { printf("  Passwords do not match. Try again.\n"); continue; }
        break;
    }
    int keep = confirm("Save this password so you can see it later in your profile");
    store_password(role, id, uemail, n1, keep);
    snprintf(keep_text, 64, "%s", keep ? n1 : "");
    printf("  Password changed. %s\n", keep ? "It is saved in your profile." : "It was NOT saved - only you know it now.");
}

static void change_password(User *u)
{
    char old[128], h[HASHLEN], keep[64];
    read_required("Current password", old, sizeof old);
    hash_password(old, u->uemail, h);
    if (strcmp(h, u->hash) != 0) { printf("  Wrong current password.\n"); return; }
    ask_new_password(u->role, u->id, u->uemail, keep);
    audit("PASSWORD_CHANGE", "own password changed (%s)", keep[0] ? "saved in profile" : "not saved");
    User fresh;
    if (user_find_by_id(u->role, u->id, &fresh)) *u = fresh;
}

/* ---------------------------- forgot password ---------------------- */

static void digits_only(const char *s, char *out, size_t sz)
{
    size_t n = 0;
    for (; *s && n + 1 < sz; s++) if (isdigit((unsigned char)*s)) out[n++] = *s;
    out[n] = 0;
}

/* Reset the password by proving who you are with the university ID and the phone number on the account. */
void reset_by_id_phone(int role)
{
    char title[64];
    snprintf(title, sizeof title, "%s - RESET PASSWORD (ID + PHONE)", role_label(role));
    print_header(title);
    printf("  Enter your university ID and the phone number you registered with.\n\n");
    for (int tries = 3; tries > 0; tries--) {
        char id[64], ph[64], p1[40], p2[40];
        User u;
        read_required("University ID", id, sizeof id);
        to_upper(id);
        read_required("Registered phone number", ph, sizeof ph);
        int ok = user_find_by_id(role, id, &u);
        if (ok) {
            digits_only(ph, p1, sizeof p1);
            digits_only(u.phone, p2, sizeof p2);
            ok = p1[0] && strcmp(p1, p2) == 0;
        }
        if (!ok) {
            printf("\n  ID and phone number do not match any %s account. %d attempt(s) left.\n\n", role_label(role), tries - 1);
            audit("RESET_FAIL", "%s reset by ID+phone failed for %s", role_label(role), id);
            continue;
        }
        printf("\n  Verified: %s (%s). Choose your new password.\n", u.name, u.id);
        char keep[64];
        ask_new_password(role, u.id, u.uemail, keep);
        audit("PASSWORD_RESET", "%s reset the password with ID + phone", u.uemail);
        printf("  You can log in now with %s\n", u.uemail);
        pause_enter();
        return;
    }
    printf("  Too many wrong attempts. Please try again later or ask the admin.\n");
}

static void send_message(const char *to_email, const char *phone, const char *text)
{
    char stamp[32], line[LINE_LEN];
    now_string(stamp, sizeof stamp);
    join_fields(line, sizeof line, 4, stamp, to_email, phone, text);
    table_append_line(F_OUTBOX, line);
}

void forgot_password(int role)
{
    char title[64], em[128];
    snprintf(title, sizeof title, "%s - FORGOT PASSWORD", role_label(role));
    print_header(title);
    printf("  1. Get a 6-digit code on my registered email / phone\n  2. Use my university ID + registered phone number\n  0. Back\n");
    int way = read_choice("Choice: ", 0, 2);
    if (way == 0) return;
    if (way == 2) { reset_by_id_phone(role); return; }
    printf("\n  We will send a 6-digit code to your registered email / phone.\n");
    read_required("Enter your university email", em, sizeof em);

    Table t;
    User u;
    int found = 0;
    table_load(user_file(role), &t);
    for (int i = 0; i < t.n && !found; i++)
        if (str_ieq(fld(t.row[i], U_UEMAIL), em)) { row_to_user(role, t.row[i], &u); found = 1; }
    table_free(&t);
    if (!found) { printf("  No %s account uses that email.\n", role_label(role)); return; }

    char code[8], msg[200], masked[160];
    snprintf(code, sizeof code, "%06d", rand() % 1000000);
    snprintf(msg, sizeof msg, "Your password reset code is %s (valid for this session only). Do not share it.", code);
    send_message(u.email, u.phone, msg);
    mask_email(u.email, masked, sizeof masked);
    printf("\n  A reset code was sent to %s and your registered phone.\n", masked);
    printf("  (Demo mode: no real e-mail is sent. The message is saved in %s)\n\n", F_OUTBOX);

    int ok = 0;
    for (int tries = 3; tries > 0 && !ok; tries--) {
        char in[32];
        read_required("Enter the 6-digit code", in, sizeof in);
        if (strcmp(in, code) == 0) ok = 1;
        else printf("  Wrong code. %d attempt(s) left.\n", tries - 1);
    }
    if (!ok) { printf("  Too many wrong codes. Start again to get a new code.\n"); return; }

    printf("\n  Code accepted. Choose your new password.\n");
    char keep[64];
    ask_new_password(role, u.id, u.uemail, keep);
    audit("PASSWORD_RESET", "%s reset the password with an e-mailed code", u.uemail);
    printf("  You can log in now with %s\n", u.uemail);
    pause_enter();
}

/* ------------------------------ profile ---------------------------- */

void profile_menu(User *u)
{
    for (;;) {
        print_header("PROFILE / ACCOUNT");
        printf("  Role            : %s\n  University ID   : %s\n  Name            : %s\n", role_label(u->role), u->id, u->name);
        if (u->role == ROLE_STUDENT) {
            printf("  Date of birth   : %s\n  Batch           : %s\n  Section         : %s\n", u->dob, u->batch, u->section);
        }
        printf("  Email           : %s\n  Phone           : %s\n  Department      : %s\n", u->email, u->phone, u->dept);
        printf("  University email: %s   (use this to log in)\n", u->uemail);
        if (u->other[0]) printf("  Other info      : %s\n", u->other);
        printf("  Password        : ********\n");
        printf("\n  1. Show my password\n  2. Change password\n  0. Back\n");
        int c = read_choice("Choice: ", 0, 2);
        if (c == 0) return;
        if (c == 1) {
            audit("PASSWORD_VIEW", "own password viewed");
            if (u->saved[0]) printf("\n  Your password: %s\n", u->saved);
            else printf("\n  Your password is not saved here (you chose not to keep it).\n"
                        "  If you forgot it, log out and use 'Forgot password' on the login menu.\n");
            pause_enter();
        } else {
            GUARD(change_password(u));
        }
    }
}
