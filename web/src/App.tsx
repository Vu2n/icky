import { useEffect, useState } from 'react'
import { Nav } from './components/Nav'
import { Footer } from './components/Footer'
import { Home } from './pages/Home'
import { Dumps } from './pages/Dumps'
import { DumpDetail } from './pages/DumpDetail'
import { Upload } from './pages/Upload'
import { Docs } from './pages/Docs'

function parseHash(): { page: string; slug?: string } {
  const raw = window.location.hash.replace(/^#\/?/, '') || ''
  const [path] = raw.split('?')
  const parts = path.split('/').filter(Boolean)
  if (parts[0] === 'dump' && parts[1]) {
    return { page: 'dump', slug: parts[1] }
  }
  if (parts[0] === 'dumps') return { page: 'dumps' }
  if (parts[0] === 'upload') return { page: 'upload' }
  if (parts[0] === 'docs') return { page: 'docs' }
  return { page: 'home' }
}

export default function App() {
  const [route, setRoute] = useState(parseHash)

  useEffect(() => {
    const onHash = () => setRoute(parseHash())
    window.addEventListener('hashchange', onHash)
    return () => window.removeEventListener('hashchange', onHash)
  }, [])

  return (
    <div className="min-h-dvh">
      <Nav />
      <main>
        {route.page === 'home' && <Home />}
        {route.page === 'dumps' && <Dumps />}
        {route.page === 'upload' && <Upload />}
        {route.page === 'docs' && <Docs />}
        {route.page === 'dump' && route.slug && <DumpDetail slug={route.slug} />}
      </main>
      <Footer />
    </div>
  )
}
