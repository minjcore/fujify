// Fujify site — single-file content loader.
// Fetches a Markdown page from R2 (Cloudflare), falls back to local ./content,
// renders it into #content. Edit copy in R2 → site updates with no redeploy.
(function () {
  "use strict";

  // Served by the fujify-cdn Worker (reads private R2) on the custom domain.
  var R2_BASE   = "https://cdn.fujify.app/content/";
  var LOCAL_BASE = "./content/";   // dev / offline fallback

  function pageName() {
    var p = new URLSearchParams(location.search).get("p") ||
            location.hash.replace(/^#\/?/, "");
    return (p || "home").replace(/[^a-z0-9_-]/gi, "") || "home";
  }

  // ---- minimal Markdown → HTML (no deps) ----
  function esc(s) {
    return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
  }
  function inline(s) {
    return esc(s)
      .replace(/!\[([^\]]*)\]\(([^)]+)\)/g, '<img alt="$1" src="$2">')
      .replace(/\[([^\]]+)\]\(([^)]+)\)/g, '<a href="$2">$1</a>')
      .replace(/\*\*([^*]+)\*\*/g, "<strong>$1</strong>")
      .replace(/\*([^*]+)\*/g, "<em>$1</em>")
      .replace(/`([^`]+)`/g, "<code>$1</code>");
  }
  function mdToHtml(md) {
    var lines = md.replace(/\r\n/g, "\n").split("\n");
    var out = [], i = 0;
    while (i < lines.length) {
      var line = lines[i];
      if (/^```/.test(line)) {                       // fenced code
        var buf = []; i++;
        while (i < lines.length && !/^```/.test(lines[i])) buf.push(esc(lines[i++]));
        i++; out.push("<pre><code>" + buf.join("\n") + "</code></pre>"); continue;
      }
      if (/^\s*$/.test(line)) { i++; continue; }
      if (/^---\s*$/.test(line)) { out.push("<hr>"); i++; continue; }
      var h = line.match(/^(#{1,4})\s+(.*)$/);       // headings
      if (h) { var n = h[1].length; out.push("<h" + n + ">" + inline(h[2]) + "</h" + n + ">"); i++; continue; }
      if (/^>\s?/.test(line)) {                       // blockquote
        var b = [];
        while (i < lines.length && /^>\s?/.test(lines[i])) b.push(lines[i++].replace(/^>\s?/, ""));
        out.push("<blockquote>" + mdToHtml(b.join("\n")) + "</blockquote>"); continue;
      }
      if (/^[-*]\s+/.test(line)) {                    // unordered list
        var ul = [];
        while (i < lines.length && /^[-*]\s+/.test(lines[i])) ul.push("<li>" + inline(lines[i++].replace(/^[-*]\s+/, "")) + "</li>");
        out.push("<ul>" + ul.join("") + "</ul>"); continue;
      }
      if (/^\d+\.\s+/.test(line)) {                   // ordered list
        var ol = [];
        while (i < lines.length && /^\d+\.\s+/.test(lines[i])) ol.push("<li>" + inline(lines[i++].replace(/^\d+\.\s+/, "")) + "</li>");
        out.push("<ol>" + ol.join("") + "</ol>"); continue;
      }
      var para = [];                                  // paragraph
      while (i < lines.length && !/^\s*$/.test(lines[i]) &&
             !/^(#{1,4}\s|>|[-*]\s|\d+\.\s|---|```)/.test(lines[i])) para.push(lines[i++]);
      out.push("<p>" + inline(para.join(" ")) + "</p>");
    }
    return out.join("\n");
  }

  function load() {
    var el = document.getElementById("content");
    el.innerHTML = '<p class="loading">Loading…</p>';
    var page = pageName();
    fetch(R2_BASE + page + ".md")
      .then(function (r) { if (!r.ok) throw new Error("r2"); return r.text(); })
      .catch(function () { return fetch(LOCAL_BASE + page + ".md").then(function (r) { return r.text(); }); })
      .then(function (md) { el.innerHTML = mdToHtml(md); window.scrollTo(0, 0); })
      .catch(function () { el.innerHTML = '<p class="loading">Không tải được nội dung.</p>'; });
  }

  window.addEventListener("hashchange", load);
  document.addEventListener("DOMContentLoaded", load);
})();
