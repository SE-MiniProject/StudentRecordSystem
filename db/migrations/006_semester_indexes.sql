-- 006_semester_indexes.sql
--
-- Indexes for semester-scoped student and subject lookups (SRS-28).
-- Grades and attendance derive their semester through subjects.semester; they
-- intentionally do not duplicate semester on every record. Timetable already
-- has idx_timetable_class(department, semester) from migration 004.
--
-- Re-runnable so the complete migration chain can safely be applied again.

BEGIN;

-- Supports semester filtering while preserving the roll-number ordering used
-- by the paginated student list.
CREATE INDEX IF NOT EXISTS idx_students_semester_roll_no
    ON students(semester, roll_no);

-- Supports semester-scoped subject lookups and includes the subject key used
-- to join grades and attendance to their semester.
CREATE INDEX IF NOT EXISTS idx_subjects_semester_code
    ON subjects(semester, code);

COMMIT;
