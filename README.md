# Student Record System.

A web-based student record management system with role-based access for administrators, teachers, and students. The C++ server provides the REST API and serves the browser client from the same origin; PostgreSQL stores application data.

## Features

- **Admin:** Manage student records and user accounts; view system records.
- **Teacher:** View assigned subjects and students, record attendance, update grades, and view teaching schedules.
- **Student:** View their own profile, grades, attendance, and timetable.
- **Security:** Server-side validation, role-based authorization, salted password hashes, hashed session tokens, and mandatory TLS.

## Technology

- C++20, CMake, cpp-httplib, libpqxx, OpenSSL
- PostgreSQL 16
- HTML, CSS, and JavaScript ES modules
- Docker Compose for the application and database stack

## Run with Docker Compose

Docker Engine and the Docker Compose plugin are required. No host PostgreSQL server, `psql` client, C++ compiler, or Node.js installation is needed to run the application.

Copy the example environment file:

```sh
cp .env.example .env
```

### Development

Start the database, apply migrations, load development fixtures, build the server image, and start the application:

```sh
docker compose --profile dev up --force-recreate migrate
```

Open **https://localhost:8080**.

The development profile automatically generates a self-signed TLS certificate and stores it in a Docker volume. Your browser will display a certificate warning because the certificate is not issued by a trusted certificate authority. You can proceed for local development or explicitly configure your system to trust the certificate.

The application uses HTTPS. There is no plaintext HTTP mode.

Development accounts:

| Role | Display name | Username | Password |
|---|---|---|---|
| Admin | Anil Deshmukh | `admin` | `admin` |
| Teacher | Dr. Meera Nair | `teacher` | `teacher` |
| Teacher | Prof. Arjun Menon | `teacher2` | `teacher2` |
| Teacher | Dr. Neha Kulkarni | `neha.kulkarni` | `neha123` |
| Teacher | Prof. Rahul Verma | `rahul.verma` | `rahul123` |
| Student | Aarav Sharma | `student` | `student` |
| Student | Aditi Nair | `student2` | `student2` |

All accounts and records above are development fixtures. The seed preserves the original four usernames and passwords.

Stop the development stack:

```sh
docker compose --profile dev down
```

## Run tests

### Web tests

The web unit tests use Node.js's built-in test runner. From the repository root:

```sh
cd web
npm ci
npm test
```

### Backend tests

The C++ backend tests are built with CMake and run with CTest. Route tests use in-memory stores; the PostgreSQL store integration tests run when `SRS_TEST_DATABASE_URL` points to a disposable database with the project migrations applied. Without that variable, those integration tests are skipped.

The build requires:

- A C++20 toolchain
- CMake
- OpenSSL development files
- PostgreSQL client development files

```sh
cmake -S server -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

To run the PostgreSQL store integration tests, set `SRS_TEST_DATABASE_URL` to a disposable migrated test database when invoking CTest:

```sh
SRS_TEST_DATABASE_URL='postgresql://postgres:postgres@localhost:5432/srs_test' \
  ctest --test-dir build --output-on-failure
```

### Browser smoke tests

Install the web test dependencies required by `web/tests/smoke.mjs`, then run:

```sh
cd web
npm run test:smoke
```

### Playwright system tests

The Playwright suite exercises the HTTPS app against seeded PostgreSQL data. It creates, searches, updates, and deletes one uniquely named temporary student; the cleanup request targets only that record if a UI step fails. Run against an isolated test Compose database. The suite captures screenshots and writes its HTML report and artifacts under `docs/reports/`.

Start the development app and wait for the seed jobs to complete. Then, from `web/`, run:

```sh
npm ci
npx playwright install chromium
npm run test:e2e
```

See [web/e2e/README.md](web/e2e/README.md) for base URL, browser, and fixture credential settings.

## Repository layout

```text
server/       C++ REST API and HTTPS server
web/          Browser client (served by the C++ server)
db/
  migrations/ Ordered PostgreSQL migrations
  seeds/      Development-only sample data
docs/         API, security, requirements, architecture, and test documentation
.github/      CI workflows
```

## Project management and contribution

Project work is tracked in [Jira](https://stu-team-ls27bn2k.atlassian.net/jira/software/projects/SRS/summary).

See [CONTRIBUTING.md](CONTRIBUTING.md) for branch naming, commit messages, pull-request requirements, and code ownership.

## Security notes

See [docs/security.md](docs/security.md) for security controls and operational requirements.

Never expose development credentials, development fixtures, or self-signed development certificates in a production environment.
