// IDM Browser Extension - Manifest V3 Background Service Worker

const NATIVE_HOST_NAME = "com.idm.native_host";
const LOCAL_API_URL = "http://127.0.0.1:8080/api/add_download";

const DEFAULT_FILE_TYPES = [
  "zip", "rar", "7z", "iso", "tar", "gz", "exe", "msi",
  "mp4", "mkv", "webm", "mp3", "flac", "wav", "pdf",
  "dmg", "dat", "bin", "apk", "csv", "tsv", "img"
];

// Helper: Sync settings from IDM REST server
async function syncSettingsFromIDM() {
  try {
    const res = await fetch("http://127.0.0.1:8080/api/settings", { signal: AbortSignal.timeout(1500) });
    if (res.ok) {
      const data = await res.json();
      if (data && data.status === "SUCCESS") {
        const updates = {};
        if (data.enable_browser_interception !== undefined) {
          updates.enabled = data.enable_browser_interception;
        }
        if (data.file_types) {
          const types = data.file_types.split(/[\s,]+/).map(s => s.trim().toLowerCase()).filter(Boolean);
          updates.fileTypes = types.join(",");
        }
        if (data.min_capture_size_bytes !== undefined) {
          updates.minCaptureSize = data.min_capture_size_bytes;
        }
        if (data.enable_small_file_bypass !== undefined) {
          updates.enableSmallFileBypass = data.enable_small_file_bypass;
        }
        await chrome.storage.local.set(updates);
      }
    }
  } catch (e) {
    // IDM core offline or not started; fallback to stored configuration
  }
}

// Helper: Ensure storage settings
function ensureSettings() {
  chrome.storage.local.get(["enabled", "fileTypes", "minCaptureSize", "enableSmallFileBypass"], (data) => {
    const updates = {};
    if (data.enabled === undefined) updates.enabled = true;
    if (!data.fileTypes) updates.fileTypes = DEFAULT_FILE_TYPES.join(",");
    if (data.minCaptureSize === undefined) updates.minCaptureSize = 10 * 1024 * 1024;
    if (data.enableSmallFileBypass === undefined) updates.enableSmallFileBypass = true;
    if (Object.keys(updates).length > 0) chrome.storage.local.set(updates);
  });
  syncSettingsFromIDM();
}

chrome.runtime.onInstalled.addListener(() => {
  ensureSettings();
  chrome.contextMenus.create({
    id: "idm_download_link",
    title: "Download with IDM",
    contexts: ["link", "video", "audio", "image"]
  });
  chrome.contextMenus.create({
    id: "idm_download_all_links",
    title: "Download all links with IDM",
    contexts: ["page"]
  });
});

chrome.runtime.onStartup.addListener(() => {
  ensureSettings();
});

if (chrome.tabs && chrome.tabs.onActivated) {
  chrome.tabs.onActivated.addListener(() => {
    syncSettingsFromIDM();
  });
}

// Helper: Identify current browser
function getBrowserTag() {
  const ua = navigator.userAgent;
  if (ua.includes("Edg/") || ua.includes("Edge/")) return "edge";
  if (navigator.brave && typeof navigator.brave.isBrave === "function") return "brave";
  if (ua.includes("Firefox/")) return "firefox";
  if (ua.includes("Chrome/")) return "chrome";
  return "unknown";
}

// In-page link click listener from content.js
chrome.runtime.onMessage.addListener((msg, sender, sendResponse) => {
  if (msg.action === "INTERCEPT_URL" && msg.url) {
    (async () => {
      try {
        console.log("[IDM Extension] In-page link caught:", msg.url);
        const cookies = await getCookiesForUrl(msg.url);
        const payload = {
          url: msg.url,
          filename: "",
          cookies: cookies,
          user_agent: navigator.userAgent,
          browser: getBrowserTag(),
          referer: msg.referer || sender?.tab?.url || "",
          page_title: msg.page_title || sender?.tab?.title || "Page Download",
          total_size: 0,
          user_force_override: true
        };
        sendToNativeHost(payload);
        sendResponse({ success: true });
      } catch (err) {
        console.error("[IDM Extension] Error handling in-page intercept:", err);
        sendResponse({ success: false, error: err.message });
      }
    })();
    return true; // Required by MV3 to keep sendResponse valid asynchronously
  }
});

// Context menu click listener
chrome.contextMenus.onClicked.addListener(async (info, tab) => {
  if (info.menuItemId === "idm_download_all_links" && tab?.id) {
    chrome.tabs.sendMessage(tab.id, { action: "GRAB_ALL_LINKS" }, async (resp) => {
      if (resp && resp.links && resp.links.length > 0) {
        for (const link of resp.links) {
          const cookies = await getCookiesForUrl(link);
          sendToNativeHost({
            url: link,
            filename: "",
            cookies: cookies,
            user_agent: navigator.userAgent,
            browser: getBrowserTag(),
            referer: tab.url || "",
            page_title: resp.page_title || "Batch Download",
            total_size: 0,
            user_force_override: true
          });
        }
      }
    });
    return;
  }

  const targetUrl = info.linkUrl || info.srcUrl;
  if (!targetUrl) return;

  const cookies = await getCookiesForUrl(targetUrl);
  const payload = {
    url: targetUrl,
    filename: "",
    cookies: cookies,
    user_agent: navigator.userAgent,
    browser: getBrowserTag(),
    referer: tab?.url || "",
    page_title: tab?.title || "Context Menu Download",
    total_size: 0,
    user_force_override: true
  };

  sendToNativeHost(payload);
});

// Intercept browser downloads deterministically when headers and file sizes are resolved
chrome.downloads.onDeterminingFilename.addListener((downloadItem, suggest) => {
  handleDeterminingFilename(downloadItem, suggest);
  return true; // Required by Chrome to keep suggest callback valid for async execution
});

async function handleDeterminingFilename(downloadItem, suggest) {
  try {
    const { enabled, fileTypes, minCaptureSize, enableSmallFileBypass } = await chrome.storage.local.get([
      "enabled", "fileTypes", "minCaptureSize", "enableSmallFileBypass"
    ]);
    if (enabled === false) {
      suggest();
      return;
    }

    const minSizeLimit = (minCaptureSize !== undefined) ? minCaptureSize : (10 * 1024 * 1024);
    const smallBypassEnabled = (enableSmallFileBypass !== undefined) ? enableSmallFileBypass : true;

    const url = downloadItem.finalUrl || downloadItem.url;
    if (!url || url.startsWith("blob:") || url.startsWith("data:")) {
      suggest();
      return;
    }

    // 1. Resolve file size (inspect downloadItem.totalBytes, URL query parameters, or fast HEAD)
    let fileSize = (downloadItem.totalBytes && downloadItem.totalBytes > 0) ? downloadItem.totalBytes : 0;

    // Check if URL specifies bytes parameter e.g. __down?bytes=1048576 or /bytes/1048576
    const bytesParamMatch = url.match(/[?&]bytes=(\d+)/i) || url.match(/\/bytes\/(\d+)/i);
    if (bytesParamMatch) {
      fileSize = parseInt(bytesParamMatch[1], 10);
    }

    // If size is still unknown (<= 0), perform a fast HEAD request with a 2-second timeout
    if (fileSize <= 0) {
      try {
        const headRes = await fetch(url, { method: "HEAD", signal: AbortSignal.timeout(2000) });
        const cl = headRes.headers.get("content-length");
        if (cl) {
          const parsedCl = parseInt(cl, 10);
          if (parsedCl > 0) fileSize = parsedCl;
        }
      } catch (e) {
        // Proceed if HEAD fails (e.g. CORS or server does not support HEAD)
      }
    }

    // 2. Small File Bypass: If verified < minSizeLimit, let browser download natively
    if (smallBypassEnabled && fileSize > 0 && fileSize < minSizeLimit) {
      console.log(`[IDM Extension] Small file bypass (${fileSize} bytes < ${minSizeLimit} bytes). Letting browser download natively:`, url);
      suggest();
      return;
    }

    // 3. Extension filter check
    function extractExtension(filename, urlStr) {
      let target = "";
      if (filename) {
        target = filename;
      } else if (urlStr) {
        try {
          const parsed = new URL(urlStr);
          target = parsed.pathname;
        } catch {
          target = urlStr.split("?")[0].split("#")[0];
        }
      }
      target = target.replace(/\/+$/, "");
      const lastSlash = Math.max(target.lastIndexOf("/"), target.lastIndexOf("\\"));
      const basename = lastSlash !== -1 ? target.slice(lastSlash + 1) : target;
      const lastDot = basename.lastIndexOf(".");
      if (lastDot !== -1 && lastDot < basename.length - 1) {
        return basename.slice(lastDot + 1).toLowerCase();
      }
      return "";
    }

    const userTypes = fileTypes ? fileTypes.split(/[\s,]+/).map(s => s.trim().toLowerCase()).filter(Boolean) : [];
    const allowed = userTypes.length > 0 ? userTypes : DEFAULT_FILE_TYPES;

    const ext = extractExtension(downloadItem.filename, url);

    const isWhitelistedExt = ext && (allowed.includes(ext) || allowed.includes("*"));
    const isLargeByteDownload = fileSize >= minSizeLimit;

    // If extension is not whitelisted and size is not a verified large download, bypass
    if (!isWhitelistedExt && !isLargeByteDownload) {
      console.log("[IDM Extension] File not whitelisted and not a large byte download. Letting browser download natively:", url);
      suggest();
      return;
    }

    // 4. Intercept: File is >= 10 MB or whitelisted extension
    console.log(`[IDM Extension] Intercepting file (${fileSize} bytes, ext: ${ext}):`, url);

    const cookies = await getCookiesForUrl(url);
    const payload = {
      url: url,
      filename: downloadItem.filename || "",
      cookies: cookies,
      user_agent: navigator.userAgent,
      browser: getBrowserTag(),
      referer: downloadItem.referrer || "",
      page_title: downloadItem.filename || "Browser Download",
      total_size: fileSize > 0 ? fileSize : 0,
      user_force_override: false
    };

    sendToNativeHost(payload, () => {
      chrome.downloads.cancel(downloadItem.id, () => {
        chrome.downloads.erase({ id: downloadItem.id });
      });
    });
    suggest(); // Release suggest callback cleanly
  } catch (err) {
    console.error("[IDM Extension] Error handling determining filename:", err);
    suggest();
  }
}

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
function sendToNativeHost(payload, onSuccess) {
  try {
    chrome.runtime.sendNativeMessage(NATIVE_HOST_NAME, payload, (response) => {
      if (chrome.runtime.lastError || !response || !response.success) {
        if (response && (response.message === "IGNORED" || (response.message && response.message.includes("IGNORED")))) {
          console.log("[IDM Extension] Interception ignored by IDM per browser settings. Letting browser download natively.");
          return;
        }
        console.warn("[IDM] Native messaging fallback to HTTP REST API...", chrome.runtime.lastError);
        sendViaHttpFallback(payload, onSuccess);
      } else {
        console.log("[IDM] Successfully dispatched to IDM native host:", response);
        if (onSuccess) onSuccess();
      }
    });
  } catch (e) {
    sendViaHttpFallback(payload, onSuccess);
  }
}

// Fallback: Direct LocalServer REST API call
async function sendViaHttpFallback(payload, onSuccess) {
  try {
    const res = await fetch(LOCAL_API_URL, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload)
    });
    if (res.ok) {
      const data = await res.json().catch(() => null);
      if (data && (data.status === "IGNORED" || data.success === false)) {
        console.log("[IDM Extension] Interception ignored by IDM per settings:", data.message);
        return;
      }
      console.log("[IDM] Dispatched via local HTTP fallback API");
      if (onSuccess) onSuccess();
    } else {
      console.error("[IDM] IDM engine returned non-OK status:", res.status);
    }
  } catch (err) {
    console.error("[IDM] Failed to connect to IDM engine:", err);
  }
}
