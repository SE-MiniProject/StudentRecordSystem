-- 002_students.sql
--
-- Students, their grades and attendance, plus the account-to-student link that
-- 001 deliberately left out.
--
-- These tables were originally defined in a standalone db/init.sql that nothing
-- referenced and no ordering applied; that file has been removed. They live here
-- so `psql -f` over migrations/ in filename order produces the whole schema,
-- which is what README.md describes.

BEGIN;

CREATE TABLE IF NOT EXISTS students (
    id              BIGSERIAL UNIQUE NOT NULL,
    roll_no         VARCHAR(32) PRIMARY KEY,
    name            VARCHAR(120) NOT NULL CHECK (length(trim(name)) > 0),
    date_of_birth   DATE,
    department      VARCHAR(100) NOT NULL CHECK (length(trim(department)) > 0),
    semester        INTEGER NOT NULL CHECK (semester BETWEEN 1 AND 12),
    contact_details VARCHAR(255) NOT NULL CHECK (length(trim(contact_details)) > 0),
    created_at      TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- A student has at most one grade per subject. The UNIQUE key is what makes the
-- seed's ON CONFLICT clause meaningful; without it a re-run inserts duplicates
-- that no query can distinguish from real repeat grades.
CREATE TABLE IF NOT EXISTS grades (
    id           BIGSERIAL PRIMARY KEY,
    roll_no      VARCHAR(32) NOT NULL REFERENCES students(roll_no) ON DELETE CASCADE,
    subject_code VARCHAR(32) NOT NULL,
    score        NUMERIC(3,1) NOT NULL CHECK (score >= 0 AND score <= 10),
    UNIQUE (roll_no, subject_code)
);

-- One attendance row per student, subject and session date.
CREATE TABLE IF NOT EXISTS attendance (
    id           BIGSERIAL PRIMARY KEY,
    roll_no      VARCHAR(32) NOT NULL REFERENCES students(roll_no) ON DELETE CASCADE,
    subject_code VARCHAR(32) NOT NULL,
    session_date DATE NOT NULL,
    present      BOOLEAN NOT NULL DEFAULT false,
    UNIQUE (roll_no, subject_code, session_date)
);

CREATE INDEX IF NOT EXISTS idx_grades_roll_no ON grades(roll_no);
CREATE INDEX IF NOT EXISTS idx_attendance_roll_no ON attendance(roll_no);

-- The account-to-student link, per docs/api.md "Account to student link".
-- UNIQUE so one account maps to at most one student; admin and teacher accounts
-- leave it NULL. ON DELETE CASCADE: deleting the student removes the account
-- rather than leaving it pointing at nothing.
ALTER TABLE users
    ADD COLUMN IF NOT EXISTS roll_no TEXT UNIQUE
    REFERENCES students(roll_no) ON DELETE CASCADE;

COMMIT;