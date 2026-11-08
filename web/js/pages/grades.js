// Grades page — SRS-F-012 / F-013 / F-014 (AC-012 / AC-013 / AC-014).
//
// Two layouts:
//   Student          read-only view of their own grades, resolved via GET /api/me.
//   Admin, Teacher   a populated student list with per-row "View / edit grades"
//                    actions that open a dialog. Search narrows the list.
//                    Grade entry (create / update) happens inside the dialog and
//                    is restricted to Teacher accounts; Admin sees the same
//                    dialog without the form.
//
// Scoping is the server's job, not this page's:
//   - GET /api/students returns what the caller may list
//   - GET /api/grades/{rollNo} returns what the caller may see (Teacher: only
//     grades in their assigned subjects; another Student's roll: 403)
//   - POST /api/grades rejects a teacher not assigned to the subject (403)
//
// The client-side teacher narrowing below is a usability filter on the list,
// derived from the teacher's own timetable. It is not a security boundary;
// every action that reaches the server is re-authorised there.

import { el, mount, statusBar, openDialog } from '../dom.js';
import { Role } from '../session.js';

const FETCH_LIMIT = 200;

export function createGradesPage({ api, session, onUnauthorized }) {
  if (session.role === Role.Student) {
    return createStudentView({ api, session, onUnauthorized });
  }
  return createStaffView({ api, session, onUnauthorized });
}

// ---- Students see their own grades directly, with no list and no actions ----

function createStudentView({ api, session, onUnauthorized }) {
  const status = statusBar();
  const tableBody = el('tbody');

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

  const root = el(
    'section',
    { class: 'page' },
    el(
      'header',
      { class: 'page-head' },
      el('div', {}, el('h1', {}, 'My grades'), el('p', { class: 'muted small' }, 'Your own grades.')),
    ),
    status.node,
    el('div', { class: 'table-wrap' }, table),
  );

  async function load() {
    status.set('Loading…', 'info');
    try {
      const me = await api.getMe();
      if (!me.rollNo) {
        status.set('Your account is not linked to a student record. Please contact an administrator.', 'error');
        return;
      }
      const { items } = await api.getGrades(me.rollNo);
      render(me.rollNo, items);
    } catch (error) {
      if (error.status === 401 && onUnauthorized) return onUnauthorized();
      status.set(error.message || 'Could not load grades.', 'error');
    }
  }

  function render(rollNo, items) {
    if (items.length === 0) {
      mount(tableBody, el('tr', {}, el('td', { class: 'empty', colSpan: 5 }, 'No grades to show.')));
      status.set(`Roll number ${rollNo} · no grades found.`, 'info');
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
          el('td', { class: 'num' }, Number(item.grade).toFixed(1)),
          el('td', {}, item.updatedAt ? new Date(item.updatedAt).toLocaleString() : '—'),
        ),
      ),
    );
    status.set(`Roll number ${rollNo} · ${items.length} grade${items.length === 1 ? '' : 's'}.`, 'info');
  }

  return { root, reload: load };
}

// ---- Admin and Teacher: populated student list + per-row grade dialog ------

function createStaffView({ api, session, onUnauthorized }) {
  const canEnterGrades = session.role === Role.Teacher;
  const status = statusBar();
  const tableBody = el('tbody');

  const state = {
    students: [],
    search: '',
    semester: 0,
    loading: false,
    teacherClasses: null,
  };

  const searchInput = el('input', {
    type: 'search',
    id: 'grade-student-search',
    placeholder: 'Roll number or name',
    autocomplete: 'off',
  });
  const semesterSelect = el(
    'select',
    { id: 'grade-student-semester', 'aria-label': 'Semester' },
    el('option', { value: '0' }, 'All semesters'),
    ...Array.from({ length: 8 }, (_, i) => el('option', { value: String(i + 1) }, `Semester ${i + 1}`)),
  );
  const searchButton = el('button', { type: 'submit', class: 'primary' }, 'Search');

  const searchForm = el(
    'form',
    {
      class: 'toolbar',
      onsubmit: (event) => {
        event.preventDefault();
        state.search = searchInput.value.trim();
        state.semester = Number(semesterSelect.value) || 0;
        loadStudents();
      },
    },
    searchInput,
    semesterSelect,
    searchButton,
  );

  const table = el(
    'table',
    { class: 'data-table', id: 'grades-table' },
    el(
      'thead',
      {},
      el(
        'tr',
        {},
        el('th', { scope: 'col' }, 'Roll No.'),
        el('th', { scope: 'col' }, 'Name'),
        el('th', { scope: 'col' }, 'Department'),
        el('th', { scope: 'col' }, 'Semester'),
        el('th', { scope: 'col' }, 'Actions'),
      ),
    ),
    tableBody,
  );

  const scopeNote = canEnterGrades
    ? 'Students in your assigned classes. Add or update grades for your own subjects.'
    : 'Grades for any student. Grade entry is restricted to assigned teachers.';

  const root = el(
    'section',
    { class: 'page' },
    el(
      'header',
      { class: 'page-head' },
      el('div', {}, el('h1', {}, 'Grades'), el('p', { class: 'muted small' }, scopeNote)),
    ),
    searchForm,
    status.node,
    el('div', { class: 'table-wrap' }, table),
  );

  // TODO(server): replace with GET /api/students?taughtBy=self once an
  // enrollment table exists. Today the teacher's "assigned classes" are
  // approximated from timetable slots: any (department, semester) the teacher
  // has a slot in is assumed to contain students they may grade.
  async function loadTeacherClasses() {
    try {
      const { items } = await api.getTimetable();
      const byKey = new Map();
      for (const slot of items) {
        byKey.set(`${slot.department}|${slot.semester}`, {
          department: slot.department,
          semester: slot.semester,
        });
      }
      return Array.from(byKey.values());
    } catch {
      return [];
    }
  }

  async function loadStudents() {
    setBusy(true);
    status.set('Loading…', 'info');
    try {
      if (canEnterGrades && state.teacherClasses === null) {
        state.teacherClasses = await loadTeacherClasses();
      }

      const page = await api.searchStudents({
        search: state.search,
        semester: state.semester,
        limit: FETCH_LIMIT,
        offset: 0,
      });

      let students = page.items;
      if (canEnterGrades && state.teacherClasses.length > 0) {
        students = students.filter((s) =>
          state.teacherClasses.some(
            (c) => c.department === s.department && c.semester === s.semester,
          ),
        );
      }

      state.students = students;
      render();
    } catch (error) {
      mount(tableBody);
      if (error.status === 401 && onUnauthorized) return onUnauthorized();
      status.set(error.message || 'Could not load students.', 'error');
    } finally {
      setBusy(false);
    }
  }

  function render() {
    if (state.students.length === 0) {
      const msg =
        canEnterGrades && state.teacherClasses && state.teacherClasses.length === 0
          ? 'Your class assignments could not be determined. Ask an administrator to timetable you or assign you to subjects.'
          : canEnterGrades
            ? 'No students in your assigned classes.'
            : 'No students found.';
      mount(tableBody, el('tr', {}, el('td', { class: 'empty', colSpan: 5 }, msg)));
      status.set(msg, 'info');
      return;
    }
    mount(tableBody, ...state.students.map(renderRow));
    status.set(`${state.students.length} student${state.students.length === 1 ? '' : 's'}.`, 'info');
  }

  function renderRow(student) {
    return el(
      'tr',
      {},
      el('td', {}, student.rollNo),
      el('td', {}, student.fullName),
      el('td', {}, student.department),
      el('td', {}, String(student.semester)),
      el(
        'td',
        { class: 'row-actions' },
        el(
          'button',
          { type: 'button', class: 'link', onclick: () => openGradeDialog(student) },
          canEnterGrades ? 'View / edit grades' : 'View grades',
        ),
      ),
    );
  }

  function setBusy(busy) {
    state.loading = busy;
    searchButton.disabled = busy;
    searchInput.disabled = busy;
    semesterSelect.disabled = busy;
  }

  async function openGradeDialog(student) {
    const dialogStatus = statusBar();
    const gradeTableBody = el('tbody');
    const gradeTable = el(
      'table',
      { class: 'data-table' },
      el(
        'thead',
        {},
        el(
          'tr',
          {},
          el('th', { scope: 'col' }, 'Course'),
          el('th', { scope: 'col' }, 'Code'),
          el('th', { scope: 'col', class: 'num' }, 'Grade'),
          el('th', { scope: 'col' }, 'Updated'),
        ),
      ),
      gradeTableBody,
    );

    let courseInput = null;
    let gradeInput = null;
    let saveButton = null;
    let formEl = null;

    if (canEnterGrades) {
      courseInput = el('input', {
        type: 'text',
        name: 'courseCode',
        placeholder: 'e.g. CS501',
        required: true,
        maxlength: 32,
      });
      gradeInput = el('input', {
        type: 'number',
        name: 'grade',
        min: '0',
        max: '10',
        step: '0.1',
        placeholder: '0.0–10.0',
        required: true,
      });
      saveButton = el('button', { type: 'submit', class: 'primary' }, 'Save grade');
      formEl = el(
        'form',
        {
          class: 'toolbar',
          onsubmit: async (event) => {
            event.preventDefault();
            const code = courseInput.value.trim();
            const grade = Number(gradeInput.value);
            if (!code) {
              dialogStatus.set('Enter a course code.', 'error');
              return;
            }
            if (
              !Number.isFinite(grade) ||
              grade < 0 ||
              grade > 10 ||
              Math.abs(grade * 10 - Math.round(grade * 10)) > 1e-9
            ) {
              dialogStatus.set('Grade must be between 0.0 and 10.0 in 0.1 increments.', 'error');
              return;
            }
            saveButton.disabled = true;
            dialogStatus.set('Saving…', 'info');
            try {
              await api.upsertGrade({ studentRollNo: student.rollNo, courseCode: code, grade });
              courseInput.value = '';
              gradeInput.value = '';
              await refreshGrades();
              dialogStatus.set('Grade saved.', 'success');
            } catch (error) {
              if (error.status === 401 && onUnauthorized) return onUnauthorized();
              dialogStatus.set(error.message || 'Could not save grade.', 'error');
            } finally {
              saveButton.disabled = false;
            }
          },
        },
        el('strong', {}, 'Add or update a grade'),
        courseInput,
        gradeInput,
        saveButton,
      );
    }

    async function refreshGrades() {
      try {
        const { items } = await api.getGrades(student.rollNo);
        if (items.length === 0) {
          mount(gradeTableBody, el('tr', {}, el('td', { class: 'empty', colSpan: 4 }, 'No grades yet.')));
        } else {
          mount(
            gradeTableBody,
            ...items.map((g) =>
              el(
                'tr',
                {},
                el('td', {}, g.courseName || g.courseCode),
                el('td', {}, g.courseCode),
                el('td', { class: 'num' }, Number(g.grade).toFixed(1)),
                el('td', {}, g.updatedAt ? new Date(g.updatedAt).toLocaleString() : '—'),
              ),
            ),
          );
        }
      } catch (error) {
        if (error.status === 401 && onUnauthorized) return onUnauthorized();
        mount(
          gradeTableBody,
          el('tr', {}, el('td', { class: 'empty', colSpan: 4 }, error.message || 'Could not load grades.')),
        );
      }
    }

    const body = el(
      'div',
      {},
      el(
        'p',
        { class: 'muted small' },
        `${student.fullName} · ${student.rollNo} · ${student.department} · Semester ${student.semester}`,
      ),
      formEl,
      dialogStatus.node,
      el('div', { class: 'table-wrap' }, gradeTable),
    );

    openDialog(`Grades — ${student.fullName}`, body);
    await refreshGrades();
  }

  return { root, reload: loadStudents };
}
