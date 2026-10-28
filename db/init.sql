BEGIN;

CREATE TABLE IF NOT EXISTS students (
    id BIGSERIAL UNIQUE NOT NULL,
    roll_no VARCHAR(32) PRIMARY KEY,
    name VARCHAR(120) NOT NULL CHECK (length(trim(name)) > 0),
    date_of_birth DATE,
    department VARCHAR(100) NOT NULL CHECK (length(trim(department)) > 0),
    semester INTEGER NOT NULL CHECK (semester BETWEEN 1 AND 12),
    contact_details VARCHAR(255) NOT NULL CHECK (length(trim(contact_details)) > 0),
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS grades (
    id BIGSERIAL PRIMARY KEY,
    roll_no VARCHAR(32) NOT NULL REFERENCES students(roll_no) ON DELETE CASCADE,
    subject_code VARCHAR(32) NOT NULL,
    score NUMERIC(3,1) NOT NULL CHECK (score >= 0 AND score <= 10)
);

CREATE TABLE IF NOT EXISTS attendance (
    id BIGSERIAL PRIMARY KEY,
    roll_no VARCHAR(32) NOT NULL REFERENCES students(roll_no) ON DELETE CASCADE,
    subject_code VARCHAR(32) NOT NULL,
    session_date DATE NOT NULL,
    present BOOLEAN NOT NULL DEFAULT false
);

CREATE INDEX IF NOT EXISTS idx_grades_roll_no ON grades(roll_no);
CREATE INDEX IF NOT EXISTS idx_attendance_roll_no ON attendance(roll_no);

COMMIT;
