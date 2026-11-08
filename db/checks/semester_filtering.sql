-- Database-level regression checks for SRS-28.
-- Run after demo_data.sql and demo_records.sql have been applied.

BEGIN;

-- Add one semester-3 attendance row so both sides of the filter have data.
INSERT INTO attendance (roll_no, subject_code, session_date, present)
VALUES ('1ABC26CS001', 'CS301', DATE '2026-11-01', true)
ON CONFLICT (roll_no, subject_code, session_date) DO NOTHING;

DO $$
DECLARE
    actual_count BIGINT;
BEGIN
    SELECT count(*) INTO actual_count
    FROM students
    WHERE roll_no = '1ABC26CS001' AND semester = 5;
    IF actual_count <> 1 THEN
        RAISE EXCEPTION 'student semester 5 filter expected 1 row, got %', actual_count;
    END IF;

    SELECT count(*) INTO actual_count
    FROM students
    WHERE roll_no = '1ABC26CS001' AND semester = 3;
    IF actual_count <> 0 THEN
        RAISE EXCEPTION 'student semester 3 filter expected 0 rows, got %', actual_count;
    END IF;

    SELECT count(*) INTO actual_count
    FROM grades g
    JOIN subjects s ON s.code = g.subject_code
    WHERE g.roll_no = '1ABC26CS001' AND s.semester = 5;
    IF actual_count <> 3 THEN
        RAISE EXCEPTION 'grades semester 5 filter expected 3 rows, got %', actual_count;
    END IF;

    SELECT count(*) INTO actual_count
    FROM grades g
    JOIN subjects s ON s.code = g.subject_code
    WHERE g.roll_no = '1ABC26CS001' AND s.semester = 3;
    IF actual_count <> 1 THEN
        RAISE EXCEPTION 'grades semester 3 filter expected 1 row, got %', actual_count;
    END IF;

    SELECT count(*) INTO actual_count
    FROM attendance a
    JOIN subjects s ON s.code = a.subject_code
    WHERE a.roll_no = '1ABC26CS001' AND s.semester = 5;
    IF actual_count <> 8 THEN
        RAISE EXCEPTION 'attendance semester 5 filter expected 8 rows, got %', actual_count;
    END IF;

    SELECT count(*) INTO actual_count
    FROM attendance a
    JOIN subjects s ON s.code = a.subject_code
    WHERE a.roll_no = '1ABC26CS001' AND s.semester = 3;
    IF actual_count <> 1 THEN
        RAISE EXCEPTION 'attendance semester 3 filter expected 1 row, got %', actual_count;
    END IF;

    SELECT count(*) INTO actual_count
    FROM timetable
    WHERE department = 'Computer Science' AND semester = 5;
    IF actual_count <> 6 THEN
        RAISE EXCEPTION 'timetable semester 5 filter expected 6 rows, got %', actual_count;
    END IF;

    SELECT count(*) INTO actual_count
    FROM timetable
    WHERE department = 'Computer Science' AND semester = 3;
    IF actual_count <> 2 THEN
        RAISE EXCEPTION 'timetable semester 3 filter expected 2 rows, got %', actual_count;
    END IF;
END;
$$;

ROLLBACK;
