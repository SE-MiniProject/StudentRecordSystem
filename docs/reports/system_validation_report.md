# System test execution report

**Execution:** 2026-11-15  
**Environment:** isolated Docker Compose development stack, HTTPS at `https://localhost:8080`, seeded PostgreSQL data  
**Result:** 7 Playwright tests passed; 0 failed; 0 skipped.

The browser tests exercised the real served web client and API. They did not enable `?mock=1` or rely on fake stores. One CRUD journey created a uniquely generated temporary student, verified search and UI updates, then deleted it through the UI; an admin API cleanup in `finally` targets only that generated roll number if any UI step fails. No seeded records were changed. The suite logged out authenticated sessions. It also verified successful admin, teacher, and student sign-in; generic unknown-account and empty-form behavior; logout invalidation; seeded student, account, grade, attendance, and timetable reads; semester filters; unauthenticated rejection; and representative role and student-ownership denials.

Screenshots and the Playwright HTML report are in [playwright-artifacts](playwright-artifacts/) and [playwright-report](playwright-report/index.html). The test-to-plan case mapping, including partial and unrun cases, is in [system_automation_mapping.md](system_automation_mapping.md).

This run is partial system-level evidence, not completion of the test plan. In particular, most write flows, exhaustive permission matrices, security scans, performance/load, restart recovery, portability, and formal usability/design reviews were not executed. TC-SEC-009 remains blocked because the Audit Logger is absent. See the mapping before treating any test-plan case as passed.
