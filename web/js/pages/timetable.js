// Timetable page — SRS-F-020 (AC-020 / TC-020).
//
//   Teacher  their own teaching schedule
//   Student  their own class's schedule
//   Admin    every slot, with optional teacher / department / semester filters
//
// The page asks GET /api/timetable and renders exactly what comes back as a
// weekly grid (weekdays across, time slots down). Which slots those are is
// decided by the server; there is no client-side role filtering to bypass.

import { el, mount, statusBar } from '../dom.js';
import { Role } from '../session.js';

export const DAYS = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'];

/**
 * Lays slots out as rows of time ranges against weekday columns.
 * Pure (no DOM), so it is unit-tested directly.
 *
 * @returns {{ days: number[], times: string[], cells: Map<string, object[]> }}
 *   `days` are ISO weekdays to show (Mon–Fri always, Sat/Sun only if used),
 *   `times` are "HH:MM–HH:MM" row keys in order, and `cells` maps
 *   "<day>|<time>" to the slots in that cell.
 */
export function layoutWeek(items) {
  const days = [1, 2, 3, 4, 5];
  for (const extra of [6, 7]) if (items.some((s) => s.dayOfWeek === extra)) days.push(extra);

  const timeKey = (s) => `${s.startTime}–${s.endTime}`;
  const times = [...new Set(items.map(timeKey))].sort();
  const cells = new Map();
  for (const slot of items) {
    const key = `${slot.dayOfWeek}|${timeKey(slot)}`;
    if (!cells.has(key)) cells.set(key, []);
    cells.get(key).push(slot);
  }
  return { days, times, cells };
}

export function createTimetablePage({ api, session, onUnauthorized }) {
  const isAdmin = session.role === Role.Admin;
  const status = statusBar();
  const grid = el('div', { class: 'table-wrap timetable-wrap' });
  const scopeLine = el('p', { class: 'muted small' });

  // Filters are an Admin tool. A Teacher or Student has exactly one schedule,
  // so there is nothing to choose — and the server refuses anything else.
  let filters = null;
  let toolbar = null;
  let builder = null;
  if (isAdmin) {
    filters = {
      teacher: el('input', { type: 'search', placeholder: 'Teacher username', 'aria-label': 'Teacher username', autocomplete: 'off' }),
      department: el('input', { type: 'search', placeholder: 'Department', 'aria-label': 'Department', autocomplete: 'off' }),
      semester: el(
        'select',
        { 'aria-label': 'Semester' },
        el('option', { value: '0' }, 'All semesters'),
        ...Array.from({ length: 8 }, (_, i) => el('option', { value: String(i + 1) }, `Semester ${i + 1}`)),
      ),
    };
    toolbar = el(
      'form',
      {
        class: 'toolbar record-toolbar',
        onsubmit: (event) => {
          event.preventDefault();
          load();
        },
      },
      filters.teacher,
      filters.department,
      filters.semester,
      el('button', { type: 'submit', class: 'primary' }, 'Apply'),
    );

    const day = el('select', { name: 'dayOfWeek', required: true, 'aria-label': 'Day' },
      ...DAYS.map((name, index) => el('option', { value: String(index + 1) }, name)));
    const start = el('input', { type: 'time', name: 'startTime', required: true, 'aria-label': 'Start time' });
    const end = el('input', { type: 'time', name: 'endTime', required: true, 'aria-label': 'End time' });
    const course = el('input', { name: 'courseCode', required: true, maxLength: 32, placeholder: 'Course code', 'aria-label': 'Course code' });
    const teacher = el('input', { name: 'teacher', required: true, maxLength: 64, placeholder: 'Teacher username', 'aria-label': 'Teacher username' });
    const department = el('input', { name: 'department', required: true, maxLength: 100, placeholder: 'Department', 'aria-label': 'Department' });
    const semester = el('input', { type: 'number', name: 'semester', required: true, min: 1, max: 12, step: 1, placeholder: 'Semester', 'aria-label': 'Semester' });
    const room = el('input', { name: 'room', required: true, maxLength: 32, placeholder: 'Room', 'aria-label': 'Room' });
    builder = el('form', {
      class: 'toolbar record-toolbar slot-builder',
      'aria-label': 'Create timetable slot',
      onsubmit: async (event) => {
        event.preventDefault();
        const submit = builder.querySelector('button[type="submit"]');
        submit.disabled = true;
        status.set('Creating timetable slot…', 'info');
        try {
          await api.createTimetableSlot({
            dayOfWeek: Number(day.value),
            startTime: start.value,
            endTime: end.value,
            courseCode: course.value.trim(),
            teacher: teacher.value.trim(),
            department: department.value.trim(),
            semester: Number(semester.value),
            room: room.value.trim(),
          });
          builder.reset();
          await load();
          status.set('Timetable slot created.', 'success');
        } catch (error) {
          status.set((error && error.message) || 'Could not create the timetable slot.', 'error');
        } finally {
          submit.disabled = false;
        }
      },
    }, day, start, end, course, teacher, department, semester, room,
    el('button', { type: 'submit', class: 'primary' }, 'Create slot'));
  }

  const title = {
    [Role.Admin]: 'Timetable',
    [Role.Teacher]: 'My teaching schedule',
    [Role.Student]: 'My class timetable',
  }[session.role];

  const root = el(
    'section',
    { class: 'page' },
    el('header', { class: 'page-head' }, el('div', {}, el('h1', {}, title), scopeLine)),
    toolbar,
    builder,
    status.node,
    grid,
  );

  let generation = 0;

  async function load() {
    const mine = ++generation;
    status.set('Loading…', 'info');
    const query = filters
      ? {
          teacher: filters.teacher.value.trim(),
          department: filters.department.value.trim(),
          semester: Number(filters.semester.value) || 0,
        }
      : {};
    try {
      const result = await api.getTimetable(query);
      if (mine !== generation) return;
      render(result);
    } catch (error) {
      if (mine !== generation) return;
      mount(grid);
      scopeLine.textContent = '';
      if (error && error.status === 401 && onUnauthorized) {
        onUnauthorized();
        return;
      }
      status.set((error && error.message) || 'Could not load the timetable.', 'error');
    }
  }

  function describeScope(scope) {
    if (session.role === Role.Teacher) return 'Classes you teach this week.';
    if (session.role === Role.Student && scope.department) {
      return `${scope.department} · Semester ${scope.semester}`;
    }
    const parts = [];
    if (scope.teacher) parts.push(`teacher ${scope.teacher}`);
    if (scope.department) parts.push(scope.department);
    if (scope.semester) parts.push(`semester ${scope.semester}`);
    return parts.length ? `Filtered: ${parts.join(' · ')}` : 'All classes and teachers.';
  }

  function slotCard(slot) {
    // What is worth repeating in a cell depends on who is looking: a Teacher
    // knows who teaches it (they do) but needs the class; a Student knows the
    // class but needs the teacher; an Admin needs both.
    const showTeacher = session.role !== Role.Teacher;
    const showClass = session.role !== Role.Student;
    return el(
      'div',
      { class: 'slot', 'data-course': slot.courseCode },
      el('strong', {}, slot.courseCode),
      el('span', { class: 'slot-name' }, slot.courseName),
      showClass ? el('span', { class: 'slot-meta' }, `${slot.department} · Sem\u00a0${slot.semester}`) : null,
      showTeacher ? el('span', { class: 'slot-meta' }, slot.teacherName || slot.teacher) : null,
      slot.room ? el('span', { class: 'slot-meta' }, `Room ${slot.room}`) : null,
    );
  }

  function render({ scope, items }) {
    scopeLine.textContent = describeScope(scope);
    if (items.length === 0) {
      mount(grid, el('p', { class: 'empty-note' }, 'No classes scheduled.'));
      status.set('No timetable entries.', 'info');
      return;
    }
    const { days, times, cells } = layoutWeek(items);
    const table = el(
      'table',
      { class: 'data-table timetable', id: 'timetable-grid' },
      el(
        'thead',
        {},
        el('tr', {}, el('th', { scope: 'col' }, 'Time'), ...days.map((d) => el('th', { scope: 'col' }, DAYS[d - 1]))),
      ),
      el(
        'tbody',
        {},
        ...times.map((time) =>
          el(
            'tr',
            {},
            el('th', { scope: 'row', class: 'slot-time' }, time),
            ...days.map((d) => el('td', {}, ...(cells.get(`${d}|${time}`) || []).map(slotCard))),
          ),
        ),
      ),
    );
    mount(grid, table);
    status.set(`${items.length} class${items.length === 1 ? '' : 'es'} per week.`, 'info');
  }

  return { root, reload: load };
}
