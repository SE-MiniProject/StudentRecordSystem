-- 005_timetable_conflicts.sql
-- SRS-F-019 / F-021: index timetable overlap lookups and remove the old
-- teacher/start-only uniqueness rule. The API serializes timetable writers
-- with a transaction-scoped advisory lock before checking interval overlap.
-- Keep a natural-key index so the demo seed remains idempotent.

BEGIN;

ALTER TABLE timetable
    DROP CONSTRAINT IF EXISTS timetable_teacher_username_day_of_week_start_time_key;

CREATE UNIQUE INDEX IF NOT EXISTS uq_timetable_slot_identity
    ON timetable (subject_code, teacher_username, department, semester,
                  day_of_week, start_time, end_time, room);

CREATE INDEX IF NOT EXISTS idx_timetable_teacher_day_semester_overlap
    ON timetable (teacher_username, day_of_week, semester, start_time, end_time);

CREATE INDEX IF NOT EXISTS idx_timetable_room_day_semester_overlap
    ON timetable (room, day_of_week, semester, start_time, end_time);

COMMIT;
