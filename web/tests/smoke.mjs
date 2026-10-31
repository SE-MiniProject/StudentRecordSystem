// Headless Chromium smoke test over the DevTools Protocol, no npm deps.
// Boots ?mock=1, logs in, searches, opens dialogs, visits Accounts, logs out.
//
// Self-hosting: this script starts its own static server and its own Chromium
// (each on an ephemeral port) and tears both down again, so `npm run test:smoke`
// needs nothing already running. Set SMOKE_URL to point at an existing server
// and skip the spawn; set CHROME/CHROMIUM to choose the browser binary.
import { spawn } from 'node:child_process';
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { extname, join, normalize, sep } from 'node:path';
import { fileURLToPath } from 'node:url';

const WEB_ROOT = fileURLToPath(new URL('..', import.meta.url));
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const children = [];

// ---- static file server (only when SMOKE_URL is not supplied) --------------
const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.json': 'application/json; charset=utf-8',
};

async function startServer() {
  const server = createServer(async (req, res) => {
    const urlPath = decodeURIComponent(new URL(req.url, 'http://localhost').pathname);
    const rel = normalize(urlPath === '/' ? 'index.html' : urlPath.replace(/^\/+/, ''));
    // Refuse anything that escapes WEB_ROOT.
    if (rel.split(sep).includes('..')) {
      res.writeHead(403).end('forbidden');
      return;
    }
    try {
      const body = await readFile(join(WEB_ROOT, rel));
      res.writeHead(200, { 'content-type': MIME[extname(rel)] || 'application/octet-stream' });
      res.end(body);
    } catch {
      res.writeHead(404).end('not found');
    }
  });
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const { port } = server.address();
  children.push({ kill: () => server.close() });
  return `http://127.0.0.1:${port}/?mock=1`;
}

// ---- headless Chromium with an ephemeral debug port ------------------------
// Names the browser goes by depending on distro and install method. GitHub's
// ubuntu-latest ships `chromium` as a snap shim that cannot run in a container,
// so google-chrome (installed by the workflow) is tried before it.
const CANDIDATES = ['chromium', 'chromium-browser', 'google-chrome', 'google-chrome-stable'];

async function startChromium() {
  const binary = process.env.CHROME || process.env.CHROMIUM || CANDIDATES[0];
  const args = [
    '--headless=new',
    '--remote-debugging-port=0',
    '--user-data-dir=' + join(WEB_ROOT, '.smoke-profile'),
    '--no-sandbox',
    '--disable-gpu',
    '--disable-dev-shm-usage',
    '--no-first-run',
    '--no-default-browser-check',
    'about:blank',
  ];
  const proc = spawn(binary, args, { stdio: ['ignore', 'ignore', 'pipe'] });
  children.push(proc);

  // Chromium prints "DevTools listening on ws://127.0.0.1:PORT/devtools/browser/…"
  const endpoint = await new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error('Chromium did not report a DevTools endpoint')), 20000);
    let buffered = '';
    proc.stderr.on('data', (chunk) => {
      buffered += chunk.toString();
      const match = buffered.match(/DevTools listening on (ws:\/\/\S+)/);
      if (match) {
        clearTimeout(timer);
        resolve(match[1]);
      }
    });
    // ENOENT means the binary is not on PATH. Without this the promise waits
    // out the full 20s timeout and reports "no DevTools endpoint", which hides
    // the real cause behind a symptom.
    proc.on('error', (err) => {
      clearTimeout(timer);
      reject(new Error(
        `Could not start "${binary}" (${err.code}). Install it or set CHROME=/path/to/browser.`));
    });
    proc.on('exit', (code) => {
      clearTimeout(timer);
      reject(new Error(`Chromium exited early with code ${code}\n${buffered}`));
    });
  });
  const { hostname, port } = new URL(endpoint);
  return `http://${hostname}:${port}`;
}

const BASE = process.env.SMOKE_URL || (await startServer());
const DEBUG_HTTP = process.env.SMOKE_URL
  ? (process.env.DEBUG_HTTP || 'http://127.0.0.1:9222')
  : await startChromium();

const cleanup = () => {
  for (const child of children) {
    try { child.kill('SIGKILL'); } catch { /* already gone */ }
  }
};
process.on('exit', cleanup);
process.on('uncaughtException', (error) => { cleanup(); console.error(error); process.exit(1); });

const target = await (await fetch(`${DEBUG_HTTP}/json/new?about:blank`, { method: 'PUT' })).json();
const ws = new WebSocket(target.webSocketDebuggerUrl);
await new Promise((r, j) => { ws.onopen = r; ws.onerror = j; });

let id = 0;
const pending = new Map();
const problems = [];
ws.onmessage = (ev) => {
  const msg = JSON.parse(ev.data);
  if (msg.id && pending.has(msg.id)) { pending.get(msg.id)(msg); pending.delete(msg.id); }
  if (msg.method === 'Runtime.exceptionThrown') {
    problems.push('EXCEPTION: ' + (msg.params.exceptionDetails.exception?.description || msg.params.exceptionDetails.text));
  }
  if (msg.method === 'Runtime.consoleAPICalled' && msg.params.type === 'error') {
    problems.push('CONSOLE: ' + msg.params.args.map(a => a.value ?? a.description ?? '').join(' '));
  }
  if (msg.method === 'Log.entryAdded' && msg.params.entry.level === 'error') {
    problems.push('LOG: ' + msg.params.entry.text + ' ' + (msg.params.entry.url || ''));
  }
};
const send = (method, params = {}) => new Promise((res) => {
  const mid = ++id; pending.set(mid, res);
  ws.send(JSON.stringify({ id: mid, method, params }));
});
const evalJs = async (expression) => {
  const r = await send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true });
  if (r.result?.exceptionDetails) throw new Error('eval failed: ' + JSON.stringify(r.result.exceptionDetails));
  return r.result?.result?.value;
};
const waitFor = async (expr, label, timeout = 8000) => {
  const start = Date.now();
  while (Date.now() - start < timeout) { if (await evalJs(expr)) return true; await sleep(50); }
  throw new Error('timeout waiting for: ' + label);
};
const check = (ok, label) => { console.log((ok ? 'PASS  ' : 'FAIL  ') + label); if (!ok) problems.push('ASSERT: ' + label); };

await send('Runtime.enable'); await send('Log.enable'); await send('Page.enable');
await send('Page.navigate', { url: BASE });
await waitFor("document.querySelector('.login-card') !== null", 'login card');
check(await evalJs("document.querySelector('.demo-badge') !== null"), 'demo badge visible in mock mode');

// blank submit
await evalJs("document.querySelector('.login-form button[type=submit]').click()");
await sleep(200);
check(await evalJs("document.querySelector('.form-error').hidden === false"), 'blank submit shows an error');
check(await evalJs("document.querySelector('.form-error').textContent === 'Please enter your username and password.'"), 'blank submit uses the server wording');
check(await evalJs("document.activeElement.id === 'login-username'"), 'focus returns to username');

// wrong password
await evalJs(`(() => {
  document.getElementById('login-username').value = 'admin';
  document.getElementById('login-password').value = 'nope';
  document.querySelector('.login-form button[type=submit]').click();
})()`);
await sleep(900);
check(await evalJs("document.querySelector('.form-error').textContent === 'Invalid username or password.'"), 'bad password gives the generic error');
check(await evalJs("document.getElementById('login-password').value === ''"), 'password cleared after failure');

// success
await evalJs(`(() => {
  document.getElementById('login-username').value = 'admin';
  document.getElementById('login-password').value = 'admin123';
  document.querySelector('.login-form button[type=submit]').click();
})()`);
await waitFor("document.querySelector('.shell') !== null", 'shell');
check(true, 'admin login reaches the shell');
check(await evalJs("JSON.parse(localStorage.getItem('srs.session')||'null')?.role === 'admin'"), 'session persisted to localStorage');
check(await evalJs("[...document.querySelectorAll('.nav-item')].map(b=>b.textContent).join(',') === 'Students,Accounts'"), 'admin sees Students and Accounts');

// students list
await waitFor("document.querySelectorAll('.data-table tbody tr').length > 0", 'student rows');
const rows = await evalJs("document.querySelectorAll('.data-table tbody tr').length");
check(rows === 10, `page shows 10 rows (got ${rows})`);
check(await evalJs("/Showing 1-10 of 12/.test(document.querySelector('.page')?.textContent || '')"), 'status line reports the total');

// paging
await evalJs("[...document.querySelectorAll('.pager button')].find(b=>/Next/.test(b.textContent)).click()");
await waitFor("document.querySelectorAll('.data-table tbody tr').length === 2", 'second page');
check(await evalJs("/Page 2 of 2/.test(document.querySelector('.pager').textContent)"), 'pager reports page 2 of 2');

// search
await evalJs(`(() => {
  document.getElementById('student-search').value = 'Ananya';
  document.querySelector('.toolbar button[type=submit]').click();
})()`);
await waitFor("document.querySelectorAll('.data-table tbody tr').length === 1", 'one search hit');
check(await evalJs("/Ananya Rao/.test(document.querySelector('.data-table tbody').textContent)"), 'search found the right student');

// no match
await evalJs(`(() => {
  document.getElementById('student-search').value = 'zzz-nobody';
  document.querySelector('.toolbar button[type=submit]').click();
})()`);
await sleep(800);
check(await evalJs("document.querySelectorAll('.data-table tbody tr').length === 0"), 'no-match shows no rows');
check(await evalJs("/no (students|matches)|nothing/i.test(document.body.textContent)"), 'no-match state is explained');

// create dialog
await evalJs(`(() => {
  document.getElementById('student-search').value = '';
  document.querySelector('.toolbar button[type=submit]').click();
})()`);
await sleep(800);
await evalJs("[...document.querySelectorAll('button')].find(b=>/Add student/.test(b.textContent)).click()");
await waitFor("document.querySelector('dialog.modal') !== null", 'create dialog');
check(await evalJs("document.querySelector('dialog.modal').open === true"), 'Add student opens a modal');
check(await evalJs("document.querySelectorAll('dialog.modal .form-grid > *').length >= 6"), 'dialog has the student fields');
check(await evalJs("[...document.querySelectorAll('dialog.modal .form-grid input')].length >= 5"), 'dialog has real inputs');
await evalJs("document.querySelector('dialog.modal .ghost[aria-label=Close]').click()");
await sleep(200);
check(await evalJs("document.querySelector('dialog.modal') === null"), 'dialog closes');

// accounts
await evalJs("[...document.querySelectorAll('.nav-item')].find(b=>b.textContent === 'Accounts').click()");
await waitFor("document.querySelector('.page h1')?.textContent === 'Accounts'", 'accounts page');
await waitFor("document.querySelectorAll('.data-table tbody tr').length >= 3", 'account rows');
check(true, 'accounts page lists the seeded accounts');
check(await evalJs("!/password hash|\\$2[aby]\\$|token/i.test(document.querySelector('.content').innerHTML)"), 'accounts page leaks no secrets');

// ---------------- create a student (happy path) ----------------------------
// Back to Students: the accounts visit above left .content on another page.
await evalJs("[...document.querySelectorAll('.nav-item')].find(b=>b.textContent === 'Students').click()");
await waitFor("document.getElementById('student-search') !== null", 'students page again');
await evalJs(`(() => {
  document.getElementById('student-search').value = '';
  document.querySelector('.toolbar button[type=submit]').click();
})()`);
await sleep(800);

// The dialog must have exactly one footer, and named fields.
const fillStudent = (v) => `(() => {
  const d = document.querySelector('dialog.modal');
  const set = (n, val) => {
    const node = d.querySelector('[name="' + n + '"]');
    if (!node) throw new Error('no field named ' + n);
    node.value = val;
  };
  set('roll-number', ${JSON.stringify(v.roll)});
  set('full-name', ${JSON.stringify(v.name)});
  set('date-of-birth', ${JSON.stringify(v.dob)});
  set('department', ${JSON.stringify(v.dept)});
  set('semester', ${JSON.stringify(v.sem)});
  set('contact', ${JSON.stringify(v.contact)});
  d.querySelector('button[type=submit]').click();
})()`;

await evalJs("[...document.querySelectorAll('button')].find(b=>/Add student/.test(b.textContent)).click()");
await waitFor("document.querySelector('dialog.modal') !== null", 'create dialog again');
check(await evalJs("document.querySelectorAll('dialog.modal > footer').length === 0"), 'dialog adds no second, empty footer');
check(await evalJs("document.querySelectorAll('dialog.modal footer').length === 1"), 'dialog has exactly one footer (the form own)');
check(await evalJs("document.querySelectorAll('dialog.modal [name]').length >= 6"), 'dialog fields are named');
await evalJs(fillStudent({ roll: 'PES2UG23CS999', name: 'Zara Test', dob: '2005-01-15', dept: 'CSE', sem: '3', contact: 'zara@example.edu' }));
await waitFor("document.querySelector('dialog.modal') === null", 'dialog closes after create', 8000);
check(true, 'create student succeeds');
await evalJs(`(() => {
  document.getElementById('student-search').value = 'PES2UG23CS999';
  document.querySelector('.toolbar button[type=submit]').click();
})()`);
await waitFor("document.querySelectorAll('.data-table tbody tr').length === 1", 'created student appears', 12000);
check(await evalJs("/Zara Test/.test(document.querySelector('.data-table tbody').textContent)"), 'created student is searchable');

// ---------------- duplicate roll number is rejected -----------------------
await evalJs(`(() => {
  document.getElementById('student-search').value = '';
  document.querySelector('.toolbar button[type=submit]').click();
})()`);
await sleep(800);
await evalJs("[...document.querySelectorAll('button')].find(b=>/Add student/.test(b.textContent)).click()");
await waitFor("document.querySelector('dialog.modal') !== null", 'create dialog for duplicate');
await evalJs(fillStudent({ roll: 'PES2UG23CS999', name: 'Duplicate Person', dob: '2005-01-15', dept: 'CSE', sem: '3', contact: 'dup@example.edu' }));
await sleep(900);
check(await evalJs("document.querySelector('dialog.modal') !== null"), 'duplicate keeps the dialog open');
check(await evalJs("/already exists|already taken|duplicate/i.test(document.querySelector('dialog.modal').textContent)"), 'duplicate error is explained');
await evalJs("document.querySelector('dialog.modal .ghost[aria-label=Close]').click()");
await sleep(200);

// ---------------- edit an existing student --------------------------------
await evalJs(`(() => {
  document.getElementById('student-search').value = 'PES2UG23CS999';
  document.querySelector('.toolbar button[type=submit]').click();
})()`);
await waitFor("document.querySelectorAll('.data-table tbody tr').length === 1", 'row for edit');
await evalJs("document.querySelector('.row-actions button').click()");
await waitFor("document.querySelector('dialog.modal') !== null", 'edit dialog');
check(await evalJs("/Edit student/.test(document.querySelector('dialog.modal').textContent)"), 'edit dialog is titled Edit student');
check(await evalJs("document.querySelector('dialog.modal [name=roll-number]').disabled === true"), 'roll number is not editable');
check(await evalJs("document.querySelector('dialog.modal [name=date-of-birth]').disabled === true"), 'date of birth is not editable');
await evalJs(`(() => {
  const d = document.querySelector('dialog.modal');
  d.querySelector('[name=full-name]').value = 'Zara Renamed';
  d.querySelector('button[type=submit]').click();
})()`);
await waitFor("document.querySelector('dialog.modal') === null", 'edit closes', 8000);
await sleep(700);
check(await evalJs("/Zara Renamed/.test(document.querySelector('.data-table tbody').textContent)"), 'edit is reflected in the table');

// ---------------- delete with confirmation --------------------------------
await evalJs("document.querySelector('.row-actions .danger-text').click()");
await waitFor("document.querySelector('dialog.modal') !== null", 'confirm dialog');
check(await evalJs("/cannot be undone/i.test(document.querySelector('dialog.modal').textContent)"), 'delete warns that it is irreversible');
await evalJs("document.querySelector('dialog.modal .ghost[aria-label=Close]').click()");
await sleep(200);
check(await evalJs("document.querySelectorAll('.data-table tbody tr').length === 1"), 'cancelling delete keeps the row');
await evalJs("document.querySelector('.row-actions .danger-text').click()");
await waitFor("document.querySelector('dialog.modal') !== null", 'confirm dialog again');
const confirmLabels = await evalJs("[...document.querySelectorAll('dialog.modal .dialog-actions button')].map(b=>b.textContent).join('|')");
check(/Delete/.test(confirmLabels), 'confirm dialog offers Delete (' + confirmLabels + ')');
await evalJs("[...document.querySelectorAll('dialog.modal .dialog-actions button')].find(b=>b.textContent.trim()==='Delete').click()");
await waitFor("document.querySelectorAll('.data-table tbody tr').length === 0", 'row removed', 8000);
check(true, 'confirmed delete removes the row');

// ---------------- create an account ---------------------------------------
await evalJs("[...document.querySelectorAll('.nav-item')].find(b=>b.textContent === 'Accounts').click()");
await waitFor("document.querySelector('.page h1')?.textContent === 'Accounts'", 'accounts page again');
const before = await evalJs("document.querySelectorAll('.data-table tbody tr').length");
await evalJs("[...document.querySelectorAll('button')].find(b=>/add account/i.test(b.textContent)).click()");
await waitFor("document.querySelector('dialog.modal') !== null", 'account dialog');
await evalJs(`(() => {
  const d = document.querySelector('dialog.modal');
  const set = (n, v) => { const node = d.querySelector('[name="' + n + '"]'); if (!node) throw new Error('no ' + n); node.value = v; };
  set('username', 'newteacher');
  set('full-name', 'New Teacher');
  set('role', 'teacher');
  set('password', 'secret123');
  d.querySelector('button[type=submit]').click();
})()`);
await waitFor("document.querySelector('dialog.modal') === null", 'account dialog closes', 8000);
await sleep(500);
const after = await evalJs("document.querySelectorAll('.data-table tbody tr').length");
check(after === before + 1, `account count grew ${before} -> ${after}`);
check(await evalJs("/newteacher/.test(document.querySelector('.data-table tbody').textContent)"), 'new account appears in the list');

// logout
await evalJs("[...document.querySelectorAll('button')].find(b=>/log out/i.test(b.textContent)).click()");
await waitFor("document.querySelector('.login-card') !== null", 'back to login');
check(await evalJs("localStorage.getItem('srs.session') === null"), 'logout removes the stored session');

// teacher sees no Accounts link
await evalJs(`(() => {
  document.getElementById('login-username').value = 'teacher';
  document.getElementById('login-password').value = 'teacher123';
  document.querySelector('.login-form button[type=submit]').click();
})()`);
await waitFor("document.querySelector('.shell') !== null", 'teacher shell');
check(await evalJs("[...document.querySelectorAll('.nav-item')].map(b=>b.textContent).join(',') === 'Students'"), 'teacher sees Students only');
check(await evalJs("![...document.querySelectorAll('button')].some(b=>/Add student/.test(b.textContent))"), 'teacher cannot see Add student');

await send('Target.closeTarget', { targetId: target.id }).catch(() => {});
ws.close();
console.log('---');
// The static server and Chromium keep the event loop alive, so exit explicitly
// rather than falling off the end and hanging the runner.
if (problems.length) {
  console.log('PROBLEMS:\n' + problems.join('\n'));
  process.exit(1);
}
console.log('all checks passed');
process.exit(0);
