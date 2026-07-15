# Icky Dumps (GitHub Pages)

Public catalog of game offsets / SDKs produced by the **Icky** multi-engine dumper.

Same visual language as the main Icky marketing site (dark frame, accent `#7b8cd2`, Syne + DM Sans).

## Dev

```bash
cd web
npm install
npm run dev
```

## Build

```bash
npm run build
# dist/ → GitHub Pages
```

For project pages set base path:

```bash
VITE_BASE=/your-repo-name/ npm run build
```

## Adding an official dump

1. Run Icky → copy `icky.dump.json`
2. Save as `public/dumps/{slug}/dump.json`
3. Append an entry to `public/dumps/catalog.json` (or use **Upload → Download PR pack**)
4. Commit & push — workflow deploys automatically

## Community uploads

The **Upload** page validates `icky.dump/v1` client-side and can:

- Save dumps to **localStorage** (personal library)
- Download a **PR pack** (dump + catalog entry) for maintainers

No backend is required for GitHub Pages.

## Schema

See `../docs/DUMP_FORMAT.md`.

<!-- deploy 2026-07-15T16:01:09.3729725+01:00 -->

