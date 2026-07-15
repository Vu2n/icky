import { useEffect, useMemo, useState } from 'react'
import { ArrowLeft, Copy, Check, Download } from 'lucide-react'
import type { IckyDump } from '../lib/types'
import {
  downloadJson,
  engineColor,
  fetchCatalog,
  fetchDump,
  formatDate,
  loadLocalDumps,
} from '../lib/dump'
import { Button } from '../components/ui/Button'

export function DumpDetail({ slug, localOnly }: { slug: string; localOnly?: boolean }) {
  const [dump, setDump] = useState<IckyDump | null>(null)
  const [err, setErr] = useState<string | null>(null)
  const [tab, setTab] = useState<'globals' | 'types' | 'layout'>('globals')
  const [q, setQ] = useState('')
  const [copied, setCopied] = useState<string | null>(null)

  useEffect(() => {
    let cancelled = false
    ;(async () => {
      try {
        if (localOnly) {
          const d = loadLocalDumps().find((x) => x.game.slug === slug)
          if (!d) throw new Error('Local dump not found')
          if (!cancelled) setDump(d)
          return
        }
        // try official path from catalog
        const cat = await fetchCatalog()
        const entry = cat.dumps.find((d) => d.slug === slug)
        if (entry) {
          const d = await fetchDump(entry.path)
          if (!cancelled) setDump(d)
          return
        }
        const local = loadLocalDumps().find((x) => x.game.slug === slug)
        if (local) {
          if (!cancelled) setDump(local)
          return
        }
        throw new Error('Dump not found in catalog or local library')
      } catch (e) {
        if (!cancelled) setErr(e instanceof Error ? e.message : 'Load failed')
      }
    })()
    return () => {
      cancelled = true
    }
  }, [slug, localOnly])

  const types = useMemo(() => {
    if (!dump) return []
    const s = q.toLowerCase()
    return dump.types.filter((t) => {
      if (!s) return true
      return (
        t.name.toLowerCase().includes(s) ||
        (t.package || '').toLowerCase().includes(s) ||
        (t.full_name || '').toLowerCase().includes(s)
      )
    })
  }, [dump, q])

  const copy = async (text: string, id: string) => {
    await navigator.clipboard.writeText(text)
    setCopied(id)
    setTimeout(() => setCopied(null), 1200)
  }

  if (err) {
    return (
      <section className="mx-auto max-w-6xl px-5 pb-24 pt-28">
        <a href="#/dumps" className="mb-6 inline-flex items-center gap-2 text-sm text-muted hover:text-text">
          <ArrowLeft size={14} /> Back
        </a>
        <p className="text-bad">{err}</p>
      </section>
    )
  }

  if (!dump) {
    return (
      <section className="mx-auto max-w-6xl px-5 pb-24 pt-28">
        <p className="text-muted">Loading dump…</p>
      </section>
    )
  }

  return (
    <section className="mx-auto max-w-6xl px-5 pb-24 pt-28 sm:px-8">
      <a href="#/dumps" className="mb-6 inline-flex items-center gap-2 text-sm text-muted hover:text-text">
        <ArrowLeft size={14} /> Catalog
      </a>

      <div className="flex flex-col gap-6 lg:flex-row lg:items-start lg:justify-between">
        <div>
          <div className="mb-3 flex flex-wrap items-center gap-2">
            <span className={`rounded-md border px-2 py-0.5 text-xs ${engineColor(dump.engine.id)}`}>
              {dump.engine.label}
            </span>
            <span className="rounded-md border border-white/10 px-2 py-0.5 text-xs text-muted">
              {dump.mode}
            </span>
          </div>
          <h1 className="font-display text-3xl font-bold tracking-tight">{dump.game.name}</h1>
          <p className="mt-1 font-mono text-sm text-dim">{dump.game.executable}</p>
          <p className="mt-3 text-sm text-muted">
            {formatDate(dump.generated_at)} · Icky {dump.tool.version}
            {dump.engine.detail ? ` · ${dump.engine.detail}` : ''}
          </p>
        </div>
        <Button
          variant="outline"
          onClick={() => downloadJson(`${dump.game.slug}.icky.dump.json`, dump)}
        >
          <Download size={16} />
          Download JSON
        </Button>
      </div>

      <dl className="mt-8 grid grid-cols-2 gap-3 sm:grid-cols-4 lg:grid-cols-6">
        {[
          ['Types', dump.stats.types],
          ['Classes', dump.stats.classes],
          ['Structs', dump.stats.structs],
          ['Enums', dump.stats.enums],
          ['Functions', dump.stats.functions],
          ['Globals', dump.stats.globals],
        ].map(([k, v]) => (
          <div key={k as string} className="card-surface px-4 py-3 text-center">
            <dt className="text-[10px] uppercase tracking-wider text-dim">{k}</dt>
            <dd className="font-display text-xl font-semibold">{v as number}</dd>
          </div>
        ))}
      </dl>

      <div className="mt-10 flex gap-2 border-b border-white/[0.06] pb-px">
        {(['globals', 'types', 'layout'] as const).map((t) => (
          <button
            key={t}
            type="button"
            onClick={() => setTab(t)}
            className={`rounded-t-lg px-4 py-2 text-sm capitalize transition-colors ${
              tab === t ? 'bg-card text-text' : 'text-muted hover:text-text'
            }`}
          >
            {t}
          </button>
        ))}
      </div>

      <div className="card-surface mt-0 rounded-tl-none p-0 overflow-hidden">
        {tab === 'globals' && (
          <div className="overflow-x-auto">
            <table className="w-full min-w-[640px] text-left text-sm">
              <thead className="border-b border-white/[0.06] bg-side/80 text-xs uppercase tracking-wider text-dim">
                <tr>
                  <th className="px-4 py-3 font-medium">Name</th>
                  <th className="px-4 py-3 font-medium">RVA</th>
                  <th className="px-4 py-3 font-medium">Address</th>
                  <th className="px-4 py-3 font-medium">Type</th>
                  <th className="px-4 py-3 font-medium" />
                </tr>
              </thead>
              <tbody>
                {dump.globals.map((g) => (
                  <tr key={g.name} className="border-b border-white/[0.04] hover:bg-white/[0.02]">
                    <td className="px-4 py-2.5 font-medium text-text">{g.name}</td>
                    <td className="px-4 py-2.5 font-mono text-xs text-accent">{g.rva}</td>
                    <td className="px-4 py-2.5 font-mono text-xs text-muted">{g.address}</td>
                    <td className="px-4 py-2.5 text-muted">{g.type}</td>
                    <td className="px-4 py-2.5">
                      <button
                        type="button"
                        className="text-dim hover:text-text"
                        onClick={() => copy(g.rva, g.name)}
                        aria-label="Copy RVA"
                      >
                        {copied === g.name ? <Check size={14} className="text-good" /> : <Copy size={14} />}
                      </button>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}

        {tab === 'types' && (
          <div>
            <div className="border-b border-white/[0.06] p-3">
              <input
                value={q}
                onChange={(e) => setQ(e.target.value)}
                placeholder="Filter types / packages…"
                className="w-full rounded-lg border border-white/[0.07] bg-bg px-3 py-2 text-sm outline-none focus:border-accent/40"
              />
            </div>
            <div className="max-h-[560px] overflow-y-auto">
              {types.slice(0, 500).map((t, i) => (
                <details
                  key={`${t.name}-${i}`}
                  className="border-b border-white/[0.04] px-4 py-3 open:bg-white/[0.015]"
                >
                  <summary className="cursor-pointer list-none">
                    <div className="flex flex-wrap items-center gap-2">
                      <span className="rounded bg-white/[0.05] px-1.5 py-0.5 text-[10px] uppercase text-dim">
                        {t.kind}
                      </span>
                      <span className="font-medium">{t.name}</span>
                      {t.parent && (
                        <span className="text-xs text-dim">: {t.parent}</span>
                      )}
                      {t.size != null && t.size > 0 && (
                        <span className="font-mono text-[11px] text-muted">0x{t.size.toString(16)}</span>
                      )}
                    </div>
                    <p className="mt-0.5 truncate font-mono text-[11px] text-dim">
                      {t.package} · {t.full_name}
                    </p>
                  </summary>
                  {(t.fields?.length ?? 0) > 0 && (
                    <div className="mt-3 overflow-x-auto">
                      <table className="w-full text-left text-xs">
                        <thead className="text-dim">
                          <tr>
                            <th className="py-1 pr-3">Offset</th>
                            <th className="py-1 pr-3">Name</th>
                            <th className="py-1">Type</th>
                          </tr>
                        </thead>
                        <tbody>
                          {t.fields!.slice(0, 80).map((f) => (
                            <tr key={f.name + f.offset}>
                              <td className="py-0.5 pr-3 font-mono text-accent">
                                0x{f.offset.toString(16)}
                              </td>
                              <td className="py-0.5 pr-3">{f.name}</td>
                              <td className="py-0.5 text-muted">{f.type}</td>
                            </tr>
                          ))}
                        </tbody>
                      </table>
                      {(t.fields?.length ?? 0) > 80 && (
                        <p className="mt-1 text-[11px] text-dim">
                          +{(t.fields!.length - 80).toString()} more fields in JSON
                        </p>
                      )}
                    </div>
                  )}
                  {(t.enum_members?.length ?? 0) > 0 && (
                    <ul className="mt-2 space-y-0.5 font-mono text-xs text-muted">
                      {t.enum_members!.slice(0, 40).map((e) => (
                        <li key={e.name}>
                          {e.name} = {e.value}
                        </li>
                      ))}
                    </ul>
                  )}
                </details>
              ))}
              {types.length > 500 && (
                <p className="p-4 text-center text-xs text-dim">
                  Showing 500 / {types.length} — download JSON for full dump
                </p>
              )}
            </div>
          </div>
        )}

        {tab === 'layout' && (
          <pre className="overflow-x-auto p-4 font-mono text-xs text-muted">
            {JSON.stringify(
              {
                module: dump.module,
                layout: dump.layout,
                engine: dump.engine,
              },
              null,
              2,
            )}
          </pre>
        )}
      </div>
    </section>
  )
}
