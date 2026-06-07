import React, { useState, useEffect, useMemo, useCallback, useRef, lazy, Suspense } from 'react';
import { Battery, BatteryCharging, Zap, AlertTriangle, Activity, Clock, TrendingDown, AlertCircle, Home, BarChart3, RefreshCw, Plus, X, Trash2, LogOut, Layers, Unlink, Repeat, Moon, Gauge, Download, Sliders, Settings } from 'lucide-react';
import { XAxis, YAxis, ResponsiveContainer, Legend, Tooltip, CartesianGrid, AreaChart, Area } from 'recharts';
import { useNavigate } from 'react-router-dom';
import { useAuth } from '../context/AuthContext';
import { apiFetch } from '../lib/api';
import AdminBmsConfig from '../components/AdminBmsConfig';

// Lazy-loaded so Plotly (~3 MB) is split into its own async chunk instead of
// bloating the main bundle. It only loads when a pack-detail view (the only
// place the thermal heatmap renders) is actually opened.
const ThermalHeatmapPlotly = lazy(() => import('../components/charts/ThermalHeatmap'));

// Utility Components
const StatusBadge = ({ status }) => {
  const styles = {
    safe: 'bg-emerald-50 text-emerald-600 border-emerald-200',
    caution: 'bg-amber-50 text-amber-600 border-amber-200',
    alert: 'bg-rose-50 text-rose-600 border-rose-200',
    default: 'bg-gray-50 text-gray-600 border-gray-200'
  };

  return (
    <span className={`px-2.5 py-1 rounded-md text-xs font-semibold border ${styles[status] || styles.default}`}>
      {status.charAt(0).toUpperCase() + status.slice(1)}
    </span>
  );
};

// Small pill for cards whose data is a placeholder preview, not a live device.
const DemoBadge = () => (
  <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded-md text-[10px] font-bold uppercase tracking-wide bg-amber-100 text-amber-700 border border-amber-300">
    <span className="h-1.5 w-1.5 rounded-full bg-amber-500" />
    Demo
  </span>
);

// Format a wattage with a sensible unit (W under 1 kW, kW above).
const formatPower = (watts) => {
  const w = Number(watts) || 0;
  return Math.abs(w) >= 1000 ? `${(w / 1000).toFixed(2)} kW` : `${w.toFixed(0)} W`;
};

// Compact "x ago" for stale/offline timestamps. Returns '' on bad/empty input.
const timeAgo = (iso) => {
  if (!iso) return '';
  const then = new Date(iso).getTime();
  if (Number.isNaN(then)) return '';
  const secs = Math.max(0, Math.round((Date.now() - then) / 1000));
  if (secs < 60) return `${secs}s ago`;
  const mins = Math.round(secs / 60);
  if (mins < 60) return `${mins}m ago`;
  const hrs = Math.round(mins / 60);
  if (hrs < 24) return `${hrs}h ago`;
  return `${Math.round(hrs / 24)}d ago`;
};


// =============================================================================
// Editable full-scale limits for the top dashboard gauges. Voltage derives from
// each pack's cell config (series × cellFullVolts); current and power have no
// rated value in the data model, so they scale against these constants — change
// them in ONE place here to re-calibrate every gauge.
// =============================================================================
const GAUGE_LIMITS = {
  maxAmps: 50,        // current redline (A) — used by the Current and Power gauges
  cellFullVolts: 4.2, // per-cell full-charge voltage → pack voltage full-scale
};

// A themed 180° semicircle gauge (pure SVG, no extra deps). The coloured arc
// sweeps left→right in proportion to |value| / max.
//   tone 'charge' → stays brand-blue (fuller = better, e.g. voltage)
//   tone 'load'   → blue → amber → rose as it approaches the redline (current,
//                   power), like a car tachometer nearing the red zone.
// `format` returns the centre string (may include its own unit, e.g. kW); pass
// unit='' in that case so it isn't doubled.
const GAUGE_ARC = 'M 12 92 A 80 80 0 0 1 172 92'; // r=80, centre (92,92) — top semicircle
// Named ArcGauge to avoid colliding with lucide-react's `Gauge` icon import.
const ArcGauge = ({ value, max, label, unit = '', tone = 'load', format }) => {
  const v = Number(value) || 0;
  const ceiling = max > 0 ? max : 1;
  const frac = Math.max(0, Math.min(1, Math.abs(v) / ceiling));
  const pct = Math.round(frac * 100);

  const color =
    tone === 'charge'
      ? '#2563EB'
      : frac >= 0.9 ? '#E11D48' : frac >= 0.7 ? '#F59E0B' : '#2563EB';

  const fmt = format || ((n) => n.toFixed(1));
  const u = (n) => `${fmt(n)}${unit ? ` ${unit}` : ''}`;

  return (
    <div className="flex flex-col items-center px-3 py-2">
      <span className="text-[11px] font-semibold uppercase tracking-wider text-gray-400">{label}</span>
      <div className="relative mx-auto w-full max-w-[190px]">
        <svg viewBox="0 0 184 100" className="w-full">
          <path d={GAUGE_ARC} fill="none" stroke="#EEF1F5" strokeWidth="13" strokeLinecap="round" />
          <path
            d={GAUGE_ARC}
            fill="none"
            stroke={color}
            strokeWidth="13"
            strokeLinecap="round"
            pathLength="100"
            strokeDasharray={`${frac * 100} 100`}
            style={{ transition: 'stroke-dasharray 0.6s ease, stroke 0.3s ease' }}
          />
        </svg>
        <div className="absolute inset-0 flex flex-col items-center justify-end pb-0.5">
          <span className="text-2xl font-bold leading-none text-gray-900 tabular-nums">
            {fmt(v)}{unit && <span className="ml-0.5 text-sm font-semibold text-gray-400">{unit}</span>}
          </span>
          <span className="mt-1 text-[10px] text-gray-400 tabular-nums">{pct}% of {u(ceiling)}</span>
        </div>
      </div>
    </div>
  );
};

// Compact pack-health pill shown in the gauges panel header.
const AlertsChip = ({ alerts }) => {
  const total = (alerts?.critical || 0) + (alerts?.warnings || 0);
  if (total === 0) {
    return (
      <span className="inline-flex items-center gap-1.5 rounded-full border border-emerald-100 bg-emerald-50 px-2.5 py-1 text-xs font-medium text-emerald-600">
        <span className="h-1.5 w-1.5 rounded-full bg-emerald-500" /> All nominal
      </span>
    );
  }
  return (
    <span
      className={`inline-flex items-center gap-1.5 rounded-full border px-2.5 py-1 text-xs font-semibold ${
        alerts.critical > 0
          ? 'border-rose-100 bg-rose-50 text-rose-600'
          : 'border-amber-100 bg-amber-50 text-amber-600'
      }`}
    >
      <AlertTriangle className="h-3.5 w-3.5" />
      {alerts.critical} critical · {alerts.warnings} warning{alerts.warnings === 1 ? '' : 's'}
    </span>
  );
};

// Blended panel: Voltage / Current / Power gauges in one rounded surface
// (hairline-separated, not three separate cards) plus a pack-health chip.
//   orientation 'row' → three across (top of the summary view)
//   orientation 'col' → stacked vertically (fills the pack-detail column)
const MetricGauges = ({
  voltage, voltageMax, current, currentMax, power, powerMax, alerts,
  orientation = 'row', title = 'Live readings', subtitle = 'Across all active packs',
}) => {
  const body =
    orientation === 'col'
      ? 'grid grid-cols-1 divide-y divide-gray-100 px-2 pb-3 pt-1'
      : 'grid grid-cols-1 divide-y divide-gray-100 px-2 pb-4 pt-1 sm:grid-cols-3 sm:divide-x sm:divide-y-0';
  return (
    <div className="rounded-2xl bg-white shadow-sm ring-1 ring-gray-200/70">
      <div className="flex items-center justify-between px-5 pt-4">
        <div>
          <h2 className="text-sm font-semibold text-gray-900">{title}</h2>
          {subtitle && <p className="text-[11px] text-gray-400">{subtitle}</p>}
        </div>
        {alerts && <AlertsChip alerts={alerts} />}
      </div>
      <div className={body}>
        <ArcGauge label="Voltage" value={voltage} max={voltageMax} unit="V" tone="charge" />
        <ArcGauge label="Current" value={current} max={currentMax} unit="A" tone="load" />
        <ArcGauge label="Power" value={power} max={powerMax} unit="" tone="load" format={formatPower} />
      </div>
    </div>
  );
};

// Build the Voltage/Current/Power gauge inputs for a set of packs. Voltage is
// the mean pack voltage (full-scale from each pack's cell config); current and
// power are pack totals scaled against GAUGE_LIMITS. Pass [pack] for a single
// pack. Returns null for an empty list so callers can skip the panel.
const gaugeInputsFor = (packs) => {
  if (!packs || !packs.length) return null;
  const n = packs.length;
  const num = (x) => parseFloat(x) || 0;
  // Per-pack full-scales: voltage from cell config; current from the pack's own
  // specs (C × Ah × P) when configured, else the global GAUGE_LIMITS fallback.
  const perPack = packs.map((p) => {
    const series = parseInt(p.series) || 3;
    const parallel = parseInt(p.parallel) || 1;
    const voltFull = series * GAUGE_LIMITS.cellFullVolts;
    const cap = num(p.cell_capacity_ah);
    const cRate = num(p.max_discharge_c);
    const ratedA = cap > 0 && cRate > 0 ? cRate * cap * parallel : GAUGE_LIMITS.maxAmps;
    return { voltFull, ratedA };
  });
  return {
    voltage: packs.reduce((s, p) => s + num(p.voltage), 0) / n,
    voltageMax: perPack.reduce((a, b) => a + b.voltFull, 0) / n,
    current: packs.reduce((s, p) => s + num(p.current), 0),
    currentMax: perPack.reduce((a, b) => a + b.ratedA, 0),
    power: packs.reduce((s, p) => s + num(p.voltage) * num(p.current), 0),
    powerMax: perPack.reduce((a, b) => a + b.voltFull * b.ratedA, 0),
  };
};

// Redlines a set of cell specs implies, for the live preview in the spec form.
const computeRedlines = ({ series, parallel, cellCapacityAh, maxDischargeC }) => {
  const s = parseInt(series) || 0;
  const p = parseInt(parallel) || 0;
  const cap = parseFloat(cellCapacityAh) || 0;
  const c = parseFloat(maxDischargeC) || 0;
  const voltFull = s * GAUGE_LIMITS.cellFullVolts;
  const ratedA = cap * c * p;
  return { voltFull, ratedA, powerW: voltFull * ratedA };
};

// Shared per-cell spec inputs + a computed-redlines preview. Used by both the
// Add Pack (manual) flow and the right-click Edit window so they stay in sync.
// `values` carries cell_nominal_voltage / cell_capacity_ah / max_discharge_c;
// onChange receives the merged object.
const PackSpecFields = ({ values, onChange, series, parallel }) => {
  const { voltFull, ratedA, powerW } = computeRedlines({
    series, parallel,
    cellCapacityAh: values.cell_capacity_ah,
    maxDischargeC: values.max_discharge_c,
  });
  const set = (k) => (e) => onChange({ ...values, [k]: e.target.value });
  const fields = [
    { k: 'cell_nominal_voltage', label: 'Nominal', unit: 'V / cell', placeholder: '3.6', step: '0.1' },
    { k: 'cell_capacity_ah', label: 'Capacity', unit: 'Ah / cell', placeholder: '3.5', step: '0.1' },
    { k: 'max_discharge_c', label: 'Max discharge', unit: 'C-rate', placeholder: '2', step: '0.5' },
  ];
  const preview = [
    { v: voltFull, unit: 'V', fmt: (x) => x.toFixed(1), label: 'voltage' },
    { v: ratedA, unit: 'A', fmt: (x) => x.toFixed(0), label: 'current' },
    { v: powerW, unit: '', fmt: (x) => formatPower(x), label: 'power' },
  ];
  return (
    <div className="space-y-3">
      <div className="grid grid-cols-3 gap-3">
        {fields.map(({ k, label, unit, placeholder, step }) => (
          <div key={k}>
            <label className="mb-1 block text-xs font-medium text-gray-600">{label}</label>
            <input
              type="number" min="0" step={step} inputMode="decimal"
              value={values[k] ?? ''}
              onChange={set(k)}
              placeholder={placeholder}
              className="w-full rounded-md border border-gray-300 px-3 py-2 tabular-nums focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
            <span className="mt-1 block text-[10px] text-gray-400">{unit}</span>
          </div>
        ))}
      </div>
      <div className="rounded-lg border border-blue-100 bg-blue-50/60 px-3 py-2.5">
        <div className="mb-1.5 text-[10px] font-semibold uppercase tracking-wider text-blue-700/70">
          Gauge redlines (computed)
        </div>
        <div className="grid grid-cols-3 gap-2 text-center">
          {preview.map(({ v, unit, fmt, label }) => (
            <div key={label}>
              <div className="text-sm font-bold text-gray-900 tabular-nums">
                {v > 0 ? fmt(v) : '—'}
                {unit && v > 0 && <span className="ml-0.5 text-[10px] font-normal text-gray-400">{unit}</span>}
              </div>
              <div className="text-[10px] text-gray-400">{label}</div>
            </div>
          ))}
        </div>
      </div>
    </div>
  );
};

// Shared themed tooltip for the Recharts trend/area charts — keeps the hover
// card on-brand (white, hairline border, coloured dots) instead of the default.
const ChartTooltip = ({ active, payload, label, unit }) => {
  if (!active || !payload?.length) return null;
  return (
    <div className="rounded-lg border border-gray-100 bg-white/95 px-3 py-2 shadow-lg backdrop-blur-sm">
      <div className="mb-1 text-[11px] font-medium text-gray-400">{label}</div>
      {payload.map((p) => (
        <div key={p.dataKey} className="flex items-center gap-2 text-xs">
          <span className="h-2 w-2 rounded-full" style={{ background: p.color || p.stroke }} />
          <span className="text-gray-500">{p.name}</span>
          <span className="ml-auto font-semibold text-gray-900 tabular-nums">
            {p.value}{unit || ''}
          </span>
        </div>
      ))}
    </div>
  );
};

// Battery Grid Component
// Renders an S × P cell layout: S series positions across, P parallel cells
// stacked per column. Parallel cells in a string share voltage in real life,
// so when the backend only delivers one reading per series position we
// replicate it down the column (and when it delivers padded zeros for the
// extra parallel slots we collapse to the real per-series value).
const BatteryGrid = ({ cells, config, series, parallel }) => {
  const s = parseInt(series) || 13;
  const p = parseInt(parallel) || 1;

  // Collapse raw readings into one voltage per series position.
  const seriesCells = useMemo(() => {
    const out = [];
    for (let col = 0; col < s; col++) {
      // Prefer a non-zero reading from any of the p positions for this column.
      let pick = null;
      for (let row = 0; row < p; row++) {
        const c = cells[row * s + col] ?? cells[col];
        if (!c) continue;
        const v = parseFloat(c.value);
        if (!Number.isNaN(v) && v > 0) { pick = c; break; }
        if (!pick) pick = c;
      }
      out.push(pick || { value: '0.00', status: 'safe' });
    }
    return out;
  }, [cells, s, p]);

  const cellStats = useMemo(() => ({
    safe: seriesCells.filter(c => c.status === 'safe').length,
    caution: seriesCells.filter(c => c.status === 'caution').length,
    alert: seriesCells.filter(c => c.status === 'alert').length,
  }), [seriesCells]);

  const cellStyle = (status) => {
    switch (status) {
      case 'safe':
        return 'bg-gradient-to-b from-emerald-50 to-emerald-100/70 text-emerald-800 border-emerald-300/80 shadow-emerald-100';
      case 'caution':
        return 'bg-gradient-to-b from-amber-50 to-amber-100/80 text-amber-800 border-amber-300 shadow-amber-100';
      case 'alert':
        return 'bg-gradient-to-b from-rose-50 to-rose-100/80 text-rose-800 border-rose-300 shadow-rose-100';
      default:
        return 'bg-gray-50 text-gray-500 border-gray-200';
    }
  };

  const terminalColor = (status) => ({
    safe: 'bg-emerald-300/80',
    caution: 'bg-amber-300',
    alert: 'bg-rose-300',
  }[status] || 'bg-gray-300');

  const legendDot = (status) => ({
    safe: 'bg-emerald-400',
    caution: 'bg-amber-400',
    alert: 'bg-rose-400',
  }[status] || 'bg-gray-300');

  const totalCells = s * p;
  const headerConfig = `${s}S${p}P (${totalCells} cell${totalCells === 1 ? '' : 's'})`;
  // Tighten cell height as the parallel stack grows so the whole grid stays compact.
  const cellHeight = p >= 4 ? 'h-7' : p === 3 ? 'h-8' : p === 2 ? 'h-9' : 'h-11';

  return (
    <div className="space-y-3">
      <div className="flex items-center justify-between text-xs text-gray-400 font-medium">
        <span>{config || headerConfig}</span>
        <span className="tabular-nums">{s}S × {p}P</span>
      </div>

      <div
        className="grid gap-1.5"
        style={{ gridTemplateColumns: `repeat(${s}, minmax(0, 1fr))` }}
      >
        {seriesCells.map((cell, col) => (
          <div key={col} className="flex flex-col items-stretch gap-1 min-w-0">
            <div className="text-[9px] text-center text-gray-400 font-semibold leading-none tabular-nums">
              {col + 1}
            </div>
            <div className="relative flex flex-col gap-1">
              {/* Battery terminal nub at top of the column */}
              <div
                className={`absolute -top-1 left-1/2 -translate-x-1/2 w-2.5 h-1 rounded-t ${terminalColor(cell.status)}`}
              />
              {Array.from({ length: p }).map((_, row) => (
                <div
                  key={row}
                  className={`${cellHeight} rounded-md border flex items-center justify-center shadow-sm transition-all ${cellStyle(cell.status)}`}
                  title={`S${col + 1}${p > 1 ? `·P${row + 1}` : ''}: ${cell.value} V`}
                >
                  <span className="text-[10px] font-mono font-bold leading-none tabular-nums">
                    {cell.value}
                  </span>
                </div>
              ))}
            </div>
          </div>
        ))}
      </div>

      <div className="flex items-center gap-4 text-xs text-gray-500">
        {Object.entries(cellStats).map(([status, count]) => (
          <div key={status} className="flex items-center gap-1.5">
            <div className={`w-2 h-2 rounded-full ${legendDot(status)}`}></div>
            <span>{status.charAt(0).toUpperCase() + status.slice(1)} ({count})</span>
          </div>
        ))}
      </div>
    </div>
  );
};

// Enhanced Chart Component - Memoized to prevent unnecessary re-renders
const TrendChart = React.memo(({ data, title, subtitle, dataKeys }) => (
  <div className="rounded-2xl bg-white p-6 shadow-sm ring-1 ring-gray-200/70">
    <div className="mb-4">
      <h2 className="text-sm font-semibold text-gray-900">{title}</h2>
      {subtitle && <p className="mt-0.5 text-xs text-gray-400">{subtitle}</p>}
    </div>

    <div className="h-64">
      {data.length > 0 ? (
        <ResponsiveContainer width="100%" height="100%">
          <AreaChart data={data} margin={{ top: 8, right: 12, left: 0, bottom: 0 }}>
            <defs>
              {dataKeys.map(({ key, color }) => (
                <linearGradient key={key} id={`trend-${key}`} x1="0" y1="0" x2="0" y2="1">
                  <stop offset="0%" stopColor={color} stopOpacity={0.18} />
                  <stop offset="100%" stopColor={color} stopOpacity={0} />
                </linearGradient>
              ))}
            </defs>
            <CartesianGrid strokeDasharray="4 4" stroke="#F1F5F9" vertical={false} />
            <XAxis
              dataKey="time"
              axisLine={false}
              tickLine={false}
              minTickGap={28}
              tick={{ fontSize: 11, fill: '#9CA3AF' }}
            />
            <YAxis
              axisLine={false}
              tickLine={false}
              width={36}
              tick={{ fontSize: 11, fill: '#9CA3AF' }}
            />
            <Tooltip content={<ChartTooltip />} cursor={{ stroke: '#CBD5E1', strokeDasharray: '4 4' }} />
            {dataKeys.length > 1 && (
              <Legend iconType="circle" wrapperStyle={{ fontSize: 12, paddingTop: 8 }} />
            )}
            {dataKeys.map(({ key, color, name }) => (
              <Area
                key={key}
                type="monotone"
                dataKey={key}
                name={name}
                stroke={color}
                strokeWidth={2.5}
                fill={`url(#trend-${key})`}
                dot={false}
                activeDot={{ r: 4, strokeWidth: 2, stroke: '#fff' }}
                connectNulls
                isAnimationActive={false}
              />
            ))}
          </AreaChart>
        </ResponsiveContainer>
      ) : (
        <div className="flex h-full items-center justify-center text-sm text-gray-400">
          <p>No data yet — readings will appear as they're collected.</p>
        </div>
      )}
    </div>
  </div>
));

// Battery % vs Time Chart
const BatteryRangeChart = React.memo(({ data }) => {
  const lastPoint = data[data.length - 1];
  const currentPct = lastPoint ? lastPoint.percentage : 0;

  return (
    <div className="rounded-2xl bg-white p-6 shadow-sm ring-1 ring-gray-200/70">
      <div className="mb-4 flex items-end justify-between">
        <div>
          <h2 className="mb-1 text-xs font-semibold uppercase tracking-wider text-gray-400">Battery Percentage</h2>
          <span className="text-3xl font-bold text-gray-900 tabular-nums">{currentPct}%</span>
        </div>
        <span className="text-[11px] text-gray-400">State of charge over time</span>
      </div>

      <div className="h-72">
        {data.length > 1 ? (
          <ResponsiveContainer width="100%" height="100%">
            <AreaChart data={data} margin={{ top: 10, right: 12, left: 0, bottom: 0 }}>
              <defs>
                <linearGradient id="batteryFill" x1="0" y1="0" x2="0" y2="1">
                  <stop offset="0%" stopColor="#2563EB" stopOpacity={0.28} />
                  <stop offset="100%" stopColor="#2563EB" stopOpacity={0.02} />
                </linearGradient>
              </defs>
              <CartesianGrid strokeDasharray="4 4" stroke="#F1F5F9" vertical={false} />
              <XAxis
                dataKey="time"
                axisLine={false}
                tickLine={false}
                minTickGap={28}
                tick={{ fontSize: 11, fill: '#9CA3AF' }}
              />
              <YAxis
                domain={[0, 100]}
                axisLine={false}
                tickLine={false}
                width={36}
                tick={{ fontSize: 11, fill: '#9CA3AF' }}
                tickFormatter={(v) => `${v}%`}
              />
              <Tooltip content={<ChartTooltip unit="%" />} cursor={{ stroke: '#CBD5E1', strokeDasharray: '4 4' }} />
              <Area
                type="monotone"
                dataKey="percentage"
                name="Battery"
                stroke="#2563EB"
                strokeWidth={2.5}
                fill="url(#batteryFill)"
                dot={false}
                activeDot={{ r: 4, strokeWidth: 2, stroke: '#fff' }}
                isAnimationActive={false}
              />
            </AreaChart>
          </ResponsiveContainer>
        ) : (
          <div className="flex h-full items-center justify-center text-sm text-gray-400">
            Collecting data...
          </div>
        )}
      </div>
    </div>
  );
});

// Add Pack Modal Component
const AddPackModal = ({ isOpen, onClose, onPackCreated }) => {
  const [activeTab, setActiveTab] = useState('pair');
  const [pairingCode, setPairingCode] = useState('');
  const [form, setForm] = useState({ name: '', pack_identifier: '', pairing_code: '', series_count: 3, parallel_count: 1, cell_nominal_voltage: '', cell_capacity_ah: '', max_discharge_c: '' });
  const [error, setError] = useState('');
  const [success, setSuccess] = useState('');
  const [submitting, setSubmitting] = useState(false);

  if (!isOpen) return null;

  const handlePair = async (e) => {
    e.preventDefault();
    const code = pairingCode.trim().toUpperCase();
    if (!code) {
      setError('Please enter a pairing code');
      return;
    }
    setSubmitting(true);
    setError('');
    setSuccess('');
    try {
      const pack = await apiFetch('/v1/packs/claim', {
        method: 'POST',
        body: JSON.stringify({ pairing_code: code }),
      });
      setSuccess(`Paired successfully! "${pack.name}" added to your dashboard.`);
      setPairingCode('');
      onPackCreated();
      setTimeout(() => { setSuccess(''); onClose(); }, 1500);
    } catch (err) {
      if (err.status === 404) {
        setError('No device found with this pairing code. Make sure your pack is powered on and connected.');
      } else if (err.status === 409) {
        setError('This pack is already claimed by another user.');
      } else {
        setError(err.message || 'Failed to pair device');
      }
    } finally {
      setSubmitting(false);
    }
  };

  const handleManualSubmit = async (e) => {
    e.preventDefault();
    if (!form.name.trim() || !form.pack_identifier.trim()) {
      setError('Pack name and identifier are required');
      return;
    }
    setSubmitting(true);
    setError('');
    try {
      await apiFetch('/v1/packs', {
        method: 'POST',
        body: JSON.stringify({
          name: form.name,
          pack_identifier: form.pack_identifier,
          pairing_code: form.pairing_code || form.pack_identifier.toUpperCase(),
          series_count: parseInt(form.series_count) || 3,
          parallel_count: parseInt(form.parallel_count) || 1,
          cell_nominal_voltage: form.cell_nominal_voltage ? parseFloat(form.cell_nominal_voltage) : null,
          cell_capacity_ah: form.cell_capacity_ah ? parseFloat(form.cell_capacity_ah) : null,
          max_discharge_c: form.max_discharge_c ? parseFloat(form.max_discharge_c) : null,
        }),
      });
      setForm({ name: '', pack_identifier: '', pairing_code: '', series_count: 3, parallel_count: 1, cell_nominal_voltage: '', cell_capacity_ah: '', max_discharge_c: '' });
      onPackCreated();
      onClose();
    } catch (err) {
      setError(err.message || 'Failed to create pack');
    } finally {
      setSubmitting(false);
    }
  };

  return (
    <div className="fixed inset-0 bg-black/50 z-50 flex items-center justify-center p-4">
      <div className="bg-white rounded-xl shadow-2xl w-full max-w-md">
        <div className="flex items-center justify-between p-6 border-b border-gray-200">
          <h2 className="text-xl font-bold text-gray-900">Add Battery Pack</h2>
          <button onClick={() => { setError(''); setSuccess(''); onClose(); }} className="p-1 hover:bg-gray-100 rounded-lg">
            <X className="h-5 w-5 text-gray-500" />
          </button>
        </div>

        {/* Tab switcher */}
        <div className="flex border-b border-gray-200">
          <button
            onClick={() => { setActiveTab('pair'); setError(''); setSuccess(''); }}
            className={`flex-1 py-3 text-sm font-medium text-center transition ${
              activeTab === 'pair' ? 'text-blue-600 border-b-2 border-blue-600' : 'text-gray-500 hover:text-gray-700'
            }`}
          >
            Pair Device
          </button>
          <button
            onClick={() => { setActiveTab('manual'); setError(''); setSuccess(''); }}
            className={`flex-1 py-3 text-sm font-medium text-center transition ${
              activeTab === 'manual' ? 'text-blue-600 border-b-2 border-blue-600' : 'text-gray-500 hover:text-gray-700'
            }`}
          >
            Manual Setup
          </button>
        </div>

        <div className="p-6">
          {error && (
            <div className="mb-4 p-3 bg-red-50 border border-red-200 rounded-md text-sm text-red-600">{error}</div>
          )}
          {success && (
            <div className="mb-4 p-3 bg-emerald-50 border border-emerald-200 rounded-md text-sm text-emerald-600">{success}</div>
          )}

          {activeTab === 'pair' ? (
            <form onSubmit={handlePair} className="space-y-4">
              <div>
                <label className="block text-sm font-medium text-gray-700 mb-1">Pairing Code</label>
                <input
                  type="text"
                  value={pairingCode}
                  onChange={(e) => setPairingCode(e.target.value.toUpperCase().replace(/[^A-F0-9]/g, '').slice(0, 6))}
                  placeholder="e.g. 27AF28"
                  maxLength={6}
                  className="w-full px-3 py-2 border border-gray-300 rounded-md focus:outline-none focus:ring-2 focus:ring-blue-500 font-mono text-lg tracking-widest text-center uppercase"
                />
                <p className="mt-2 text-xs text-gray-500">
                  Find the 6-character code on your device label or in the serial monitor output.
                </p>
              </div>
              <button
                type="submit"
                disabled={submitting || pairingCode.length < 6}
                className={`w-full py-3 px-4 rounded-md text-sm font-medium text-white transition ${
                  submitting || pairingCode.length < 6 ? 'bg-blue-300 cursor-not-allowed' : 'bg-blue-600 hover:bg-blue-700'
                }`}
              >
                {submitting ? 'Pairing...' : 'Pair Device'}
              </button>
            </form>
          ) : (
            <form onSubmit={handleManualSubmit} className="space-y-4">
              <div>
                <label className="block text-sm font-medium text-gray-700 mb-1">Pack Name</label>
                <input
                  type="text"
                  value={form.name}
                  onChange={(e) => setForm(f => ({ ...f, name: e.target.value }))}
                  placeholder="e.g. Pack Alpha"
                  className="w-full px-3 py-2 border border-gray-300 rounded-md focus:outline-none focus:ring-2 focus:ring-blue-500"
                />
              </div>
              <div>
                <label className="block text-sm font-medium text-gray-700 mb-1">Pack Identifier</label>
                <input
                  type="text"
                  value={form.pack_identifier}
                  onChange={(e) => setForm(f => ({ ...f, pack_identifier: e.target.value }))}
                  placeholder="e.g. wbms-27af28"
                  className="w-full px-3 py-2 border border-gray-300 rounded-md focus:outline-none focus:ring-2 focus:ring-blue-500"
                />
              </div>
              <div className="grid grid-cols-2 gap-4">
                <div>
                  <label className="block text-sm font-medium text-gray-700 mb-1">Series Count</label>
                  <input
                    type="number"
                    min="1"
                    value={form.series_count}
                    onChange={(e) => setForm(f => ({ ...f, series_count: e.target.value }))}
                    className="w-full px-3 py-2 border border-gray-300 rounded-md focus:outline-none focus:ring-2 focus:ring-blue-500"
                  />
                </div>
                <div>
                  <label className="block text-sm font-medium text-gray-700 mb-1">Parallel Count</label>
                  <input
                    type="number"
                    min="1"
                    value={form.parallel_count}
                    onChange={(e) => setForm(f => ({ ...f, parallel_count: e.target.value }))}
                    className="w-full px-3 py-2 border border-gray-300 rounded-md focus:outline-none focus:ring-2 focus:ring-blue-500"
                  />
                </div>
              </div>
              <div className="p-3 bg-gray-50 rounded-lg text-sm text-gray-600">
                Configuration: <span className="font-semibold">{form.series_count}S{form.parallel_count}P</span> ({(parseInt(form.series_count) || 0) * (parseInt(form.parallel_count) || 0)} cells)
              </div>

              {/* Cell specs → gauge redlines (optional). Shared with the Edit window. */}
              <div className="border-t border-gray-100 pt-4">
                <p className="mb-3 text-xs font-semibold uppercase tracking-wider text-gray-400">
                  Cell specs <span className="font-normal normal-case text-gray-400">(optional — powers the gauges)</span>
                </p>
                <PackSpecFields
                  values={form}
                  onChange={setForm}
                  series={form.series_count}
                  parallel={form.parallel_count}
                />
              </div>

              <button
                type="submit"
                disabled={submitting}
                className={`w-full py-3 px-4 rounded-md text-sm font-medium text-white transition ${
                  submitting ? 'bg-blue-300 cursor-not-allowed' : 'bg-blue-600 hover:bg-blue-700'
                }`}
              >
                {submitting ? 'Creating...' : 'Add Pack'}
              </button>
            </form>
          )}
        </div>
      </div>
    </div>
  );
};

// Pack Specs window — opened from the sidebar (right-click or the specs icon).
// Edits name, S/P config and the per-cell specs that drive the gauge redlines.
const EditPackModal = ({ isOpen, pack, onClose, onSaved }) => {
  const [form, setForm] = useState({});
  const [error, setError] = useState('');
  const [submitting, setSubmitting] = useState(false);

  useEffect(() => {
    if (!pack) return;
    setForm({
      name: pack.name || '',
      series_count: pack.series_count ?? 3,
      parallel_count: pack.parallel_count ?? 1,
      cell_nominal_voltage: pack.cell_nominal_voltage ?? '',
      cell_capacity_ah: pack.cell_capacity_ah ?? '',
      max_discharge_c: pack.max_discharge_c ?? '',
    });
    setError('');
  }, [pack]);

  if (!isOpen || !pack) return null;

  const numOrNull = (v) => (v === '' || v == null ? null : parseFloat(v));

  const handleSubmit = async (e) => {
    e.preventDefault();
    if (!form.name?.trim()) { setError('Pack name is required'); return; }
    setSubmitting(true);
    setError('');
    try {
      await apiFetch(`/v1/packs/${pack.id}`, {
        method: 'PATCH',
        body: JSON.stringify({
          name: form.name.trim(),
          series_count: parseInt(form.series_count) || 1,
          parallel_count: parseInt(form.parallel_count) || 1,
          cell_nominal_voltage: numOrNull(form.cell_nominal_voltage),
          cell_capacity_ah: numOrNull(form.cell_capacity_ah),
          max_discharge_c: numOrNull(form.max_discharge_c),
        }),
      });
      onSaved();
      onClose();
    } catch (err) {
      setError(err.message || 'Failed to update pack');
    } finally {
      setSubmitting(false);
    }
  };

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/50 p-4">
      <div className="w-full max-w-md rounded-2xl bg-white shadow-2xl">
        <div className="flex items-center justify-between border-b border-gray-200 p-6">
          <div>
            <h2 className="text-xl font-bold text-gray-900">Pack specs</h2>
            <p className="font-mono text-xs text-gray-400">{pack.pack_identifier}</p>
          </div>
          <button onClick={onClose} className="rounded-lg p-1 hover:bg-gray-100">
            <X className="h-5 w-5 text-gray-500" />
          </button>
        </div>

        <form onSubmit={handleSubmit} className="space-y-4 p-6">
          {error && (
            <div className="rounded-md border border-red-200 bg-red-50 p-3 text-sm text-red-600">{error}</div>
          )}
          <div>
            <label className="mb-1 block text-sm font-medium text-gray-700">Pack Name</label>
            <input
              type="text"
              value={form.name ?? ''}
              onChange={(e) => setForm((f) => ({ ...f, name: e.target.value }))}
              className="w-full rounded-md border border-gray-300 px-3 py-2 focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>
          <div className="grid grid-cols-2 gap-4">
            <div>
              <label className="mb-1 block text-sm font-medium text-gray-700">Series (S)</label>
              <input
                type="number" min="1"
                value={form.series_count ?? ''}
                onChange={(e) => setForm((f) => ({ ...f, series_count: e.target.value }))}
                className="w-full rounded-md border border-gray-300 px-3 py-2 focus:outline-none focus:ring-2 focus:ring-blue-500"
              />
            </div>
            <div>
              <label className="mb-1 block text-sm font-medium text-gray-700">Parallel (P)</label>
              <input
                type="number" min="1"
                value={form.parallel_count ?? ''}
                onChange={(e) => setForm((f) => ({ ...f, parallel_count: e.target.value }))}
                className="w-full rounded-md border border-gray-300 px-3 py-2 focus:outline-none focus:ring-2 focus:ring-blue-500"
              />
            </div>
          </div>

          <div className="border-t border-gray-100 pt-4">
            <p className="mb-3 text-xs font-semibold uppercase tracking-wider text-gray-400">Cell specs</p>
            <PackSpecFields
              values={form}
              onChange={setForm}
              series={form.series_count}
              parallel={form.parallel_count}
            />
          </div>

          <button
            type="submit"
            disabled={submitting}
            className={`w-full rounded-md px-4 py-3 text-sm font-medium text-white transition ${
              submitting ? 'cursor-not-allowed bg-blue-300' : 'bg-blue-600 hover:bg-blue-700'
            }`}
          >
            {submitting ? 'Saving…' : 'Save specs'}
          </button>
        </form>
      </div>
    </div>
  );
};

// Create / Edit Group Modal
const GroupModal = ({ isOpen, onClose, onSaved, packs }) => {
  const [name, setName] = useState('');
  const [connectionType, setConnectionType] = useState('parallel');
  const [selectedIds, setSelectedIds] = useState([]);
  const [error, setError] = useState('');
  const [submitting, setSubmitting] = useState(false);

  useEffect(() => {
    if (isOpen) {
      setName('');
      setConnectionType('parallel');
      setSelectedIds([]);
      setError('');
    }
  }, [isOpen]);

  if (!isOpen) return null;

  const togglePack = (id) => {
    setSelectedIds(prev => prev.includes(id) ? prev.filter(p => p !== id) : [...prev, id]);
  };

  const handleSubmit = async (e) => {
    e.preventDefault();
    if (!name.trim()) { setError('Group name is required'); return; }
    if (selectedIds.length < 2) { setError('Pick at least 2 packs to combine'); return; }
    setSubmitting(true);
    setError('');
    try {
      await apiFetch('/v1/groups', {
        method: 'POST',
        body: JSON.stringify({ name: name.trim(), connection_type: connectionType, pack_ids: selectedIds }),
      });
      onSaved();
      onClose();
    } catch (err) {
      setError(err.message || 'Failed to create group');
    } finally {
      setSubmitting(false);
    }
  };

  return (
    <div className="fixed inset-0 bg-black/50 z-50 flex items-center justify-center p-4">
      <div className="bg-white rounded-xl shadow-2xl w-full max-w-md max-h-[90vh] flex flex-col">
        <div className="flex items-center justify-between p-6 border-b border-gray-200">
          <h2 className="text-xl font-bold text-gray-900">Combine Packs into Group</h2>
          <button onClick={onClose} className="p-1 hover:bg-gray-100 rounded-lg">
            <X className="h-5 w-5 text-gray-500" />
          </button>
        </div>

        <form onSubmit={handleSubmit} className="p-6 space-y-4 overflow-y-auto">
          {error && (
            <div className="p-3 bg-red-50 border border-red-200 rounded-md text-sm text-red-600">{error}</div>
          )}
          <div>
            <label className="block text-sm font-medium text-gray-700 mb-1">Group Name</label>
            <input
              type="text"
              value={name}
              onChange={(e) => setName(e.target.value)}
              placeholder="e.g. E-Bike Pack"
              className="w-full px-3 py-2 border border-gray-300 rounded-md focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>

          <div>
            <label className="block text-sm font-medium text-gray-700 mb-2">Connection</label>
            <div className="grid grid-cols-2 gap-2">
              {[
                { v: 'parallel', label: 'Parallel', hint: 'Sum currents, avg voltage' },
                { v: 'series', label: 'Series', hint: 'Sum voltages, avg current' },
              ].map(({ v, label, hint }) => (
                <button
                  key={v}
                  type="button"
                  onClick={() => setConnectionType(v)}
                  className={`p-3 border rounded-lg text-left transition ${
                    connectionType === v
                      ? 'border-blue-500 bg-blue-50 text-blue-700'
                      : 'border-gray-200 hover:border-gray-300 text-gray-700'
                  }`}
                >
                  <div className="text-sm font-semibold">{label}</div>
                  <div className="text-[11px] text-gray-500 mt-0.5">{hint}</div>
                </button>
              ))}
            </div>
          </div>

          <div>
            <label className="block text-sm font-medium text-gray-700 mb-2">
              Packs to Combine <span className="text-gray-400">({selectedIds.length} selected)</span>
            </label>
            {packs.length === 0 ? (
              <p className="text-sm text-gray-500 py-4 text-center">No packs available. Add a pack first.</p>
            ) : (
              <div className="space-y-1 max-h-56 overflow-y-auto border border-gray-200 rounded-md p-2">
                {packs.map(p => (
                  <label
                    key={p.id}
                    className={`flex items-center gap-3 p-2 rounded cursor-pointer transition ${
                      selectedIds.includes(p.id) ? 'bg-blue-50' : 'hover:bg-gray-50'
                    }`}
                  >
                    <input
                      type="checkbox"
                      checked={selectedIds.includes(p.id)}
                      onChange={() => togglePack(p.id)}
                      className="h-4 w-4 text-blue-600 rounded"
                    />
                    <Battery className="h-4 w-4 text-blue-500" />
                    <div className="flex-1 min-w-0">
                      <div className="text-sm font-medium text-gray-700 truncate">{p.name}</div>
                      <div className="text-[11px] text-gray-400">{p.series_count}S{p.parallel_count}P · {p.pack_identifier}</div>
                    </div>
                  </label>
                ))}
              </div>
            )}
          </div>

          <button
            type="submit"
            disabled={submitting}
            className={`w-full py-3 px-4 rounded-md text-sm font-medium text-white transition ${
              submitting ? 'bg-blue-300 cursor-not-allowed' : 'bg-blue-600 hover:bg-blue-700'
            }`}
          >
            {submitting ? 'Creating...' : 'Create Group'}
          </button>
        </form>
      </div>
    </div>
  );
};

// Combined Group Card (shows merged readings for grouped packs)
const GroupCard = ({ group, onSeparate, onRemovePack }) => {
  const accentColor = {
    safe: 'bg-emerald-500',
    caution: 'bg-amber-500',
    alert: 'bg-rose-500'
  }[group.status] || 'bg-gray-400';

  const socColor = group.soc > 50 ? 'bg-slate-700' : group.soc > 20 ? 'bg-amber-500' : 'bg-rose-500';
  const sohColor = group.soh > 80 ? 'bg-slate-700' : group.soh > 60 ? 'bg-amber-500' : 'bg-rose-500';

  return (
    <div className={`bg-white rounded-2xl border-2 shadow-sm hover:shadow-md transition-shadow duration-200 overflow-hidden ${group.demo ? 'border-amber-300' : 'border-blue-200'}`}>
      <div className={`h-0.5 ${accentColor}`} />

      <div className="p-5">
        <div className="flex items-center justify-between mb-4">
          <div className="flex items-center gap-3">
            <div className="h-10 w-10 rounded-lg bg-blue-100 flex items-center justify-center">
              <Layers className="h-5 w-5 text-blue-600" />
            </div>
            <div>
              <div className="flex items-center gap-2">
                <h3 className="text-base font-semibold text-gray-900">{group.name}</h3>
                <span className="text-[10px] font-semibold uppercase tracking-wider px-1.5 py-0.5 bg-blue-100 text-blue-700 rounded">
                  {group.connection_type}
                </span>
              </div>
              <p className="text-xs text-gray-400">{(group.members || []).length} packs combined</p>
            </div>
          </div>
          <div className="flex items-center gap-2">
            {group.demo && <DemoBadge />}
            <StatusBadge status={group.status} />
            <button
              onClick={() => onSeparate(group)}
              className="p-1.5 text-gray-400 hover:text-rose-500 hover:bg-rose-50 rounded transition-colors"
              title="Separate group"
            >
              <Unlink className="h-4 w-4" />
            </button>
          </div>
        </div>

        {/* Member chips */}
        {group.members && group.members.length > 0 && (
          <div className="flex flex-wrap gap-1.5 mb-5">
            {group.members.map(m => (
              <div key={m.id} className="group flex items-center gap-1.5 px-2 py-1 bg-gray-50 border border-gray-200 rounded-md text-xs">
                <Battery className="h-3 w-3 text-blue-500" />
                <span className="text-gray-700">{m.name}</span>
                <button
                  onClick={() => onRemovePack(group.id, m.id)}
                  className="opacity-0 group-hover:opacity-100 text-gray-400 hover:text-rose-500 transition"
                  title="Remove from group"
                >
                  <X className="h-3 w-3" />
                </button>
              </div>
            ))}
          </div>
        )}

        {/* SOC / SOH */}
        <div className="grid grid-cols-2 gap-5 mb-5">
          {[
            { label: 'SOC', value: group.soc, barColor: socColor },
            { label: 'SOH', value: group.soh, barColor: sohColor }
          ].map(({ label, value, barColor }) => (
            <div key={label}>
              <div className="flex items-baseline justify-between mb-1.5">
                <span className="text-xs font-medium text-gray-400 uppercase tracking-wide">{label}</span>
                <span className="text-xl font-bold text-gray-900">{value}%</span>
              </div>
              <div className="w-full bg-gray-100 rounded-full h-1.5">
                <div className={`${barColor} h-1.5 rounded-full transition-all duration-300`} style={{ width: `${value}%` }} />
              </div>
            </div>
          ))}
        </div>

        {/* Combined readings */}
        <div className="grid grid-cols-3 gap-3 mb-5">
          {[
            { label: 'Voltage', value: group.voltage, unit: 'V' },
            { label: 'Current', value: group.current, unit: 'A' },
            { label: 'Temp', value: group.temp, unit: '°C' }
          ].map(({ label, value, unit }) => (
            <div key={label} className="bg-gray-50 rounded-lg px-3 py-2.5 text-center">
              <div className="text-base font-bold text-gray-900">{value}<span className="text-xs font-normal text-gray-400 ml-0.5">{unit}</span></div>
              <div className="text-[10px] text-gray-400 font-medium uppercase tracking-wider mt-0.5">{label}</div>
            </div>
          ))}
        </div>

        {/* Combined cell grid */}
        {group.cells && group.cells.length > 0 && (
          <BatteryGrid cells={group.cells} config={group.config} series={group.series} parallel={group.parallel} />
        )}
      </div>
    </div>
  );
};

// =============================================================================
// Helpers shared by the new stats cards
// =============================================================================
const formatRelativeTime = (input) => {
  if (!input) return '—';
  // input may be a "HH:MM:SS" timestamp string from rangeData; treat as today.
  let target;
  if (typeof input === 'string' && /^\d{1,2}:\d{2}/.test(input)) {
    const [h, m, s] = input.split(':').map(Number);
    target = new Date();
    target.setHours(h || 0, m || 0, s || 0, 0);
  } else {
    target = new Date(input);
  }
  const ms = Date.now() - target.getTime();
  const mins = Math.floor(ms / 60000);
  if (mins < 1) return 'just now';
  if (mins < 60) return `${mins}m ago`;
  const hours = Math.floor(mins / 60);
  if (hours < 24) return `${hours}h ago`;
  return `${Math.floor(hours / 24)}d ago`;
};

const formatDuration = (mins) => {
  if (!Number.isFinite(mins) || mins <= 0) return '—';
  if (mins < 60) return `${Math.round(mins)}m`;
  const h = Math.floor(mins / 60);
  const m = Math.round(mins % 60);
  return m > 0 ? `${h}h ${m}m` : `${h}h`;
};

// Range data is throttled to one sample / 5 s by the polling loop.
const SAMPLE_INTERVAL_SEC = 5;

// Least-squares slope (%/sample) over a noisy SOC series. Returns null if
// the series is too short or degenerate.
const regressionSlope = (series) => {
  if (!series || series.length < 4) return null;
  const N = series.length;
  let sumX = 0, sumY = 0, sumXY = 0, sumXX = 0;
  for (let i = 0; i < N; i++) {
    const y = series[i].percentage;
    sumX += i;
    sumY += y;
    sumXY += i * y;
    sumXX += i * i;
  }
  const denom = N * sumXX - sumX * sumX;
  if (denom === 0) return null;
  return (N * sumXY - sumX * sumY) / denom;
};

// Time-to-empty in minutes from a regression slope. Falls back to the
// instantaneous current draw when the SOC trend is too flat or rising,
// so the field shows a useful estimate instead of a perpetual em-dash.
const computeTimeToEmptyMin = (series, currentSoc, currentAmps) => {
  if (currentSoc <= 0) return null;
  const slope = regressionSlope(series); // %/sample, negative when discharging
  if (slope != null && slope < -0.005) {
    const samplesUntilEmpty = currentSoc / -slope;
    return (samplesUntilEmpty * SAMPLE_INTERVAL_SEC) / 60;
  }
  // Fallback: rough estimate using current draw against an assumed pack capacity.
  // Assumes ~20 Ah usable per pack equivalent — tuned to give reasonable numbers
  // for typical e-bike / small EV packs without backend capacity metadata.
  const ASSUMED_CAPACITY_AH = 20;
  const draw = Math.abs(Number(currentAmps) || 0);
  if (draw < 0.2) return null;
  const remainingAh = (currentSoc / 100) * ASSUMED_CAPACITY_AH;
  return (remainingAh / draw) * 60;
};

// Walks an SOC-percentage time series and pulls out (a) the most recent
// charge event, (b) the current discharge slope, and (c) a rough cycle count.
const deriveChargeStats = (series, currentSoc, currentAmps) => {
  if (!series || series.length < 2) {
    return { lastCharged: null, lastChargedTo: null, timeToEmptyMin: null, cycles: 0 };
  }

  // Last charge: find the latest run of consecutive rises and take its peak.
  let lastCharged = null;
  let lastChargedTo = null;
  for (let i = series.length - 1; i > 0; i--) {
    if (series[i].percentage > series[i - 1].percentage + 0.2) {
      let j = i;
      while (j < series.length - 1 && series[j + 1].percentage >= series[j].percentage) j++;
      lastCharged = series[j].time;
      lastChargedTo = series[j].percentage;
      break;
    }
  }

  const timeToEmptyMin = computeTimeToEmptyMin(series, currentSoc, currentAmps);

  // Cycle count: number of distinct rising-edge transitions (very rough).
  let cycles = 0;
  let inCharge = false;
  for (let i = 1; i < series.length; i++) {
    const delta = series[i].percentage - series[i - 1].percentage;
    if (delta > 0.5 && !inCharge) {
      cycles++;
      inCharge = true;
    } else if (delta < -0.2) {
      inCharge = false;
    }
  }

  return { lastCharged, lastChargedTo, timeToEmptyMin, cycles };
};

// Aggregate per-pack DB charge-stats (/charge-stats) into one set for the Summary
// view: sum cycles, take the most-recent charge event, and the soonest time-to-empty.
// Returns null if no pack has stats yet (card then falls back to session-derived).
const aggregateDbChargeStats = (list) => {
  const valid = (list || []).filter(Boolean);
  if (!valid.length) return null;
  let cycles = 0, last_charged = null, last_charged_to = null, time_to_empty_min = null;
  for (const s of valid) {
    cycles += s.cycles || 0;
    if (s.last_charged && (!last_charged || new Date(s.last_charged) > new Date(last_charged))) {
      last_charged = s.last_charged;
      last_charged_to = s.last_charged_to;
    }
    if (s.time_to_empty_min != null) {
      time_to_empty_min = time_to_empty_min == null
        ? s.time_to_empty_min
        : Math.min(time_to_empty_min, s.time_to_empty_min);
    }
  }
  return { last_charged, last_charged_to, time_to_empty_min, cycles };
};

// Pulls notable SOC events from the aggregate range series.
const deriveActivityEvents = (series) => {
  if (!series || series.length < 4) return [];
  const events = [];
  let segStart = 0;
  for (let i = 1; i < series.length; i++) {
    const delta = series[i].percentage - series[segStart].percentage;
    const span  = i - segStart;
    if (Math.abs(delta) >= 3 && span >= 2) {
      const rate = Math.abs(delta) / span; // %/sample
      let label;
      if (delta > 0) label = 'Charged';
      else if (rate < 0.4 && span >= 6) label = 'Idle drain';
      else label = 'Drive';
      events.push({ label, delta, time: series[i].time });
      segStart = i;
    }
  }
  return events.slice(-4).reverse();
};

// =============================================================================
// Charge Stats Card — renders next to the pack configuration card
// =============================================================================
const ChargeStatsCard = ({ pack, chartData, dbStats }) => {
  // Pull out the two primitives the memo actually uses so the dependency list
  // is exact (and lint-clean) without re-running when other pack fields churn.
  const { soc, current } = pack;
  const derived = useMemo(
    () => deriveChargeStats(chartData, soc, current),
    [chartData, soc, current]
  );

  // Prefer real DB-computed stats (passed by the per-pack detail view); fall
  // back to the session-derived SOC series for the aggregate summary card.
  const fromDb = !!dbStats;
  const stats = fromDb
    ? {
        lastCharged: dbStats.last_charged,
        lastChargedTo: dbStats.last_charged_to,
        timeToEmptyMin: dbStats.time_to_empty_min,
        cycles: dbStats.cycles,
      }
    : derived;

  const rows = [
    {
      label: 'Last charged',
      value: stats.lastCharged ? formatRelativeTime(stats.lastCharged) : '—',
      sub: stats.lastCharged ? 'time since charge' : 'no charge logged',
      Icon: Clock,
    },
    {
      label: 'Charged to',
      value: stats.lastChargedTo != null ? `${Number(stats.lastChargedTo).toFixed(0)}%` : '—',
      sub: 'peak SOC',
      Icon: BatteryCharging,
    },
    {
      label: 'Time to empty',
      value: formatDuration(stats.timeToEmptyMin),
      sub: pack.current ? `at ${Number(pack.current).toFixed(1)} A` : 'at current rate',
      Icon: Gauge,
    },
    {
      label: 'Charge cycles',
      value: String(stats.cycles),
      sub: fromDb ? 'from stored history' : 'this session',
      Icon: Repeat,
    },
  ];

  return (
    <div className="bg-white rounded-2xl ring-1 ring-gray-200/70 shadow-sm overflow-hidden">
      <div className="h-0.5 bg-blue-500" />
      <div className="p-5">
        <div className="flex items-center gap-3 mb-5">
          <div className="h-10 w-10 rounded-lg bg-blue-50 flex items-center justify-center">
            <Zap className="h-5 w-5 text-blue-600" />
          </div>
          <div>
            <h3 className="text-base font-semibold text-gray-900">Charge Stats</h3>
            <p className="text-xs text-gray-400">{fromDb ? 'From stored readings' : 'Recent charge activity'}</p>
          </div>
        </div>

        <div className="space-y-3">
          {rows.map(({ label, value, sub, Icon }) => (
            <div
              key={label}
              className="flex items-center justify-between py-2 border-b border-gray-100 last:border-0"
            >
              <div className="flex items-center gap-3">
                <div className="h-8 w-8 rounded-md bg-gray-50 flex items-center justify-center">
                  <Icon className="h-4 w-4 text-gray-500" />
                </div>
                <div>
                  <div className="text-xs font-semibold text-gray-700">{label}</div>
                  <div className="text-[10px] text-gray-400">{sub}</div>
                </div>
              </div>
              <div className="text-base font-bold text-gray-900 font-mono tabular-nums">{value}</div>
            </div>
          ))}
        </div>
      </div>
    </div>
  );
};

// =============================================================================
// Activity Insights Card — renders next to the Pack Groups section
// =============================================================================
const ActivityInsightsCard = ({ rangeData, currentAmps }) => {
  const { timeLeftMin, events } = useMemo(() => {
    if (!rangeData || rangeData.length < 2) {
      return { timeLeftMin: null, events: [] };
    }
    const currentSoc = rangeData[rangeData.length - 1].percentage;
    const timeLeftMin = computeTimeToEmptyMin(rangeData, currentSoc, currentAmps);
    return { timeLeftMin, events: deriveActivityEvents(rangeData) };
  }, [rangeData, currentAmps]);

  const eventStyle = (label) => {
    if (label === 'Charged') return { Icon: BatteryCharging, color: 'text-emerald-600', bg: 'bg-emerald-50' };
    if (label === 'Idle drain') return { Icon: Moon, color: 'text-amber-600', bg: 'bg-amber-50' };
    return { Icon: TrendingDown, color: 'text-rose-600', bg: 'bg-rose-50' };
  };

  return (
    <div className="bg-white rounded-2xl ring-1 ring-gray-200/70 shadow-sm overflow-hidden">
      <div className="h-0.5 bg-emerald-500" />
      <div className="p-5">
        <div className="flex items-center gap-3 mb-5">
          <div className="h-10 w-10 rounded-lg bg-emerald-50 flex items-center justify-center">
            <Activity className="h-5 w-5 text-emerald-600" />
          </div>
          <div>
            <h3 className="text-base font-semibold text-gray-900">Pack Activity</h3>
            <p className="text-xs text-gray-400">Time left & recent events</p>
          </div>
        </div>

        {/* Time left highlight */}
        <div className="bg-gray-50 rounded-lg p-4 mb-5">
          <div className="flex items-center gap-2 mb-1">
            <Clock className="h-3.5 w-3.5 text-gray-400" />
            <div className="text-[10px] text-gray-500 font-semibold uppercase tracking-wider">
              Time left at this rate
            </div>
          </div>
          <div className="text-2xl font-bold text-gray-900 font-mono tabular-nums">
            {formatDuration(timeLeftMin)}
          </div>
        </div>

        {/* Recent events */}
        <div>
          <div className="text-[10px] text-gray-500 font-semibold uppercase tracking-wider mb-2">
            Recent activity
          </div>
          {events.length === 0 ? (
            <p className="text-xs text-gray-400 italic py-2">
              {rangeData && rangeData.length >= 4 ? 'No notable activity recently' : 'Collecting data…'}
            </p>
          ) : (
            <div className="space-y-2">
              {events.map((e, i) => {
                const { Icon, color, bg } = eventStyle(e.label);
                const sign = e.delta > 0 ? '+' : '';
                return (
                  <div
                    key={`${e.time}-${i}`}
                    className="flex items-center justify-between py-1.5"
                  >
                    <div className="flex items-center gap-3">
                      <div className={`h-7 w-7 rounded-md ${bg} flex items-center justify-center`}>
                        <Icon className={`h-3.5 w-3.5 ${color}`} />
                      </div>
                      <div>
                        <div className="text-xs font-semibold text-gray-700">{e.label}</div>
                        <div className="text-[10px] text-gray-400">{e.time}</div>
                      </div>
                    </div>
                    <span className={`text-sm font-mono font-semibold ${e.delta < 0 ? 'text-rose-600' : 'text-emerald-600'}`}>
                      {sign}{e.delta.toFixed(0)}%
                    </span>
                  </div>
                );
              })}
            </div>
          )}
        </div>
      </div>
    </div>
  );
};

// =============================================================================
// Pack Summary Card — reusable individual-pack tile shown both in PackDetail
// and on the Summary & Stats view as a fallback when no groups exist.
// =============================================================================
const PackSummaryCard = ({ pack }) => {
  const accentColor = {
    safe: 'bg-emerald-500',
    caution: 'bg-amber-500',
    alert: 'bg-rose-500'
  }[pack.status] || 'bg-gray-400';

  const socColor = pack.soc > 50 ? 'bg-slate-700' : pack.soc > 20 ? 'bg-amber-500' : 'bg-rose-500';
  const sohColor = pack.soh > 80 ? 'bg-slate-700' : pack.soh > 60 ? 'bg-amber-500' : 'bg-rose-500';

  return (
    <div className={`bg-white rounded-2xl border shadow-sm overflow-hidden ${pack.demo ? 'border-amber-300 ring-1 ring-amber-100' : pack.stale ? 'border-slate-300' : 'border-gray-200'}`}>
      <div className={`h-0.5 ${accentColor}`} />

      <div className="p-5">
        <div className="flex items-center justify-between mb-5">
          <div className="flex items-center gap-3 min-w-0">
            <div className="h-10 w-10 rounded-lg bg-slate-100 flex items-center justify-center flex-shrink-0">
              <Battery className="h-5 w-5 text-slate-600" />
            </div>
            <div className="min-w-0">
              <h3 className="text-base font-semibold text-gray-900 truncate">{pack.name}</h3>
              <p className="text-xs text-gray-400 font-mono truncate">{pack.id}</p>
            </div>
          </div>
          <div className="flex items-center gap-2 flex-shrink-0">
            {pack.demo && <DemoBadge />}
            {!pack.demo && pack.stale && (
              <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded-md text-[10px] font-bold uppercase tracking-wide bg-slate-100 text-slate-600 border border-slate-300">
                Offline
              </span>
            )}
            <StatusBadge status={pack.status} />
          </div>
        </div>

        {/* SOC / SOH */}
        <div className="grid grid-cols-2 gap-5 mb-5">
          {[
            { label: 'SOC', value: pack.soc, barColor: socColor },
            { label: 'SOH', value: pack.soh, barColor: sohColor }
          ].map(({ label, value, barColor }) => (
            <div key={label}>
              <div className="flex items-baseline justify-between mb-1.5">
                <span className="text-xs font-medium text-gray-400 uppercase tracking-wide">{label}</span>
                <span className="text-xl font-bold text-gray-900">{value}%</span>
              </div>
              <div className="w-full bg-gray-100 rounded-full h-1.5">
                <div className={`${barColor} h-1.5 rounded-full transition-all duration-300`} style={{ width: `${value}%` }} />
              </div>
            </div>
          ))}
        </div>

        {/* Electrical readings */}
        <div className="grid grid-cols-3 gap-3 mb-5">
          {[
            { label: 'Voltage', value: pack.voltage, unit: 'V' },
            { label: 'Current', value: pack.current, unit: 'A' },
            { label: 'Temp', value: pack.temp, unit: '°C' }
          ].map(({ label, value, unit }) => (
            <div key={label} className="bg-gray-50 rounded-lg px-3 py-2.5 text-center">
              <div className="text-base font-bold text-gray-900">{value}<span className="text-xs font-normal text-gray-400 ml-0.5">{unit}</span></div>
              <div className="text-[10px] text-gray-400 font-medium uppercase tracking-wider mt-0.5">{label}</div>
            </div>
          ))}
        </div>

        {/* Cell grid */}
        <BatteryGrid cells={pack.cells} config={pack.config} series={pack.series} parallel={pack.parallel} />

        {Array.isArray(pack.protections) && pack.protections.length > 0 && (
          <div className="mt-4 rounded-md border border-rose-200 bg-rose-50 px-2.5 py-2">
            <p className="flex items-center gap-1.5 text-[11px] font-semibold text-rose-700 uppercase tracking-wide">
              <AlertTriangle className="h-3.5 w-3.5 flex-shrink-0" />
              BMS Protection Active
            </p>
            <div className="mt-1.5 flex flex-wrap gap-1.5">
              {pack.protections.map((p) => (
                <span key={p.code} title={p.label}
                  className="inline-flex items-center px-1.5 py-0.5 rounded text-[10px] font-bold bg-rose-100 text-rose-700 border border-rose-300">
                  {p.code}
                </span>
              ))}
            </div>
          </div>
        )}

        {pack.demo && (
          <p className="mt-4 flex items-center gap-1.5 text-[11px] text-amber-600 bg-amber-50 border border-amber-200 rounded-md px-2.5 py-1.5">
            <span className="h-1.5 w-1.5 rounded-full bg-amber-400 flex-shrink-0" />
            Sample data — pair a device to see live readings.
          </p>
        )}
        {!pack.demo && pack.stale && (
          <p className="mt-4 flex items-center gap-1.5 text-[11px] text-slate-500 bg-slate-50 border border-slate-200 rounded-md px-2.5 py-1.5">
            <span className="h-1.5 w-1.5 rounded-full bg-slate-400 flex-shrink-0" />
            Offline — last update {timeAgo(pack.last_update)}.
          </p>
        )}
      </div>
    </div>
  );
};

// Tesla-style Pack Detail
const PackDetail = ({ pack, packId, chartData, thermalHistory }) => {
  // Real charge stats computed by the backend from stored DB readings for this
  // pack. Fetched on open (and when the selected pack changes); null until it
  // arrives, in which case ChargeStatsCard falls back to the live-session derive.
  const [dbStats, setDbStats] = useState(null);
  useEffect(() => {
    if (!packId) { setDbStats(null); return; }
    let cancelled = false;
    apiFetch(`/v1/packs/${packId}/charge-stats`)
      .then((d) => { if (!cancelled) setDbStats(d); })
      .catch(() => { if (!cancelled) setDbStats(null); });
    return () => { cancelled = true; };
  }, [packId]);

  // Per-pack gauge inputs + a single-pack health summary for the gauges panel.
  const gauges = gaugeInputsFor([pack]);
  const packAlerts = {
    critical: pack.status === 'alert' ? 1 : 0,
    warnings: pack.status === 'caution' ? 1 : 0,
  };

  return (
    <div className="space-y-6">
      {/* Battery % chart */}
      <BatteryRangeChart data={chartData} />

      {/* Pack summary + charge stats + live gauges (fills the third column) */}
      <div className="grid grid-cols-1 lg:grid-cols-2 xl:grid-cols-3 gap-6 items-start">
        <PackSummaryCard pack={pack} />

        {/* Charge stats sit next to the pack configuration card */}
        <ChargeStatsCard pack={pack} chartData={chartData} dbStats={dbStats} />

        {gauges && (
          <MetricGauges {...gauges} alerts={packAlerts} orientation="col" subtitle="This pack" />
        )}
      </div>

      {/* Thermal heatmap — full-width, no card. Lazy-loaded (Plotly chunk). */}
      <Suspense
        fallback={
          <div className="rounded-2xl bg-white p-6 text-sm text-gray-400 shadow-sm ring-1 ring-gray-200/70">
            Loading thermal map…
          </div>
        }
      >
        <ThermalHeatmapPlotly series={thermalHistory || []} />
      </Suspense>
    </div>
  );
};

// =============================================================================
// Export Panel — pick a pack + time range, preview rows, download CSV.
// Backed by GET /v1/packs/{id}/readings (json preview / csv stream). Times are
// treated as UTC to match how the backend stores them.
// =============================================================================
const RANGE_PRESETS = [
  { value: '1h', label: 'Last hour', ms: 3600e3 },
  { value: '24h', label: 'Last 24 hours', ms: 86400e3 },
  { value: '7d', label: 'Last 7 days', ms: 7 * 86400e3 },
  { value: 'all', label: 'All time', ms: null },
  { value: 'custom', label: 'Custom range', ms: undefined },
];

const ExportPanel = ({ packs }) => {
  const [packId, setPackId] = useState(packs[0]?.id ?? '');
  const [preset, setPreset] = useState('24h');
  const [customStart, setCustomStart] = useState('');
  const [customEnd, setCustomEnd] = useState('');
  const [preview, setPreview] = useState(null);
  const [loading, setLoading] = useState(false);
  const [downloading, setDownloading] = useState(false);
  const [error, setError] = useState('');

  // Default to the first pack once the list loads (state init misses late data).
  useEffect(() => {
    if (!packId && packs.length) setPackId(packs[0].id);
  }, [packs, packId]);

  const buildRange = () => {
    if (preset === 'all') return {};
    if (preset === 'custom') {
      const r = {};
      if (customStart) r.start = new Date(customStart).toISOString();
      if (customEnd) r.end = new Date(customEnd).toISOString();
      return r;
    }
    const p = RANGE_PRESETS.find((x) => x.value === preset);
    return { start: new Date(Date.now() - p.ms).toISOString() };
  };

  const queryString = (extra) => new URLSearchParams({ ...buildRange(), ...extra }).toString();

  const handlePreview = async () => {
    if (!packId) { setError('Pick a pack first'); return; }
    setLoading(true); setError(''); setPreview(null);
    try {
      const data = await apiFetch(`/v1/packs/${packId}/readings?${queryString({ format: 'json', limit: 100 })}`);
      setPreview(data);
    } catch (err) {
      setError(err.message || 'Failed to load preview');
    } finally {
      setLoading(false);
    }
  };

  const handleDownload = async () => {
    if (!packId) { setError('Pick a pack first'); return; }
    setDownloading(true); setError('');
    try {
      // Raw fetch (not apiFetch) so we can stream the CSV body as a blob.
      const token = localStorage.getItem('wbms_token');
      const res = await fetch(`/v1/packs/${packId}/readings?${queryString({ format: 'csv', limit: 100000 })}`, {
        headers: token ? { Authorization: `Bearer ${token}` } : {},
      });
      if (!res.ok) throw new Error(`Download failed (${res.status})`);
      const blob = await res.blob();
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      const pack = packs.find((p) => p.id === Number(packId));
      a.href = url;
      a.download = `${pack?.pack_identifier || 'pack'}-readings.csv`;
      document.body.appendChild(a); a.click(); a.remove();
      URL.revokeObjectURL(url);
    } catch (err) {
      setError(err.message || 'Download failed');
    } finally {
      setDownloading(false);
    }
  };

  if (!packs.length) {
    return (
      <div className="text-center py-16 text-gray-500">
        <Download className="h-10 w-10 text-gray-300 mx-auto mb-3" />
        Add a pack first to export its data.
      </div>
    );
  }

  return (
    <div className="space-y-6 max-w-5xl">
      <div className="bg-white rounded-xl border border-gray-200 shadow-sm p-6">
        <h2 className="text-lg font-semibold text-gray-900 mb-1">Export pack data</h2>
        <p className="text-sm text-gray-500 mb-5">
          Pick a pack and a time range, preview the readings, then download them as CSV. Times are in UTC.
        </p>

        <div className="grid grid-cols-1 sm:grid-cols-2 gap-4">
          <div>
            <label className="block text-sm font-medium text-gray-700 mb-1">Pack</label>
            <select
              value={packId}
              onChange={(e) => setPackId(Number(e.target.value))}
              className="w-full px-3 py-2 border border-gray-300 rounded-md focus:outline-none focus:ring-2 focus:ring-blue-500 bg-white"
            >
              {packs.map((p) => (
                <option key={p.id} value={p.id}>{p.name} · {p.pack_identifier}</option>
              ))}
            </select>
          </div>
          <div>
            <label className="block text-sm font-medium text-gray-700 mb-1">Time range</label>
            <select
              value={preset}
              onChange={(e) => setPreset(e.target.value)}
              className="w-full px-3 py-2 border border-gray-300 rounded-md focus:outline-none focus:ring-2 focus:ring-blue-500 bg-white"
            >
              {RANGE_PRESETS.map((r) => (
                <option key={r.value} value={r.value}>{r.label}</option>
              ))}
            </select>
          </div>
        </div>

        {preset === 'custom' && (
          <div className="grid grid-cols-1 sm:grid-cols-2 gap-4 mt-4">
            <div>
              <label className="block text-sm font-medium text-gray-700 mb-1">Start (UTC)</label>
              <input
                type="datetime-local"
                value={customStart}
                onChange={(e) => setCustomStart(e.target.value)}
                className="w-full px-3 py-2 border border-gray-300 rounded-md focus:outline-none focus:ring-2 focus:ring-blue-500"
              />
            </div>
            <div>
              <label className="block text-sm font-medium text-gray-700 mb-1">End (UTC)</label>
              <input
                type="datetime-local"
                value={customEnd}
                onChange={(e) => setCustomEnd(e.target.value)}
                className="w-full px-3 py-2 border border-gray-300 rounded-md focus:outline-none focus:ring-2 focus:ring-blue-500"
              />
            </div>
          </div>
        )}

        {error && (
          <div className="mt-4 p-3 bg-red-50 border border-red-200 rounded-md text-sm text-red-600">{error}</div>
        )}

        <div className="flex flex-wrap gap-3 mt-5">
          <button
            onClick={handlePreview}
            disabled={loading}
            className={`px-4 py-2 rounded-md text-sm font-medium border transition ${
              loading ? 'bg-gray-100 text-gray-400 border-gray-200 cursor-not-allowed' : 'bg-white text-gray-700 border-gray-300 hover:bg-gray-50'
            }`}
          >
            {loading ? 'Loading…' : 'Preview'}
          </button>
          <button
            onClick={handleDownload}
            disabled={downloading}
            className={`flex items-center gap-2 px-4 py-2 rounded-md text-sm font-medium text-white transition ${
              downloading ? 'bg-blue-300 cursor-not-allowed' : 'bg-blue-600 hover:bg-blue-700'
            }`}
          >
            <Download className="h-4 w-4" />
            {downloading ? 'Preparing…' : 'Download CSV'}
          </button>
        </div>
      </div>

      {preview && (
        <div className="bg-white rounded-xl border border-gray-200 shadow-sm overflow-hidden">
          <div className="px-6 py-4 border-b border-gray-200 flex items-center justify-between">
            <h3 className="text-sm font-semibold text-gray-900">
              Preview <span className="text-gray-400 font-normal">— {preview.count} row{preview.count === 1 ? '' : 's'} (newest 100)</span>
            </h3>
          </div>
          {preview.count === 0 ? (
            <div className="px-6 py-12 text-center text-gray-400 text-sm">No readings in this range.</div>
          ) : (
            <div className="overflow-x-auto max-h-[28rem]">
              <table className="w-full text-sm">
                <thead className="sticky top-0 bg-gray-50">
                  <tr>
                    {preview.columns.map((c) => (
                      <th key={c} className="text-left py-2.5 px-3 font-medium text-gray-600 whitespace-nowrap">{c}</th>
                    ))}
                  </tr>
                </thead>
                <tbody>
                  {preview.rows.map((row, i) => (
                    <tr key={i} className="border-t border-gray-100 hover:bg-gray-50">
                      {row.map((cell, j) => (
                        <td key={j} className="py-2 px-3 text-gray-700 whitespace-nowrap tabular-nums">
                          {cell === null ? '—' : String(cell)}
                        </td>
                      ))}
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          )}
        </div>
      )}
    </div>
  );
};

// Main Dashboard Component
const Dashboard = () => {
  const { user, logout } = useAuth();
  const navigate = useNavigate();

  const [batteryPacks, setBatteryPacks] = useState([]);
  const [historicalData, setHistoricalData] = useState([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(null);
  const [refreshKey, setRefreshKey] = useState(0);
  const [showAddPack, setShowAddPack] = useState(false);
  const [deletingPackId, setDeletingPackId] = useState(null);
  const [userPacks, setUserPacks] = useState([]);
  const [activeTab, setActiveTab] = useState('summary');
  const [rangeData, setRangeData] = useState([]);
  const [groups, setGroups] = useState([]);
  const [showGroupModal, setShowGroupModal] = useState(false);
  const [editPack, setEditPack] = useState(null); // pack whose specs window is open
  const [selectedPackId, setSelectedPackId] = useState(null);
  const [packRangeData, setPackRangeData] = useState({});
  const [packThermalHistory, setPackThermalHistory] = useState({});
  const [alarms, setAlarms] = useState([]);
  // Real DB-computed charge stats, aggregated across packs, for the Summary view's
  // Charge Stats card (the per-pack detail view fetches its own).
  const [aggChargeStats, setAggChargeStats] = useState(null);
  // Transient banner for failed user actions / background fetches so errors
  // aren't swallowed silently. Cleared on the next successful poll or dismiss.
  const [actionError, setActionError] = useState(null);
  const lastAlarmStates = useRef({});

  // Add this ref to track last update time
  const lastUpdateTime = useRef(null);
  const lastRangeUpdate = useRef(null);
  // Guards the one-time seed of SOC history from the DB (see effect below).
  const seededHistory = useRef(false);

  const handleLogout = () => {
    logout();
    navigate('/');
  };

  // Fetch user's registered packs
  const fetchUserPacks = useCallback(async () => {
    try {
      const data = await apiFetch('/v1/packs');
      setUserPacks(data);
    } catch (err) {
      if (err.status === 401) {
        logout();
        navigate('/login');
        return;
      }
      setActionError(err.message || 'Failed to load your packs');
    }
  }, [logout, navigate]);

  // Delete a pack
  const handleDeletePack = async (packId) => {
    if (!confirm('Are you sure you want to remove this battery pack?')) return;
    setDeletingPackId(packId);
    try {
      await apiFetch(`/v1/packs/${packId}`, { method: 'DELETE' });
      fetchUserPacks();
      setRefreshKey(k => k + 1);
    } catch (err) {
      console.error('Failed to delete pack:', err);
      setActionError(err.message || 'Failed to remove pack');
    } finally {
      setDeletingPackId(null);
    }
  };

  // Fetch groups (combined readings)
  const fetchGroups = useCallback(async () => {
    try {
      const data = await apiFetch('/v1/groups/data/latest');
      setGroups(data.groups || []);
      // A good poll means connectivity recovered — clear any stale action banner.
      setActionError(null);
    } catch (err) {
      if (err.status === 401) {
        logout();
        navigate('/login');
        return;
      }
      setActionError(err.message || 'Failed to load groups');
    }
  }, [logout, navigate]);

  const handleSeparateGroup = async (group) => {
    if (!confirm(`Separate group "${group.name}"? The packs themselves will remain.`)) return;
    try {
      await apiFetch(`/v1/groups/${group.id}`, { method: 'DELETE' });
      // Reset chart history so totals start fresh after the group is broken up.
      setRangeData([]);
      setPackRangeData({});
      lastRangeUpdate.current = null;
      fetchGroups();
    } catch (err) {
      console.error('Failed to separate group:', err);
      setActionError(err.message || 'Failed to separate group');
    }
  };

  const handleRemovePackFromGroup = async (groupId, packId) => {
    try {
      await apiFetch(`/v1/groups/${groupId}/packs/${packId}`, { method: 'DELETE' });
      // Reset chart history so the disconnected pack's contribution is no longer carried over
      setRangeData([]);
      setPackRangeData({});
      lastRangeUpdate.current = null;
      fetchGroups();
    } catch (err) {
      console.error('Failed to remove pack from group:', err);
      setActionError(err.message || 'Failed to remove pack from group');
    }
  };

  // Fetch battery data
  const fetchBatteryData = useCallback(async () => {
    try {
      setError(null);
      const data = await apiFetch('/v1/packs/data/latest');

      setBatteryPacks(data.packs || []);

      // Update historical data with new data point
      const now = new Date();
      const timestamp = now.toLocaleTimeString('en-US', {
        hour: '2-digit',
        minute: '2-digit',
        second: '2-digit',
        hour12: true
      });

      if (data.packs && data.packs.length > 0) {
        // Only update if at least 1 second has passed
        if (!lastUpdateTime.current || now.getTime() - lastUpdateTime.current >= 1000) {
          lastUpdateTime.current = now.getTime();

          const avgVoltage = data.packs.reduce((sum, p) => sum + parseFloat(p.voltage), 0) / data.packs.length;
          const avgCurrent = data.packs.reduce((sum, p) => sum + parseFloat(p.current), 0) / data.packs.length;
          const avgTemp = data.packs.reduce((sum, p) => sum + parseFloat(p.temp), 0) / data.packs.length;

          const newDataPoint = {
            time: timestamp,
            voltage: Number(avgVoltage.toFixed(1)),
            current: Number(avgCurrent.toFixed(1)),
            temperature: Number(avgTemp.toFixed(1)),
            power: Number((avgVoltage * avgCurrent).toFixed(1))
          };

          setHistoricalData(prev => {
            const newData = [...prev, newDataPoint];
            // Keep only last 50 data points for better visibility
            return newData.slice(-50);
          });

          // Track battery % vs time — throttle to once every 5 seconds
          if (!lastRangeUpdate.current || now.getTime() - lastRangeUpdate.current >= 5000) {
            lastRangeUpdate.current = now.getTime();
            const avgSoc = data.packs.reduce((sum, p) => sum + (p.soc || 0), 0) / data.packs.length;

            setRangeData(prev => {
              const next = [...prev, { time: timestamp, percentage: Number(avgSoc.toFixed(1)) }];
              return next.slice(-200);
            });

            // Per-pack SOC history (keyed by pack identifier)
            setPackRangeData(prev => {
              const next = { ...prev };
              data.packs.forEach(p => {
                const existing = next[p.id] || [];
                next[p.id] = [...existing, { time: timestamp, percentage: Number(p.soc) }].slice(-200);
              });
              return next;
            });

            // Per-pack thermal history (left/middle/right). The backend always
            // sends a 3-element `thermistors` array — real per-sensor values
            // when the firmware reports them, otherwise the mean spread across
            // all three (flat = no spatial data). No client-side fabrication.
            const shortTime = now.toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit', hour12: false });
            setPackThermalHistory(prev => {
              const next = { ...prev };
              data.packs.forEach(p => {
                const baseT = parseFloat(p.temp) || 0;
                const t = Array.isArray(p.thermistors) ? p.thermistors : [];
                const l = parseFloat(t[0]?.value) || baseT;
                const m = parseFloat(t[1]?.value) || baseT;
                const r = parseFloat(t[2]?.value) || baseT;
                const existing = next[p.id] || [];
                next[p.id] = [...existing, { t: now, time: shortTime, left: l, middle: m, mid: m, right: r }].slice(-60);
              });
              return next;
            });
          }
        }
      }

      setLoading(false);
    } catch (err) {
      if (err.status === 401) {
        logout();
        navigate('/login');
        return;
      }
      console.error("Error fetching battery data:", err);
      setError(err.message);
      setLoading(false);
    }
  }, [logout, navigate]);

  // Fetch user packs on mount
  useEffect(() => {
    fetchUserPacks();
  }, [fetchUserPacks, refreshKey]);

  // Seed SOC history from real DB readings once packs are known, so Charge
  // Stats and Pack Activity have real history immediately instead of slowly
  // re-accumulating from live polls (which reset on every refresh and left
  // Pack Activity stuck on "Collecting data"). Live polls append after this.
  useEffect(() => {
    if (seededHistory.current || userPacks.length === 0) return;
    seededHistory.current = true;

    const fmt = (iso) =>
      new Date(iso).toLocaleTimeString('en-US', {
        hour: '2-digit', minute: '2-digit', second: '2-digit', hour12: true,
      });

    (async () => {
      const since = new Date(Date.now() - 24 * 3600e3).toISOString();
      const results = await Promise.all(
        userPacks.map((p) =>
          apiFetch(`/v1/packs/${p.id}/readings?start=${since}&limit=500`)
            .then((d) => ({ p, rows: d.rows || [] }))
            .catch(() => null)
        )
      );

      // Per-pack series, keyed by identifier to match the live-poll path.
      const nextPack = {};
      // Aggregate points carry an epoch (t) for correct cross-pack sorting.
      const aggPoints = [];
      results.filter(Boolean).forEach(({ p, rows }) => {
        const series = rows
          .filter((r) => r[1] != null)        // r = [timestamp, soc, ...]
          .map((r) => ({ time: fmt(r[0]), percentage: Number(r[1]) }));
        if (series.length) nextPack[p.pack_identifier] = series;
        rows.forEach((r) => {
          if (r[1] != null) aggPoints.push({ t: new Date(r[0]).getTime(), soc: Number(r[1]) });
        });
      });

      if (Object.keys(nextPack).length) {
        setPackRangeData((prev) => ({ ...nextPack, ...prev }));
      }

      // Aggregate: average SOC of readings sharing the same second, ordered by time.
      if (aggPoints.length) {
        const buckets = new Map();
        aggPoints.forEach(({ t, soc }) => {
          const key = Math.round(t / 1000);
          const b = buckets.get(key) || { sum: 0, n: 0 };
          b.sum += soc; b.n += 1; buckets.set(key, b);
        });
        const agg = [...buckets.entries()]
          .sort((a, b) => a[0] - b[0])
          .map(([sec, { sum, n }]) => ({
            time: fmt(sec * 1000),
            percentage: Number((sum / n).toFixed(1)),
          }))
          .slice(-200);
        setRangeData((prev) => (prev.length ? prev : agg));
      }
    })();
  }, [userPacks]);

  // Real charge stats for the Summary view, aggregated across all packs from the
  // backend (cycles, last charged, charged-to, time-to-empty). Without this the
  // Summary card fell back to a session-derived estimate that was usually empty.
  useEffect(() => {
    if (userPacks.length === 0) { setAggChargeStats(null); return; }
    let cancelled = false;
    Promise.all(
      userPacks.map((p) => apiFetch(`/v1/packs/${p.id}/charge-stats`).catch(() => null))
    ).then((list) => { if (!cancelled) setAggChargeStats(aggregateDbChargeStats(list)); });
    return () => { cancelled = true; };
  }, [userPacks, refreshKey]);

  // Fetch data on mount and set up polling
  useEffect(() => {
    fetchBatteryData();
    fetchGroups();
    const interval = setInterval(() => {
      fetchBatteryData();
      fetchGroups();
    }, 3000);
    return () => clearInterval(interval);
  }, [fetchBatteryData, fetchGroups, refreshKey]);

  // Detect new alarm conditions on each data poll
  useEffect(() => {
    if (!batteryPacks.length) return;
    const now = new Date();
    const newAlarms = [];

    batteryPacks.forEach(pack => {
      const prev = lastAlarmStates.current[pack.id] || {};
      const cellVs = (pack.cells || []).map(c => parseFloat(c.value)).filter(v => !isNaN(v) && v > 0);
      const cellDeltaMv = cellVs.length ? (Math.max(...cellVs) - Math.min(...cellVs)) * 1000 : 0;
      const currentSignals = {
        status: pack.status,
        lowSoc: pack.soc < 20,
        lowSoh: pack.soh < 80,
        highTemp: parseFloat(pack.temp) > 40,
        imbalanced: cellDeltaMv > 600,
        // Digital-twin cross-check: |device SoC − VPS model SoC| ≥ 10 pts.
        socDivergence: pack.soc_divergence != null && Math.abs(pack.soc_divergence) >= 10,
      };

      if (currentSignals.status === 'alert' && prev.status !== 'alert') {
        newAlarms.push({
          id: `${pack.id}-status-${now.getTime()}`,
          time: now.toISOString(),
          packId: pack.id,
          packName: pack.name,
          severity: 'critical',
          type: 'Pack Alert',
          cause: `Pack entered alert state (SOC ${pack.soc}%, SOH ${pack.soh}%, Temp ${pack.temp}°C)`,
        });
      } else if (currentSignals.status === 'caution' && prev.status === 'safe') {
        newAlarms.push({
          id: `${pack.id}-status-${now.getTime()}`,
          time: now.toISOString(),
          packId: pack.id,
          packName: pack.name,
          severity: 'warning',
          type: 'Pack Caution',
          cause: `Pack degraded to caution (SOC ${pack.soc}%, SOH ${pack.soh}%)`,
        });
      }

      if (currentSignals.lowSoc && !prev.lowSoc) {
        newAlarms.push({
          id: `${pack.id}-soc-${now.getTime()}`,
          time: now.toISOString(),
          packId: pack.id,
          packName: pack.name,
          severity: 'critical',
          type: 'Low SOC',
          cause: `State of charge dropped below 20% (${pack.soc}%)`,
        });
      }
      if (currentSignals.lowSoh && !prev.lowSoh) {
        newAlarms.push({
          id: `${pack.id}-soh-${now.getTime()}`,
          time: now.toISOString(),
          packId: pack.id,
          packName: pack.name,
          severity: 'warning',
          type: 'Degraded Health',
          cause: `State of health below 80% (${pack.soh}%)`,
        });
      }
      if (currentSignals.highTemp && !prev.highTemp) {
        newAlarms.push({
          id: `${pack.id}-temp-${now.getTime()}`,
          time: now.toISOString(),
          packId: pack.id,
          packName: pack.name,
          severity: 'critical',
          type: 'Overheating',
          cause: `Temperature above 40°C (${pack.temp}°C)`,
        });
      }
      if (currentSignals.imbalanced && !prev.imbalanced) {
        newAlarms.push({
          id: `${pack.id}-balance-${now.getTime()}`,
          time: now.toISOString(),
          packId: pack.id,
          packName: pack.name,
          severity: 'critical',
          type: 'Cell Imbalance',
          cause: `Cell voltage spread above 600 mV (${cellDeltaMv.toFixed(0)} mV)`,
        });
      }

      const cellAlerts = (pack.cells || []).filter(c => c.status === 'alert').length;
      if (cellAlerts > 0 && !prev.cellAlerts) {
        newAlarms.push({
          id: `${pack.id}-cells-${now.getTime()}`,
          time: now.toISOString(),
          packId: pack.id,
          packName: pack.name,
          severity: 'critical',
          type: 'Cell Out of Range',
          cause: `${cellAlerts} cell(s) reading outside safe voltage range`,
        });
      }
      currentSignals.cellAlerts = cellAlerts > 0;

      // BQ76952 hardware protection faults (OC/OV/UV/SC/OT/UT...) forwarded from
      // the BMS over the live link. Raise one critical alarm per newly-active
      // protection so the website surfaces them when online (not just the AP).
      const prots = Array.isArray(pack.protections) ? pack.protections : [];
      const protKey = prots.map(p => p.code).sort().join(',');
      if (protKey && protKey !== prev.protKey) {
        prots.forEach(p => newAlarms.push({
          id: `${pack.id}-prot-${p.code}-${now.getTime()}`,
          time: now.toISOString(),
          packId: pack.id,
          packName: pack.name,
          severity: 'critical',
          type: `Protection: ${p.code}`,
          cause: `${p.label} protection tripped on the BMS`,
        }));
      }
      currentSignals.protKey = protKey;

      // Digital-twin cross-check: a large gap between the device SoC and the VPS
      // model SoC suggests a sensor/firmware problem. Warn (not critical) on a
      // newly-large divergence so it isn't re-raised every poll.
      if (currentSignals.socDivergence && !prev.socDivergence) {
        newAlarms.push({
          id: `${pack.id}-divergence-${now.getTime()}`,
          time: now.toISOString(),
          packId: pack.id,
          packName: pack.name,
          severity: 'warning',
          type: 'SoC Divergence',
          cause: `Device ${pack.soc}% vs model ${pack.vps_ekf_soc ?? '—'}% (Δ ${pack.soc_divergence > 0 ? '+' : ''}${pack.soc_divergence}%) — possible sensor/firmware drift`,
        });
      }

      lastAlarmStates.current[pack.id] = currentSignals;
    });

    if (newAlarms.length) {
      setAlarms(prev => [...newAlarms, ...prev].slice(0, 200));
    }
  }, [batteryPacks]);

  // Calculate aggregate statistics
  const stats = useMemo(() => {
    if (!batteryPacks.length) {
      return {
        totalPower: 0,
        totalCurrent: 0,
        activePacks: 0,
        alerts: { critical: 0, warnings: 0 },
        avgSoc: 0,
        avgSoh: 0
      };
    }

    const totalCurrent = batteryPacks.reduce((sum, p) => sum + (parseFloat(p.current) || 0), 0);
    // True aggregate power = sum of each pack's own V×I (watts), not the
    // cross-term (ΣV·ΣI/N) the previous formula used.
    const totalPowerW = batteryPacks.reduce(
      (sum, p) => sum + (parseFloat(p.voltage) || 0) * (parseFloat(p.current) || 0),
      0
    );

    const alerts = batteryPacks.reduce((acc, p) => {
      if (p.status === 'alert') acc.critical++;
      if (p.status === 'caution') acc.warnings++;
      return acc;
    }, { critical: 0, warnings: 0 });

    return {
      totalPower: totalPowerW,
      totalCurrent: totalCurrent.toFixed(2),
      activePacks: batteryPacks.length,
      alerts,
      avgSoc: (batteryPacks.reduce((sum, p) => sum + p.soc, 0) / batteryPacks.length).toFixed(0),
      avgSoh: (batteryPacks.reduce((sum, p) => sum + p.soh, 0) / batteryPacks.length).toFixed(0)
    };
  }, [batteryPacks]);

  // Inputs for the top Voltage / Current / Power gauges (aggregate over all
  // active packs). Null when there are no packs so the panel is skipped.
  const gaugeData = useMemo(() => gaugeInputsFor(batteryPacks), [batteryPacks]);

  // (Gauge/MetricGauges and the other stat tiles live at module scope — see
  // above — so they aren't recreated each render, which used to remount the
  // cards on every 3s poll. AlarmHistory and DashboardOverview are invoked as
  // plain functions at their call sites for the same reason; neither uses
  // hooks, so this is safe.)
  const AlarmHistory = ({ alarms, onClear }) => {
    const sevStyle = {
      critical: 'bg-rose-50 text-rose-700 border-rose-200',
      warning: 'bg-amber-50 text-amber-700 border-amber-200',
      info: 'bg-blue-50 text-blue-700 border-blue-200',
    };
    const sevIcon = {
      critical: <AlertTriangle className="h-4 w-4" />,
      warning: <AlertCircle className="h-4 w-4" />,
      info: <AlertCircle className="h-4 w-4" />,
    };

    return (
      <div className="bg-white rounded-lg p-6 shadow-sm border border-gray-200">
        <div className="flex items-center justify-between mb-4">
          <div>
            <h3 className="text-lg font-semibold text-gray-900">Alarm History</h3>
            <p className="text-xs text-gray-500">Events detected since you opened this session</p>
          </div>
          {alarms.length > 0 && (
            <button
              onClick={onClear}
              className="flex items-center gap-2 px-3 py-1.5 text-sm text-gray-600 hover:bg-gray-100 rounded-lg transition-colors"
            >
              <Trash2 className="h-4 w-4" />
              Clear
            </button>
          )}
        </div>

        {alarms.length === 0 ? (
          <div className="text-center py-12">
            <div className="inline-flex h-12 w-12 items-center justify-center rounded-full bg-emerald-50 mb-3">
              <AlertCircle className="h-6 w-6 text-emerald-500" />
            </div>
            <p className="text-gray-500">No alarms — all packs operating normally.</p>
          </div>
        ) : (
          <div className="overflow-x-auto">
            <table className="w-full">
              <thead>
                <tr className="border-b border-gray-200">
                  <th className="text-left py-3 px-4 font-medium text-gray-900 text-sm">Time</th>
                  <th className="text-left py-3 px-4 font-medium text-gray-900 text-sm">Severity</th>
                  <th className="text-left py-3 px-4 font-medium text-gray-900 text-sm">Pack</th>
                  <th className="text-left py-3 px-4 font-medium text-gray-900 text-sm">Type</th>
                  <th className="text-left py-3 px-4 font-medium text-gray-900 text-sm">Cause</th>
                </tr>
              </thead>
              <tbody>
                {alarms.map((a) => (
                  <tr key={a.id} className="border-b border-gray-100 hover:bg-gray-50">
                    <td className="py-3 px-4 text-gray-600 text-sm whitespace-nowrap">
                      {new Date(a.time).toLocaleString('en-US', { hour12: true })}
                    </td>
                    <td className="py-3 px-4">
                      <span className={`inline-flex items-center gap-1.5 px-2 py-0.5 rounded border text-xs font-medium ${sevStyle[a.severity] || sevStyle.info}`}>
                        {sevIcon[a.severity]}
                        {a.severity.charAt(0).toUpperCase() + a.severity.slice(1)}
                      </span>
                    </td>
                    <td className="py-3 px-4 text-gray-900 text-sm font-medium">{a.packName}</td>
                    <td className="py-3 px-4 text-gray-700 text-sm">{a.type}</td>
                    <td className="py-3 px-4 text-gray-600 text-sm">{a.cause}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </div>
    );
  };

  // Dashboard Overview Component
  const DashboardOverview = () => (
    <div className="space-y-8">
      {/* Voltage / Current / Power gauges (blended panel) + pack-health chip */}
      {gaugeData && <MetricGauges {...gaugeData} alerts={stats.alerts} />}

      {/* Battery % vs time */}
      <BatteryRangeChart data={rangeData} />

      {/* Pack Groups (or fallback Pack) · Charge Stats · Pack Activity */}
      {batteryPacks.length > 0 && (
        <div className="grid grid-cols-1 lg:grid-cols-2 xl:grid-cols-3 gap-6 items-start">
          {groups.length > 0 ? (
            <div className="space-y-4">
              <h2 className="text-lg font-semibold text-gray-900">Pack Groups</h2>
              <div className="space-y-6">
                {groups.map((g) => (
                  <GroupCard
                    key={`group-${g.id}`}
                    group={g}
                    onSeparate={handleSeparateGroup}
                    onRemovePack={handleRemovePackFromGroup}
                  />
                ))}
              </div>
            </div>
          ) : (
            <div className="space-y-4">
              <h2 className="text-lg font-semibold text-gray-900">Pack</h2>
              <PackSummaryCard pack={batteryPacks[0]} />
            </div>
          )}
          <div className="space-y-4">
            <h2 className="text-lg font-semibold text-gray-900">Charge Stats</h2>
            <ChargeStatsCard pack={aggregatePack} chartData={rangeData} dbStats={aggChargeStats} />
          </div>
          <div className="space-y-4">
            <h2 className="text-lg font-semibold text-gray-900">Pack Activity</h2>
            <ActivityInsightsCard
              rangeData={rangeData}
              currentAmps={aggregatePack.current}
            />
          </div>
        </div>
      )}

      {!loading && !error && batteryPacks.length === 0 && (
        <div className="text-center py-12">
          <Battery className="h-12 w-12 text-gray-300 mx-auto mb-4" />
          <p className="text-gray-500 mb-2">No battery packs yet</p>
          <p className="text-sm text-gray-400 mb-4">Click the + button to register your first battery pack</p>
          <button
            onClick={() => setShowAddPack(true)}
            className="inline-flex items-center gap-2 px-4 py-2 bg-blue-600 text-white rounded-lg hover:bg-blue-700 transition"
          >
            <Plus className="h-4 w-4" />
            Add Pack
          </button>
        </div>
      )}
    </div>
  );

  // Aggregate pack used by the dashboard-level Charge Stats / Pack Activity
  // cards. Falls back to safe zeros when there are no packs registered.
  const aggregatePack = useMemo(() => {
    if (!batteryPacks || batteryPacks.length === 0) return { soc: 0, current: 0 };
    const avgSoc = batteryPacks.reduce((s, p) => s + (Number(p.soc) || 0), 0) / batteryPacks.length;
    const totalCurrent = batteryPacks.reduce((s, p) => s + (Number(p.current) || 0), 0);
    return { soc: avgSoc, current: totalCurrent };
  }, [batteryPacks]);

  const navItems = [
    { value: 'summary', label: 'Summary & Stats', icon: Home },
    { value: 'charts', label: 'Charts', icon: BarChart3 },
    { value: 'alerts', label: 'Alerts', icon: AlertCircle },
    { value: 'export', label: 'Export', icon: Download },
    // Admin-only BMS console (mirrors the slave AP). Gated by DB role; the
    // backend require_admin is the real enforcement, this just hides the tab.
    ...(user?.role === 'admin' ? [{ value: 'admin', label: 'BMS Admin', icon: Sliders }] : []),
  ];

  return (
    <div className="min-h-screen bg-gray-50 flex">
      {/* Sidebar */}
      <aside className="w-64 bg-white border-r border-gray-200 flex flex-col fixed inset-y-0 left-0 z-30">
        {/* Brand */}
        <div className="px-5 py-5 border-b border-gray-200">
          <div className="flex items-center gap-3">
            <div className="h-10 w-10 bg-gradient-to-br from-blue-500 to-blue-600 rounded-lg flex items-center justify-center shadow">
              <Battery className="h-5 w-5 text-white" />
            </div>
            <div className="min-w-0">
              <h1 className="text-sm font-bold text-gray-900 leading-tight">Battery Management</h1>
              <p className="text-[11px] text-gray-500 leading-tight">Wireless ESP-NOW</p>
            </div>
          </div>
        </div>

        {/* Navigation */}
        <nav className="px-3 py-4 space-y-1">
          {navItems.map(({ value, label, icon: Icon }) => (
            <button
              key={value}
              onClick={() => { setActiveTab(value); setSelectedPackId(null); }}
              className={`w-full flex items-center gap-3 px-3 py-2 text-sm font-medium rounded-lg transition-colors ${
                activeTab === value
                  ? 'bg-blue-50 text-blue-700'
                  : 'text-gray-600 hover:bg-gray-50 hover:text-gray-900'
              }`}
            >
              <Icon className="h-4 w-4" />
              {label}
            </button>
          ))}
        </nav>

        {/* Pack list + Groups */}
        <div className="px-3 py-2 border-t border-gray-200 flex-1 overflow-y-auto">
          <div className="flex items-center justify-between px-2 mb-2">
            <h3 className="text-[11px] font-semibold text-gray-400 uppercase tracking-wider">Battery Packs</h3>
            <button
              onClick={() => setShowAddPack(true)}
              className="p-1 text-blue-600 hover:bg-blue-50 rounded transition-colors"
              title="Add pack"
            >
              <Plus className="h-3.5 w-3.5" />
            </button>
          </div>
          {userPacks.length === 0 ? (
            <p className="px-2 text-xs text-gray-400">No packs yet</p>
          ) : (
            <div className="space-y-1">
              {userPacks.map((pack) => {
                const isActive = activeTab === 'pack' && selectedPackId === pack.pack_identifier;
                return (
                  <div
                    key={pack.id}
                    onClick={() => {
                      setSelectedPackId(pack.pack_identifier);
                      setActiveTab('pack');
                    }}
                    onContextMenu={(e) => { e.preventDefault(); setEditPack(pack); }}
                    title="Right-click to edit specs"
                    className={`group flex items-center gap-2 px-2 py-1.5 rounded-md text-sm cursor-pointer transition-colors ${
                      isActive ? 'bg-blue-50 text-blue-700' : 'hover:bg-gray-50'
                    }`}
                  >
                    <Battery className={`h-3.5 w-3.5 flex-shrink-0 ${isActive ? 'text-blue-600' : 'text-blue-500'}`} />
                    <div className="min-w-0 flex-1">
                      <div className={`font-medium truncate ${isActive ? 'text-blue-700' : 'text-gray-700'}`}>{pack.name}</div>
                      <div className="text-[10px] text-gray-400">{pack.series_count}S{pack.parallel_count}P</div>
                    </div>
                    <button
                      onClick={(e) => { e.stopPropagation(); setEditPack(pack); }}
                      className="opacity-0 group-hover:opacity-100 p-1 text-gray-400 hover:text-blue-600 transition"
                      title="Edit specs"
                    >
                      <Settings className="h-3 w-3" />
                    </button>
                    <button
                      onClick={(e) => { e.stopPropagation(); handleDeletePack(pack.id); }}
                      disabled={deletingPackId === pack.id}
                      className="opacity-0 group-hover:opacity-100 p-1 text-gray-400 hover:text-red-500 transition"
                      title="Remove pack"
                    >
                      <Trash2 className="h-3 w-3" />
                    </button>
                  </div>
                );
              })}
            </div>
          )}

          <div className="flex items-center justify-between px-2 mt-5 mb-2">
            <h3 className="text-[11px] font-semibold text-gray-400 uppercase tracking-wider">Pack Groups</h3>
            <button
              onClick={() => setShowGroupModal(true)}
              disabled={userPacks.length < 2}
              className="p-1 text-blue-600 hover:bg-blue-50 rounded transition-colors disabled:text-gray-300 disabled:hover:bg-transparent"
              title={userPacks.length < 2 ? 'Need 2+ packs to group' : 'Combine packs'}
            >
              <Plus className="h-3.5 w-3.5" />
            </button>
          </div>
          {groups.length === 0 ? (
            <p className="px-2 text-xs text-gray-400">No groups</p>
          ) : (
            <div className="space-y-1">
              {groups.map((g) => (
                <div
                  key={g.id}
                  className="group flex items-center gap-2 px-2 py-1.5 rounded-md hover:bg-gray-50 text-sm"
                >
                  <Layers className="h-3.5 w-3.5 text-blue-600 flex-shrink-0" />
                  <div className="min-w-0 flex-1">
                    <div className="font-medium text-gray-700 truncate">{g.name}</div>
                    <div className="text-[10px] text-gray-400">{g.connection_type} · {(g.members || []).length} packs</div>
                  </div>
                  <button
                    onClick={() => handleSeparateGroup(g)}
                    className="opacity-0 group-hover:opacity-100 p-1 text-gray-400 hover:text-rose-500 transition"
                    title="Separate group"
                  >
                    <Unlink className="h-3 w-3" />
                  </button>
                </div>
              ))}
            </div>
          )}
        </div>

        {/* User footer */}
        {user && (
          <div className="px-3 py-3 border-t border-gray-200">
            <div className="flex items-center gap-2 px-2 py-1.5">
              <div className="h-8 w-8 rounded-full bg-gradient-to-br from-blue-500 to-blue-600 text-white flex items-center justify-center text-xs font-semibold">
                {(user.first_name?.[0] || '') + (user.last_name?.[0] || '')}
              </div>
              <div className="min-w-0 flex-1">
                <div className="text-sm font-medium text-gray-700 truncate">
                  {user.first_name} {user.last_name}
                </div>
              </div>
              <button
                onClick={handleLogout}
                className="p-1.5 text-gray-400 hover:text-red-500 hover:bg-red-50 rounded transition-colors"
                title="Log out"
              >
                <LogOut className="h-4 w-4" />
              </button>
            </div>
          </div>
        )}
      </aside>

      {/* Main content */}
      <div className="flex-1 ml-64 flex flex-col min-h-screen">
        {/* Top bar */}
        <header className="bg-white border-b border-gray-200 px-8 py-4 flex items-center justify-between sticky top-0 z-20">
          <div>
            <h2 className="text-xl font-bold text-gray-900">
              {activeTab === 'pack'
                ? (batteryPacks.find(p => p.id === selectedPackId)?.name || 'Pack Details')
                : navItems.find(n => n.value === activeTab)?.label}
            </h2>
            <p className="text-xs text-gray-500">
              {activeTab === 'pack' ? 'Pack details and live readings' : 'Real-time monitoring of wireless battery packs'}
            </p>
          </div>
          <div className="flex items-center gap-3">
            <button
              onClick={() => setRefreshKey(k => k + 1)}
              className="p-2 hover:bg-gray-100 rounded-lg transition-colors"
              title="Refresh data"
            >
              <RefreshCw className="h-4 w-4 text-gray-600" />
            </button>
            {user && (
              <button
                onClick={handleLogout}
                className="flex items-center gap-1.5 px-3 py-1.5 text-sm font-medium text-red-600 hover:bg-red-50 rounded-lg transition-colors"
                title="Log out"
              >
                <LogOut className="h-4 w-4" />
                Logout
              </button>
            )}
          </div>
        </header>

        {/* Page content */}
        <main className="flex-1 px-8 py-6">
          {actionError && (
            <div className="mb-4 flex items-start justify-between gap-3 rounded-lg border border-red-200 bg-red-50 px-4 py-3 text-sm text-red-700">
              <div className="flex items-start gap-2">
                <AlertCircle className="h-5 w-5 flex-shrink-0" />
                <span>{actionError}</span>
              </div>
              <button
                onClick={() => setActionError(null)}
                className="flex-shrink-0 text-red-400 hover:text-red-600"
                title="Dismiss"
              >
                <X className="h-4 w-4" />
              </button>
            </div>
          )}

          {activeTab === 'summary' && DashboardOverview()}

          {activeTab === 'charts' && (
            <div className="space-y-6">
              <TrendChart
                data={historicalData}
                title="Voltage & Current Trends"
                subtitle="Real-time electrical measurements"
                dataKeys={[
                  { key: 'voltage', color: '#3B82F6', name: 'Voltage (V)' },
                  { key: 'current', color: '#10B981', name: 'Current (A)' }
                ]}
              />
              <TrendChart
                data={historicalData}
                title="Temperature Monitoring"
                subtitle="Battery pack temperature over time"
                dataKeys={[
                  { key: 'temperature', color: '#F59E0B', name: 'Temperature (°C)' }
                ]}
              />
              <TrendChart
                data={historicalData}
                title="Power Output"
                subtitle="Calculated power (V × A)"
                dataKeys={[
                  { key: 'power', color: '#8B5CF6', name: 'Power (W)' }
                ]}
              />
            </div>
          )}

          {activeTab === 'alerts' && AlarmHistory({ alarms, onClear: () => setAlarms([]) })}

          {activeTab === 'export' && <ExportPanel packs={userPacks} />}
          {activeTab === 'admin' && user?.role === 'admin' && <AdminBmsConfig packs={userPacks} />}

          {activeTab === 'pack' && (() => {
            const pack = batteryPacks.find(p => p.id === selectedPackId);
            if (!pack) {
              return (
                <div className="text-center py-12 text-gray-500">
                  Pack not found or no live data yet.
                </div>
              );
            }
            // charge-stats endpoint needs the NUMERIC pack id; selectedPackId is
            // the identifier string, so map it via userPacks.
            const numericId = userPacks.find(u => u.pack_identifier === selectedPackId)?.id;
            return <PackDetail pack={pack} packId={numericId} chartData={packRangeData[pack.id] || []} thermalHistory={packThermalHistory[pack.id] || []} />;
          })()}
        </main>
      </div>

      {/* Floating Add Pack Button */}
      <button
        onClick={() => setShowAddPack(true)}
        className="fixed bottom-8 right-8 h-14 w-14 bg-blue-600 hover:bg-blue-700 text-white rounded-full shadow-lg hover:shadow-xl transition-all flex items-center justify-center z-40"
        title="Add battery pack"
      >
        <Plus className="h-6 w-6" />
      </button>

      {/* Add Pack Modal */}
      <AddPackModal
        isOpen={showAddPack}
        onClose={() => setShowAddPack(false)}
        onPackCreated={() => {
          fetchUserPacks();
          setRefreshKey(k => k + 1);
        }}
      />

      {/* Create Group Modal */}
      <GroupModal
        isOpen={showGroupModal}
        onClose={() => setShowGroupModal(false)}
        onSaved={fetchGroups}
        packs={userPacks}
      />

      {/* Pack Specs (edit) Modal — opened via right-click / specs icon */}
      <EditPackModal
        isOpen={!!editPack}
        pack={editPack}
        onClose={() => setEditPack(null)}
        onSaved={() => {
          fetchUserPacks();
          setRefreshKey((k) => k + 1);
        }}
      />
    </div>
  );
};

export default Dashboard;