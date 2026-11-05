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

// -- GET /api/attendance/{rollNo} (SRS-F-017, F-018) ------------------------------

describe('ApiClient.getAttendance', () => {
  const OK = {
    studentRollNo: '1ABC26CS001',
    items: [{ courseCode: 'CS502', courseName: 'OS', semester: 5, conducted: 3, present: 2, percentage: 66.67 }],
    overall: { conducted: 3, present: 2, percentage: 66.67 },
  };

  test('requests the path with optional filters', async () => {
    const s = stub({ status: 200, body: OK });
    await s.client().getAttendance('1ABC26CS001');
    await s.client().getAttendance('1ABC26CS001', { semester: 5, courseCode: 'CS502' });
    assert.equal(s.calls[0].url, '/api/attendance/1ABC26CS001');
    assert.equal(s.calls[1].url, '/api/attendance/1ABC26CS001?semester=5&courseCode=CS502');
  });

  // AC-018: the client shows the server's two-decimal value, unchanged.
  test('keeps the server percentage as-is', async () => {
    const s = stub({ status: 200, body: OK });
    const result = await s.client().getAttendance('1ABC26CS001');
    assert.equal(result.items[0].percentage, 66.67);
    assert.equal(result.overall.percentage, 66.67);
  });

  // AC-018: present > conducted is rejected, never rendered.
  test('rejects a body where present exceeds conducted', async () => {
    const bad = { ...OK, items: [{ ...OK.items[0], present: 4, conducted: 3 }] };
    const s = stub({ status: 200, body: bad });
    await assert.rejects(s.client().getAttendance('1ABC26CS001'), (e) => e.code === 'inconsistent_attendance');
  });

  test('403 becomes a permission message', async () => {
    const s = stub({ status: 403, body: { error: 'forbidden' } });
    await assert.rejects(s.client().getAttendance('X'), /permission to view that student's attendance/);
  });

  test('500 from the server keeps its request ref', async () => {
    const s = stub({ status: 500, body: { error: 'internal', requestId: 'abc123' } });
    await assert.rejects(s.client().getAttendance('X'), /code 500\). Ref: abc123/);
  });
});

describe('attendance percentage formatting', () => {
  test('two decimals, and a dash for no sessions', async () => {
    // Imported lazily: the page module touches `document` only when a page is built.
    const { formatPercentage } = await import('../js/pages/attendance.js');
    assert.equal(formatPercentage(85), '85.00%');
    assert.equal(formatPercentage(66.67), '66.67%');
    assert.equal(formatPercentage(null), '—');
  });
});

describe('MockApiClient attendance', () => {
  test('mock rounding matches the server examples', () => {
    assert.equal(MockApiClient.percentage(101, 120), 84.17);
    assert.equal(MockApiClient.percentage(2, 3), 66.67);
    assert.equal(MockApiClient.percentage(1, 3), 33.33);
    assert.equal(MockApiClient.percentage(0, 0), null);
  });

  test('a teacher sees only own subjects, a student only themselves', async () => {
    const teacher = new MockApiClient({ latency: 0 });
    await teacher.login('teacher', 'teacher123');
    const t = await teacher.getAttendance('PES2UG23CS101');
    assert.deepEqual(t.items.map((i) => i.courseCode).sort(), ['CS501', 'CS503']);

    const student = new MockApiClient({ latency: 0 });
    await student.login('student', 'student123');
    await assert.rejects(student.getAttendance('PES2UG23CS101'), (e) => e.status === 403);
  });

  test('every role may open Attendance', () => {
    for (const role of [Role.Admin, Role.Teacher, Role.Student]) {
      assert.equal(canUse(role, 'attendance.view'), true, role);
    }
  });
});
