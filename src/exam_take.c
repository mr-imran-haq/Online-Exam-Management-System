#include "common.h"

/* ---------------------------------------------------------------------
   Taking an exam.
   Questions are loaded into an array of Question structures. For every
   student the order of the questions - and the order of the A-D options of
   each MCQ - is shuffled (Fisher-Yates) and the result is saved in order.txt,
   so the order that student saw can always be reproduced.
   --------------------------------------------------------------------- */

static Question qs[MAX_Q];          /* questions in their original order */
static int      order[MAX_Q];       /* order[k] = index in qs[] of the k-th question shown */
static char     perm[MAX_Q][5];     /* perm[k][d] = original option letter shown at position d of question k */
static char     answers[MAX_Q][ANS_LEN];   /* answers[k] belongs to the k-th question shown */

static void shuffle_ints(int *a, int n)
{
    for (int i = n - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int t = a[i]; a[i] = a[j]; a[j] = t;
    }
}

static void make_order(int nq)
{
    for (int k = 0; k < nq; k++) order[k] = k;
    shuffle_ints(order, nq);
    for (int k = 0; k < nq; k++) {
        int p[4] = { 0, 1, 2, 3 };
        shuffle_ints(p, 4);
        for (int d = 0; d < 4; d++) perm[k][d] = (char)('A' + p[d]);
        perm[k][4] = 0;
    }
}

/* order.txt row:  exam | student | 3,1,2 | 3:BADC;1:CDAB   (question numbers, then qno:option-order) */
static void save_order(const char *exam, const char *student, int nq)
{
    char qlist[LINE_LEN] = "", olist[LINE_LEN] = "", row[LINE_LEN], tmp[96];
    for (int k = 0; k < nq; k++) {
        const Question *q = &qs[order[k]];
        snprintf(tmp, sizeof tmp, "%s%d", k ? "," : "", q->no);
        strncat(qlist, tmp, sizeof qlist - strlen(qlist) - 1);
        if (q->type == 'M') {
            snprintf(tmp, sizeof tmp, "%s%d:%.4s", olist[0] ? ";" : "", q->no, perm[k]);
            strncat(olist, tmp, sizeof olist - strlen(olist) - 1);
        }
    }
    join_fields(row, sizeof row, 4, exam, student, qlist, olist);
    table_append_line(F_ORDER, row);
}

static void show_remaining(time_t deadline)
{
    long rem = (long)(deadline - time(NULL));
    if (rem < 0) rem = 0;
    printf("Time left: %02ld:%02ld:%02ld\n", rem / 3600, (rem % 3600) / 60, rem % 60);
}

static void mark_attempt_end(const char *exam, const char *student, int auto_sub)
{
    Table t;
    char endbuf[32], row[LINE_LEN];
    table_load(F_ATTEMPTS, &t);
    int i = table_find2(&t, AT_EXAM, exam, AT_STUDENT, student);
    if (i >= 0) {
        snprintf(endbuf, sizeof endbuf, "%lld", (long long)time(NULL));
        join_fields(row, sizeof row, 5, exam, student, fld(t.row[i], AT_START),
                    auto_sub ? "AUTO_SUBMITTED" : "SUBMITTED", endbuf);
        table_set(&t, i, row);
        table_save(F_ATTEMPTS, &t);
    }
    table_free(&t);
}

/* Saves every answer in its own file; MCQs are evaluated automatically. */
static void submit_answers(const char *exam, const char *student, int nq, int show_mcq_now, int auto_sub)
{
    double mcq_got = 0, mcq_max = 0;
    int has_mcq = 0, has_manual = 0;

    for (int k = 0; k < nq; k++) {
        const Question *q = &qs[order[k]];
        char row[LINE_LEN], marks[32], qno[16];
        snprintf(qno, sizeof qno, "%d", q->no);

        if (q->type == 'M') {
            has_mcq = 1;
            mcq_max += q->marks;
            char orig[2] = { 0, 0 };
            if (answers[k][0]) orig[0] = perm[k][toupper((unsigned char)answers[k][0]) - 'A'];   /* shown letter -> original letter */
            int right = orig[0] && orig[0] == q->correct;
            if (right) mcq_got += q->marks;
            fmt_num(right ? q->marks : 0.0, marks, sizeof marks);
            join_fields(row, sizeof row, 5, exam, student, qno, orig[0] ? orig : "-", marks);
            table_append_line(F_MCQ, row);
        } else {
            has_manual = 1;
            /* -1 = waiting for the teacher; an empty answer is simply 0 */
            join_fields(row, sizeof row, 5, exam, student, qno, answers[k], answers[k][0] ? "-1" : "0");
            table_append_line(q->type == 'S' ? F_SHORT : F_LONG, row);
        }
    }
    mark_attempt_end(exam, student, auto_sub);
    audit(auto_sub ? "EXAM_AUTO_SUBMIT" : "EXAM_SUBMIT", "exam %s", exam);

    print_header("SUBMISSION CONFIRMATION");
    printf("  Your answers were submitted%s.\n", auto_sub ? " AUTOMATICALLY because time was over" : "");
    if (has_mcq) {
        if (show_mcq_now)
            printf("  MCQ evaluated: you got %.2f / %.2f marks.\n", mcq_got, mcq_max);
        else
            printf("  MCQ answers were evaluated and stored. Marks appear in your results.\n");
    }
    if (has_manual) printf("  Short/long answers were saved for your teacher to mark.\n");
}

void take_exam(const User *s, const char *code)
{
    Exam ex;
    if (!exam_get(code, &ex)) { printf("  Exam not found.\n"); return; }

    int nq = questions_load(code, qs, MAX_Q);
    if (nq == 0) { printf("  This exam has no questions yet.\n"); return; }
    memset(answers, 0, sizeof answers);
    make_order(nq);

    time_t now = time(NULL);
    time_t deadline = now + (time_t)ex.duration * 60;
    time_t window_end = parse_datetime(ex.edate, ex.etime);
    if (window_end < deadline) deadline = window_end;

    char startbuf[32], row[LINE_LEN];
    snprintf(startbuf, sizeof startbuf, "%lld", (long long)now);
    join_fields(row, sizeof row, 5, code, s->id, startbuf, "STARTED", "");
    table_append_line(F_ATTEMPTS, row);

    save_order(code, s->id, nq);        /* remember exactly what this student was shown */
    audit("EXAM_START", "exam %s", code);
    g_session.exam_mode = 1;            /* the exam timer decides now, not the idle timeout */

    print_header(ex.name);
    printf("  Timer started: %d minute(s) (the exam also closes at %s %s).\n", ex.duration, ex.edate, ex.etime);
    printf("  Questions and options are in a random order, different for every student.\n");
    printf("  Type your answer, then choose Next / Previous / Submit.\n");
    printf("  NOTE: time is checked every time you press Enter; late answers are not saved.\n");

    int cur = 0, auto_sub = 0;
    for (;;) {
        if (time(NULL) >= deadline) {
            printf("\n  *** Time is over! Your exam is being submitted automatically. ***\n");
            auto_sub = 1;
            break;
        }
        const Question *q = &qs[order[cur]];
        char prev[ANS_LEN];
        strcpy(prev, answers[cur]);

        printf("\n");
        print_rule();
        show_remaining(deadline);
        printf("Question %d of %d   [%s, %g mark(s)]\n", cur + 1, nq,
               q->type == 'M' ? "MCQ" : q->type == 'S' ? "Short question" : "Long question", q->marks);
        print_unescaped(q->text, "  ");

        if (q->type == 'M') {
            for (int d = 0; d < 4; d++)
                printf("    %c) %s\n", 'A' + d, q->opt[perm[cur][d] - 'A']);
            char prompt[96], in[64];
            snprintf(prompt, sizeof prompt, "Select option A-D%s: ",
                     answers[cur][0] ? " (Enter keeps your current answer)" : "");
            read_line(prompt, in, sizeof in);
            if (in[0]) {
                char c = (char)toupper((unsigned char)in[0]);
                if (!in[1] && c >= 'A' && c <= 'D') { answers[cur][0] = c; answers[cur][1] = 0; }
                else printf("  Invalid option - answer unchanged.\n");
            }
        } else if (q->type == 'S') {
            if (answers[cur][0]) printf("  Current answer: %s\n", answers[cur]);
            char in[1024];
            read_line("Type answer (Enter keeps current): ", in, sizeof in);
            if (in[0]) strcpy(answers[cur], in);
        } else {
            int write_it = 1;
            if (answers[cur][0]) {
                printf("  Current answer:\n");
                print_unescaped(answers[cur], "    ");
                write_it = confirm("Rewrite this answer");
            }
            if (write_it) {
                char in[ANS_LEN];
                read_multiline("Type your answer", in, sizeof in);
                if (in[0]) strcpy(answers[cur], in);
            }
        }

        /* TIME CHECK */
        if (time(NULL) >= deadline) {
            strcpy(answers[cur], prev);
            printf("\n  *** Time ran out before you finished this answer - it was not saved. ***\n");
            printf("  *** Submitting automatically. ***\n");
            auto_sub = 1;
            break;
        }

        char nav[32];
        read_line("[N]ext  [P]revious  [S]ubmit  (Enter = next): ", nav, sizeof nav);
        char c = (char)toupper((unsigned char)nav[0]);
        if (c == 'S') {
            int answered = 0;
            for (int k = 0; k < nq; k++) if (answers[k][0]) answered++;
            printf("  You answered %d of %d questions.\n", answered, nq);
            if (confirm("Submit the exam now")) break;
        } else if (c == 'P') {
            if (cur > 0) cur--; else printf("  This is the first question.\n");
        } else {
            if (cur < nq - 1) cur++; else printf("  This is the last question. Press S to submit.\n");
        }
    }

    submit_answers(code, s->id, nq, ex.show_mcq, auto_sub);
    {
        char b[16];
        read_line("\nPress Enter to exit...", b, sizeof b);
    }
    g_session.exam_mode = 0;
    g_session.last_activity = time(NULL);
    printf("  EXAM END.\n");
}
