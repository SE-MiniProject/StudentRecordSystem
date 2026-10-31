// Entry point. Ports client/src/main.cpp.
//
// The Qt main() did four things: build a client, honour SRS_MOCK / SRS_API_BASE,
// run the login dialog modally, and only then build the main window. The shape
// here is the same — login first, shell only on success — because that is what
// guarantees no screen is ever built without a session.
//
// Configuration comes from the query string rather than environment variables,
// since a browser has none:
//
//   ?mock=1        demo mode: answers locally, no server or database needed
//   ?api=<base>    talk to a server on another origin (needs CORS; the normal
//                  deployment serves these files from the same origin, so this
//                  is for development against a separately-run server)

import { ApiClient, MockApiClient, mockRequested } from './api.js';
import {
  clearSession,
  loadSession,
  saveSession,
} from './session.js';
import { createLoginPage } from './pages/login.js';
import { createShell } from './shell.js';
import { mount } from './dom.js';

const root = document.getElementById('app');

function apiBaseFromLocation() {
  const params = new URLSearchParams(location.search);
  const base = params.get('api');
  return base ? base.replace(/\/$/, '') : '';
}

function start() {
  const mock = mockRequested();
  let session = mock ? null : loadSession();

  // onUnauthorized: the server revoked the token (expiry, deactivation, or a
  // logout elsewhere). Drop the stored copy and return to the login screen;
  // without this the app would sit on a screen that can never load.
  const handleUnauthorized = () => {
    if (!session) return;
    session = null;
    clearSession();
    showLogin('Your session has expired. Please log in again.');
  };

  const api = mock
    ? new MockApiClient()
    : new ApiClient({
        baseUrl: apiBaseFromLocation(),
        token: () => (session ? session.token : ''),
        onUnauthorized: handleUnauthorized,
      });

  function showLogin(notice = '') {
    const login = createLoginPage({
      mock,
      onLogin: async (username, password) => {
        const result = await api.login(username, password);
        session = result.session;
        // The team chose localStorage (over sessionStorage); logout is therefore
        // the only thing that ends a session on the client, so it must remove
        // this key — see session.js.
        saveSession(session);
        showShell();
        return result;
      },
    });
    mount(root, login.root);
    if (notice) {
      const box = login.root.querySelector('.form-error');
      if (box) {
        box.textContent = notice;
        box.hidden = false;
      }
    }
    login.focus();
  }

  function showShell() {
    const shell = createShell({
      api,
      session,
      onLogout: () => {
        session = null;
        clearSession();
        location.hash = '';
        showLogin();
      },
      onUnauthorized: handleUnauthorized,
    });
    mount(root, shell.root);
    shell.start();
  }

  if (session) showShell();
  else showLogin();
}

start();