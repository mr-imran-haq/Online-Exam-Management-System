#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <setjmp.h>

/* ------------------------------------------------------------------ */
/*  Data files (all plain text, one record per line, '|' separated)    */
/* ------------------------------------------------------------------ */
#define DATA_DIR    "data"
#define F_UNIVIDS   DATA_DIR "/university_ids.txt"
#define F_STUDENTS  DATA_DIR "/students.txt"
#define F_TEACHERS  DATA_DIR "/teachers.txt"
#define F_ADMINS    DATA_DIR "/admins.txt"
#define F_DEPTS     DATA_DIR "/departments.txt"
#define F_COURSES   DATA_DIR "/courses.txt"
#define F_ENROLL    DATA_DIR "/enrollments.txt"
#define F_EXAMS     DATA_DIR "/exams.txt"
#define F_QUESTIONS DATA_DIR "/questions.txt"
#define F_ATTEMPTS  DATA_DIR "/attempts.txt"
#define F_MCQ       DATA_DIR "/mcq.txt"
#define F_SHORT     DATA_DIR "/short_ans.txt"
#define F_LONG      DATA_DIR "/long_ans.txt"
#define F_FINAL     DATA_DIR "/final_results.txt"

/* Column indexes for every file */
/* U_PASS = salted hash (always used to check a login).
   U_SAVED = the plain password, ONLY when the user chose to keep it viewable in their profile. */
enum { U_ID, U_NAME, U_DOB, U_EMAIL, U_PHONE, U_DEPT, U_UEMAIL, U_PASS, U_OTHER,
       U_SAVED, U_BATCH, U_SECTION, U_COLS };
enum { D_CODE, D_NAME };
enum { C_CODE, C_TITLE, C_DEPT, C_TEACHER, C_ACCESS };
enum { EN_COURSE, EN_STUDENT };
enum { E_CODE, E_COURSE, E_NAME, E_TOTAL, E_SDATE, E_EDATE, E_STIME, E_ETIME,
       E_DUR, E_SHOWMCQ, E_FINAL, E_PUB };
enum { Q_EXAM, Q_NO, Q_TYPE, Q_MARKS, Q_TEXT, Q_A, Q_B, Q_C, Q_D, Q_CORRECT };
enum { AT_EXAM, AT_STUDENT, AT_START, AT_STATUS, AT_END };
enum { A_EXAM, A_STUDENT, A_QNO, A_ANS, A_MARKS };   /* mcq / short / long answer rows */

#define F_OUTBOX    DATA_DIR "/outbox.txt"      /* simulated e-mail / SMS messages (not sealed) */
#define F_ORDER     DATA_DIR "/order.txt"       /* the random question/option order each student got */
#define F_AUDIT     DATA_DIR "/audit_log.txt"   /* who did what and when */
#define F_INTEG     DATA_DIR "/integrity.txt"   /* SHA-256 seal of every data file */
#define HASHLEN     80                          /* hex SHA-256 = 64 chars + '\0' */
#define IDLE_SECONDS 600                        /* automatic logout after 10 minutes without input */
#define SECTION_CAP 50                          /* max students in one section */
#define LINE_LEN 4096
#define MAX_Q    200
#define MAX_LIST 500
#define ANS_LEN  2048

enum { ROLE_STUDENT = 0, ROLE_TEACHER = 1, ROLE_ADMIN = 2 };

typedef struct {
    int  role;
    char id[64], name[128], dob[32], email[128], phone[32], dept[32];
    char uemail[128], hash[HASHLEN], other[256];
    char saved[64], batch[8], section[8];
} User;

/* One exam, one question and one attempt as C structures (loaded from the text files). */
typedef struct {
    char   code[64], course[64], name[128];
    double total;
    char   sdate[16], edate[16], stime[8], etime[8];
    int    duration;              /* minutes */
    int    show_mcq, finalized, published;
} Exam;

typedef struct {
    int    no;                    /* question number inside the exam */
    char   type;                  /* 'M' MCQ, 'S' short, 'L' long */
    double marks;
    char   text[1024];
    char   opt[4][256];           /* MCQ options A-D */
    char   correct;               /* 'A'-'D' for MCQ */
} Question;

typedef struct {
    char   exam[64], student[64];
    time_t start, end;
    char   status[24];            /* STARTED, SUBMITTED, AUTO_SUBMITTED */
} Attempt;

/* The logged-in session: who is in, since when, and the idle-timeout state. */
typedef struct {
    int    active;
    const User *user;
    time_t login_at, last_activity;
    int    idle_limit;            /* seconds; 0 = never */
    int    exam_mode;             /* 1 while an exam is running (the exam timer rules then) */
    int    expired;               /* set when the session timed out */
} Session;
extern Session g_session;

typedef struct {
    char (*row)[LINE_LEN];
    int n, cap;
} Table;

typedef struct {
    double mcq, shortm, longm, total;
    int pending, attended;
} Score;

/* util.c ------------------------------------------------------------ */

/* "Go back" support.
   - GUARD(stmt): runs stmt; if the user leaves a required field blank anywhere inside it,
     control returns here, stmt is abandoned and the menu continues.  Never put
     return / break / continue inside GUARD(...).
   - "Home": while a portal is open, typing H at a menu jumps back to the dashboard. */
extern jmp_buf *cancel_top;
extern jmp_buf  home_jmp;
extern int      home_active;
#define GUARD(...) do { \
        jmp_buf *g_prev_ = cancel_top; jmp_buf g_jb_; cancel_top = &g_jb_; \
        if (setjmp(g_jb_) == 0) { __VA_ARGS__; } \
        else { printf("\n  Cancelled - going back.\n"); } \
        cancel_top = g_prev_; } while (0)

void   trim(char *s);
void   read_line(const char *prompt, char *buf, size_t size);
void   read_required(const char *prompt, char *buf, size_t size);   /* blank = go back */
int    read_int(const char *prompt, int lo, int hi);                /* menu number only */
int    read_choice(const char *prompt, int lo, int hi);             /* menu number or H = Home */
int    read_number(const char *prompt, int lo, int hi);             /* data entry, blank = go back */
double read_double(const char *prompt, double lo, double hi);       /* data entry, blank = go back */
int    valid_student_id(const char *s);
void   batch_of(const char *student_id, char *out);
int    valid_section(const char *s);
void   mask_email(const char *e, char *out, size_t sz);
void   read_multiline(const char *prompt, char *buf, size_t size);
int    confirm(const char *prompt);
void   pause_enter(void);
void   print_header(const char *title);
void   print_rule(void);
void   print_unescaped(const char *s, const char *indent);
int    str_ieq(const char *a, const char *b);
int    contains_ci(const char *hay, const char *needle);
void   to_upper(char *s);
void   to_lower(char *s);
void   hash_password(const char *pass, const char *salt, char *out);
void   random_password(char *out, int len);
void   random_code(char *out, int len);
void   now_string(char *out, size_t sz);
int    valid_date(const char *s);
int    valid_time(const char *s);
time_t parse_datetime(const char *date, const char *tm);
void   format_epoch(time_t t, char *out, size_t sz);
void   fmt_num(double v, char *out, size_t sz);
const char *fld(const char *line, int idx);
void   join_fields(char *out, size_t size, int n, ...);
void   table_load(const char *path, Table *t);
void   table_free(Table *t);
int    table_save(const char *path, const Table *t);
void   table_add(Table *t, const char *line);
void   table_remove(Table *t, int idx);
void   table_set(Table *t, int idx, const char *line);
int    table_find(const Table *t, int col, const char *val);
int    table_find2(const Table *t, int c1, const char *v1, int c2, const char *v2);
int    table_find3(const Table *t, int c1, const char *v1, int c2, const char *v2,
                   int c3, const char *v3);
int    table_append_line(const char *path, const char *line);
const char *lookup_field(const char *path, int keycol, const char *key, int valcol);
int    purge_rows(const char *path, int col, const char *val);
int    count_rows(const char *path, int col, const char *val);
void   ensure_data(void);

/* security.c: SHA-256, integrity seal, audit log, session -------------- */
void   sha256_hex(const void *data, size_t len, char *out_hex);      /* out_hex: 65 bytes */
void   integrity_update(const char *path);                           /* re-seal one data file */
void   integrity_seal_all(void);
int    integrity_check(char bad[][96], int max);                     /* returns how many problems */
int    integrity_problems(void);                                     /* quick count */
void   audit(const char *action, const char *fmt, ...);              /* append to the activity log */
void   session_start(const User *u);
void   session_end(void);
void   session_touch(void);                                          /* called on every input line */
void   secure_menu(void);                                           /* admin: Security menu */
void   print_integrity_banner(void);

/* store.c ----------------------------------------------------------- */
int    exam_get(const char *code, Exam *e);
int    questions_load(const char *exam, Question *q, int max);
int    attempt_get(const char *exam, const char *student, Attempt *a);
int    exam_window_state_of(const Exam *e);                          /* -1 upcoming, 0 open, 1 closed */
const char *attempt_state(const Exam *e, const Attempt *a);          /* human text for lists */
void   reset_attempts_menu(const char *course_or_null);              /* interrupted attempts */
int    pick_user_list(int role, char *id_out);                        /* list everyone, pick / search */
const char *user_file(int role);
const char *role_label(int role);
void   list_departments(void);
int    dept_exists(const char *code);
int    choose_department(char *out);
const char *user_name(int role, const char *id);
const char *course_title(const char *code);
int    is_enrolled(const char *course, const char *student);
int    attempt_exists(const char *exam, const char *student);
int    select_course_any(char *out);
int    select_exam_in_course(const char *course, char *code_out);
int    attended_list(const char *exam, char ids[][64], int max);
int    exam_question_count(const char *exam);
double exam_question_marks(const char *exam, char type);   /* type 0 = all */
int    exam_has_type(const char *exam, char type);
time_t exam_start_epoch(const char *erow);
time_t exam_end_epoch(const char *erow);
int    exam_window_state(const char *erow);                /* -1 upcoming, 0 open, 1 closed */
void   print_exam_info(const char *erow);
void   delete_exam_cascade(const char *exam);
void   delete_course_cascade(const char *course);
void   delete_student_cascade(const char *id);

/* results.c --------------------------------------------------------- */
void   exam_score(const char *exam, const char *student, Score *s);
double exam_declared_total(const char *exam);
const char *grade_for(double pct);
int    is_pass(double pct);
void   print_result_card(const char *exam, const char *student);
void   teacher_final_list(const char *exam);
void   student_wise_results(const char *student);
void   admin_result_management(void);

/* auth.c ------------------------------------------------------------ */
int    user_find_by_id(int role, const char *id, User *u);
int    user_create(int role, const User *base, char *uemail_out, char *plain_out);
int    user_login(int role, User *out);
int    user_register(int role, User *out);
void   forgot_password(int role);
void   reset_by_id_phone(int role);                      /* forgot password: university ID + phone */
void   profile_menu(User *u);
void   show_credentials(int role, const char *uemail, const char *plain);
int    section_count(const char *section);
void   read_section(char *out);                         /* asks until valid and not full */

/* exam_take.c / exam_mgmt.c / marking.c ---------------------------- */
void   take_exam(const User *s, const char *exam);
void   exam_management(const char *course);
void   view_exam_history(const char *course);
void   mark_distribution(const char *course);

/* dashboards -------------------------------------------------------- */
void   student_dashboard(User *u);
void   teacher_dashboard(User *u);
void   admin_dashboard(User *u);

#endif
