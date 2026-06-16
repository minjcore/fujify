/**
 * Best-effort EXIF → overlay lines (ImagePicker `asset.exif` shape varies by OS).
 */

import type { ShareAssTags } from "./shareAss";

function str(v: unknown): string {
  if (v === null || v === undefined) return "";
  if (typeof v === "string") return v.trim();
  if (typeof v === "number" && Number.isFinite(v)) return String(v);
  return String(v).trim();
}

export function shareTagsFromImagePickerExif(exif: Record<string, unknown> | null | undefined): ShareAssTags {
  if (!exif) {
    return { lineCamera: "Photo", lineExposure: "" };
  }

  const make = str(exif.Make ?? exif.make);
  const model = str(exif.Model ?? exif.model);
  const lens = str(exif.LensModel ?? exif.lensModel ?? exif.LensMake);

  const cameraParts: string[] = [];
  const mm = `${make} ${model}`.trim();
  if (mm) cameraParts.push(mm);
  if (lens) cameraParts.push(lens);
  const lineCamera = cameraParts.length ? cameraParts.join("  ·  ") : "Photo";

  const iso = exif.ISOSpeedRatings ?? exif.ISO ?? exif.iso;
  const isoStr =
    typeof iso === "number"
      ? `ISO ${Math.round(iso)}`
      : typeof iso === "string" && iso
        ? `ISO ${iso}`
        : "";

  const fn = exif.FNumber ?? exif.ApertureValue;
  let aperture = "";
  if (typeof fn === "number" && fn > 0) {
    aperture = `f/${fn.toFixed(1)}`.replace(".0", "");
  } else if (typeof fn === "string" && fn) {
    aperture = fn.startsWith("f/") ? fn : `f/${fn}`;
  }

  const et = exif.ExposureTime ?? exif.exposureTime;
  let shutter = "";
  if (typeof et === "number" && et > 0) {
    if (et >= 1) {
      const rounded = Math.round(et * 10) / 10;
      shutter = `${Number.isInteger(rounded) ? Math.round(et) : rounded}s`;
    } else {
      const inv = 1 / et;
      const r = Math.round(inv);
      shutter = Math.abs(inv - r) < 0.15 ? `1/${r}s` : `1/${Math.round(inv)}s`;
    }
  } else if (typeof et === "string" && et) {
    shutter = et;
  }

  const fl = exif.FocalLength ?? exif.focalLength;
  let focal = "";
  if (typeof fl === "number" && fl > 0) focal = `${Math.round(fl)}mm`;
  else if (typeof fl === "string" && fl) focal = fl.includes("mm") ? fl : `${fl}mm`;

  const expParts = [aperture, shutter, isoStr, focal].filter(Boolean);
  const lineExposure = expParts.join("  ·  ");

  return { lineCamera, lineExposure };
}
