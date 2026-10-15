// IDM Browser Extension - Manifest V3 Background Service Worker

const NATIVE_HOST_NAME = "com.idm.native_host";
const LOCAL_API_URL = "http://127.0.0.1:8080/api/add_download";

const DEFAULT_FILE_TYPES = [
  "zip", "rar", "7z", "iso", "tar", "gz", "exe", "msi",
  "mp4", "mkv", "webm", "mp3", "flac", "wav", "pdf",
  "dmg", "dat", "bin", "apk", "csv", "tsv", "img"
];

// Helper: Ensure storage settings
function ensureSettings() {
  chrome.storage.local.get(["enabled", "fileTypes"], (data) => {
    const updates = {};
    if (data.enabled === undefined) updates.enabled = true;
    if (!data.fileTypes) updates.fileTypes = DEFAULT_FILE_TYPES.join(",");
    if (Object.keys(updates).length > 0) chrome.storage.local.set(updates);
  });
}

chrome.runtime.onInstalled.addListener(() => {
  ensureSettings();
  chrome.contextMenus.create({
    id: "idm_download_link",
    title: "Download with IDM",
    contexts: ["link", "video", "audio", "image"]
  });
});

chrome.runtime.onStartup.addListener(() => {
  ensureSettings();
});

// Context menu click listener
chrome.contextMenus.onClicked.addListener(async (info, tab) => {
  const targetUrl = info.linkUrl || info.srcUrl;
  if (!targetUrl) return;

  const cookies = await getCookiesForUrl(targetUrl);
  const payload = {
    url: targetUrl,
    filename: "",
    cookies: cookies,
    user_agent: navigator.userAgent,
    referer: tab?.url || "",
    page_title: tab?.title || "Context Menu Download",
    total_size: 0,
    user_force_override: true
  };

  sendToNativeHost(payload);
});

// Intercept browser downloads
chrome.downloads.onCreated.addListener(async (downloadItem) => {
  const { enabled, fileTypes } = await chrome.storage.local.get(["enabled", "fileTypes"]);
  if (enabled === false) return;

  const url = downloadItem.finalUrl || downloadItem.url;
  if (!url || url.startsWith("blob:") || url.startsWith("data:")) return;

  // 1. Extension filter check (always include core defaults)
  const userTypes = fileTypes ? fileTypes.split(",").map(s => s.trim().toLowerCase()).filter(Boolean) : [];
  const allowed = Array.from(new Set([...DEFAULT_FILE_TYPES, ...userTypes]));

  const extMatch = url.match(/\.([a-zA-Z0-9]+)(?:[?#]|$)/);
  const ext = extMatch ? extMatch[1].toLowerCase() : "";
  if (ext && !allowed.includes(ext) && !allowed.includes("*")) {
    console.log("[IDM Extension] Extension not matching IDM whitelist, bypassing to browser:", ext);
    return;
  }

  // 2. Task 8B: Small File Bypass (10 MB = 10,485,760 bytes)
  const TEN_MB_BYTES = 10 * 1024 * 1024;
  if (downloadItem.fileSize > 0 && downloadItem.fileSize < TEN_MB_BYTES) {
    console.log(`[IDM Extension] Small file bypass (${downloadItem.fileSize} bytes < 10 MB). Letting browser download natively.`);
    return;
  }

  console.log("[IDM Extension] Intercepting file (> 10MB or unknown size):", url);

  // 3. Cancel and erase browser download
  chrome.downloads.cancel(downloadItem.id, () => {
    chrome.downloads.erase({ id: downloadItem.id });
  });

  const cookies = await getCookiesForUrl(url);
  const payload = {
    url: url,
    filename: downloadItem.filename || "",
    cookies: cookies,
    user_agent: navigator.userAgent,
    referer: downloadItem.referrer || "",
    page_title: downloadItem.filename || "Browser Download",
    total_size: downloadItem.fileSize > 0 ? downloadItem.fileSize : 0,
    user_force_override: true
  };

  sendToNativeHost(payload);
});

// Helper: Query domain cookies
async function getCookiesForUrl(url) {
  try {
    const cookies = await chrome.cookies.getAll({ url: url });
    return cookies.map(c => `${c.name}=${c.value}`).join("; ");
  } catch (err) {
    console.warn("[IDM] Cookie query warning:", err);
    return "";
  }
}

// Helper: Send payload to Native Messaging Host, with HTTP fallback
function sendToNativeHost(payload) {
  try {
    chrome.runtime.sendNativeMessage(NATIVE_HOST_NAME, payload, (response) => {
      if (chrome.runtime.lastError || !response || !response.success) {
        console.warn("[IDM] Native messaging fallback to HTTP REST API...", chrome.runtime.lastError);
        sendViaHttpFallback(payload);
      } else {
        console.log("[IDM] Successfully dispatched to IDM native host:", response);
      }
    });
  } catch (e) {
    sendViaHttpFallback(payload);
  }
}

// Fallback: Direct LocalServer REST API call
async function sendViaHttpFallback(payload) {
  try {
    await fetch(LOCAL_API_URL, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload)
    });
    console.log("[IDM] Dispatched via local HTTP fallback API");
  } catch (err) {
    console.error("[IDM] Failed to connect to IDM engine:", err);
  }
}
