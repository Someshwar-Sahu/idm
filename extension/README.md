# Browser Extension (`extension/`)

This directory contains the Chrome & Microsoft Edge browser extension (Manifest V3) for seamless download interception.

## Structure

| File | Purpose |
| :--- | :--- |
| [`manifest.json`](manifest.json) | Manifest V3 extension configuration (permissions: `downloads`, `nativeMessaging`, `contextMenus`, `storage`). |
| [`background.js`](background.js) | Service worker script. Intercepts browser download events (`chrome.downloads.onCreated`), filters against excluded file types and URL patterns, and relays tasks to the desktop app via `NativeHost.exe`. |
| [`content.js`](content.js) | Page content script. Injects the IDM download panel over HTML5 `<video>` / `<audio>` elements for one-click media capture. |
| [`popup.html`](popup.html) / [`popup.js`](popup.js) | Extension toolbar popup UI allowing quick toggling of download interception and manual URL entry. |
| [`com.idm.native_host.json`](com.idm.native_host.json) | Native Messaging Host manifest registering `NativeHost.exe` with Chromium browsers. |
| [`install_native_host.ps1`](install_native_host.ps1) | PowerShell script to register the Native Messaging Host registry keys on Windows. |
| `icon*.png` / `idm.ico` | High-resolution toolbar and extension brand icons. |
