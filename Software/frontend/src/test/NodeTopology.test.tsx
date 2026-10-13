import { describe, expect, it } from 'vitest'
import { render, screen, within } from '@testing-library/react'
import { NodeTopology } from '../components/NodeTopology'
import { liveTelemetry } from './fixtures'

const card = (name: string) => screen.getByText(name).closest('li')!

describe('NodeTopology', () => {
  it('labels a node that the gateway is filling in as Simulated', () => {
    render(<NodeTopology live={liveTelemetry('SIMULATED', 'LIVE')} gatewayOnline />)

    expect(within(card('Master')).getByText('Simulated')).toBeInTheDocument()
    expect(within(card('Controller')).getByText('Live')).toBeInTheDocument()
    expect(card('Master')).toHaveAttribute('data-source', 'SIMULATED')
  })

  it('shows every pack node as Offline when the gateway is offline', () => {
    render(<NodeTopology live={liveTelemetry()} gatewayOnline={false} />)

    for (const name of ['Master', 'Controller', 'Gateway']) {
      expect(within(card(name)).getByText('Offline')).toBeInTheDocument()
    }
  })
})
