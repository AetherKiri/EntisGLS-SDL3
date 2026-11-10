# Game file streams

The SDL game opener uses the same read-only stream policy on every platform.
Native paths are opened by the SDK's native opener; Android document-tree paths
are opened through the selected SAF provider. The resulting seekable, read-only
stream is wrapped once. Its `Duplicate()` objects share that opened stream and
each retain their own 64-bit logical read position.

Reads lock the shared stream, seek to the caller's position, read, and advance
only that caller's position. The lock covers file access, not resource decoding
or rendering. This synchronized implementation works with the SDK stream
interface across platforms and does not rely on `dup()` providing independent
OS file offsets. The last owner closes the underlying stream.

This removes repeated provider opens when the archive reader creates separate
streams for hundreds of resources inside one `.noa` archive. It does not cache
entire archives in memory or keep a global file cache. A new open still resolves
the current file; duplicates retain the identity of the originally opened file,
even if its pathname is subsequently replaced. Writable streams and streams
that cannot seek retain their original handling. Saves continue to use
`$(CURRENT)\savedata`.

## Executable-packaged resources

An explicitly selected EXE may contain an older Molebox VFS overlay instead of
placing its patch archive on disk. Discovery reads the `XOJUMANJ` / `STELPACK`
NewDES revision as bounded data: catalog and directory CRC32, per-block CRC16,
zlib lengths, relative UTF-16 names, and directory cycles are validated. Windows
loader code and embedded DLLs are never executed. Hash-only filenames and
external mounts are not synthesized.

The decoded files form a session-owned read-only layer above the game directory.
Archive reads and enumeration see the packaged version; writes still target the
existing storage backend. SDK opens receive independent seekable temporary
streams that close and unlink automatically. No patch is extracted over the
user's original files, and savedata entries are excluded from the overlay.
`entisgls-packaged-resources` tests valid and malformed asset-free packages,
overlay precedence, nested enumeration, teardown, and persistent save writes.

## Diagnostics and CI

`--trace-file-io` enables cumulative native duplicate/read/seek/length counters.
The runtime logs snapshots around skin loading rather than logging every read.
Android debug builds also accept the `trace_file_io` boolean launch extra and
record provider queries, descriptor opens and lock wait times. Both native and
Java timings are disabled during normal launches; Android release builds ignore
the diagnostic extra. Counter maxima are cumulative peaks, not interval deltas.

The asset-free `sdl_system_test`, run by the macOS jobs in **Build and release**,
checks native and injected document-tree routes. Its shared-stream checks cover
independent cursors, concurrent reads, lifetime, file replacement and provider
open counts. Android's fake-provider checks run in its build job. These checks
do not measure game startup time on a phone; compare phase timings on the same
device and resources for that measurement.
