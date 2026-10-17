// IDM Integration - Clean In-Page Link Interceptor & Professional Video Sniffer
(function() {
  'use strict';

  // 1. Blacklist sites where videos are protected MSE/DASH blob streams without direct MP4s
  const STREAM_ONLY_DOMAINS = ["instagram.com", "tiktok.com", "netflix.com", "hotstar.com"];
  const currentHost = window.location.hostname.toLowerCase();
  const isStreamOnlySite = STREAM_ONLY_DOMAINS.some(domain => currentHost.includes(domain));

  // 2. Alt+Click Link Interceptor
  document.addEventListener("click", (e) => {
    const link = e.target.closest("a");
    if (!link || !link.href) return;
    const href = link.href;
    if (href.startsWith("javascript:") || href.startsWith("#") || href.startsWith("blob:") || href.startsWith("data:")) return;

    if (e.altKey) {
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

  // 3. Batch Link Grabber
  chrome.runtime.onMessage.addListener((msg, sender, sendResponse) => {
    if (msg.action === "GRAB_ALL_LINKS") {
      const links = Array.from(document.querySelectorAll("a[href]"))
        .map(a => a.href)
        .filter(href => /\.(zip|rar|7z|iso|tar|gz|exe|msi|mp4|mkv|webm|mp3|flac|wav|pdf|dmg|dat|bin|apk|csv)(?:[?#]|$)/i.test(href));
      sendResponse({ links: Array.from(new Set(links)), page_title: document.title });
    }
  });

  // If this site is Instagram or protected blob streaming, never inject the video sniffer button
  if (isStreamOnlySite) {
    return;
  }

  // 4. Authentic IDM Hover-Only Video Sniffer Panel
  let activeVideo = null;
  const panel = document.createElement("div");
  panel.id = "idm-video-panel";
  panel.innerHTML = `
    <svg style="width:13px; height:13px; margin-right:5px; flex-shrink:0;" viewBox="0 0 16 16">
      <path d="M2 3h12v10H2V3z" fill="#0D47A1"/>
      <polygon points="6,5 11,8 6,11" fill="#4CAF50"/>
    </svg>
    <span style="font-weight:600; font-size:11px; color:#1e293b;">Download this video</span>
  `;

  // Authentic IDM Styling: Translucent by default (0.6 opacity), 1.0 on hover
  Object.assign(panel.style, {
    position: "absolute",
    zIndex: "2147483647",
    background: "linear-gradient(180deg, #FFFFFF 0%, #E2E8F0 100%)",
    border: "1px solid #94A3B8",
    borderRadius: "3px",
    padding: "3px 8px",
    boxShadow: "0 2px 8px rgba(0,0,0,0.25)",
    cursor: "pointer",
    display: "none",
    alignItems: "center",
    userSelect: "none",
    opacity: "0.6",
    transition: "opacity 0.2s ease",
    fontFamily: "Segoe UI, Tahoma, sans-serif"
  });

  panel.addEventListener("mouseenter", () => { panel.style.opacity = "1.0"; });
  panel.addEventListener("mouseleave", () => { panel.style.opacity = "0.6"; });

  document.body.appendChild(panel);
  function findVideoUnderCursor(x, y) {
    const videos = document.querySelectorAll("video");
    for (const v of videos) {
      const r = v.getBoundingClientRect();
      if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) {
        return v;
      }
    }
    return null;
  }

  function getDownloadableUrl(v) {
    if (!v) return null;
    let src = v.currentSrc || v.src;
    if (!src) {
      const sourceEl = v.querySelector("source[src]");
      if (sourceEl) src = sourceEl.src;
    }
    if (src && !src.startsWith("blob:") && !src.startsWith("data:")) {
      try {
        return new URL(src, window.location.href).href;
      } catch (e) {
        return src;
      }
    }
    return null;
  }

  function updatePosition() {
    if (!activeVideo || !document.contains(activeVideo)) {
      panel.style.display = "none";
      return;
    }
    const r = activeVideo.getBoundingClientRect();
    if (r.width < 160 || r.height < 90 || r.bottom < 0 || r.top > window.innerHeight) {
      panel.style.display = "none";
      return;
    }
    panel.style.top = `${window.scrollY + r.top + 8}px`;
    panel.style.left = `${window.scrollX + r.right - 165}px`;
    panel.style.display = "flex";
  }
  panel.addEventListener("click", (e) => {
    e.stopPropagation();
    e.preventDefault();
    const url = getDownloadableUrl(activeVideo);
    if (url) {
      chrome.runtime.sendMessage({
        action: "INTERCEPT_URL",
        url: url,
        referer: window.location.href,
        page_title: document.title || "Video Download",
        user_force_override: true
      });
      panel.style.display = "none";
    }
  });


  // Detect video under cursor even through transparent player control overlays
  document.addEventListener("mousemove", (e) => {
    // If hovering over the download button itself, keep it visible
    if (panel.contains(e.target)) return;

    const v = findVideoUnderCursor(e.clientX, e.clientY);
    if (v) {
      const url = getDownloadableUrl(v);
      const r = v.getBoundingClientRect();
      if (url && r.width >= 160 && r.height >= 90) {
        activeVideo = v;
        updatePosition();
        return;
      }
    }

    // Hide if moving mouse away from video area
    if (activeVideo) {
      const r = activeVideo.getBoundingClientRect();
      if (e.clientX < r.left - 20 || e.clientX > r.right + 20 ||
          e.clientY < r.top - 20 || e.clientY > r.bottom + 20) {
        panel.style.display = "none";
        activeVideo = null;
      }
    }
  }, { passive: true });


  window.addEventListener("scroll", updatePosition, { passive: true });
  window.addEventListener("resize", updatePosition, { passive: true });
})();
