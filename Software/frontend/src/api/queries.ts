import { keepPreviousData, useMutation, useQuery, useQueryClient } from '@tanstack/react-query'
import { api } from './client'
import type {
  Alarm, CellHistory, Command, CommandType, History, LiveTelemetry, Me, Page, Pack, PackUpdate, Role, User,
} from './types'

export const keys = {
  me: ['me'] as const,
  packs: ['packs'] as const,
  latest: (packId: number) => ['packs', packId, 'latest'] as const,
  history: (packId: number, from: string, to: string) => ['packs', packId, 'history', from, to] as const,
  cellHistory: (packId: number, cellNo: number, from: string) => ['packs', packId, 'cell', cellNo, from] as const,
  alarms: (packId: number) => ['packs', packId, 'alarms'] as const,
  commands: (packId: number) => ['packs', packId, 'commands'] as const,
  users: ['users'] as const,
}

// ---------------------------------------------------------------- session

export function useMe() {
  return useQuery({
    queryKey: keys.me,
    queryFn: () => api<Me>('/api/v1/auth/me'),
    retry: false,
    staleTime: 5 * 60_000,
  })
}

export function useLogin() {
  const qc = useQueryClient()
  return useMutation({
    mutationFn: (body: { username: string; password: string }) => api<Me>('/api/v1/auth/login', 'POST', body),
    onSuccess: (me) => qc.setQueryData(keys.me, me),
  })
}

export function useLogout() {
  const qc = useQueryClient()
  return useMutation({
    mutationFn: () => api<void>('/api/v1/auth/logout', 'POST'),
    onSettled: () => {
      qc.clear()
      qc.setQueryData(keys.me, null)
    },
  })
}

// ---------------------------------------------------------------- packs

export function usePacks(enabled = true) {
  return useQuery({
    queryKey: keys.packs,
    queryFn: () => api<Pack[]>('/api/v1/packs'),
    enabled,
    refetchInterval: 15_000,
  })
}

export function useUpdatePack(packId: number) {
  const qc = useQueryClient()
  return useMutation({
    mutationFn: (body: PackUpdate) => api<Pack>(`/api/v1/packs/${packId}`, 'PATCH', body),
    onSuccess: () => qc.invalidateQueries({ queryKey: keys.packs }),
  })
}

/** First paint before the live stream connects; the stream then takes over. */
export function useLatest(packId: number | undefined) {
  return useQuery({
    queryKey: keys.latest(packId ?? 0),
    queryFn: async () => (await api<LiveTelemetry | undefined>(`/api/v1/packs/${packId}/telemetry/latest`)) ?? null,
    enabled: packId !== undefined,
    staleTime: Infinity,
  })
}

// ---------------------------------------------------------------- history

export function useHistory(packId: number | undefined, from: string, to: string) {
  return useQuery({
    queryKey: keys.history(packId ?? 0, from, to),
    queryFn: () => api<History>(`/api/v1/packs/${packId}/telemetry?from=${enc(from)}&to=${enc(to)}`),
    enabled: packId !== undefined,
    placeholderData: keepPreviousData,
  })
}

export function useCellHistory(packId: number | undefined, cellNo: number | null, from: string, to: string) {
  return useQuery({
    queryKey: keys.cellHistory(packId ?? 0, cellNo ?? 0, from),
    queryFn: () => api<CellHistory>(`/api/v1/packs/${packId}/cells/${cellNo}/history?from=${enc(from)}&to=${enc(to)}`),
    enabled: packId !== undefined && cellNo !== null,
    placeholderData: keepPreviousData,
  })
}

export function exportUrl(packId: number, from: string, to: string) {
  return `/api/v1/packs/${packId}/telemetry/export?from=${enc(from)}&to=${enc(to)}`
}

// ---------------------------------------------------------------- alarms

export interface AlarmFilter {
  status: 'ACTIVE' | 'CLEARED' | 'ALL'
  severity: 'CRITICAL' | 'WARNING' | 'ALL'
  page: number
  size: number
}

export function useAlarms(packId: number | undefined, f: AlarmFilter) {
  return useQuery({
    queryKey: [...keys.alarms(packId ?? 0), f],
    queryFn: () => api<Page<Alarm>>(
      `/api/v1/packs/${packId}/alarms?status=${f.status}&severity=${f.severity}&page=${f.page}&size=${f.size}`),
    enabled: packId !== undefined,
    placeholderData: keepPreviousData,
    refetchInterval: 10_000,
  })
}

export function useAcknowledgeAlarm(packId: number) {
  const qc = useQueryClient()
  return useMutation({
    mutationFn: (alarmId: number) => api<Alarm>(`/api/v1/alarms/${alarmId}`, 'PATCH', { acknowledged: true }),
    onSuccess: () => qc.invalidateQueries({ queryKey: keys.alarms(packId) }),
  })
}

// ---------------------------------------------------------------- commands

export function useCommands(packId: number | undefined, page = 0) {
  return useQuery({
    queryKey: [...keys.commands(packId ?? 0), page],
    queryFn: () => api<Page<Command>>(`/api/v1/packs/${packId}/commands?page=${page}&size=10`),
    enabled: packId !== undefined,
    placeholderData: keepPreviousData,
    // poll quickly only while something is still waiting for the gateway
    refetchInterval: (q) =>
      q.state.data?.content.some((c) => c.status === 'PENDING' || c.status === 'PUBLISHED') ? 1000 : 10_000,
  })
}

export function useIssueCommand(packId: number) {
  const qc = useQueryClient()
  return useMutation({
    mutationFn: (body: { type: CommandType; argument: number | null }) =>
      api<Command>(`/api/v1/packs/${packId}/commands`, 'POST', body),
    onSettled: () => qc.invalidateQueries({ queryKey: keys.commands(packId) }),
  })
}

// ---------------------------------------------------------------- users

export function useUsers(enabled: boolean) {
  return useQuery({
    queryKey: keys.users,
    queryFn: () => api<Page<User>>('/api/v1/users?size=50&sort=username'),
    enabled,
  })
}

export function useCreateUser() {
  const qc = useQueryClient()
  return useMutation({
    mutationFn: (body: { username: string; displayName: string; password: string; role: Role }) =>
      api<User>('/api/v1/users', 'POST', body),
    onSuccess: () => qc.invalidateQueries({ queryKey: keys.users }),
  })
}

export function useUpdateUser() {
  const qc = useQueryClient()
  return useMutation({
    mutationFn: ({ id, ...body }: { id: number; role?: Role; enabled?: boolean }) =>
      api<User>(`/api/v1/users/${id}`, 'PATCH', body),
    onSuccess: () => qc.invalidateQueries({ queryKey: keys.users }),
  })
}

function enc(s: string) {
  return encodeURIComponent(s)
}
