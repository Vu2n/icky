import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import tailwindcss from '@tailwindcss/vite'

// GitHub Pages project sites need base '/repo-name/'.
// Override with: VITE_BASE=/icky-dumps/ npm run build
export default defineConfig({
  plugins: [react(), tailwindcss()],
  base: process.env.VITE_BASE || './',
})
