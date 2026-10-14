document.addEventListener("DOMContentLoaded", async () => {
  const toggle = document.getElementById("toggleCapture");
  const statusBadge = document.getElementById("status");
  const manualUrl = document.getElementById("manualUrl");
  const btnDownload = document.getElementById("btnDownload");

  // Load toggle state
  const data = await chrome.storage.local.get("enabled");
  toggle.checked = data.enabled !== false;

  toggle.addEventListener("change", () => {
    chrome.storage.local.set({ enabled: toggle.checked });
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
  btnDownload.addEventListener("click", () => {
    const url = manualUrl.value.trim();
    if (!url) return;
    chrome.runtime.sendNativeMessage("com.idm.native_host", {
      url: url,
      filename: "",
      cookies: "",
      user_agent: navigator.userAgent,
      referer: "",
      page_title: "Manual Catch",
      total_size: 0,
      user_force_override: true
    });
    manualUrl.value = "";
  });
});
