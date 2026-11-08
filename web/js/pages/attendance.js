// Attendance page — SRS-F-017, F-018 (AC-017, AC-018 / TC-017, TC-018).
//
// Same two-layout split as the Grades page:
//   Student          read-only view of their own attendance.
//   Admin, Teacher   populated student list; per-row "View / record attendance"
//                    dialog shows the summary and, for Teachers, a form to
//                    record or update one session.
//
// The percentage shown everywhere is the server's value. It is never recomputed
// here so the two cannot disagree.

import { el, mount, statusBar, openDialog } from '../dom.js';
import { Role } from '../session.js';

export const RISK_THRESHOLD = 75;
const FETCH_LIMIT = 200;

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

function todayIso() {
  return new Date().toLocaleDateString('en-CA');
}

export function createAttendancePage({ api, session, onUnauthorized }) {
  if (session.role === Role.Student) {
    return createStudentView({ api, session, onUnauthorized });
  }
  return createStaffView({ api, session, onUnauthorized });
}

// ---- Student view --------------------------------------------------------

function createStudentView({ api, session, onUnauthorized }) {
  const status = statusBar();
  const tableBody = el('tbody');
  const tableFoot = el('tfoot');

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

  const root = el(
    'section',
    { class: 'page' },
    el(
      'header',
      { class: 'page-head' },
      el(
        'div',
        {},
        el('h1', {}, 'My attendance'),
        el('p', { class: 'muted small' }, 'Your own attendance. Percentage = present ÷ conducted × 100.'),
      ),
    ),
    status.node,
    el('div', { class: 'table-wrap' }, table),
  );

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

  async function load() {
    status.set('Loading…', 'info');
    try {
      const me = await api.getMe();
      if (!me.rollNo) {
        status.set('Your account is not linked to a student record. Please contact an administrator.', 'error');
        return;
      }
      const result = await api.getAttendance(me.rollNo);
      render(result);
    } catch (error) {
      mount(tableBody);
      mount(tableFoot);
      if (error.status === 401 && onUnauthorized) return onUnauthorized();
      status.set(error.message || 'Could not load attendance.', 'error');
    }
  }

  function render({ studentRollNo, items, overall }) {
    if (items.length === 0) {
      mount(tableBody, el('tr', {}, el('td', { class: 'empty', colSpan: 7 }, 'No attendance recorded.')));
      mount(tableFoot);
      status.set(`Roll number ${studentRollNo} · no attendance found.`, 'info');
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
        ? `Roll number ${studentRollNo} · ${atRisk.length} course${atRisk.length === 1 ? '' : 's'} below ${RISK_THRESHOLD}%.`
        : `Roll number ${studentRollNo} · ${items.length} course${items.length === 1 ? '' : 's'}.`,
      atRisk.length ? 'error' : 'info',
    );
  }

  return { root, reload: load };
}

// ---- Staff view ----------------------------------------------------------

function createStaffView({ api, session, onUnauthorized }) {
  const canRecord = session.role === Role.Teacher;
  const status = statusBar();
  const tableBody = el('tbody');

  const state = {
    students: [],
    search: '',
    semester: 0,
    teacherClasses: null,
  };

  const searchInput = el('input', {
    type: 'search',
    id: 'attendance-student-search',
    placeholder: 'Roll number or name',
    autocomplete: 'off',
  });
  const semesterSelect = el(
    'select',
    { id: 'attendance-student-semester', 'aria-label': 'Semester' },
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
    { class: 'data-table', id: 'attendance-table' },
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

  const scopeNote = canRecord
    ? 'Students in your assigned classes. Record or update attendance for your own subjects.'
    : 'Attendance for any student. Session entry is restricted to assigned teachers.';

  const root = el(
    'section',
    { class: 'page' },
    el(
      'header',
      { class: 'page-head' },
      el('div', {}, el('h1', {}, 'Attendance'), el('p', { class: 'muted small' }, scopeNote)),
    ),
    searchForm,
    status.node,
    el('div', { class: 'table-wrap' }, table),
  );

  // Same TODO(server) as the Grades page: replace with a server-side
  // ?taughtBy=self filter once an enrollment table exists.
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
      if (canRecord && state.teacherClasses === null) {
        state.teacherClasses = await loadTeacherClasses();
      }
      const page = await api.searchStudents({
        search: state.search,
        semester: state.semester,
        limit: FETCH_LIMIT,
        offset: 0,
      });
      let students = page.items;
      if (canRecord && state.teacherClasses.length > 0) {
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
        canRecord && state.teacherClasses && state.teacherClasses.length === 0
          ? 'Your class assignments could not be determined. Ask an administrator to timetable you or assign you to subjects.'
          : canRecord
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
          { type: 'button', class: 'link', onclick: () => openAttendanceDialog(student) },
          canRecord ? 'View / record attendance' : 'View attendance',
        ),
      ),
    );
  }

  function setBusy(busy) {
    searchButton.disabled = busy;
    searchInput.disabled = busy;
    semesterSelect.disabled = busy;
  }

  async function openAttendanceDialog(student) {
    const dialogStatus = statusBar();
    const summaryBody = el('tbody');
    const summaryFoot = el('tfoot');
    const summaryTable = el(
      'table',
      { class: 'data-table' },
      el(
        'thead',
        {},
        el(
          'tr',
          {},
          el('th', { scope: 'col' }, 'Course'),
          el('th', { scope: 'col', class: 'num' }, 'Present'),
          el('th', { scope: 'col', class: 'num' }, 'Conducted'),
          el('th', { scope: 'col', class: 'num' }, 'Percentage'),
          el('th', { scope: 'col' }, ''),
        ),
      ),
      summaryBody,
      summaryFoot,
    );

    let formEl = null;
    let courseInput = null;
    let dateInput = null;
    let statusSelect = null;
    let operationSelect = null;
    let submitButton = null;

    if (canRecord) {
      courseInput = el('input', { type: 'text', name: 'courseCode', placeholder: 'e.g. CS501', required: true, maxlength: 32 });
      dateInput = el('input', { type: 'date', name: 'sessionDate', required: true, max: todayIso() });
      statusSelect = el('select', { name: 'attendanceStatus', required: true },
        el('option', { value: 'present' }, 'Present'),
        el('option', { value: 'absent' }, 'Absent'));
      operationSelect = el('select', { name: 'operation', required: true },
        el('option', { value: 'create' }, 'Record new session'),
        el('option', { value: 'update' }, 'Update existing session'));
      submitButton = el('button', { type: 'submit', class: 'primary' }, 'Save attendance');

      formEl = el(
        'form',
        {
          class: 'toolbar',
          onsubmit: async (event) => {
            event.preventDefault();
            const entry = {
              rollNo: student.rollNo,
              courseCode: courseInput.value.trim(),
              sessionDate: dateInput.value,
              status: statusSelect.value,
            };
            if (!entry.courseCode || !entry.sessionDate) {
              dialogStatus.set('Enter a course code and a session date.', 'error');
              return;
            }
            submitButton.disabled = true;
            dialogStatus.set('Saving attendance…', 'info');
            try {
              if (operationSelect.value === 'create') await api.createAttendance(entry);
              else await api.updateAttendance(entry);
              await refreshSummary();
              dialogStatus.set('Attendance saved.', 'success');
            } catch (error) {
              if (error.status === 401 && onUnauthorized) return onUnauthorized();
              dialogStatus.set(error.message || 'Could not save attendance.', 'error');
            } finally {
              submitButton.disabled = false;
            }
          },
        },
        el('strong', {}, 'Record or update one session'),
        courseInput,
        dateInput,
        statusSelect,
        operationSelect,
        submitButton,
      );
    }

    async function refreshSummary() {
      try {
        const result = await api.getAttendance(student.rollNo);
        const { items, overall } = result;
        if (items.length === 0) {
          mount(summaryBody, el('tr', {}, el('td', { class: 'empty', colSpan: 5 }, 'No attendance recorded.')));
          mount(summaryFoot);
        } else {
          mount(
            summaryBody,
            ...items.map((item) =>
              el(
                'tr',
                {},
                el('td', {}, `${item.courseCode} — ${item.courseName || item.courseCode}`),
                el('td', { class: 'num' }, String(item.present)),
                el('td', { class: 'num' }, String(item.conducted)),
                el('td', { class: 'num', 'data-percentage': item.percentage === null ? '' : String(item.percentage) }, formatPercentage(item.percentage)),
                el('td', {}, riskTag(item.percentage)),
              ),
            ),
          );
          mount(
            summaryFoot,
            el(
              'tr',
              { class: 'total-row' },
              el('th', { scope: 'row' }, 'Overall'),
              el('td', { class: 'num' }, String(overall.present)),
              el('td', { class: 'num' }, String(overall.conducted)),
              el('td', { class: 'num', 'data-percentage': overall.percentage === null ? '' : String(overall.percentage) }, formatPercentage(overall.percentage)),
              el('td', {}),
            ),
          );
        }
      } catch (error) {
        if (error.status === 401 && onUnauthorized) return onUnauthorized();
        mount(summaryBody, el('tr', {}, el('td', { class: 'empty', colSpan: 5 }, error.message || 'Could not load attendance.')));
        mount(summaryFoot);
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
      el('div', { class: 'table-wrap' }, summaryTable),
    );

    openDialog(`Attendance — ${student.fullName}`, body);
    await refreshSummary();
  }

  return { root, reload: loadStudents };
}
