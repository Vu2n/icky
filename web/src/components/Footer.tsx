export function Footer() {
  const year = new Date().getFullYear()
  return (
    <footer className="border-t border-white/[0.05] bg-side">
      <div className="mx-auto flex max-w-6xl flex-col gap-6 px-5 py-12 sm:px-8 md:flex-row md:items-center md:justify-between">
        <div className="flex items-center gap-2.5">
          <svg width="24" height="24" viewBox="0 0 28 28" aria-hidden>
            <circle cx="14" cy="14" r="10" fill="none" stroke="#7b8cd2" strokeWidth="1.5" />
            <circle cx="14" cy="14" r="2.2" fill="#7b8cd2" />
          </svg>
          <div>
            <p className="font-display text-sm font-semibold tracking-[0.14em] text-text">ICKY</p>
            <p className="text-xs text-dim">Offsets that stay fresh.</p>
          </div>
        </div>
        <p className="text-xs text-dim">
          Dump schema <code className="text-muted">icky.dump/v1</code> · © {year}
        </p>
      </div>
    </footer>
  )
}
