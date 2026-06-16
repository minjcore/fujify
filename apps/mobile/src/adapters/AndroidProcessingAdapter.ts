import * as ImageManipulator from "expo-image-manipulator";
import { DEFAULT_SETTINGS, ProcessingSettings } from "../contracts";

export interface AndroidProcessRequest {
  uri: string;
  settings: Partial<ProcessingSettings>;
}

export interface AndroidProcessResult {
  outputUri: string;
  effectiveSettings: ProcessingSettings;
}

/**
 * JPEG-first Android adapter for V1.
 * This path intentionally keeps mobile processing simple and offline.
 */
export async function processImageOnAndroid(
  request: AndroidProcessRequest,
): Promise<AndroidProcessResult> {
  const effectiveSettings: ProcessingSettings = {
    ...DEFAULT_SETTINGS,
    ...request.settings,
  };

  // V1 approximation: brightness/contrast as first-pass visual parity.
  // Full parity with Python WB/tone model can be introduced in native module v2.
  const brightness = effectiveSettings.brightness;
  const contrast = effectiveSettings.contrast;
  const warmBias = ((effectiveSettings.temp ?? 5200) - 5200) / 1000;
  const saturation = Math.max(0, 1 + contrast * 0.35 - warmBias * 0.08);

  let saved: { uri: string };

  if (typeof (ImageManipulator as any).manipulate === "function") {
    // SDK 13+ builder API (Android)
    const context = (ImageManipulator as any).manipulate(request.uri);
    context.adjust({ brightness: brightness * 0.4, contrast: contrast * 0.4, saturation });
    const rendered = await context.renderAsync();
    saved = await rendered.saveAsync({ compress: 0.95, format: ImageManipulator.SaveFormat.JPEG });
  } else {
    // Fallback (iOS) — re-encode without color adjustments for now
    saved = await ImageManipulator.manipulateAsync(
      request.uri, [],
      { compress: 0.95, format: ImageManipulator.SaveFormat.JPEG },
    );
  }

  return {
    outputUri: saved.uri,
    effectiveSettings,
  };
}

