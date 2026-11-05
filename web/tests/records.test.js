// Sprint 2 client tests: role-scoped record reads.
//
// Same approach as api.test.js — a stub fetch records exactly what the client
// sent, so these assert the wire shape (path, verb, query) and how each status
// is worded, without a live server. The server-side scoping itself is covered
// by server/tests/test_*_routes.cpp and the end-to-end CI step.
//
// Run: node --test web/tests/

import { test, describe } from 'node:test';
import assert from 'node:assert/strict';

import { ApiClient, MockApiClient, SESSION_EXPIRED } from '../js/api.js';
import { Role, canUse } from '../js/session.js';

function stub(response) {
  const calls = [];
  const fetch = async (url, init = {}) => {
    calls.push({ url, method: init.method, headers: init.headers });
    const body = response.body === undefined ? '' : response.body;
    return {
      status: response.status,
      ok: response.status >= 200 && response.status < 300,
      text: async () => (typeof body === 'string' ? body : JSON.stringify(body)),
    };
  };
  return { calls, client: () => new ApiClient({ fetch, token: () => 'tok' }) };
}

// -- GET /api/me ----------------------------------------------------------------

describe('ApiClient.getMe', () => {
  test('returns the linked roll number for a student', async () => {
    const s = stub({
      status: 200,
      body: { username: 'student', fullName: 'Demo Student', role: 'student', rollNo: '1ABC26CS001' },
    });
    const me = await s.client().getMe();
    assert.equal(me.rollNo, '1ABC26CS001');
    assert.equal(s.calls[0].url, '/api/me');
    assert.equal(s.calls[0].headers.Authorization, 'Bearer tok');
  });

  test('a null roll number stays null, never an empty string', async () => {
    const s = stub({ status: 200, body: { username: 'admin', role: 'admin', rollNo: null } });
    assert.equal((await s.client().getMe()).rollNo, null);
  });
});

// -- GET /api/grades/{rollNo} (SRS-F-014) ---------------------------------------

describe('ApiClient.getGrades', () => {
  test('requests the roll number in the path, encoded', async () => {
    const s = stub({ status: 200, body: { studentRollNo: 'A/B', items: [] } });
    await s.client().getGrades('A/B');
    assert.equal(s.calls[0].url, '/api/grades/A%2FB');
    assert.equal(s.calls[0].method, 'GET');
  });

  test('passes the semester filter only when set', async () => {
    const s = stub({ status: 200, body: { items: [] } });
    await s.client().getGrades('1ABC26CS001', { semester: 5 });
    await s.client().getGrades('1ABC26CS001', { semester: 0 });
    assert.equal(s.calls[0].url, '/api/grades/1ABC26CS001?semester=5');
    assert.equal(s.calls[1].url, '/api/grades/1ABC26CS001');
  });

  test('returns the items exactly as the server scoped them', async () => {
    const items = [{ courseCode: 'CS501', courseName: 'DBMS', grade: 8.5, semester: 5, updatedAt: 'x' }];
    const s = stub({ status: 200, body: { studentRollNo: '1ABC26CS001', items } });
    const result = await s.client().getGrades('1ABC26CS001');
    assert.deepEqual(result.items, items);
  });

  // TC-014: the refusal is the server's; the client only words it.
  test('403 from the server becomes a permission message', async () => {
    const s = stub({ status: 403, body: { error: 'forbidden', requestId: 'r1' } });
    await assert.rejects(s.client().getGrades('1ABC26CS002'), (e) => {
      assert.equal(e.status, 403);
      assert.match(e.message, /permission to view that student's grades/);
      return true;
    });
  });

  test('404 says the student does not exist', async () => {
    const s = stub({ status: 404, body: { error: 'not_found' } });
    await assert.rejects(s.client().getGrades('NOPE'), /No student with that roll number/);
  });

  test('401 is a session expiry', async () => {
    const s = stub({ status: 401, body: { error: 'unauthenticated' } });
    await assert.rejects(s.client().getGrades('1ABC26CS001'), (e) => e.message === SESSION_EXPIRED);
  });
});

// -- demo mode mirrors the server's scoping --------------------------------------

describe('MockApiClient grade scoping', () => {
  async function as(username, password) {
    const api = new MockApiClient({ latency: 0 });
    await api.login(username, password);
    return api;
  }

  test('a student reads only their own grades', async () => {
    const api = await as('student', 'student123');
    const me = await api.getMe();
    const own = await api.getGrades(me.rollNo);
    assert.ok(own.items.length > 0);
    await assert.rejects(api.getGrades('PES2UG23CS101'), (e) => e.status === 403);
  });

  test('a teacher sees only their own subjects', async () => {
    const api = await as('teacher', 'teacher123');
    const { items } = await api.getGrades('PES2UG23CS101');
    assert.deepEqual(items.map((i) => i.courseCode).sort(), ['CS501', 'CS503']);
  });

  test('an admin sees every subject', async () => {
    const api = await as('admin', 'admin123');
    const { items } = await api.getGrades('PES2UG23CS101');
    assert.equal(items.length, 4);
  });
});

describe('navigation permissions', () => {
  test('every role may open Grades', () => {
    for (const role of [Role.Admin, Role.Teacher, Role.Student]) {
      assert.equal(canUse(role, 'grades.view'), true, role);
    }
  });
});
