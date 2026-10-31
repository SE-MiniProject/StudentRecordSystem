// The account a signed-in user is acting as. Parsed once at the login boundary
// so screens compare a Role rather than re-testing raw strings.
//
// A port of client/src/session/Session.{h,cpp}. The rule that matters is the
// same: an unrecognised role is treated as the LEAST privileged, because the
// server is the authority on access and a client-side parse must never be able
// to grant more than the server would.

export const Role = Object.freeze({
  Admin: 'admin',
  Teacher: 'teacher',
  Student: 'student',
});

/** Returns Student for anything unrecognised — see the note above. */
export function roleFromString(value) {
  if (value === 'admin') return Role.Admin;
  if (value === 'teacher') return Role.Teacher;
  return Role.Student;
}

/** Admin >= Teacher >= Student. Not enum ordering, so reordering cannot break it. */
export function rank(role) {
  switch (role) {
    case Role.Admin:
      return 3;
    case Role.Teacher:
      return 2;
    case Role.Student:
      return 1;
    default:
      return 0;
  }
}

export function allowed(role, minimum) {
  return rank(role) >= rank(minimum);
}

/** The post-login shell's permission table, in one place (SRS-15). */
export const ACTIONS = Object.freeze([
  { name: 'students.view', label: 'View / Search', minimum: Role.Student },
  { name: 'students.create', label: 'Add Student', minimum: Role.Admin },
  { name: 'students.update', label: 'Edit Student', minimum: Role.Admin },
  { name: 'students.delete', label: 'Delete Student', minimum: Role.Admin },
  { name: 'accounts.manage', label: 'Manage Accounts', minimum: Role.Admin },
]);

/**
 * True when the signed-in role may use the named action.
 * Not in the table means denied, so a forgotten entry fails closed rather than
 * quietly granting access — same rule as MainWindow::canUse.
 */
export function canUse(role, actionName) {
  const rule = ACTIONS.find((a) => a.name === actionName);
  return rule ? allowed(role, rule.minimum) : false;
}

export function makeSession({ username, fullName, role, token }) {
  return {
    username: username || '',
    fullName: fullName || '',
    role: roleFromString(role),
    token: token || '',
  };
}

export function isValidSession(session) {
  return Boolean(session && session.token && session.username);
}

// ---------------------------------------------------------------------------
// Persistence.
//
// The Qt client kept the token in process memory, so closing the app ended the
// session. A browser has no equivalent, and the team chose localStorage: the
// token now survives a tab close and is readable by any script on the origin.
// Two consequences are handled deliberately rather than left as TODOs:
//
//   1. logout() must remove the stored session — it is the only thing ending a
//      session on the client (SRS-F-004);
//   2. a 401 from any endpoint clears it, because the server may have revoked
//      the token (deactivated account, expiry, or an explicit logout elsewhere).
// ---------------------------------------------------------------------------

const STORAGE_KEY = 'srs.session';

/** Reads the stored session, or null. Never throws on corrupt storage. */
export function loadSession(storage = globalThis.localStorage) {
  if (!storage) return null;
  let raw;
  try {
    raw = storage.getItem(STORAGE_KEY);
  } catch {
    return null; // private mode / storage disabled
  }
  if (!raw) return null;
  try {
    const parsed = JSON.parse(raw);
    const session = makeSession(parsed || {});
    return isValidSession(session) ? session : null;
  } catch {
    // A corrupt value is not a session; drop it rather than fail every reload.
    try {
      storage.removeItem(STORAGE_KEY);
    } catch {
      /* ignore */
    }
    return null;
  }
}

export function saveSession(session, storage = globalThis.localStorage) {
  if (!storage) return;
  try {
    storage.setItem(STORAGE_KEY, JSON.stringify(session));
  } catch {
    /* quota or disabled storage: the in-memory session still works */
  }
}

export function clearSession(storage = globalThis.localStorage) {
  if (!storage) return;
  try {
    storage.removeItem(STORAGE_KEY);
  } catch {
    /* ignore */
  }
}