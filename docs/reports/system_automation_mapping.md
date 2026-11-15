# System test plan automation mapping

This mapping compares the current real-app Playwright suite with Appendix A in [`docs/test_plan.docx`](../test_plan.docx). It records only what the browser suite actually exercised in the 2026-11-15 run. Passing an automated subset does not pass the whole test case.

The suite used the isolated HTTPS Compose app and seeded PostgreSQL records. It did not use mock mode or alter seeded records. One uniquely generated temporary student was created, updated, and deleted through the UI; a scoped admin API cleanup in `finally` removes it if the UI journey fails. Result: **7/7 Playwright tests passed**. See [system_validation_report.md](system_validation_report.md) and the [HTML report](playwright-report/index.html). Test definitions are in [`web/e2e/real_app.spec.js`](../../web/e2e/real_app.spec.js).

## Functional and integration cases

| Plan case | Automation evidence | Status |
|---|---|---|
| TC-001 Authentication | Successful login for admin, teacher, student; empty form and unknown username behavior. Wrong password equivalence and five-failure lockout/reset not exercised. | Partial |
| TC-002 RBAC | Teacher account-list denial; student account/list and other-student denials; role-specific navigation visibility. No unknown-role test or complete endpoint matrix. | Partial |
| TC-003 Session token | Anonymous requests to `/api/me`, students, accounts rejected. Missing/malformed/tampered tokens and idle/absolute expiry not exercised. | Partial |
| TC-004 Logout | Student logout clears UI session; replay of the old token against `/api/me` returns 401. New-token uniqueness not checked. | Partial |
| TC-005 Create accounts | No account creation or post-create login. | Not run |
| TC-006 Modify/deactivate accounts | Account list is read only; no edits or deactivation. | Not run |
| TC-007 Add student | Admin creates a uniquely generated temporary student through the UI, then searches by its roll number and verifies the row. Duplicate, missing-field, invalid-date, and non-admin cases not tested. | Partial |
| TC-008 View student record | Admin reads seeded student; student is denied access to another student's record. Teacher broad access and 404 case not explicitly tested. | Partial |
| TC-009 Search students | Admin exact roll-number and partial-name search return seeded Aarav Sharma. No-match, student denial, and SQL metacharacters not tested. | Partial |
| TC-010 Update student | Admin updates the temporary student's name, department, semester, and contact through the UI and verifies rendered values. Roll-number immutability, invalid values, and non-admin attempt not tested. | Partial |
| TC-011 Delete student | UI confirmation is shown and confirmed; the temporary row disappears from search results. Cancel behavior and dependent-record cascade/orphan checks not tested. | Partial |
| TC-012 Add grade | No grade write or boundary/assignment validation. | Not run |
| TC-013 Update grade | No grade update. | Not run |
| TC-014 View grades | Admin and teacher read Aarav's seeded grades; student reads own grades; student is denied another student's grades. Empty-state case not tested. | Partial |
| TC-015 Record attendance | No attendance write or duplicate/assignment/date validation. | Not run |
| TC-016 Update attendance | No attendance update. | Not run |
| TC-017 View attendance | Teacher reads Aarav's seeded attendance; student reads own attendance and is denied another student's attendance. | Partial |
| TC-018 Attendance calculation | No calculation boundary or zero-session UI checks in this browser suite. | Not run here; see unit tests |
| TC-019 Create timetable slot | No timetable creation. | Not run |
| TC-020 View timetable | Teacher and student see seeded role-scoped timetable data; student request for another student's schedule is denied. Admin all-schedule view not checked. | Partial |
| TC-021 Timetable conflicts | No slot creation or conflict/concurrency checks. | Not run |
| TC-023 Semester filtering | Admin student-list and timetable filters and student's grade/attendance filters return selected-semester records only. Full UI coverage across all roles/features is not shown. | Partial |
| TC-INT-001 Login integration | Browser login succeeds for all three roles and navigation reflects role. Internal token/store chain is not separately instrumented. | Partial |
| TC-INT-002 Grade guard chain | No grade POST guard-chain/error-path test. | Not run |
| TC-INT-003 API/client error handling | Observed representative 200 reads, anonymous 401s, and role/ownership 403s. No 201, 400, 404, 409, 500, or client recovery matrix. | Partial |
| TC-INT-004 Database transaction | No fault injection or multi-table rollback test in browser run. | Not run |

## Non-functional and security cases

| Plan case | Automation evidence | Status |
|---|---|---|
| TC-NF-001 Search/list performance | No 1,000-student/10-course dataset or repeated timing measurement. | Not run |
| TC-NF-002 Restart/recovery | No service restart or interrupted-write check. | Not run |
| TC-NF-003 Form usability | Empty login displays inline validation. Other forms and destructive-action confirmations were not reviewed. | Partial |
| TC-NF-004 Maintainability/design review | Not a browser automation case; no review recorded here. | Not run |
| TC-NF-005 Invalid input/data integrity | No invalid write payloads or before/after database comparison. | Not run |
| TC-NF-006 Portability | One Linux Chromium environment only; no Windows or other browser run. | Not run |
| TC-NF-007 Concurrent clients/load | No concurrent-client workload. | Not run |
| TC-SEC-001 Unauthenticated mutations | Only unauthenticated GETs tested; no POST/PUT/DELETE matrix. | Not run |
| TC-SEC-002 Restricted deletion | No delete requests by non-admin roles. | Not run |
| TC-SEC-003 Malformed/injection input | No malformed, oversized, or injection payloads. | Not run |
| TC-SEC-004 RBAC matrix | Representative role and ownership reads only; no full endpoint × role × own/other matrix. | Partial |
| TC-SEC-005 Secret exposure in logs | Account API response checked not to expose password hash/salt/token fields; logs and failure output not inspected. | Partial |
| TC-SEC-006 Password storage | No database password-hash or salt inspection. | Not run |
| TC-SEC-007 TLS requirements | Suite connected over HTTPS with a test certificate. Protocol versions, plaintext rejection, and packet contents not checked. | Partial |
| TC-SEC-008 Session protection | Logout replay rejected; token entropy, idle-time replay, URL/log leakage not checked. | Partial |
| TC-SEC-009 Audit trail | Audit Logger is absent, so audit-event assertions cannot run. | **Blocked** |

## Current boundaries

- Screenshots are captured for the executed browser journeys; they document observed UI states, not unexecuted test cases.
- The suite reads seeded accounts and academic records. Its only write journey uses a unique temporary student and deletes it; cleanup targets only that generated roll number, and seed records remain unchanged.
- C++ unit, web unit, database-check, and CI results are separate evidence and are not counted as system cases by this mapping.
- Performance, load, restart/recovery, broad API permission/security matrices, Windows portability, ZAP/packet-capture checks, and the audit trail remain unverified here.
