export function Docs() {
  return (
    <section className="mx-auto max-w-3xl px-5 pb-24 pt-28 sm:px-8">
      <h1 className="font-display text-3xl font-bold tracking-tight">Dump format</h1>
      <p className="mt-3 text-muted">
        Icky always emits <code className="text-accent">icky.dump.json</code> (
        <code className="text-accent">schema: icky.dump/v1</code>) next to your C++ SDK headers.
      </p>

      <div className="card-surface mt-8 space-y-4 p-6 text-sm leading-relaxed text-muted">
        <h2 className="font-display text-lg font-semibold text-text">Required fields</h2>
        <ul className="list-disc space-y-1 pl-5">
          <li>
            <code>game.slug</code>, <code>game.name</code>, <code>game.executable</code>
          </li>
          <li>
            <code>engine.id</code> — unreal | il2cpp | mono | source1 | source2
          </li>
          <li>
            <code>globals[]</code> — name, rva, address, type
          </li>
          <li>
            <code>types[]</code> — kind, name, package, fields, methods
          </li>
          <li>
            <code>stats</code> — counts for the UI cards
          </li>
        </ul>

        <h2 className="pt-4 font-display text-lg font-semibold text-text">From the dumper</h2>
        <pre className="overflow-x-auto rounded-lg bg-side p-4 font-mono text-xs text-accent">
{`icky_sdk_<Game>/
  icky.dump.json          ← share this
  internal/unreal/
    icky.dump.json
    Offsets.hpp
    SDK.hpp
    ...`}
        </pre>

        <h2 className="pt-4 font-display text-lg font-semibold text-text">Single-game catalog</h2>
        <p>
          The public site always has <strong className="text-text">0 or 1</strong> dump. Publishing
          wipes the previous game folder and writes the new one, then deploys.
        </p>

        <h2 className="pt-4 font-display text-lg font-semibold text-text">Easiest publish path</h2>
        <ol className="list-decimal space-y-1 pl-5">
          <li>
            Open <a className="text-accent hover:underline" href="#/upload">Share a dump</a>
          </li>
          <li>Drop the JSON (or whole dump folder)</li>
          <li>
            Click <strong className="text-text">Download &amp; open issue</strong>
          </li>
          <li>Drag the file onto the GitHub issue and submit — bot replaces the live dump</li>
        </ol>

        <h2 className="pt-4 font-display text-lg font-semibold text-text">Full docs</h2>
        <p>
          See <code className="text-text">docs/DUMP_FORMAT.md</code> in the Icky repo for the
          complete schema and catalog layout.
        </p>
      </div>
    </section>
  )
}
