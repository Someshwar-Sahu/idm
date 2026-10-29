# Security, Antivirus & Credential Storage (`src/security`)

This module protects the system from malware threats and manages encrypted authentication credentials.

## Components

| File | Purpose |
| :--- | :--- |
| [`AntivirusScanner.h`](AntivirusScanner.h) / [`.cpp`](AntivirusScanner.cpp) | Multi-layered antivirus scanner: <br>• Real-time in-memory buffer inspection via Windows AMSI (Antimalware Scan Interface)<br>• Staging file scans using Microsoft Defender CLI (`MpCmdRun.exe`)<br>• Zone.Identifier Mark-of-the-Web (ZoneId=3) Alternate Data Stream (ADS) tagging<br>• Safe quarantine folder isolation with threat metadata sidecars<br>• Persistent SHA-256 hash and domain whitelisting (`security_whitelist.json`) |
| [`Vault.h`](Vault.h) / [`.cpp`](Vault.cpp) | AES-256 encrypted credential store. Secures site login credentials (HTTP Basic, Digest, Bearer) under a master password with PBKDF2 / SHA-256 key derivation. |
