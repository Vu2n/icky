/**
 * Client-side External SDK generator from icky.dump/v1 JSON.
 * Mirrors the C++ sdk_writer external layout closely enough for cheats / reverse.
 */
import type { DumpField, DumpType, IckyDump } from './types'
import { fieldIsEncrypted } from './dump'
import { buildZip, downloadBlob, type ZipEntry } from './zip'

function sanitizeIdent(name: string): string {
  if (!name) return 'unnamed'
  let o = ''
  for (let i = 0; i < name.length; i++) {
    const c = name[i]
    if (/[a-zA-Z0-9_]/.test(c)) o += c
    else o += '_'
  }
  if (/^\d/.test(o)) o = '_' + o
  const kws = new Set([
    'class',
    'struct',
    'enum',
    'template',
    'operator',
    'new',
    'delete',
    'default',
    'float',
    'int',
    'bool',
    'void',
    'this',
    'true',
    'false',
    'private',
    'public',
    'namespace',
    'using',
  ])
  if (kws.has(o)) o += '_'
  return o || 'unnamed'
}

function hex(n: number | string | undefined): string {
  if (n === undefined || n === null) return '0x0'
  if (typeof n === 'string') {
    if (n.startsWith('0x') || n.startsWith('0X')) return '0x' + n.slice(2).toUpperCase()
    const v = parseInt(n, 10)
    if (!Number.isFinite(v)) return '0x0'
    return '0x' + v.toString(16).toUpperCase()
  }
  return '0x' + (n >>> 0).toString(16).toUpperCase()
}

function parseRva(rva?: string): number {
  if (!rva) return 0
  if (rva.startsWith('0x') || rva.startsWith('0X')) return parseInt(rva.slice(2), 16) || 0
  return parseInt(rva, 10) || 0
}

function mapCppType(t: string): string {
  if (!t) return 'std::uint8_t'
  const L = t.toLowerCase()
  if (L.includes('bool') || L === 'boolean') return 'bool'
  if (L.includes('int16') || L === 'short') return 'std::int16_t'
  if (L.includes('int32') || L === 'int' || L.endsWith('int')) return 'std::int32_t'
  if (L.includes('int64') || L === 'long') return 'std::int64_t'
  if (L.includes('uint32') || L === 'uint') return 'std::uint32_t'
  if (L.includes('uint64') || L === 'ulong') return 'std::uint64_t'
  if (L.includes('single') || L.includes('float')) return 'float'
  if (L.includes('double')) return 'double'
  if (L.includes('byte')) return 'std::uint8_t'
  if (L.startsWith('encrypted<') || t.startsWith('Encrypted<')) return 'void*'
  if (L.includes('string') || L.includes('object') || L.includes('class') || L.includes('ptr'))
    return 'void*'
  if (L.includes('vector3')) return 'Icky::Vector3'
  if (L.includes('vector2')) return 'Icky::Vector2'
  return 'void*'
}

function parentOk(p?: string): boolean {
  if (!p || p === 'None' || p === 'object' || p === 'Object') return false
  if (p.length <= 1) return false
  return /^[A-Za-z_][A-Za-z0-9_]*$/.test(p)
}

function banner(dump: IckyDump): string {
  return `// ============================================================
//  Icky SDK - EXTERNAL (generated on site from icky.dump.json)
//  Engine : ${dump.engine.label}${dump.engine.detail ? ` (${dump.engine.detail})` : ''}
//  Game   : ${dump.game.name}
//  Module : ${dump.module.name}  base=${dump.module.base_at_dump}
//  Generated: ${dump.generated_at}
// ============================================================

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>

`
}

function writeType(t: DumpType): string {
  const lines: string[] = []
  const name = sanitizeIdent(t.name)

  if (t.kind === 'enum') {
    lines.push(`enum class ${name} : int64_t {`)
    for (const e of t.enum_members || []) {
      lines.push(`    ${sanitizeIdent(e.name)} = ${e.value},`)
    }
    lines.push('};\n')
    return lines.join('\n')
  }

  if (t.kind === 'namespace') {
    lines.push(`// namespace/assembly: ${t.name}`)
    return lines.join('\n') + '\n'
  }

  const kw = t.kind === 'struct' ? 'struct' : 'class'
  lines.push(`// ${t.full_name || t.name}${t.package ? `  [${t.package}]` : ''}`)
  if (t.size) lines.push(`// size=0x${t.size.toString(16)}`)

  let decl = `${kw} ${name}`
  if (parentOk(t.parent) && t.parent !== 'Object') {
    decl += ` : public ${sanitizeIdent(t.parent!)}`
  }
  lines.push(decl + ' {')
  lines.push('public:')

  const fields = [...(t.fields || [])].filter((f) => !f.static)
  fields.sort((a, b) => a.offset - b.offset)

  let pad = 0
  let cursor = 0
  let first = true
  for (const f of fields) {
    if (first) {
      cursor = f.offset
      first = false
    } else if (f.offset > cursor && f.offset - cursor < 0x10000) {
      lines.push(
        `    char pad_${pad++}[0x${(f.offset - cursor).toString(16)}];`,
      )
    }
    let ty = mapCppType(f.type)
    if (fieldIsEncrypted(f)) ty = 'void*'
    let line = `    ${ty} ${sanitizeIdent(f.name)}; // 0x${f.offset.toString(16)}`
    if (f.type) line += ` ${f.type}`
    if (fieldIsEncrypted(f)) line += ' [ENCRYPTED]'
    if (f.decrypt?.decrypt_rva) line += ` decrypt_rva=${f.decrypt.decrypt_rva}`
    if (f.decrypt?.algo) line += ` ${f.decrypt.algo}`
    lines.push(line)
    const span = f.size > 0 ? f.size : 8
    cursor = f.offset + span
  }

  const methods = t.methods || []
  if (methods.length) {
    lines.push('')
    lines.push('    // Methods')
    const cap = Math.min(methods.length, 80)
    for (let i = 0; i < cap; i++) {
      const m = methods[i]
      const ret = m.return_type || 'void'
      const params = (m.params || [])
        .map((p) => `${p.type || 'void*'} ${sanitizeIdent(p.name || 'arg')}`)
        .join(', ')
      let ml = `    // ${ret} ${sanitizeIdent(m.name)}(${params})`
      if (m.rva) ml += `  rva ${m.rva}`
      lines.push(ml)
    }
    if (methods.length > cap) {
      lines.push(`    // … +${methods.length - cap} more methods in icky.dump.json`)
    }
  }

  lines.push('};\n')
  return lines.join('\n')
}

function genOffsets(dump: IckyDump): string {
  let s = banner(dump)
  s += `namespace Icky {
    constexpr const char* EngineName = "${dump.engine.label.replace(/"/g, '\\"')}";
    constexpr const char* GameName = "${dump.game.name.replace(/"/g, '\\"')}";
    constexpr const char* ModuleName = "${dump.module.name.replace(/"/g, '\\"')}";
    constexpr std::uintptr_t ModuleSize = ${hex(dump.module.size)}ULL;
}

namespace Icky::External {
    // All values are RVAs — add remote module base
    constexpr const char* ModuleName = "${dump.module.name.replace(/"/g, '\\"')}";
    struct Offsets {
`
  for (const g of dump.globals || []) {
    s += `        static constexpr std::uintptr_t ${sanitizeIdent(g.name)} = ${hex(g.rva)}ULL;\n`
  }
  s += `    };
    inline std::uintptr_t Resolve(std::uintptr_t moduleBase, std::uintptr_t rva) {
        return moduleBase + rva;
    }
}

// External RPM sketch:
// uintptr_t remote = moduleBase + Icky::External::Offsets::SomeGlobal;
// ReadProcessMemory(h, (void*)remote, &buf, sizeof(buf), nullptr);

`
  return s
}

function genBasicTypes(dump: IckyDump): string {
  return (
    banner(dump) +
    `namespace Icky {
struct FName { int32_t ComparisonIndex; int32_t Number; };
struct FString { wchar_t* Data; int32_t Num; int32_t Max; };
template<typename T> struct TArray { T* Data; int32_t Num; int32_t Max; };
struct Vector3 { float x, y, z; };
struct Vector2 { float x, y; };
struct QAngle { float x, y, z; };
struct Il2CppObject { void* klass; void* monitor; };
struct MonoObject { void* vtable; void* sync; };
}
`
  )
}

function rol32Helper(): string {
  return `inline constexpr std::uint32_t rol32(std::uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

`
}

function emitDecryptHandle(f: DumpField, tag: string): string {
  const d = f.decrypt
  if (!d) return ''
  const xors = (d.xor || []).map((x) => parseRva(x)).filter(Boolean)
  const adds = (d.add || []).map((x) => parseRva(x)).filter(Boolean)
  const rols = d.rol || []
  if (!xors.length && !adds.length && !rols.length) return ''

  let body = `  inline std::uint64_t ${tag}_decrypt_handle(std::uint64_t raw) {
    std::uint32_t w[2] = { (std::uint32_t)raw, (std::uint32_t)(raw >> 32) };
    for (int i = 0; i < 2; ++i) {
      std::uint32_t x = w[i];
`
  if (adds.length && rols.length >= 2 && xors.length >= 1) {
    body += `      x += ${hex(adds[0])}u;\n`
    body += `      x = rol32(x, ${rols[0]});\n`
    body += `      x ^= ${hex(xors[0])}u;\n`
    body += `      x = rol32(x, ${rols[1]});\n`
  } else if (xors.length >= 2 && rols.length >= 1) {
    body += `      x ^= ${hex(xors[0])}u;\n`
    body += `      x = rol32(x, ${rols[0]});\n`
    body += `      x ^= ${hex(xors[1])}u;\n`
  } else {
    for (const a of adds) body += `      x += ${hex(a)}u;\n`
    for (const r of rols) body += `      x = rol32(x, ${r});\n`
    for (const x of xors) body += `      x ^= ${hex(x)}u;\n`
  }
  body += `      w[i] = x;
    }
    return ((std::uint64_t)w[1] << 32) | w[0];
  }

`
  return body
}

function genDecrypt(dump: IckyDump): string | null {
  const rows: { type: string; field: DumpField }[] = []
  for (const t of dump.types) {
    for (const f of t.fields || []) {
      if (f.decrypt?.decrypt_rva) rows.push({ type: t.name, field: f })
    }
  }
  if (!rows.length) return null

  let s = banner(dump)
  s += `// High-confidence encrypted field decrypts (from dump JSON)
// Encrypted handle layout: +0x14 has_value, +0x18 uint64 handle

namespace Icky::Decrypt {

${rol32Helper()}`

  const byClass = new Map<string, DumpField[]>()
  for (const r of rows) {
    const list = byClass.get(r.type) || []
    list.push(r.field)
    byClass.set(r.type, list)
  }

  for (const [cls, fields] of byClass) {
    s += `namespace ${sanitizeIdent(cls)} {\n`
    for (const f of fields) {
      const tag = sanitizeIdent(f.name)
      const d = f.decrypt!
      s += `  // ${f.type} @ +0x${f.offset.toString(16)}`
      if (d.algo) s += `  algo=${d.algo}`
      s += `\n`
      s += `  constexpr std::uintptr_t ${tag}_offset = ${hex(f.offset)};\n`
      if (d.getter_rva) s += `  constexpr std::uintptr_t ${tag}_getter_rva = ${hex(d.getter_rva)};\n`
      s += `  constexpr std::uintptr_t ${tag}_decrypt_rva = ${hex(d.decrypt_rva)};\n`
      if (d.typeinfo_rva && d.typeinfo_rva !== '0x0')
        s += `  constexpr std::uintptr_t ${tag}_typeinfo_rva = ${hex(d.typeinfo_rva)};\n`
      s += emitDecryptHandle(f, tag)
    }
    s += `} // ${sanitizeIdent(cls)}\n\n`
  }
  s += `} // namespace Icky::Decrypt\n`
  return s
}

const CHUNK = 400

/** Generate external SDK files from a published dump */
export function generateSdkFiles(dump: IckyDump): ZipEntry[] {
  const engine = dump.engine.id || 'unknown'
  const prefix = `external/${engine}/`
  const entries: ZipEntry[] = []

  entries.push({ path: `${prefix}Offsets.hpp`, data: genOffsets(dump) })
  entries.push({ path: `${prefix}BasicTypes.hpp`, data: genBasicTypes(dump) })

  const dec = genDecrypt(dump)
  if (dec) entries.push({ path: `${prefix}Decrypt.hpp`, data: dec })

  // Group by package
  const groups = new Map<string, DumpType[]>()
  for (const t of dump.types) {
    const key = t.package?.trim() || 'Global'
    const list = groups.get(key) || []
    list.push(t)
    groups.set(key, list)
  }

  const includes: string[] = ['Offsets.hpp', 'BasicTypes.hpp']
  if (dec) includes.push('Decrypt.hpp')

  let fileIdx = 0
  const sortedKeys = [...groups.keys()].sort((a, b) => a.localeCompare(b))
  for (const ns of sortedKeys) {
    const list = groups.get(ns)!
    let offset = 0
    let part = 0
    while (offset < list.length) {
      const end = Math.min(offset + CHUNK, list.length)
      let body = banner(dump)
      body += `#include "BasicTypes.hpp"\n\n`
      body += `// Package / namespace: ${ns}`
      if (list.length > CHUNK) body += ` (part ${part})`
      body += `\n\nnamespace Icky::External::Types {\n\n`
      for (let i = offset; i < end; i++) body += writeType(list[i]) + '\n'
      body += `} // namespace\n`

      let fname = sanitizeIdent(ns) || 'Package'
      if (list.length > CHUNK) fname += `_p${part}`
      fname += `_${fileIdx}.hpp`
      entries.push({ path: `${prefix}${fname}`, data: body })
      includes.push(fname)
      fileIdx++
      part++
      offset = end
    }
  }

  let sdk = banner(dump)
  for (const inc of includes) sdk += `#include "${inc}"\n`
  entries.push({ path: `${prefix}SDK.hpp`, data: sdk })

  const readme = `# Icky External SDK — ${dump.game.name}

Generated **on the website** from published \`icky.dump.json\`.

- Engine: **${dump.engine.label}** ${dump.engine.detail ? `(${dump.engine.detail})` : ''}
- Module: \`${dump.module.name}\`
- Mode: external (RVAs)
- Types: ${dump.stats?.types ?? dump.types.length}
- Globals: ${dump.stats?.globals ?? dump.globals.length}
${dump.stats?.encrypted_with_decrypt ? `- Encrypted decrypts: ${dump.stats.encrypted_with_decrypt}\n` : ''}
## Usage

\`\`\`cpp
#include "SDK.hpp"

// RVA resolve
uintptr_t base = /* remote GameAssembly base */;
uintptr_t addr = base + Icky::External::Offsets::il2cpp_domain_get;
\`\`\`

${dec ? 'See `Decrypt.hpp` for encrypted field offsets + handle decrypt helpers.\n' : ''}
Full dump JSON is the source of truth for methods not fully expanded here.
`
  entries.push({ path: `${prefix}README.md`, data: readme })
  // Include original dump for reference
  entries.push({
    path: `${prefix}icky.dump.json`,
    data: JSON.stringify(dump, null, 2),
  })

  return entries
}

export async function downloadSdkZip(
  dump: IckyDump,
  onProgress?: (msg: string) => void,
): Promise<void> {
  onProgress?.('Generating headers…')
  // Yield so UI can paint
  await new Promise((r) => setTimeout(r, 0))
  const files = generateSdkFiles(dump)
  onProgress?.(`Packing ${files.length} files…`)
  await new Promise((r) => setTimeout(r, 0))
  const zip = buildZip(files)
  const name = `icky_sdk_${dump.game.slug || 'dump'}_external.zip`
  onProgress?.('Download starting…')
  downloadBlob(name, zip)
}
