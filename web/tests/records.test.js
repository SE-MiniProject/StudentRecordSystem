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
    calls.push({ url, method: init.method, headers: init.headers, body: init.body });
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

describe('ApiClient attendance writes (SRS-24)', () => {
  const entry = { rollNo: '1ABC26CS001', courseCode: 'CS501', sessionDate: '2026-11-05', status: 'present' };

  test('creates a session with POST', async () => {
    const s = stub({ status: 201, body: entry });
    await s.client().createAttendance(entry);
    assert.equal(s.calls[0].method, 'POST');
    assert.equal(s.calls[0].url, '/api/attendance');
    assert.deepEqual(JSON.parse(s.calls[0].body), entry);
  });

  test('updates an existing session with PUT', async () => {
    const s = stub({ status: 200, body: entry });
    await s.client().updateAttendance(entry);
    assert.equal(s.calls[0].method, 'PUT');
    assert.equal(s.calls[0].url, '/api/attendance/1ABC26CS001/CS501/2026-11-05');
    assert.deepEqual(JSON.parse(s.calls[0].body), { status: 'present' });
  });

  test('duplicate creation is a conflict', async () => {
    const s = stub({ status: 409, body: { error: 'conflict' } });
    await assert.rejects(s.client().createAttendance(entry), /already exists/);
  });

  test('update requires an existing session', async () => {
    const s = stub({ status: 404, body: { error: 'not_found' } });
    await assert.rejects(s.client().updateAttendance(entry), /No attendance entry exists/);
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

  test('teacher can record, update, and cannot duplicate a session', async () => {
    const api = new MockApiClient({ latency: 0 });
    await api.login('teacher', 'teacher123');
    const entry = { rollNo: 'PES2UG23CS105', courseCode: 'CS501', sessionDate: '2026-11-05', status: 'present' };
    await api.createAttendance(entry);
    await assert.rejects(api.createAttendance(entry), (e) => e.status === 409);
    await api.updateAttendance({ ...entry, status: 'absent' });
    assert.equal(api.attendanceSessions.get('PES2UG23CS105|CS501|2026-11-05'), 'absent');
  });

  test('teacher cannot write attendance for an unassigned course', async () => {
    const api = new MockApiClient({ latency: 0 });
    await api.login('teacher', 'teacher123');
    await assert.rejects(api.createAttendance({ rollNo: 'PES2UG23CS105', courseCode: 'CS502', sessionDate: '2026-11-05', status: 'present' }), (e) => e.status === 403);
  });

  test('every role may open Attendance', () => {
    for (const role of [Role.Admin, Role.Teacher, Role.Student]) {
      assert.equal(canUse(role, 'attendance.view'), true, role);
    }
  });
});

// -- GET /api/timetable (SRS-F-020) ------------------------------------------------

describe('ApiClient.getTimetable', () => {
  test('no filters by default; admin filters go in the query', async () => {
    const s = stub({ status: 200, body: { scope: { role: 'teacher' }, items: [] } });
    await s.client().getTimetable();
    await s.client().getTimetable({ teacher: 't2', department: 'Computer Science', semester: 5 });
    assert.equal(s.calls[0].url, '/api/timetable');
    assert.equal(s.calls[1].url, '/api/timetable?teacher=t2&department=Computer+Science&semester=5');
  });

  // TC-020: a cross-user fetch is refused by the server; the client words it.
  test('403 says you can only view your own schedule', async () => {
    const s = stub({ status: 403, body: { error: 'forbidden' } });
    await assert.rejects(s.client().getTimetable({ teacher: 'other' }), (e) => {
      assert.equal(e.status, 403);
      assert.match(e.message, /only view your own schedule/);
      return true;
    });
  });
});

describe('ApiClient.createTimetableSlot (SRS-F-019/F-021)', () => {
  const slot = {
    dayOfWeek: 1, startTime: '13:00', endTime: '14:00', courseCode: 'CS501',
    teacher: 'teacher', department: 'Computer Science', semester: 5, room: 'C101',
  };

  test('posts the complete slot and returns the created row', async () => {
    const created = { ...slot, id: 9, courseName: 'DBMS', teacherName: 'Demo Teacher' };
    const s = stub({ status: 201, body: created });
    assert.deepEqual(await s.client().createTimetableSlot(slot), created);
    assert.equal(s.calls[0].url, '/api/timetable');
    assert.equal(s.calls[0].method, 'POST');
    assert.deepEqual(JSON.parse(s.calls[0].body), slot);
  });

  test('409 explains a teacher or room conflict', async () => {
    const s = stub({ status: 409, body: { error: { code: 'timetable_conflict' } } });
    await assert.rejects(s.client().createTimetableSlot(slot), (error) => {
      assert.equal(error.status, 409);
      assert.match(error.message, /teacher or room is already booked/);
      return true;
    });
  });
});

describe('timetable layout', () => {
  test('weekdays across, ordered time rows down, slots placed by day and time', async () => {
    const { layoutWeek } = await import('../js/pages/timetable.js');
    const items = [
      { id: 2, dayOfWeek: 3, startTime: '11:00', endTime: '12:00', courseCode: 'B' },
      { id: 1, dayOfWeek: 1, startTime: '09:00', endTime: '10:00', courseCode: 'A' },
      { id: 3, dayOfWeek: 1, startTime: '11:00', endTime: '12:00', courseCode: 'C' },
    ];
    const { days, times, cells } = layoutWeek(items);
    assert.deepEqual(days, [1, 2, 3, 4, 5]);
    assert.deepEqual(times, ['09:00–10:00', '11:00–12:00']);
    assert.equal(cells.get('1|09:00–10:00')[0].courseCode, 'A');
    assert.equal(cells.get('3|11:00–12:00')[0].courseCode, 'B');
    assert.equal(cells.get('2|09:00–10:00'), undefined);
  });

  test('Saturday appears only when used', async () => {
    const { layoutWeek } = await import('../js/pages/timetable.js');
    const { days } = layoutWeek([{ dayOfWeek: 6, startTime: '09:00', endTime: '10:00' }]);
    assert.deepEqual(days, [1, 2, 3, 4, 5, 6]);
  });
});

describe('MockApiClient timetable scoping', () => {
  async function as(username, password) {
    const api = new MockApiClient({ latency: 0 });
    await api.login(username, password);
    return api;
  }

  test('teacher sees own schedule and cannot ask for another teacher', async () => {
    const api = await as('teacher', 'teacher123');
    const { items } = await api.getTimetable();
    assert.ok(items.length > 0);
    assert.ok(items.every((s) => s.teacher === 'teacher'));
    await assert.rejects(api.getTimetable({ teacher: 'teacher2' }), (e) => e.status === 403);
  });

  test('student sees own class only and cannot ask for another class', async () => {
    const api = await as('student', 'student123');
    const { items, scope } = await api.getTimetable();
    assert.ok(items.length > 0);
    assert.ok(items.every((s) => s.department === scope.department && s.semester === scope.semester));
    await assert.rejects(api.getTimetable({ semester: 3 }), (e) => e.status === 403);
  });

  test('admin sees every slot', async () => {
    const api = await as('admin', 'admin123');
    assert.equal((await api.getTimetable()).items.length, 8);
  });

  test('admin creates a slot and the mock timetable reflects it', async () => {
    const api = await as('admin', 'admin123');
    const created = await api.createTimetableSlot({
      dayOfWeek: 1, startTime: '13:00', endTime: '14:00', courseCode: 'CS501',
      teacher: 'teacher', department: 'CSE-AIML', semester: 5, room: 'C101',
    });
    assert.equal(created.courseCode, 'CS501');
    assert.equal((await api.getTimetable()).items.length, 9);
  });

  test('mock rejects a teacher or room overlap without changing slots', async () => {
    const api = await as('admin', 'admin123');
    const before = (await api.getTimetable()).items.length;
    await assert.rejects(api.createTimetableSlot({
      dayOfWeek: 1, startTime: '09:30', endTime: '10:30', courseCode: 'CS503',
      teacher: 'teacher', department: 'CSE-AIML', semester: 5, room: 'C101',
    }), (error) => error.status === 409);
    assert.equal((await api.getTimetable()).items.length, before);
  });

  test('mock only allows assigned teacher-course pairs', async () => {
    const api = await as('admin', 'admin123');
    await assert.rejects(api.createTimetableSlot({
      dayOfWeek: 1, startTime: '13:00', endTime: '14:00', courseCode: 'CS502',
      teacher: 'teacher', department: 'CSE-AIML', semester: 5, room: 'C101',
    }), (error) => error.status === 400);
  });

  test('teacher cannot create a slot in mock mode', async () => {
    const api = await as('teacher', 'teacher123');
    await assert.rejects(api.createTimetableSlot({}), (error) => error.status === 403);
  });

  test('every role may open the timetable', () => {
    for (const role of [Role.Admin, Role.Teacher, Role.Student]) {
      assert.equal(canUse(role, 'timetable.view'), true, role);
    }
  });
});
