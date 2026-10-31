// The post-login shell. Ports client/src/shell/MainWindow.cpp.
//
// The navigation is built from the same permission table (session.js ACTIONS),
// so a Teacher does not see "Accounts" and a Student does not see "Students".
// This is a usability gate only — the server rejects anything outside the role
// regardless, which is what makes it safe to get the gate wrong here.
//
// The Qt shell was a menu bar with six actions, three of which were disabled
// placeholders. A browser has no menu bar worth imitating, so the same table
// drives a sidebar instead, and actions with no page yet are not rendered at
// all rather than shown greyed out.

import { el, mount } from './dom.js';
import { ACTIONS, canUse } from './session.js';
import { createStudentsPage } from './pages/students.js';
import { createAccountsPage } from './pages/accounts.js';

/**
 * @param {object} options
 * @param {object} options.api       ApiClient or MockApiClient
 * @param {object} options.session   the signed-in session
 * @param {Function} options.onLogout    called after the server logout settles
 * @param {Function} options.onUnauthorized  called when a request 401s
 */
export function createShell({ api, session, onLogout, onUnauthorized }) {
  // Pages are built lazily so switching tabs does not re-fetch on every click,
  // and reloaded when revisited so another admin's change is visible.
  const pages = new Map();

  const routes = [];
  if (canUse(session.role, 'students.view')) {
    routes.push({
      id: 'students',
      label: 'Students',
      build: () => createStudentsPage({ api, session, onUnauthorized }),
    });
  }
  if (canUse(session.role, 'accounts.manage')) {
    routes.push({
      id: 'accounts',
      label: 'Accounts',
      build: () => createAccountsPage({ api, session, onUnauthorized }),
    });
  }

  const navList = el('nav', { class: 'sidebar-nav', 'aria-label': 'Main' });
  const content = el('main', { class: 'content', id: 'content', tabindex: '-1' });

  const navButtons = new Map(
    routes.map((route) => {
      const button = el(
        'button',
        {
          type: 'button',
          class: 'nav-item',
          'aria-current': 'false',
          onclick: () => navigate(route.id),
        },
        route.label,
      );
      navList.append(button);
      return [route.id, button];
    }),
  );

  if (routes.length === 0) {
    navList.append(el('p', { class: 'muted small' }, 'No pages are available for your role.'));
  }

  const logoutButton = el('button', { type: 'button', class: 'ghost block' }, 'Log out');

  const shell = el(
    'div',
    { class: 'shell' },
    el(
      'header',
      { class: 'topbar' },
      el(
        'div',
        { class: 'brand' },
        el('span', { class: 'brand-mark', 'aria-hidden': 'true' }, 'SRS'),
        el('span', { class: 'brand-name' }, 'Student Record System'),
      ),
      el(
        'div',
        { class: 'who' },
        el(
          'div',
          { class: 'who-text' },
          el('span', { class: 'who-name' }, session.fullName || session.username),
          el('span', { class: 'who-role' }, `${session.role} · ${session.username}`),
        ),
        logoutButton,
      ),
    ),
    el('div', { class: 'shell-body' }, el('aside', { class: 'sidebar' }, navList), content),
  );

  logoutButton.addEventListener('click', async () => {
    logoutButton.disabled = true;
    logoutButton.textContent = 'Logging out…';
    try {
      await api.logout();
    } catch {
      // A failed logout call must not trap the user in the app: the local
      // session is cleared regardless, and the server token expires on its own.
    }
    onLogout();
  });

  function navigate(id) {
    let page = pages.get(id);
    if (!page) {
      const route = routes.find((r) => r.id === id);
      if (!route) return;
      page = route.build();
      pages.set(id, page);
    }
    for (const [routeId, button] of navButtons) {
      button.setAttribute('aria-current', routeId === id ? 'page' : 'false');
      button.classList.toggle('active', routeId === id);
    }
    mount(content, page.root);
    content.focus();
    if (page.reload) page.reload();
  }

  // Hash routing keeps the back button and reloads honest without a router
  // library: #students survives an F5 and is linkable.
  function currentRoute() {
    const id = (globalThis.location.hash || '').replace(/^#\/?/, '');
    return routes.some((r) => r.id === id) ? id : routes[0]?.id;
  }

  globalThis.addEventListener('hashchange', () => {
    const id = currentRoute();
    if (id) navigate(id);
  });

  return {
    root: shell,
    start() {
      const id = currentRoute();
      if (id) navigate(id);
    },
    navigate,
  };
}

export { ACTIONS };