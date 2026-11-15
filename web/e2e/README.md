# Playwright system tests

The suite runs the web client and API against the HTTPS development server backed by seeded PostgreSQL data. It reads seeded records and creates one uniquely named temporary student for the admin CRUD journey. The UI creates, searches, updates, and deletes that student; a scoped cleanup request deletes only that generated roll number if a UI step fails. Seeded records remain unchanged. Each authenticated browser session logs out during teardown.

Run the suite against an isolated test Compose database. From `web/`, start the development app and wait for its seed jobs to complete, then run:

```sh
npm ci
npx playwright install chromium
npm run test:e2e
```

The default base URL is `https://localhost:8080`. Set `SRS_E2E_BASE_URL` to target another running test app. The development certificate is self-signed, and Playwright accepts it for these tests.

By default, the tests use the development fixture credentials listed in the root README. Set `SRS_E2E_ADMIN_USERNAME` / `SRS_E2E_ADMIN_PASSWORD`, `SRS_E2E_TEACHER_USERNAME` / `SRS_E2E_TEACHER_PASSWORD`, and `SRS_E2E_STUDENT_USERNAME` / `SRS_E2E_STUDENT_PASSWORD` to override them. Set `SRS_E2E_CHROMIUM` to use a system-installed Chromium binary.

Each test captures screenshots. Playwright writes the HTML report and screenshot/trace artifacts to `docs/reports/playwright-report/` and `docs/reports/playwright-artifacts/`.
