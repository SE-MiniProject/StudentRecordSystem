-- 003_subjects_teaching.sql
--
-- Subjects and the teacher-to-subject assignment that every role-scoped read in
-- Sprint 2 joins against (SRS-F-014 grades, F-017/F-018 attendance, F-020
-- timetable): "a Teacher sees their own subjects" is only answerable if the
-- database records which subjects are theirs.
--
-- grades.subject_code and attendance.subject_code are deliberately NOT given a
-- foreign key to subjects here. Existing rows, tests/acceptance.sh and any
-- in-flight grade/attendance entry work insert arbitrary codes; a new FK would
-- turn those inserts into failures in someone else's lane. Reads LEFT JOIN
-- subjects instead, so an unknown code still displays (with its code as the
-- name) and simply belongs to no teacher.
--
-- Re-runnable: every statement is IF NOT EXISTS, so CI's "apply twice, compare
-- row counts" check passes.

BEGIN;

-- One row per subject. `semester` is the semester the subject is taught in; the
-- grades and attendance responses report it from here rather than storing it
-- again on every grade row (api.md "Grades": semester).
CREATE TABLE IF NOT EXISTS subjects (
    code       VARCHAR(32) PRIMARY KEY CHECK (length(trim(code)) > 0),
    name       VARCHAR(120) NOT NULL CHECK (length(trim(name)) > 0),
    semester   INTEGER NOT NULL CHECK (semester BETWEEN 1 AND 12),
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- Which teacher teaches which subject. Many-to-many: a subject may be shared by
-- two teachers, and a teacher has several subjects. Both sides cascade, so
-- deleting an account or a subject never leaves an assignment pointing at
-- nothing.
CREATE TABLE IF NOT EXISTS teacher_subjects (
    username     TEXT NOT NULL REFERENCES users(username) ON DELETE CASCADE,
    subject_code VARCHAR(32) NOT NULL REFERENCES subjects(code) ON DELETE CASCADE,
    PRIMARY KEY (username, subject_code)
);

-- The scoping query filters by subject for a given teacher; the PK above
-- already serves "subjects of teacher X". This index serves the reverse
-- lookup, "does any assignment of this subject belong to X", from the grades
-- and attendance side.
CREATE INDEX IF NOT EXISTS idx_teacher_subjects_subject ON teacher_subjects(subject_code);

-- api.md "Grades": updatedAt is server-set on create and update (AC-013). The
-- column did not exist in 002; existing rows get the migration time.
ALTER TABLE grades
    ADD COLUMN IF NOT EXISTS updated_at TIMESTAMPTZ NOT NULL DEFAULT now();

COMMIT;
