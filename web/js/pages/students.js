// Students page: search, semester filter, paging, and — for an Admin — create,
// edit and delete.
//
// Ports client/src/students/StudentsWidget.cpp for the search half and adds the
// SRS-20 / SRS-25 / SRS-26 surfaces the Qt client never grew. The list
// behaviour is deliberately the same: 10 per page, "Showing X-Y of Z", a
// "No students found." message on an empty result rather than an error, and
// Previous/Next disabled at the ends.

import { el, field, mount, setFieldError, statusBar, confirmDialog, openDialog } from '../dom.js';
import { canUse, Role } from '../session.js';

const PAGE_SIZE = 10;

// The contract says rollNo/fullName/contact; the old server said
// roll_no/name/contact_details. Same tolerance as the Qt client's pick().
function pick(object, a, b) {
  const first = object[a];
  return first === undefined || first === null || first === '' ? object[b] : first;
}

export function createStudentsPage({ api, session, onUnauthorized }) {
  const state = {
    search: '',
    semester: 0,
    offset: 0,
    total: 0,
    items: [],
    loading: false,
  };

  const mayCreate = canUse(session.role, 'students.create');
  const mayUpdate = canUse(session.role, 'students.update');
  const mayDelete = canUse(session.role, 'students.delete');

  const searchInput = el('input', {
    type: 'search',
    id: 'student-search',
    placeholder: 'Roll number or part of a name',
    autocomplete: 'off',
  });

  const semesterSelect = el(
    'select',
    { id: 'student-semester', 'aria-label': 'Semester' },
    el('option', { value: '0' }, 'All semesters'),
    ...Array.from({ length: 8 }, (_, i) =>
      el('option', { value: String(i + 1) }, `Semester ${i + 1}`),
    ),
  );

  const searchButton = el('button', { type: 'submit', class: 'primary' }, 'Search');
  const addButton = mayCreate
    ? el('button', { type: 'button', class: 'primary' }, '+ Add student')
    : null;

  const status = statusBar();
  const pageLabel = el('span', { class: 'page-label' });
  const prevButton = el('button', { type: 'button', class: 'ghost' }, '< Previous');
  const nextButton = el('button', { type: 'button', class: 'ghost' }, 'Next >');
  const tableBody = el('tbody');

  const table = el(
    'table',
    { class: 'data-table' },
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
        el('th', { scope: 'col' }, 'Contact'),
        mayUpdate || mayDelete ? el('th', { scope: 'col' }, 'Actions') : null,
      ),
    ),
    tableBody,
  );

  const form = el(
    'form',
    {
      class: 'toolbar',
      onsubmit: (event) => {
        event.preventDefault();
        state.search = searchInput.value.trim();
        state.semester = Number(semesterSelect.value) || 0;
        state.offset = 0;
        runSearch();
      },
    },
    searchInput,
    semesterSelect,
    searchButton,
    addButton,
  );

  const root = el(
    'section',
    { class: 'page' },
    el('header', { class: 'page-head' }, el('h1', {}, 'Students')),
    form,
    status.node,
    el('div', { class: 'table-wrap' }, table),
    el(
      'nav',
      { class: 'pager', 'aria-label': 'Pagination' },
      prevButton,
      pageLabel,
      nextButton,
    ),
  );

  prevButton.addEventListener('click', () => {
    state.offset = Math.max(0, state.offset - PAGE_SIZE);
    runSearch();
  });
  nextButton.addEventListener('click', () => {
    state.offset += PAGE_SIZE;
    runSearch();
  });
  if (addButton) addButton.addEventListener('click', () => openStudentForm());
  searchInput.addEventListener('search', () => {
    if (searchInput.value === '') {
      state.search = '';
      state.offset = 0;
      runSearch();
    }
  });

  function setBusy(busy) {
    state.loading = busy;
    searchButton.disabled = busy;
    searchInput.disabled = busy;
    semesterSelect.disabled = busy;
    prevButton.disabled = busy || state.offset === 0;
    nextButton.disabled = busy || state.offset + PAGE_SIZE >= state.total;
  }

  function updatePaging() {
    prevButton.disabled = state.offset === 0;
    nextButton.disabled = state.offset + PAGE_SIZE >= state.total;
    const pages = state.total === 0 ? 1 : Math.ceil(state.total / PAGE_SIZE);
    pageLabel.textContent = `Page ${Math.floor(state.offset / PAGE_SIZE) + 1} of ${pages}`;
  }

  async function runSearch() {
    setBusy(true);
    status.set('Loading…', 'info');
    try {
      const page = await api.searchStudents({
        search: state.search,
        semester: state.semester,
        limit: PAGE_SIZE,
        offset: state.offset,
      });
      render(page);
    } catch (error) {
      renderFailure(error);
    } finally {
      setBusy(false);
      updatePaging();
    }
  }

  function render(page) {
    state.items = page.items;
    state.total = page.total;
    mount(tableBody, ...page.items.map(renderRow));

    if (page.items.length === 0) {
      status.set('No students found.', 'info');
    } else {
      status.set(
        `Showing ${state.offset + 1}-${state.offset + page.items.length} of ${state.total}`,
        'info',
      );
    }
  }

  function renderFailure(error) {
    mount(tableBody);
    state.items = [];
    state.total = 0;
    if (error.status === 401 && onUnauthorized) {
      onUnauthorized();
      return;
    }
    status.set(error.message || 'Could not load students.', 'error');
  }

  function renderRow(student) {
    const rollNo = pick(student, 'rollNo', 'roll_no');
    const actions = [];
    if (mayUpdate) {
      actions.push(
        el(
          'button',
          { type: 'button', class: 'link', onclick: () => openStudentForm(student) },
          'Edit',
        ),
      );
    }
    if (mayDelete) {
      actions.push(
        el(
          'button',
          { type: 'button', class: 'link danger-text', onclick: () => removeStudent(student) },
          'Delete',
        ),
      );
    }

    return el(
      'tr',
      {},
      el('td', {}, rollNo),
      el('td', {}, pick(student, 'fullName', 'name')),
      el('td', {}, student.department),
      el('td', {}, String(student.semester)),
      el('td', {}, pick(student, 'contact', 'contact_details')),
      mayUpdate || mayDelete ? el('td', { class: 'row-actions' }, ...actions) : null,
    );
  }

  async function removeStudent(student) {
    const rollNo = pick(student, 'rollNo', 'roll_no');
    const confirmed = await confirmDialog({
      title: 'Delete student',
      message: `Delete ${pick(student, 'fullName', 'name')} (${rollNo})? This also deletes their grades and attendance and cannot be undone.`,
      confirmLabel: 'Delete',
      danger: true,
    });
    if (!confirmed) return;
    try {
      await api.deleteStudent(rollNo);
      // Deleting the last row of a page would leave it empty; step back one.
      if (state.items.length === 1 && state.offset >= PAGE_SIZE) {
        state.offset -= PAGE_SIZE;
      }
      await runSearch();
      status.set(`Deleted ${rollNo}.`, 'success');
    } catch (error) {
      if (error.status === 401 && onUnauthorized) {
        onUnauthorized();
        return;
      }
      status.set(error.message || 'Could not delete that student.', 'error');
    }
  }

  /**
   * Create (no student) or edit (student given). Create sends the full contract
   * object; edit sends only the fields PUT accepts, since rollNo and dateOfBirth
   * are immutable.
   */
  function openStudentForm(student = null) {
    const editing = Boolean(student);
    const values = student || {};

    const inputs = {
      rollNo: el('input', {
        type: 'text',
        value: pick(values, 'rollNo', 'roll_no') || '',
        required: true,
        disabled: editing,
        autocomplete: 'off',
        placeholder: 'PES2UG23CS101',
      }),
      fullName: el('input', {
        type: 'text',
        value: pick(values, 'fullName', 'name') || '',
        required: true,
        autocomplete: 'name',
      }),
      dateOfBirth: el('input', {
        type: 'date',
        value: values.dateOfBirth || '',
        required: true,
        disabled: editing,
      }),
      department: el('input', {
        type: 'text',
        value: values.department || '',
        required: true,
        placeholder: 'CSE-AIML',
      }),
      semester: el(
        'select',
        { required: true },
        ...Array.from({ length: 8 }, (_, i) =>
          el(
            'option',
            { value: String(i + 1), selected: Number(values.semester) === i + 1 },
            `Semester ${i + 1}`,
          ),
        ),
      ),
      contact: el('input', {
        type: 'email',
        value: pick(values, 'contact', 'contact_details') || '',
        required: true,
        autocomplete: 'email',
      }),
    };

    const wrappers = {
      rollNo: field('Roll number', inputs.rollNo, {
        hint: editing ? 'Roll numbers cannot be changed.' : '',
      }),
      fullName: field('Full name', inputs.fullName),
      dateOfBirth: field('Date of birth', inputs.dateOfBirth, {
        hint: editing ? 'Date of birth cannot be changed.' : '',
      }),
      department: field('Department', inputs.department),
      semester: field('Semester', inputs.semester),
      contact: field('Contact', inputs.contact),
    };

    const formError = el('p', { class: 'form-error', role: 'alert', hidden: true });
    const submit = el('button', { type: 'submit', class: 'primary' }, editing ? 'Save changes' : 'Create student');
    const cancel = el('button', { type: 'button', class: 'ghost' }, 'Cancel');

    const formEl = el(
      'form',
      {
        class: 'student-form',
        onsubmit: (event) => {
          event.preventDefault();
          submitForm();
        },
      },
      el(
        'div',
        { class: 'form-grid' },
        wrappers.rollNo,
        wrappers.fullName,
        wrappers.dateOfBirth,
        wrappers.department,
        wrappers.semester,
        wrappers.contact,
      ),
      formError,
      el('footer', { class: 'dialog-actions' }, cancel, submit),
    );

    const handle = openDialog(editing ? 'Edit student' : 'Add student', formEl, {
      actions: [],
    });
    cancel.addEventListener('click', () => handle.close(null));

    function clearErrors() {
      for (const wrapper of Object.values(wrappers)) setFieldError(wrapper, '');
      formError.hidden = true;
      formError.textContent = '';
    }

    async function submitForm() {
      clearErrors();

      // Inline pre-check (SRS-NF-003): the server validates the same rules, but
      // a round trip per empty field is not what "inline validation" means.
      let firstInvalid = null;
      const require = (key, message) => {
        if (!inputs[key].value.trim()) {
          setFieldError(wrappers[key], message);
          firstInvalid = firstInvalid || inputs[key];
        }
      };
      require('fullName', 'Enter the student’s full name.');
      require('department', 'Enter a department.');
      require('contact', 'Enter a contact address.');
      if (!editing) {
        require('rollNo', 'Enter a roll number.');
        require('dateOfBirth', 'Enter a date of birth.');
        if (inputs.dateOfBirth.value && inputs.dateOfBirth.value >= todayIso()) {
          setFieldError(wrappers.dateOfBirth, 'Date of birth must be in the past.');
          firstInvalid = firstInvalid || inputs.dateOfBirth;
        }
      }
      if (firstInvalid) {
        firstInvalid.focus();
        return;
      }

      const payload = {
        fullName: inputs.fullName.value.trim(),
        department: inputs.department.value.trim(),
        semester: Number(inputs.semester.value),
        contact: inputs.contact.value.trim(),
      };
      if (!editing) {
        payload.rollNo = inputs.rollNo.value.trim();
        payload.dateOfBirth = inputs.dateOfBirth.value;
      }

      submit.disabled = true;
      submit.textContent = editing ? 'Saving…' : 'Creating…';
      try {
        if (editing) {
          await api.updateStudent(payload.rollNo || pick(values, 'rollNo', 'roll_no'), payload);
        } else {
          await api.createStudent(payload);
        }
        // Refresh before closing: a caller (or a test) that waits for the dialog
        // to disappear must not then observe the table still showing the old
        // page. Closing first made the refresh look like it never happened.
        state.offset = editing ? state.offset : 0;
        await runSearch();
        handle.close(true);
        status.set(editing ? 'Student updated.' : `Created ${payload.rollNo}.`, 'success');
      } catch (error) {
        submit.disabled = false;
        submit.textContent = editing ? 'Save changes' : 'Create student';
        if (error.status === 401 && onUnauthorized) {
          handle.close(null);
          onUnauthorized();
          return;
        }
        if (error.fields) {
          // The server reports every failed field at once (SR-003); show them
          // against the inputs rather than as one sentence.
          for (const [key, message] of Object.entries(error.fields)) {
            if (wrappers[key]) setFieldError(wrappers[key], message);
          }
          const firstKey = Object.keys(error.fields)[0];
          if (inputs[firstKey]) inputs[firstKey].focus();
        }
        formError.textContent = error.message || 'Could not save that student.';
        formError.hidden = false;
      }
    }

    (editing ? inputs.fullName : inputs.rollNo).focus();
  }

  return { root, reload: runSearch };
}

function todayIso() {
  const now = new Date();
  const month = String(now.getMonth() + 1).padStart(2, '0');
  const day = String(now.getDate()).padStart(2, '0');
  return `${now.getFullYear()}-${month}-${day}`;
}

// Role is re-exported so the shell can pass it without importing session twice.
export { Role };