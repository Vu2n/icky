import type { Catalog, CatalogEntry, DumpField, IckyDump } from './types'
import { buildDumpIssueUrl } from './site'

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

export function countEncryption(dump: IckyDump): {
  encrypted: number
  withDecrypt: number
  withAlgo: number
} {
  let encrypted = dump.stats?.encrypted_fields ?? 0
  let withDecrypt = dump.stats?.encrypted_with_decrypt ?? 0
  let withAlgo = dump.stats?.encrypted_with_algo ?? 0
  if (encrypted || withDecrypt) {
    return { encrypted, withDecrypt, withAlgo }
  }
  // Fallback scan for older dumps without stats
  for (const t of dump.types || []) {
    for (const f of t.fields || []) {
      if (f.encrypted || f.decrypt?.decrypt_rva) encrypted++
      if (f.decrypt?.decrypt_rva) withDecrypt++
      if (f.decrypt?.xor?.length || f.decrypt?.rol?.length || f.decrypt?.add?.length) withAlgo++
    }
  }
  return { encrypted, withDecrypt, withAlgo }
}

export function hasEncryption(dump: IckyDump): boolean {
  const c = countEncryption(dump)
  return c.encrypted > 0 || c.withDecrypt > 0 || !!dump.encryption
}

export function catalogFromDump(dump: IckyDump, path?: string): CatalogEntry {
  const enc = countEncryption(dump)
  const tags = [dump.engine.id, dump.mode]
  if (enc.withDecrypt > 0 || enc.encrypted > 0) tags.push('encrypted')
  if (enc.withDecrypt > 0) tags.push('decrypt')
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
      encrypted_fields: enc.encrypted || undefined,
      encrypted_with_decrypt: enc.withDecrypt || undefined,
    },
    path: path ?? `dumps/${dump.game.slug}/dump.json`,
    tags,
  }
}

export async function fetchCatalog(): Promise<Catalog> {
  // Cache-bust so re-published dumps show up after deploy (GH Pages is aggressive)
  const url = `${import.meta.env.BASE_URL}dumps/catalog.json?t=${Date.now()}`
  const res = await fetch(url, { cache: 'no-store' })
  if (!res.ok) throw new Error(`catalog ${res.status}`)
  return res.json() as Promise<Catalog>
}

export async function fetchDump(path: string): Promise<IckyDump> {
  const bare = path.replace(/^\//, '')
  const sep = bare.includes('?') ? '&' : '?'
  const url = path.startsWith('http')
    ? path
    : `${import.meta.env.BASE_URL}${bare}${sep}t=${Date.now()}`
  const res = await fetch(url, { cache: 'no-store' })
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

/**
 * Strip method lists so huge IL2CPP dumps fit GitHub issue attachments (~25MB).
 * Keeps fields, decrypt metadata, globals — enough for catalog + site SDK gen.
 */
export function slimDumpForPublish(dump: IckyDump): IckyDump {
  const types = (dump.types || []).map((t) => {
    const { methods: _m, ...rest } = t
    return {
      ...rest,
      // Keep empty array so schema stays valid; methods are optional for site SDK
      methods: [] as typeof t.methods,
    }
  })
  return {
    ...dump,
    types,
    layout: {
      ...(dump.layout || {}),
      publish_slim: '1',
      publish_note: 'methods stripped for GitHub issue size limit',
    },
  }
}

/** Gzip bytes (browser CompressionStream) */
export async function gzipBytes(data: Uint8Array): Promise<Uint8Array> {
  if (typeof CompressionStream === 'undefined') {
    throw new Error('Gzip not supported in this browser — use slim JSON without .gz')
  }
  const stream = new Blob([data.buffer as ArrayBuffer])
    .stream()
    .pipeThrough(new CompressionStream('gzip'))
  const buf = await new Response(stream).arrayBuffer()
  return new Uint8Array(buf)
}

/** Download slim (and preferably gzipped) dump for attaching to a GitHub issue */
export async function downloadDumpForGitHubIssue(dump: IckyDump): Promise<{
  filename: string
  bytes: number
  slimmed: boolean
}> {
  const slim = slimDumpForPublish(dump)
  const json = JSON.stringify(slim)
  const raw = new TextEncoder().encode(json)
  let out: Uint8Array = raw
  let filename = 'icky.dump.json'
  try {
    out = await gzipBytes(raw)
    filename = 'icky.dump.json.gz'
  } catch {
    /* plain slim json */
  }
  const blob = new Blob([out.buffer as ArrayBuffer], {
    type: filename.endsWith('.gz') ? 'application/gzip' : 'application/json',
  })
  const a = document.createElement('a')
  a.href = URL.createObjectURL(blob)
  a.download = filename
  a.click()
  URL.revokeObjectURL(a.href)
  return { filename, bytes: out.length, slimmed: true }
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
  const enc = countEncryption(dump)
  const url = buildDumpIssueUrl({
    game: dump.game.name,
    engine: dump.engine.id,
    slug: dump.game.slug,
    types: dump.stats?.types ?? dump.types.length,
    globals: dump.stats?.globals ?? dump.globals.length,
    mode: dump.mode,
    encrypted: enc.withDecrypt > 0 || enc.encrypted > 0,
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

export function fieldIsEncrypted(f: DumpField): boolean {
  return !!(f.encrypted || f.decrypt?.decrypt_rva || (f.type && f.type.startsWith('Encrypted<')))
}
