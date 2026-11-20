#include "common.h"

/* Opens the dashboard inside a session. Typing H at any menu jumps back to the home page, and a
   session that stays idle too long is closed (see session_touch in security.c). */
static void run_portal(User *u)
{
    session_start(u);
    for (;;) {
        cancel_top = NULL;
        home_active = 1;
        if (setjmp(home_jmp) == 0) {
            if (u->role == ROLE_STUDENT) student_dashboard(u);
            else if (u->role == ROLE_TEACHER) teacher_dashboard(u);
            else admin_dashboard(u);
            break;                      /* normal logout */
        }
        if (g_session.expired) break;   /* logged out because of inactivity */
        printf("\n  Back to the home page.\n");
    }
    session_end();
    home_active = 0;
    cancel_top = NULL;
}

static void role_menu(int role)
{
    for (;;) {
        User u;
        print_header(role_label(role));
        printf("  1. Login\n  2. Register\n  3. Forgot password\n  0. Back\n");
        int c = read_choice("Choice: ", 0, 3);
        if (c == 0) return;
        if (c == 1) {
            if (user_login(role, &u)) run_portal(&u);
        } else if (c == 2) {
            GUARD(user_register(role, &u));   /* after registering, the person logs in themselves */
        } else {
            GUARD(forgot_password(role));
        }
    }
}

int main(void)
{
    srand((unsigned)time(NULL));
    ensure_data();
    if (integrity_problems()) {         /* data files were edited outside the program */
        print_integrity_banner();
        audit("INTEGRITY_ALERT", "start-up check found modified data files");
    }
    for (;;) {
        print_header("UNIVERSITY EXAM MANAGEMENT SYSTEM");
        printf("  Select user role\n  1. Student\n  2. Teacher\n  3. Admin\n  0. Exit\n");
        int c = read_choice("Choice: ", 0, 3);
        if (c == 0) break;
        role_menu(c - 1);
    }
    printf("\nGoodbye!\n");
    return 0;
}
