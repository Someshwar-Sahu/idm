function getBrowserTag() {
  const ua = navigator.userAgent;
  if (ua.includes("Edg/") || ua.includes("Edge/")) return "edge";
  if (navigator.brave && typeof navigator.brave.isBrave === "function") return "brave";
  if (ua.includes("Firefox/")) return "firefox";
  if (ua.includes("Chrome/")) return "chrome";
  return "unknown";
}

document.addEventListener("DOMContentLoaded", async () => {
  const toggleCapture = document.getElementById("toggleCapture");
  const toggleBypass = document.getElementById("toggleBypass");
  const statusBadge = document.getElementById("status");
  const manualUrl = document.getElementById("manualUrl");
  const btnDownload = document.getElementById("btnDownload");
  const btnOpenOptions = document.getElementById("btnOpenOptions");

  // Load local preferences
  const localData = await chrome.storage.local.get(["enabled", "enable_small_file_bypass"]);
  toggleCapture.checked = localData.enabled !== false;
  toggleBypass.checked = localData.enable_small_file_bypass !== false;

  // Query core engine settings for live synchronization
  try {
    const res = await fetch("http://127.0.0.1:8080/api/settings", { signal: AbortSignal.timeout(1500) });
    if (res.ok) {
      const liveSettings = await res.json();
      if (typeof liveSettings.enable_browser_interception === "boolean") {
        toggleCapture.checked = liveSettings.enable_browser_interception;
        chrome.storage.local.set({ enabled: liveSettings.enable_browser_interception });
      }
      if (typeof liveSettings.enable_small_file_bypass === "boolean") {
        toggleBypass.checked = liveSettings.enable_small_file_bypass;
        chrome.storage.local.set({ enable_small_file_bypass: liveSettings.enable_small_file_bypass });
      }
      statusBadge.textContent = "Engine Connected";
      statusBadge.className = "status-badge";
    } else {
      throw new Error();
    }
  } catch (e) {
    statusBadge.textContent = "Engine Offline";
    statusBadge.className = "status-badge offline";
  }

  // Toggle 1: Capture Browser Downloads
  toggleCapture.addEventListener("change", async () => {
    chrome.storage.local.set({ enabled: toggleCapture.checked });
    try {
      await fetch("http://127.0.0.1:8080/api/settings", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ enable_browser_interception: toggleCapture.checked })
      });
    } catch (e) {
      // Core engine offline or sandboxed
    }
  });

  // Toggle 2: Bypass Small Files (<10MB)
  toggleBypass.addEventListener("change", async () => {
    chrome.storage.local.set({ enable_small_file_bypass: toggleBypass.checked });
    try {
      await fetch("http://127.0.0.1:8080/api/settings", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ enable_small_file_bypass: toggleBypass.checked })
      });
    } catch (e) {
      // Core engine offline
    }
  });

  // Manual download catch
  btnDownload.addEventListener("click", async () => {
    const url = manualUrl.value.trim();
    if (!url) return;
    const payload = {
      url: url,
      filename: "",
      cookies: "",
      user_agent: navigator.userAgent,
      browser: getBrowserTag(),
      referer: "",
      page_title: "Manual Catch",
      total_size: 0,
      user_force_override: true
    };

    try {
      const res = await fetch("http://127.0.0.1:8080/api/add_download", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload)
      });
      if (res.ok) {
        manualUrl.value = "";
        btnDownload.textContent = "Added to IDM!";
        setTimeout(() => { btnDownload.textContent = "Catch Link"; }, 2000);
        return;
      }
    } catch (e) {
      // Fallback to native host
    }

    chrome.runtime.sendNativeMessage("com.idm.native_host", payload, (response) => {
      if (chrome.runtime.lastError || (response && response.success === false)) {
        btnDownload.textContent = "Dispatch Failed";
        setTimeout(() => { btnDownload.textContent = "Catch Link"; }, 2500);
        return;
      }
      manualUrl.value = "";
      btnDownload.textContent = "Dispatched!";
      setTimeout(() => { btnDownload.textContent = "Catch Link"; }, 2000);
    });
  });

  // Open IDM App / Options
  btnOpenOptions.addEventListener("click", async () => {
    btnOpenOptions.textContent = "Opening IDM...";
    try {
      const res = await fetch("http://127.0.0.1:8080/api/show", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ action: "show" })
      });
      if (res.ok) {
        setTimeout(() => { btnOpenOptions.textContent = "⚙ Open IDM Options / Window"; }, 1500);
        return;
      }
    } catch (e) {}

    // Fallback: send message to native host
    chrome.runtime.sendNativeMessage("com.idm.native_host", { action: "show" }, () => {
      setTimeout(() => { btnOpenOptions.textContent = "⚙ Open IDM Options / Window"; }, 1500);
    });
  });
});
