// Talks to the server. All HTTP lives here so screens never build requests.
//
// A port of client/src/apiclient/ApiClient.cpp. The contract in docs/api.md is
// unchanged: same paths, same verbs, same JSON, same status codes. What changed
// is only the transport (fetch instead of QNetworkAccessManager) and the fact
// that every endpoint in the contract is covered, not just login and search.
//
// Error mapping is deliberately identical to the Qt client's, because the Qt
// client's wording was chosen to satisfy specific requirements and the tests
// assert on it:
//
//   status 0   -> "Cannot reach the server. Is it running?"   (fetch threw)
//   400 login  -> the shared missing-credentials sentence
//   401 login  -> one generic sentence (SR-008: unknown user, wrong password
//                 and lockout must be indistinguishable)
//   401 other  -> session expired
//   403        -> permission denied, worded per resource
//   else       -> "Something went wrong (code N). Ref: <requestId>"

import { isValidSession, makeSession, clearSession } from './session.js';

// One sentence, one place: the login screen pre-checks the same condition the
// server answers 400 for, so both paths must say the same thing.
export const MISSING_CREDENTIALS = 'Please enter your username and password.';
// Same text for wrong password, unknown user and locked account (SR-008).
export const GENERIC_LOGIN_ERROR = 'Invalid username or password.';
export const UNREACHABLE = 'Cannot reach the server. Is it running?';
export const UNEXPECTED_RESPONSE = 'The server sent an unexpected response.';
export const SESSION_EXPIRED = 'Your session has expired. Please log in again.';

/** An error carrying the HTTP status and the contract's error envelope. */
export class ApiError extends Error {
  constructor(message, { status = 0, code = '', fields = null, requestId = '' } = {}) {
    super(message);
    this.name = 'ApiError';
    this.status = status;
    this.code = code;
    this.fields = fields;
    this.requestId = requestId;
  }
}

/** Pulls the contract's error envelope out of a parsed body, tolerating junk. */
function errorEnvelope(body) {
  if (!body || typeof body !== 'object') return { code: '', fields: null, requestId: '' };
  // Routes send {error:{code,message,fields,requestId}} (student/account routes
  // and error_response.cpp). Older shapes put code at the top level, and the
  // contract's own example shows a bare string; accept all three so a response
  // shape change degrades to a worse message rather than a crash.
  const nested = body.error && typeof body.error === 'object' ? body.error : null;
  const code =
    (nested && nested.code) || (typeof body.error === 'string' ? body.error : '') || '';
  return {
    code,
    fields: (nested && nested.fields) || body.fields || null,
    requestId: (nested && nested.requestId) || body.requestId || '',
  };
}

async function readBody(response) {
  const text = await response.text();
  if (!text) return null;
  try {
    return JSON.parse(text);
  } catch {
    return null; // a gateway's HTML error page is not JSON; callers fall back
  }
}

/**
 * The client. Construct with a base URL and a token provider.
 *
 *   const api = new ApiClient({ token: () => session?.token ?? '' });
 *
 * `onUnauthorized` fires once when any authenticated request returns 401, so the
 * shell can drop the stored session and return to the login screen. That is the
 * browser-specific bit: the Qt client had no stored session to drop.
 */
export class ApiClient {
  constructor({ baseUrl = '', token = () => '', onUnauthorized = null, fetch: fetchImpl = null } = {}) {
    this.baseUrl = baseUrl; // '' = same origin, which is how it is served
    this.token = token;
    this.onUnauthorized = onUnauthorized;
    // Injected so tests can drive a stub without a network or a global.
    this.fetch = fetchImpl || globalThis.fetch.bind(globalThis);
  }

  url(path) {
    return this.baseUrl + path;
  }

  /**
   * One request. Returns {status, body}; throws ApiError only when no HTTP
   * response arrived at all, so callers can map every real status themselves
   * exactly as the Qt client's lambda did.
   */
  async request(method, path, { body = undefined, auth = true, signal = undefined } = {}) {
    const headers = { Accept: 'application/json' };
    if (body !== undefined) headers['Content-Type'] = 'application/json';
    if (auth) {
      const token = this.token();
      if (token) headers.Authorization = `Bearer ${token}`;
    }

    let response;
    try {
      response = await this.fetch(this.url(path), {
        method,
        headers,
        body: body === undefined ? undefined : JSON.stringify(body),
        signal,
      });
    } catch (cause) {
      // The only failure that is not an HTTP status: DNS, refused connection,
      // CORS, offline. The Qt client called this status 0 and said so plainly.
      throw new ApiError(UNREACHABLE, { status: 0, code: 'unreachable' });
    }

    const parsed = await readBody(response);

    if (response.status === 401 && auth && this.onUnauthorized) {
      this.onUnauthorized();
    }

    return { status: response.status, body: parsed };
  }

  /** The generic "unexpected status" sentence, carrying the traceable ref. */
  static unexpected(status, body) {
    const { requestId } = errorEnvelope(body);
    return `Something went wrong (code ${status}). Ref: ${requestId}`;
  }

  // -- Auth ----------------------------------------------------------------

  /**
   * POST /api/auth/login.
   * Resolves to {session, expiresAt} on success and throws ApiError otherwise.
   * A 200 that is not a well-formed login response is rejected, not accepted:
   * that regression is what used to open the app unauthenticated.
   */
  async login(username, password, { signal } = {}) {
    const { status, body } = await this.request('POST', '/api/auth/login', {
      auth: false,
      body: { username, password },
      signal,
    });

    if (status === 200) {
      const user = body && typeof body === 'object' ? body.user : null;
      const session = makeSession({
        username: user && user.username,
        fullName: user && user.fullName,
        role: user && user.role,
        token: body && body.token,
      });
      if (!user || !isValidSession(session)) {
        throw new ApiError(UNEXPECTED_RESPONSE, { status, code: 'malformed_success' });
      }
      return { session, expiresAt: (body && body.expiresAt) || '' };
    }
    if (status === 400) {
      throw new ApiError(MISSING_CREDENTIALS, { status, ...errorEnvelope(body) });
    }
    if (status === 401) {
      throw new ApiError(GENERIC_LOGIN_ERROR, { status, ...errorEnvelope(body) });
    }
    throw new ApiError(ApiClient.unexpected(status, body), { status, ...errorEnvelope(body) });
  }

  /** POST /api/auth/logout. 204 on success; a dead token is a 401, not an error. */
  async logout() {
    const { status, body } = await this.request('POST', '/api/auth/logout');
    if (status === 204) return true;
    if (status === 401) return false;
    throw new ApiError(ApiClient.unexpected(status, body), { status, ...errorEnvelope(body) });
  }

  // -- Students ------------------------------------------------------------

  /**
   * GET /api/students — paginated list and search (F-009, F-023).
   * Resolves to {items, total, limit, offset}. Both the contract shape
   * ({items,total,limit,offset}) and the old bare-array shape are accepted, as
   * the Qt client did, so a stale server still renders.
   */
  async searchStudents({ search = '', semester = 0, limit = 10, offset = 0, signal } = {}) {
    const query = new URLSearchParams();
    const trimmed = search.trim();
    if (trimmed) query.set('search', trimmed);
    if (semester > 0) query.set('semester', String(semester));
    query.set('limit', String(limit));
    query.set('offset', String(offset));

    const { status, body } = await this.request('GET', `/api/students?${query}`, { signal });

    if (status === 200) {
      if (Array.isArray(body)) {
        return { items: body, total: body.length, limit, offset };
      }
      const items = (body && body.items) || [];
      return {
        items,
        total: (body && Number.isInteger(body.total)) ? body.total : items.length,
        limit: (body && body.limit) || limit,
        offset: (body && body.offset) || offset,
      };
    }
    if (status === 401) throw new ApiError(SESSION_EXPIRED, { status, ...errorEnvelope(body) });
    if (status === 403) {
      throw new ApiError('You do not have permission to view students.', {
        status,
        ...errorEnvelope(body),
      });
    }
    throw new ApiError(ApiClient.unexpected(status, body), { status, ...errorEnvelope(body) });
  }

  /** GET /api/students/{rollNo}. */
  async getStudent(rollNo, { signal } = {}) {
    const { status, body } = await this.request(
      'GET',
      `/api/students/${encodeURIComponent(rollNo)}`,
      { signal },
    );
    if (status === 200) return body;
    if (status === 404) throw new ApiError('No student with that roll number.', { status, ...errorEnvelope(body) });
    if (status === 403) {
      throw new ApiError('You do not have permission to view that student.', { status, ...errorEnvelope(body) });
    }
    throw new ApiError(ApiClient.unexpected(status, body), { status, ...errorEnvelope(body) });
  }

  /** POST /api/students — Admin only (SRS-20). 400 carries per-field messages. */
  async createStudent(student, { signal } = {}) {
    const { status, body } = await this.request('POST', '/api/students', { body: student, signal });
    if (status === 201) return body;
    if (status === 400) {
      const env = errorEnvelope(body);
      throw new ApiError('Please correct the highlighted fields.', { status, ...env });
    }
    if (status === 409) {
      throw new ApiError('A student with that roll number already exists.', {
        status,
        ...errorEnvelope(body),
        fields: { rollNo: 'already exists' },
      });
    }
    throw new ApiError(ApiClient.unexpected(status, body), { status, ...errorEnvelope(body) });
  }

  /** PUT /api/students/{rollNo} — Admin only (SRS-25). Omitted fields unchanged. */
  async updateStudent(rollNo, changes, { signal } = {}) {
    const { status, body } = await this.request(
      'PUT',
      `/api/students/${encodeURIComponent(rollNo)}`,
      { body: changes, signal },
    );
    if (status === 200) return body;
    if (status === 400) {
      throw new ApiError('Please correct the highlighted fields.', { status, ...errorEnvelope(body) });
    }
    if (status === 404) throw new ApiError('No student with that roll number.', { status, ...errorEnvelope(body) });
    throw new ApiError(ApiClient.unexpected(status, body), { status, ...errorEnvelope(body) });
  }

  /** DELETE /api/students/{rollNo} — Admin only, cascading (SRS-26). */
  async deleteStudent(rollNo, { signal } = {}) {
    const { status, body } = await this.request(
      'DELETE',
      `/api/students/${encodeURIComponent(rollNo)}`,
      { signal },
    );
    if (status === 204) return true;
    if (status === 404) throw new ApiError('No student with that roll number.', { status, ...errorEnvelope(body) });
    throw new ApiError(ApiClient.unexpected(status, body), { status, ...errorEnvelope(body) });
  }

  // -- Accounts ------------------------------------------------------------

  /** GET /api/accounts — Admin only (SRS-17). Never carries hashes or tokens. */
  async listAccounts({ signal } = {}) {
    const { status, body } = await this.request('GET', '/api/accounts', { signal });
    if (status === 200) {
      const items = (body && body.items) || [];
      return { items, total: (body && body.total) || items.length };
    }
    if (status === 403) {
      throw new ApiError('You do not have permission to manage accounts.', { status, ...errorEnvelope(body) });
    }
    throw new ApiError(ApiClient.unexpected(status, body), { status, ...errorEnvelope(body) });
  }

  /** POST /api/accounts — Admin only (SRS-19). */
  async createAccount(account, { signal } = {}) {
    const { status, body } = await this.request('POST', '/api/accounts', { body: account, signal });
    if (status === 201) return body;
    if (status === 400) {
      throw new ApiError('Please correct the highlighted fields.', { status, ...errorEnvelope(body) });
    }
    if (status === 409) {
      throw new ApiError('That username is already taken.', {
        status,
        ...errorEnvelope(body),
        fields: { username: 'already taken' },
      });
    }
    throw new ApiError(ApiClient.unexpected(status, body), { status, ...errorEnvelope(body) });
  }

  /** PATCH /api/accounts/{username} — Admin only (SRS-17). */
  async updateAccount(username, changes, { signal } = {}) {
    const { status, body } = await this.request(
      'PATCH',
      `/api/accounts/${encodeURIComponent(username)}`,
      { body: changes, signal },
    );
    if (status === 200) return body;
    if (status === 400) {
      throw new ApiError('Please correct the highlighted fields.', { status, ...errorEnvelope(body) });
    }
    if (status === 404) throw new ApiError('No account with that username.', { status, ...errorEnvelope(body) });
    throw new ApiError(ApiClient.unexpected(status, body), { status, ...errorEnvelope(body) });
  }
}

// ---------------------------------------------------------------------------
// Demo mode (SRS_MOCK=1 in the Qt client). Answers locally so the UI can be
// exercised without a server or database — which is exactly what the demo
// machine may lack. Same three accounts and the same lockout rule.
// ---------------------------------------------------------------------------

export class MockApiClient {
  constructor({ latency = 120 } = {}) {
    this.latency = latency;
    this.failures = new Map();
    this.students = MockApiClient.seed();
    this.accounts = [
      { username: 'admin', fullName: 'Demo Admin', role: 'admin', active: true },
      { username: 'teacher', fullName: 'Demo Teacher', role: 'teacher', active: true },
      { username: 'student', fullName: 'Demo Student', role: 'student', active: true },
    ];
    this.passwords = new Map([
      ['admin', 'admin123'],
      ['teacher', 'teacher123'],
      ['student', 'student123'],
    ]);
  }

  static seed() {
    const names = [
      ['Aarav Sharma', 'CSE-AIML'],
      ['Advitha Shetty', 'CSE'],
      ['Ananya Rao', 'ISE'],
      ['Bhavya Nair', 'ECE'],
      ['Chirag Patel', 'CSE-AIML'],
      ['Divya Kulkarni', 'CSE'],
      ['Eshan Gupta', 'MECH'],
      ['Farhan Khan', 'CIVIL'],
      ['Gayathri Iyer', 'ISE'],
      ['Harsh Verma', 'ECE'],
      ['Ishita Menon', 'CSE'],
      ['Jayant Reddy', 'CSE-AIML'],
    ];
    return names.map(([fullName, department], i) => ({
      rollNo: `PES2UG23CS${101 + i}`,
      fullName,
      dateOfBirth: '2005-04-11',
      department,
      semester: 1 + (i % 8),
      contact: `student${i + 1}@example.edu`,
    }));
  }

  wait(value) {
    return new Promise((resolve) => setTimeout(() => resolve(value), this.latency));
  }

  async login(username, password) {
    await this.wait();
    const user = this.accounts.find((a) => a.username === username);
    const locked = (this.failures.get(username) || 0) >= 5;
    if (user && user.active && !locked && this.passwords.get(username) === password) {
      this.failures.delete(username);
      const session = makeSession({ ...user, token: 'mock-token' });
      return { session, expiresAt: '' };
    }
    if (user) this.failures.set(username, (this.failures.get(username) || 0) + 1);
    throw new ApiError(GENERIC_LOGIN_ERROR, { status: 401, code: 'unauthenticated' });
  }

  async logout() {
    await this.wait();
    return true;
  }

  async searchStudents({ search = '', semester = 0, limit = 10, offset = 0 } = {}) {
    await this.wait();
    const q = search.trim().toLowerCase();
    const matches = this.students.filter((s) => {
      const hit =
        !q ||
        s.fullName.toLowerCase().includes(q) ||
        s.rollNo.toLowerCase().includes(q);
      const sem = semester > 0 ? s.semester === semester : true;
      return hit && sem;
    });
    return {
      items: matches.slice(offset, offset + limit),
      total: matches.length,
      limit,
      offset,
    };
  }

  async getStudent(rollNo) {
    await this.wait();
    const found = this.students.find((s) => s.rollNo === rollNo);
    if (!found) throw new ApiError('No student with that roll number.', { status: 404 });
    return found;
  }

  async createStudent(student) {
    await this.wait();
    if (this.students.some((s) => s.rollNo === student.rollNo)) {
      throw new ApiError('A student with that roll number already exists.', {
        status: 409,
        fields: { rollNo: 'already exists' },
      });
    }
    const created = { ...student };
    this.students.unshift(created);
    return created;
  }

  async updateStudent(rollNo, changes) {
    await this.wait();
    const found = this.students.find((s) => s.rollNo === rollNo);
    if (!found) throw new ApiError('No student with that roll number.', { status: 404 });
    for (const [key, value] of Object.entries(changes)) {
      if (key !== 'rollNo' && value !== undefined && value !== '') found[key] = value;
    }
    return { ...found };
  }

  async deleteStudent(rollNo) {
    await this.wait();
    const before = this.students.length;
    this.students = this.students.filter((s) => s.rollNo !== rollNo);
    if (this.students.length === before) {
      throw new ApiError('No student with that roll number.', { status: 404 });
    }
    return true;
  }

  async listAccounts() {
    await this.wait();
    return { items: this.accounts.map((a) => ({ ...a })), total: this.accounts.length };
  }

  async createAccount(account) {
    await this.wait();
    if (this.accounts.some((a) => a.username === account.username)) {
      throw new ApiError('That username is already taken.', {
        status: 409,
        fields: { username: 'already taken' },
      });
    }
    const created = {
      username: account.username,
      fullName: account.fullName,
      role: account.role,
      active: true,
    };
    this.accounts.push(created);
    this.passwords.set(account.username, account.password);
    return created;
  }

  async updateAccount(username, changes) {
    await this.wait();
    const found = this.accounts.find((a) => a.username === username);
    if (!found) throw new ApiError('No account with that username.', { status: 404 });
    if (changes.fullName) found.fullName = changes.fullName;
    if (changes.role) found.role = changes.role;
    if (typeof changes.active === 'boolean') found.active = changes.active;
    if (changes.password) this.passwords.set(username, changes.password);
    return { ...found };
  }
}

/** True when the page was loaded with ?mock=1 or the build was configured mock. */
export function mockRequested(location = globalThis.location) {
  if (!location) return false;
  try {
    const params = new URLSearchParams(location.search);
    return params.get('mock') === '1';
  } catch {
    return false;
  }
}

export { clearSession };