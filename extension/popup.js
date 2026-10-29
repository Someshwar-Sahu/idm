function getBrowserTag() {
  const ua = navigator.userAgent;
  if (ua.includes("Edg/") || ua.includes("Edge/")) return "edge";
  if (navigator.brave && typeof navigator.brave.isBrave === "function") return "brave";
  if (ua.includes("Firefox/")) return "firefox";
  if (ua.includes("Chrome/")) return "chrome";
  return "unknown";
}

document.addEventListener("DOMContentLoaded", async () => {
  const toggle = document.getElementById("toggleCapture");
  const statusBadge = document.getElementById("status");
  const manualUrl = document.getElementById("manualUrl");
  const btnDownload = document.getElementById("btnDownload");

  // Load toggle state
  const data = await chrome.storage.local.get("enabled");
  toggle.checked = data.enabled !== false;

  toggle.addEventListener("change", async () => {
    chrome.storage.local.set({ enabled: toggle.checked });
    try {
      await fetch("http://127.0.0.1:8080/api/settings", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ enable_browser_interception: toggle.checked })
      });
    } catch (e) {
      // Core engine offline
    }
  });

  // Check engine connectivity
  try {
    const res = await fetch("http://127.0.0.1:8080/api/tasks", { signal: AbortSignal.timeout(1500) });
    if (res.ok) {
      statusBadge.textContent = "Engine Connected";
      statusBadge.className = "status-badge";
    } else {
      throw new Error();
    }
  } catch (e) {
    statusBadge.textContent = "Engine Offline";
    statusBadge.className = "status-badge offline";
  }

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
});
