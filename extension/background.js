// IDM Browser Extension - Manifest V3 Background Service Worker

const NATIVE_HOST_NAME = "com.idm.native_host";
const LOCAL_API_URL = "http://127.0.0.1:8080/api/add_download";

// Default settings
chrome.runtime.onInstalled.addListener(() => {
  chrome.storage.local.set({
    enabled: true,
    fileTypes: "zip,rar,7z,iso,tar,gz,exe,msi,mp4,mkv,webm,mp3,flac,wav,pdf,dmg",
    minSizeThreshold: 0
  });

  // Setup context menu
  chrome.contextMenus.create({
    id: "idm_download_link",
    title: "Download with IDM",
    contexts: ["link", "video", "audio", "image"]
  });
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
  if (!enabled) return;

  const url = downloadItem.finalUrl || downloadItem.url;
  if (!url || url.startsWith("blob:") || url.startsWith("data:")) return;

  // Extension filter check
  if (fileTypes) {
    const extMatch = url.match(/\.([a-zA-Z0-9]+)(?:[?#]|$)/);
    const ext = extMatch ? extMatch[1].toLowerCase() : "";
    const allowed = fileTypes.split(",").map(s => s.trim().toLowerCase());
    if (ext && !allowed.includes(ext)) {
      return; // Let browser handle non-matching extensions
    }
  }

  // Cancel and erase browser download
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
    user_force_override: false
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
