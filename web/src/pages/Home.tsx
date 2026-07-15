import { motion } from 'framer-motion'
import { ArrowRight, Database, Upload, Shield, Sparkles } from 'lucide-react'
import { Button } from '../components/ui/Button'
import { fadeUp, staggerContainer } from '../lib/motion'

export function Home() {
  return (
    <>
      <section className="relative overflow-hidden pt-28 pb-16 sm:pt-36 sm:pb-24">
        <div className="pointer-events-none absolute inset-0 ambient-bloom" />
        <div className="pointer-events-none absolute -left-32 top-40 h-72 w-72 rounded-full bg-accent/10 blur-[100px]" />
        <div className="pointer-events-none absolute -right-20 bottom-0 h-80 w-80 rounded-full bg-[#4a5a9a]/15 blur-[120px]" />
        <div
          className="pointer-events-none absolute inset-0 opacity-[0.035]"
          style={{
            backgroundImage:
              'linear-gradient(to right, white 1px, transparent 1px), linear-gradient(to bottom, white 1px, transparent 1px)',
            backgroundSize: '64px 64px',
            maskImage: 'radial-gradient(ellipse 70% 60% at 50% 30%, black, transparent)',
          }}
        />

        <div className="relative mx-auto max-w-6xl px-5 sm:px-8">
          <motion.div
            variants={staggerContainer}
            initial="hidden"
            animate="visible"
            className="max-w-2xl"
          >
            <motion.div
              variants={fadeUp}
              className="mb-6 inline-flex items-center gap-2 rounded-full border border-white/[0.07] bg-card/80 px-3 py-1.5 text-xs text-muted"
            >
              <Sparkles size={13} className="text-accent" />
              Multi-engine · icky.dump/v1
            </motion.div>

            <motion.h1
              variants={fadeUp}
              className="font-display text-[2.6rem] font-bold leading-[1.08] tracking-tight sm:text-5xl md:text-[3.25rem]"
            >
              Fresh offsets.
              <br />
              <span className="gradient-text">Every game dump.</span>
            </motion.h1>

            <motion.p variants={fadeUp} className="mt-6 text-base leading-relaxed text-muted sm:text-lg">
              One live game dump from the Icky dumper. When someone publishes an update, the
              catalog is replaced so offsets stay current — Unreal, IL2CPP, Mono, or Source.
            </motion.p>

            <motion.div variants={fadeUp} className="mt-9 flex flex-wrap gap-3">
              <Button href="#/upload" size="lg">
                Share a dump
                <ArrowRight size={16} />
              </Button>
              <Button href="#/dumps" variant="outline" size="lg">
                Browse catalog
              </Button>
            </motion.div>

            <motion.dl
              variants={fadeUp}
              className="mt-14 grid grid-cols-3 gap-4 border-t border-white/[0.06] pt-8"
            >
              {[
                { k: 'Catalog', v: '1 game · always current' },
                { k: 'Format', v: 'icky.dump/v1' },
                { k: 'Updates', v: 'Replace on publish' },
              ].map((s) => (
                <div key={s.k}>
                  <dt className="text-[11px] uppercase tracking-wider text-dim">{s.k}</dt>
                  <dd className="mt-1 font-display text-sm font-semibold text-text sm:text-base">
                    {s.v}
                  </dd>
                </div>
              ))}
            </motion.dl>
          </motion.div>

          <div className="mt-16 grid gap-4 sm:grid-cols-3">
            {[
              {
                icon: Database,
                title: 'Single live dump',
                body: 'Only one game is published at a time. Re-upload to overwrite offsets after a patch.',
              },
              {
                icon: Upload,
                title: 'Drop → issue → live',
                body: 'Validate in the browser, attach to a GitHub issue. The bot updates the site automatically.',
              },
              {
                icon: Shield,
                title: 'Schema lock',
                body: 'Only icky.dump/v1 is accepted. Bad files fail fast with a clear error.',
              },
            ].map((c) => (
              <div key={c.title} className="card-surface p-6">
                <c.icon className="mb-3 text-accent" size={22} />
                <h3 className="font-display text-lg font-semibold">{c.title}</h3>
                <p className="mt-2 text-sm leading-relaxed text-muted">{c.body}</p>
              </div>
            ))}
          </div>
        </div>
      </section>
    </>
  )
}
