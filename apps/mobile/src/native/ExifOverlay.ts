import { NativeModules, Platform } from "react-native";

const { RNExifOverlay } = NativeModules;

export async function renderExifOverlay(
  imageUri: string,
  lineCamera: string,
  lineExposure: string,
): Promise<string> {
  if (Platform.OS !== "ios" || !RNExifOverlay) {
    throw new Error("ExifOverlay native module not available");
  }
  return RNExifOverlay.render(imageUri, lineCamera, lineExposure);
}

export const isExifOverlayAvailable =
  Platform.OS === "ios" && !!RNExifOverlay;
