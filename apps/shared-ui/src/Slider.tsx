import React, { useState } from "react";
import { StyleSheet, Text, View } from "react-native";

interface SliderProps {
  label: string;
  value: number | null;
  min: number;
  max: number;
  step?: number;
  nullLabel?: string;
  onChange: (v: number | null) => void;
}

const THUMB = 22;

export function Slider({
  label,
  value,
  min,
  max,
  step = 0.01,
  nullLabel = "auto",
  onChange,
}: SliderProps) {
  const [trackW, setTrackW] = useState(0);
  const eff = value ?? (min + max) / 2;
  const pct = Math.max(0, Math.min(1, (eff - min) / (max - min)));

  const onTouch = (x: number) => {
    if (!trackW) return;
    const raw = min + (Math.max(0, Math.min(trackW, x)) / trackW) * (max - min);
    const snapped = step ? Math.round(raw / step) * step : raw;
    onChange(parseFloat(Math.max(min, Math.min(max, snapped)).toFixed(4)));
  };

  const thumbLeft = trackW > 0
    ? Math.max(0, Math.min(trackW - THUMB, pct * trackW - THUMB / 2))
    : 0;

  const display =
    value === null
      ? nullLabel
      : step >= 1
      ? Math.round(value).toString()
      : value.toFixed(2);

  return (
    <View style={s.row}>
      <Text style={s.lbl}>{label}</Text>
      <View
        style={s.trackWrap}
        onLayout={(e) => setTrackW(e.nativeEvent.layout.width)}
        onStartShouldSetResponder={() => true}
        onMoveShouldSetResponder={() => true}
        onResponderGrant={(e) => onTouch(e.nativeEvent.locationX)}
        onResponderMove={(e) => onTouch(e.nativeEvent.locationX)}
      >
        <View style={s.track}>
          <View style={[s.fill, { width: `${pct * 100}%` as any }]} />
        </View>
        {trackW > 0 && <View style={[s.thumb, { left: thumbLeft }]} />}
      </View>
      <Text style={s.val}>{display}</Text>
    </View>
  );
}

const s = StyleSheet.create({
  row: {
    flexDirection: "row",
    alignItems: "center",
    gap: 10,
    paddingVertical: 5,
  },
  lbl: { width: 78, fontSize: 12, color: "#a09890" },
  trackWrap: {
    flex: 1,
    height: THUMB,
    justifyContent: "center",
    position: "relative",
  },
  track: {
    height: 3,
    backgroundColor: "#2a2a2a",
    borderRadius: 2,
    overflow: "hidden",
  },
  fill: { height: 3, backgroundColor: "#c8a96e", borderRadius: 2 },
  thumb: {
    position: "absolute",
    width: THUMB,
    height: THUMB,
    borderRadius: THUMB / 2,
    backgroundColor: "#c8a96e",
    top: -THUMB / 2 + 1.5,
    elevation: 4,
  },
  val: { width: 44, fontSize: 11, color: "#c8a96e", textAlign: "right" },
});
