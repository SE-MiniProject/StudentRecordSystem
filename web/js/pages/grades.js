// Grades page — SRS-F-012 / F-013 / F-014 (AC-012 / AC-013 / AC-014).
//
//   Teacher  may create or update grades for subjects assigned to them.
//   Student  sees their own grades only (roll number resolved via GET /api/me).
//   Admin    may view every grade but cannot enter grades (SRS-F-012).
//
// The server remains authoritative for role, subject assignment and grade range.
// No average is shown — F-012 forbids cross-subject averaging.

import { el, field, mount, setFieldError, statusBar } from '../dom.js';
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
  const canEnterGrades = session.role === Role.Teacher;
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
    [Role.Admin]: 'You can view every grade for any student. Grade entry is restricted to assigned teachers.',
    [Role.Teacher]: 'You see grades only for subjects assigned to you. You can add or update grades for those subjects.',
    [Role.Student]: 'Your own grades.',
  }[session.role];

  let entryForm = null;
  let entryFields = null;
  if (canEnterGrades) {
    const rollInput = el('input', {
      type: 'text',
      id: 'grade-entry-roll',
      placeholder: 'e.g. 1ABC26CS001',
      autocomplete: 'off',
      required: true,
      maxlength: 32,
    });
    const courseInput = el('input', {
      type: 'text',
      id: 'grade-entry-course',
      placeholder: 'e.g. CS501',
      autocomplete: 'off',
      required: true,
      maxlength: 32,
    });
    const gradeInput = el('input', {
      type: 'number',
      id: 'grade-entry-value',
      min: '0',
      max: '10',
      step: '0.1',
      placeholder: '0.0–10.0',
      required: true,
    });
    const rollField = field('Student roll number', rollInput);
    const courseField = field('Subject code', courseInput);
    const gradeField = field('Grade', gradeInput, { hint: 'Enter a value from 0.0 to 10.0.' });
    const saveButton = el('button', { type: 'submit', class: 'primary' }, 'Save grade');
    entryFields = { rollInput, courseInput, gradeInput, rollField, courseField, gradeField, saveButton };
    entryForm = el(
      'form',
      {
        class: 'toolbar grade-entry-form',
        'aria-label': 'Add or update a grade',
        onsubmit: submitGrade,
      },
      rollField,
      courseField,
      gradeField,
      saveButton,
    );
  }

  const root = el(
    'section',
    { class: 'page' },
    el(
      'header',
      { class: 'page-head' },
      el('div', {}, el('h1', {}, isStudent ? 'My grades' : 'Grades'), el('p', { class: 'muted small' }, scopeNote)),
    ),
    entryForm,
    lookup.node,
    status.node,
    el('div', { class: 'table-wrap' }, table),
  );

  if (!isStudent) {
    status.set(canEnterGrades ? 'Enter a student, an assigned subject code and a grade to add or update it.' : 'Enter a roll number to view grades.', 'info');
  }

  let generation = 0;

  async function submitGrade(event) {
    event.preventDefault();
    if (!entryFields) return;

    const rollNo = entryFields.rollInput.value.trim();
    const courseCode = entryFields.courseInput.value.trim();
    const grade = Number(entryFields.gradeInput.value);
    setFieldError(entryFields.rollField, '');
    setFieldError(entryFields.courseField, '');
    setFieldError(entryFields.gradeField, '');

    if (!rollNo || rollNo.length > 32) {
      setFieldError(entryFields.rollField, 'Enter a roll number of at most 32 characters.');
      entryFields.rollInput.focus();
      return;
    }
    if (!courseCode || courseCode.length > 32) {
      setFieldError(entryFields.courseField, 'Enter a subject code of at most 32 characters.');
      entryFields.courseInput.focus();
      return;
    }
    if (entryFields.gradeInput.value === '' || !Number.isFinite(grade) || grade < 0 || grade > 10 || Math.abs(grade * 10 - Math.round(grade * 10)) > 1e-9) {
      setFieldError(entryFields.gradeField, 'Enter a grade from 0.0 to 10.0 in 0.1 increments.');
      entryFields.gradeInput.focus();
      return;
    }

    setEntryBusy(true);
    status.set('Saving grade…', 'info');
    try {
      const result = await api.upsertGrade({ studentRollNo: rollNo, courseCode, grade });
      status.set(result.created ? 'Grade added.' : 'Grade updated.', 'success');
      entryFields.gradeInput.value = '';
      const current = lookup.current();
      const target = lookup.setTarget({
        rollNo,
        semester: current && current.rollNo === rollNo ? current.semester : 0,
      });
      await load(target);
    } catch (error) {
      if (error && error.status === 401 && onUnauthorized) {
        onUnauthorized();
        return;
      }
      if (error && error.fields && error.fields.grade) {
        setFieldError(entryFields.gradeField, error.fields.grade);
      }
      status.set((error && error.message) || 'Could not save grade.', 'error');
    } finally {
      setEntryBusy(false);
    }
  }

  function setEntryBusy(busy) {
    if (!entryFields) return;
    for (const control of [
      entryFields.rollInput,
      entryFields.courseInput,
      entryFields.gradeInput,
      entryFields.saveButton,
    ]) {
      control.disabled = busy;
    }
  }

  async function load({ rollNo, semester }) {
    const mine = ++generation;
    lookup.setBusy(true);
    status.set('Loading…', 'info');
    try {
      const result = await api.getGrades(rollNo, { semester });
      if (mine !== generation) return;
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
    reload: () => lookup.start(),
  };
}
