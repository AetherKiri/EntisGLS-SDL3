These tiny synthetic PE overlays contain no executable code or game assets.
The two payloads are `packaged archive` and `中文对白`, compressed with zlib and
encrypted with NewDES. The malformed variants exercise traversal, a directory
cycle, a wrong block CRC, and an excessive declared file size. The catalog and
tree checksums remain valid in those variants so the specific boundary is tested.
