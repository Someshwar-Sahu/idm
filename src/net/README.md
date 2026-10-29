# Advanced Networking & Hardware Monitoring (`src/net`)

This module provides multi-homing channel bonding, dynamic latency-aware QoS throttling, system power monitoring, and remote web control.

## Components

| File | Purpose |
| :--- | :--- |
| [`ChannelBonding.h`](ChannelBonding.h) / [`.cpp`](ChannelBonding.cpp) | Network interface aggregation. Discovers local adapters (Wi-Fi, Ethernet, LTE), balances connections across interfaces, and assigns specific network sockets to individual chunk workers. |
| [`NetworkMonitor.h`](NetworkMonitor.h) / [`.cpp`](NetworkMonitor.cpp) | Real-time latency, ping, and packet loss monitor. Implements **Smart Auto-QoS**: automatically throttles download speed when interactive ping increases to avoid lagging games or video calls. |
| [`PowerMonitor.h`](PowerMonitor.h) / [`.cpp`](PowerMonitor.cpp) | Hardware power state observer. Detects AC vs. battery power; automatically scales down concurrent worker threads or pauses the queue on low battery. |
| [`RemoteDashboard.h`](RemoteDashboard.h) / [`.cpp`](RemoteDashboard.cpp) | Embedded micro-webserver and HTML5 web dashboard allowing users to view progress and control downloads remotely from any web browser on the local network. |
| [`SwarmDownloader.h`](SwarmDownloader.h) / [`.cpp`](SwarmDownloader.cpp) | Local peer-to-peer chunk sharing across LAN computers, accelerating downloads of shared large files. |
