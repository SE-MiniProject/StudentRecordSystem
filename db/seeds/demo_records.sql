-- demo_records.sql
--
-- Sprint 2 demo data: subjects, which teacher teaches what, and extra grade
-- and attendance rows, so the role-scoped reads (SRS-F-014 grades, F-017/F-018
-- attendance) have something to scope.
-- Applied AFTER demo_data.sql, because it references the users and students
-- that file creates:
--
--   psql "$DATABASE_URL" -f db/migrations/001_roles_users.sql
--   psql "$DATABASE_URL" -f db/migrations/002_students.sql
--   psql "$DATABASE_URL" -f db/migrations/003_subjects_teaching.sql
--   psql "$DATABASE_URL" -f db/migrations/004_timetable.sql
--   psql "$DATABASE_URL" -f db/seeds/demo_data.sql
--   psql "$DATABASE_URL" -f db/seeds/demo_records.sql
--
-- Kept separate from demo_data.sql so the subject assignments, grades,
-- attendance sessions, and timetable can be loaded after the core student
-- accounts and directory records.
--
-- Demo credentials added here (development only):
--
--   teacher2 / teacher2
--   neha.kulkarni / neha123
--   rahul.verma / rahul123
--
-- The digest was produced by server/src/auth/password_hash.cpp Hash() from the
-- salt beside it, exactly like the accounts in demo_data.sql.
--
-- Re-runnable: inserts use stable unique keys; account upserts refresh display names.

BEGIN;

INSERT INTO users (username, full_name, role, password_hash, password_salt, password_algo)
VALUES
    ('teacher2', 'Prof. Arjun Menon', 'teacher',
     '20d83c89ee9949382283db138df379df75deae811609917902c2f9397675eb5a',
     '9fb8f6f2d5f263ded589efb8be391e70', 'sha256'),
    ('neha.kulkarni', 'Dr. Neha Kulkarni', 'teacher',
     '766de9dc33b4d79b1f12d8c3ca9c79dc9e3b4990b632a79da643d7ff68c2c9f4',
     '31525e2da71612b4d2426f12cfbd476a', 'sha256'),
    ('rahul.verma', 'Prof. Rahul Verma', 'teacher',
     '990a4f01fcbc8073daced19f6e9f65882ea839452752f7993de94e94443bfc59',
     'af5ab16fae194d740ffcf489fa874bde', 'sha256')
ON CONFLICT (username) DO UPDATE SET full_name = EXCLUDED.full_name;

INSERT INTO subjects (code, name, semester) VALUES
    ('CS501', 'Database Management Systems', 5),
    ('CS502', 'Operating Systems',           5),
    ('CS503', 'Computer Networks',           5),
    ('CS301', 'Data Structures',             3)
ON CONFLICT (code) DO NOTHING;

-- teacher  teaches CS501 and CS503
-- teacher2 teaches CS502
-- CS301 is assigned to nobody: only Admin and the owning Student see it.
INSERT INTO teacher_subjects (username, subject_code) VALUES
    ('teacher',  'CS501'),
    ('teacher',  'CS503'),
    ('teacher2', 'CS502')
ON CONFLICT (username, subject_code) DO NOTHING;

-- demo_data.sql already has CS501/CS502 for 1ABC26CS001 and CS501 for
-- 1ABC26CS002. These fill in the other subjects.
INSERT INTO grades (roll_no, subject_code, score) VALUES
    ('1ABC26CS001', 'CS503', 7.0),
    ('1ABC26CS001', 'CS301', 8.0),
    ('1ABC26CS002', 'CS502', 6.5)
ON CONFLICT (roll_no, subject_code) DO NOTHING;

-- Attendance (SRS-F-017/F-018). demo_data.sql already has, for 1ABC26CS001,
-- CS501 on 10-20 (present) and 10-22 (absent). With the rows below the demo
-- student's expected percentages are:
--
--   CS501  3 / 4  = 75.00   (teacher)
--   CS502  2 / 3  = 66.67   (teacher2)  <- exercises two-decimal rounding
--   CS503  1 / 1  = 100.00  (teacher)
--   overall 6 / 8 = 75.00
--
-- What each role sees for 1ABC26CS001: admin and the student all three,
-- teacher CS501 + CS503 (overall 4 / 5 = 80.00), teacher2 CS502 only.
INSERT INTO attendance (roll_no, subject_code, session_date, present) VALUES
    ('1ABC26CS001', 'CS501', '2026-10-27', true),
    ('1ABC26CS001', 'CS501', '2026-10-29', true),
    ('1ABC26CS001', 'CS502', '2026-10-21', true),
    ('1ABC26CS001', 'CS502', '2026-10-23', true),
    ('1ABC26CS001', 'CS502', '2026-10-28', false),
    ('1ABC26CS001', 'CS503', '2026-10-24', true),
    ('1ABC26CS002', 'CS502', '2026-10-21', false)
ON CONFLICT (roll_no, subject_code, session_date) DO NOTHING;

-- Timetable (SRS-F-020). A second class (Computer Science, semester 3) with a
-- subject that has no grades or attendance, so adding it changes nothing the
-- grade/attendance checks above expect.
INSERT INTO subjects (code, name, semester) VALUES
    ('CS302', 'Discrete Mathematics', 3)
ON CONFLICT (code) DO NOTHING;

INSERT INTO teacher_subjects (username, subject_code) VALUES
    ('teacher2', 'CS302')
ON CONFLICT (username, subject_code) DO NOTHING;

-- Expected per role:
--   student  (Computer Science, sem 5)  6 slots, the whole sem-5 week
--   teacher  4 slots (CS501 x2, CS503 x2)
--   teacher2 4 slots (CS502 x2, CS302 x2)
--   admin    8 slots
INSERT INTO timetable (subject_code, teacher_username, department, semester,
                       day_of_week, start_time, end_time, room) VALUES
    ('CS501', 'teacher',  'Computer Science', 5, 1, '09:00', '10:00', 'A101'),
    ('CS502', 'teacher2', 'Computer Science', 5, 1, '10:00', '11:00', 'A101'),
    ('CS503', 'teacher',  'Computer Science', 5, 2, '09:00', '10:00', 'A102'),
    ('CS501', 'teacher',  'Computer Science', 5, 3, '11:00', '12:00', 'A101'),
    ('CS502', 'teacher2', 'Computer Science', 5, 4, '14:00', '15:00', 'Lab 2'),
    ('CS503', 'teacher',  'Computer Science', 5, 5, '10:00', '11:00', 'A102'),
    ('CS302', 'teacher2', 'Computer Science', 3, 1, '11:00', '12:00', 'B201'),
    ('CS302', 'teacher2', 'Computer Science', 3, 3, '09:00', '10:00', 'B201')
ON CONFLICT DO NOTHING;


-- Additional departments, subjects, and teachers for a more representative
-- directory and role-scoped records demo. The original teacher/teacher2
-- assignments remain unchanged for the focused regression checks.
INSERT INTO subjects (code, name, semester) VALUES
    ('EC501', 'Digital Signal Processing', 5),
    ('EC502', 'Microprocessors and Microcontrollers', 5),
    ('IS301', 'Software Engineering', 3),
    ('IS302', 'Computer Organization', 3)
ON CONFLICT (code) DO NOTHING;

INSERT INTO teacher_subjects (username, subject_code) VALUES
    ('neha.kulkarni', 'EC501'),
    ('neha.kulkarni', 'EC502'),
    ('rahul.verma', 'IS301'),
    ('rahul.verma', 'IS302')
ON CONFLICT (username, subject_code) DO NOTHING;

-- Grades for the additional Computer Science, Electronics and Communication,
-- and Information Science students.
INSERT INTO grades (roll_no, subject_code, score) VALUES
    ('1ABC26CS003', 'CS501', 8.2),
    ('1ABC26CS003', 'CS502', 7.6),
    ('1ABC26CS003', 'CS503', 9.1),
    ('1ABC26CS004', 'CS501', 6.8),
    ('1ABC26CS004', 'CS502', 8.7),
    ('1ABC26CS004', 'CS503', 7.9),
    ('1ABC26CS005', 'CS501', 9.3),
    ('1ABC26CS005', 'CS502', 7.2),
    ('1ABC26CS005', 'CS503', 8.5),
    ('1ABC26EC001', 'EC501', 6.9),
    ('1ABC26EC001', 'EC502', 9.0),
    ('1ABC26EC002', 'EC501', 7.8),
    ('1ABC26EC002', 'EC502', 8.4),
    ('1ABC26EC003', 'EC501', 7.1),
    ('1ABC26EC003', 'EC502', 9.2),
    ('1ABC26IS001', 'IS301', 8.0),
    ('1ABC26IS001', 'IS302', 7.7),
    ('1ABC26IS002', 'IS301', 8.8),
    ('1ABC26IS002', 'IS302', 6.5),
    ('1ABC26IS003', 'IS301', 9.4),
    ('1ABC26IS003', 'IS302', 7.3)
ON CONFLICT (roll_no, subject_code) DO NOTHING;

-- Two dated sessions per subject and student. Varied attendance produces
-- meaningful percentages without changing the original regression fixtures.
INSERT INTO attendance (roll_no, subject_code, session_date, present) VALUES
    ('1ABC26CS003', 'CS501', '2026-10-06', false),
    ('1ABC26CS003', 'CS501', '2026-10-13', false),
    ('1ABC26CS003', 'CS502', '2026-10-06', true),
    ('1ABC26CS003', 'CS502', '2026-10-13', true),
    ('1ABC26CS003', 'CS503', '2026-10-06', true),
    ('1ABC26CS003', 'CS503', '2026-10-13', true),
    ('1ABC26CS004', 'CS501', '2026-10-06', true),
    ('1ABC26CS004', 'CS501', '2026-10-13', true),
    ('1ABC26CS004', 'CS502', '2026-10-06', true),
    ('1ABC26CS004', 'CS502', '2026-10-13', true),
    ('1ABC26CS004', 'CS503', '2026-10-06', true),
    ('1ABC26CS004', 'CS503', '2026-10-13', false),
    ('1ABC26CS005', 'CS501', '2026-10-06', true),
    ('1ABC26CS005', 'CS501', '2026-10-13', true),
    ('1ABC26CS005', 'CS502', '2026-10-06', true),
    ('1ABC26CS005', 'CS502', '2026-10-13', false),
    ('1ABC26CS005', 'CS503', '2026-10-06', false),
    ('1ABC26CS005', 'CS503', '2026-10-13', true),
    ('1ABC26EC001', 'EC501', '2026-10-06', true),
    ('1ABC26EC001', 'EC501', '2026-10-13', false),
    ('1ABC26EC001', 'EC502', '2026-10-06', false),
    ('1ABC26EC001', 'EC502', '2026-10-13', true),
    ('1ABC26EC002', 'EC501', '2026-10-06', false),
    ('1ABC26EC002', 'EC501', '2026-10-13', true),
    ('1ABC26EC002', 'EC502', '2026-10-06', true),
    ('1ABC26EC002', 'EC502', '2026-10-13', true),
    ('1ABC26EC003', 'EC501', '2026-10-06', true),
    ('1ABC26EC003', 'EC501', '2026-10-13', true),
    ('1ABC26EC003', 'EC502', '2026-10-06', true),
    ('1ABC26EC003', 'EC502', '2026-10-13', false),
    ('1ABC26IS001', 'IS301', '2026-10-06', true),
    ('1ABC26IS001', 'IS301', '2026-10-13', false),
    ('1ABC26IS001', 'IS302', '2026-10-06', true),
    ('1ABC26IS001', 'IS302', '2026-10-13', true),
    ('1ABC26IS002', 'IS301', '2026-10-06', true),
    ('1ABC26IS002', 'IS301', '2026-10-13', true),
    ('1ABC26IS002', 'IS302', '2026-10-06', false),
    ('1ABC26IS002', 'IS302', '2026-10-13', true),
    ('1ABC26IS003', 'IS301', '2026-10-06', false),
    ('1ABC26IS003', 'IS301', '2026-10-13', true),
    ('1ABC26IS003', 'IS302', '2026-10-06', true),
    ('1ABC26IS003', 'IS302', '2026-10-13', false)
ON CONFLICT (roll_no, subject_code, session_date) DO NOTHING;

-- Additional weekly schedules for the two new departments. Each teacher has
-- four non-overlapping slots; each class has no simultaneous sessions.
INSERT INTO timetable (subject_code, teacher_username, department, semester,
                       day_of_week, start_time, end_time, room) VALUES
    ('EC501', 'neha.kulkarni', 'Electronics and Communication', 5, 1, '09:00', '10:00', 'E201'),
    ('EC502', 'neha.kulkarni', 'Electronics and Communication', 5, 2, '09:00', '10:00', 'E202'),
    ('EC501', 'neha.kulkarni', 'Electronics and Communication', 5, 3, '11:00', '12:00', 'E201'),
    ('EC502', 'neha.kulkarni', 'Electronics and Communication', 5, 4, '14:00', '15:00', 'E202'),
    ('IS301', 'rahul.verma', 'Information Science', 3, 1, '11:00', '12:00', 'I301'),
    ('IS302', 'rahul.verma', 'Information Science', 3, 2, '11:00', '12:00', 'I302'),
    ('IS301', 'rahul.verma', 'Information Science', 3, 3, '09:00', '10:00', 'I301'),
    ('IS302', 'rahul.verma', 'Information Science', 3, 4, '10:00', '11:00', 'I302')
ON CONFLICT DO NOTHING;

-- Additional student accounts for repeatable load testing.
-- Password for student03 through student25: student2026.
INSERT INTO users (username, full_name, role, password_hash, password_salt, password_algo)
VALUES
    ('student03', 'Asha Rao', 'student', '63aa3b41744ce59d7a574255ae9e3f61c4a3089de28cb32b28a1136ffc0e3919', '06a10d06838c25921ffb8d81e6fe97b8', 'sha256'),
    ('student04', 'Kabir Menon', 'student', '8f3c8770e07f1e58f0514c66f4a545a7ea4af4f5c1890ea069e1a1b8bcee047e', '54d04df09c3cb9e2bcfbaefc45ab2620', 'sha256'),
    ('student05', 'Isha Kapoor', 'student', 'b7437f2bb8fcf9db65f19402ad5d713404160a08d1b252cc2c599733bea53e43', 'f73de15b70dd6c218234127f082e3f64', 'sha256'),
    ('student06', 'Rohan Desai', 'student', 'a0cc289e93a7c158449810de25437d45942cc1e3b259c22dfede439e04d9596b', '6c40e5ea16a28160ff010922731dbdbc', 'sha256'),
    ('student07', 'Meera Iyer', 'student', 'f2bb52ce9dde3f34905759a6dad797d4401ac077f618d68fa950066249854343', '30b1dc34378cfb958dd6366b81f19058', 'sha256'),
    ('student08', 'Arjun Pillai', 'student', '84ca790926b743205c899f990686c0f1a71b2da1ff5faa0a7c3ff2cbaf1ec857', 'fb0dfd454d1626ee52a34f3994121c4f', 'sha256'),
    ('student09', 'Ananya Rao', 'student', '6c807694eee93aa78fa91d28990066eea042e3363e8729246fa4583cf34c1b55', '8e770ebba68f368e771fd0bc3e7e9342', 'sha256'),
    ('student10', 'Vihaan Shah', 'student', '180d4983b6282aacbcbe8d9a525b0ca471e37ac2c735b56356bed412042eaa86', '704074893797a50c15666dfdc595aa5f', 'sha256'),
    ('student11', 'Diya Krishnan', 'student', '84d1b4bcdde095acf2603f41c6393e1c0d25242ccafa8b399399e3663adf587d', '4801a762a66048c11ed788839a38bf49', 'sha256'),
    ('student12', 'Sana Kapoor', 'student', 'f7efdaf0acbc1914d9d5e9017890d76557efb4acbc6bbb12dcff6a8c07a49cd2', '8cc5d79deb88faf444e7147013193455', 'sha256'),
    ('student13', 'Aditya Rao', 'student', '73455f46ae13d464c7466c0f161f95641b1762063d0977eac8b586914ce87764', 'd07d19760cf67974fe9454522dea7255', 'sha256'),
    ('student14', 'Kavya Menon', 'student', '2b5f877d285c396d3187bd9a63be4c0e82cadd8d741108e926ac8f77d57d3eeb', 'd4f14b4e2d4ef9c3b9a38b967abdcb51', 'sha256'),
    ('student15', 'Ishaan Gupta', 'student', 'fc78391cc200bedd8357c6b2b6dbf76ceda82dc0521ed96f8d0369869c6dcad7', '7742153f773c3462295195dc69468c71', 'sha256'),
    ('student16', 'Mira Nair', 'student', '7e4d38e088cefe388cbfeefcdd007c3940041801ba1cb8aa04c647daf83ca062', '02367f07eaae53e44d1081d248907bd1', 'sha256'),
    ('student17', 'Dev Patel', 'student', '01121a6868a342cd2a0fda70caace053ad6bfa775f1e741f6c82895908c9c048', '1152b455ef5081d4381e8c60819e9412', 'sha256'),
    ('student18', 'Tara Shah', 'student', '3740fd42fbdd46c72dc56f08dd80203afdeefa16f019fa910eb3aa98b3a3e66f', '3026a9ffdec511b56e8880323643f517', 'sha256'),
    ('student19', 'Nikhil Reddy', 'student', 'd227686649dbdf1f3a1bb06af870655c41d0b2602b3dd95524e86a617aa4a7a7', '7732f299dd5237938b3bb1afc20fcf8b', 'sha256'),
    ('student20', 'Anika Bose', 'student', '0fc025fc0fb11c2d8ca8dfa396d294531db31353e16203f071b453605a9d05a9', '97309b996d08c7357437f9787bf17e09', 'sha256'),
    ('student21', 'Karan Iyer', 'student', '907105b96952c32461eeec73d241a3faf651fbc59b19da60d6a40023ba3420f0', '93fb9e0b417ff8dc69db45c5c8e14a0e', 'sha256'),
    ('student22', 'Riya Das', 'student', 'a3ecc2aa4b8ca0a83d91583940e39ec2ca81242ea8a89886e0e4fc68642543f1', '028fea5235b15601c787183f60e14563', 'sha256'),
    ('student23', 'Arnav Joshi', 'student', 'c507177f8a7610060bd4745190622c789353ff0b42122b667fd29ef920f4b698', '3f1c0d283bfefab535366552fc5b6132', 'sha256'),
    ('student24', 'Pooja Nair', 'student', 'd0377524c20ddb94183ca39bbc945822d06fb697548268fdfd33f99facd35887', 'b441dc926f4633f329a10783b04411ab', 'sha256'),
    ('student25', 'Reyansh Kulkarni', 'student', 'b84f2fc988fa0cddcc70f68e5d88f041192091ec137ead5f7902c559a08892aa', '643089d38bdfaaa36e2fc9770e31e7c4', 'sha256')
ON CONFLICT (username) DO UPDATE SET
    full_name = EXCLUDED.full_name,
    password_hash = EXCLUDED.password_hash,
    password_salt = EXCLUDED.password_salt,
    password_algo = EXCLUDED.password_algo;

-- Fourteen additional student records bring the total to 25.
INSERT INTO students (roll_no, name, date_of_birth, department, semester, contact_details)
VALUES
    ('1ABC26CS006', 'Sana Kapoor', '2004-02-14', 'Computer Science', 5, 'sana.kapoor@example.edu'),
    ('1ABC26CS007', 'Aditya Rao', '2003-07-19', 'Computer Science', 5, 'aditya.rao@example.edu'),
    ('1ABC26CS008', 'Kavya Menon', '2004-04-08', 'Computer Science', 5, 'kavya.menon@example.edu'),
    ('1ABC26CS009', 'Ishaan Gupta', '2003-10-21', 'Computer Science', 5, 'ishaan.gupta@example.edu'),
    ('1ABC26CS010', 'Mira Nair', '2004-01-16', 'Computer Science', 5, 'mira.nair@example.edu'),
    ('1ABC26EC004', 'Dev Patel', '2003-05-12', 'Electronics and Communication', 5, 'dev.patel@example.edu'),
    ('1ABC26EC005', 'Tara Shah', '2004-08-23', 'Electronics and Communication', 5, 'tara.shah@example.edu'),
    ('1ABC26EC006', 'Nikhil Reddy', '2003-03-17', 'Electronics and Communication', 5, 'nikhil.reddy@example.edu'),
    ('1ABC26EC007', 'Anika Bose', '2004-11-02', 'Electronics and Communication', 5, 'anika.bose@example.edu'),
    ('1ABC26EC008', 'Karan Iyer', '2003-06-28', 'Electronics and Communication', 5, 'karan.iyer@example.edu'),
    ('1ABC26IS004', 'Riya Das', '2004-09-13', 'Information Science', 3, 'riya.das@example.edu'),
    ('1ABC26IS005', 'Arnav Joshi', '2003-12-05', 'Information Science', 3, 'arnav.joshi@example.edu'),
    ('1ABC26IS006', 'Pooja Nair', '2004-07-24', 'Information Science', 3, 'pooja.nair@example.edu'),
    ('1ABC26IS007', 'Reyansh Kulkarni', '2003-02-09', 'Information Science', 3, 'reyansh.kulkarni@example.edu')
ON CONFLICT (roll_no) DO UPDATE SET
    name = EXCLUDED.name,
    contact_details = EXCLUDED.contact_details;

-- Link the new accounts to the remaining existing and new student records.
UPDATE users SET roll_no = '1ABC26CS002' WHERE username = 'student03' AND roll_no IS NULL;
UPDATE users SET roll_no = '1ABC26CS004' WHERE username = 'student04' AND roll_no IS NULL;
UPDATE users SET roll_no = '1ABC26CS005' WHERE username = 'student05' AND roll_no IS NULL;
UPDATE users SET roll_no = '1ABC26EC001' WHERE username = 'student06' AND roll_no IS NULL;
UPDATE users SET roll_no = '1ABC26EC002' WHERE username = 'student07' AND roll_no IS NULL;
UPDATE users SET roll_no = '1ABC26EC003' WHERE username = 'student08' AND roll_no IS NULL;
UPDATE users SET roll_no = '1ABC26IS001' WHERE username = 'student09' AND roll_no IS NULL;
UPDATE users SET roll_no = '1ABC26IS002' WHERE username = 'student10' AND roll_no IS NULL;
UPDATE users SET roll_no = '1ABC26IS003' WHERE username = 'student11' AND roll_no IS NULL;
UPDATE users SET roll_no = '1ABC26CS006' WHERE username = 'student12' AND roll_no IS NULL;
UPDATE users SET roll_no = '1ABC26CS007' WHERE username = 'student13' AND roll_no IS NULL;
UPDATE users SET roll_no = '1ABC26CS008' WHERE username = 'student14' AND roll_no IS NULL;
UPDATE users SET roll_no = '1ABC26CS009' WHERE username = 'student15' AND roll_no IS NULL;
UPDATE users SET roll_no = '1ABC26CS010' WHERE username = 'student16' AND roll_no IS NULL;
UPDATE users SET roll_no = '1ABC26EC004' WHERE username = 'student17' AND roll_no IS NULL;
UPDATE users SET roll_no = '1ABC26EC005' WHERE username = 'student18' AND roll_no IS NULL;
UPDATE users SET roll_no = '1ABC26EC006' WHERE username = 'student19' AND roll_no IS NULL;
UPDATE users SET roll_no = '1ABC26EC007' WHERE username = 'student20' AND roll_no IS NULL;
UPDATE users SET roll_no = '1ABC26EC008' WHERE username = 'student21' AND roll_no IS NULL;
UPDATE users SET roll_no = '1ABC26IS004' WHERE username = 'student22' AND roll_no IS NULL;
UPDATE users SET roll_no = '1ABC26IS005' WHERE username = 'student23' AND roll_no IS NULL;
UPDATE users SET roll_no = '1ABC26IS006' WHERE username = 'student24' AND roll_no IS NULL;
UPDATE users SET roll_no = '1ABC26IS007' WHERE username = 'student25' AND roll_no IS NULL;



COMMIT;
