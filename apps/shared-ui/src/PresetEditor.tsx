import React from "react";
import { StyleSheet, View } from "react-native";
import { Slider } from "./Slider";

export interface ProcessingSettingsLike {
  temp: number | null;
  tint: number;
  wb_shift_b: number;
  contrast: number;
  highlights: number;
  brightness: number;
  shadows: number;
}

interface PresetEditorProps<T extends ProcessingSettingsLike> {
  settings: T;
  presets?: Record<string, Partial<T>>;
  onChange: (next: Partial<T>) => void;
}

export function PresetEditor<T extends ProcessingSettingsLike>({
  settings,
  onChange,
}: PresetEditorProps<T>) {
  return (
    <View style={s.wrap}>
      <Slider
        label="Temp"
        value={settings.temp}
        min={3200}
        max={8000}
        step={100}
        nullLabel="auto"
        onChange={(v) => onChange({ temp: v } as Partial<T>)}
      />
      <Slider
        label="Tint"
        value={settings.tint}
        min={-20}
        max={20}
        step={1}
        onChange={(v) => onChange({ tint: (v ?? 0) as T["tint"] } as Partial<T>)}
      />
      <Slider
        label="WB Shift B"
        value={settings.wb_shift_b}
        min={-20}
        max={20}
        step={1}
        onChange={(v) => onChange({ wb_shift_b: (v ?? 0) as T["wb_shift_b"] } as Partial<T>)}
      />
      <Slider
        label="Brightness"
        value={settings.brightness}
        min={-1}
        max={1}
        step={0.05}
        onChange={(v) => onChange({ brightness: (v ?? 0) as T["brightness"] } as Partial<T>)}
      />
      <Slider
        label="Contrast"
        value={settings.contrast}
        min={-1}
        max={1}
        step={0.05}
        onChange={(v) => onChange({ contrast: (v ?? 0) as T["contrast"] } as Partial<T>)}
      />
      <Slider
        label="Shadows"
        value={settings.shadows}
        min={-1}
        max={1}
        step={0.05}
        onChange={(v) => onChange({ shadows: (v ?? 0) as T["shadows"] } as Partial<T>)}
      />
      <Slider
        label="Highlights"
        value={settings.highlights}
        min={-1}
        max={1}
        step={0.05}
        onChange={(v) => onChange({ highlights: (v ?? 0) as T["highlights"] } as Partial<T>)}
      />
    </View>
  );
}

const s = StyleSheet.create({
  wrap: { gap: 2 },
});
