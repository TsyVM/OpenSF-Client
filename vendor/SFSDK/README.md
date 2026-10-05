# SF1SDK — Verified C++23 SDK for Soldier Front 1

> A modern, exception-free C++23 SDK for the Dragonfly FPS *Soldier Front 1*
> (Korean: *Special Force*). Companion to the [Encyclopedia](../Encyclopedia).

**Rule.** Every address and layout in this SDK is `CONFIRMED` from the shipped binary or
data. Unverified values are declared but return `Result::error(NotYetVerified)`. Nothing
is guessed. Every non-generic address is tagged with the target build hash
(`@3c7699` = MD5 `3c7699c35e1e90cb04efcf810546728e`).

## The two pillars

| Pillar | Directory | What it does |
|---|---|---|
| **Data** | [`include/sf1/data/`](include/sf1/data), [`src/data/`](src/data) | Offline file-format readers: `.sff` container, `.mrg` archive, `.cfg` parser, TGA/BMP extractors. No dependency on Windows, no dependency on VanHooks. Works from a byte buffer. |
| **Runtime** | [`include/sf1/runtime/`](include/sf1/runtime) — header-only | In-process helpers: `Image` (rebasing), pattern scan, typed views, class DB lookups, RAII `Patch`. Consumed by the hook layer. |

The two pillars share nothing but the `Result<T>` primitive and the class DB tables
generated from `data/`. Neither depends on the other.

## Three-tier API

```cpp
// Tier 1 — one-liners.
auto unlock = sf1::patch::unlock_framerate();          // RAII, defaults to 60.

// Tier 2 — explicit.
sf1::hook::install_d3d9_wrapper()
    .on_pre_present([](IDirect3DDevice9* dev) { my_overlay.render(dev); });

// Tier 3 — raw.
auto rva = sf1::image::current().find_pattern("55 8B EC 83 EC ?? A1 ?? ?? ?? ??");
auto trampoline = vh::trampoline<recv_fn>(rva, &my_recv);
```

## Generate from data — never hand-typed

Every class table, offset table, and known-address list lives as a CSV under
[`data/`](data). Extractors in [`tools/`](tools) regenerate the CSVs from the binary +
shipped files; a build step transforms them into `.inl` tables that the headers include.
Re-running the extractors re-flows the SDK. There are no hand-typed class facts.

| CSV | Populated by | Consumed by |
|---|---|---|
| `data/classes.csv` | `tools/extract_classes.py` | `include/sf1/runtime/class_db.inl` |
| `data/patterns.csv` | `tools/extract_patterns.py` | `include/sf1/runtime/patterns.inl` |
| `data/opcodes.csv` | `tools/extract_opcodes.py` | `include/sf1/net/opcodes.inl` |
| `data/file_catalogue.csv` | `tools/census.py` | `include/sf1/data/catalogue.inl` |

Empty rows are represented as literal empty rows. The build never silently synthesises a
value.

## Hooking is an adapter, not a hard dep

SF1SDK finds and identifies. [VanHooks](https://github.com/tsyvm/vanhooks) intercepts. The
SDK ships an optional `sf1::hook` adapter that speaks VanHooks; a project that wants to
use MinHook or another backend can substitute its own adapter.

## Verify

- Unit tests (Catch2) for every data-format reader, using shipped samples as fixtures.
- `static_assert` invariants on every generated table (class count, opcode count).
- A diagnostic tool (`tools/verify_catalogue.py`) re-derives the file catalogue and
  reports 0 drift against `data/file_catalogue.csv`.

Zero-parse-failure gate: the extractor must round-trip 1,833 `.sff` files with no errors
before the `.sff` reader's Status advances from 🟡 to ✅ in the Encyclopedia.

## Build

Single CMake, dependencies via FetchContent, consistent with the VanHooks layout.

```
cmake -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release
ctest --test-dir build -C Release
```

## Layout

```
SDK/
  include/sf1/
    data/       ← header-only + free functions (sff, mrg, cfg, tga, bmp)
    runtime/    ← header-only (image, pattern, patch, class_db)
    hook/       ← VanHooks adapter (optional)
    net/        ← packet framing, crypto, opcode table (mostly TODO)
    result.hpp  ← std::expected-based Result<T>
  src/          ← the few things that must have a .cpp (extractors, non-inline data lib)
  data/         ← CSVs (see table above)
  tools/        ← Python extractors + verifiers
  examples/     ← buildable, runnable examples
  tests/        ← Catch2 unit tests + shipped-file fixtures (SFF samples, not redistributed)
  CMakeLists.txt
```

## Scope

Single-player / interoperability research on binaries the user is authorised to analyse.
No ripped Dragonfly assets are shipped with this SDK; the `tests/fixtures/` directory is
`.gitignore`d and populated locally from the reader's own SF Alpha install.
