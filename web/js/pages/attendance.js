// Attendance page — SRS-F-017, F-018 (AC-017, AC-018 / TC-017, TC-018).
//
// Per-course attendance with a percentage column, plus an overall row. The
// percentage shown is the server's value (present / conducted * 100, rounded
// to two decimals server-side); this page only formats it for display and
// never recomputes it, so the client and server cannot disagree.
//
// Scoping is the server's, exactly as on the Grades page: Student own only,
// Teacher own subjects, Admin everything.

import { el, mount, statusBar } from '../dom.js';
import { Role } from '../session.js';
import { createRecordLookup } from './record-lookup.js';

// Below this the row is flagged so absence risk is visible at a glance
// (the story's "monitor absence risk immediately"). Presentation only.
export const RISK_THRESHOLD = 75;

/** "85.00%" from the server's number; "—" when nothing was conducted. */
export function formatPercentage(value) {
  return typeof value === 'number' && Number.isFinite(value) ? `${value.toFixed(2)}%` : '—';
}

function riskTag(value) {
  if (typeof value !== 'number') return null;
  return value < RISK_THRESHOLD
    ? el('span', { class: 'tag tag-risk', title: `Below ${RISK_THRESHOLD}%` }, 'At risk')
    : null;
}

export function createAttendancePage({ api, session, onUnauthorized }) {
  const isStudent = session.role === Role.Student;
  const status = statusBar();
  const tableBody = el('tbody');
  const tableFoot = el('tfoot');

  const lookup = createRecordLookup({ api, session, onLookup: load, onError: showError });

  const table = el(
    'table',
    { class: 'data-table', id: 'attendance-table' },
    el(
      'thead',
      {},
      el(
        'tr',
        {},
        el('th', { scope: 'col' }, 'Course code'),
        el('th', { scope: 'col' }, 'Course'),
        el('th', { scope: 'col' }, 'Semester'),
        el('th', { scope: 'col', class: 'num' }, 'Present'),
        el('th', { scope: 'col', class: 'num' }, 'Conducted'),
        el('th', { scope: 'col', class: 'num' }, 'Percentage'),
        el('th', { scope: 'col' }, ''),
      ),
    ),
    tableBody,
    tableFoot,
  );

  const scopeNote = {
    [Role.Admin]: 'Attendance for any student, every subject.',
    [Role.Teacher]: 'Attendance only for the subjects assigned to you.',
    [Role.Student]: 'Your own attendance.',
  }[session.role];

  const root = el(
    'section',
    { class: 'page' },
    el(
      'header',
      { class: 'page-head' },
      el(
        'div',
        {},
        el('h1', {}, isStudent ? 'My attendance' : 'Attendance'),
        el('p', { class: 'muted small' }, `${scopeNote} Percentage = present ÷ conducted × 100.`),
      ),
    ),
    lookup.node,
    status.node,
    el('div', { class: 'table-wrap' }, table),
  );

  if (!isStudent) status.set('Enter a roll number to view attendance.', 'info');

  let generation = 0;

  async function load({ rollNo, semester }) {
    const mine = ++generation;
    lookup.setBusy(true);
    status.set('Loading…', 'info');
    try {
      const result = await api.getAttendance(rollNo, { semester });
      if (mine !== generation) return;
      render(result);
    } catch (error) {
      if (mine !== generation) return;
      showError(error);
    } finally {
      if (mine === generation) lookup.setBusy(false);
    }
  }

  function row(cells, { className = '', heading = false } = {}) {
    const [code, name, semester, present, conducted, pct] = cells;
    const first = heading ? el('th', { scope: 'row' }, code) : el('td', {}, code);
    return el(
      'tr',
      { class: className },
      first,
      el('td', {}, name),
      el('td', {}, semester),
      el('td', { class: 'num' }, String(present)),
      el('td', { class: 'num' }, String(conducted)),
      el('td', { class: 'num', 'data-percentage': pct === null ? '' : String(pct) }, formatPercentage(pct)),
      el('td', {}, riskTag(pct)),
    );
  }

  function render({ studentRollNo, items, overall }) {
    const who = `Roll number ${studentRollNo}`;
    if (items.length === 0) {
      mount(tableBody, el('tr', {}, el('td', { class: 'empty', colSpan: 7 }, 'No attendance recorded.')));
      mount(tableFoot);
      status.set(`${who} · no attendance found.`, 'info');
      return;
    }
    mount(
      tableBody,
      ...items.map((item) =>
        row([
          item.courseCode,
          item.courseName || item.courseCode,
          item.semester ? String(item.semester) : '—',
          item.present,
          item.conducted,
          item.percentage,
        ]),
      ),
    );
    mount(
      tableFoot,
      row(['Overall', '', '', overall.present, overall.conducted, overall.percentage], {
        className: 'total-row',
        heading: true,
      }),
    );
    const atRisk = items.filter((i) => typeof i.percentage === 'number' && i.percentage < RISK_THRESHOLD);
    status.set(
      atRisk.length
        ? `${who} · ${atRisk.length} course${atRisk.length === 1 ? '' : 's'} below ${RISK_THRESHOLD}%.`
        : `${who} · ${items.length} course${items.length === 1 ? '' : 's'}.`,
      atRisk.length ? 'error' : 'info',
    );
  }

  function showError(error) {
    mount(tableBody);
    mount(tableFoot);
    if (error && error.status === 401 && onUnauthorized) {
      onUnauthorized();
      return;
    }
    status.set((error && error.message) || 'Could not load attendance.', 'error');
  }

  return { root, reload: () => lookup.start() };
}
