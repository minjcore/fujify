import React, { forwardRef } from "react";
import { Image, StyleSheet, Text, View } from "react-native";

interface Props {
  imageUri: string;
  width: number;
  height: number;
  lineCamera: string;
  lineExposure: string;
  showBrand?: boolean;
  onReady?: () => void;
}

export const SocialImageComposer = forwardRef<View, Props>(
  ({ imageUri, width, height, lineCamera, lineExposure, showBrand = true, onReady }, ref) => {
    const barH = Math.round(width * 0.13);
    const fontSize = Math.round(width * 0.032);
    const fontSizeSub = Math.round(width * 0.026);

    return (
      <View ref={ref} style={{ width, height, backgroundColor: "#000" }}>
        <Image
          source={{ uri: imageUri }}
          style={{ width, height: height - barH }}
          resizeMode="cover"
          onLoadEnd={onReady}
        />
        <View style={[s.bar, { height: barH, paddingHorizontal: width * 0.05 }]}>
          <View style={s.left}>
            {lineCamera ? (
              <Text style={[s.camera, { fontSize }]} numberOfLines={1}>
                {lineCamera}
              </Text>
            ) : null}
            {lineExposure ? (
              <Text style={[s.exposure, { fontSize: fontSizeSub }]} numberOfLines={1}>
                {lineExposure}
              </Text>
            ) : null}
          </View>
          {showBrand && (
            <Text style={[s.brand, { fontSize: fontSizeSub }]}>Fujify</Text>
          )}
        </View>
      </View>
    );
  }
);

const s = StyleSheet.create({
  bar: {
    backgroundColor: "#0a0a0a",
    flexDirection: "row",
    alignItems: "center",
    justifyContent: "space-between",
  },
  left: { flex: 1, gap: 3 },
  camera: {
    color: "#e8e0d4",
    fontWeight: "600",
    letterSpacing: 0.3,
  },
  exposure: {
    color: "#c8a96e",
    letterSpacing: 0.5,
  },
  brand: {
    color: "#5a5450",
    fontStyle: "italic",
    fontFamily: "serif",
    marginLeft: 12,
  },
});
