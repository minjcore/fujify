export type XYPoint = [number, number];

export interface ProcessingSettings {
  temp: number | null;
  tint: number;
  wb_shift_a: number;
  wb_shift_b: number;
  wb_shift_g: number;
  wb_shift_m: number;
  wb_auto: boolean;
  brightness: number;
  contrast: number;
  shadows: number;
  highlights: number;
  wb_pick: XYPoint | null;
  wb_pick_radius: number;
}

export const DEFAULT_SETTINGS: ProcessingSettings = {
  temp: null,
  tint: 0,
  wb_shift_a: 0,
  wb_shift_b: 0,
  wb_shift_g: 0,
  wb_shift_m: 0,
  wb_auto: false,
  brightness: 0,
  contrast: 0,
  shadows: 0,
  highlights: 0,
  wb_pick: null,
  wb_pick_radius: 6,
};

export const PRESETS: Record<string, Partial<ProcessingSettings>> = {
  case01_flower_warm_fix: {
    temp: 5100,
    tint: 3,
    wb_shift_b: 8,
    contrast: 0.12,
    shadows: 0.08,
    highlights: -0.1,
  },
  default_indoor: {
    temp: 5600,
    tint: 2,
    wb_shift_b: -2,
    brightness: -0.01,
    contrast: 0.05,
    shadows: 0.02,
    highlights: -0.12,
  },
};

export function mergeSettings(
  base: ProcessingSettings,
  patch: Partial<ProcessingSettings>,
): ProcessingSettings {
  return { ...base, ...patch };
}

