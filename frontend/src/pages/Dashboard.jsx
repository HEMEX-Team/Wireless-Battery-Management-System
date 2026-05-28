import React, { useState, useEffect, useMemo, useCallback, useRef } from 'react';
import { Battery, BatteryCharging, Zap, AlertTriangle, Activity, History, Clock, TrendingDown, AlertCircle, Home, BarChart3, RefreshCw, Plus, X, Trash2, LogOut, Layers, Unlink, Repeat, Moon, Gauge } from 'lucide-react';
import { LineChart, Line, XAxis, YAxis, ResponsiveContainer, Legend, Tooltip, CartesianGrid, AreaChart, Area } from 'recharts';
import { useNavigate } from 'react-router-dom';
import { useAuth } from '../context/AuthContext';
import { apiFetch } from '../lib/api';
import ThermalHeatmapPlotly from '../components/charts/ThermalHeatmap';

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

const LoadingSpinner = () => (
  <div className="flex items-center justify-center p-8">
    <RefreshCw className="h-8 w-8 text-blue-500 animate-spin" />
  </div>
);

const ErrorMessage = ({ message }) => (
  <div className="bg-red-50 border border-red-200 rounded-lg p-4 text-red-700">
    <AlertCircle className="h-5 w-5 inline mr-2" />
    {message}
  </div>
);

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
  <div className="bg-white rounded-lg p-6 shadow-sm border border-gray-200">
    <div className="mb-6">
      <h2 className="text-lg font-semibold text-gray-900 mb-2">{title}</h2>
      {subtitle && <p className="text-sm text-gray-500">{subtitle}</p>}
    </div>

    <div className="h-64">
      {data.length > 0 ? (
        <ResponsiveContainer width="100%" height="100%">
          <LineChart data={data} margin={{ top: 5, right: 30, left: 20, bottom: 5 }}>
            <CartesianGrid strokeDasharray="3 3" stroke="#E5E7EB" />
            <XAxis 
              dataKey="time" 
              axisLine={false}
              tickLine={false}
              tick={{ fontSize: 12, fill: '#6B7280' }}
            />
            <YAxis 
              axisLine={false}
              tickLine={false}
              tick={{ fontSize: 12, fill: '#6B7280' }}
            />
            <Tooltip 
              contentStyle={{ 
                backgroundColor: 'rgba(255, 255, 255, 0.95)', 
                border: '1px solid #E5E7EB',
                borderRadius: '8px'
              }}
            />
            <Legend />
            {dataKeys.map(({ key, color, name }) => (
              <Line 
                key={key}
                type="monotone" 
                dataKey={key} 
                stroke={color} 
                strokeWidth={2}
                name={name}
                dot={false}
                activeDot={{ r: 6 }}
                connectNulls={false}
                isAnimationActive={false}
              />
            ))}
          </LineChart>
        </ResponsiveContainer>
      ) : (
        <div className="flex items-center justify-center h-full text-gray-400">
          <p>No data available yet. Data will appear as it's collected.</p>
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
    <div className="bg-white rounded-xl p-6 shadow-sm border border-gray-200">
      <div className="mb-6">
        <h2 className="text-xs font-semibold text-gray-500 uppercase tracking-wider mb-1">Battery Percentage</h2>
        <span className="text-3xl font-bold text-gray-900">{currentPct}%</span>
      </div>

      <div className="h-72">
        {data.length > 1 ? (
          <ResponsiveContainer width="100%" height="100%">
            <AreaChart data={data} margin={{ top: 10, right: 20, left: 0, bottom: 5 }}>
              <defs>
                <linearGradient id="batteryFill" x1="0" y1="0" x2="0" y2="1">
                  <stop offset="0%" stopColor="#3B82F6" stopOpacity={0.35} />
                  <stop offset="100%" stopColor="#3B82F6" stopOpacity={0.02} />
                </linearGradient>
              </defs>
              <CartesianGrid strokeDasharray="3 3" stroke="#E5E7EB" vertical={false} />
              <XAxis
                dataKey="time"
                axisLine={false}
                tickLine={false}
                tick={{ fontSize: 11, fill: '#6B7280' }}
                label={{ value: 'Time', position: 'insideBottom', offset: -2, fill: '#6B7280', fontSize: 11 }}
              />
              <YAxis
                domain={[0, 100]}
                axisLine={false}
                tickLine={false}
                tick={{ fontSize: 11, fill: '#6B7280' }}
                tickFormatter={(v) => `${v}%`}
              />
              <Tooltip
                contentStyle={{
                  backgroundColor: 'rgba(255, 255, 255, 0.95)',
                  border: '1px solid #E5E7EB',
                  borderRadius: '8px',
                  color: '#111827'
                }}
                formatter={(v) => [`${v}%`, 'Battery']}
              />
              <Area
                type="monotone"
                dataKey="percentage"
                stroke="#3B82F6"
                strokeWidth={2}
                fill="url(#batteryFill)"
                isAnimationActive={false}
              />
            </AreaChart>
          </ResponsiveContainer>
        ) : (
          <div className="flex items-center justify-center h-full text-gray-400 text-sm">
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
  const [form, setForm] = useState({ name: '', pack_identifier: '', pairing_code: '', series_count: 3, parallel_count: 1 });
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
        }),
      });
      setForm({ name: '', pack_identifier: '', pairing_code: '', series_count: 3, parallel_count: 1 });
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
    <div className="bg-white rounded-xl border-2 border-blue-200 shadow-sm hover:shadow-md transition-shadow duration-200 overflow-hidden">
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

// Pulls notable SOC events from the aggregate range series.
const deriveActivityEvents = (series) => {
  if (!series || series.length < 4) return [];
  const events = [];
  let segStart = 0;
  for (let i = 1; i < series.length; i++) {
    const delta = series[i].percentage - series[segStart].percentage;
    const span  = i - segStart;
    if (Math.abs(delta) >= 4 && span >= 2) {
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
const ChargeStatsCard = ({ pack, chartData }) => {
  const stats = useMemo(
    () => deriveChargeStats(chartData, pack.soc, pack.current),
    [chartData, pack.soc, pack.current]
  );

  const rows = [
    {
      label: 'Last charged',
      value: stats.lastCharged ? formatRelativeTime(stats.lastCharged) : '—',
      sub: stats.lastCharged ? 'time since charge' : 'no charge logged',
      Icon: Clock,
    },
    {
      label: 'Charged to',
      value: stats.lastChargedTo != null ? `${stats.lastChargedTo.toFixed(0)}%` : '—',
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
      sub: 'this session',
      Icon: Repeat,
    },
  ];

  return (
    <div className="bg-white rounded-xl border border-gray-200 shadow-sm overflow-hidden">
      <div className="h-0.5 bg-blue-500" />
      <div className="p-5">
        <div className="flex items-center gap-3 mb-5">
          <div className="h-10 w-10 rounded-lg bg-blue-50 flex items-center justify-center">
            <Zap className="h-5 w-5 text-blue-600" />
          </div>
          <div>
            <h3 className="text-base font-semibold text-gray-900">Charge Stats</h3>
            <p className="text-xs text-gray-400">Recent charge activity</p>
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
    <div className="bg-white rounded-xl border border-gray-200 shadow-sm overflow-hidden">
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
            <p className="text-xs text-gray-400 italic py-2">Collecting data...</p>
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
    <div className="bg-white rounded-xl border border-gray-200 shadow-sm overflow-hidden">
      <div className={`h-0.5 ${accentColor}`} />

      <div className="p-5">
        <div className="flex items-center justify-between mb-5">
          <div className="flex items-center gap-3">
            <div className="h-10 w-10 rounded-lg bg-slate-100 flex items-center justify-center">
              <Battery className="h-5 w-5 text-slate-600" />
            </div>
            <div>
              <h3 className="text-base font-semibold text-gray-900">{pack.name}</h3>
              <p className="text-xs text-gray-400 font-mono">{pack.id}</p>
            </div>
          </div>
          <StatusBadge status={pack.status} />
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
      </div>
    </div>
  );
};

// Tesla-style Pack Detail
const PackDetail = ({ pack, chartData, thermalHistory }) => {
  return (
    <div className="space-y-6">
      {/* Battery % chart */}
      <BatteryRangeChart data={chartData} />

      {/* Compact pack summary + cell grid (matches GroupCard sizing) */}
      <div className="grid grid-cols-1 lg:grid-cols-2 xl:grid-cols-3 gap-6">
        <PackSummaryCard pack={pack} />

        {/* Charge stats sit next to the pack configuration card */}
        <ChargeStatsCard pack={pack} chartData={chartData} />
      </div>

      {/* Thermal heatmap — full-width, no card */}
      <ThermalHeatmapPlotly series={thermalHistory || []} />
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
  const [selectedPackId, setSelectedPackId] = useState(null);
  const [packRangeData, setPackRangeData] = useState({});
  const [packThermalHistory, setPackThermalHistory] = useState({});
  const [alarms, setAlarms] = useState([]);
  const lastAlarmStates = useRef({});

  // Add this ref to track last update time
  const lastUpdateTime = useRef(null);
  const lastRangeUpdate = useRef(null);

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
      }
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
    } finally {
      setDeletingPackId(null);
    }
  };

  // Fetch groups (combined readings)
  const fetchGroups = useCallback(async () => {
    try {
      const data = await apiFetch('/v1/groups/data/latest');
      setGroups(data.groups || []);
    } catch (err) {
      if (err.status === 401) {
        logout();
        navigate('/login');
      }
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

            // Per-pack thermal history (left/middle/right). Falls back to pack.temp
            // when the firmware hasn't published per-thermistor readings yet.
            const shortTime = now.toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit', hour12: false });
            setPackThermalHistory(prev => {
              const next = { ...prev };
              data.packs.forEach(p => {
                const baseT = parseFloat(p.temp) || 0;
                let l = baseT, m = baseT, r = baseT;
                if (Array.isArray(p.thermistors) && p.thermistors.length === 3) {
                  l = parseFloat(p.thermistors[0]?.value) || baseT;
                  m = parseFloat(p.thermistors[1]?.value) || baseT;
                  r = parseFloat(p.thermistors[2]?.value) || baseT;
                } else {
                  // Synthesize a small spatial variation around the avg so the heatmap
                  // is visually informative until the per-thermistor data flows in.
                  const jitter = () => (Math.random() - 0.5) * 1.5;
                  l = baseT + jitter();
                  m = baseT + jitter();
                  r = baseT + jitter();
                }
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

    const totalVoltage = batteryPacks.reduce((sum, p) => sum + parseFloat(p.voltage), 0);
    const totalCurrent = batteryPacks.reduce((sum, p) => sum + parseFloat(p.current), 0);
    const totalPower = (totalVoltage * totalCurrent / batteryPacks.length) / 1000; // kW
    
    const alerts = batteryPacks.reduce((acc, p) => {
      if (p.status === 'alert') acc.critical++;
      if (p.status === 'caution') acc.warnings++;
      return acc;
    }, { critical: 0, warnings: 0 });

    return {
      totalPower: totalPower.toFixed(2),
      totalCurrent: totalCurrent.toFixed(2),
      activePacks: batteryPacks.length,
      alerts,
      avgSoc: (batteryPacks.reduce((sum, p) => sum + p.soc, 0) / batteryPacks.length).toFixed(0),
      avgSoh: (batteryPacks.reduce((sum, p) => sum + p.soh, 0) / batteryPacks.length).toFixed(0)
    };
  }, [batteryPacks]);

  // Stat Card Component
  const StatCard = ({ title, value, icon: Icon, color, subtitle }) => (
    <div className="bg-white rounded-lg p-6 shadow-sm border border-gray-200">
      <div className="flex items-center justify-between">
        <div>
          <p className="text-gray-500 text-sm font-medium">{title}</p>
          <p className="text-2xl font-bold text-gray-900 mt-1">{value}</p>
          {subtitle && <p className="text-xs text-gray-400 mt-1">{subtitle}</p>}
        </div>
        <div className={`p-3 rounded-lg ${color}`}>
          <Icon className="w-6 h-6 text-white" />
        </div>
      </div>
    </div>
  );

  // Alarm History Component
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
      {/* Stats Cards */}
      <div className="grid grid-cols-1 md:grid-cols-3 gap-6">
        <StatCard
          title="Total Power"
          value={`${stats.totalPower} kW`}
          icon={Zap}
          color="bg-blue-500"
        />
        <StatCard
          title="Total Current"
          value={`${stats.totalCurrent} A`}
          icon={Activity}
          color="bg-green-500"
        />
        <StatCard
          title="Active Alerts"
          value={stats.alerts.critical + stats.alerts.warnings}
          icon={AlertTriangle}
          color={stats.alerts.critical > 0 ? "bg-red-500" : "bg-yellow-500"}
          subtitle={`${stats.alerts.critical} critical, ${stats.alerts.warnings} warnings`}
        />
      </div>

      {/* Battery % vs Miles Driven */}
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
            <ChargeStatsCard pack={aggregatePack} chartData={rangeData} />
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
    { value: 'history', label: 'History', icon: History },
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
          {activeTab === 'summary' && <DashboardOverview />}

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

          {activeTab === 'history' && <AlarmHistory alarms={alarms} onClear={() => setAlarms([])} />}

          {activeTab === 'pack' && (() => {
            const pack = batteryPacks.find(p => p.id === selectedPackId);
            if (!pack) {
              return (
                <div className="text-center py-12 text-gray-500">
                  Pack not found or no live data yet.
                </div>
              );
            }
            return <PackDetail pack={pack} chartData={packRangeData[pack.id] || []} thermalHistory={packThermalHistory[pack.id] || []} />;
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
    </div>
  );
};

export default Dashboard;