// Fujify CDN Worker — serves the R2 content bucket over HTTP.
//
// Routes (bind on cdn.fujify.app/*):
//   GET /content/<page>.md   → markdown for the site (app.js fetches these)
//   GET /download/<file>     → release binaries (DMG, zip…) with Range support
//   GET /                    → tiny index (health check)
//
// The R2 bucket stays PRIVATE — public access is NOT enabled. This Worker is the only
// reader (via the BUCKET binding) and the sole public gateway, so it controls exactly
// what is exposed. Adds CORS, caching, ETag/304, and Range (resumable downloads).
// Served content is meant to be publicly readable, so Access-Control-Allow-Origin is "*".

const CORS = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Methods": "GET, HEAD, PUT, OPTIONS",
  "Access-Control-Allow-Headers": "Range, If-None-Match, If-Modified-Since, Authorization, Content-Type",
  "Access-Control-Expose-Headers": "Content-Length, Content-Range, ETag, Accept-Ranges",
};

const TYPES = {
  md: "text/markdown; charset=utf-8",
  json: "application/json; charset=utf-8",
  txt: "text/plain; charset=utf-8",
  png: "image/png",
  jpg: "image/jpeg",
  jpeg: "image/jpeg",
  svg: "image/svg+xml",
  dmg: "application/x-apple-diskimage",
  zip: "application/zip",
};

function contentType(key, meta) {
  if (meta && meta.contentType) return meta.contentType;
  const ext = key.split(".").pop().toLowerCase();
  return TYPES[ext] || "application/octet-stream";
}

// Cache longer for binaries, shorter for editable markdown.
function cacheControl(key) {
  if (key.startsWith("download/")) return "public, max-age=86400, immutable";
  if (key.endsWith(".md")) return "public, max-age=60, stale-while-revalidate=300";
  return "public, max-age=3600";
}

export default {
  async fetch(request, env) {
    if (request.method === "OPTIONS") {
      return new Response(null, { status: 204, headers: CORS });
    }

    const url = new URL(request.url);
    let key = decodeURIComponent(url.pathname.replace(/^\/+/, ""));

    // Authenticated upload: PUT /library/<name> (Bearer UPLOAD_TOKEN) → write to R2.
    if (request.method === "PUT") {
      if (!key.startsWith("library/") || key.includes(".."))
        return new Response("Forbidden", { status: 403, headers: CORS });
      const auth = request.headers.get("Authorization") || "";
      if (!env.UPLOAD_TOKEN || auth !== "Bearer " + env.UPLOAD_TOKEN)
        return new Response("Unauthorized", { status: 401, headers: CORS });
      const obj = await env.BUCKET.put(key, request.body, {
        httpMetadata: { contentType: contentType(key) },
      });
      return new Response(JSON.stringify({ ok: true, key, size: obj.size }), {
        status: 201, headers: { ...CORS, "Content-Type": "application/json" },
      });
    }

    if (request.method !== "GET" && request.method !== "HEAD") {
      return new Response("Method Not Allowed", { status: 405, headers: CORS });
    }

    if (key === "" || key === "index.html") {
      return new Response("Fujify CDN — ok\n", {
        headers: { ...CORS, "Content-Type": "text/plain" },
      });
    }

    // Guard against path traversal / odd keys.
    if (key.includes("..")) return new Response("Bad Request", { status: 400, headers: CORS });

    // Range (resumable downloads for big binaries).
    const rangeHeader = request.headers.get("Range");
    let range;
    if (rangeHeader) {
      const m = /^bytes=(\d*)-(\d*)$/.exec(rangeHeader.trim());
      if (m) {
        const start = m[1] ? parseInt(m[1], 10) : undefined;
        const end = m[2] ? parseInt(m[2], 10) : undefined;
        if (start !== undefined && end !== undefined) range = { offset: start, length: end - start + 1 };
        else if (start !== undefined) range = { offset: start };
        else if (end !== undefined) range = { suffix: end };
      }
    }

    const obj = await env.BUCKET.get(key, {
      range,
      onlyIf: request.headers,   // honours If-None-Match / If-Modified-Since → 304
    });

    if (obj === null) {
      return new Response("Not Found: " + key, { status: 404, headers: CORS });
    }

    const headers = new Headers(CORS);
    obj.writeHttpMetadata(headers);              // ETag, Last-Modified, etc.
    headers.set("Content-Type", contentType(key, obj.httpMetadata));
    headers.set("Cache-Control", cacheControl(key));
    headers.set("Accept-Ranges", "bytes");
    if (obj.httpEtag) headers.set("ETag", obj.httpEtag);

    // Conditional request matched → 304 (R2 returns a body-less object).
    if (!obj.body) return new Response(null, { status: 304, headers });

    // Range satisfied → 206 with Content-Range (only when the client asked for a range;
    // R2 populates obj.range even on full GETs, so gate on the request header).
    if (rangeHeader && obj.range && typeof obj.range.offset === "number") {
      const start = obj.range.offset;
      const len = obj.range.length ?? obj.size - start;
      headers.set("Content-Range", `bytes ${start}-${start + len - 1}/${obj.size}`);
      headers.set("Content-Length", String(len));
      return new Response(request.method === "HEAD" ? null : obj.body, { status: 206, headers });
    }

    headers.set("Content-Length", String(obj.size));
    return new Response(request.method === "HEAD" ? null : obj.body, { status: 200, headers });
  },
};
