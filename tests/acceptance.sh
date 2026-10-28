#!/usr/bin/env bash
set -euo pipefail

BASE="${SRS_API_BASE:-http://127.0.0.1:8080}"
ADMIN="${ADMIN_TOKEN:-local-admin-token-change-me}"
TEACHER="${TEACHER_TOKEN:-local-teacher-token-change-me}"
DB_URL="${DATABASE_URL:-postgresql://srs_user:change-this-local-password@localhost:5432/student_record_system}"

admin=(-H "Authorization: Bearer ${ADMIN}" -H 'Content-Type: application/json')
teacher=(-H "Authorization: Bearer ${TEACHER}" -H 'Content-Type: application/json')
tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT

# A test student is inserted directly for a repeatable story-level test.
psql "$DB_URL" -v ON_ERROR_STOP=1 <<'SQL'
DELETE FROM students WHERE roll_no = 'TEST-UPDATE-DELETE-001';
INSERT INTO students(roll_no, name, date_of_birth, department, semester, contact_details)
VALUES ('TEST-UPDATE-DELETE-001', 'Before Update', '2004-01-02', 'CSE', 2, 'before@example.edu');
INSERT INTO grades(roll_no, subject_code, score)
VALUES ('TEST-UPDATE-DELETE-001', 'CS101', 8.5);
INSERT INTO attendance(roll_no, subject_code, session_date, present)
VALUES ('TEST-UPDATE-DELETE-001', 'CS101', CURRENT_DATE, true);
SQL

ID="$(psql "$DB_URL" -Atc "SELECT id FROM students WHERE roll_no='TEST-UPDATE-DELETE-001'")"

status="$(curl -sS -o "$tmp" -w '%{http_code}' "${admin[@]}" -X PUT "$BASE/api/students/$ID" \
  -d '{"name":"After Update","contact_details":"after@example.edu","department":"AI-ML","semester":3}')"
[[ "$status" == "200" ]] || { echo "FAIL: valid update status=$status body=$(cat "$tmp")"; exit 1; }
psql "$DB_URL" -Atc "SELECT name || '|' || contact_details || '|' || department || '|' || semester FROM students WHERE id=$ID" | grep -qx 'After Update|after@example.edu|AI-ML|3'
echo "PASS: updated values persisted"

status="$(curl -sS -o "$tmp" -w '%{http_code}' "${admin[@]}" -X PUT "$BASE/api/students/$ID" \
  -d '{"roll_no":"MUTATED-ROLL","name":"Should Not Save","contact_details":"x@y.z","department":"CSE","semester":4}')"
[[ "$status" == "400" ]] || { echo "FAIL: roll_no mutation status=$status body=$(cat "$tmp")"; exit 1; }
[[ "$(psql "$DB_URL" -Atc "SELECT roll_no FROM students WHERE id=$ID")" == "TEST-UPDATE-DELETE-001" ]]
echo "PASS: roll number cannot be modified"

status="$(curl -sS -o "$tmp" -w '%{http_code}' "${teacher[@]}" -X DELETE "$BASE/api/students/$ID")"
[[ "$status" == "403" ]] || { echo "FAIL: non-admin delete status=$status body=$(cat "$tmp")"; exit 1; }
echo "PASS: non-Administrator delete refused server-side"

status="$(curl -sS -o "$tmp" -w '%{http_code}' "${admin[@]}" -X DELETE "$BASE/api/students/$ID")"
[[ "$status" == "200" ]] || { echo "FAIL: admin delete status=$status body=$(cat "$tmp")"; exit 1; }
[[ "$(psql "$DB_URL" -Atc "SELECT count(*) FROM grades WHERE roll_no='TEST-UPDATE-DELETE-001'")" == "0" ]]
[[ "$(psql "$DB_URL" -Atc "SELECT count(*) FROM attendance WHERE roll_no='TEST-UPDATE-DELETE-001'")" == "0" ]]
echo "PASS: Administrator delete cascades to grades and attendance"
echo "All acceptance checks passed."
