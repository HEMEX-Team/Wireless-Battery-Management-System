import React, { useEffect, useMemo, useRef } from 'react';
import Plotly from 'plotly.js-dist-min';

// =============================================================================
// 1. SYNTHETIC DATA GENERATOR
// =============================================================================
//
// 48 h of realistic 3-thermistor BMS data sampled every 20 min:
//   - Ambient ~24 °C with gentle ±1.5 °C daily swing (peak mid-afternoon).
//   - Discharge cycles every ~6 h, parabolic load profile, heat gen ∝ load².
//   - Middle thermistor runs hottest (heat gen > edge cooling).
//   - Slow ±1.5 °C left/right asymmetry drift.
//   - ±0.3 °C white noise per reading.
//
function generateSyntheticSeries({
  hours = 48,
  intervalMin = 20,
  endAt = Date.now(),
} = {}) {
  const samples = Math.floor((hours * 60) / intervalMin);
  const start = endAt - hours * 3_600_000;
  const out = [];

  for (let i = 0; i < samples; i++) {
    const tMs = start + i * intervalMin * 60_000;
    const d = new Date(tMs);
    const tHours = (tMs - start) / 3_600_000;

    // Daily ambient swing — peak around 3 pm (dayFrac = 0.625).
    const dayFrac = (d.getHours() * 60 + d.getMinutes()) / 1440;
    const ambient = 24 + 1.5 * Math.cos((dayFrac - 0.625) * 2 * Math.PI);

    // 6-hour discharge cycle, parabolic load profile peaking mid-cycle.
    const cyclePos = (tHours % 6) / 6;            // 0..1
    const load     = 4 * cyclePos * (1 - cyclePos); // 0..1, peak 1 at cyclePos=0.5
    const heatGen  = 6 * load * load;              // quadratic in load, ~6 °C peak

    // Geometry — mid hotter than the edges.
    const edgeCool = 0.4 * heatGen;
    const midBias  = 1.0;

    // Slow left/right asymmetry drift across the 48 h window.
    const asym = 1.5 * Math.sin((tHours / 48) * 2 * Math.PI * 0.7);

    const noise = () => (Math.random() - 0.5) * 0.6; // ±0.3 °C

    out.push({
      t:     d,
      left:  ambient + heatGen - edgeCool - asym / 2 + noise(),
      mid:   ambient + heatGen + midBias            + noise(),
      right: ambient + heatGen - edgeCool + asym / 2 + noise(),
    });
  }
  return out;
}

// =============================================================================
// 3. HEATMAP COMPONENT
// =============================================================================

const FONT_FAMILY = 'ui-monospace, "SF Mono", Menlo, Consolas, monospace';
const COLOR_TEXT  = '#1f2937';   // gray-800 — matches Tailwind dashboard text
const COLOR_GRID  = '#e5e7eb';   // gray-200
const COLOR_BG    = '#ffffff';

const NX = 80;   // chunky pixel grid — horizontal cells
const NY = 24;   // chunky pixel grid — vertical cells

// Fixed color range so the gradient reads as absolute temperature, not relative.
const Z_MIN = 25;
const Z_MAX = 50;

// Pack background ("normal" / ambient) temperature — anywhere away from a
// thermistor decays toward this value. Sits comfortably below sensor readings
// so the sensor positions stand out as warm spots.
const T_AMBIENT = 27;

// Sensor positions in normalized pack space (x = length, y = depth).
// Slightly inset from the edges; all on the centerline of the pack.
const SENSORS = [
  { x: 0.15, y: 0.50, key: 'left'  },
  { x: 0.50, y: 0.50, key: 'mid'   },
  { x: 0.85, y: 0.50, key: 'right' },
];

// Gaussian bump width — small enough that the three sensors stay
// visually distinct rather than blurring into one band.
const SIGMA_X = 0.13;
const SIGMA_Y = 0.30;

// Plotly 3.x doesn't ship 'Inferno' as a built-in named scale — provide it explicitly.
const INFERNO = [
  [0.0,  '#000004'],
  [0.13, '#1B0C42'],
  [0.25, '#4A0C6B'],
  [0.38, '#781C6D'],
  [0.5,  '#A52C60'],
  [0.63, '#CF4446'],
  [0.75, '#ED6925'],
  [0.88, '#FB9A06'],
  [1.0,  '#FCFFA4'],
];

export default function ThermalHeatmap({
  series,                  // optional Reading[] from a live feed
  showColorbar = true,
  height = 360,
}) {
  // Take only the most recent reading — this is a spatial pack view,
  // not a time series. Falls back to a single synthetic sample so the
  // visualization still renders before live data arrives.
  const latest = useMemo(() => {
    if (series && series.length > 0) {
      return series[series.length - 1];
    }
    const synth = generateSyntheticSeries();
    return synth[synth.length - 1];
  }, [series]);

  // Build the 2-D z matrix from the current reading.
  // Each thermistor contributes a localized Gaussian bump above the ambient
  // pack temperature, so the three sensor positions read as warm spots and
  // areas between them decay back toward T_AMBIENT.
  const { z, x, y } = useMemo(() => {
    const denomX = NX - 1 || 1;
    const denomY = NY - 1 || 1;
    const z = Array.from({ length: NY }, (_, iy) => {
      const yn = iy / denomY;
      const row = new Float32Array(NX);
      for (let ix = 0; ix < NX; ix++) {
        const xn = ix / denomX;
        let t = T_AMBIENT;
        for (const s of SENSORS) {
          const dx = (xn - s.x) / SIGMA_X;
          const dy = (yn - s.y) / SIGMA_Y;
          const w = Math.exp(-0.5 * (dx * dx + dy * dy));
          t += (latest[s.key] - T_AMBIENT) * w;
        }
        row[ix] = t;
      }
      return row;
    });
    const x = Array.from({ length: NX }, (_, i) => (i / denomX) * 100);
    const y = Array.from({ length: NY }, (_, i) => (i / denomY) * 100);
    return { z, x, y };
  }, [latest]);

  const divRef = useRef(null);

  useEffect(() => {
    if (!divRef.current) return;

    const trace = {
      type: 'heatmap',
      z, x, y,
      colorscale: INFERNO,
      zmin: Z_MIN,
      zmax: Z_MAX,
      zsmooth: false,
      hovertemplate:
        'Pack length: %{x:.0f}%<br>Pack depth: %{y:.0f}%<br>%{z:.1f} °C<extra></extra>',
      showscale: showColorbar,
      colorbar: showColorbar ? {
        thickness: 10,
        outlinewidth: 0,
        tickfont: { family: FONT_FAMILY, color: COLOR_TEXT, size: 10 },
        title: {
          text: '°C',
          font: { family: FONT_FAMILY, color: COLOR_TEXT, size: 11 },
        },
        len: 1,
        x: 1.02,
      } : undefined,
    };

    const layout = {
      paper_bgcolor: COLOR_BG,
      plot_bgcolor:  COLOR_BG,
      font:   { family: FONT_FAMILY, color: COLOR_TEXT, size: 11 },
      margin: { l: 60, r: showColorbar ? 60 : 20, t: 10, b: 50 },
      xaxis: {
        title: {
          text: 'Pack length (%)',
          font: { family: FONT_FAMILY, color: COLOR_TEXT, size: 11 },
        },
        tickvals: [0, 25, 50, 75, 100],
        gridcolor:    COLOR_GRID,
        zerolinecolor: COLOR_GRID,
        linecolor:    COLOR_GRID,
        tickcolor:    COLOR_GRID,
        tickfont:     { family: FONT_FAMILY, color: COLOR_TEXT, size: 10 },
        ticks: 'outside',
        ticklen: 4,
      },
      yaxis: {
        title: {
          text: 'Pack depth (%)',
          font: { family: FONT_FAMILY, color: COLOR_TEXT, size: 11 },
        },
        tickvals: [0, 25, 50, 75, 100],
        gridcolor:    COLOR_GRID,
        zerolinecolor: COLOR_GRID,
        linecolor:    COLOR_GRID,
        tickcolor:    COLOR_GRID,
        tickfont:     { family: FONT_FAMILY, color: COLOR_TEXT, size: 10 },
        ticks: 'outside',
        ticklen: 4,
      },
    };

    Plotly.react(divRef.current, [trace], layout, {
      displayModeBar: false,
      responsive: true,
    });
  }, [z, x, y, showColorbar]);

  useEffect(() => {
    const el = divRef.current;
    return () => { if (el) Plotly.purge(el); };
  }, []);

  return (
    <div style={{
      background: COLOR_BG,
      borderRadius: 12,
      border: '1px solid #e5e7eb',
      boxShadow: '0 1px 2px 0 rgba(0, 0, 0, 0.05)',
      padding: 12,
      fontFamily: FONT_FAMILY,
      color: COLOR_TEXT,
    }}>
      <div ref={divRef} style={{ width: '100%', height }} />
    </div>
  );
}
