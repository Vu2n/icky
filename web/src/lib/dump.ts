import type { Catalog, CatalogEntry, IckyDump } from './types'
import { buildDumpIssueUrl } from './site'

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
  // Single-game site: browser library keeps only the latest dump
  localStorage.setItem(LOCAL_KEY, JSON.stringify([dump]))
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

/** Prefer browser "Save As" with a stable name for attaching to GitHub issues */
export function downloadDumpForSubmit(dump: IckyDump) {
  downloadJson('icky.dump.json', dump)
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

export function formatBytes(n: number): string {
  if (n < 1024) return `${n} B`
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KB`
  return `${(n / (1024 * 1024)).toFixed(1)} MB`
}

/** Walk a FileList (including folder drops) and return the best dump candidate */
export async function pickDumpFromFiles(files: FileList | File[]): Promise<
  { ok: true; dump: IckyDump; file: File; size: number } | { ok: false; error: string }
> {
  const list = Array.from(files)
  if (!list.length) return { ok: false, error: 'No files dropped' }

  // Prefer icky.dump.json by name, then any .json
  const ranked = [...list].sort((a, b) => {
    const score = (f: File) => {
      const n = f.name.toLowerCase()
      const path = ((f as File & { webkitRelativePath?: string }).webkitRelativePath || n).toLowerCase()
      if (n === 'icky.dump.json' || path.endsWith('/icky.dump.json')) return 0
      if (n.endsWith('.icky.dump.json')) return 1
      if (n.includes('icky') && n.endsWith('.json')) return 2
      if (n.endsWith('.json')) return 3
      return 9
    }
    return score(a) - score(b)
  })

  const errors: string[] = []
  for (const file of ranked) {
    if (!file.name.toLowerCase().endsWith('.json') && file.type && !file.type.includes('json')) {
      continue
    }
    try {
      const text = await file.text()
      const json = JSON.parse(text) as unknown
      const v = validateDump(json)
      if (v.ok) {
        return { ok: true, dump: v.dump, file, size: file.size }
      }
      errors.push(`${file.name}: ${v.error}`)
    } catch (e) {
      errors.push(`${file.name}: ${e instanceof Error ? e.message : 'parse error'}`)
    }
  }

  if (errors.length) {
    return {
      ok: false,
      error: errors.slice(0, 3).join(' · ') + (errors.length > 3 ? ' …' : ''),
    }
  }
  return {
    ok: false,
    error: 'No icky.dump.json found. Drop the file from your dump folder (or the whole folder).',
  }
}

export function openSubmitIssue(dump: IckyDump) {
  const url = buildDumpIssueUrl({
    game: dump.game.name,
    engine: dump.engine.id,
    slug: dump.game.slug,
    types: dump.stats?.types ?? dump.types.length,
    globals: dump.stats?.globals ?? dump.globals.length,
    mode: dump.mode,
  })
  window.open(url, '_blank', 'noopener,noreferrer')
}

/** Approximate serialized size */
export function estimateDumpSize(dump: IckyDump): number {
  try {
    return new Blob([JSON.stringify(dump)]).size
  } catch {
    return 0
  }
}
