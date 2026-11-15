// Test suite: Chrome extension filtering and stream bypass rules
const assert = require("assert");

const DEFAULT_FILE_TYPES = [
  "zip", "rar", "7z", "tar", "gz", "bz2", "xz", "iso", "dmg", "pkg", "deb", "rpm",
  "exe", "msi", "apk", "bin", "cue", "img", "vhd", "vmdk",
  "mp4", "mkv", "avi", "mov", "wmv", "flv", "webm", "m4v", "ts", "m2ts",
  "mp3", "flac", "wav", "aac", "ogg", "wma", "m4a", "opus",
  "pdf", "doc", "docx", "xls", "xlsx", "ppt", "pptx", "csv"
];

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

function evaluateInterception({
  url,
  filename,
  fileSize,
  minCaptureSize = 10 * 1024 * 1024,
  enableSmallFileBypass = true,
  exceptionSites = "",
  fileTypes = DEFAULT_FILE_TYPES.join(",")
}) {
  // Check exception sites
  if (exceptionSites && exceptionSites.trim()) {
    const sites = exceptionSites.split(/[\s,;]+/).map(s => s.trim().toLowerCase()).filter(Boolean);
    try {
      const parsedUrl = new URL(url);
      const host = parsedUrl.hostname.toLowerCase();
      if (sites.some(site => host === site || host.endsWith("." + site))) {
        return { intercept: false, reason: "exception_site" };
      }
    } catch (e) {}
  }

  // 2. Small File Bypass
  if (enableSmallFileBypass && fileSize > 0 && fileSize < minCaptureSize) {
    return { intercept: false, reason: "small_file_bypass" };
  }

  // 3. Extension filter check
  const userTypes = fileTypes ? fileTypes.split(/[\s,]+/).map(s => s.trim().toLowerCase()).filter(Boolean) : [];
  const allowed = userTypes.length > 0 ? userTypes : DEFAULT_FILE_TYPES;
  const ext = extractExtension(filename, url);

  const isWhitelistedExt = ext && (allowed.includes(ext) || allowed.includes("*"));
  const isLargeByteDownload = fileSize >= minCaptureSize;

  if (!isWhitelistedExt && !isLargeByteDownload) {
    return { intercept: false, reason: "not_whitelisted" };
  }

  // 4. Dynamic Stream Guard for In-Browser Web Apps (e.g. Google Docs / Office exports)
  const isDynamicDocExport = /docs\.google\.com\/(document|spreadsheets|presentation)\/.*export/i.test(url) ||
                             /drive\.google\.com\/.*export/i.test(url) ||
                             /officeapps\.live\.com/i.test(url);
  if (enableSmallFileBypass && fileSize <= 0 && isDynamicDocExport) {
    return { intercept: false, reason: "dynamic_doc_export_bypass" };
  }

  return { intercept: true, ext, fileSize };
}

console.log("==========================================================");
console.log("  Unit Test: Extension Filtering & Stream Guard Rules     ");
console.log("==========================================================");

// Test 1: Real MKV movie with initial unknown size (0 bytes) -> MUST INTERCEPT!
console.log("\n[1/7] Testing Large MKV Movie with Unknown Initial Size...");
const t1 = evaluateInterception({
  url: "https://workers.dev/stream/Ramayana_(2026)_Hindi_Movie_1080p_HDTC.mkv",
  filename: "Ramayana_(2026)_Hindi_Movie_1080p_HDTC.mkv",
  fileSize: 0
});
assert.strictEqual(t1.intercept, true);
console.log("  -> MKV movie with fileSize 0 is INTERCEPTED (Not falsely bypassed): PASSED");

// Test 2: Large ISO image with initial unknown size (-1 bytes) -> MUST INTERCEPT!
console.log("\n[2/7] Testing ISO Disk Image with Unknown Initial Size...");
const t2 = evaluateInterception({
  url: "https://releases.ubuntu.com/24.04/ubuntu-24.04-desktop-amd64.iso",
  filename: "ubuntu-24.04-desktop-amd64.iso",
  fileSize: -1
});
assert.strictEqual(t2.intercept, true);
console.log("  -> ISO image with fileSize -1 is INTERCEPTED: PASSED");

// Test 3: Google Docs Dynamic Export -> MUST BE BYPASSED!
console.log("\n[3/7] Testing Google Docs Dynamic PDF Export...");
const t3 = evaluateInterception({
  url: "https://docs.google.com/document/d/1BxiMVs0XRA5nFMdKvBdBZjgmUUqptlbs74OgvE2upms/export?format=pdf",
  filename: "202451025-PRACTICAL-7.pdf",
  fileSize: 0
});
assert.strictEqual(t3.intercept, false);
assert.strictEqual(t3.reason, "dynamic_doc_export_bypass");
console.log("  -> Google Docs dynamic export is BYPASSED to browser: PASSED");

// Test 4: Google Sheets Dynamic Export -> MUST BE BYPASSED!
console.log("\n[4/7] Testing Google Sheets Dynamic XLSX Export...");
const t4 = evaluateInterception({
  url: "https://docs.google.com/spreadsheets/d/1BxiMVs0XRA5nFMdKvBdBZjgmUUqptlbs74OgvE2upms/export?format=xlsx",
  filename: "Financial_Report.xlsx",
  fileSize: 0
});
assert.strictEqual(t4.intercept, false);
assert.strictEqual(t4.reason, "dynamic_doc_export_bypass");
console.log("  -> Google Sheets dynamic export is BYPASSED to browser: PASSED");

// Test 5: Exception Site Domain Match -> MUST BE BYPASSED!
console.log("\n[5/7] Testing Exception Sites Whitelist Bypass...");
const t5 = evaluateInterception({
  url: "https://internal.trusted-corp.com/downloads/setup.exe",
  filename: "setup.exe",
  fileSize: 50 * 1024 * 1024,
  exceptionSites: "trusted-corp.com, intranet.local"
});
assert.strictEqual(t5.intercept, false);
assert.strictEqual(t5.reason, "exception_site");
console.log("  -> Exception site is BYPASSED to browser: PASSED");

// Test 6: Small File Bypass (< 10 MB) -> MUST BE BYPASSED!
console.log("\n[6/7] Testing Small File Bypass (< 10 MB)...");
const t6 = evaluateInterception({
  url: "https://example.com/icon.png",
  filename: "icon.png",
  fileSize: 256 * 1024 // 256 KB
});
assert.strictEqual(t6.intercept, false);
assert.strictEqual(t6.reason, "small_file_bypass");
console.log("  -> 256 KB file correctly bypassed: PASSED");

// Test 7: Large Non-Whitelisted Binary (150 MB .customdat) -> MUST INTERCEPT!
console.log("\n[7/7] Testing Large Non-Whitelisted Binary (150 MB)...");
const t7 = evaluateInterception({
  url: "https://example.com/game_asset.customdat",
  filename: "game_asset.customdat",
  fileSize: 150 * 1024 * 1024
});
assert.strictEqual(t7.intercept, true);
console.log("  -> 150 MB binary file intercepted by size rule: PASSED");

console.log("\n==========================================================");
console.log("  [PASSED] ALL 7/7 EXTENSION FILTER RULES PASSED!         ");
console.log("==========================================================");

