# University Exam Management System (C, console)

Diagram অনুযায়ী বানানো। সব data `data/*.txt` ফাইলে save হয়।

## Build & run
```
make                # Linux / Mac
./exam_system
```
Windows (cmd, project folder এর ভেতর থেকে):
```
gcc -std=gnu99 -o exam_system.exe src/main.c src/util.c src/store.c src/results.c src/auth.c src/exam_take.c src/exam_mgmt.c src/marking.c src/student.c src/teacher.c src/admin.c src/security.c
exam_system.exe
```
**পুরনো version চালিয়ে থাকলে `data` folder টা মুছে দিন** (file format ও password hash বদলেছে)।

## প্রথমবার কীভাবে শুরু করবেন
1. Demo ID শুধু একটা: admin `ADM001`। `3 (Admin) → 2 (Register)` দিয়ে register করুন।
2. Admin হিসেবে login করে `7. University ID registry` এ student / teacher ID যোগ করুন
   (একসাথে অনেকগুলো দেওয়া যায়, space বা comma দিয়ে আলাদা করে)। শুধু এই registry তে থাকা ID দিয়েই register করা যায়।
3. Student ID এর format: `253-33-666`, প্রথম ৩ digit (`253`) = **batch**।
4. Student register এর সময় **section** দিতে হয়, যেমন `46N`। একটা section এ সর্বোচ্চ **৫০ জন**।
5. Admin → Course management থেকে course teacher কে assign করুন। তারপর teacher access code বানাবে, student code দিয়ে course join করবে।

## Password
- Register করলে system email (`<id>@student.uni.edu` ইত্যাদি) ও password দেয়।
- যেকোনো সময় login করে **Profile / Account → Show my password** দিলে নিজের সব info ও password দেখা যায়। অন্য কেউ (admin সহ) কারো password দেখতে পায় না।
- Password change করলে system জিজ্ঞাসা করে সেটা profile এ save রাখবে কিনা। "n" দিলে শুধু hash থাকে, profile এ আর দেখানো হবে না।
- ভুলে গেলে login menu র **Forgot password**: email দিলে একটা ৬ digit code যায়, code দিলে নতুন password দেওয়া যায়।
  **Demo mode:** আসল email/SMS পাঠানো হয় না, message টা `data/outbox.txt` ফাইলে লেখা হয়। ওখান থেকে code দেখে নিন।

## Navigation
- প্রতিটা menu তে **H = Home** দিলে সরাসরি dashboard এ যাওয়া যায় (তারপর Logout)।
- লেখার প্রতিটা ঘরে বন্ধনীতে hint থাকে: `Full name [blank = go back]:`। ঘর ফাঁকা রেখে Enter চাপলে আগের menu তে ফিরে যায় (কিছু save হয় না)।
  Optional ঘরে `[optional - Enter to skip]` লেখা থাকে, ফাঁকা রাখলে এড়িয়ে যায়।
- Exam দেওয়ার সময় H কাজ করে না (ইচ্ছা করে, যাতে ভুলে বের হয়ে না যায়)।

## v4: ছোট fix
- Register শেষে email ও password দেখায় এবং বলে দেয় "1. Login" দিয়ে এগুলো দিয়ে login করতে হবে (আর সরাসরি portal এ ঢোকে না)। পরে Profile / Account থেকে password দেখা/বদলানো যায়।
- Admin এর Student/Teacher Edit, Delete, Search/View এ আগে **সবার তালিকা** (ID, নাম, batch, section, dept, phone, email) দেখায়; তারপর list এর নম্বর দিন, অথবা ID/নাম/batch/section/dept লিখে search করুন। Result search, exam search, registry remove, course/department/exam add এর আগেও existing তালিকা দেখায়।
- Login ভুল হলে (বা Forgot password → 2) **University ID + registered phone** দিয়ে নতুন password দেওয়া যায় (৩ বার চেষ্টা)।

## v3: নতুন সুবিধা
- **Random question order:** প্রতিটা student আলাদা এলোমেলো ক্রমে question ও MCQ option পায়। কে কোন ক্রমে পেয়েছে `data/order.txt` এ save থাকে; MCQ উত্তর মূল option অনুযায়ী মিলিয়ে নম্বর দেওয়া হয়।
- **Integrity check:** প্রতিটা data ফাইলের SHA-256 seal `data/integrity.txt` এ থাকে। Program ছাড়া কেউ ফাইল হাতে edit করলে শুরুতে ও Admin dashboard এ warning আসে এবং **result submit বন্ধ** থাকে। Admin → Security → *Accept current data* দিলে আবার seal হয় (log এ লেখা থাকে)।
- **Activity log:** `data/audit_log.txt` এ login/logout, registration, exam start/submit, marking, result publish, reset ইত্যাদি সময়সহ থাকে। Password কখনো log হয় না। Admin → Security থেকে দেখা ও search করা যায়।
- **Interrupted attempt reset:** exam শুরু করে বন্ধ হয়ে গেলে (attempt STARTED, submit নেই) teacher (Exam management → *Reset interrupted attempt*) বা admin (Security → 5) reset করলে student আবার দিতে পারে।
- **Idle logout:** ১০ মিনিট কিছু না করলে session বন্ধ হয় (exam চলার সময় exam timer কাজ করে)। Test এর জন্য `EXAM_IDLE_SECONDS=5` environment variable দেওয়া যায়।
- Password এখন salted SHA-256 (২০০০ round) hash।

## Code structure (struct / array)
`User, Exam, Question, Attempt, Session, Table, Score` struct এবং `Question qs[]`, order/permutation array ব্যবহার হয়েছে। C তে OOP (class) নেই, তাই struct + function দিয়ে module ভাগ করা: `auth, admin, teacher, student, exam_mgmt, exam_take, marking, results, store, security, util`।

## Data files
`university_ids.txt students.txt teachers.txt admins.txt departments.txt courses.txt enrollments.txt exams.txt questions.txt attempts.txt mcq.txt short_ans.txt long_ans.txt final_results.txt outbox.txt order.txt audit_log.txt integrity.txt`
(`|` দিয়ে আলাদা, প্রতি লাইনে একটা record)

## সীমাবদ্ধতা
- Integrity seal এর key program এর ভেতরে; এটা হাতে ফাইল edit ধরে, কিন্তু source code জানা কেউ ঠেকাতে পারবে না।
- একসাথে একাধিক জন একই ফাইলে লিখলে file locking নেই।
- Idle timeout ও exam timer Enter চাপার সময় check হয়।
- Password দেখানোর সুবিধার জন্য save রাখা password `students.txt` ইত্যাদিতে plain text এ থাকে। Coursework এর জন্য ঠিক আছে, কিন্তু আসল system এ এটা করা উচিত না।
- Timer Enter চাপার সময় check হয়, তাই সময় শেষে Enter না চাপা পর্যন্ত auto-submit screen এ দেখা যায় না (late answer save হয় না)।
