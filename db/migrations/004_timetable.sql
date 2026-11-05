-- 004_timetable.sql
--
-- The weekly timetable for SRS-F-020 (AC-020 / TC-020). One row is one weekly
-- slot: a subject, taught by a teacher, to one class, on one weekday, between
-- two times.
--
-- A "class" is a (department, semester) pair, the same two columns the
-- students table already has, so "a Student sees their own class schedule" is
-- a plain equality join on students — no separate classes table is needed
-- for Sprint 2.
--
-- Depends on 003 (subjects, teacher_subjects). Re-runnable.

BEGIN;

CREATE TABLE IF NOT EXISTS timetable (
    id               BIGSERIAL PRIMARY KEY,
    subject_code     VARCHAR(32) NOT NULL,
    teacher_username TEXT NOT NULL,
    department       VARCHAR(100) NOT NULL CHECK (length(trim(department)) > 0),
    semester         INTEGER NOT NULL CHECK (semester BETWEEN 1 AND 12),
    -- ISO weekday: 1 = Monday … 7 = Sunday.
    day_of_week      SMALLINT NOT NULL CHECK (day_of_week BETWEEN 1 AND 7),
    start_time       TIME NOT NULL,
    end_time         TIME NOT NULL,
    room             VARCHAR(32) NOT NULL DEFAULT '',
    CHECK (end_time > start_time),

    -- A teacher can only be timetabled for a subject they are assigned to. The
    -- composite FK makes that a database fact rather than an application
    -- convention, and it is what keeps "Teacher sees own schedule" and
    -- "Teacher sees own subjects" (grades, attendance) from disagreeing.
    -- Removing the assignment removes its slots.
    FOREIGN KEY (teacher_username, subject_code)
        REFERENCES teacher_subjects(username, subject_code) ON DELETE CASCADE,

    -- A class is not in two places at once, and neither is a teacher. Also the
    -- keys the seed's ON CONFLICT relies on to be re-runnable.
    UNIQUE (department, semester, day_of_week, start_time),
    UNIQUE (teacher_username, day_of_week, start_time)
);

-- The two scoping queries: a teacher's own slots, and one class's slots.
CREATE INDEX IF NOT EXISTS idx_timetable_teacher ON timetable(teacher_username);
CREATE INDEX IF NOT EXISTS idx_timetable_class ON timetable(department, semester);

COMMIT;
