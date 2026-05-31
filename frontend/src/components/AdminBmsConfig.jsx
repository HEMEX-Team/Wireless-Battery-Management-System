import { useState, useEffect, useCallback, useRef } from 'react';
import { apiFetch } from '../lib/api';
import { AlertTriangle, RefreshCw, ShieldAlert, Zap, Battery, Moon, Sliders, Activity } from 'lucide-react';

// SCD threshold/delay dropdown values (enum index -> label), mirroring the AP.
const SCD_AMPS = [10, 20, 40, 60, 80, 100, 125, 150, 175, 200];
const SCD_DELAYS_US = [15, 31, 47, 62, 78, 93, 109, 125];

// Firmware NVS defaults — the cloud can't read the current on-device thresholds
// (those live on the local AP), so the form starts from these for editing.
const PROT_DEFAULTS = {
  cuv: 2750, cuv_d: 250, cov: 4100, cov_d: 250, occ: 10, occ_d: 73,
  ocd1: 12, ocd1_d: 201, ocd2: 20, ocd2_d: 50, scd: 2, scd_d: 2,
};
const PROT_RANGES = {
  cuv: [2000, 3500], cuv_d: [3, 419], cov: [3500, 4500], cov_d: [3, 419],
  occ: [1, 50], occ_d: [3, 419], ocd1: [1, 100], ocd1_d: [3, 419],
  ocd2: [1, 100], ocd2_d: [3, 419],
};

// Safety Status bit -> short name (mirror firmware decode), for the snapshot view.
const SS_A_BITS = [[7, 'SCD'], [6, 'OCD2'], [5, 'OCD1'], [4, 'OCC'], [3, 'COV'], [2, 'CUV'], [1, 'SFD'], [0, 'OTP']];
const SS_B_BITS = [[7, 'OTF'], [6, 'OTINT'], [5, 'OTD'], [4, 'OTC'], [2, 'UTINT'], [1, 'UTD'], [0, 'UTC']];
const PWR_MODES = ['ACTIVE', 'SLEEP', 'DEEP SLEEP', 'SHUTDOWN', 'OFFLINE'];

function decodeBits(val, bits) {
  return bits.filter(([b]) => (val >> b) & 1).map(([, n]) => n);
}

const SUBTABS = [
  { id: 'protection', label: 'Protection', icon: ShieldAlert },
  { id: 'balancing', label: 'Balancing', icon: Activity },
  { id: 'fet', label: 'FET', icon: Zap },
  { id: 'power', label: 'Power', icon: Moon },
  { id: 'resets', label: 'Resets', icon: RefreshCw },
  { id: 'snapshot', label: 'Live Snapshot', icon: Sliders },
];

export default function AdminBmsConfig({ packs = [] }) {
  const [packId, setPackId] = useState(packs[0]?.id ?? null);
  const [subTab, setSubTab] = useState('protection');
  const [snapshot, setSnapshot] = useState(null);
  const [snapAge, setSnapAge] = useState(null);
  const [online, setOnline] = useState(true);
  const [toast, setToast] = useState(null);
  const [cmdLog, setCmdLog] = useState([]); // {seq, action, status}
  const [prot, setProt] = useState(PROT_DEFAULTS);
  const [bal, setBal] = useState({ trigger: 3400, delta: 5 });
  const pollers = useRef({});

  const note = (msg) => { setToast(msg); setTimeout(() => setToast(null), 4000); };
  const handleErr = (e) => { if (e.status === 409) setOnline(false); note(e.message || 'Request failed'); };

  const loadSnapshot = useCallback(async () => {
    if (!packId) return;
    try {
      const d = await apiFetch(`/v1/packs/${packId}/bms/snapshot`);
      setSnapshot(d.payload);
      setSnapAge(d.age_s);
      if (d.age_s != null) setOnline(d.age_s < 30);
    } catch (e) { /* leave last snapshot */ }
  }, [packId]);

  useEffect(() => {
    loadSnapshot();
    const iv = setInterval(loadSnapshot, 10000); // slow auto-refresh while the tab is open
    return () => clearInterval(iv);
  }, [loadSnapshot]);

  // Clean up any status pollers on unmount.
  useEffect(() => () => { Object.values(pollers.current).forEach(clearInterval); }, []);

  const pollStatus = (seq, action, reboots) => {
    let tries = 0;
    const max = reboots ? 35 : 25; // ~70s for reboot commands, ~50s otherwise
    const iv = setInterval(async () => {
      tries++;
      try {
        const s = await apiFetch(`/v1/packs/${packId}/bms/command/${seq}`);
        setCmdLog((log) => log.map((c) => (c.seq === seq ? { ...c, status: s.status } : c)));
        if (s.status !== 'pending') { clearInterval(iv); delete pollers.current[seq]; loadSnapshot(); }
        else if (tries >= max) {
          clearInterval(iv); delete pollers.current[seq];
          setCmdLog((log) => log.map((c) => (c.seq === seq ? { ...c, status: 'timeout' } : c)));
        }
      } catch { if (tries >= max) { clearInterval(iv); delete pollers.current[seq]; } }
    }, 2000);
    pollers.current[seq] = iv;
  };

  const sendCmd = async (action, args = {}, opts = {}) => {
    if (opts.confirm && !window.confirm(opts.confirm)) return;
    if (opts.typed) {
      const ans = window.prompt(`This is a HIGH-RISK action.\nType ${opts.typed} to confirm "${action}".`);
      if (ans !== opts.typed) { note('Cancelled'); return; }
    }
    try {
      const r = await apiFetch(`/v1/packs/${packId}/bms/command`, { method: 'POST', body: JSON.stringify({ action, args }) });
      setOnline(true);
      setCmdLog((log) => [{ seq: r.seq, action, status: 'pending' }, ...log].slice(0, 12));
      note(`${action} dispatched (#${r.seq})${r.reboots ? ' — pack will reboot (~45s)' : ''}`);
      pollStatus(r.seq, action, r.reboots);
    } catch (e) { handleErr(e); }
  };

  const requestSnapshot = async () => {
    try {
      await apiFetch(`/v1/packs/${packId}/bms/snapshot/request`, { method: 'POST' });
      setOnline(true);
      note('Snapshot requested…');
      setTimeout(loadSnapshot, 1500);
      setTimeout(loadSnapshot, 3500);
    } catch (e) { handleErr(e); }
  };

  const saveProtection = () => {
    const args = {};
    for (const k of Object.keys(PROT_DEFAULTS)) args[k] = Number(prot[k]);
    sendCmd('setProtection', args, {
      confirm: 'Writing protection thresholds REBOOTS the pack (~45s offline) to re-run the BQ config. Continue?',
    });
  };

  if (!packs.length) {
    return <div className="bg-white rounded-xl border border-gray-200 shadow-sm p-8 text-center text-gray-500">No packs to administer.</div>;
  }

  const clampField = (k, v) => {
    const r = PROT_RANGES[k];
    if (!r) return v;
    const n = Number(v);
    return Number.isNaN(n) ? v : Math.max(r[0], Math.min(r[1], n));
  };

  const btn = (extra = '') => `px-3 py-2 rounded-lg text-sm font-medium transition-colors disabled:opacity-40 ${extra}`;
  const primary = btn('bg-slate-800 text-white hover:bg-slate-700');
  const neutral = btn('bg-gray-100 text-gray-800 hover:bg-gray-200 border border-gray-200');
  const danger = btn('bg-rose-50 text-rose-700 border border-rose-200 hover:bg-rose-100');
  const numInput = 'w-full px-2 py-1.5 border border-gray-200 rounded-md text-sm';

  return (
    <div className="space-y-4">
      {/* Header: pack select + online status */}
      <div className="bg-white rounded-xl border border-gray-200 shadow-sm p-4 flex flex-wrap items-center gap-3 justify-between">
        <div className="flex items-center gap-3">
          <ShieldAlert className="h-5 w-5 text-slate-700" />
          <div>
            <h2 className="text-base font-semibold text-gray-900">BMS Admin Console</h2>
            <p className="text-xs text-gray-400">Drives the pack's BMS remotely — admin only.</p>
          </div>
        </div>
        <div className="flex items-center gap-3">
          <select value={packId ?? ''} onChange={(e) => { setPackId(Number(e.target.value)); setSnapshot(null); }}
            className="px-3 py-2 border border-gray-200 rounded-lg text-sm">
            {packs.map((p) => <option key={p.id} value={p.id}>{p.name} ({p.pairing_code || p.pack_identifier})</option>)}
          </select>
          <span className={`inline-flex items-center gap-1 px-2.5 py-1 rounded-md text-xs font-bold uppercase ${online ? 'bg-emerald-50 text-emerald-700 border border-emerald-200' : 'bg-amber-50 text-amber-700 border border-amber-200'}`}>
            <span className={`h-1.5 w-1.5 rounded-full ${online ? 'bg-emerald-500' : 'bg-amber-500'}`} />
            {online ? 'Online' : 'Offline'}
          </span>
        </div>
      </div>

      {!online && (
        <div className="flex items-center gap-2 text-sm text-amber-700 bg-amber-50 border border-amber-200 rounded-lg px-3 py-2">
          <AlertTriangle className="h-4 w-4" /> Pack is offline (no telemetry in 30s). Commands are disabled until it reconnects.
        </div>
      )}

      {/* Sub-tabs */}
      <div className="flex flex-wrap gap-1 border-b border-gray-200">
        {SUBTABS.map(({ id, label, icon: Icon }) => (
          <button key={id} onClick={() => setSubTab(id)}
            className={`flex items-center gap-1.5 px-3 py-2 text-sm font-medium border-b-2 -mb-px ${subTab === id ? 'border-slate-800 text-slate-900' : 'border-transparent text-gray-500 hover:text-gray-800'}`}>
            <Icon className="h-4 w-4" /> {label}
          </button>
        ))}
      </div>

      <fieldset disabled={!online} className="space-y-4">
        {subTab === 'protection' && (
          <div className="bg-white rounded-xl border border-gray-200 shadow-sm p-5">
            <h3 className="text-sm font-semibold text-gray-900 mb-1">Protection Thresholds</h3>
            <p className="text-xs text-amber-600 mb-4 flex items-center gap-1"><AlertTriangle className="h-3.5 w-3.5" /> Saving reboots the pack (~45s). Values start from firmware defaults.</p>
            <div className="grid grid-cols-2 md:grid-cols-3 gap-3">
              {[
                ['cuv', 'Cell UV (mV)'], ['cuv_d', 'CUV delay (ms)'],
                ['cov', 'Cell OV (mV)'], ['cov_d', 'COV delay (ms)'],
                ['occ', 'OC Charge (A)'], ['occ_d', 'OCC delay (ms)'],
                ['ocd1', 'OC Dchg 1 (A)'], ['ocd1_d', 'OCD1 delay (ms)'],
                ['ocd2', 'OC Dchg 2 (A)'], ['ocd2_d', 'OCD2 delay (ms)'],
              ].map(([k, lbl]) => (
                <label key={k} className="text-xs text-gray-500">{lbl}
                  <input type="number" className={numInput} value={prot[k]}
                    onChange={(e) => setProt({ ...prot, [k]: e.target.value })}
                    onBlur={(e) => setProt((p) => ({ ...p, [k]: clampField(k, e.target.value) }))} />
                </label>
              ))}
              <label className="text-xs text-gray-500">SCD threshold (A)
                <select className={numInput} value={prot.scd} onChange={(e) => setProt({ ...prot, scd: Number(e.target.value) })}>
                  {SCD_AMPS.map((a, i) => <option key={i} value={i}>{a} A</option>)}
                </select>
              </label>
              <label className="text-xs text-gray-500">SCD delay (µs)
                <select className={numInput} value={prot.scd_d} onChange={(e) => setProt({ ...prot, scd_d: Number(e.target.value) })}>
                  {SCD_DELAYS_US.map((d, i) => <option key={i} value={i}>{d} µs</option>)}
                </select>
              </label>
            </div>
            <button className={`${danger} mt-4`} onClick={saveProtection}>Save thresholds (reboots pack)</button>
          </div>
        )}

        {subTab === 'balancing' && (
          <div className="bg-white rounded-xl border border-gray-200 shadow-sm p-5 space-y-4">
            <h3 className="text-sm font-semibold text-gray-900">Cell Balancing</h3>
            <div className="flex flex-wrap gap-2">
              <button className={neutral} onClick={() => sendCmd('toggleBalMaster')}>Toggle Master Enable</button>
              <button className={neutral} onClick={() => sendCmd('toggleBal', {}, { confirm: 'Toggle balancing mode (autonomous/host)? Requires a BMS reset to apply.' })}>Toggle Mode</button>
            </div>
            <div className="flex flex-wrap items-end gap-3">
              <label className="text-xs text-gray-500">Host trigger (mV)
                <input type="number" className={numInput} value={bal.trigger} onChange={(e) => setBal({ ...bal, trigger: e.target.value })} />
              </label>
              <label className="text-xs text-gray-500">Host delta (mV)
                <input type="number" className={numInput} value={bal.delta} onChange={(e) => setBal({ ...bal, delta: e.target.value })} />
              </label>
              <button className={primary} onClick={() => sendCmd('setHostBalParams', { trigger: Number(bal.trigger), delta: Number(bal.delta) })}>Apply</button>
            </div>
          </div>
        )}

        {subTab === 'fet' && (
          <div className="bg-white rounded-xl border border-gray-200 shadow-sm p-5 space-y-3">
            <h3 className="text-sm font-semibold text-gray-900">FET Control</h3>
            <p className="text-xs text-gray-400">Individual toggles require Test Mode (FET master off). Verify state in the Live Snapshot — a lost command is not auto-retried.</p>
            <div className="flex flex-wrap gap-2">
              <button className={neutral} onClick={() => sendCmd('fetMasterToggle', {}, { confirm: 'Toggle FET master (AUTO/TEST mode)?' })}>FET Master Toggle</button>
              <button className={neutral} onClick={() => sendCmd('chgTog', {}, { confirm: 'Toggle CHG FET?' })}>CHG Toggle</button>
              <button className={neutral} onClick={() => sendCmd('dsgTog', {}, { confirm: 'Toggle DSG FET?' })}>DSG Toggle</button>
              <button className={neutral} onClick={() => sendCmd('pchgTog', {}, { confirm: 'Toggle PCHG FET?' })}>PCHG Toggle</button>
              <button className={neutral} onClick={() => sendCmd('pdsgTog', {}, { confirm: 'Toggle PDSG FET?' })}>PDSG Toggle</button>
            </div>
            <div className="flex flex-wrap gap-2 pt-1">
              <button className={primary} onClick={() => sendCmd('allFetsOn', {}, { confirm: 'Turn ALL FETs ON (exit maintenance)?' })}>All FETs ON</button>
              <button className={danger} onClick={() => sendCmd('allFetsOff', {}, { typed: 'OFF' })}>All FETs OFF</button>
            </div>
          </div>
        )}

        {subTab === 'power' && (
          <div className="bg-white rounded-xl border border-gray-200 shadow-sm p-5 space-y-3">
            <h3 className="text-sm font-semibold text-gray-900">Power Modes</h3>
            <p className="text-xs text-rose-500">Deep Sleep stops telemetry — the pack goes offline and can only be woken physically or by a Wake that the device must be awake to receive. Use with care.</p>
            <div className="flex flex-wrap gap-2">
              <button className={danger} onClick={() => sendCmd('pwrDeep', {}, { typed: 'SLEEP' })}>Deep Sleep</button>
              <button className={primary} onClick={() => sendCmd('pwrWake')}>Wake</button>
              <button className={neutral} onClick={() => sendCmd('toggleAutoSleep')}>Toggle Auto-Sleep</button>
            </div>
          </div>
        )}

        {subTab === 'resets' && (
          <div className="bg-white rounded-xl border border-gray-200 shadow-sm p-5 space-y-3">
            <h3 className="text-sm font-semibold text-gray-900">Resets &amp; Fault Clearing</h3>
            <div className="flex flex-wrap gap-2">
              <button className={neutral} onClick={() => sendCmd('clearFaults')}>Clear Faults</button>
              <button className={danger} onClick={() => sendCmd('pfReset', {}, { confirm: 'Reset Permanent Failure latches?' })}>PF Reset</button>
              <button className={neutral} onClick={() => sendCmd('ekfReset', {}, { confirm: 'Reset EKF + coulomb counter?' })}>EKF + CC Reset</button>
              <button className={neutral} onClick={() => sendCmd('resetCharge', {}, { confirm: 'Reset accumulated charge?' })}>Reset Charge</button>
              <button className={danger} onClick={() => sendCmd('reset', {}, { typed: 'RESET' })}>Reset BMS IC (reboots)</button>
            </div>
          </div>
        )}

        {subTab === 'snapshot' && (
          <SnapshotView snapshot={snapshot} snapAge={snapAge} onRefresh={requestSnapshot} />
        )}
      </fieldset>

      {/* Command log */}
      {cmdLog.length > 0 && (
        <div className="bg-white rounded-xl border border-gray-200 shadow-sm p-4">
          <h3 className="text-xs font-semibold text-gray-500 uppercase mb-2">Recent commands</h3>
          <div className="space-y-1">
            {cmdLog.map((c) => (
              <div key={c.seq} className="flex items-center justify-between text-sm">
                <span className="text-gray-700">#{c.seq} {c.action}</span>
                <span className={`text-xs font-semibold ${c.status === 'applied' ? 'text-emerald-600' : c.status === 'failed' ? 'text-rose-600' : c.status === 'timeout' ? 'text-amber-600' : 'text-gray-400'}`}>
                  {c.status === 'pending' ? 'applying…' : c.status}
                </span>
              </div>
            ))}
          </div>
        </div>
      )}

      {toast && (
        <div className="fixed bottom-6 right-6 bg-slate-900 text-white text-sm px-4 py-2.5 rounded-lg shadow-lg z-50">{toast}</div>
      )}
    </div>
  );
}

function SnapshotView({ snapshot, snapAge, onRefresh }) {
  const s = snapshot;
  const faults = s ? [...decodeBits(s.ssA || 0, SS_A_BITS), ...decodeBits(s.ssB || 0, SS_B_BITS)] : [];
  const Stat = ({ label, value }) => (
    <div className="bg-gray-50 rounded-lg px-3 py-2">
      <div className="text-[10px] text-gray-400 uppercase tracking-wide">{label}</div>
      <div className="text-sm font-semibold text-gray-900">{value}</div>
    </div>
  );
  return (
    <div className="bg-white rounded-xl border border-gray-200 shadow-sm p-5">
      <div className="flex items-center justify-between mb-4">
        <h3 className="text-sm font-semibold text-gray-900">Live Snapshot</h3>
        <div className="flex items-center gap-3">
          <span className="text-xs text-gray-400">{snapAge == null ? 'never' : `updated ${snapAge}s ago`}</span>
          <button onClick={onRefresh} className="flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-sm bg-slate-800 text-white hover:bg-slate-700">
            <RefreshCw className="h-3.5 w-3.5" /> Refresh
          </button>
        </div>
      </div>
      {!s ? (
        <p className="text-sm text-gray-500">No snapshot yet — click Refresh to request one from the device.</p>
      ) : (
        <div className="space-y-4">
          {faults.length > 0 && (
            <div className="flex flex-wrap gap-1.5">
              {faults.map((f) => <span key={f} className="px-1.5 py-0.5 rounded text-[10px] font-bold bg-rose-100 text-rose-700 border border-rose-300">{f}</span>)}
            </div>
          )}
          <div className="grid grid-cols-2 md:grid-cols-4 gap-2">
            <Stat label="SOC (EKF)" value={`${(s.soc_ekf ?? 0).toFixed(1)}%`} />
            <Stat label="SOH" value={`${(s.soh ?? 0).toFixed(1)}%`} />
            <Stat label="Pack V" value={`${((s.vPack ?? 0) / 1000).toFixed(2)} V`} />
            <Stat label="Current" value={`${((s.current ?? 0) / 1000).toFixed(2)} A`} />
            <Stat label="Power mode" value={PWR_MODES[s.pwr] ?? s.pwr} />
            <Stat label="Bal mode" value={s.balMode === 1 ? 'Host' : 'Auto'} />
            <Stat label="Bal master" value={s.balMaster ? 'On' : 'Off'} />
            <Stat label="FET enable" value={s.fetEn ? 'TEST' : 'AUTO'} />
            <Stat label="SS A/B/C" value={`${s.ssA}/${s.ssB}/${s.ssC}`} />
            <Stat label="PF A/B/C/D" value={`${s.pfA}/${s.pfB}/${s.pfC}/${s.pfD}`} />
            <Stat label="Cell Δ" value={`${s.cellDelta ?? 0} mV`} />
            <Stat label="Watchdog" value={s.wdFault ? 'FAULT' : 'OK'} />
          </div>
          {Array.isArray(s.cellBalTimes) && (
            <div>
              <div className="text-[10px] text-gray-400 uppercase tracking-wide mb-1">Per-cell balancing (s)</div>
              <div className="grid grid-cols-8 gap-1">
                {s.cellBalTimes.map((t, i) => (
                  <div key={i} className="text-center bg-gray-50 rounded px-1 py-1">
                    <div className="text-[9px] text-gray-400">C{i + 1}</div>
                    <div className="text-[11px] font-medium text-gray-700">{t}</div>
                  </div>
                ))}
              </div>
            </div>
          )}
        </div>
      )}
    </div>
  );
}
