# Tools & Verification Utilities (`src/tools`)

This module provides archive inspection and integrity verification tools.

## Components

| File | Purpose |
| :--- | :--- |
| [`ArchiveVerifier.h`](ArchiveVerifier.h) / [`.cpp`](ArchiveVerifier.cpp) | Pre-extraction integrity checker for compressed archives (`.zip`, `.rar`, `.7z`, `.tar.gz`). Validates archive headers and CRC32/MD5/SHA checksums without fully decompressing to disk. |
