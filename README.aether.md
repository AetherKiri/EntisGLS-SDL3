# Aether integration branch

Aether development takes place on `aether`. The upstream launcher tree is kept
intact, and the host provider and tests live in [aether_runtime](aether_runtime/README.md).
Aether consumes this repository as a public Git submodule pinned to a commit.

The adaptations add embedded presentation and host input, pause/audio lifecycle
handling, validated configuration decoding and bundled-font discovery. Native
regressions cover fixed-point sprite pivots, image hotspot changes, shrinking
image invalidation, camera-driven cached composition, restored font descriptions
and object graph serialization. Saved games use each game's `savedata` directory.

See the provider README for build commands, compatibility scope and retained
license notices. Upstream commit `8235a2afc57cb3284fe76d53998fd1b99e6b3e41`
is the integration baseline; Git history replaces the former copied-file manifest.
