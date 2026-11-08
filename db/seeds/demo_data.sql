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
--   student2 / student2
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
    ('admin', 'Anil Deshmukh', 'admin',
     '5878f4dd05d8a6ed4955541105d291ba67ca45cef29421be92f9ed0797e4a3ad',
     '0d1f67bbf157a573ab36606c1fc3e332', 'sha256'),
    ('teacher', 'Dr. Meera Nair', 'teacher',
     'aabc7524bc3ad85d44e7a78e6c6387241b91fa7261c5658759b8d159b98993db',
     'd86f2e41830e578d7898f9ffeac1f0a7', 'sha256'),
    ('student', 'Aarav Sharma', 'student',
     '60adc2e6b9849e8e6083df5174355710ac92fe2e814cd7f6160a56eb87f1ceda',
     '1115025deb4d3823b2a222f60d59f693', 'sha256'),
    ('student2', 'Aditi Nair', 'student',
     'c4398ed13525b0b728f8e1f3cdf4294f3d5b7b697745055ab883f40d51c3d6f4',
     'f4474ac71d12926e17e78cfe0152cb99', 'sha256')
ON CONFLICT (username) DO UPDATE SET full_name = EXCLUDED.full_name;

INSERT INTO students (roll_no, name, date_of_birth, department, semester, contact_details)
VALUES
    ('1ABC26CS001', 'Aarav Sharma', '2003-04-12', 'Computer Science', 5, 'aarav.sharma@example.edu'),
    ('1ABC26CS002', 'Asha Rao',     '2003-01-30', 'Computer Science', 5, 'asha.rao@example.edu'),
    ('1ABC26CS003', 'Aditi Nair',   '2004-02-18', 'Computer Science', 5, 'aditi.nair@example.edu'),
    ('1ABC26CS004', 'Kabir Menon',  '2003-11-07', 'Computer Science', 5, 'kabir.menon@example.edu'),
    ('1ABC26CS005', 'Isha Kapoor',  '2004-06-22', 'Computer Science', 5, 'isha.kapoor@example.edu'),
    ('1ABC26EC001', 'Rohan Desai',  '2003-08-14', 'Electronics and Communication', 5, 'rohan.desai@example.edu'),
    ('1ABC26EC002', 'Meera Iyer',   '2004-03-09', 'Electronics and Communication', 5, 'meera.iyer@example.edu'),
    ('1ABC26EC003', 'Arjun Pillai', '2003-12-01', 'Electronics and Communication', 5, 'arjun.pillai@example.edu'),
    ('1ABC26IS001', 'Ananya Rao',   '2004-01-25', 'Information Science', 3, 'ananya.rao@example.edu'),
    ('1ABC26IS002', 'Vihaan Shah',  '2003-09-17', 'Information Science', 3, 'vihaan.shah@example.edu'),
    ('1ABC26IS003', 'Diya Krishnan','2004-05-11', 'Information Science', 3, 'diya.krishnan@example.edu')
ON CONFLICT (roll_no) DO UPDATE SET
    name = EXCLUDED.name,
    contact_details = EXCLUDED.contact_details;

-- Link the student account to the demo student row, exercising the nullable FK
-- from 002_students.sql.
UPDATE users SET roll_no = '1ABC26CS001' WHERE username = 'student' AND roll_no IS NULL;
UPDATE users SET roll_no = '1ABC26CS003' WHERE username = 'student2' AND roll_no IS NULL;

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