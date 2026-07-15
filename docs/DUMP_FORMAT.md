# Icky dump format (`icky.dump/v1`)

Every dump writes **`icky.dump.json`** next to the SDK headers.  
The [Icky Dumps](../web) website and community uploads all speak this schema.

## File name

| Location | Purpose |
|----------|---------|
| `{out}/internal/{engine}/icky.dump.json` | Full dump (mode internal) |
| `{out}/external/{engine}/icky.dump.json` | Full dump (mode external) |
| `web/public/dumps/{slug}/dump.json` | Published copy on the site |
| `web/public/dumps/catalog.json` | Lightweight index of all published dumps |

## Schema

```json
{
  "schema": "icky.dump/v1",
  "generated_at": "2026-07-15T12:00:00Z",
  "tool": { "name": "Icky", "version": "1.0.0" },
  "game": {
    "name": "AVF2",
    "executable": "AVF2-Win64-Shipping.exe",
    "slug": "avf2-win64-shipping"
  },
  "engine": {
    "id": "unreal",
    "label": "Unreal Engine",
    "detail": "ObjObjects=+0x20 ..."
  },
  "module": {
    "name": "AVF2-Win64-Shipping.exe",
    "size": 53702656,
    "base_at_dump": "0x7FF6575D0000"
  },
  "mode": "internal",
  "stats": {
    "types": 4056,
    "classes": 1981,
    "structs": 1326,
    "enums": 749,
    "functions": 6079,
    "globals": 10,
    "packages": 42,
    "encrypted_fields": 79,
    "encrypted_with_decrypt": 79,
    "encrypted_with_algo": 11
  },
  "encryption": {
    "scheme": "il2cpp_encrypted_handle",
    "note": "encrypted=true fields are Facepunch-style wrappers; use field.decrypt …",
    "fields_total": 79,
    "fields_with_decrypt": 79,
    "fields_with_algo": 11
  },
  "globals": [
    {
      "name": "GObjects",
      "rva": "0x2EED188",
      "address": "0x7FF65A4BD188",
      "type": "FUObjectArray*",
      "comment": ""
    }
  ],
  "layout": {
    "uobject_class": 16,
    "uobject_name": 24,
    "uobject_outer": 32
  },
  "types": [
    {
      "kind": "class",
      "name": "Actor",
      "full_name": "Class /Script/Engine.Actor",
      "package": "/Script/Engine",
      "parent": "Object",
      "size": 640,
      "address": "0x...",
      "rva": "0x0",
      "fields": [
        {
          "name": "playerEyes",
          "offset": 896,
          "size": 0,
          "type": "Encrypted<PlayerEyes>",
          "encrypted": true,
          "decrypt": {
            "getter_rva": "0x29D04C0",
            "decrypt_rva": "0x1B28EE0",
            "typeinfo_rva": "0xFC7D500",
            "algo": "ADD_0xbcfc6da8;ROL19;ROL10;XOR_0x73437527;",
            "inner_type": "PlayerEyes",
            "xor": ["0x73437527"],
            "add": ["0xBCFC6DA8"],
            "rol": [19, 10]
          }
        }
      ],
      "methods": [
        { "name": "BeginPlay", "rva": "0x0", "flags": 0 }
      ],
      "enum_members": []
    }
  ],
  "contributor": {
    "name": "optional",
    "note": "optional free text"
  }
}
```

### Engine `id` values

`unreal` · `il2cpp` · `mono` · `source1` · `source2` · `unknown`

### Type `kind` values

`class` · `struct` · `enum` · `interface` · `namespace`

### Addresses

Prefer **hex strings** with `0x` prefix for portability in JS.  
RVAs are always relative to the primary module.

## Catalog entry (`catalog.json`)

```json
{
  "schema": "icky.catalog/v1",
  "updated_at": "2026-07-15T12:00:00Z",
  "dumps": [
    {
      "slug": "avf2-win64-shipping",
      "game": "AVF2",
      "executable": "AVF2-Win64-Shipping.exe",
      "engine": "unreal",
      "engine_label": "Unreal Engine",
      "mode": "internal",
      "generated_at": "2026-07-15T12:00:00Z",
      "stats": { "types": 4056, "classes": 1981, "globals": 10 },
      "path": "dumps/avf2-win64-shipping/dump.json",
      "featured": true,
      "tags": ["ue", "shipping"]
    }
  ]
}
```

## Community upload flow (GitHub Pages)

1. Run Icky → get `icky.dump.json`
2. Open the site → **Upload**
3. Drop the file (client-side schema validation)
4. Preview offsets / types
5. Either:
   - **Save locally** (browser storage for personal library), or
   - **Download pack** + open a PR into `web/public/dumps/{slug}/`

No backend required for validation and personal library. Official catalog is git-backed.
