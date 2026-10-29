# Chromium Native Messaging Host (`src/native_host`)

This module builds the standalone native bridge executable (`NativeHost.exe`) spawned by Google Chrome and Microsoft Edge.

## Components

| File | Purpose |
| :--- | :--- |
| [`NativeHost.h`](NativeHost.h) / [`.cpp`](NativeHost.cpp) | Implements the Chrome Native Messaging protocol: 32-bit little-endian length prefix followed by UTF-8 encoded JSON. Relays download capture events from the browser extension directly to the local IDM IPC server via loopback sockets. |
| [`main_native_host.cpp`](main_native_host.cpp) | Main binary entry point for `NativeHost.exe`. Operates headless via standard I/O (stdin/stdout). |
