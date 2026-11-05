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
--   psql "$DATABASE_URL" -f db/seeds/demo_data.sql
--   psql "$DATABASE_URL" -f db/seeds/demo_records.sql
--
-- Kept separate from demo_data.sql so Sprint 1's seed (and SeedData.* tests,
-- which parse its users block) is untouched.
--
-- Demo credentials added here (development only):
--
--   teacher2 / teacher2
--
-- The digest was produced by server/src/auth/password_hash.cpp Hash() from the
-- salt beside it, exactly like the accounts in demo_data.sql.
--
-- Re-runnable: every insert has ON CONFLICT DO NOTHING against a real key.

BEGIN;

INSERT INTO users (username, full_name, role, password_hash, password_salt, password_algo)
VALUES
    ('teacher2', 'Second Teacher', 'teacher',
     '20d83c89ee9949382283db138df379df75deae811609917902c2f9397675eb5a',
     '9fb8f6f2d5f263ded589efb8be391e70', 'sha256')
ON CONFLICT (username) DO NOTHING;

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

COMMIT;
