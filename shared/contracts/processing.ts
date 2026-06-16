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

export interface ProcessImageRequest {
  input_path: string;
  output_path?: string;
  compare?: boolean;
  quality?: number;
  preset?: string | null;
  settings?: Partial<ProcessingSettings>;
}

export interface ProcessImageResult {
  ok: boolean;
  input_path: string;
  output_path: string;
  compare_output_path?: string;
  preset?: string | null;
  effective_settings: ProcessingSettings;
  elapsed_ms: number;
  engine: "python";
  error?: string;
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

