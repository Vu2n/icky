import { useEffect, useState } from 'react'
import { Menu, X } from 'lucide-react'
import { Button } from './ui/Button'

const links = [
  { href: '#/', label: 'Home' },
  { href: '#/dumps', label: 'Dumps' },
  { href: '#/upload', label: 'Upload' },
  { href: '#/docs', label: 'Format' },
]

export function Nav() {
  const [scrolled, setScrolled] = useState(false)
  const [open, setOpen] = useState(false)

  useEffect(() => {
    const onScroll = () => setScrolled(window.scrollY > 16)
    onScroll()
    window.addEventListener('scroll', onScroll, { passive: true })
    return () => window.removeEventListener('scroll', onScroll)
  }, [])

  return (
    <header
      className={`fixed inset-x-0 top-0 z-50 transition-all duration-300 ${
        scrolled || open ? 'glass-nav' : 'bg-transparent'
      }`}
    >
      <nav className="mx-auto flex h-16 max-w-6xl items-center justify-between px-5 sm:px-8">
        <a href="#/" className="group flex items-center gap-2.5">
          <svg width="28" height="28" viewBox="0 0 28 28" aria-hidden>
            <circle
              cx="14"
              cy="14"
              r="10"
              fill="none"
              stroke="#7b8cd2"
              strokeWidth="1.5"
              className="opacity-90 transition-opacity group-hover:opacity-100"
            />
            <circle cx="14" cy="14" r="2.2" fill="#7b8cd2" />
          </svg>
          <span className="font-display text-[15px] font-semibold tracking-[0.14em] text-text">
            ICKY
            <span className="ml-1.5 font-normal tracking-normal text-muted">dumps</span>
          </span>
        </a>

        <ul className="hidden items-center gap-1 md:flex">
          {links.map((l) => (
            <li key={l.href}>
              <a
                href={l.href}
                className="rounded-lg px-3 py-2 text-sm text-muted transition-colors hover:text-text"
              >
                {l.label}
              </a>
            </li>
          ))}
        </ul>

        <div className="hidden md:block">
          <Button href="#/upload" size="md">
            Upload dump
          </Button>
        </div>

        <button
          type="button"
          className="flex h-10 w-10 items-center justify-center rounded-lg text-text md:hidden"
          aria-label={open ? 'Close menu' : 'Open menu'}
          onClick={() => setOpen((v) => !v)}
        >
          {open ? <X size={20} /> : <Menu size={20} />}
        </button>
      </nav>

      {open && (
        <div className="border-t border-white/[0.05] bg-card/95 px-5 py-4 backdrop-blur md:hidden">
          <ul className="flex flex-col gap-1">
            {links.map((l) => (
              <li key={l.href}>
                <a
                  href={l.href}
                  className="block rounded-lg px-3 py-2.5 text-sm text-muted hover:bg-white/[0.04] hover:text-text"
                  onClick={() => setOpen(false)}
                >
                  {l.label}
                </a>
              </li>
            ))}
          </ul>
          <Button href="#/upload" className="mt-3 w-full" onClick={() => setOpen(false)}>
            Upload dump
          </Button>
        </div>
      )}
    </header>
  )
}
