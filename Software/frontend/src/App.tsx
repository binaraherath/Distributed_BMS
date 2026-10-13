import { createBrowserRouter, Navigate, RouterProvider, useLocation } from 'react-router'
import { useMe } from './api/queries'
import { PackProvider } from './app/pack'
import { AppShell } from './layout/AppShell'
import { BrandMark } from './layout/BrandMark'
import { AlarmsPage } from './pages/AlarmsPage'
import { CellsPage } from './pages/CellsPage'
import { ControlPage } from './pages/ControlPage'
import { HistoryPage } from './pages/HistoryPage'
import { LoginPage } from './pages/LoginPage'
import { OverviewPage } from './pages/OverviewPage'
import { SettingsPage } from './pages/SettingsPage'

/** Everything except /login needs a session; the cookie is checked with /auth/me. */
function Protected() {
  const me = useMe()
  const location = useLocation()
  if (me.isLoading) {
    return (
      <div style={{ minHeight: '100dvh', display: 'grid', placeItems: 'center', opacity: 0.6 }}>
        <BrandMark size={36} />
      </div>
    )
  }
  if (!me.data) {
    return <Navigate to="/login" replace state={{ from: location.pathname }} />
  }
  return (
    <PackProvider>
      <AppShell me={me.data} />
    </PackProvider>
  )
}

const router = createBrowserRouter([
  { path: '/login', element: <LoginPage /> },
  {
    element: <Protected />,
    children: [
      { index: true, element: <OverviewPage />, handle: { title: 'Overview' } },
      { path: 'cells', element: <CellsPage />, handle: { title: 'Cells' } },
      { path: 'history', element: <HistoryPage />, handle: { title: 'History' } },
      { path: 'alarms', element: <AlarmsPage />, handle: { title: 'Alarms' } },
      { path: 'control', element: <ControlPage />, handle: { title: 'Control' } },
      { path: 'settings', element: <SettingsPage />, handle: { title: 'Settings' } },
      { path: '*', element: <Navigate to="/" replace /> },
    ],
  },
])

export function App() {
  return <RouterProvider router={router} />
}
