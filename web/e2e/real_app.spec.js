import { expect, test } from '@playwright/test';
import { randomBytes } from 'node:crypto';

const credentials = {
  admin: {
    username: process.env.SRS_E2E_ADMIN_USERNAME || 'admin',
    password: process.env.SRS_E2E_ADMIN_PASSWORD || 'admin',
  },
  teacher: {
    username: process.env.SRS_E2E_TEACHER_USERNAME || 'teacher',
    password: process.env.SRS_E2E_TEACHER_PASSWORD || 'teacher',
  },
  student: {
    username: process.env.SRS_E2E_STUDENT_USERNAME || 'student',
    password: process.env.SRS_E2E_STUDENT_PASSWORD || 'student',
  },
};

let healthRequest;

test.beforeAll(async ({ playwright }) => {
  healthRequest = await playwright.request.newContext({
    baseURL: process.env.SRS_E2E_BASE_URL || 'https://localhost:8080',
    ignoreHTTPSErrors: true,
  });
  const health = await healthRequest.get('/health');
  expect(health.ok(), 'the live app must be running at SRS_E2E_BASE_URL').toBeTruthy();
  expect(await health.json()).toMatchObject({ status: 'ok' });
});

test.afterAll(async () => {
  await healthRequest?.dispose();
});

test.afterEach(async ({ page }) => {
  // Close each real server session so repeated runs do not accumulate sessions.
  const openDialog = page.locator('dialog.modal[open]');
  if (await openDialog.isVisible().catch(() => false)) await page.keyboard.press('Escape');
  const logout = page.getByRole('button', { name: 'Log out' });
  if (await logout.isVisible().catch(() => false)) {
    await logout.click();
    await expect(page.locator('.login-card')).toBeVisible();
  }
});

async function signIn(page, role) {
  await page.goto('/');
  await expect(page.locator('.demo-badge')).toHaveCount(0);
  await expect(page.getByRole('heading', { name: 'Student Record System' })).toBeVisible();
  await page.getByLabel('Username').fill(credentials[role].username);
  await page.getByLabel('Password').fill(credentials[role].password);
  await page.getByRole('button', { name: 'Log in' }).click();
  await expect(page.locator('.shell')).toBeVisible();
  await expect(page.locator('.who-role')).toContainText(`${role} ·`);
}

async function saveScreenshot(page, testInfo, name) {
  const path = testInfo.outputPath(name);
  await page.screenshot({ path, fullPage: true });
  await testInfo.attach(name, { path, contentType: 'image/png' });
}

async function authenticatedGet(page, path) {
  return page.evaluate(async (requestPath) => {
    const session = JSON.parse(localStorage.getItem('srs.session') || 'null');
    const response = await fetch(requestPath, {
      headers: session?.token ? { Authorization: `Bearer ${session.token}` } : {},
    });
    const body = await response.json().catch(() => null);
    return { status: response.status, body };
  }, path);
}

async function anonymousGet(page, path) {
  return page.evaluate(async (requestPath) => {
    const response = await fetch(requestPath);
    return response.status;
  }, path);
}

async function adminDeleteStudent(token, rollNo) {
  const response = await healthRequest.delete(`/api/students/${encodeURIComponent(rollNo)}`, {
    headers: { Authorization: `Bearer ${token}` },
  });
  return response.status();
}

test('unknown account login fails generically and empty login shows validation', async ({ page }, testInfo) => {
  await page.goto('/');
  await expect(page.locator('.demo-badge')).toHaveCount(0);
  await page.getByRole('button', { name: 'Log in' }).click();
  await expect(page.locator('.form-error')).toContainText('Please enter your username and password.');
  await page.getByLabel('Username').fill('e2e-user-that-does-not-exist');
  await page.getByLabel('Password').fill('not-a-real-password');
  await page.getByRole('button', { name: 'Log in' }).click();
  await expect(page.locator('.form-error')).toHaveText('Invalid username or password.');
  await expect(page.locator('.shell')).toHaveCount(0);
  await saveScreenshot(page, testInfo, 'anonymous-login-errors.png');
});

test('logout invalidates the real server session token', async ({ page }, testInfo) => {
  await signIn(page, 'student');
  const oldToken = await page.evaluate(() => JSON.parse(localStorage.getItem('srs.session')).token);
  await page.getByRole('button', { name: 'Log out' }).click();
  await expect(page.locator('.login-card')).toBeVisible();
  const oldTokenStatus = await page.evaluate(async (token) => {
    const response = await fetch('/api/me', { headers: { Authorization: `Bearer ${token}` } });
    return response.status;
  }, oldToken);
  expect(oldTokenStatus).toBe(401);
  await saveScreenshot(page, testInfo, 'student-logged-out.png');
});

test('admin sees real seeded students and accounts', async ({ page }, testInfo) => {
  await signIn(page, 'admin');
  await expect(page.getByRole('button', { name: 'Students' })).toBeVisible();
  await expect(page.getByRole('button', { name: 'Accounts' })).toBeVisible();

  await expect(page.locator('.data-table tbody tr').first()).toBeVisible();
  await page.locator('#student-search').fill('1ABC26CS001');
  await page.getByRole('button', { name: 'Search' }).click();
  await expect(page.locator('.data-table tbody')).toContainText('Aarav Sharma');
  await saveScreenshot(page, testInfo, 'admin-real-student-search.png');

  await page.locator('#student-search').fill('Aarav');
  await page.getByRole('button', { name: 'Search' }).click();
  await expect(page.locator('.data-table tbody')).toContainText('Aarav Sharma');

  await page.locator('#student-search').fill('');
  await page.locator('#student-semester').selectOption('3');
  await page.getByRole('button', { name: 'Search' }).click();
  await expect(page.locator('.data-table tbody')).toContainText('Ananya Rao');
  const displayedSemesters = await page.locator('.data-table tbody tr').evaluateAll((rows) =>
    rows.map((row) => row.cells[3]?.textContent.trim()),
  );
  expect(displayedSemesters.length).toBeGreaterThan(0);
  expect(displayedSemesters.every((semester) => semester === '3')).toBe(true);
  await saveScreenshot(page, testInfo, 'admin-semester-filter.png');

  const semesterThreeTimetable = await authenticatedGet(page, '/api/timetable?semester=3');
  expect(semesterThreeTimetable.status).toBe(200);
  expect(semesterThreeTimetable.body.items.length).toBeGreaterThan(0);
  expect(semesterThreeTimetable.body.items.every((slot) => slot.semester === 3)).toBe(true);

  const accountResponse = await authenticatedGet(page, '/api/accounts');
  expect(accountResponse.status).toBe(200);
  expect(accountResponse.body.items.some((item) => item.username === 'admin')).toBe(true);
  expect(JSON.stringify(accountResponse.body)).not.toMatch(/password_hash|password_salt|token/i);

  await page.getByRole('button', { name: 'Grades', exact: true }).click();
  await page.locator('#grade-student-search').fill('1ABC26CS001');
  await page.getByRole('button', { name: 'Search' }).click();
  await expect(page.locator('#grades-table tbody')).toContainText('Aarav Sharma');
  const aaravGradeRow = page.locator('#grades-table tbody tr').filter({ hasText: 'Aarav Sharma' });
  await expect(aaravGradeRow).toHaveCount(1);
  await aaravGradeRow.getByRole('button', { name: 'View grades' }).click();
  await expect(page.locator('dialog.modal')).toContainText('CS501');
  await expect(page.locator('dialog.modal')).toContainText('8.5');
  await saveScreenshot(page, testInfo, 'admin-real-grades.png');
  await page.locator('dialog.modal .ghost[aria-label="Close"]').click();
  await expect(page.locator('dialog.modal')).toHaveCount(0);

  await page.getByRole('button', { name: 'Accounts' }).click();
  await expect(page.locator('.page h1')).toHaveText('Accounts');
  await expect(page.locator('.data-table tbody')).toContainText('teacher2');
  await saveScreenshot(page, testInfo, 'admin-real-accounts.png');
});

test('admin creates, searches, updates, and deletes an isolated temporary student', async ({ page }, testInfo) => {
  await signIn(page, 'admin');
  const rollNo = `E2E-${Date.now()}-${randomBytes(4).toString('hex')}`;
  const adminToken = await page.evaluate(() => JSON.parse(localStorage.getItem('srs.session')).token);
  const originalName = `E2E Temporary ${rollNo.slice(-8)}`;
  const updatedName = `E2E Updated ${rollNo.slice(-8)}`;

  try {
    const collisionCheck = await authenticatedGet(page, `/api/students?search=${encodeURIComponent(rollNo)}`);
    expect(collisionCheck.status).toBe(200);
    expect(collisionCheck.body.items.some((student) => student.rollNo === rollNo)).toBe(false);
    await page.getByRole('button', { name: 'Students' }).click();
    await page.getByRole('button', { name: /Add student/ }).click();
    const createDialog = page.locator('dialog.modal[open]');
    await expect(createDialog).toContainText('Add student');
    await createDialog.getByLabel('Roll number').fill(rollNo);
    await createDialog.getByLabel('Full name').fill(originalName);
    await createDialog.getByLabel('Date of birth').fill('2005-04-11');
    await createDialog.getByLabel('Department').fill('CSE-E2E');
    await createDialog.getByLabel('Semester').selectOption('3');
    await createDialog.getByLabel('Contact').fill(`${rollNo.toLowerCase()}@example.test`);
    await createDialog.getByRole('button', { name: 'Create student' }).click();
    await expect(createDialog).toHaveCount(0);
    await page.locator('#student-search').fill(rollNo);
    await page.getByRole('button', { name: 'Search' }).click();
    const createdRow = page.locator('.data-table tbody tr').filter({ hasText: rollNo });
    await expect(createdRow).toContainText(originalName);
    await saveScreenshot(page, testInfo, 'crud-temporary-student-created.png');

    const searchedRow = page.locator('.data-table tbody tr').filter({ hasText: rollNo });
    await expect(searchedRow).toContainText(originalName);

    await searchedRow.getByRole('button', { name: 'Edit' }).click();
    const editDialog = page.locator('dialog.modal[open]');
    await expect(editDialog).toContainText('Edit student');
    await editDialog.getByLabel('Full name').fill(updatedName);
    await editDialog.getByLabel('Department').fill('CSE-UPDATED');
    await editDialog.getByLabel('Semester').selectOption('4');
    await editDialog.getByLabel('Contact').fill(`${rollNo.toLowerCase()}-updated@example.test`);
    await editDialog.getByRole('button', { name: 'Save changes' }).click();
    await expect(editDialog).toHaveCount(0);
    const updatedRow = page.locator('.data-table tbody tr').filter({ hasText: rollNo });
    await expect(updatedRow).toContainText(updatedName);
    await expect(updatedRow).toContainText('CSE-UPDATED');
    await expect(updatedRow).toContainText('4');
    await saveScreenshot(page, testInfo, 'crud-temporary-student-updated.png');

    await updatedRow.getByRole('button', { name: 'Delete' }).click();
    const confirmDialog = page.locator('dialog.modal[open]');
    await expect(confirmDialog).toContainText('Delete student');
    await expect(confirmDialog).toContainText('cannot be undone');
    await saveScreenshot(page, testInfo, 'crud-temporary-student-delete-confirmation.png');
    await confirmDialog.getByRole('button', { name: 'Delete', exact: true }).click();
    await expect(page.locator('.data-table tbody tr').filter({ hasText: rollNo })).toHaveCount(0);
    await expect(page.locator('.status')).toContainText(`Deleted ${rollNo}.`);
    await saveScreenshot(page, testInfo, 'crud-temporary-student-deleted.png');
  } finally {
    // The UI journey is the primary check. This scoped admin API cleanup also
    // removes the generated row if a failure interrupts any UI step above.
    const cleanupStatus = await adminDeleteStudent(adminToken, rollNo).catch(() => 0);
    expect([204, 404]).toContain(cleanupStatus);
  }
});

test('teacher sees assigned records but no account administration', async ({ page }, testInfo) => {
  await signIn(page, 'teacher');
  await expect(page.getByRole('button', { name: 'Students' })).toBeVisible();
  await expect(page.getByRole('button', { name: 'Accounts' })).toHaveCount(0);

  await page.getByRole('button', { name: 'Grades', exact: true }).click();
  await expect(page.locator('.page h1')).toHaveText('Grades');
  await expect(page.locator('#grade-student-search')).toBeVisible();
  await page.locator('#grade-student-search').fill('1ABC26CS001');
  await page.getByRole('button', { name: 'Search' }).click();
  await expect(page.locator('#grades-table tbody')).toContainText('Aarav Sharma');
  const teacherGradeRow = page.locator('#grades-table tbody tr').filter({ hasText: 'Aarav Sharma' });
  await expect(teacherGradeRow).toHaveCount(1);
  await teacherGradeRow.getByRole('button', { name: 'View / edit grades' }).click();
  await expect(page.locator('dialog.modal')).toContainText('CS501');
  await expect(page.locator('dialog.modal')).toContainText('8.5');
  await saveScreenshot(page, testInfo, 'teacher-real-grades.png');
  await page.locator('dialog.modal .ghost[aria-label="Close"]').click();
  await expect(page.locator('dialog.modal')).toHaveCount(0);

  await page.getByRole('button', { name: 'Attendance', exact: true }).click();
  await expect(page.locator('.page h1')).toHaveText('Attendance');
  await expect(page.locator('#attendance-student-search')).toBeVisible();
  await page.locator('#attendance-student-search').fill('1ABC26CS001');
  await page.getByRole('button', { name: 'Search' }).click();
  await expect(page.locator('#attendance-table tbody')).toContainText('Aarav Sharma');
  const teacherAttendanceRow = page.locator('#attendance-table tbody tr').filter({ hasText: 'Aarav Sharma' });
  await expect(teacherAttendanceRow).toHaveCount(1);
  await teacherAttendanceRow.getByRole('button', { name: 'View / record attendance' }).click();
  await expect(page.locator('dialog.modal')).toContainText('CS501');
  await saveScreenshot(page, testInfo, 'teacher-real-attendance.png');
  await page.locator('dialog.modal .ghost[aria-label="Close"]').click();
  await expect(page.locator('dialog.modal')).toHaveCount(0);

  const accountStatus = await authenticatedGet(page, '/api/accounts');
  expect(accountStatus.status).toBe(403);
  const studentListStatus = await authenticatedGet(page, '/api/students');
  expect(studentListStatus.status).toBe(200);

  await page.getByRole('button', { name: 'My timetable' }).click();
  await expect(page.locator('.page h1')).toHaveText('My teaching schedule');
  await expect(page.locator('#timetable-grid .slot')).not.toHaveCount(0);
  await expect(page.locator('#timetable-grid')).toContainText('CS501');
  await saveScreenshot(page, testInfo, 'teacher-real-timetable.png');
});

test('student sees own seeded records and server denies another student', async ({ page }, testInfo) => {
  await signIn(page, 'student');
  await expect(page.getByRole('button', { name: 'Students' })).toHaveCount(0);
  await expect(page.getByRole('button', { name: 'Accounts' })).toHaveCount(0);

  await page.getByRole('button', { name: 'My grades' }).click();
  await expect(page.locator('.page h1')).toHaveText('My grades');
  await expect(page.locator('#grades-table tbody')).toContainText('CS501');
  await saveScreenshot(page, testInfo, 'student-real-grades.png');
  const semesterFiveGrades = await authenticatedGet(page, '/api/grades/1ABC26CS001?semester=5');
  expect(semesterFiveGrades.status).toBe(200);
  expect(semesterFiveGrades.body.items.length).toBeGreaterThan(0);
  expect(semesterFiveGrades.body.items.every((grade) => grade.semester === 5)).toBe(true);

  await page.getByRole('button', { name: 'My attendance' }).click();
  await expect(page.locator('.page h1')).toHaveText('My attendance');
  await expect(page.locator('#attendance-table tbody')).toContainText('CS501');
  await saveScreenshot(page, testInfo, 'student-real-attendance.png');
  const semesterFiveAttendance = await authenticatedGet(page, '/api/attendance/1ABC26CS001?semester=5');
  expect(semesterFiveAttendance.status).toBe(200);
  expect(semesterFiveAttendance.body.items.length).toBeGreaterThan(0);
  expect(semesterFiveAttendance.body.items.every((item) => item.semester === 5)).toBe(true);

  const denied = await page.evaluate(async () => {
    const token = JSON.parse(localStorage.getItem('srs.session')).token;
    const response = await fetch('/api/students/1ABC26CS002', {
      headers: { Authorization: `Bearer ${token}` },
    });
    return response.status;
  });
  expect(denied).toBe(403);

  for (const path of [
    '/api/students/1ABC26CS003',
    '/api/grades/1ABC26CS003',
    '/api/attendance/1ABC26CS003',
    '/api/timetable/1ABC26CS003',
    '/api/accounts',
    '/api/students',
  ]) {
    expect((await authenticatedGet(page, path)).status, `student access to ${path}`).toBe(403);
  }

  await page.getByRole('button', { name: 'My timetable' }).click();
  await expect(page.locator('.page h1')).toHaveText('My class timetable');
  await expect(page.locator('#timetable-grid .slot')).not.toHaveCount(0);
  await expect(page.locator('#timetable-grid')).toContainText('CS501');
  await saveScreenshot(page, testInfo, 'student-real-timetable.png');
});

test('unauthenticated reads are rejected by the real API', async ({ page }, testInfo) => {
  await page.goto('/');
  for (const path of ['/api/me', '/api/students', '/api/accounts']) {
    expect(await anonymousGet(page, path), `anonymous access to ${path}`).toBe(401);
  }
  await saveScreenshot(page, testInfo, 'unauthenticated-login.png');
});
