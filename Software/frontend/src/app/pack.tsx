import { createContext, useCallback, useContext, useMemo, useState, type ReactNode } from 'react'
import { usePacks } from '../api/queries'
import type { Pack } from '../api/types'

interface PackState {
  packs: Pack[]
  pack: Pack | undefined
  select: (id: number) => void
  loading: boolean
}

const KEY = 'dbms.pack'
const PackContext = createContext<PackState | null>(null)

function stored(): number | null {
  try {
    const v = Number(localStorage.getItem(KEY))
    return Number.isFinite(v) && v > 0 ? v : null
  } catch {
    return null
  }
}

/**
 * Packs register themselves when their gateway first reports. The dashboard shows one
 * at a time: the last one picked, else the first that is online, else the first.
 */
export function PackProvider({ children }: { children: ReactNode }) {
  const { data, isLoading } = usePacks()
  const [chosen, setChosen] = useState<number | null>(stored)

  const packs = useMemo(() => data ?? [], [data])
  const pack = packs.find((p) => p.id === chosen) ?? packs.find((p) => p.gatewayOnline) ?? packs[0]

  const select = useCallback((id: number) => {
    setChosen(id)
    try {
      localStorage.setItem(KEY, String(id))
    } catch {
      // not remembered, still selected
    }
  }, [])

  const value = useMemo(() => ({ packs, pack, select, loading: isLoading }), [packs, pack, select, isLoading])
  return <PackContext.Provider value={value}>{children}</PackContext.Provider>
}

export function usePack(): PackState {
  const ctx = useContext(PackContext)
  if (!ctx) throw new Error('usePack must be used inside PackProvider')
  return ctx
}
