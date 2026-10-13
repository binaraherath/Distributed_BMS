import { useEffect, useState } from 'react'
import { useQueryClient } from '@tanstack/react-query'
import { keys, useLatest } from '../api/queries'
import type { LiveHeartbeat, LiveTelemetry } from '../api/types'

export type StreamState = 'connecting' | 'open' | 'reconnecting'

export interface Live {
  data: LiveTelemetry | null
  /** The gateway's status and last message time, refreshed by heartbeats even when no telemetry arrives. */
  gatewayOnline: boolean
  lastMessageAt: string | null
  stream: StreamState
  loading: boolean
}

/**
 * Live pack data over Server-Sent Events. The REST "latest" call paints the first
 * frame; the stream then pushes one snapshot per second plus a heartbeat every 3 s.
 * EventSource reconnects on its own after network loss.
 */
export function useLive(packId: number | undefined): Live {
  const qc = useQueryClient()
  const latest = useLatest(packId)
  const [stream, setStream] = useState<StreamState>('connecting')
  const [beat, setBeat] = useState<LiveHeartbeat | null>(null)

  useEffect(() => {
    if (packId === undefined) return
    const es = new EventSource(`/api/v1/packs/${packId}/stream`)
    es.onopen = () => setStream('open')
    es.onerror = () => setStream('reconnecting')
    es.addEventListener('telemetry', (e) => {
      const t = JSON.parse((e as MessageEvent<string>).data) as LiveTelemetry
      qc.setQueryData(keys.latest(packId), t)
      setBeat({ packId, gatewayOnline: t.gatewayOnline, lastMessageAt: t.lastMessageAt, serverTime: t.serverTime })
    })
    es.addEventListener('heartbeat', (e) => {
      setBeat(JSON.parse((e as MessageEvent<string>).data) as LiveHeartbeat)
    })
    return () => es.close()
  }, [packId, qc])

  const data = latest.data ?? null
  return {
    data,
    gatewayOnline: beat?.gatewayOnline ?? data?.gatewayOnline ?? false,
    lastMessageAt: beat?.lastMessageAt ?? data?.lastMessageAt ?? null,
    stream,
    loading: latest.isLoading,
  }
}
