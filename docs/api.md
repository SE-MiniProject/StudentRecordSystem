# API Contract

Wire contract between the Qt client and the C++ server. Endpoints and roles from SAD §4.4, status codes from §4.5, fields from SRS §4.3–4.7.

Changing this file requires a PR reviewed by both the client and server owners.

## Conventions

- Base path `/api`. JSON request and response bodies.
- `Authorization: Bearer <token>` on every endpoint except login.
- Timestamps ISO 8601 UTC: `2026-10-26T14:03:00Z`. Dates `YYYY-MM-DD`.
- Students are addressed by roll number, which is immutable after create (F-010).
- Unknown request fields are ignored.
- Timetable endpoints are not implemented.

## Errors

All errors:

```json
{
  "error": "validation_failed",
  "message": "grade must be between 0.0 and 10.0",
  "fields": { "grade": "out of range" },
  "requestId": "b1e4c9d2"
}
```

`fields` appears only on `400`. `requestId` appears on every error.

| Status | `error` | When |
| --- | --- | --- |
| 200 | none | success |
| 201 | none | created |
| 204 | none | success, no body |
| 400 | `validation_failed` | malformed body or failed validation |
| 401 | `unauthenticated` | missing, malformed, expired or revoked token |
| 403 | `forbidden` | role or ownership not permitted |
| 404 | `not_found` | no such record |
| 409 | `conflict` | duplicate roll number or duplicate session |
| 500 | `internal` | unhandled server fault |

`422` and `429` are not used.

## Auth

### `POST /api/auth/login` (public)

Request: `{ "username": "…", "password": "…" }`

Response `200`:

```json
{
  "token": "…",
  "expiresAt": "2026-10-26T22:03:00Z",
  "user": { "username": "a.shetty", "fullName": "Advitha Shetty", "role": "admin" }
}
```

Unknown username and wrong password both return `401 unauthenticated` with the same body (SR-008). Missing field returns `400`.

After 5 consecutive failed attempts the account is locked and further logins return the same `401 unauthenticated` body, including attempts with the correct password. A successful login clears the counter. See [Account lockout](#account-lockout) for the storage rule.

### `POST /api/auth/logout` (any authenticated)

Invalidates the presented token. Response `204`. A second call with the same token returns `401`.

### Tokens

Opaque random string. The server stores a hash of it, never the token. Absolute expiry in `expiresAt`, idle timeout enforced server-side (SR-008). Sent only in the `Authorization` header.

Sessions are persisted in a `sessions` table, so they survive a server restart. The server deletes expired rows on a periodic sweep and treats a row whose `expires_at` or `last_seen_at` has lapsed as invalid.

## Students

| Field | Type | Create | Update | Constraint |
| --- | --- | --- | --- | --- |
| `rollNo` | string | required | none | primary key, immutable |
| `fullName` | string | required | required | 1–120 chars |
| `dateOfBirth` | `YYYY-MM-DD` | required | none | must be in the past |
| `department` | string | required | required | e.g. `CSE-AIML` |
| `semester` | integer | required | required | 1–8 |
| `contact` | string | required | required | non-empty |
| `createdAt` | string | none | none | response only |
| `updatedAt` | string | none | none | response only |

### `GET /api/students` (Admin, Teacher)

Query: `search` (partial name or roll number, F-009), `semester` (F-023), `limit` (default 50, max 200), `offset` (default 0).

Both paging parameters are optional; omitting them returns the first page. `limit` above 200 returns `400`.

```json
{
  "items": [ { "rollNo": "…", "fullName": "…", "department": "…", "semester": 3, "contact": "…", "dateOfBirth": "2005-04-11" } ],
  "total": 42,
  "limit": 50,
  "offset": 0
}
```

No match returns `200` with `items: []`.

### `GET /api/students/{rollNo}` (Admin, Teacher, Student (self))

Response `200`: one student object.

Student requesting another roll number returns `403`.

### `POST /api/students` (Admin)

Body: create fields. Response `201` with the stored object.

Duplicate `rollNo` returns `409`. Invalid field returns `400` with `fields` (SR-003).

### `PUT /api/students/{rollNo}` (Admin)

Body: `fullName`, `department`, `semester`, `contact`. Omitted fields are unchanged. `rollNo` in the body is ignored.

Returns `404` if absent, `400` if a field is invalid.

### `DELETE /api/students/{rollNo}` (Admin)

Response `204`. Dependent grades and attendance are cascade-deleted (F-011). Hard delete. Returns `404` if absent.

## Grades

| Field | Type | Constraint |
| --- | --- | --- |
| `studentRollNo` | string | |
| `courseCode` | string | |
| `courseName` | string | response only |
| `grade` | number | 0.0–10.0 inclusive |
| `semester` | integer | |
| `updatedAt` | string | server-set on create and update (AC-013) |

### `GET /api/grades/{studentRollNo}` (Admin, Teacher, Student (self))

Query: `semester` (F-023).

```json
{
  "studentRollNo": "…",
  "items": [ { "courseCode": "UE23CS251B", "courseName": "DBMS", "grade": 8.5, "semester": 3, "updatedAt": "…" } ]
}
```

No aggregate or average is returned (F-012 requires no cross-subject averaging).

Student requesting another roll number returns `403`.

### `POST /api/grades` (Admin, Teacher)

Body: `studentRollNo`, `courseCode`, `grade`, `semester`.

Upserts on `(studentRollNo, courseCode, semester)`. Returns `201` when created, `200` when updated.

`grade` outside 0.0–10.0 returns `400` with `fields.grade`. Teacher not assigned to the course returns `403`. Unknown student returns `404`.

No delete endpoint.

## Attendance

| Field | Type | Constraint |
| --- | --- | --- |
| `studentRollNo` | string | |
| `courseCode` | string | |
| `date` | `YYYY-MM-DD` | not in the future |
| `status` | string | `present` or `absent` |
| `semester` | integer | |

### `POST /api/attendance` (Admin, Teacher)

Body: the fields above.

Upserts on `(studentRollNo, courseCode, date)`, satisfying F-015 and F-016 with one endpoint. Returns `201` when created, `200` when updated.

Invalid `status` or a future `date` returns `400`.

### `GET /api/attendance/{studentRollNo}` (Admin, Teacher, Student (self))

Query: `courseCode` (optional), `semester` (F-023).

```json
{
  "studentRollNo": "…",
  "items": [ { "courseCode": "UE23CS251B", "conducted": 40, "present": 34, "percentage": 85.00 } ],
  "overall": { "conducted": 120, "present": 101, "percentage": 84.17 }
}
```

`percentage` is `present / conducted * 100` rounded to two decimals, computed server-side (F-018). `conducted` counts sessions and `present` counts sessions with `status = 'present'`, so `present > conducted` cannot occur.

Student requesting another roll number returns `403`.

## Accounts

### `GET /api/accounts` (Admin)

```json
{
  "items": [ { "username": "a.shetty", "fullName": "Advitha Shetty", "role": "teacher", "active": true } ],
  "total": 12
}
```

Never returns password hashes, salts or tokens.

### `POST /api/accounts` (Admin)

Body: `username`, `password`, `fullName`, `role` (`admin` | `teacher` | `student`).

Returns `201`. Duplicate username returns `409` (AC-005). Password policy failure returns `400` with `fields.password`.

### `PATCH /api/accounts/{username}` (Admin)

Body, all optional: `fullName`, `role`, `active`, `password`.

Returns `200` with the updated account, `404` if absent. Setting `active: false` revokes the user's sessions and blocks login (F-006).

## Timetable (not implemented)

| Endpoint | Role |
| --- | --- |
| `GET /api/timetable/{studentRollNo}` | Admin, Teacher, Student (self) |
| `POST /api/timetable` | Admin |

TC-019, TC-020 and TC-021 are marked not implemented in the test plan.

## Decisions

Settled 26 Oct 2026. Do not reopen without a PR reviewed by both the client and server owners.

### Account to student link

A `student`-role account links through a nullable `users.roll_no` referencing `students.roll_no`, `UNIQUE` so one account maps to at most one student. Admin and teacher accounts leave it `NULL`.

The FK is added in the students migration, not in `001`, so the user foundation does not depend on a table that does not exist yet.

### Table: users

Created in `001_roles_users.sql`.

| Column | Type | Constraint |
| --- | --- | --- |
| `username` | `text` | primary key, 3-64 chars, lowercase alphanumeric plus `.` `_` `-` |
| `full_name` | `text` | not null, 1-120 chars |
| `role` | `text` | not null, `check (role in ('admin','teacher','student'))` |
| `password_hash` | `text` | not null, 64 hex chars |
| `password_salt` | `text` | not null, 32 hex chars |
| `password_algo` | `text` | not null, default `'sha256'`, allows a later migration to `'argon2id'` without a schema change |
| `active` | `boolean` | not null, default `true` |
| `failed_attempts` | `integer` | not null, default `0`, `check (failed_attempts >= 0)` |
| `locked_until` | `timestamptz` | nullable; non-null means login is refused until this instant |
| `created_at` | `timestamptz` | not null, default `now()` |
| `updated_at` | `timestamptz` | not null, default `now()` |
| `roll_no` | `text` | nullable, `references students(roll_no) on delete cascade`, `unique` |

Hash column rules (SR-003, SR-006):

- `password_hash` and `password_salt` are separate columns, not one `salt$hash` string.
- `password_algo` records the algorithm per row. Changing it is a data migration, not a schema change.
- The default is SHA-256 with a random 16-byte salt. Argon2id can be adopted by setting `password_algo` and rehashing on next login.
- No plaintext password column exists, and no password or hash is logged.

`roll_no` is added by `002_students.sql`:

```sql
alter table users
  add column roll_no text unique
  references students(roll_no) on delete cascade;
```

### Table: sessions

Created in `001_roles_users.sql`, after `users`.

| Column | Type | Constraint |
| --- | --- | --- |
| `token_hash` | `text` | primary key, sha256 hex of the bearer token |
| `username` | `text` | not null, `references users(username) on delete cascade` |
| `issued_at` | `timestamptz` | not null, default `now()` |
| `last_seen_at` | `timestamptz` | not null, default `now()` |
| `expires_at` | `timestamptz` | not null |

```sql
create table sessions (
  token_hash   text primary key,
  username     text not null references users(username) on delete cascade,
  issued_at    timestamptz not null default now(),
  last_seen_at timestamptz not null default now(),
  expires_at   timestamptz not null
);

create index sessions_username_idx on sessions (username);
create index sessions_expires_at_idx on sessions (expires_at);
```

`username` is indexed for logout-all and cascade on account deactivation. `expires_at` is indexed for the sweep. Idle timeout is `last_seen_at`, absolute expiry is `expires_at` (SR-008). Expired rows are treated as invalid even before the sweep deletes them.

Logout deletes the row. Deactivating an account (`active = false`) deletes all its rows, which is what makes the deactivation immediate rather than advisory.

### Account lockout


- `failed_attempts` counts consecutive failures. A wrong password increments it; a successful login resets it to `0`.
- When the increment reaches `5`, the same write sets `locked_until = now() + 15 minutes` and the attempt is refused.
- While `locked_until` is in the future, login is refused **before** the password is checked, so a correct password does not get through and does not reset the counter.
- Lockout expiry is lazy: a request whose `locked_until` has passed clears `failed_attempts` and `locked_until` and proceeds normally. No scheduled job is needed.
- `active = false` is separate from lockout and is not cleared by time; it is the admin action in `PATCH /api/users/{username}` (F-006).
- Every lockout response is the generic `401 unauthenticated` body (SR-008). The client is not told whether the account is locked, because that would reveal the username exists.
- The counter is per-account, not per-IP. Per-IP rate limiting is out of scope for this story.

Both columns live in `001_roles_users.sql`, so `001` carries the whole login contract and the login route needs no follow-up migration.

### Grades aggregate

Not returned. The grades response carries the item list only.

### Pagination

`GET /api/students` is paginated. `limit` defaults to 50 and is capped at 200; `offset` defaults to 0. The response carries `total` so the client can size the view. 1,000 seeded students (NF-001) are therefore returned in 20 pages at the default, not one.
