// Accounts page — Admin only (SRS-17, SRS-19).
//
// Lists accounts (never password hashes or tokens: the server does not send
// them), creates new ones, and lets an admin change a role, reset a password or
// deactivate an account. Deactivating revokes the account's sessions server-side
// (F-006), which is why it is a confirmation and not a toggle that fires on
// change.

import { el, field, mount, setFieldError, statusBar, confirmDialog, openDialog } from '../dom.js';

const ROLES = ['admin', 'teacher', 'student'];

export function createAccountsPage({ api, session, onUnauthorized }) {
  const state = { items: [], loading: false };
  const status = statusBar();
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
        el('th', { scope: 'col' }, 'Username'),
        el('th', { scope: 'col' }, 'Full name'),
        el('th', { scope: 'col' }, 'Role'),
        el('th', { scope: 'col' }, 'Status'),
        el('th', { scope: 'col' }, 'Actions'),
      ),
    ),
    tableBody,
  );

  const addButton = el('button', { type: 'button', class: 'primary' }, '+ Add account');

  const root = el(
    'section',
    { class: 'page' },
    el(
      'header',
      { class: 'page-head' },
      el('h1', {}, 'Accounts'),
      addButton,
    ),
    status.node,
    el('div', { class: 'table-wrap' }, table),
  );

  addButton.addEventListener('click', () => openAccountForm());

  async function load() {
    state.loading = true;
    status.set('Loading…', 'info');
    try {
      const page = await api.listAccounts();
      state.items = page.items;
      render();
    } catch (error) {
      if (error.status === 401 && onUnauthorized) {
        onUnauthorized();
        return;
      }
      mount(tableBody);
      status.set(error.message || 'Could not load accounts.', 'error');
    } finally {
      state.loading = false;
    }
  }

  function render() {
    mount(tableBody, ...state.items.map(renderRow));
    status.set(`${state.items.length} account${state.items.length === 1 ? '' : 's'}.`, 'info');
  }

  function renderRow(account) {
    // The signed-in admin cannot deactivate themselves out of the system: the
    // server would allow it, but the client should not offer the footgun.
    const isSelf = account.username === session.username;
    const actions = [
      el(
        'button',
        { type: 'button', class: 'link', onclick: () => openAccountForm(account) },
        'Edit',
      ),
    ];
    if (!isSelf) {
      actions.push(
        el(
          'button',
          {
            type: 'button',
            class: `link ${account.active ? 'danger-text' : ''}`,
            onclick: () => toggleActive(account),
          },
          account.active ? 'Deactivate' : 'Reactivate',
        ),
      );
    }

    return el(
      'tr',
      { class: account.active ? '' : 'inactive-row' },
      el('td', {}, account.username, isSelf ? el('span', { class: 'tag' }, 'you') : null),
      el('td', {}, account.fullName),
      el('td', {}, account.role),
      el(
        'td',
        {},
        el(
          'span',
          { class: `pill ${account.active ? 'pill-on' : 'pill-off'}` },
          account.active ? 'Active' : 'Inactive',
        ),
      ),
      el('td', { class: 'row-actions' }, ...actions),
    );
  }

  async function toggleActive(account) {
    const turningOff = account.active;
    const confirmed = await confirmDialog({
      title: turningOff ? 'Deactivate account' : 'Reactivate account',
      message: turningOff
        ? `Deactivate ${account.username}? Their sessions are revoked immediately and they cannot log in until reactivated.`
        : `Reactivate ${account.username}? They will be able to log in again.`,
      confirmLabel: turningOff ? 'Deactivate' : 'Reactivate',
      danger: turningOff,
    });
    if (!confirmed) return;
    try {
      await api.updateAccount(account.username, { active: !account.active });
      await load();
      status.set(`${account.username} ${turningOff ? 'deactivated' : 'reactivated'}.`, 'success');
    } catch (error) {
      if (error.status === 401 && onUnauthorized) {
        onUnauthorized();
        return;
      }
      status.set(error.message || 'Could not update that account.', 'error');
    }
  }

  /** Create (no account) or edit (account given). */
  function openAccountForm(account = null) {
    const editing = Boolean(account);
    const values = account || {};

    const inputs = {
      username: el('input', {
        type: 'text',
        value: values.username || '',
        required: !editing,
        disabled: editing,
        autocomplete: 'off',
        placeholder: 'a.shetty',
      }),
      fullName: el('input', {
        type: 'text',
        value: values.fullName || '',
        required: true,
        autocomplete: 'name',
      }),
      role: el(
        'select',
        {},
        ...ROLES.map((role) =>
          el(
            'option',
            { value: role, selected: (values.role || 'student') === role },
            role.charAt(0).toUpperCase() + role.slice(1),
          ),
        ),
      ),
      password: el('input', {
        type: 'password',
        // On edit the field is a reset, not a required value.
        required: !editing,
        autocomplete: 'new-password',
        placeholder: editing ? 'Leave blank to keep the current password' : '',
      }),
    };

    const wrappers = {
      username: field('Username', inputs.username, {
        hint: editing ? 'Usernames cannot be changed.' : '',
      }),
      fullName: field('Full name', inputs.fullName),
      role: field('Role', inputs.role),
      password: field(editing ? 'New password' : 'Password', inputs.password, {
        hint: 'At least 8 characters.',
      }),
    };

    const formError = el('p', { class: 'form-error', role: 'alert', hidden: true });
    const submit = el(
      'button',
      { type: 'submit', class: 'primary' },
      editing ? 'Save changes' : 'Create account',
    );
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
        wrappers.username,
        wrappers.fullName,
        wrappers.role,
        wrappers.password,
      ),
      formError,
      el('footer', { class: 'dialog-actions' }, cancel, submit),
    );

    const handle = openDialog(editing ? `Edit ${values.username}` : 'Add account', formEl, {
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

      let firstInvalid = null;
      const require = (key, message) => {
        if (!inputs[key].value.trim()) {
          setFieldError(wrappers[key], message);
          firstInvalid = firstInvalid || inputs[key];
        }
      };
      if (!editing) require('username', 'Enter a username.');
      require('fullName', 'Enter a full name.');
      if (!editing) require('password', 'Enter a password.');
      if (inputs.password.value && inputs.password.value.length < 8) {
        setFieldError(wrappers.password, 'Use at least 8 characters.');
        firstInvalid = firstInvalid || inputs.password;
      }
      if (firstInvalid) {
        firstInvalid.focus();
        return;
      }

      submit.disabled = true;
      submit.textContent = editing ? 'Saving…' : 'Creating…';
      try {
        if (editing) {
          const changes = {
            fullName: inputs.fullName.value.trim(),
            role: inputs.role.value,
          };
          if (inputs.password.value) changes.password = inputs.password.value;
          await api.updateAccount(values.username, changes);
        } else {
          await api.createAccount({
            username: inputs.username.value.trim(),
            password: inputs.password.value,
            fullName: inputs.fullName.value.trim(),
            role: inputs.role.value,
          });
        }
        // Refresh before closing: the caller only sees the dialog go away, and
        // closing first would let it read a stale list.
        await load();
        handle.close(true);
        status.set(
          editing ? `Updated ${values.username}.` : `Created ${inputs.username.value.trim()}.`,
          'success',
        );
      } catch (error) {
        submit.disabled = false;
        submit.textContent = editing ? 'Save changes' : 'Create account';
        if (error.status === 401 && onUnauthorized) {
          handle.close(null);
          onUnauthorized();
          return;
        }
        if (error.fields) {
          for (const [key, message] of Object.entries(error.fields)) {
            if (wrappers[key]) setFieldError(wrappers[key], message);
          }
          const firstKey = Object.keys(error.fields)[0];
          if (inputs[firstKey]) inputs[firstKey].focus();
        }
        formError.textContent = error.message || 'Could not save that account.';
        formError.hidden = false;
      }
    }

    (editing ? inputs.fullName : inputs.username).focus();
  }

  return { root, reload: load };
}