-- demo_data.sql
--
-- A small, deterministic dataset for local development and the end-to-end login
-- test. Applied after migrations/ in filename order:
--
--   psql "$DATABASE_URL" -f db/migrations/001_roles_users.sql
--   psql "$DATABASE_URL" -f db/migrations/002_students.sql
--   psql "$DATABASE_URL" -f db/seeds/demo_data.sql
--
-- The password digests below were produced by the server's own hashing code
-- (server/src/auth/password_hash.cpp) from a fresh random salt, so they verify
-- against the production login path rather than a hand-written hex string. To
-- regenerate after changing the hashing scheme, run:
--
--   printf '%s' "$PASSWORD" | sha256sum   # with the salt prepended, see Hash()
--
-- Demo credentials (development only — these are fixtures, not secrets):
--
--   admin   / admin
--   teacher / teacher
--   student / student
--
-- These are development fixtures with deliberately trivial passwords. The salt
-- and digest on each row were computed together by Hash(); changing either one
-- without recomputing the other makes the account permanently unauthenticatable.
--
-- A password column does not exist; only the salt and digest are stored, per
-- SR-003. Committing these digests is the same trust decision as committing any
-- test fixture: they grant nothing on a real deployment, where the seed is not
-- applied.

BEGIN;

INSERT INTO users (username, full_name, role, password_hash, password_salt, password_algo)
VALUES
    ('admin', 'System Administrator', 'admin',
     '5878f4dd05d8a6ed4955541105d291ba67ca45cef29421be92f9ed0797e4a3ad',
     '0d1f67bbf157a573ab36606c1fc3e332', 'sha256'),
    ('teacher', 'Demo Teacher', 'teacher',
     'aabc7524bc3ad85d44e7a78e6c6387241b91fa7261c5658759b8d159b98993db',
     'd86f2e41830e578d7898f9ffeac1f0a7', 'sha256'),
    ('student', 'Demo Student', 'student',
     '60adc2e6b9849e8e6083df5174355710ac92fe2e814cd7f6160a56eb87f1ceda',
     '1115025deb4d3823b2a222f60d59f693', 'sha256')
ON CONFLICT (username) DO NOTHING;

INSERT INTO students (roll_no, name, date_of_birth, department, semester, contact_details)
VALUES
    ('1ABC26CS001', 'Demo Student', '2003-04-12', 'Computer Science', 5, 'student@example.edu'),
    ('1ABC26CS002', 'Asha Rao',     '2003-01-30', 'Computer Science', 5, 'asha.rao@example.edu')
ON CONFLICT (roll_no) DO NOTHING;

-- Link the student account to the demo student row, exercising the nullable FK
-- from 002_students.sql.
UPDATE users SET roll_no = '1ABC26CS001' WHERE username = 'student' AND roll_no IS NULL;

-- ON CONFLICT makes the seed re-runnable: the UNIQUE keys in 002_students.sql
-- are what these clauses target, so a second apply is a no-op rather than a
-- silent duplication of every grade and attendance row.
INSERT INTO grades (roll_no, subject_code, score) VALUES
    ('1ABC26CS001', 'CS501', 8.5),
    ('1ABC26CS001', 'CS502', 9.0),
    ('1ABC26CS002', 'CS501', 7.5)
ON CONFLICT (roll_no, subject_code) DO NOTHING;

INSERT INTO attendance (roll_no, subject_code, session_date, present) VALUES
    ('1ABC26CS001', 'CS501', '2026-10-20', true),
    ('1ABC26CS001', 'CS501', '2026-10-22', false),
    ('1ABC26CS002', 'CS501', '2026-10-20', true)
ON CONFLICT (roll_no, subject_code, session_date) DO NOTHING;

COMMIT;