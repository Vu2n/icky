import { useEffect, useState } from 'react'
import { Database, RefreshCw, ArrowRight } from 'lucide-react'
import type { CatalogEntry } from '../lib/types'
import { engineColor, fetchCatalog, formatDate } from '../lib/dump'
import { Button } from '../components/ui/Button'

export function Dumps() {
  const [current, setCurrent] = useState<CatalogEntry | null>(null)
  const [err, setErr] = useState<string | null>(null)
  const [loading, setLoading] = useState(true)
  const [updatedAt, setUpdatedAt] = useState<string | null>(null)

  useEffect(() => {
    let cancelled = false
    ;(async () => {
      try {
        const cat = await fetchCatalog()
        if (cancelled) return
        setUpdatedAt(cat.updated_at || null)
        // Single-game catalog: at most one official dump
        setCurrent((cat.dumps && cat.dumps[0]) || null)
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

  return (
    <section className="mx-auto max-w-3xl px-5 pb-24 pt-28 sm:px-8">
      <div className="mb-2 inline-flex items-center gap-2 rounded-full border border-white/[0.07] bg-card/80 px-3 py-1 text-xs text-muted">
        <Database size={12} className="text-accent" />
        Single-game catalog
      </div>
      <h1 className="font-display text-3xl font-bold tracking-tight sm:text-4xl">Current dump</h1>
      <p className="mt-3 text-muted">
        This site hosts <strong className="text-text">one game at a time</strong>. Uploading a new
        dump replaces whatever is live — same game = update, different game = switch.
      </p>

      {loading && <p className="mt-10 text-sm text-muted">Loading…</p>}
      {err && (
        <p className="mt-6 rounded-xl border border-bad/30 bg-bad/10 px-4 py-3 text-sm text-bad">
          {err}
        </p>
      )}

      {!loading && !err && !current && (
        <div className="card-surface mt-10 p-8 text-center">
          <p className="font-display text-lg font-semibold">No dump published yet</p>
          <p className="mt-2 text-sm text-muted">
            Be the first — share an <code className="text-accent">icky.dump.json</code> and it becomes
            the live dump.
          </p>
          <Button href="#/upload" className="mt-6" size="lg">
            Share a dump
            <ArrowRight size={16} />
          </Button>
        </div>
      )}

      {current && (
        <a
          href={`#/dump/${current.slug}`}
          className="card-surface group mt-10 block p-6 transition-all hover:-translate-y-0.5 hover:border-accent/25"
        >
          <div className="mb-3 flex flex-wrap items-center gap-2">
            <span className="rounded-md border border-good/30 bg-good/10 px-2 py-0.5 text-[11px] font-medium text-good">
              Live
            </span>
            <span
              className={`rounded-md border px-2 py-0.5 text-[11px] font-medium ${engineColor(current.engine)}`}
            >
              {current.engine_label || current.engine}
            </span>
            <span className="rounded-md border border-white/10 px-2 py-0.5 text-[11px] text-muted">
              {current.mode}
            </span>
          </div>
          <h2 className="font-display text-2xl font-semibold group-hover:text-accent-hi">
            {current.game}
          </h2>
          <p className="mt-1 font-mono text-xs text-dim">{current.executable}</p>

          <dl className="mt-6 grid grid-cols-3 gap-3 border-t border-white/[0.06] pt-5 text-center">
            <div>
              <dt className="text-[10px] uppercase text-dim">Types</dt>
              <dd className="font-display text-xl font-semibold">{current.stats.types}</dd>
            </div>
            <div>
              <dt className="text-[10px] uppercase text-dim">Globals</dt>
              <dd className="font-display text-xl font-semibold">{current.stats.globals}</dd>
            </div>
            <div>
              <dt className="text-[10px] uppercase text-dim">Updated</dt>
              <dd className="text-xs text-muted">
                {formatDate(current.generated_at || updatedAt || '')}
              </dd>
            </div>
          </dl>

          <p className="mt-5 flex items-center gap-2 text-sm text-accent">
            View offsets
            <ArrowRight size={14} className="transition-transform group-hover:translate-x-0.5" />
          </p>
        </a>
      )}

      <div className="mt-8 flex flex-wrap items-center gap-3 text-sm text-muted">
        <RefreshCw size={14} className="text-dim" />
        <span>
          Re-upload the same (or another) game from{' '}
          <a href="#/upload" className="text-accent hover:underline">
            Share a dump
          </a>{' '}
          to replace this entry.
        </span>
      </div>
    </section>
  )
}
