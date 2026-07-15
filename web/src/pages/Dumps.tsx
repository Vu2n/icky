import { useEffect, useState } from 'react'
import { Search } from 'lucide-react'
import type { CatalogEntry } from '../lib/types'
import { engineColor, fetchCatalog, formatDate } from '../lib/dump'

export function Dumps() {
  const [official, setOfficial] = useState<CatalogEntry[]>([])
  const [q, setQ] = useState('')
  const [engine, setEngine] = useState('all')
  const [err, setErr] = useState<string | null>(null)
  const [loading, setLoading] = useState(true)

  useEffect(() => {
    let cancelled = false
    ;(async () => {
      try {
        const cat = await fetchCatalog()
        if (!cancelled) setOfficial(cat.dumps || [])
      } catch (e) {
        if (!cancelled) setErr(e instanceof Error ? e.message : 'Failed to load catalog')
      } finally {
        if (!cancelled) setLoading(false)
      }
    })()
    return () => {
      cancelled = true
    }
  }, [])

  const filtered = official.filter((d) => {
    if (engine !== 'all' && d.engine !== engine) return false
    if (!q.trim()) return true
    const s = q.toLowerCase()
    return (
      d.game.toLowerCase().includes(s) ||
      d.executable.toLowerCase().includes(s) ||
      d.slug.includes(s) ||
      d.engine.includes(s) ||
      (d.tags || []).some((t) => t.includes(s))
    )
  })

  return (
    <section className="mx-auto max-w-6xl px-5 pb-24 pt-28 sm:px-8">
      <div className="mb-10">
        <h1 className="font-display text-3xl font-bold tracking-tight sm:text-4xl">Dump catalog</h1>
        <p className="mt-3 max-w-xl text-muted">
          One entry per game. Uploading a dump for a game that already exists{' '}
          <strong className="text-text">updates</strong> that entry instead of duplicating it.
        </p>
      </div>

      <div className="mb-8 flex flex-col gap-3 sm:flex-row sm:items-center">
        <div className="relative flex-1">
          <Search
            className="pointer-events-none absolute left-3 top-1/2 -translate-y-1/2 text-dim"
            size={16}
          />
          <input
            value={q}
            onChange={(e) => setQ(e.target.value)}
            placeholder="Search games, engines, executables…"
            className="w-full rounded-xl border border-white/[0.07] bg-card py-2.5 pl-10 pr-4 text-sm text-text outline-none placeholder:text-dim focus:border-accent/40"
          />
        </div>
        <select
          value={engine}
          onChange={(e) => setEngine(e.target.value)}
          className="rounded-xl border border-white/[0.07] bg-card px-4 py-2.5 text-sm text-text outline-none focus:border-accent/40"
        >
          <option value="all">All engines</option>
          <option value="unreal">Unreal</option>
          <option value="il2cpp">IL2CPP</option>
          <option value="mono">Mono</option>
          <option value="source1">Source 1</option>
          <option value="source2">Source 2</option>
        </select>
      </div>

      {loading && <p className="text-sm text-muted">Loading catalog…</p>}
      {err && (
        <p className="mb-4 rounded-xl border border-bad/30 bg-bad/10 px-4 py-3 text-sm text-bad">
          {err}
        </p>
      )}

      <div className="grid gap-4 sm:grid-cols-2 lg:grid-cols-3">
        {filtered.map((d) => {
          const enc =
            (d.stats.encrypted_with_decrypt ?? 0) > 0 ||
            (d.stats.encrypted_fields ?? 0) > 0 ||
            (d.tags || []).includes('encrypted') ||
            (d.tags || []).includes('decrypt')
          return (
            <a
              key={d.slug}
              href={`#/dump/${d.slug}`}
              className="card-surface group block p-5 transition-all hover:-translate-y-0.5 hover:border-accent/25"
            >
              <div className="mb-3 flex items-start justify-between gap-2">
                <h2 className="font-display text-lg font-semibold group-hover:text-accent-hi">
                  {d.game}
                </h2>
              </div>
              <p className="truncate font-mono text-xs text-dim">{d.executable}</p>
              <div className="mt-4 flex flex-wrap items-center gap-2">
                <span
                  className={`rounded-md border px-2 py-0.5 text-[11px] font-medium ${engineColor(d.engine)}`}
                >
                  {d.engine_label || d.engine}
                </span>
                <span className="rounded-md border border-white/10 bg-white/[0.03] px-2 py-0.5 text-[11px] text-muted">
                  {d.mode}
                </span>
                {enc && (
                  <span className="rounded-md border border-amber-500/30 bg-amber-500/10 px-2 py-0.5 text-[11px] text-amber-200">
                    encrypted
                    {(d.stats.encrypted_with_decrypt ?? 0) > 0
                      ? ` · ${d.stats.encrypted_with_decrypt} decrypts`
                      : ''}
                  </span>
                )}
              </div>
              <dl className="mt-4 grid grid-cols-3 gap-2 border-t border-white/[0.05] pt-4 text-center">
                <div>
                  <dt className="text-[10px] uppercase text-dim">Types</dt>
                  <dd className="font-display text-sm font-semibold">{d.stats.types}</dd>
                </div>
                <div>
                  <dt className="text-[10px] uppercase text-dim">Globals</dt>
                  <dd className="font-display text-sm font-semibold">{d.stats.globals}</dd>
                </div>
                <div>
                  <dt className="text-[10px] uppercase text-dim">Updated</dt>
                  <dd className="truncate text-[11px] text-muted">{formatDate(d.generated_at)}</dd>
                </div>
              </dl>
            </a>
          )
        })}
      </div>

      {!loading && filtered.length === 0 && (
        <p className="mt-10 text-center text-muted">
          No dumps yet.{' '}
          <a href="#/upload" className="text-accent hover:underline">
            Share one
          </a>
          .
        </p>
      )}
    </section>
  )
}
