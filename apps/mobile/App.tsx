import React, { useMemo, useRef, useState } from "react";
import {
  Alert,
  Image,
  Pressable,
  SafeAreaView,
  ScrollView,
  StatusBar,
  StyleSheet,
  Text,
  View,
} from "react-native";
import * as ImagePicker from "expo-image-picker";
import * as ImageManipulator from "expo-image-manipulator";
import * as MediaLibrary from "expo-media-library";
import * as Sharing from "expo-sharing";
import { processImageOnAndroid } from "./src/adapters/AndroidProcessingAdapter";
import { DEFAULT_SETTINGS, mergeSettings, PRESETS, ProcessingSettings } from "./src/contracts";
import { BeforeAfterCompare } from "../shared-ui/src/BeforeAfterCompare";
import { PresetEditor } from "../shared-ui/src/PresetEditor";
import { renderShareVideoOnDevice } from "./src/video/renderShareVideoOnDevice";
import { shareTagsFromImagePickerExif } from "./src/video/shareTagsFromExif";
import { SocialImageComposer } from "./src/components/SocialImageComposer";
import { isExifOverlayAvailable, renderExifOverlay } from "./src/native/ExifOverlay";

type StorageState = "local_only" | "uploaded" | "swapped";
type SocialFormat = "story" | "feed" | "square";
type Screen = "import" | "edit" | "export";

const FORMATS: Record<SocialFormat, { w: number; h: number; label: string; sub: string; aspect: number }> = {
  story:  { w: 2560, h: 4552, label: "Story",  sub: "9:16",  aspect: 9 / 16 },
  feed:   { w: 2560, h: 3200, label: "Feed",   sub: "4:5",   aspect: 4 / 5 },
  square: { w: 2560, h: 2560, label: "Square", sub: "1:1",   aspect: 1 },
};

const PRESET_LABELS: Record<string, string> = {
  case01_flower_warm_fix: "Flower Warm",
  default_indoor:         "Indoor",
};

const getImageSize = (uri: string): Promise<{ width: number; height: number }> =>
  new Promise((resolve, reject) => Image.getSize(uri, (w, h) => resolve({ w, h } as any), reject));

export default function App() {
  const [screen, setScreen] = useState<Screen>("import");
  const [inputUri, setInputUri] = useState<string | null>(null);
  const [outputUri, setOutputUri] = useState<string | null>(null);
  const [socialUri, setSocialUri] = useState<string | null>(null);
  const [rawSocialUri, setRawSocialUri] = useState<string | null>(null);
  const [videoUri, setVideoUri] = useState<string | null>(null);
  const [socialFormat, setSocialFormat] = useState<SocialFormat>("story");
  const [pickerExif, setPickerExif] = useState<Record<string, unknown> | null>(null);
  const [storageState] = useState<StorageState>("local_only");
  const [settings, setSettings] = useState<ProcessingSettings>({
    ...DEFAULT_SETTINGS,
    ...PRESETS.default_indoor,
  });
  const [busy, setBusy] = useState(false);

  const apiBase = useMemo(() => (process.env.EXPO_PUBLIC_FUJIFY_API ?? "").replace(/\/$/, ""), []);
  const composerRef = useRef<View>(null);

  // ── actions ──────────────────────────────────────────────

  const pickImage = async () => {
    const res = await ImagePicker.launchImageLibraryAsync({
      mediaTypes: ["images"],
      allowsEditing: false,
      quality: 1,
    });
    if (!res.canceled && res.assets.length > 0) {
      const asset = res.assets[0];
      setInputUri(asset.uri);
      setPickerExif((asset.exif as Record<string, unknown>) ?? null);
      setOutputUri(null);
      setSocialUri(null);
      setVideoUri(null);
    }
  };

  const processImage = async () => {
    if (!inputUri || busy) return;
    setBusy(true);
    try {
      const result = await processImageOnAndroid({ uri: inputUri, settings });
      setOutputUri(result.outputUri);
      setSocialUri(null);
      setVideoUri(null);
    } catch (e) {
      Alert.alert("Process failed", e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  };

  const exportSocial = async () => {
    if (!outputUri || busy) return;
    setBusy(true);
    try {
      const target = FORMATS[socialFormat];
      const size = await getImageSize(outputUri);
      const sw = (size as any).w as number;
      const sh = (size as any).h as number;
      const srcAspect = sw / sh;
      const tgtAspect = target.w / target.h;

      let cropX = 0, cropY = 0, cropW = sw, cropH = sh;
      if (srcAspect > tgtAspect) {
        cropW = Math.round(sh * tgtAspect);
        cropX = Math.floor((sw - cropW) / 2);
      } else if (srcAspect < tgtAspect) {
        cropH = Math.round(sw / tgtAspect);
        cropY = Math.floor((sh - cropH) / 2);
      }

      const social = await ImageManipulator.manipulateAsync(
        outputUri,
        [
          { crop: { originX: cropX, originY: cropY, width: cropW, height: cropH } },
          { resize: { width: target.w, height: target.h } },
        ],
        { compress: 1, format: ImageManipulator.SaveFormat.JPEG },
      );
      setRawSocialUri(social.uri);
      setVideoUri(null);

      const tags = shareTagsFromImagePickerExif(pickerExif);

      // 1. CoreGraphics on-device (iOS)
      if (isExifOverlayAvailable) {
        try {
          const out = await renderExifOverlay(social.uri, tags.lineCamera, tags.lineExposure);
          setSocialUri(out);
          setBusy(false);
          return;
        } catch { /* fall through */ }
      }

      // 2. Server-side fallback (Python/Pillow)
      if (apiBase) {
        try {
          const form = new FormData();
          form.append("file", { uri: social.uri, type: "image/jpeg", name: "social.jpg" } as any);
          form.append("format", socialFormat);
          const res = await fetch(`${apiBase}/social/image`, { method: "POST", body: form });
          if (res.ok) {
            const data = await res.json() as { files?: { image?: string } };
            const rel = data.files?.image;
            if (rel) {
              const { downloadAsync, cacheDirectory } = await import("expo-file-system");
              const dl = await downloadAsync(`${apiBase}${rel}`, `${cacheDirectory}social_exif_${Date.now()}.jpg`);
              setSocialUri(dl.uri);
              setBusy(false);
              return;
            }
          }
        } catch { /* fall through */ }
      }

      // 3. Plain social image
      setSocialUri(social.uri);
      setBusy(false);
    } catch (e) {
      Alert.alert("Export failed", e instanceof Error ? e.message : String(e));
      setBusy(false);
    }
  };


  const saveToGallery = async (uri: string) => {
    const { status } = await MediaLibrary.requestPermissionsAsync();
    if (status !== "granted") { Alert.alert("Permission needed"); return; }
    try {
      await MediaLibrary.saveToLibraryAsync(uri);
      Alert.alert("Saved", "Saved to gallery.");
    } catch (e) {
      Alert.alert("Save failed", e instanceof Error ? e.message : String(e));
    }
  };

  const share = async (uri: string, mimeType = "image/jpeg") => {
    if (!(await Sharing.isAvailableAsync())) { Alert.alert("Sharing unavailable"); return; }
    try {
      await Sharing.shareAsync(uri, { mimeType, dialogTitle: "Share" });
    } catch (e) {
      Alert.alert("Share failed", e instanceof Error ? e.message : String(e));
    }
  };

  const exportVideo = async () => {
    const src = socialUri ?? outputUri;
    if (!src || busy) return;
    setBusy(true);
    setVideoUri(null);
    try {
      const tags = shareTagsFromImagePickerExif(pickerExif ?? undefined);
      const out = await renderShareVideoOnDevice({ inputImageUri: src, durationSec: 5, layout: "vertical", tags });
      setVideoUri(out);
    } catch (e) {
      Alert.alert("Video failed", `${e instanceof Error ? e.message : String(e)}\n\nRequires expo run:android (not Expo Go).`);
    } finally {
      setBusy(false);
    }
  };

  // ── screens ──────────────────────────────────────────────

  function renderImport() {
    return (
      <View style={s.screen}>
        <View style={s.importWrap}>
          {/* Brand */}
          <View style={s.brandRow}>
            <Text style={s.brandName}>Fujify</Text>
            <Text style={s.brandTagline}>Edit · Share · Keep</Text>
          </View>

          {/* Photo picker */}
          <Pressable style={s.photoArea} onPress={pickImage}>
            {inputUri ? (
              <Image source={{ uri: inputUri }} style={s.photoPreview} resizeMode="cover" />
            ) : (
              <View style={s.photoEmpty}>
                <Text style={s.photoEmptyIcon}>▣</Text>
                <Text style={s.photoEmptyText}>Choose a photo</Text>
              </View>
            )}
          </Pressable>

          {inputUri && (
            <Pressable onPress={pickImage}>
              <Text style={s.changePhoto}>Change photo</Text>
            </Pressable>
          )}

          <Pressable
            style={[s.goldBtn, !inputUri && s.btnOff]}
            disabled={!inputUri}
            onPress={() => setScreen("edit")}
          >
            <Text style={s.goldBtnText}>Edit Photo  →</Text>
          </Pressable>
        </View>
      </View>
    );
  }

  function renderEdit() {
    const compareH = 300;
    return (
      <View style={s.screen}>
        <View style={s.header}>
          <Pressable style={s.hdrSide} onPress={() => setScreen("import")}>
            <Text style={s.hdrBack}>←</Text>
          </Pressable>
          <Text style={s.hdrTitle}>Edit</Text>
          <Pressable
            style={s.hdrSide}
            disabled={!outputUri}
            onPress={() => setScreen("export")}
          >
            <Text style={[s.hdrNext, outputUri ? s.gold : s.muted]}>Export →</Text>
          </Pressable>
        </View>

        {/* Before / after */}
        {inputUri && outputUri ? (
          <BeforeAfterCompare beforeUri={inputUri} afterUri={outputUri} height={compareH} />
        ) : (
          <Image source={{ uri: inputUri! }} style={{ height: compareH, backgroundColor: "#000" }} resizeMode="contain" />
        )}

        {/* Bottom panel */}
        <ScrollView
          style={s.editPanel}
          contentContainerStyle={s.editPanelInner}
          keyboardShouldPersistTaps="handled"
        >
          {/* Preset chips */}
          <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={s.presetStrip}>
            {Object.keys(PRESETS).map((name) => (
              <Pressable
                key={name}
                style={s.presetChip}
                onPress={() => setSettings((prev) => mergeSettings(prev, PRESETS[name]))}
              >
                <Text style={s.presetChipText}>
                  {PRESET_LABELS[name] ?? name.replace(/_/g, " ")}
                </Text>
              </Pressable>
            ))}
          </ScrollView>

          {/* Sliders */}
          <PresetEditor
            settings={settings}
            onChange={(patch) => setSettings((prev) => mergeSettings(prev, patch))}
          />

          {/* Apply */}
          <Pressable
            style={[s.goldBtn, (busy || !inputUri) && s.btnOff]}
            disabled={busy || !inputUri}
            onPress={processImage}
          >
            <Text style={s.goldBtnText}>{busy ? "Processing…" : "Apply"}</Text>
          </Pressable>
        </ScrollView>
      </View>
    );
  }

  function renderExport() {
    const fmt = FORMATS[socialFormat];
    return (
      <View style={s.screen}>
        <View style={s.header}>
          <Pressable style={s.hdrSide} onPress={() => setScreen("edit")}>
            <Text style={s.hdrBack}>← Edit</Text>
          </Pressable>
          <Text style={s.hdrTitle}>Export</Text>
          <View style={s.hdrSide} />
        </View>

        {/* Composer OUTSIDE ScrollView so captureRef works on iOS */}
        {rawSocialUri && (() => {
          const tags = shareTagsFromImagePickerExif(pickerExif);
          const previewW = 355;
          const previewH = Math.round(previewW / fmt.aspect);
          return (
            <View style={s.composerWrap}>
              <SocialImageComposer
                ref={composerRef}
                imageUri={rawSocialUri}
                width={previewW}
                height={previewH}
                lineCamera={tags.lineCamera}
                lineExposure={tags.lineExposure}
              />
            </View>
          );
        })()}

        <ScrollView contentContainerStyle={s.exportContent}>
          {/* Format tabs */}
          <View style={s.formatRow}>
            {(Object.keys(FORMATS) as SocialFormat[]).map((f) => {
              const d = FORMATS[f];
              const active = f === socialFormat;
              return (
                <Pressable key={f} style={s.formatOpt} onPress={() => setSocialFormat(f)}>
                  <View
                    style={[
                      s.formatBox,
                      { aspectRatio: d.aspect },
                      active && s.formatBoxActive,
                    ]}
                  />
                  <Text style={[s.formatLabel, active && s.gold]}>{d.label}</Text>
                  <Text style={[s.formatSub, active && s.gold]}>{d.sub}</Text>
                </Pressable>
              );
            })}
          </View>

          {/* Export HQ */}
          <Pressable
            style={[s.goldBtn, (!outputUri || busy) && s.btnOff]}
            disabled={!outputUri || busy}
            onPress={exportSocial}
          >
            <Text style={s.goldBtnText}>
              {busy ? "Exporting…" : `Export ${fmt.label} ${fmt.sub} HQ`}
            </Text>
          </Pressable>


          {socialUri && (
            <>
              <View style={s.actionRow}>
                <Pressable style={s.ghostBtn} onPress={() => share(socialUri)}>
                  <Text style={s.ghostBtnText}>Share</Text>
                </Pressable>
                <Pressable style={s.ghostBtn} onPress={() => saveToGallery(socialUri)}>
                  <Text style={s.ghostBtnText}>Save</Text>
                </Pressable>
                <Pressable style={[s.ghostBtn, busy && s.btnOff]} disabled={busy} onPress={exportVideo}>
                  <Text style={s.ghostBtnText}>{busy ? "…" : "Video"}</Text>
                </Pressable>
              </View>
            </>
          )}

          {/* Video actions */}
          {videoUri && (
            <View style={s.actionRow}>
              <Pressable style={s.ghostBtn} onPress={() => share(videoUri, "video/mp4")}>
                <Text style={s.ghostBtnText}>Share Video</Text>
              </Pressable>
              <Pressable style={s.ghostBtn} onPress={() => saveToGallery(videoUri)}>
                <Text style={s.ghostBtnText}>Save Video</Text>
              </Pressable>
            </View>
          )}

          {/* Server video fallback */}
          {apiBase ? (
            <Text style={s.hint}>Server: {apiBase}</Text>
          ) : null}

          {/* Storage badge */}
          <View style={s.badge}>
            <Text style={s.badgeDot}>●</Text>
            <Text style={s.badgeText}>
              {storageState === "local_only" ? "Local only" : storageState === "uploaded" ? "Uploaded" : "Swapped"}
            </Text>
          </View>
        </ScrollView>
      </View>
    );
  }

  return (
    <SafeAreaView style={s.safe}>
      <StatusBar barStyle="light-content" backgroundColor="#0a0a0a" />
      {screen === "import" && renderImport()}
      {screen === "edit" && renderEdit()}
      {screen === "export" && renderExport()}
    </SafeAreaView>
  );
}

// ── design tokens ────────────────────────────────────────
const GOLD   = "#c8a96e";
const BG     = "#0a0a0a";
const SURF   = "#161616";
const BORDER = "#2a2a2a";
const TEXT   = "#e8e0d4";
const DIM    = "#a09890";
const MUTED  = "#4a4640";

const s = StyleSheet.create({
  safe:   { flex: 1, backgroundColor: BG },
  screen: { flex: 1, backgroundColor: BG },

  // ── import ──
  importWrap: { flex: 1, padding: 32, justifyContent: "center", gap: 28 },
  brandRow:   { gap: 6 },
  brandName:  { fontSize: 42, fontFamily: "serif", fontStyle: "italic", color: GOLD, letterSpacing: 1 },
  brandTagline: { fontSize: 12, color: DIM, letterSpacing: 3, textTransform: "uppercase" },

  photoArea:    { borderRadius: 14, overflow: "hidden" },
  photoPreview: { width: "100%", height: 260 },
  photoEmpty: {
    width: "100%", height: 260, borderRadius: 14,
    borderWidth: 1, borderColor: BORDER, borderStyle: "dashed",
    alignItems: "center", justifyContent: "center", gap: 14,
    backgroundColor: SURF,
  },
  photoEmptyIcon: { fontSize: 36, color: MUTED },
  photoEmptyText: { fontSize: 14, color: MUTED },
  changePhoto: { textAlign: "center", color: DIM, fontSize: 13 },

  goldBtn:     { backgroundColor: GOLD, borderRadius: 12, paddingVertical: 16, alignItems: "center" },
  goldBtnText: { color: BG, fontWeight: "700", fontSize: 15, letterSpacing: 0.3 },
  btnOff:      { opacity: 0.35 },

  // ── header ──
  header: {
    flexDirection: "row", alignItems: "center", justifyContent: "space-between",
    paddingHorizontal: 16, paddingVertical: 13,
    borderBottomWidth: 1, borderBottomColor: BORDER,
  },
  hdrSide:  { minWidth: 70 },
  hdrBack:  { color: TEXT, fontSize: 15 },
  hdrTitle: { color: TEXT, fontSize: 16, fontWeight: "600" },
  hdrNext:  { fontSize: 15, textAlign: "right" },
  gold:     { color: GOLD },
  muted:    { color: MUTED },

  // ── edit ──
  editPanel:      { backgroundColor: SURF, borderTopWidth: 1, borderTopColor: BORDER },
  editPanelInner: { padding: 16, gap: 14 },
  presetStrip:    { flexDirection: "row", gap: 8, paddingBottom: 2 },
  presetChip: {
    paddingHorizontal: 16, paddingVertical: 8,
    backgroundColor: "#1e1e1e", borderRadius: 999,
    borderWidth: 1, borderColor: BORDER,
  },
  presetChipText: { color: TEXT, fontSize: 13 },

  // ── export ──
  exportContent: { padding: 20, gap: 20 },
  formatRow:     { flexDirection: "row", gap: 20, justifyContent: "center", paddingVertical: 8 },
  formatOpt:     { alignItems: "center", gap: 6 },
  formatBox: {
    width: 52,
    backgroundColor: SURF,
    borderWidth: 1, borderColor: BORDER, borderRadius: 6,
  },
  formatBoxActive: { borderColor: GOLD, backgroundColor: "rgba(200,169,110,0.08)" },
  formatLabel: { color: DIM, fontSize: 13, fontWeight: "600" },
  formatSub:   { color: MUTED, fontSize: 10 },

  socialPreview: { width: "100%", borderRadius: 10, backgroundColor: "#111" },

  actionRow:     { flexDirection: "row", gap: 10 },
  ghostBtn: {
    flex: 1, paddingVertical: 13, borderRadius: 10,
    backgroundColor: SURF, borderWidth: 1, borderColor: BORDER,
    alignItems: "center",
  },
  ghostBtnText: { color: TEXT, fontSize: 13, fontWeight: "600" },

  offscreen: { position: "absolute", left: -9999, opacity: 0 },
  composerWrap: { alignItems: "center", paddingHorizontal: 16 },
  hint:  { color: MUTED, fontSize: 11, textAlign: "center" },

  badge: {
    flexDirection: "row", alignItems: "center", gap: 6,
    alignSelf: "center", paddingVertical: 6, paddingHorizontal: 14,
    backgroundColor: SURF, borderRadius: 999, borderWidth: 1, borderColor: BORDER,
  },
  badgeDot:  { color: MUTED, fontSize: 8 },
  badgeText: { color: DIM, fontSize: 12 },
});
