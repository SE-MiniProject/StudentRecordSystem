// Grades page — SRS-F-014 (AC-014 / TC-014).
//
//   Student  sees their own grades only (roll number resolved via GET /api/me)
//   Teacher  looks up a student and sees grades in their own subjects only
//   Admin    looks up a student and sees every grade
//
// The page never filters by role itself: whatever the server returns is what
// this caller is allowed to see, and a refused request (403) is shown as such.
// No average is shown — F-012 forbids cross-subject averaging.

import { el, mount, statusBar } from '../dom.js';
import { Role } from '../session.js';
import { createRecordLookup } from './record-lookup.js';

function formatGrade(value) {
  return typeof value === 'number' && Number.isFinite(value) ? value.toFixed(1) : '—';
}

function formatUpdated(iso) {
  if (!iso) return '—';
  const date = new Date(iso);
  return Number.isNaN(date.getTime()) ? iso : date.toLocaleString();
}

export function createGradesPage({ api, session, onUnauthorized }) {
  const isStudent = session.role === Role.Student;
  const status = statusBar();
  const tableBody = el('tbody');

  const lookup = createRecordLookup({
    api,
    session,
    onLookup: load,
    onError: showError,
  });

  const table = el(
    'table',
    { class: 'data-table', id: 'grades-table' },
    el(
      'thead',
      {},
      el(
        'tr',
        {},
        el('th', { scope: 'col' }, 'Course code'),
        el('th', { scope: 'col' }, 'Course'),
        el('th', { scope: 'col' }, 'Semester'),
        el('th', { scope: 'col', class: 'num' }, 'Grade (0–10)'),
        el('th', { scope: 'col' }, 'Last updated'),
      ),
    ),
    tableBody,
  );

  const scopeNote = {
    [Role.Admin]: 'You can view every grade for any student.',
    [Role.Teacher]: 'You see grades only for the subjects assigned to you.',
    [Role.Student]: 'Your own grades.',
  }[session.role];

  const root = el(
    'section',
    { class: 'page' },
    el(
      'header',
      { class: 'page-head' },
      el('div', {}, el('h1', {}, isStudent ? 'My grades' : 'Grades'), el('p', { class: 'muted small' }, scopeNote)),
    ),
    lookup.node,
    status.node,
    el('div', { class: 'table-wrap' }, table),
  );

  if (!isStudent) {
    status.set('Enter a roll number to view grades.', 'info');
  }

  let generation = 0;

  async function load({ rollNo, semester }) {
    const mine = ++generation;
    lookup.setBusy(true);
    status.set('Loading…', 'info');
    try {
      const result = await api.getGrades(rollNo, { semester });
      if (mine !== generation) return; // a newer lookup superseded this one
      render(result);
    } catch (error) {
      if (mine !== generation) return;
      showError(error);
    } finally {
      if (mine === generation) lookup.setBusy(false);
    }
  }

  function render({ studentRollNo, items }) {
    if (items.length === 0) {
      mount(
        tableBody,
        el('tr', {}, el('td', { class: 'empty', colSpan: 5 }, 'No grades to show.')),
      );
      status.set(`Roll number ${studentRollNo} · no grades found.`, 'info');
      return;
    }
    mount(
      tableBody,
      ...items.map((item) =>
        el(
          'tr',
          {},
          el('td', {}, item.courseCode),
          el('td', {}, item.courseName || item.courseCode),
          el('td', {}, item.semester ? String(item.semester) : '—'),
          el('td', { class: 'num' }, formatGrade(item.grade)),
          el('td', {}, formatUpdated(item.updatedAt)),
        ),
      ),
    );
    status.set(`Roll number ${studentRollNo} · ${items.length} grade${items.length === 1 ? '' : 's'}.`, 'info');
  }

  function showError(error) {
    mount(tableBody);
    if (error && error.status === 401 && onUnauthorized) {
      onUnauthorized();
      return;
    }
    status.set((error && error.message) || 'Could not load grades.', 'error');
  }

  return {
    root,
    // Called by the shell every time the page is shown, so a Student's grades
    // refresh and a staff lookup is re-run with the same roll number.
    reload: () => lookup.start(),
  };
}
