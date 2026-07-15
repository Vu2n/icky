import type { Catalog, CatalogEntry, IckyDump } from './types'

const LOCAL_KEY = 'icky.community_dumps.v1'

export function validateDump(data: unknown): { ok: true; dump: IckyDump } | { ok: false; error: string } {
  if (!data || typeof data !== 'object') return { ok: false, error: 'Not a JSON object' }
  const d = data as Record<string, unknown>
  if (typeof d.schema !== 'string' || !d.schema.startsWith('icky.dump')) {
    return { ok: false, error: 'Missing schema "icky.dump/v1" — re-dump with a recent Icky build' }
  }
  if (!d.game || typeof d.game !== 'object') return { ok: false, error: 'Missing game block' }
  if (!d.engine || typeof d.engine !== 'object') return { ok: false, error: 'Missing engine block' }
  if (!Array.isArray(d.globals)) return { ok: false, error: 'Missing globals array' }
  if (!Array.isArray(d.types)) return { ok: false, error: 'Missing types array' }
  const game = d.game as Record<string, unknown>
  if (!game.slug || !game.name) return { ok: false, error: 'game.slug and game.name required' }
  return { ok: true, dump: data as IckyDump }
}

export function catalogFromDump(dump: IckyDump, path?: string): CatalogEntry {
  return {
    slug: dump.game.slug,
    game: dump.game.name,
    executable: dump.game.executable,
    engine: dump.engine.id,
    engine_label: dump.engine.label,
    mode: dump.mode,
    generated_at: dump.generated_at,
    stats: {
      types: dump.stats?.types ?? dump.types.length,
      classes: dump.stats?.classes,
      globals: dump.stats?.globals ?? dump.globals.length,
      packages: dump.stats?.packages,
    },
    path: path ?? `dumps/${dump.game.slug}/dump.json`,
    tags: [dump.engine.id, dump.mode],
  }
}

export function loadLocalDumps(): IckyDump[] {
  try {
    const raw = localStorage.getItem(LOCAL_KEY)
    if (!raw) return []
    const arr = JSON.parse(raw) as IckyDump[]
    return Array.isArray(arr) ? arr : []
  } catch {
    return []
  }
}

export function saveLocalDump(dump: IckyDump): void {
  const list = loadLocalDumps().filter((d) => d.game.slug !== dump.game.slug)
  list.unshift(dump)
  localStorage.setItem(LOCAL_KEY, JSON.stringify(list.slice(0, 20)))
}

export function removeLocalDump(slug: string): void {
  const list = loadLocalDumps().filter((d) => d.game.slug !== slug)
  localStorage.setItem(LOCAL_KEY, JSON.stringify(list))
}

export async function fetchCatalog(): Promise<Catalog> {
  const url = `${import.meta.env.BASE_URL}dumps/catalog.json`
  const res = await fetch(url)
  if (!res.ok) throw new Error(`catalog ${res.status}`)
  return res.json() as Promise<Catalog>
}

export async function fetchDump(path: string): Promise<IckyDump> {
  // path is relative like dumps/foo/dump.json
  const url = path.startsWith('http')
    ? path
    : `${import.meta.env.BASE_URL}${path.replace(/^\//, '')}`
  const res = await fetch(url)
  if (!res.ok) throw new Error(`dump ${res.status}`)
  const json = await res.json()
  const v = validateDump(json)
  if (!v.ok) throw new Error(v.error)
  return v.dump
}

export function engineColor(id: string): string {
  switch (id) {
    case 'unreal':
      return 'bg-sky-500/15 text-sky-300 border-sky-500/25'
    case 'il2cpp':
      return 'bg-violet-500/15 text-violet-300 border-violet-500/25'
    case 'mono':
      return 'bg-amber-500/15 text-amber-300 border-amber-500/25'
    case 'source1':
    case 'source2':
      return 'bg-orange-500/15 text-orange-300 border-orange-500/25'
    default:
      return 'bg-white/5 text-muted border-white/10'
  }
}

export function downloadJson(filename: string, data: unknown) {
  const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' })
  const a = document.createElement('a')
  a.href = URL.createObjectURL(blob)
  a.download = filename
  a.click()
  URL.revokeObjectURL(a.href)
}

export function formatDate(iso: string): string {
  try {
    return new Date(iso).toLocaleString(undefined, {
      dateStyle: 'medium',
      timeStyle: 'short',
    })
  } catch {
    return iso
  }
}
