# Unit Test Report

**Execution date:** 2026-11-15  
**Scope:** C++ route/unit tests, PostgreSQL store integration tests, and web client unit tests.

## Results

| Suite | Command | Result |
| --- | --- | --- |
| C++ server and PostgreSQL integration | `SRS_TEST_DATABASE_URL=postgresql://postgres:postgres@127.0.0.1:55432/srs ctest --test-dir build --output-on-failure` | **203/203 passed**, including 6 PostgreSQL integration tests |
| Web client | `npm test` from `web/` | **88/88 passed** |

The PostgreSQL tests ran against the isolated Compose development database seeded with project demo data. C++ route tests use fake stores; the six separately labeled integration tests exercise PostgreSQL stores directly. The Playwright suite changed session state for logout coverage but did not modify seeded academic records.

C++ tests cover password-hash, fail-closed default-store, profile-route, malformed-input, store-failure, and supported static-asset MIME cases.

The local build uses `-Wno-error=deprecated-declarations` to allow the vendored `httplib.h` OpenSSL deprecation warnings through `-Werror`. Those warnings remain visible. See [line coverage](code_coverage_report.md) and [branch coverage](branch_coverage_report.md) for measured coverage.
