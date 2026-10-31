// Login screen. Ports client/src/login/LoginDialog.cpp.
//
// Same three behaviours the Qt dialog had, each of which has a requirement
// behind it:
//
//   - a blank username or password is caught here, with the same sentence the
//     server's 400 carries, so the two paths cannot drift apart;
//   - the form disables while a request is in flight, so a slow server shows as
//     busy rather than as a dead click;
//   - a failed login clears the password and returns focus to it.

import { el, field, mount, setFieldError } from '../dom.js';
import { MISSING_CREDENTIALS } from '../api.js';

export function createLoginPage({ onLogin, mock = false }) {
  const username = el('input', {
    type: 'text',
    id: 'login-username',
    autocomplete: 'username',
    autocapitalize: 'none',
    spellcheck: false,
    required: true,
  });
  const password = el('input', {
    type: 'password',
    id: 'login-password',
    autocomplete: 'current-password',
    required: true,
  });

  const usernameField = field('Username', username);
  const passwordField = field('Password', password);

  const errorBox = el('p', { class: 'form-error', role: 'alert', hidden: true });
  const submit = el('button', { type: 'submit', class: 'primary block' }, 'Log in');

  const form = el(
    'form',
    {
      class: 'login-form',
      novalidate: true,
      onsubmit: (event) => {
        event.preventDefault();
        attemptLogin();
      },
    },
    usernameField,
    passwordField,
    errorBox,
    submit,
  );

  const root = el(
    'main',
    { class: 'login-screen' },
    el(
      'div',
      { class: 'login-card' },
      el(
        'header',
        { class: 'login-head' },
        el('h1', {}, 'Student Record System'),
        el('p', { class: 'muted' }, 'Sign in to continue'),
        mock ? el('p', { class: 'demo-badge' }, 'Demo mode — no server required') : null,
      ),
      form,
      mock
        ? el(
            'p',
            { class: 'demo-hint' },
            'Try ',
            el('code', {}, 'admin / admin123'),
            ', ',
            el('code', {}, 'teacher / teacher123'),
            ' or ',
            el('code', {}, 'student / student123'),
            '.',
          )
        : null,
    ),
  );

  function setBusy(busy) {
    submit.disabled = busy;
    submit.textContent = busy ? 'Signing in…' : 'Log in';
    username.disabled = busy;
    password.disabled = busy;
  }

  function showError(message) {
    setBusy(false);
    errorBox.textContent = message;
    errorBox.hidden = false;
    setFieldError(passwordField, message);
    password.value = '';
    password.focus();
  }

  async function attemptLogin() {
    const user = username.value.trim();
    const pass = password.value;

    errorBox.hidden = true;
    setFieldError(usernameField, '');
    setFieldError(passwordField, '');

    if (!user || !pass) {
      // The server would answer 400; catch it here first, with its wording.
      setFieldError(!user ? usernameField : passwordField, MISSING_CREDENTIALS);
      errorBox.textContent = MISSING_CREDENTIALS;
      errorBox.hidden = false;
      (!user ? username : password).focus();
      return;
    }

    setBusy(true);
    try {
      const { session } = await onLogin(user, pass);
      setBusy(false);
      return session;
    } catch (error) {
      showError(error.message || 'Could not sign in.');
      return null;
    }
  }

  return {
    root,
    focus: () => username.focus(),
    prefill(usernameValue = '', passwordValue = '') {
      username.value = usernameValue;
      password.value = passwordValue;
    },
    submit: attemptLogin,
  };
}