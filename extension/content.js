// IDM Integration - Direct In-Page Link Interceptor
(function() {
  document.addEventListener("click", (e) => {
    const link = e.target.closest("a");
    if (!link || !link.href) return;

    const href = link.href;
    if (href.startsWith("javascript:") || href.startsWith("#") || href.startsWith("blob:") || href.startsWith("data:")) return;

    // Supported downloadable file extensions
    const fileExtRegex = /\.(zip|rar|7z|iso|tar|gz|exe|msi|mp4|mkv|webm|mp3|flac|wav|pdf|dmg|dat|bin|apk|csv|tsv|img)(?:[?#]|$)/i;

    if (fileExtRegex.test(href)) {
      console.log("[IDM Extension] Link clicked, dispatching to IDM:", href);
      e.preventDefault();
      e.stopPropagation();

      chrome.runtime.sendMessage({
        action: "INTERCEPT_URL",
        url: href,
        referer: window.location.href,
        page_title: document.title
      });
    }
  }, true);
})();
