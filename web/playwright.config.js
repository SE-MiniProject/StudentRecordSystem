import { defineConfig } from '@playwright/test';

const baseURL = process.env.SRS_E2E_BASE_URL || 'https://localhost:8080';
const chromiumPath = process.env.SRS_E2E_CHROMIUM;

export default defineConfig({
  testDir: './e2e',
  fullyParallel: true,
  reporter: [['list'], ['html', { outputFolder: '../docs/reports/playwright-report', open: 'never' }]],
  outputDir: '../docs/reports/playwright-artifacts',
  use: {
    baseURL,
    ignoreHTTPSErrors: true,
    screenshot: 'on',
    trace: 'retain-on-failure',
    ...(chromiumPath ? { launchOptions: { executablePath: chromiumPath } } : {}),
  },
});
