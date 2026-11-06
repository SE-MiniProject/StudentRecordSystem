# Student Record System

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

| Role | Username | Password |
|---|---|---|
| Admin | `admin` | `admin` |
| Teacher | `teacher` | `teacher` |
| Teacher | `teacher2` | `teacher2` |
| Student | `student` | `student` |

Stop the development stack:

```sh
docker compose --profile dev down
```

## Run tests

### Web tests

The web unit tests use Node.js's built-in test runner:

```sh
cd web
npm test
```

### Backend tests

The backend test suite is built with CMake and run with CTest. It requires:

- A C++20 toolchain
- CMake
- OpenSSL development files
- PostgreSQL client development files
- A reachable PostgreSQL database configured through `DATABASE_URL`

The backend tests also require the database fixtures used by the test suite.

```sh
cmake -S server -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

### Browser smoke tests

Install the web test dependencies required by `web/tests/smoke.mjs`, then run:

```sh
cd web
npm run test:smoke
```

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
