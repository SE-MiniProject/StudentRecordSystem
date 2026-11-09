# API Contract

This document describes the routes registered by `server/src/api/router.cpp` and their handlers. The server also serves the browser client and exposes `GET /health`. All API routes use JSON unless a route has no response body. The API is hosted over HTTPS; there is no plaintext HTTP listener.

## Conventions and errors

Protected routes require `Authorization: Bearer <token>`. `POST /api/auth/login` is public. Dates use `YYYY-MM-DD`; timestamps are UTC ISO 8601. Student records are addressed by immutable `rollNo`.

Most errors use this envelope:

```json
{
  "error": "validation_failed",
  "message": "invalid request",
  "requestId": "request-id",
  "fields": { "grade": "must be in range" }
}
```

`fields` is included when applicable. Some route-specific validation responses use an equivalent nested error shape; clients should use the HTTP status and message and avoid relying on every route having identical field formatting. Common statuses are `200` success, `201` created, `204` success without a body, `400` malformed/invalid request, `401` unauthenticated, `403` forbidden, `404` missing resource, `409` conflict, and `500` internal failure.

## Authentication and profile

| Method and path | Access | Behavior |
|---|---|---|
| `POST /api/auth/login` | Public | Accepts `username`, `password`; returns bearer `token`, `expiresAt`, and user identity on success. Invalid credentials return generic `401`. |
| `POST /api/auth/logout` | Authenticated | Revokes the presented session; success is `204`. |
| `GET /api/me` | Authenticated | Returns caller's username, full name, role, and linked `rollNo` (null when not linked to a student). |

Sessions are stored in PostgreSQL by token digest, with an eight-hour absolute lifetime and a 30-minute idle timeout. Five consecutive failures lock an account for 15 minutes. Successful login clears the failed-attempt counter.

## Students

| Method and path | Access | Behavior |
|---|---|---|
| `GET /api/students?search=&semester=&limit=&offset=` | Admin, Teacher | Search partial name/roll number; optional semester; limit defaults to 50 (maximum 200), offset defaults to 0. Returns `{items,total,limit,offset}`. |
| `GET /api/students/{rollNo}` | Admin, Teacher, Student (self) | Returns one student; a student asking for another roll number is forbidden. |
| `POST /api/students` | Admin | Creates a student; duplicate roll number returns conflict. |
| `PUT /api/students/{rollNo}` | Admin | Updates supported mutable fields; roll number is immutable. |
| `DELETE /api/students/{rollNo}` | Admin | Deletes student and dependent records through database cascades; returns `204` on success. |

## Accounts

| Method and path | Access | Behavior |
|---|---|---|
| `GET /api/accounts` | Admin | Lists username, full name, role, and active state; never returns password material or tokens. |
| `POST /api/accounts` | Admin | Creates an account from `username`, `password`, `fullName`, and `role`. Duplicate username returns `409`. |
| `PATCH /api/accounts/{username}` | Admin | Supports `fullName`, `role`, `active`, and `password` changes. Deactivation revokes active sessions. |

## Grades

| Method and path | Access | Behavior |
|---|---|---|
| `GET /api/grades/{rollNo}?semester=` | Admin, Teacher, Student (self) | Returns `{studentRollNo,items}` with course code/name, grade, semester, and update timestamp. Teacher results are limited to assigned subjects. No aggregate average is returned. |
| `POST /api/grades` | Teacher | Accepts `studentRollNo`, `courseCode`, and `grade`. Teacher identity comes from the session and must be assigned to the subject. Creates (`201`) or replaces (`200`) the grade. |

Grades are from 0.0 to 10.0 inclusive in increments of 0.1. Admin and Student accounts cannot submit grades.

## Attendance

| Method and path | Access | Behavior |
|---|---|---|
| `GET /api/attendance/{rollNo}?courseCode=&semester=` | Admin, Teacher, Student (self) | Returns per-course counts and percentages plus an overall count/percentage. Teacher results include assigned courses only; students see their own records. |
| `POST /api/attendance` | Teacher | Accepts `rollNo`, `courseCode`, `sessionDate`, and `status` (`present` or `absent`). Creates a record and returns `201`; duplicate key returns `409`. |
| `PUT /api/attendance/{rollNo}/{courseCode}/{sessionDate}` | Teacher | Corrects the status of an existing entry; returns `200`, or `404` if absent. Teacher must be assigned to the course. |

A future or invalid date is rejected. Percentages are calculated from integer counts, rounded to two decimals; when no sessions exist the percentage is `null`. The overall percentage uses summed counts, not an average of course percentages.

## Timetable

A slot is a weekly class with ISO weekday (1 Monday through 7 Sunday), start/end time, course, teacher, department, semester, and room.

| Method and path | Access | Behavior |
|---|---|---|
| `POST /api/timetable` | Admin | Creates a slot after field, assignment, and database conflict checks. Returns `201`; a conflict returns `409`. |
| `GET /api/timetable?teacher=&department=&semester=` | Authenticated | Admin can filter all slots; Teacher sees their own slots; Student sees their own class timetable. Cross-scope filters are forbidden. Response includes the resolved `scope` and `items`. |
| `GET /api/timetable/{rollNo}` | Admin, Student (self) | Returns the class timetable for that student. Teachers cannot use this route; they use the unparameterized route for their own schedule. |

Slot creation uses a transaction-scoped advisory lock and checks interval overlap for the teacher or room on the same day and semester. Database constraints also enforce end-after-start and assigned teacher/course relationships. Room conflicts across different semesters are not checked by the current overlap query.

## Health

`GET /health` is unauthenticated and returns a small JSON health payload containing status, service name, and version. The web client is served from the same origin after API routes are registered.
