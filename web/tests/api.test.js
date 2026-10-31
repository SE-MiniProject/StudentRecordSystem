// Ports client/tests/test_apiclient.cpp to node:test.
//
// The Qt tests drove a real QNetworkAccessManager against a throwaway TCP
// server, and the comment there says why: "a mocked-out client would pass even
// if the URL, verb, or parsing were wrong". The equivalent here is a stub fetch
// that records the request it was given. That keeps the assertion on the wire
// shape (path, method, headers, body) without needing a live server, and it is
// what the later Playwright suite adds on top.
//
// Run: node --test web/tests/

import { test, describe } from 'node:test';
import assert from 'node:assert/strict';

import {
  ApiClient,
  ApiError,
  GENERIC_LOGIN_ERROR,
  MISSING_CREDENTIALS,
  SESSION_EXPIRED,
  UNREACHABLE,
} from '../js/api.js';
import { Role, makeSession, canUse, loadSession, saveSession, clearSession } from '../js/session.js';

// A stub server: answers every request with one canned response and remembers
// the last request, so a test can assert what the client actually sent.
function stubServer(response, { throws = false } = {}) {
  const calls = [];
  const fetch = async (url, init = {}) => {
    calls.push({ url, method: init.method, headers: init.headers, body: init.body });
    if (throws) throw new TypeError('fetch failed');
    const body = response.body === undefined ? '' : response.body;
    return {
      status: response.status,
      ok: response.status >= 200 && response.status < 300,
      text: async () => (typeof body === 'string' ? body : JSON.stringify(body)),
    };
  };
  return {
    fetch,
    calls,
    last: () => calls[calls.length - 1],
    client: (opts = {}) => new ApiClient({ fetch, ...opts }),
  };
}

const OK_BODY = {
  token: 'opaque-token',
  expiresAt: '2026-10-26T22:03:00Z',
  user: { username: 'a.shetty', fullName: 'Advitha Shetty', role: 'admin' },
};

// -- login -------------------------------------------------------------------

describe('ApiClient.login', () => {
  // A well-formed 200 is the happy path: the session must carry every field
  // api.md promises, so the shell does not open with blank labels.
  test('successful login yields a full session', async () => {
    const server = stubServer({ status: 200, body: OK_BODY });
    const { session, expiresAt } = await server.client().login('a.shetty', 'correct-horse');

    assert.equal(session.username, 'a.shetty');
    assert.equal(session.fullName, 'Advitha Shetty');
    assert.equal(session.role, Role.Admin);
    assert.equal(session.token, 'opaque-token');
    assert.equal(expiresAt, '2026-10-26T22:03:00Z');
  });

  // The regression that mattered: a 200 with no token used to accept the
  // dialog and open the app unauthenticated.
  for (const [label, body] of [
    ['no token', { user: OK_BODY.user }],
    ['empty token', { token: '', user: OK_BODY.user }],
    ['no user', { token: 'opaque-token' }],
    ['empty user', { token: 'opaque-token', user: {} }],
    ['no username', { token: 'opaque-token', user: { fullName: 'x', role: 'admin' } }],
    ['not json', '<html>gateway</html>'],
    ['empty body', ''],
  ]) {
    test(`a malformed 200 is rejected (${label})`, async () => {
      const server = stubServer({ status: 200, body });
      await assert.rejects(
        () => server.client().login('a.shetty', 'correct-horse'),
        (err) => {
          assert.ok(err instanceof ApiError);
          assert.ok(err.message.length > 0, 'a malformed 200 must explain itself');
          return true;
        },
      );
    });
  }

  // SR-008: wrong password, unknown user and lockout must be indistinguishable
  // to the user, so one generic sentence covers all 401s.
  test('a 401 is generic', async () => {
    const server = stubServer({
      status: 401,
      body: { error: 'unauthenticated', requestId: 'b1e4' },
    });
    await assert.rejects(() => server.client().login('a', 'b'), (err) => {
      assert.equal(err.status, 401);
      assert.equal(err.message, GENERIC_LOGIN_ERROR);
      return true;
    });
  });

  test('a 400 uses the shared missing-credentials wording', async () => {
    const server = stubServer({
      status: 400,
      body: { error: 'validation_failed', requestId: 'b1e4' },
    });
    await assert.rejects(() => server.client().login('', ''), (err) => {
      assert.equal(err.status, 400);
      assert.equal(err.message, MISSING_CREDENTIALS);
      return true;
    });
  });

  // Any other status is unexpected, and the message must carry the server's
  // request id so a user report can be traced in the server log.
  test('an unexpected status mentions status and request id', async () => {
    const server = stubServer({ status: 500, body: { requestId: 'c0ffee' } });
    await assert.rejects(() => server.client().login('a', 'b'), (err) => {
      assert.match(err.message, /500/);
      assert.match(err.message, /c0ffee/);
      return true;
    });
  });

  // A dead server must say so rather than appear to hang.
  test('an unreachable server reports clearly', async () => {
    const server = stubServer({ status: 200 }, { throws: true });
    await assert.rejects(() => server.client().login('a', 'b'), (err) => {
      assert.equal(err.status, 0);
      assert.equal(err.message, UNREACHABLE);
      return true;
    });
  });

  // The credentials must go to the documented endpoint as JSON, not form data.
  test('posts credentials to the login endpoint as JSON', async () => {
    const server = stubServer({ status: 200, body: OK_BODY });
    await server.client().login('a.shetty', 'correct-horse');

    const call = server.last();
    assert.equal(call.method, 'POST');
    assert.equal(call.url, '/api/auth/login');
    assert.equal(call.headers['Content-Type'], 'application/json');
    assert.deepEqual(JSON.parse(call.body), {
      username: 'a.shetty',
      password: 'correct-horse',
    });
    // Login is the one endpoint that must not carry a bearer token.
    assert.equal(call.headers.Authorization, undefined);
  });

  test('a 401 on an authenticated request notifies the shell', async () => {
    let notified = 0;
    const server = stubServer({ status: 401, body: { error: 'unauthenticated' } });
    const client = server.client({ token: () => 'stale', onUnauthorized: () => (notified += 1) });
    await assert.rejects(() => client.searchStudents({}));
    assert.equal(notified, 1);
  });
});

// -- students ----------------------------------------------------------------

describe('ApiClient.searchStudents', () => {
  test('sends the documented query parameters', async () => {
    const server = stubServer({
      status: 200,
      body: { items: [], total: 0, limit: 10, offset: 20 },
    });
    await server.client({ token: () => 't' }).searchStudents({
      search: '  shetty ',
      semester: 3,
      limit: 10,
      offset: 20,
    });

    const call = server.last();
    assert.equal(call.method, 'GET');
    assert.ok(call.url.startsWith('/api/students?'), call.url);
    const query = new URLSearchParams(call.url.split('?')[1]);
    assert.equal(query.get('search'), 'shetty'); // trimmed, as the Qt client did
    assert.equal(query.get('semester'), '3');
    assert.equal(query.get('limit'), '10');
    assert.equal(query.get('offset'), '20');
    assert.equal(call.headers.Authorization, 'Bearer t');
  });

  test('omits search and semester when they are unset', async () => {
    const server = stubServer({ status: 200, body: { items: [], total: 0 } });
    await server.client().searchStudents({ search: '   ', semester: 0 });
    const query = new URLSearchParams(server.last().url.split('?')[1]);
    assert.equal(query.has('search'), false);
    assert.equal(query.has('semester'), false);
  });

  test('returns the contract page shape', async () => {
    const items = [{ rollNo: 'PES1', fullName: 'A', department: 'CSE', semester: 2, contact: 'x' }];
    const server = stubServer({ status: 200, body: { items, total: 42, limit: 10, offset: 10 } });
    const page = await server.client().searchStudents({ offset: 10 });
    assert.deepEqual(page, { items, total: 42, limit: 10, offset: 10 });
  });

  // The old server returned a bare array; the Qt client accepted both so a
  // stale server still rendered. Keep that tolerance.
  test('accepts a bare array as a page', async () => {
    const items = [{ rollNo: 'PES1' }, { rollNo: 'PES2' }];
    const server = stubServer({ status: 200, body: items });
    const page = await server.client().searchStudents({ limit: 10, offset: 0 });
    assert.deepEqual(page.items, items);
    assert.equal(page.total, 2);
  });

  test('no match is an empty page, not an error', async () => {
    const server = stubServer({ status: 200, body: { items: [], total: 0 } });
    const page = await server.client().searchStudents({ search: 'nobody' });
    assert.deepEqual(page.items, []);
    assert.equal(page.total, 0);
  });

  test('401 says the session expired', async () => {
    const server = stubServer({ status: 401, body: { error: 'unauthenticated' } });
    await assert.rejects(() => server.client().searchStudents({}), (err) => {
      assert.equal(err.message, SESSION_EXPIRED);
      return true;
    });
  });

  test('403 says permission is missing', async () => {
    const server = stubServer({ status: 403, body: { error: 'forbidden' } });
    await assert.rejects(() => server.client().searchStudents({}), (err) => {
      assert.match(err.message, /permission/i);
      return true;
    });
  });

  test('400 invalid_limit is reported with its code', async () => {
    const server = stubServer({
      status: 400,
      body: { error: { code: 'invalid_limit', message: 'limit must be between 1 and 200' } },
    });
    await assert.rejects(() => server.client().searchStudents({ limit: 1000 }), (err) => {
      assert.equal(err.code, 'invalid_limit');
      return true;
    });
  });
});

describe('ApiClient student writes', () => {
  test('create sends the contract field names and a bearer token', async () => {
    const student = {
      rollNo: 'PES2UG23CS999',
      fullName: 'New Student',
      dateOfBirth: '2005-01-01',
      department: 'CSE',
      semester: 3,
      contact: 'new@example.edu',
    };
    const server = stubServer({ status: 201, body: student });
    const created = await server.client({ token: () => 't' }).createStudent(student);

    const call = server.last();
    assert.equal(call.method, 'POST');
    assert.equal(call.url, '/api/students');
    assert.deepEqual(JSON.parse(call.body), student);
    assert.equal(created.rollNo, 'PES2UG23CS999');
  });

  test('a duplicate roll number maps to a field error on rollNo', async () => {
    const server = stubServer({ status: 409, body: { error: { code: 'duplicate_roll_no' } } });
    await assert.rejects(
      () => server.client().createStudent({ rollNo: 'X' }),
      (err) => {
        assert.equal(err.status, 409);
        assert.ok(err.fields && err.fields.rollNo);
        return true;
      },
    );
  });

  test('a validation failure carries the per-field messages', async () => {
    const server = stubServer({
      status: 400,
      body: {
        error: {
          code: 'validation_failed',
          fields: { fullName: 'required', semester: 'out of range' },
        },
      },
    });
    await assert.rejects(() => server.client().createStudent({}), (err) => {
      assert.equal(err.fields.fullName, 'required');
      assert.equal(err.fields.semester, 'out of range');
      return true;
    });
  });

  test('update PUTs to the encoded roll number', async () => {
    const server = stubServer({ status: 200, body: { rollNo: 'A/B' } });
    await server.client().updateStudent('A/B', { semester: 6 });
    const call = server.last();
    assert.equal(call.method, 'PUT');
    assert.equal(call.url, '/api/students/A%2FB');
    assert.deepEqual(JSON.parse(call.body), { semester: 6 });
  });

  test('delete accepts 204 and reports a missing student', async () => {
    const ok = stubServer({ status: 204, body: '' });
    assert.equal(await ok.client().deleteStudent('PES1'), true);

    const missing = stubServer({ status: 404, body: { error: { code: 'not_found' } } });
    await assert.rejects(() => missing.client().deleteStudent('PES1'), (err) => {
      assert.equal(err.status, 404);
      return true;
    });
  });
});

describe('ApiClient accounts', () => {
  test('list returns the items and total', async () => {
    const items = [{ username: 'a', fullName: 'A', role: 'teacher', active: true }];
    const server = stubServer({ status: 200, body: { items, total: 1 } });
    const page = await server.client().listAccounts();
    assert.deepEqual(page.items, items);
    assert.equal(page.total, 1);
  });

  test('create posts username, password, fullName and role', async () => {
    const account = { username: 'ciuser', password: 'pw', fullName: 'CI User', role: 'teacher' };
    const server = stubServer({ status: 201, body: { ...account, active: true } });
    await server.client().createAccount(account);
    assert.deepEqual(JSON.parse(server.last().body), account);
  });

  test('deactivating patches only active', async () => {
    const server = stubServer({ status: 200, body: { username: 'a', active: false } });
    await server.client().updateAccount('a', { active: false });
    const call = server.last();
    assert.equal(call.method, 'PATCH');
    assert.equal(call.url, '/api/accounts/a');
    assert.deepEqual(JSON.parse(call.body), { active: false });
  });
});

// -- session -----------------------------------------------------------------

describe('session', () => {
  test('unknown roles degrade to Student', () => {
    assert.equal(makeSession({ username: 'u', token: 't', role: 'root' }).role, Role.Student);
    assert.equal(makeSession({ username: 'u', token: 't', role: undefined }).role, Role.Student);
  });

  // Ported from test_mainwindow.cpp: the permission table is the thing worth
  // asserting, and an unlisted action must fail closed.
  test('the permission table gates by role', () => {
    assert.equal(canUse(Role.Admin, 'accounts.manage'), true);
    assert.equal(canUse(Role.Teacher, 'accounts.manage'), false);
    assert.equal(canUse(Role.Student, 'students.view'), true);
    assert.equal(canUse(Role.Teacher, 'students.delete'), false);
    assert.equal(canUse(Role.Admin, 'students.delete'), true);
    assert.equal(canUse(Role.Admin, 'not.in.the.table'), false);
  });

  test('a stored session round-trips', () => {
    const store = memoryStorage();
    const session = makeSession({
      username: 'a.shetty',
      fullName: 'Advitha Shetty',
      role: 'admin',
      token: 'opaque-token',
    });
    saveSession(session, store);
    assert.deepEqual(loadSession(store), session);
    clearSession(store);
    assert.equal(loadSession(store), null);
  });

  test('corrupt storage is not a session', () => {
    const store = memoryStorage();
    store.setItem('srs.session', '{not json');
    assert.equal(loadSession(store), null);
    assert.equal(store.getItem('srs.session'), null, 'the corrupt value is dropped');
  });

  test('a session without a token is not loaded', () => {
    const store = memoryStorage();
    store.setItem('srs.session', JSON.stringify({ username: 'a', token: '' }));
    assert.equal(loadSession(store), null);
  });
});

function memoryStorage() {
  const map = new Map();
  return {
    getItem: (k) => (map.has(k) ? map.get(k) : null),
    setItem: (k, v) => map.set(k, String(v)),
    removeItem: (k) => map.delete(k),
  };
}