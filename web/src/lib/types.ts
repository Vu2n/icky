/** icky.dump/v1 — matches dumper output */

export interface IckyDump {
  schema: 'icky.dump/v1' | string
  generated_at: string
  tool: { name: string; version: string }
  game: {
    name: string
    executable: string
    slug: string
  }
  engine: {
    id: string
    label: string
    detail?: string
  }
  module: {
    name: string
    size: number
    base_at_dump: string
  }
  mode: 'internal' | 'external' | string
  stats: {
    types: number
    classes: number
    structs: number
    enums: number
    functions: number
    globals: number
    packages?: number
  }
  layout?: Record<string, string | number>
  globals: DumpGlobal[]
  types: DumpType[]
  contributor?: { name?: string; note?: string }
}

export interface DumpGlobal {
  name: string
  rva: string
  address: string
  type: string
  comment?: string
}

export interface DumpField {
  name: string
  offset: number
  size: number
  type: string
}

export interface DumpMethod {
  name: string
  rva: string
  address?: string
  flags?: number
}

export interface DumpEnumMember {
  name: string
  value: number
}

export interface DumpType {
  kind: string
  name: string
  full_name?: string
  package?: string
  parent?: string
  size?: number
  address?: string
  rva?: string
  fields?: DumpField[]
  methods?: DumpMethod[]
  enum_members?: DumpEnumMember[]
}

export interface CatalogEntry {
  slug: string
  game: string
  executable: string
  engine: string
  engine_label: string
  mode: string
  generated_at: string
  stats: {
    types: number
    classes?: number
    globals: number
    packages?: number
  }
  path: string
  featured?: boolean
  tags?: string[]
}

export interface Catalog {
  schema: string
  updated_at: string | null
  /** One entry per game.slug; re-uploads replace that entry */
  dumps: CatalogEntry[]
}
