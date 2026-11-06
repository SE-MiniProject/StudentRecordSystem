// The "whose record?" toolbar shared by the Sprint 2 per-student pages
// (grades SRS-F-014, attendance F-017/F-018).
//
//   Student          no roll-number box: the page asks GET /api/me for the
//                    linked roll number and shows that student's records only.
//   Admin, Teacher   a roll-number box plus a semester filter.
//
// This is presentation only. Hiding the box from a Student is a convenience;
// the server is what refuses a Student another roll number (403), and the
// page shows that refusal if it happens.

import { el } from '../dom.js';
import { Role } from '../session.js';

/**
 * @param {object} options
 * @param {object} options.api
 * @param {object} options.session
 * @param {(target: {rollNo: string, semester: number}) => void} options.onLookup
 * @returns {{ node: HTMLElement, start: () => Promise<void>, current: () => object|null }}
 */
export function createRecordLookup({ api, session, onLookup, onError }) {
  const isStudent = session.role === Role.Student;
  let last = null;

  const rollInput = isStudent
    ? null
    : el('input', {
        type: 'search',
        id: `record-roll-${Math.random().toString(36).slice(2, 7)}`,
        placeholder: 'Roll number, e.g. 1ABC26CS001',
        autocomplete: 'off',
        'aria-label': 'Roll number',
      });

  const semesterSelect = el(
    'select',
    { 'aria-label': 'Semester' },
    el('option', { value: '0' }, 'All semesters'),
    ...Array.from({ length: 8 }, (_, i) => el('option', { value: String(i + 1) }, `Semester ${i + 1}`)),
  );

  const submit = el('button', { type: 'submit', class: 'primary' }, isStudent ? 'Apply' : 'View');

  const node = el(
    'form',
    {
      class: 'toolbar record-toolbar',
      onsubmit: (event) => {
        event.preventDefault();
        fire();
      },
    },
    rollInput,
    semesterSelect,
    submit,
  );

  let ownRollNo = null;

  function fire() {
    const rollNo = isStudent ? ownRollNo : rollInput.value.trim();
    if (!rollNo) {
      if (!isStudent) rollInput.focus();
      return;
    }
    last = { rollNo, semester: Number(semesterSelect.value) || 0 };
    onLookup(last);
  }

  return {
    node,
    current: () => last,
    /** Select a record target from another page action (e.g. after grade entry). */
    setTarget(target) {
      last = { rollNo: target.rollNo, semester: Number(target.semester) || 0 };
      if (rollInput) rollInput.value = last.rollNo;
      semesterSelect.value = String(last.semester);
      return last;
    },
    /** Students: resolve the linked roll number and load it straight away. */
    async start() {
      if (!isStudent) {
        if (last) onLookup(last);
        return;
      }
      if (!ownRollNo) {
        try {
          const me = await api.getMe();
          ownRollNo = me.rollNo;
        } catch (error) {
          onError(error);
          return;
        }
      }
      if (!ownRollNo) {
        onError(new Error('Your account is not linked to a student record. Please contact an administrator.'));
        return;
      }
      fire();
    },
    setBusy(busy) {
      submit.disabled = busy;
      semesterSelect.disabled = busy;
      if (rollInput) rollInput.disabled = busy;
    },
  };
}
