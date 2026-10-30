-- 001_roles_users.sql
--
-- The login contract's foundation: accounts, their password digests, the lockout
-- counters, and the session table. Specified in docs/api.md ("Table: users",
-- "Table: sessions", "Account lockout"); this file must not diverge from it.
--
-- users.roll_no is deliberately absent. It references students(roll_no), and the
-- students table is created by 002_students.sql, so the column is added there.
-- Keeping the user foundation free of that dependency is what lets this
-- migration stand on its own.

BEGIN;

CREATE TABLE IF NOT EXISTS users (
    username        TEXT PRIMARY KEY
                    CHECK (username ~ '^[a-z0-9._-]{3,64}$'),
    full_name       TEXT NOT NULL
                    CHECK (length(full_name) BETWEEN 1 AND 120
                           AND length(trim(full_name)) > 0),
    role            TEXT NOT NULL
                    CHECK (role IN ('admin', 'teacher', 'student')),

    -- Separate columns rather than one salt$hash string: the digest is
    -- unverifiable without its salt and uninterpretable without the algorithm,
    -- and each needs its own check (SR-003, SR-006).
    password_hash   TEXT NOT NULL CHECK (password_hash ~ '^[0-9a-f]{64}$'),
    password_salt   TEXT NOT NULL CHECK (password_salt ~ '^[0-9a-f]{32}$'),
    -- Per-row algorithm so adopting argon2id is a data migration, not a schema
    -- change.
    password_algo   TEXT NOT NULL DEFAULT 'sha256',

    active          BOOLEAN NOT NULL DEFAULT true,

    -- Lockout state lives with the account because the policy is per-account,
    -- not per-IP (see api.md "Account lockout").
    failed_attempts INTEGER NOT NULL DEFAULT 0 CHECK (failed_attempts >= 0),
    locked_until    TIMESTAMPTZ,  -- non-null means login is refused until then

    created_at      TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- Sessions are persisted so they survive a restart. Only the hash of the bearer
-- token is stored: a database dump must not yield usable credentials.
CREATE TABLE IF NOT EXISTS sessions (
    token_hash   TEXT PRIMARY KEY CHECK (token_hash ~ '^[0-9a-f]{64}$'),
    username     TEXT NOT NULL REFERENCES users(username) ON DELETE CASCADE,
    issued_at    TIMESTAMPTZ NOT NULL DEFAULT now(),
    last_seen_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    expires_at   TIMESTAMPTZ NOT NULL
);

-- username: logout-all and the cascade on deactivation.
-- expires_at: the periodic sweep.
CREATE INDEX IF NOT EXISTS sessions_username_idx ON sessions (username);
CREATE INDEX IF NOT EXISTS sessions_expires_at_idx ON sessions (expires_at);

COMMIT;