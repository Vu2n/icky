# Icky

**Icky** is an injectable multi-engine **SDK dumper**. Inject `icky.dll` into a game, pick **Internal** or **External** in the console, and Icky does the rest: engine detection, string recovery, and header generation.

Every dump also writes **`icky.dump.json`** (`icky.dump/v1`) for the [Icky Dumps](./web) GitHub Pages site — browse offsets, search types, and accept community uploads.

See [docs/DUMP_FORMAT.md](./docs/DUMP_FORMAT.md) and [web/README.md](./web/README.md).

## Supported engines

| Engine | Detection | What you get |
|--------|-----------|--------------|
| **IL2CPP** (Unity) | `GameAssembly.dll` + exports | Types from `global-metadata.dat` (auto-decrypt) or live domain walk; string samples |
| **Mono** (Unity) | `mono-2.0-*.dll` | Image/class walk via mono exports; string decrypt helpers |
| **Unreal** | GObjects patterns / shipping EXE | Classes, structs, FName pool, property offsets |
| **Source 1** | `client.dll` + `engine.dll` (CS:GO) | `CreateInterface`, ClientClass / netvar dump |
| **Source 2** | `engine2.dll` / `schemasystem.dll` (CS2, Deadlock) | Interfaces, schema markers, module RVAs |

## Internal vs External

On inject, the console asks:

1. **Internal** — same-process SDK  
   - Absolute addresses **and** RVAs  
   - `IckyRebase()` helper for ASLR  
   - Meant for injected cheats / tools  

2. **External** — out-of-process SDK  
   - **RVA-only** offsets under `Icky::External::Offsets`  
   - RPM-friendly (`moduleBase + rva`)  
   - Meant for external overlays / readers  

3. **Both** — writes `internal/` and `external/` trees  

Output lands next to the DLL: `icky_sdk_<GameName>/`.

## Build

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Artifacts:

- `build/Release/icky.dll` — inject this  
- `build/Release/icky_loader.exe` — optional PID injector for testing  

## Usage

1. Start the game.  
2. Inject `icky.dll` (your injector, or `icky_loader.exe <pid>`).  
3. Console opens → confirm auto-detect (or override) → choose Internal / External / Both.  
4. Open the printed output folder and include `SDK.hpp`.

### Programmatic API

```c
#include <icky/icky.h>

icky_engine e = icky_detect_engine();
icky_run(e, ICKY_SDK_EXTERNAL, "C:\\sdk_out");
// or interactive:
icky_run_interactive();
```

## Encrypted strings

**IL2CPP**

- Detects bad `global-metadata.dat` sanity (`≠ 0xFAB11BAF`)
- Tries full-file XOR, rolling XOR, header-only XOR
- Samples printable strings after decrypt → `strings_decrypted.txt`
- Resolves `il2cpp_string_new` / `il2cpp_string_chars` when present

**Mono**

- Uses `mono_string_to_utf8` when exported
- Falls back to `MonoString` layout + image XOR sampling

Custom protectors may need a game-specific key; extend `string_decrypt.cpp` for those schemes.

## Layout

```
include/icky/          Public C API
src/dllmain.cpp        Inject entry + console thread
src/console/           Interactive menu
src/core/              Memory, modules, patterns, PE, FS
src/engine/            Registry + auto-detect
src/engines/unreal/
src/engines/il2cpp/
src/engines/mono/
src/engines/source/
src/generate/          Internal + external writers
src/model/             Shared SDK IR
```

## Notes

- Run as the same integrity level as the game (often admin for VAC-protected titles is still external-only — inject at your own risk).  
- Pattern/offset databases are best-effort; shipping titles change layouts.  
- For research / modding of software you own. Respect game ToS and local law.

## License

Use and modify freely for your own projects.
