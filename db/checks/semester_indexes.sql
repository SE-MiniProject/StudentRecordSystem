-- Schema assertions for SRS-28 semester-filter indexes.
DO $$
BEGIN
    IF NOT EXISTS (
        SELECT 1
        FROM pg_indexes
        WHERE schemaname = current_schema()
          AND indexname = 'idx_students_semester_roll_no'
          AND indexdef ILIKE '%(semester, roll_no)%'
    ) THEN
        RAISE EXCEPTION 'missing or incorrect idx_students_semester_roll_no';
    END IF;

    IF NOT EXISTS (
        SELECT 1
        FROM pg_indexes
        WHERE schemaname = current_schema()
          AND indexname = 'idx_subjects_semester_code'
          AND indexdef ILIKE '%(semester, code)%'
    ) THEN
        RAISE EXCEPTION 'missing or incorrect idx_subjects_semester_code';
    END IF;

    IF NOT EXISTS (
        SELECT 1
        FROM pg_indexes
        WHERE schemaname = current_schema()
          AND indexname = 'idx_timetable_class'
          AND indexdef ILIKE '%(department, semester)%'
    ) THEN
        RAISE EXCEPTION 'missing or incorrect idx_timetable_class';
    END IF;
END;
$$;
