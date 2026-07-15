import type { ReactNode } from 'react'

type Variant = 'primary' | 'ghost' | 'outline'

interface ButtonProps {
  variant?: Variant
  children: ReactNode
  size?: 'md' | 'lg'
  className?: string
  href?: string
  type?: 'button' | 'submit' | 'reset'
  onClick?: () => void
  disabled?: boolean
}

const base =
  'inline-flex items-center justify-center gap-2 rounded-[12px] font-medium transition-all duration-200 focus-visible:outline-none disabled:opacity-50 hover:-translate-y-px active:scale-[0.98]'

const variants: Record<Variant, string> = {
  primary:
    'bg-accent text-[#0b0c0f] hover:bg-accent-hi shadow-[0_0_0_1px_rgb(123_140_210_/_0.3),0_8px_28px_-8px_rgb(123_140_210_/_0.55)]',
  ghost: 'bg-transparent text-muted hover:bg-white/[0.04] hover:text-text',
  outline:
    'bg-frame/80 text-text border border-white/[0.08] hover:border-accent/40 hover:bg-frame',
}

const sizes = {
  md: 'px-5 py-2.5 text-sm',
  lg: 'px-7 py-3.5 text-[15px]',
}

export function Button({
  variant = 'primary',
  size = 'md',
  children,
  className = '',
  href,
  type = 'button',
  onClick,
  disabled,
}: ButtonProps) {
  const cls = `${base} ${variants[variant]} ${sizes[size]} ${className}`

  if (href) {
    return (
      <a href={href} className={cls} onClick={onClick}>
        {children}
      </a>
    )
  }

  return (
    <button type={type} className={cls} onClick={onClick} disabled={disabled}>
      {children}
    </button>
  )
}
