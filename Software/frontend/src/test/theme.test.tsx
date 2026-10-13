import { describe, expect, it } from 'vitest'
import { render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { ThemeProvider, useTheme } from '../app/theme'

function Picker() {
  const { choice, setChoice } = useTheme()
  return (
    <>
      <span data-testid="choice">{choice}</span>
      <button onClick={() => setChoice('dark')}>dark</button>
      <button onClick={() => setChoice('system')}>system</button>
    </>
  )
}

describe('theme', () => {
  it('remembers an explicit choice and applies it to <html>', async () => {
    const { unmount } = render(<ThemeProvider><Picker /></ThemeProvider>)
    await userEvent.click(screen.getByText('dark'))

    expect(document.documentElement).toHaveAttribute('data-theme', 'dark')
    expect(localStorage.getItem('dbms.theme')).toBe('dark')

    unmount()
    render(<ThemeProvider><Picker /></ThemeProvider>)
    expect(screen.getByTestId('choice')).toHaveTextContent('dark')
  })

  it('goes back to following the system and forgets the choice', async () => {
    localStorage.setItem('dbms.theme', 'light')
    render(<ThemeProvider><Picker /></ThemeProvider>)
    await userEvent.click(screen.getByText('system'))

    expect(document.documentElement).not.toHaveAttribute('data-theme')
    expect(localStorage.getItem('dbms.theme')).toBeNull()
  })
})
