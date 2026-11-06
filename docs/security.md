# Security Controls

## Transport security (SRS-29 / SR-007)

The server starts only as a TLS listener. It requires both environment variables:

- `SRS_TLS_CERT_FILE`: PEM-encoded server certificate (and any required chain).
- `SRS_TLS_KEY_FILE`: PEM-encoded private key matching that certificate.

If either variable is absent, the files cannot be read, or the certificate/key
pair is invalid, startup fails and no listener is opened. There is no plaintext
HTTP fallback. The vendored cpp-httplib SSL server enforces TLS 1.2 as its
minimum protocol version; TLS 1.0 and 1.1 handshakes are rejected.

Terminate TLS at this server or at a trusted ingress that is configured to
reject plaintext client connections and protect the ingress-to-server hop.
Do not expose an unencrypted backend listener to an untrusted network.

For local development, create a self-signed certificate and key, then export
`SRS_TLS_CERT_FILE` and `SRS_TLS_KEY_FILE` before starting `srs_server`.
Browsers and API clients must trust the certificate; do not disable certificate
verification in production.

## Credential storage and logging (SR-003 / SR-005 / SR-006)

Passwords are stored as a salted SHA-256 digest using a cryptographically
random 16-byte per-account salt. The algorithm is recorded per row in
`users.password_algo`; there is no plaintext password column. Session tokens
are random 32-byte values; only their SHA-256 digests are persisted. Route and
startup diagnostics must not include request bodies, passwords, authorization
headers, bearer tokens, password salts, or password digests.

## Mutation authorization and validation (SR-001 / SR-003)

Every record-changing route must authenticate and apply its role policy before
parsing or persisting user-controlled data. Server-side validators remain the
security boundary; client-side validation is only a usability feature. A
validation failure must return before the store operation, so no partial record
change occurs.
