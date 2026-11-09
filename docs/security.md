# Security Controls

This page records controls implemented in the repository and operational requirements for deployment. It does not claim an independent security audit.

## Transport security

The server starts only as a TLS listener. It requires `SRS_TLS_CERT_FILE` and `SRS_TLS_KEY_FILE`, pointing to a readable PEM certificate and its matching private key. Missing or invalid credentials prevent startup; there is no plaintext HTTP fallback. The vendored cpp-httplib server requires TLS 1.2 or later.

For local development, Compose creates a self-signed certificate in the `dev_tls` volume and publishes HTTPS on port 8080 by default. Clients must trust that certificate for local use. Production deployments must use a trusted certificate and protect any ingress-to-server connection; do not expose an unencrypted backend listener to an untrusted network.

## Authentication and credentials

Passwords currently use a per-account random 16-byte salt and SHA-256 digest. The algorithm is recorded in `users.password_algo`. This is an implementation fact, not an adequate recommendation for new production deployments: replace it with a memory-hard password hashing scheme such as Argon2id, scrypt, or bcrypt before production use.

Bearer tokens are random 32-byte values. Only their SHA-256 digests are persisted. Sessions have an eight-hour absolute expiry and a 30-minute idle timeout. Logout revokes the session; account deactivation revokes the account's sessions. Five consecutive failed logins trigger a 15-minute account lockout. Unknown usernames and incorrect passwords return the same generic authentication response.

## Authorization and validation

Every protected operation authenticates the bearer session and applies its role or ownership rule on the server. Hiding an action in the browser is only a usability measure. Student record reads resolve student ownership from the authenticated account; a client-supplied roll number does not establish authorization.

Server-side validators enforce field limits and domain constraints before persistence. PostgreSQL constraints and foreign keys provide a second integrity boundary. SQL operations use parameterized libpqxx APIs.

## Sensitive data and logging

Account list responses omit password hashes, salts, and session tokens. Avoid logging request bodies, passwords, authorization headers, bearer tokens, salts, or password digests. Review operational logs and error handling whenever new routes or diagnostics are added.

The browser stores its session token in `localStorage` so a page reload can retain login state. Any script executing in the application origin could read that token; keep dependencies and served assets controlled and deploy suitable content-security protections.

## Deployment checklist

- Set `DATABASE_URL` and TLS certificate/key paths through a secrets manager or protected runtime configuration.
- Replace development credentials and never load demo seeds in production.
- Use trusted TLS certificates and verify certificates in clients.
- Restrict database network access and use least-privilege database credentials.
- Replace salted SHA-256 password hashing before production.
- Add and verify suitable security headers, including a content security policy, at the server or trusted ingress.
- Monitor login failures and operational health without recording secrets.
