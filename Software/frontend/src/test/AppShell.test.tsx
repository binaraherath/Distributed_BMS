import { describe, expect, it, vi } from 'vitest'
import { render, screen } from '@testing-library/react'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import { createMemoryRouter, RouterProvider } from 'react-router'
import type { Me } from '../api/types'
import { ThemeProvider } from '../app/theme'
import { AppShell } from '../layout/AppShell'
import { liveTelemetry } from './fixtures'

// The shell's data comes from these hooks; the test is about what it renders.
vi.mock('../app/pack', () => ({
  usePack: () => ({
    packs: [], select: () => {}, loading: false,
    pack: { id: 1, deviceId: 'dbms-01', name: 'Leaf pack', location: null, chemistry: 'Li-ion', seriesCells: 16,
      ratedCapacityAh: 66.2, gatewayOnline: true, lastSeenAt: null, createdAt: '2026-10-01T00:00:00Z', modules: [] },
  }),
}))
vi.mock('../app/live', () => ({
  useLive: () => ({ data: liveTelemetry('SIMULATED'), gatewayOnline: true, lastMessageAt: new Date().toISOString(),
    stream: 'open', loading: false }),
}))
vi.mock('../api/queries', () => ({
  useAlarms: () => ({ data: { content: [], page: 0, size: 1, totalElements: 2, totalPages: 2 } }),
  useLogout: () => ({ mutate: () => {} }),
}))

function renderShell(me: Me) {
  const router = createMemoryRouter(
    [{ element: <AppShell me={me} />, children: [{ index: true, element: <p>page</p>, handle: { title: 'Overview' } }] }],
    { initialEntries: ['/'] },
  )
  render(
    <QueryClientProvider client={new QueryClient()}>
      <ThemeProvider><RouterProvider router={router} /></ThemeProvider>
    </QueryClientProvider>,
  )
}

describe('AppShell', () => {
  it('hides Control from viewers', () => {
    renderShell({ username: 'viewer', displayName: 'Viewer', role: 'VIEWER' })
    expect(screen.queryByRole('link', { name: /control/i })).not.toBeInTheDocument()
    expect(screen.getAllByRole('link', { name: /history/i }).length).toBeGreaterThan(0)
  })

  it('shows Control to administrators', () => {
    renderShell({ username: 'admin', displayName: 'Administrator', role: 'ADMIN' })
    expect(screen.getByRole('link', { name: /control/i })).toBeInTheDocument()
  })

  it('says so in the header when part of the data is simulated', () => {
    renderShell({ username: 'viewer', displayName: 'Viewer', role: 'VIEWER' })
    expect(screen.getByText('Partly simulated')).toBeInTheDocument()
    expect(screen.getAllByLabelText('2 active').length).toBeGreaterThan(0)
  })
})
