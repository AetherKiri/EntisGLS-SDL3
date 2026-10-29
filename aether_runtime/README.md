# Aether EntisGLS / Cotopha provider

The `aether` branch of [AetherKiri/EntisGLS-SDL3](https://github.com/AetherKiri/EntisGLS-SDL3/tree/aether)
contains Aether's public provider, engine adaptations and synthetic regression
tests. Aether pins this repository as `packages/EntisGLS-SDL3`; AetherInternal is
not required. The upstream launcher and its history are retained.

## Integration

`AetherEntisGLSProvider()` implements `engine_runtime_provider_v1_t` with runtime
ID `entisgls`. Detection reads explicit XML/EXE configurations, `entis-launcher.xml`,
`cotopha.xml`, PE `IDR_COTOMI` resources, or validated NOA/CSX fallback scripts.
Ambiguous configurations require an explicit selection. Windows executables
supply resources only; they are not executed.

The provider embeds the traditional Cotopha interpreter, NOA storage, SDK image
and audio codecs, text and fonts, sprites, saved object graphs, and motion/PSB
support. It executes scripts on a worker and presents RGBA frames from an
embedded SDL/OpenGL context on the host main thread. It restores the host CGL
context on macOS. Hidden SDL symbols and distinct Objective-C class names allow
other runtimes to load their own SDL without collisions.

The host supplies input in game coordinates and handles presentation, cursor,
IME and fullscreen requests. Pause suspends script execution, SDK clocks and
audio while preserving script-owned pauses. Only one session can use SDK globals
at a time. Destruction wakes suspended execution, cancels the interpreter, joins
the worker and releases SDK resources before another session opens.

## Saved games

Saved data is read and written directly under **`<game directory>/savedata`**.
The normalized `storage://game/savedata` URL and legacy `local://savedata` URL
resolve to that same directory. Existing saves are available immediately;
starting a game creates the directory when necessary and preserves its contents.
Save commands update the slots selected by the game, using the runtime's atomic
replacement implementation. A non-writable game directory reports an error.

The host's writable-data directory does not relocate saves. Runtime caches stay
under `<host cache>/entisgls/<game ID>`; changing the cache does not move or hide
saved games. Earlier experimental builds used a host-data save tree. Those files
are not automatically copied over existing game saves.

## Build and tests

Inside Aether, the usual desktop build enables this provider independently of
`AETHERKIRI_ENABLE_INTERNAL`. Set `AETHERKIRI_ENABLE_ENTISGLS=OFF` to exclude it,
or set `AETHERKIRI_ENTISGLS_DIR` to another checkout while developing. The desktop
packaging manifest includes the provider's shared library.

For a standalone build of this public package:

```sh
cmake -S aether_runtime -B out/aether -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DAETHER_ENTISGLS_HOST_ROOT=/path/to/Aether
cmake --build out/aether --parallel 3
ctest --test-dir out/aether --output-on-failure --no-tests=error
```

The host path supplies only the public ABI headers in `abi/include`. Dependencies
are CMake, a C++17 compiler, Python 3, Bison 3.8.2+, FreeType, nlohmann-json and
Oniguruma, plus the platform's OpenGL/SDL development dependencies. Oniguruma
is located through its CMake package (vcpkg) or pkg-config (Homebrew/Linux).
With Homebrew, use `-DCMAKE_PREFIX_PATH="$(brew --prefix)"` and
`-DTJS_BISON="$(brew --prefix bison)/bin/bison"`. Aether's vcpkg toolchain and
installed packages can also be supplied normally.

The 23 CTest groups need no commercial game. They cover provider lifetimes,
discovery and configuration decoding, text/font restoration, input, sprite
hotspots and image replacement, camera invalidation, pause and audio, object
graph serialization, bounded scene records, save directories, atomic save failure
recovery and platform I/O. Game-local storage tests read existing saves, write through both URL
schemes, reopen the SDK, switch games and change cache directories. Tests include
Unicode game paths. The default build includes all registered test executables;
`AETHERKIRI_BUILD_ENTISGLS_TESTS=OFF` excludes them from product-only builds.

`.github/workflows/aether.yml` runs standalone tests on macOS and Linux without
private credentials. Game-specific JSON probes are local development artifacts
and are not part of this integration package.

## Scope

The provider targets traditional Cotopha CSX. It does not claim compatibility
with arbitrary Loquaty programs or every title using Entis codecs. Desktop
integration is enabled; mobile and Web hosts need separate presentation and
lifecycle work. PRIMAL×HEARTS supplied the actual-game regression cases for
progression, save/load, lower-menu hover, START camera animation and save-screen
text replacement. These checks do not establish that every route or Cotopha
title is compatible. Auxiliary windows and full opening/ending movie playback
have not been validated with that game.

## Provenance and licenses

The integration started from upstream commit
`8235a2afc57cb3284fe76d53998fd1b99e6b3e41`; Git history records the adaptations.
Retain the existing SDK and third-party notices, including `EntisGLS/license.txt`,
`assets/compat/licenses`, and `vendor/sdl3/LICENSE.txt`. The SDK has its own terms;
this integration does not relicense it. No commercial scripts, assets or saves
are included.
