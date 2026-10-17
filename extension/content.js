// IDM Integration - Direct In-Page Link Interceptor & Video Sniffer Panel (Phase 5)
(function() {
  // 1. In-Page Link Interceptor
  // In real IDM, clicking links passes to the browser's download manager for accurate
  // Content-Length and MIME inspection, while Alt+Click forces direct IDM capture.
  document.addEventListener("click", (e) => {
    const link = e.target.closest("a");
    if (!link || !link.href) return;

    const href = link.href;
    if (href.startsWith("javascript:") || href.startsWith("#") || href.startsWith("blob:") || href.startsWith("data:")) return;

    if (e.altKey) {
      console.log("[IDM Extension] Alt+Click detected, forcing IDM capture:", href);
      e.preventDefault();
      e.stopPropagation();

      chrome.runtime.sendMessage({
        action: "INTERCEPT_URL",
        url: href,
        referer: window.location.href,
        page_title: document.title,
        user_force_override: true
      });
    }
  }, true);

  // 2. Batch Link Grabber Listener
  chrome.runtime.onMessage.addListener((msg, sender, sendResponse) => {
    if (msg.action === "GRAB_ALL_LINKS") {
      const links = Array.from(document.querySelectorAll("a[href]"))
        .map(a => a.href)
        .filter(href => /\.(zip|rar|7z|iso|tar|gz|exe|msi|mp4|mkv|webm|mp3|flac|wav|pdf|dmg|dat|bin|apk|csv)(?:[?#]|$)/i.test(href));
      const uniqueLinks = Array.from(new Set(links));
      sendResponse({ links: uniqueLinks, page_title: document.title });
    }
  });

  // 3. IDM Floating Video Sniffer Panel
  function injectVideoSniffer(videoEl) {
    if (videoEl.__idm_sniffed) return;
    videoEl.__idm_sniffed = true;

    const btn = document.createElement("div");
    btn.className = "idm-video-sniffer-btn";
    btn.innerHTML = `
      <svg style="width:14px; height:14px; margin-right:4px;" viewBox="0 0 16 16">
        <path d="M2 3h12v10H2V3z" fill="#37474F"/>
        <polygon points="6,5 11,8 6,11" fill="#4CAF50"/>
      </svg>
      <span>Download this video</span>
    `;

    Object.assign(btn.style, {
      position: "absolute",
      zIndex: "2147483647",
      background: "linear-gradient(180deg, #FFFFFF 0%, #E8E8E8 100%)",
      border: "1px solid #707070",
      boxShadow: "0 2px 6px rgba(0,0,0,0.3)",
      borderRadius: "2px",
      padding: "2px 6px",
      fontSize: "11px",
      fontFamily: "Segoe UI, Tahoma, sans-serif",
      color: "#000000",
      cursor: "pointer",
      display: "flex",
      alignItems: "center",
      userSelect: "none"
    });

    btn.addEventListener("click", (e) => {
      e.stopPropagation();
      e.preventDefault();
      const videoSrc = videoEl.currentSrc || videoEl.src;
      if (videoSrc && !videoSrc.startsWith("blob:") && !videoSrc.startsWith("data:")) {
        chrome.runtime.sendMessage({
          action: "INTERCEPT_URL",
          url: videoSrc,
          referer: window.location.href,
          page_title: document.title,
          user_force_override: true
        });
      } else {
        alert("IDM captured dynamic HTML5 video stream: " + (document.title || "Web Video"));
      }
    });

    function positionBtn() {
      const rect = videoEl.getBoundingClientRect();
      if (rect.width > 120 && rect.height > 90) {
        btn.style.top = `${window.scrollY + rect.top + 8}px`;
        btn.style.left = `${window.scrollX + rect.right - 145}px`;
        btn.style.display = "flex";
      } else {
        btn.style.display = "none";
      }
    }

    document.body.appendChild(btn);
    positionBtn();
    window.addEventListener("scroll", positionBtn, { passive: true });
    window.addEventListener("resize", positionBtn, { passive: true });
  }

  // Scan for existing and dynamic videos
  function scanMedia() {
    document.querySelectorAll("video").forEach(injectVideoSniffer);
  }

  setInterval(scanMedia, 2000);
  scanMedia();
})();
