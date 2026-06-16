import React, { useEffect, useImperativeHandle, forwardRef } from "react";
import {
  Canvas,
  Image,
  Rect,
  Text,
  matchFont,
  useCanvasRef,
  useImage,
} from "@shopify/react-native-skia";
import * as FileSystem from "expo-file-system";
import { Platform } from "react-native";

export interface SkiaComposerHandle {
  capture: () => Promise<string>;
}

interface Props {
  imageUri: string;
  width: number;
  height: number;
  lineCamera: string;
  lineExposure: string;
  onReady?: () => void;
}

const BAR_RATIO = 0.13;

export const SkiaSocialComposer = forwardRef<SkiaComposerHandle, Props>(
  ({ imageUri, width, height, lineCamera, lineExposure }, ref) => {
    const canvasRef = useCanvasRef();
    const skImage = useImage(imageUri);

    const barH = Math.round(height * BAR_RATIO);
    const imgH = height - barH;
    const pad = Math.round(width * 0.05);

    const fontCamera = matchFont({
      familyName: Platform.OS === "ios" ? "Helvetica Neue" : "sans-serif",
      fontSize: Math.round(width * 0.032),
      fontWeight: "600",
    });
    const fontExposure = matchFont({
      familyName: Platform.OS === "ios" ? "Helvetica Neue" : "sans-serif",
      fontSize: Math.round(width * 0.026),
    });
    const fontBrand = matchFont({
      familyName: Platform.OS === "ios" ? "Georgia" : "serif",
      fontSize: Math.round(width * 0.026),
    });

    useImperativeHandle(ref, () => ({
      capture: async () => {
        if (!canvasRef.current) throw new Error("Canvas not ready");
        const snapshot = canvasRef.current.makeImageSnapshot();
        const bytes = snapshot.encodeToBytes();
        const b64 = btoa(String.fromCharCode(...bytes));
        const uri = `${FileSystem.cacheDirectory}social_exif_${Date.now()}.jpg`;
        await FileSystem.writeAsStringAsync(uri, b64, {
          encoding: FileSystem.EncodingType.Base64,
        });
        return uri;
      },
    }));

    if (!skImage) return null;

    return (
      <Canvas ref={canvasRef} style={{ width, height }}>
        {/* Photo */}
        <Image image={skImage} x={0} y={0} width={width} height={imgH} fit="cover" />

        {/* EXIF bar */}
        <Rect x={0} y={imgH} width={width} height={barH} color="#0a0a0a" />

        {lineCamera ? (
          <Text
            x={pad}
            y={imgH + barH * 0.45}
            text={lineCamera}
            color="#e8e0d4"
            font={fontCamera}
          />
        ) : null}

        {lineExposure ? (
          <Text
            x={pad}
            y={imgH + barH * 0.82}
            text={lineExposure}
            color="#c8a96e"
            font={fontExposure}
          />
        ) : null}

        <Text
          x={width - pad - 50}
          y={imgH + barH * 0.62}
          text="Fujify"
          color="#5a5450"
          font={fontBrand}
        />
      </Canvas>
    );
  }
);
