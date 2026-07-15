import { useCallback, useState } from 'react'
import { Upload as UploadIcon, FileJson, CheckCircle2, AlertCircle } from 'lucide-react'
import type { IckyDump } from '../lib/types'
import {
  catalogFromDump,
  downloadJson,
  saveLocalDump,
  validateDump,
} from '../lib/dump'
import { Button } from '../components/ui/Button'

export function Upload() {
  const [drag, setDrag] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const [dump, setDump] = useState<IckyDump | null>(null)
  const [saved, setSaved] = useState(false)

  const onFile = useCallback(async (file: File) => {
    setError(null)
    setSaved(false)
    setDump(null)
    try {
      const text = await file.text()
      const json = JSON.parse(text) as unknown
      const v = validateDump(json)
      if (!v.ok) {
        setError(v.error)
        return
      }
      setDump(v.dump)
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Invalid JSON')
    }
  }, [])

  const onDrop = (e: React.DragEvent) => {
    e.preventDefault()
    setDrag(false)
    const f = e.dataTransfer.files?.[0]
    if (f) void onFile(f)
  }

  const save = () => {
    if (!dump) return
    saveLocalDump(dump)
    setSaved(true)
  }

  const prPack = () => {
    if (!dump) return
    const entry = catalogFromDump(dump)
    // Download dump + suggested catalog snippet
    downloadJson(`${dump.game.slug}.dump.json`, dump)
    downloadJson(`${dump.game.slug}.catalog-entry.json`, entry)
  }

  return (
    <section className="mx-auto max-w-3xl px-5 pb-24 pt-28 sm:px-8">
      <h1 className="font-display text-3xl font-bold tracking-tight">Upload a dump</h1>
      <p className="mt-3 text-muted">
        Drop an <code className="text-accent">icky.dump.json</code> from the Icky dumper. We validate
        the schema in your browser — nothing is sent to a server.
      </p>

      <div
        onDragOver={(e) => {
          e.preventDefault()
          setDrag(true)
        }}
        onDragLeave={() => setDrag(false)}
        onDrop={onDrop}
        className={`mt-10 rounded-2xl border-2 border-dashed p-10 text-center transition-colors ${
          drag
            ? 'border-accent bg-accent/5'
            : 'border-white/[0.08] bg-card/50 hover:border-white/[0.14]'
        }`}
      >
        <UploadIcon className="mx-auto mb-4 text-accent" size={32} />
        <p className="font-display text-lg font-semibold">Drop icky.dump.json here</p>
        <p className="mt-2 text-sm text-muted">or pick a file</p>
        <label className="mt-6 inline-block cursor-pointer">
          <span className="inline-flex items-center gap-2 rounded-[12px] bg-accent px-5 py-2.5 text-sm font-medium text-[#0b0c0f]">
            <FileJson size={16} />
            Choose file
          </span>
          <input
            type="file"
            accept=".json,application/json"
            className="hidden"
            onChange={(e) => {
              const f = e.target.files?.[0]
              if (f) void onFile(f)
            }}
          />
        </label>
      </div>

      {error && (
        <div className="mt-6 flex items-start gap-3 rounded-xl border border-bad/30 bg-bad/10 px-4 py-3 text-sm text-bad">
          <AlertCircle size={18} className="mt-0.5 shrink-0" />
          <div>
            <p className="font-medium">Validation failed</p>
            <p className="mt-1 opacity-90">{error}</p>
          </div>
        </div>
      )}

      {dump && (
        <div className="card-surface mt-8 p-6">
          <div className="mb-4 flex items-center gap-2 text-good">
            <CheckCircle2 size={18} />
            <span className="text-sm font-medium">Valid icky.dump/v1</span>
          </div>
          <h2 className="font-display text-xl font-semibold">{dump.game.name}</h2>
          <p className="font-mono text-xs text-dim">{dump.game.executable}</p>
          <dl className="mt-4 grid grid-cols-2 gap-3 text-sm sm:grid-cols-4">
            <div>
              <dt className="text-dim">Engine</dt>
              <dd className="font-medium">{dump.engine.label}</dd>
            </div>
            <div>
              <dt className="text-dim">Types</dt>
              <dd className="font-medium">{dump.stats.types}</dd>
            </div>
            <div>
              <dt className="text-dim">Globals</dt>
              <dd className="font-medium">{dump.stats.globals}</dd>
            </div>
            <div>
              <dt className="text-dim">Mode</dt>
              <dd className="font-medium">{dump.mode}</dd>
            </div>
          </dl>

          <div className="mt-6 flex flex-wrap gap-3">
            <Button onClick={save}>{saved ? 'Saved to library' : 'Save to my library'}</Button>
            <Button variant="outline" href={`#/dump/${dump.game.slug}?src=local`}>
              Open preview
            </Button>
            <Button variant="outline" onClick={prPack}>
              Download PR pack
            </Button>
          </div>

          <div className="mt-8 rounded-xl border border-white/[0.06] bg-side p-4 text-sm text-muted">
            <p className="font-medium text-text">Publish on GitHub Pages</p>
            <ol className="mt-2 list-decimal space-y-1 pl-5">
              <li>
                Put dump at{' '}
                <code className="text-accent">web/public/dumps/{dump.game.slug}/dump.json</code>
              </li>
              <li>
                Add the catalog entry from the PR pack into{' '}
                <code className="text-accent">web/public/dumps/catalog.json</code>
              </li>
              <li>Open a pull request — Actions will deploy the site</li>
            </ol>
          </div>
        </div>
      )}
    </section>
  )
}
