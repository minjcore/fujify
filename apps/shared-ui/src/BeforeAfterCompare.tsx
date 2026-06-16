import React, { useState } from "react";
import { Image, LayoutChangeEvent, StyleSheet, View } from "react-native";

interface BeforeAfterCompareProps {
  beforeUri: string;
  afterUri: string;
  height?: number;
}

export function BeforeAfterCompare({ beforeUri, afterUri, height = 320 }: BeforeAfterCompareProps) {
  const [width, setWidth] = useState(1);
  const [split, setSplit] = useState(0.5);

  const onLayout = (event: LayoutChangeEvent) => {
    setWidth(event.nativeEvent.layout.width || 1);
  };

  return (
    <View style={[styles.wrap, { height }]} onLayout={onLayout}>
      <Image source={{ uri: beforeUri }} style={styles.image} resizeMode="cover" />
      <View style={[styles.afterClip, { width: width * split }]}>
        <Image source={{ uri: afterUri }} style={styles.image} resizeMode="cover" />
      </View>
      <View style={[styles.handle, { left: width * split - 1 }]} />
      <View
        style={styles.touchLayer}
        onTouchMove={(e) => {
          const x = e.nativeEvent.locationX;
          const next = Math.max(0.02, Math.min(0.98, x / width));
          setSplit(next);
        }}
      />
    </View>
  );
}

const styles = StyleSheet.create({
  wrap: {
    backgroundColor: "#111",
    borderRadius: 10,
    overflow: "hidden",
    position: "relative",
  },
  image: {
    width: "100%",
    height: "100%",
  },
  afterClip: {
    position: "absolute",
    left: 0,
    top: 0,
    bottom: 0,
    overflow: "hidden",
  },
  handle: {
    position: "absolute",
    top: 0,
    bottom: 0,
    width: 2,
    backgroundColor: "#fff",
  },
  touchLayer: {
    ...StyleSheet.absoluteFillObject,
  },
});

