import { useCallback, useMemo, useState } from 'react'
import {
  Upload as UploadIcon,
  FileJson,
  CheckCircle2,
  AlertCircle,
  FolderOpen,
  Download,
  ArrowRight,
  Paperclip,
  Sparkles,
} from 'lucide-react'
import type { IckyDump } from '../lib/types'
import {
  countEncryption,
  downloadDumpForGitHubIssue,
  downloadDumpForSubmit,
  engineColor,
  estimateDumpSize,
  formatBytes,
  hasEncryption,
  openSubmitIssue,
  pickDumpFromFiles,
  slimDumpForPublish,
} from '../lib/dump'
import { Button } from '../components/ui/Button'

type Step = 'drop' | 'ready'

export function Upload() {
  const [drag, setDrag] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const [dump, setDump] = useState<IckyDump | null>(null)
  const [fileSize, setFileSize] = useState(0)
  const [busy, setBusy] = useState(false)
  const [submitHint, setSubmitHint] = useState(false)

  const step: Step = dump ? 'ready' : 'drop'

  const sizeLabel = useMemo(() => {
    if (!dump) return ''
    const n = fileSize || estimateDumpSize(dump)
    return formatBytes(n)
  }, [dump, fileSize])

  const tooBigForIssue = fileSize > 20 * 1024 * 1024
  const slimSize = useMemo(() => {
    if (!dump) return 0
    try {
      return new Blob([JSON.stringify(slimDumpForPublish(dump))]).size
    } catch {
      return 0
    }
  }, [dump])

  const ingest = useCallback(async (files: FileList | File[]) => {
    setBusy(true)
    setError(null)
    setSubmitHint(false)
    setDump(null)
    try {
      const result = await pickDumpFromFiles(files)
      if (!result.ok) {
        setError(result.error)
        return
      }
      setDump(result.dump)
      setFileSize(result.size)
    } finally {
      setBusy(false)
    }
  }, [])

  const onDrop = (e: React.DragEvent) => {
    e.preventDefault()
    setDrag(false)
    if (e.dataTransfer.files?.length) void ingest(e.dataTransfer.files)
  }

  const [publishBusy, setPublishBusy] = useState(false)
  const [publishNote, setPublishNote] = useState<string | null>(null)

  const publish = async () => {
    if (!dump) return
    setPublishBusy(true)
    setPublishNote(null)
    try {
      // Large dumps: slim (+ gzip) so GitHub issue attach works (~25MB limit)
      if (tooBigForIssue) {
        const r = await downloadDumpForGitHubIssue(dump)
        setPublishNote(
          `Downloaded ${r.filename} (${formatBytes(r.bytes)}) — slim for GitHub. Attach that file.`,
        )
      } else {
        downloadDumpForSubmit(dump)
        setPublishNote('Downloaded icky.dump.json — attach it on the issue.')
      }
      openSubmitIssue(dump)
      setSubmitHint(true)
    } catch (e) {
      setPublishNote(e instanceof Error ? e.message : 'Download failed')
    } finally {
      setPublishBusy(false)
    }
  }

  return (
    <section className="mx-auto max-w-3xl px-5 pb-24 pt-28 sm:px-8">
      <div className="mb-2 inline-flex items-center gap-2 rounded-full border border-white/[0.07] bg-card/80 px-3 py-1 text-xs text-muted">
        <Sparkles size={12} className="text-accent" />
        Takes about 30 seconds
      </div>
      <h1 className="font-display text-3xl font-bold tracking-tight sm:text-4xl">Share a dump</h1>
      <p className="mt-3 max-w-xl text-muted">
        Drop your <code className="text-accent">icky.dump.json</code> (or dump folder). If this game
        is already on the catalog, publishing <strong className="text-text">updates</strong> it —
        no duplicates.
      </p>

      {/* Steps */}
      <ol className="mt-8 grid gap-2 sm:grid-cols-3">
        {[
          { n: '1', t: 'Drop dump', d: 'File or folder' },
          { n: '2', t: 'We validate', d: 'icky.dump/v1' },
          { n: '3', t: 'Publish', d: 'Issue + attach' },
        ].map((s, i) => (
          <li
            key={s.n}
            className={`flex items-center gap-3 rounded-xl border px-3 py-2.5 text-sm ${
              (step === 'drop' && i === 0) || (step === 'ready' && i >= 1)
                ? 'border-accent/30 bg-accent/5'
                : 'border-white/[0.06] bg-card/40'
            }`}
          >
            <span className="flex h-7 w-7 items-center justify-center rounded-lg bg-frame font-display text-xs font-bold text-accent">
              {s.n}
            </span>
            <div>
              <p className="font-medium text-text">{s.t}</p>
              <p className="text-xs text-dim">{s.d}</p>
            </div>
          </li>
        ))}
      </ol>

      {/* Drop zone */}
      <div
        onDragOver={(e) => {
          e.preventDefault()
          setDrag(true)
        }}
        onDragLeave={() => setDrag(false)}
        onDrop={onDrop}
        className={`mt-8 rounded-2xl border-2 border-dashed p-10 text-center transition-colors ${
          drag
            ? 'border-accent bg-accent/5'
            : 'border-white/[0.08] bg-card/50 hover:border-white/[0.14]'
        } ${busy ? 'opacity-60' : ''}`}
      >
        <UploadIcon className="mx-auto mb-4 text-accent" size={32} />
        <p className="font-display text-lg font-semibold">
          {busy ? 'Reading…' : 'Drop icky.dump.json or a dump folder'}
        </p>
        <p className="mt-2 text-sm text-muted">
          Works with the file next to your headers, or the whole{' '}
          <code className="text-dim">icky_sdk_*</code> folder
        </p>
        <div className="mt-6 flex flex-wrap items-center justify-center gap-3">
          <label className="cursor-pointer">
            <span className="inline-flex items-center gap-2 rounded-[12px] bg-accent px-5 py-2.5 text-sm font-medium text-[#0b0c0f] shadow-[0_8px_28px_-8px_rgb(123_140_210_/_0.55)]">
              <FileJson size={16} />
              Choose file
            </span>
            <input
              type="file"
              accept=".json,application/json"
              className="hidden"
              onChange={(e) => {
                if (e.target.files?.length) void ingest(e.target.files)
              }}
            />
          </label>
          <label className="cursor-pointer">
            <span className="inline-flex items-center gap-2 rounded-[12px] border border-white/[0.08] bg-frame/80 px-5 py-2.5 text-sm font-medium text-text hover:border-accent/40">
              <FolderOpen size={16} />
              Choose folder
            </span>
            <input
              type="file"
              multiple
              className="hidden"
              ref={(el) => {
                if (el) {
                  el.setAttribute('webkitdirectory', '')
                  el.setAttribute('directory', '')
                }
              }}
              onChange={(e) => {
                if (e.target.files?.length) void ingest(e.target.files)
              }}
            />
          </label>
        </div>
      </div>

      {error && (
        <div className="mt-6 flex items-start gap-3 rounded-xl border border-bad/30 bg-bad/10 px-4 py-3 text-sm text-bad">
          <AlertCircle size={18} className="mt-0.5 shrink-0" />
          <div>
            <p className="font-medium">Could not read dump</p>
            <p className="mt-1 opacity-90">{error}</p>
          </div>
        </div>
      )}

      {dump && (
        <div className="card-surface mt-8 overflow-hidden">
          <div className="border-b border-white/[0.06] bg-good/5 px-6 py-3">
            <div className="flex items-center gap-2 text-good">
              <CheckCircle2 size={18} />
              <span className="text-sm font-medium">Valid icky.dump/v1 · ready to publish</span>
            </div>
          </div>

          <div className="p-6">
            <div className="flex flex-wrap items-start justify-between gap-4">
              <div>
                <h2 className="font-display text-2xl font-semibold">{dump.game.name}</h2>
                <p className="mt-1 font-mono text-xs text-dim">{dump.game.executable}</p>
                <div className="mt-3 flex flex-wrap gap-2">
                  <span
                    className={`rounded-md border px-2 py-0.5 text-[11px] font-medium ${engineColor(dump.engine.id)}`}
                  >
                    {dump.engine.label}
                  </span>
                  <span className="rounded-md border border-white/10 px-2 py-0.5 text-[11px] text-muted">
                    {dump.mode}
                  </span>
                  <span className="rounded-md border border-white/10 px-2 py-0.5 text-[11px] text-dim">
                    {sizeLabel}
                  </span>
                </div>
              </div>
            </div>

            <dl className="mt-6 grid grid-cols-2 gap-3 text-sm sm:grid-cols-4">
              {(
                [
                  ['Types', dump.stats.types],
                  ['Classes', dump.stats.classes],
                  ['Globals', dump.stats.globals],
                  ['Packages', dump.stats.packages ?? '—'],
                  ...(hasEncryption(dump)
                    ? ([
                        ['Encrypted', countEncryption(dump).encrypted],
                        ['Decrypts', countEncryption(dump).withDecrypt],
                      ] as const)
                    : []),
                ] as const
              ).map(([k, v]) => (
                <div key={k} className="rounded-xl bg-side px-3 py-2">
                  <dt className="text-[10px] uppercase tracking-wider text-dim">{k}</dt>
                  <dd className="font-display text-lg font-semibold">{v}</dd>
                </div>
              ))}
            </dl>
            {hasEncryption(dump) && (
              <p className="mt-3 text-xs text-amber-200/90">
                This dump includes Facepunch-style encrypted fields
                {countEncryption(dump).withDecrypt > 0
                  ? ` with ${countEncryption(dump).withDecrypt} recovered decrypt RVAs/algos`
                  : ''}
                . The site catalog tags it as <code className="text-amber-100">encrypted</code>.
              </p>
            )}

            {/* Primary publish path */}
            <div className="mt-8 rounded-2xl border border-accent/25 bg-accent/5 p-5">
              <div className="flex items-start gap-3">
                <Paperclip className="mt-0.5 shrink-0 text-accent" size={22} />
                <div className="min-w-0 flex-1">
                  <p className="font-display text-lg font-semibold text-text">
                    Publish or update
                  </p>
                  <p className="mt-1 text-sm text-muted">
                    Downloads <code className="text-accent">icky.dump.json</code> and opens a GitHub
                    issue. Attach the file and submit — the bot adds a new game or overwrites the
                    existing dump for the same <code className="text-dim">game.slug</code>.
                  </p>
                  {tooBigForIssue && (
                    <p className="mt-2 text-xs text-amber-300/90">
                      Full dump is <strong>{sizeLabel}</strong> — GitHub issues only accept ~25&nbsp;MB.
                      Publish will download a <strong>slim + gzip</strong> file (methods stripped;
                      fields &amp; decrypts kept
                      {slimSize ? ` · ~${formatBytes(slimSize)} before gzip` : ''}).
                    </p>
                  )}
                  {publishNote && (
                    <p className="mt-2 text-xs text-good">{publishNote}</p>
                  )}
                  <div className="mt-4 flex flex-wrap gap-3">
                    <Button size="lg" disabled={publishBusy} onClick={() => void publish()}>
                      <Paperclip size={16} />
                      {publishBusy
                        ? 'Preparing…'
                        : tooBigForIssue
                          ? 'Download slim for GitHub & open issue'
                          : 'Download & open issue'}
                      <ArrowRight size={16} />
                    </Button>
                  </div>
                </div>
              </div>

              {submitHint && (
                <ol className="mt-5 space-y-2 rounded-xl border border-white/[0.06] bg-bg/60 p-4 text-sm text-muted">
                  <li className="flex gap-2">
                    <span className="font-display font-bold text-accent">1.</span>
                    Attach the file from Downloads (
                    <code className="text-text">icky.dump.json</code> or{' '}
                    <code className="text-text">icky.dump.json.gz</code>) to the issue.
                  </li>
                  <li className="flex gap-2">
                    <span className="font-display font-bold text-accent">2.</span>
                    Submit — the bot validates and publishes to the catalog.
                  </li>
                </ol>
              )}
            </div>

            <div className="mt-4 flex flex-wrap gap-2">
              <Button variant="ghost" size="md" onClick={() => downloadDumpForSubmit(dump)}>
                <Download size={16} />
                Download full JSON
              </Button>
              {tooBigForIssue && (
                <Button
                  variant="ghost"
                  size="md"
                  onClick={() => void downloadDumpForGitHubIssue(dump)}
                >
                  <Download size={16} />
                  Download slim for GitHub only
                </Button>
              )}
            </div>
          </div>
        </div>
      )}

      {!dump && (
        <div className="mt-10 card-surface p-4 text-sm text-muted">
          <p className="font-medium text-text">Where is the file?</p>
          <p className="mt-1">
            After dumping: <code className="text-accent">icky_sdk_&lt;Game&gt;/icky.dump.json</code>
          </p>
        </div>
      )}
    </section>
  )
}
