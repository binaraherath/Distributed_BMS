import type { Problem } from './types'

/** An error response from the API, carrying the RFC 9457 problem detail. */
export class ApiError extends Error {
  readonly status: number
  readonly problem: Problem

  constructor(problem: Problem) {
    super(problem.detail ?? problem.title ?? `Request failed (${problem.status})`)
    this.status = problem.status
    this.problem = problem
  }
}

type Method = 'GET' | 'POST' | 'PATCH' | 'DELETE'

/**
 * Calls the backend on the same origin. The session is an httpOnly cookie, so
 * there is no token handling here. Returns undefined for 202/204 without a body.
 */
export async function api<T>(path: string, method: Method = 'GET', body?: unknown): Promise<T> {
  const res = await fetch(path, {
    method,
    credentials: 'same-origin',
    headers: body === undefined ? { Accept: 'application/json' } : {
      Accept: 'application/json',
      'Content-Type': 'application/json',
    },
    body: body === undefined ? undefined : JSON.stringify(body),
  })

  if (!res.ok) {
    let problem: Problem = { status: res.status }
    try {
      problem = { ...(await res.json()), status: res.status }
    } catch {
      // not JSON (e.g. proxy error page)
      problem.title = res.status >= 500 ? 'The server is not reachable' : res.statusText
    }
    throw new ApiError(problem)
  }
  if (res.status === 204 || res.headers.get('Content-Length') === '0') {
    return undefined as T
  }
  return (await res.json()) as T
}

export function isUnauthorized(error: unknown): boolean {
  return error instanceof ApiError && error.status === 401
}

export function errorMessage(error: unknown): string {
  if (error instanceof ApiError) {
    return error.message
  }
  if (error instanceof TypeError) {
    return 'The server is not reachable'
  }
  return error instanceof Error ? error.message : 'Something went wrong'
}
