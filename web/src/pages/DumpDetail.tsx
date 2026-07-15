import { useEffect, useMemo, useState } from 'react'
import { ArrowLeft, Copy, Check, Download, Lock, KeyRound, Package } from 'lucide-react'
import type { DumpField, IckyDump } from '../lib/types'
import {
  countEncryption,
  downloadJson,
  engineColor,
  fetchCatalog,
  fetchDump,
  fieldIsEncrypted,
  formatDate,
  hasEncryption,
} from '../lib/dump'
import { downloadSdkZip } from '../lib/sdk_gen'
import { Button } from '../components/ui/Button'

export function DumpDetail({ slug }: { slug: string }) {
  const [dump, setDump] = useState<IckyDump | null>(null)
  const [err, setErr] = useState<string | null>(null)
  const [tab, setTab] = useState<'globals' | 'types' | 'encrypted' | 'layout'>('globals')
  const [q, setQ] = useState('')
  const [encOnly, setEncOnly] = useState(false)
  const [copied, setCopied] = useState<string | null>(null)
  const [sdkBusy, setSdkBusy] = useState(false)
  const [sdkMsg, setSdkMsg] = useState<string | null>(null)

  useEffect(() => {
    let cancelled = false
    ;(async () => {
      try {
        const cat = await fetchCatalog()
        const entry = cat.dumps.find((d) => d.slug === slug)
        if (!entry) throw new Error('Dump not found in catalog')
        const d = await fetchDump(entry.path)
        if (!cancelled) setDump(d)
      } catch (e) {
        if (!cancelled) setErr(e instanceof Error ? e.message : 'Load failed')
      }
    })()
    return () => {
      cancelled = true
    }
  }, [slug])

  const encStats = useMemo(() => (dump ? countEncryption(dump) : null), [dump])
  const showEncTab = !!(dump && hasEncryption(dump))

  const types = useMemo(() => {
    if (!dump) return []
    const s = q.toLowerCase()
    return dump.types.filter((t) => {
      if (encOnly) {
        const hasEnc = (t.fields || []).some((f) => fieldIsEncrypted(f))
        if (!hasEnc) return false
      }
      if (!s) return true
      return (
        t.name.toLowerCase().includes(s) ||
        (t.package || '').toLowerCase().includes(s) ||
        (t.full_name || '').toLowerCase().includes(s)
      )
    })
  }, [dump, q, encOnly])

  const encryptedRows = useMemo(() => {
    if (!dump) return [] as { type: string; field: DumpField }[]
    const rows: { type: string; field: DumpField }[] = []
    for (const t of dump.types) {
      for (const f of t.fields || []) {
        if (!fieldIsEncrypted(f)) continue
        if (!f.decrypt?.decrypt_rva && !encOnly) {
          // Encrypted tab: show all encrypted; prefer with decrypt first via sort
        }
        rows.push({ type: t.name, field: f })
      }
    }
    rows.sort((a, b) => {
      const ad = a.field.decrypt?.decrypt_rva ? 0 : 1
      const bd = b.field.decrypt?.decrypt_rva ? 0 : 1
      if (ad !== bd) return ad - bd
      return a.type.localeCompare(b.type) || a.field.offset - b.field.offset
    })
    return rows
  }, [dump, encOnly])

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

  const tabs = (
    showEncTab
      ? (['globals', 'types', 'encrypted', 'layout'] as const)
      : (['globals', 'types', 'layout'] as const)
  )

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
            {showEncTab && (
              <span className="inline-flex items-center gap-1 rounded-md border border-amber-500/30 bg-amber-500/10 px-2 py-0.5 text-xs text-amber-200">
                <Lock size={11} />
                Encrypted fields
                {encStats && encStats.withDecrypt > 0
                  ? ` · ${encStats.withDecrypt} decrypts`
                  : ''}
              </span>
            )}
          </div>
          <h1 className="font-display text-3xl font-bold tracking-tight">{dump.game.name}</h1>
          <p className="mt-1 font-mono text-sm text-dim">{dump.game.executable}</p>
          <p className="mt-3 text-sm text-muted">
            {formatDate(dump.generated_at)} · Icky {dump.tool.version}
            {dump.engine.detail ? ` · ${dump.engine.detail}` : ''}
          </p>
        </div>
        <div className="flex flex-col items-stretch gap-2 sm:items-end">
          <div className="flex flex-wrap gap-2">
            <Button
              variant="outline"
              disabled={sdkBusy}
              onClick={async () => {
                setSdkBusy(true)
                setSdkMsg(null)
                try {
                  await downloadSdkZip(dump, (m) => setSdkMsg(m))
                  setSdkMsg('SDK zip downloaded')
                } catch (e) {
                  setSdkMsg(e instanceof Error ? e.message : 'SDK generate failed')
                } finally {
                  setSdkBusy(false)
                }
              }}
            >
              <Package size={16} />
              {sdkBusy ? 'Building SDK…' : 'Download SDK (.hpp)'}
            </Button>
            <Button
              variant="outline"
              onClick={() => downloadJson(`${dump.game.slug}.icky.dump.json`, dump)}
            >
              <Download size={16} />
              Download JSON
            </Button>
          </div>
          {sdkMsg && <p className="text-right text-xs text-muted">{sdkMsg}</p>}
        </div>
      </div>

      <dl className="mt-8 grid grid-cols-2 gap-3 sm:grid-cols-4 lg:grid-cols-6">
        {(
          [
            ['Types', dump.stats.types],
            ['Classes', dump.stats.classes],
            ['Structs', dump.stats.structs],
            ['Enums', dump.stats.enums],
            ['Functions', dump.stats.functions],
            ['Globals', dump.stats.globals],
            ...(showEncTab && encStats
              ? ([
                  ['Encrypted', encStats.encrypted],
                  ['Decrypts', encStats.withDecrypt],
                ] as const)
              : []),
          ] as const
        ).map(([k, v]) => (
          <div key={k as string} className="card-surface px-4 py-3 text-center">
            <dt className="text-[10px] uppercase tracking-wider text-dim">{k}</dt>
            <dd className="font-display text-xl font-semibold">{v as number}</dd>
          </div>
        ))}
      </dl>

      {showEncTab && dump.encryption?.note && (
        <p className="mt-4 rounded-xl border border-amber-500/20 bg-amber-500/5 px-4 py-3 text-sm text-amber-100/90">
          <KeyRound size={14} className="mr-2 inline text-amber-300" />
          {dump.encryption.note}
        </p>
      )}

      <div className="mt-10 flex gap-2 border-b border-white/[0.06] pb-px">
        {tabs.map((t) => (
          <button
            key={t}
            type="button"
            onClick={() => setTab(t)}
            className={`rounded-t-lg px-4 py-2 text-sm capitalize transition-colors ${
              tab === t ? 'bg-card text-text' : 'text-muted hover:text-text'
            }`}
          >
            {t === 'encrypted' ? (
              <span className="inline-flex items-center gap-1.5">
                <Lock size={12} /> Encrypted
              </span>
            ) : (
              t
            )}
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
            <div className="flex flex-col gap-2 border-b border-white/[0.06] p-3 sm:flex-row sm:items-center">
              <input
                value={q}
                onChange={(e) => setQ(e.target.value)}
                placeholder="Filter types / packages…"
                className="w-full flex-1 rounded-lg border border-white/[0.07] bg-bg px-3 py-2 text-sm outline-none focus:border-accent/40"
              />
              {showEncTab && (
                <label className="flex shrink-0 cursor-pointer items-center gap-2 text-xs text-muted">
                  <input
                    type="checkbox"
                    checked={encOnly}
                    onChange={(e) => setEncOnly(e.target.checked)}
                    className="rounded border-white/20"
                  />
                  Encrypted fields only
                </label>
              )}
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
                      {t.parent && <span className="text-xs text-dim">: {t.parent}</span>}
                      {(t.fields || []).some((f) => fieldIsEncrypted(f)) && (
                        <span className="inline-flex items-center gap-0.5 rounded border border-amber-500/25 bg-amber-500/10 px-1.5 py-0.5 text-[10px] text-amber-200">
                          <Lock size={10} /> enc
                        </span>
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
                            <th className="py-1 pr-3">Type</th>
                            <th className="py-1">Decrypt</th>
                          </tr>
                        </thead>
                        <tbody>
                          {t.fields!.slice(0, 100).map((f) => (
                            <tr
                              key={f.name + f.offset}
                              className={fieldIsEncrypted(f) ? 'bg-amber-500/[0.04]' : undefined}
                            >
                              <td className="py-0.5 pr-3 font-mono text-accent">
                                0x{f.offset.toString(16)}
                              </td>
                              <td className="py-0.5 pr-3">
                                {f.name}
                                {fieldIsEncrypted(f) && (
                                  <Lock size={10} className="ml-1 inline text-amber-300" />
                                )}
                              </td>
                              <td className="py-0.5 pr-3 text-muted">{f.type}</td>
                              <td className="py-0.5 font-mono text-[10px] text-dim">
                                {f.decrypt?.decrypt_rva ? (
                                  <button
                                    type="button"
                                    className="text-left text-amber-200/90 hover:text-amber-100"
                                    title={f.decrypt.algo || ''}
                                    onClick={() =>
                                      copy(
                                        f.decrypt!.decrypt_rva!,
                                        `${t.name}.${f.name}.dec`,
                                      )
                                    }
                                  >
                                    {f.decrypt.decrypt_rva}
                                    {f.decrypt.algo ? ` · ${f.decrypt.algo.slice(0, 40)}` : ''}
                                    {copied === `${t.name}.${f.name}.dec` && (
                                      <Check size={10} className="ml-1 inline text-good" />
                                    )}
                                  </button>
                                ) : fieldIsEncrypted(f) ? (
                                  <span className="text-dim">no decrypt matched</span>
                                ) : (
                                  '—'
                                )}
                              </td>
                            </tr>
                          ))}
                        </tbody>
                      </table>
                      {(t.fields?.length ?? 0) > 100 && (
                        <p className="mt-1 text-[11px] text-dim">
                          +{(t.fields!.length - 100).toString()} more fields in JSON
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

        {tab === 'encrypted' && (
          <div className="overflow-x-auto">
            <div className="border-b border-white/[0.06] px-4 py-3 text-sm text-muted">
              Facepunch / IL2CPP encrypted wrappers. Prefer rows with a{' '}
              <span className="text-amber-200">decrypt RVA</span> + algo. Copy RVA for your external
              cheat / SDK.
            </div>
            <table className="w-full min-w-[720px] text-left text-sm">
              <thead className="border-b border-white/[0.06] bg-side/80 text-xs uppercase tracking-wider text-dim">
                <tr>
                  <th className="px-4 py-3 font-medium">Class</th>
                  <th className="px-4 py-3 font-medium">Field</th>
                  <th className="px-4 py-3 font-medium">Offset</th>
                  <th className="px-4 py-3 font-medium">Type</th>
                  <th className="px-4 py-3 font-medium">Decrypt RVA</th>
                  <th className="px-4 py-3 font-medium">Algo</th>
                </tr>
              </thead>
              <tbody>
                {encryptedRows
                  .filter((r) => r.field.decrypt?.decrypt_rva)
                  .slice(0, 400)
                  .map((r) => (
                    <tr
                      key={`${r.type}.${r.field.name}.${r.field.offset}`}
                      className="border-b border-white/[0.04] hover:bg-white/[0.02]"
                    >
                      <td className="px-4 py-2 font-medium">{r.type}</td>
                      <td className="px-4 py-2">{r.field.name}</td>
                      <td className="px-4 py-2 font-mono text-xs text-accent">
                        0x{r.field.offset.toString(16)}
                      </td>
                      <td className="max-w-[200px] truncate px-4 py-2 text-xs text-muted">
                        {r.field.type}
                      </td>
                      <td className="px-4 py-2 font-mono text-xs text-amber-200">
                        <button
                          type="button"
                          className="hover:underline"
                          onClick={() =>
                            copy(r.field.decrypt!.decrypt_rva!, `e.${r.type}.${r.field.name}`)
                          }
                        >
                          {r.field.decrypt!.decrypt_rva}
                          {copied === `e.${r.type}.${r.field.name}` && (
                            <Check size={12} className="ml-1 inline text-good" />
                          )}
                        </button>
                      </td>
                      <td className="max-w-[240px] truncate px-4 py-2 font-mono text-[10px] text-dim">
                        {r.field.decrypt?.algo || '—'}
                      </td>
                    </tr>
                  ))}
              </tbody>
            </table>
            {encryptedRows.filter((r) => r.field.decrypt?.decrypt_rva).length === 0 && (
              <p className="p-6 text-sm text-muted">
                No high-confidence decrypt RVAs in this dump. Wrapper types may still be marked
                encrypted without a recovered getter.
              </p>
            )}
          </div>
        )}

        {tab === 'layout' && (
          <pre className="overflow-x-auto p-4 font-mono text-xs text-muted">
            {JSON.stringify(
              {
                module: dump.module,
                layout: dump.layout,
                engine: dump.engine,
                encryption: dump.encryption,
                stats: dump.stats,
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
