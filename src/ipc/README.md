# Inter-Process Communication & REST Server (`src/ipc`)

This module provides the local REST API server and inter-process communication bridge for IDM.

## Components

| File | Purpose |
| :--- | :--- |
| [`LocalServer.h`](LocalServer.h) / [`.cpp`](LocalServer.cpp) | Lightweight asynchronous HTTP server bound to `127.0.0.1:8080`. Receives download payloads from browser extensions, CLI tools, and the native messaging host. Provides REST endpoints for task control (`/api/downloads`), settings synchronization (`/api/settings`), channel bonding (`/api/bonding`), and credential lookups (`/api/vault`). |
