#pragma once
// web_api.h

// ==================== HTML PAGE ====================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>BQ76952 Test Board</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'IBM Plex Mono','SF Mono','Consolas',monospace;font-size:12px;background:#f3f4f6;color:#1a1d23;height:100vh;display:flex;flex-direction:column;overflow:hidden}
::-webkit-scrollbar{width:5px}::-webkit-scrollbar-track{background:transparent}::-webkit-scrollbar-thumb{background:#d1d5db;border-radius:3px}
input:focus{outline:1px solid #2563eb;border-color:#2563eb!important}
button:hover{filter:brightness(0.95)}
.header{display:flex;align-items:center;gap:12px;padding:6px 16px;background:#fff;border-bottom:1px solid #e0e3e8}
.header h1{font-size:13px;font-weight:700}
.header .sub{font-size:10px;color:#9ca3ae}
.conn{font-size:11px;color:#16a34a;font-weight:500}
.tabs{display:flex;border-bottom:1px solid #e0e3e8;background:#fff;padding:0 12px}
.tab{padding:8px 16px;cursor:pointer;font-size:11px;font-weight:400;color:#9ca3ae;border-bottom:2px solid transparent}
.tab.active{font-weight:600;color:#2563eb;border-bottom-color:#2563eb}
.content{flex:1;overflow:auto;padding:12px}
.card{border:1px solid #e0e3e8;border-radius:5px;background:#fff;margin-bottom:12px}
.card-head{padding:8px 12px;border-bottom:1px solid #ebedf0;background:#f7f8fa;border-radius:5px 5px 0 0}
.card-head h2{font-size:10px;font-weight:700;color:#5f6672;text-transform:uppercase;letter-spacing:0.8px}
.card-head .desc{font-size:10px;color:#9ca3ae;margin-top:1px}
.card-body{padding:12px}
.g2{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.g3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:12px}
.g4{display:grid;grid-template-columns:1fr 1fr 1fr 1fr;gap:10px}
@media(max-width:768px){.g2,.g3{grid-template-columns:1fr}}
.cell-box{padding:8px 10px;background:#f7f8fa;border-radius:4px;border:1px solid #ebedf0;transition:all 0.3s;}
.cell-box.warn{border-color:#fecaca}
.cell-box.bal-active{border-color:#f87171;box-shadow:0 0 8px rgba(248,113,113,0.35);background:#fef2f2;}
.cell-label{font-size:9px;color:#9ca3ae;font-weight:500}
.cell-val{font-size:22px;font-weight:700;font-variant-numeric:tabular-nums;line-height:1.2}
.cell-unit{font-size:10px;color:#9ca3ae;margin-left:2px}
.kv-label{font-size:9px;color:#9ca3ae;font-weight:500;text-transform:uppercase;letter-spacing:0.4px}
.kv-val{font-size:17px;font-weight:700;font-variant-numeric:tabular-nums;line-height:1.3}
.kv-unit{font-size:9px;font-weight:400;color:#9ca3ae;margin-left:2px}
.badge{font-size:9px;padding:2px 7px;border-radius:3px;font-weight:600;letter-spacing:0.2px;display:inline-block}
.badge-green{background:#dcfce7;color:#16a34a}
.badge-blue{background:#eff6ff;color:#2563eb}
.badge-red{background:#fee2e2;color:#dc2626}
.badge-amber{background:#fef3c7;color:#d97706}
.badge-gray{background:#f3f4f6;color:#9ca3ae}
.badge-outline{background:transparent;border:1px solid}
.btn{padding:5px 12px;border-radius:4px;border:1px solid #e0e3e8;background:#fff;color:#5f6672;font-size:11px;font-weight:500;cursor:pointer;font-family:inherit;transition:all 0.1s}
.btn:hover{background:#f7f8fa}
.btn.primary{background:#eff6ff;border-color:#bfdbfe;color:#2563eb}
.btn.danger{border-color:#fecaca;color:#dc2626}
.btn.danger:hover{background:#fef2f2}
.btn.active{background:#eff6ff;border-color:#2563eb;color:#2563eb;font-weight:600}
.btn.sm{padding:3px 8px;font-size:10px}
.toggle{width:34px;height:18px;border-radius:9px;cursor:pointer;position:relative;border:none;padding:0;transition:background 0.15s}
.toggle.on{background:#16a34a}.toggle.off{background:#d1d5db}
.toggle-dot{width:14px;height:14px;border-radius:50%;background:#fff;position:absolute;top:2px;transition:left 0.15s;box-shadow:0 1px 2px rgba(0,0,0,0.15)}
.toggle.on .toggle-dot{left:18px}.toggle.off .toggle-dot{left:2px}
.fet-row{display:flex;align-items:center;justify-content:space-between;padding:8px 0}
.fet-row+.fet-row{border-top:1px solid #ebedf0}
.pbar{height:4px;background:#e5e7eb;border-radius:2px;margin-top:4px}
.pbar-fill{height:4px;border-radius:2px;transition:width 0.3s}
.temp-box{padding:8px 10px;background:#f7f8fa;border-radius:4px;border:1px solid #ebedf0;text-align:center}
.temp-val{font-size:20px;font-weight:700;font-variant-numeric:tabular-nums}
.log-panel{background:#fff;border-top:1px solid #e0e3e8;display:flex;flex-direction:column;flex-shrink:0;transition:height 0.15s}
.log-header{display:flex;align-items:center;justify-content:space-between;padding:5px 16px;cursor:pointer;min-height:30px;flex-shrink:0}
.log-body{flex:1;overflow:auto;font-size:10px;padding:0 16px 6px}
.log-body table{width:100%;border-collapse:collapse}
.log-body th{text-align:left;padding:2px 8px;color:#9ca3ae;font-weight:500;font-size:9px;border-bottom:1px solid #e0e3e8;position:sticky;top:0;background:#fff}
.log-body td{padding:1px 8px}
.log-read{color:#2563eb;font-weight:600}.log-write{color:#16a34a;font-weight:600}
.log-ack{color:#16a34a;font-weight:600}.log-nack{color:#dc2626;font-weight:600}
.statusbar{display:flex;gap:16px;padding:3px 16px;background:#fff;border-top:1px solid #e0e3e8;font-size:10px;color:#9ca3ae}
.divider{border-top:2px solid #e0e3e8;padding-top:12px;margin-top:4px}
.section-title{font-size:11px;font-weight:700;color:#5f6672;margin-bottom:10px;text-transform:uppercase;letter-spacing:0.5px}
.led-card{display:flex;align-items:center;gap:16px;padding:12px;background:#f7f8fa;border-radius:6px;border:2px dashed #e0e3e8}
.led-indicator{width:24px;height:24px;border-radius:50%;border:2px solid #d1d5db;transition:all 0.3s}
.led-indicator.on{background:#16a34a;border-color:#16a34a;box-shadow:0 0 12px rgba(22,163,74,0.5)}
.led-indicator.off{background:#f3f4f6}
.warn-box{margin-top:6px;padding:5px 8px;border-radius:4px;background:#fffbeb;border:1px solid #fde68a;color:#d97706;font-size:10px;font-weight:500}
.chart-wrap{border:1px solid #e0e3e8;border-radius:5px;background:#fff;padding:8px 10px;margin-bottom:12px}
</style>
</head>
<body>
<div class="header">
  <h1>BQ76952</h1>
  <span class="sub">3-Cell Test Board</span>
  <div style="flex:1"></div>
  <span class="conn" id="connStatus">&#x25CF; I2C &middot; 0x08</span>
</div>
<div class="tabs">
  <div class="tab active" onclick="switchTab(0)" id="tab0">Dashboard</div>
  <div class="tab" onclick="switchTab(1)" id="tab1">Plots</div>
</div>
<div class="content" id="mainContent">
<div id="dashTab">
<div class="g2">
  <div class="card">
    <div class="card-head"><h2>Cell Voltages</h2><div class="desc">Individual cell monitoring with pack summary</div></div>
    <div class="card-body">
      <div id="cellGrid" class="g3" style="margin-bottom:12px"></div>
      <div class="g4">
        <div><div class="kv-label">Pack</div><div class="kv-val" style="color:#2563eb" id="kvPack">--<span class="kv-unit">V</span></div></div>
        <div><div class="kv-label">Min</div><div class="kv-val" id="kvMin">--<span class="kv-unit">V</span></div></div>
        <div><div class="kv-label">Max</div><div class="kv-val" id="kvMax">--<span class="kv-unit">V</span></div></div>
        <div><div class="kv-label">Delta</div><div class="kv-val" id="kvDelta">--<span class="kv-unit">mV</span></div></div>
      </div>
    </div>
  </div>
  <div class="card">
    <div class="card-head"><h2>Pack Status</h2><div class="desc">Current, power, SOC and charge accumulation</div></div>
    <div class="card-body">
      <div class="g3" style="margin-bottom:12px; display:flex; flex-wrap:wrap; gap:12px;">
        <div class="cell-box" style="flex:1; min-width:120px;"><div class="cell-label">CURRENT</div><div class="cell-val" id="pCurrent">--<span class="cell-unit">mA</span></div><div style="font-size:9px;font-weight:500;margin-top:1px" id="pCurState">IDLE</div></div>
        <div class="cell-box" style="flex:1; min-width:120px;"><div class="cell-label">BQ SOC (est)</div><div class="cell-val" id="pSoc">--<span class="cell-unit">%</span></div><div class="pbar"><div class="pbar-fill" id="pSocBar" style="width:0%;background:#16a34a"></div></div></div>
        <div class="cell-box" style="flex:1; min-width:120px;"><div class="cell-label">EKF SOC</div><div class="cell-val" id="kvSOC_EKF" style="color:#2563eb">--<span class="cell-unit">%</span></div><div style="font-size:9px;color:#9ca3ae">±<span id="kvSOC_Unc">--</span>% unc</div></div>
        <div class="cell-box" style="flex:1; min-width:120px;"><div class="cell-label">POWER</div><div class="cell-val" id="pPower">--<span class="cell-unit">W</span></div></div>
        <div class="cell-box" id="tosBox" style="flex:1; min-width:120px; transition:all 0.3s;"><div class="cell-label">TOSF DELTA</div><div class="cell-val" id="pTosDelta">--<span class="cell-unit">mV</span></div><div style="font-size:9px;font-weight:bold;margin-top:1px" id="pTosState">OK</div></div>
      </div>
      <div class="g4">
        <div><div class="kv-label">Accum. Charge</div><div class="kv-val" id="kvCharge">--<span class="kv-unit">mAh</span></div></div>
        <div><div class="kv-label" id="kvTimeL">Active Time</div><div class="kv-val" id="kvChargeTime">--<span class="kv-unit">s</span></div></div>
        <div><div class="kv-label">Stack V</div><div class="kv-val" id="kvStack">--<span class="kv-unit">mV</span></div></div>
        <div><div class="kv-label">Pack V</div><div class="kv-val" id="kvPackMv">--<span class="kv-unit">mV</span></div></div>
      </div>
    </div>
  </div>
</div>
<div class="g2">
  <div class="card">
    <div class="card-head"><h2>Temperatures</h2><div class="desc">External thermistors and BQ76952 die temperature</div></div>
    <div class="card-body">
      <div class="g4">
        <div class="temp-box"><div class="cell-label">TS1</div><div class="temp-val" id="tTs1">--</div></div>
        <div class="temp-box"><div class="cell-label">HDQ</div><div class="temp-val" id="tHDQ">--</div></div>
        <div class="temp-box"><div class="cell-label">TS3</div><div class="temp-val" id="tTs3">--</div></div>
        <div class="temp-box"><div class="cell-label">Die</div><div class="temp-val" id="tChip">--</div></div>
      </div>
      <div id="thermalWarn" style="display:none;margin-top:10px;background:#fef2f2;border:1px solid #fecaca;color:#dc2626" class="warn-box">
        <b>THERMAL LIMIT EXCEEDED:</b> Balancing functions have been automatically throttled to prevent hardware damage.
      </div>
      <div id="restartBanner" style="display:none;margin-top:10px;background:#fef3c7;border:1px solid #fde68a;color:#92400e;padding:8px 12px;border-radius:6px;font-size:11px;font-weight:500">
        &#x26A0; <b>RESTART REQUIRED:</b> <span id="restartMsg">Settings have been saved. Click Reset BMS IC to apply changes.</span>
      </div>
    </div>
  </div>
  <div class="card">
    <div class="card-head"><h2>Recovery &amp; Diagnostics</h2><div class="desc">Autonomous FET logic, LD &amp; PACK pin recovery status</div></div>
    <div class="card-body">
      
      <!-- FET AUTONOMOUS STATUS -->
      <div style="margin-bottom:12px">
        <div class="cell-label" style="margin-bottom:4px">AUTONOMOUS FET DRIVERS (BQ STATUS)</div>
        <div style="display:flex;gap:6px;flex-wrap:wrap">
          <span class="badge" id="badgeChg">CHG: --</span>
          <span class="badge" id="badgePchg">PCHG: --</span>
          <span class="badge" id="badgeDsg">DSG: --</span>
          <span class="badge" id="badgePdsg">PDSG: --</span>
        </div>
      </div>

      <!-- FAULT FLAGS -->
      <div style="margin-bottom:12px">
        <div class="cell-label" style="margin-bottom:4px">ACTIVE PROTECTION FLAGS</div>
        <div style="display:flex;gap:4px;flex-wrap:wrap" id="protFlags">
          <span class="badge badge-green">ALL CLEAR</span>
        </div>
      </div>

      <hr style="border:0;border-top:1px solid #e0e3e8;margin:12px 0">

      <!-- HARDWARE RECOVERY MONITORS -->
      <div class="g2">
        <!-- LOAD DETECT -->
        <div style="background:#fef2f2; border:1px solid #fecaca; border-radius:5px; padding:10px" id="cardLd">
          <div class="cell-label" style="color:#dc2626; margin-bottom:4px; font-weight:700">LOAD DETECT (SCD/OCD RECOVERY)</div>
          <div style="display:flex; align-items:flex-end; gap:10px">
            <div>
              <div style="font-size:9px; color:#9ca3ae">LD PIN VOLTAGE</div>
              <div style="font-size:18px; font-weight:700; color:#1a1d23" id="kV_LD">--<span style="font-size:10px; color:#9ca3ae; margin-left:2px">V</span></div>
            </div>
            <div>
              <div style="font-size:9px; color:#9ca3ae">STATUS</div>
              <div style="font-size:11px; font-weight:700; color:#16a34a" id="statLdWait">NORMAL</div>
            </div>
          </div>
          <button class="btn sm" onclick="sendCmd('ldRestart')" style="margin-top:8px; width:100%; font-size:9px">FORCE LD RESTART (0x009D)</button>
        </div>

        <!-- CHARGER DETECT -->
        <div style="background:#eff6ff; border:1px solid #bfdbfe; border-radius:5px; padding:10px" id="cardPack">
          <div class="cell-label" style="color:#2563eb; margin-bottom:4px; font-weight:700">PACK PIN (CHARGER RECOVERY)</div>
          <div style="display:flex; align-items:flex-end; gap:10px">
            <div>
              <div style="font-size:9px; color:#9ca3ae">PACK VOLTAGE</div>
              <div style="font-size:18px; font-weight:700; color:#1a1d23" id="kV_PackRec">--<span style="font-size:10px; color:#9ca3ae; margin-left:2px">V</span></div>
            </div>
            <div>
              <div style="font-size:9px; color:#9ca3ae">STATUS</div>
              <div style="font-size:11px; font-weight:700; color:#9ca3ae" id="statCharger">NOT DETECTED</div>
            </div>
          </div>
        </div>
      </div>
      
    </div>
  </div>

  <div class="card">
    <div class="card-head"><h2>Balancing Diagnostics</h2><div class="desc">Host Algorithm and Hardware Rules</div></div>
    <div class="card-body">
      <div style="margin-top:8px">
        <div class="cell-label" style="margin-bottom:4px;display:flex;align-items:center;gap:12px;flex-wrap:wrap">
          <div>BALANCING DIAGNOSTICS</div>
          <span class="badge" id="badgeRuleState" style="background:#f3f4f6;color:#5f6672">RULE: IDLE</span>
          <button id="balModeBtn" onclick="sendCmd('toggleBal')" style="background:#fef3c7;color:#d97706;padding:2px 6px;border-radius:3px;font-size:10px;font-weight:bold;border:1px solid #fde68a;cursor:pointer;margin-left:auto;">MODE: ...</button>
          <button id="balMasterBtn" onclick="sendCmd('toggleBalMaster')" style="background:#dcfce7;color:#16a34a;padding:2px 6px;border-radius:3px;font-size:10px;font-weight:bold;border:1px solid #bbf7d0;cursor:pointer;">MASTER: ON</button>
        </div>
        <div class="g4" style="margin-bottom:6px;gap:6px">
          <div style="background:#f7f8fa;padding:4px 6px;border-radius:3px"><span style="font-size:9px;color:#9ca3ae">DELTA:</span> <span id="kVBalDelta" style="font-weight:bold">--</span> mV</div>
          <div style="background:#f7f8fa;padding:4px 6px;border-radius:3px"><span style="font-size:9px;color:#9ca3ae">SW TIME:</span> <span id="kVBalTime" style="font-weight:bold">--</span> s</div>
          <div style="background:#f7f8fa;padding:4px 6px;border-radius:3px"><span style="font-size:9px;color:#9ca3ae">MASK:</span> <span id="kVBalMask" style="font-weight:bold">--</span></div>
        </div>
        <div class="cell-label" style="margin-bottom:4px">HOST ALGORITHM SETTINGS</div>
        <div class="g3" style="margin-bottom:10px; gap:8px">
          <div><div style="font-size:9px;color:#9ca3ae;margin-bottom:2px">TRIGGER (mV)</div><input type="number" id="inBalTrig" value="3400" style="width:100%;padding:4px;border:1px solid #e0e3e8;border-radius:3px;font-size:11px"></div>
          <div><div style="font-size:9px;color:#9ca3ae;margin-bottom:2px">DELTA (mV)</div><input type="number" id="inBalDelta" value="1" style="width:100%;padding:4px;border:1px solid #e0e3e8;border-radius:3px;font-size:11px"></div>
          <div style="display:flex;align-items:flex-end"><button onclick="setHostBalParams()" style="width:100%;background:#2563eb;color:#fff;padding:5px;border:none;border-radius:3px;font-size:11px;font-weight:bold;cursor:pointer">APPLY</button></div>
        </div>
        <div class="cell-label" style="margin-bottom:4px">CELL VOLTAGES &amp; BALANCING TIMES</div>
        <table style="width:100%;border-collapse:collapse;font-size:10px;text-align:left;border:1px solid #e0e3e8">
          <thead><tr style="background:#f7f8fa;color:#9ca3ae">
            <th style="padding:2px 4px;border:1px solid #e0e3e8">Cell</th>
            <th style="padding:2px 4px;border:1px solid #e0e3e8">V (mV)</th>
            <th style="padding:2px 4px;border:1px solid #e0e3e8">Time (s)</th>
            <th style="padding:2px 4px;border:1px solid #e0e3e8">Bled</th>
            <th style="padding:2px 4px;border:1px solid #e0e3e8">Stat</th>
          </tr></thead>
          <tbody id="kVCellData"></tbody>
        </table>
      </div>
    </div>
  </div>
  <div class="card">
    <div class="card-head"><h2>BQ76952 Status</h2><div class="desc">Device sealing and power management</div></div>
    <div class="card-body">
      <div class="g2" style="margin-bottom:12px">
        <div class="cell-box"><div class="cell-label">SEALING</div><div class="cell-val" id="statSeal" style="font-size:14px">--</div></div>
        <div class="cell-box"><div class="cell-label">POWER MODE</div><div class="cell-val" id="statPwr" style="font-size:14px">--</div></div>
      </div>
      <div style="display:flex;gap:6px;flex-wrap:wrap">
        <button class="btn sm" onclick="sendCmd('pwrDeep')">Deep Sleep</button>
        <button class="btn sm" onclick="sendCmd('fetMasterToggle')">FET Master Enable</button>
        <button class="btn sm primary" onclick="sendCmd('pwrWake')">Wake (I2C)</button>
      </div>
      <div class="warn-box" style="margin-top:8px; border-style:dashed; color:#5f6672; background:#f9fafb" id="pwrNote">
        <b>NOTE:</b> If the board is in <b>Deep Sleep</b> or <b>Shutdown</b>, you MUST press the physical <b>WAKE</b> button on the hardware to reactivate communication.
      </div>
    </div>
  </div>
</div>
<div class="divider">
  <div class="section-title">Device Controls</div>
  <div class="g3">
    <div class="card">
      <div class="card-head"><h2>FET Control</h2><div class="desc">Charge/discharge MOSFET switching via I2C subcommands</div></div>
      <div class="card-body">
        <div class="fet-row"><div><div style="font-size:11px;font-weight:500">Charge FET</div><div style="font-size:9px;color:#9ca3ae">Hardware Control (CFETOFF Pin)</div></div><div style="display:flex;align-items:center;gap:8px"><span class="badge" id="fetChgBadge">--</span><button class="toggle off" id="fetChgToggle" onclick="toggleFet('chg')"><div class="toggle-dot"></div></button></div></div>
        <div class="fet-row"><div><div style="font-size:11px;font-weight:500">Discharge FET</div><div style="font-size:9px;color:#9ca3ae">Hardware Control (DFETOFF Pin)</div></div><div style="display:flex;align-items:center;gap:8px"><span class="badge" id="fetDsgBadge">--</span><button class="toggle off" id="fetDsgToggle" onclick="toggleFet('dsg')"><div class="toggle-dot"></div></button></div></div>
        <div class="fet-row"><div><div style="font-size:11px;font-weight:500">Auto Sleep Mode</div><div style="font-size:9px;color:#9ca3ae">Allow BQ to sleep when idle (0x9234)</div></div><div style="display:flex;align-items:center;gap:8px"><span class="badge" id="autoSleepBadge">--</span><button class="toggle off" id="autoSleepToggle" onclick="sendCmd('toggleAutoSleep')"><div class="toggle-dot"></div></button></div></div>
        <div class="fet-row" style="border-top:1px solid #ebedf0; padding-top:8px; margin-top:8px"><div><div style="font-size:11px;font-weight:600">FET Master Enable</div><div style="font-size:9px;color:#9ca3ae">Autonomous Control (Subcmd 0x0022)</div></div><div style="display:flex;align-items:center;gap:8px"><span class="badge" id="fetMasterBadge">--</span><button class="toggle on" id="fetMasterToggle" onclick="sendCmd('fetMasterToggle')"><div class="toggle-dot"></div></button></div></div>
        <div id="fetWarn" style="display:none" class="warn-box">Both FETs off — pack is isolated</div>
      </div>
    </div>
    <div class="card">
      <div class="card-head"><h2>Reset &amp; Utilities</h2><div class="desc">Hardware reset and charge counter control</div></div>
      <div class="card-body">
        <div style="display:flex;flex-direction:column;gap:8px">
          <div style="display:flex;align-items:center;justify-content:space-between"><div><div style="font-size:11px">Reset BMS IC</div><div style="font-size:9px;color:#9ca3ae">Subcmd 0x0012</div></div><button class="btn danger" onclick="sendCmd('reset')">Reset</button></div>
          <div style="border-top:1px solid #ebedf0;padding-top:8px;display:flex;align-items:center;justify-content:space-between"><div><div style="font-size:11px">Reset EKF & CC</div><div style="font-size:9px;color:#9ca3ae">O0x0082 + Voltage Init</div></div><button class="btn danger" onclick="fetch('/api/ekf_reset').then(()=>setTimeout(fetchData,200))">Sync</button></div>
          <div style="border-top:1px solid #ebedf0;padding-top:8px;display:flex;align-items:center;justify-content:space-between"><div><div style="font-size:11px">All FETs Auto-ON</div><div style="font-size:9px;color:#9ca3ae">Subcmd 0x0096</div></div><button class="btn primary" onclick="sendCmd('allFetsOn')">Enable</button></div>
          <div style="border-top:1px solid #ebedf0;padding-top:8px;display:flex;align-items:center;justify-content:space-between"><div><div style="font-size:11px;color:#dc2626;font-weight:600">All FETs OFF</div><div style="font-size:9px;color:#9ca3ae">Subcmd 0x0095</div></div><button class="btn danger" onclick="sendCmd('allFetsOff')">Kill</button></div>
        </div>
      </div>
    </div>
    <div class="card">
      <div class="card-head"><h2>Performance Monitor</h2><div class="desc">ESP32 Core 1 RT latency tracking</div></div>
      <div class="card-body">
        <svg id="chartPerf" viewBox="0 0 700 80" style="width:100%;display:block;background:#f7f8fa;border-radius:3px;margin-bottom:8px"></svg>
        <div style="font-size:10px;text-align:center;font-weight:600;color:#5f6672;margin-bottom:8px;letter-spacing:0.5px">TOTAL CORE LOAD: <span id="vTotalLoad" style="color:#2563eb;font-weight:bold;font-size:11px">--</span></div>
        <div class="fet-row"><div><div style="font-size:11px;font-weight:500">I2C Polling</div></div><div style="font-family:monospace;font-size:11px;color:#d97706" id="perfI2C">-- ms</div></div>
        <div class="fet-row"><div><div style="font-size:11px;font-weight:500">EKF + NN Core</div></div><div style="font-family:monospace;font-size:11px;color:#16a34a" id="perfEKF">-- ms</div></div>
        <div class="fet-row"><div><div style="font-size:11px;font-weight:500">Web Dashboard API</div></div><div style="font-family:monospace;font-size:11px;color:#2563eb" id="perfWeb">-- ms</div></div>
      </div>
    </div>
    <div class="card">
      <div class="card-head"><h2>Hardware Testing Mode</h2><div class="desc">Manual gate pulsing (Requires FET Master: OFF)</div></div>
      <div class="card-body">
        <div class="warn-box" id="testModeWarn" style="margin-bottom:8px; display:block">
          <b>LOCKED:</b> Disable 'FET Master Enable' to unlock manual gate testing.
        </div>
        <div class="g2" style="gap:8px">
          <button class="btn sm" id="btnTestPchg" onclick="sendCmd('pchgTog')" disabled>PCHG Toggle</button>
          <button class="btn sm" id="btnTestPdsg" onclick="sendCmd('pdsgTog')" disabled>PDSG Toggle</button>
          <button class="btn sm" id="btnTestChg" onclick="sendCmd('chgTog')" disabled>CHG Toggle</button>
          <button class="btn sm" id="btnTestDsg" onclick="sendCmd('dsgTog')" disabled>DSG Toggle</button>
        </div>
        <div style="font-size:9px; color:#9ca3ae; margin-top:8px">Utilizes BQ76952 register 0x7E TEST bits (Bits 0-3) for direct gate drive.</div>
      </div>
    </div>
    
  </div>
</div>
<div id="plotsTab" style="display:none">
  <div style="display:flex;gap:6px;align-items:center;margin-bottom:12px"><span style="font-size:10px;color:#9ca3ae;font-weight:500">Window:</span><button class="btn sm active">60s</button><button class="btn sm">5min</button></div>
  <div class="chart-wrap"><svg id="chartVoltage" viewBox="0 0 700 200" style="width:100%;display:block"></svg></div>
  <div class="chart-wrap"><svg id="chartCurrent" viewBox="0 0 700 160" style="width:100%;display:block"></svg></div>
  <div class="chart-wrap"><svg id="chartTemp" viewBox="0 0 700 160" style="width:100%;display:block"></svg></div>
</div>
</div>
<div class="log-panel" id="logPanel" style="height:160px">
  <div class="log-header" onclick="toggleLog()">
    <span style="font-size:11px;font-weight:600;color:#5f6672"><span id="logArrow">&#x25BE;</span> I2C Log <span style="font-weight:400;color:#9ca3ae" id="logTxCount">(0)</span></span>
    <div style="display:flex;gap:4px"><button onclick="event.stopPropagation();clearLog()" style="background:none;border:1px solid #e0e3e8;border-radius:3px;padding:1px 6px;font-size:9px;color:#9ca3ae;cursor:pointer;font-family:inherit">Clear</button></div>
  </div>
  <div class="log-body" id="logBody"><table><thead><tr><th>Time</th><th>Dir</th><th>Addr</th><th>Reg</th><th>Data</th><th></th></tr></thead><tbody id="logTbody"></tbody></table></div>
</div>
<div class="statusbar">
  <span style="color:#16a34a" id="sbConn">&#x25CF; Connected</span>
  <span>I2C 0x08</span>
  <span id="sbElapsed">0:00</span>
  <span id="sbTx">Tx: 0</span>
  <div style="flex:1"></div>
  <span>wBMS Test Board v0.1</span>
</div>
<script>
const CELLS=13;
const regNames={0x03:'SafetyAlert_A',0x05:'SafetyFault_B',0x12:'BatStatus',0x14:'Cell1',0x16:'Cell2',0x18:'Cell3',0x34:'VStack',0x36:'VPack',0x3A:'CC2_Cur',0x3E:'Subcmd',0x40:'Resp_Buf',0x68:'IntTemp',0x70:'TS1',0x72:'TS2',0x74:'TS3',0x7F:'FET_Stat',0x83:'CBActive'};
const cg=document.getElementById('cellGrid');
for(let i=0;i<CELLS;i++){cg.innerHTML+=`<div class="cell-box" id="cbox${i}"><div style="display:flex;justify-content:space-between"><div class="cell-label">CELL ${i+1}</div><div id="bal${i}" class="badge badge-red" style="display:none;font-size:8px;padding:1px 3px">BAL</div></div><div class="cell-val" id="cv${i}">--<span class="cell-unit">V</span></div></div>`;}
let logOpen=true,startTime=Date.now(),logs=[];
let vHist=Array.from({length:CELLS},()=>[]),curHist=[],tHist=[[],[],[]];
const HMAX=120;
function switchTab(n){document.getElementById('dashTab').style.display=n===0?'block':'none';document.getElementById('plotsTab').style.display=n===1?'block':'none';document.getElementById('tab0').className='tab'+(n===0?' active':'');document.getElementById('tab1').className='tab'+(n===1?' active':'');if(n===1)drawCharts();}
function toggleLog(){logOpen=!logOpen;document.getElementById('logPanel').style.height=logOpen?'160px':'30px';document.getElementById('logBody').style.display=logOpen?'block':'none';document.getElementById('logArrow').innerHTML=logOpen?'&#x25BE;':'&#x25B8;';}
function clearLog(){logs=[];renderLog();}
function renderLog(){const tb=document.getElementById('logTbody');let h='';for(const l of logs.slice(-40)){h+=`<tr><td style="color:#9ca3ae">${l.ts}</td><td class="${l.wr?'log-write':'log-read'}">${l.wr?'WRITE':'READ'}</td><td>0x${l.addr.toString(16).toUpperCase().padStart(2,'0')}</td><td style="color:#5f6672">${regNames[l.addr]||'?'}</td><td>0x${l.val.toString(16).toUpperCase().padStart(4,'0')}</td><td class="${l.ok?'log-ack':'log-nack'}">${l.ok?'ACK':'NACK'}</td></tr>`;}tb.innerHTML=h;const lb=document.getElementById('logBody');lb.scrollTop=lb.scrollHeight;}
function tempColor(v){return v>45?'#dc2626':v>35?'#d97706':'#16a34a';}

function setHostBalParams(){let trig=document.getElementById('inBalTrig').value;let delta=document.getElementById('inBalDelta').value;fetch('/api/cmd?action=setHostBalParams&trigger='+trig+'&delta='+delta);}

function updateUI(d){
  try {
  const volts=d.v.slice(0,CELLS).map(mv=>mv/1000);
  const minV=Math.min(...volts),maxV=Math.max(...volts);
  const delta=((maxV-minV)*1000).toFixed(0);
  const pack=volts.reduce((a,b)=>a+b,0);
  const soc = d.cc_soc !== undefined ? d.cc_soc : Math.min(100,Math.max(0,((minV-3.0)/(4.2-3.0))*100));
  
  // Update Recovery & Diagnostics UI
  const setFetBadge = (id, state) => {
    const el = document.getElementById(id);
    if (!el) return;
    el.className = state ? 'badge badge-green' : 'badge badge-red';
    el.textContent = id.replace('badge', '').toUpperCase() + ': ' + (state ? 'ON' : 'OFF');
  };
  // Note: isCharging is true if CHG || PCHG. We must use pure states.
  setFetBadge('badgeChg', d.isCharging && !d.f_pchg);
  setFetBadge('badgePchg', d.f_pchg);
  setFetBadge('badgeDsg', d.isDischarging && !d.f_pdsg);
  setFetBadge('badgePdsg', d.f_pdsg);
  
  // LD Pin
  document.getElementById('kV_LD').innerHTML = (d.vLd * 10 / 1000.0).toFixed(2) + '<span style="font-size:10px; color:#9ca3ae; margin-left:2px">V</span>';
  const slw = document.getElementById('statLdWait');
  if (slw) {
    if (d.ldWait) {
      if (d.vLd < 200) { slw.textContent = 'SHORT ACTIVE'; slw.style.color = '#dc2626'; }
      else { slw.textContent = 'RECOVERING...'; slw.style.color = '#d97706'; }
    } else {
      slw.textContent = 'NORMAL'; slw.style.color = '#16a34a';
    }
  }

  // Pack Pin / Charger
  document.getElementById('kV_PackRec').innerHTML = (d.vPack * 10 / 1000.0).toFixed(2) + '<span style="font-size:10px; color:#9ca3ae; margin-left:2px">V</span>';
  const sChg = document.getElementById('statCharger');
  if (sChg) {
    if ((d.vPack - d.vStack) * 10 > 500) { sChg.textContent = 'CHARGER DETECTED'; sChg.style.color = '#2563eb'; }
    else { sChg.textContent = 'NOT DETECTED'; sChg.style.color = '#9ca3ae'; }
  }

  // Fault Flags
  const pf = document.getElementById('protFlags');
  if (pf) {
    let fHtml = '';
    const addF = (name, val) => { if(val) fHtml += `<span class="badge badge-red">${name}</span>`; };
    addF('SCD', d.prot_sc); addF('OCD2', d.prot_oc2); addF('OCD1', d.prot_oc1); addF('OCC', d.prot_occ);
    addF('COV', d.prot_ov); addF('CUV', d.prot_uv);
    addF('OTF', d.temp_otf); addF('OTINT', d.temp_oti); addF('OTD', d.temp_otd); addF('OTC', d.temp_otc);
    if (fHtml === '') fHtml = '<span class="badge badge-green">ALL CLEAR</span>';
    pf.innerHTML = fHtml;
  }

  const mbBtn = document.getElementById('balMasterBtn');
  if (mbBtn && d.balMaster !== undefined) {
      if (d.balMaster) {
          mbBtn.textContent = 'MASTER: ON';
          mbBtn.style.color = '#16a34a'; mbBtn.style.background = '#dcfce7'; mbBtn.style.borderColor = '#bbf7d0';
      } else {
          mbBtn.textContent = 'MASTER: OFF';
          mbBtn.style.color = '#dc2626'; mbBtn.style.background = '#fee2e2'; mbBtn.style.borderColor = '#fecaca';
      }
  }
  
  document.getElementById('kVBalDelta').textContent = d.cellDelta || '0';
  document.getElementById('kVBalTime').textContent = d.balTime || '0';
  document.getElementById('kVBalMask').textContent = '0x' + (d.bal||0).toString(16).toUpperCase().padStart(4,'0');
  
  let cbTimes = d.cellBalTimes || new Array(16).fill(0);
  let cellHtml = '';

  for(let i=0;i<CELLS;i++){
    const isBal = (d.bal & (1 << i)) || (i == CELLS-1 && (d.bal & (1 << 15)));
    document.getElementById('bal'+i).style.display = isBal ? 'block' : 'none';
    document.getElementById('cv'+i).innerHTML=volts[i].toFixed(3)+'<span class="cell-unit">V</span>';
    let boxClass = 'cell-box';
    if(volts[i]<2.7||volts[i]>4.1) boxClass += ' warn';
    if(isBal) boxClass += ' bal-active';
    document.getElementById('cbox'+i).className = boxClass;
    document.getElementById('cv'+i).style.color=(volts[i]<2.7||volts[i]>4.1)?'#dc2626':'#1a1d23';
    
    let mvv = Math.round(volts[i]*1000);
    let st = isBal ? '<span style="color:#2563eb;font-weight:bold">⚡ BAL</span>' : 
             (mvv == d.minV ? '<span style="color:#16a34a">MIN</span>' : 
             (mvv == d.maxV ? '<span style="color:#d97706">MAX</span>' : ''));
             
    let cbt = (i == CELLS-1) ? cbTimes[15] : cbTimes[i];
    let pbW = Math.min(100, ((cbt||0)/3600.0)*100); // Progress relative to 1 hour
    let cbtStr = (cbt||0) + 's <div class="pbar"><div class="pbar-fill" style="width:'+pbW+'%;background:#d97706"></div></div>';
    let bledMh = ((cbt||0) * (37.0 / 3600.0)).toFixed(2) + ' mAh';
    
    cellHtml += '<tr><td style="padding:2px 4px;border:1px solid #e0e3e8">C' + (i+1) + 
                '</td><td style="padding:2px 4px;border:1px solid #e0e3e8">' + mvv + 
                '</td><td style="padding:2px 4px;border:1px solid #e0e3e8">' + cbtStr + 
                '</td><td style="padding:2px 4px;border:1px solid #e0e3e8;color:#d97706">' + bledMh + 
                '</td><td style="padding:2px 4px;border:1px solid #e0e3e8">' + st + '</td></tr>';
  }
  document.getElementById('kVCellData').innerHTML = cellHtml;
  
  const bmode = document.getElementById('balModeBtn');
  if (bmode) {
      if (d.balMode == 0) {
          bmode.textContent = "MODE: AUTONOMOUS";
          bmode.style.background = "#dcfce7";
          bmode.style.color = "#16a34a";
          bmode.style.border = "1px solid #86efac";
      } else if (d.balMode == 1) {
          bmode.textContent = "MODE: HOST-ALGO";
          bmode.style.background = "#fef3c7";
          bmode.style.color = "#d97706";
          bmode.style.border = "1px solid #fde68a";
      } else if (d.balMode == 2) {
          bmode.textContent = "MODE: MANUAL OVERRIDE";
          bmode.style.background = "#fee2e2";
          bmode.style.color = "#dc2626";
          bmode.style.border = "1px solid #fca5a5";
      }
  }

  if (document.activeElement && document.activeElement.id !== 'inBalTrig' && d.balTrig !== undefined) document.getElementById('inBalTrig').value = d.balTrig;
  if (document.activeElement && document.activeElement.id !== 'inBalDelta' && d.balDelta !== undefined) document.getElementById('inBalDelta').value = d.balDelta;

  const sec = d.manStat & 0x03; 
  document.getElementById('statSeal').innerHTML = (sec === 3 ? "SEALED" : (sec === 2 ? "UNSEALED" : "FULL ACCESS"));
  document.getElementById('statSeal').style.color = (sec === 3 ? "#16a34a" : "#dc2626");
  
  document.getElementById('perfI2C').textContent = (d.p_i2c||0).toFixed(1) + ' ms';
  document.getElementById('perfEKF').textContent = (d.p_ekf||0).toFixed(1) + ' ms';
  document.getElementById('perfWeb').textContent = (d.p_web||0).toFixed(1) + ' ms';
  
  const loadMs = (d.p_i2c||0) + (d.p_ekf||0) + (d.p_web||0);
  const loadPct = (loadMs / 1000.0) * 100.0;
  const idle = Math.max(0, 1000.0 - loadMs);
  
  const ll = document.getElementById('vTotalLoad');
  ll.textContent = loadPct.toFixed(1) + "% (" + idle.toFixed(0) + "ms idle)";
  ll.style.color = loadPct > 80 ? '#dc2626' : loadPct > 30 ? '#d97706' : '#16a34a';

  if (!window.perfHist) window.perfHist = [];
  window.perfHist.push(loadMs);
  if(window.perfHist.length > 50) window.perfHist.shift();
  drawChart('chartPerf', 'Combined Load (ms/s)', [{label:'Load', color:'#16a34a', data:window.perfHist}], 80);
  
  const pModes = ["ACTIVE", "SLEEP", "DEEP SLEEP", "SHUTDOWN", "OFFLINE (I2C)"];
  const pendingNames = ["", "SLEEP", "DEEP SLEEP", "WAKING"];
  let curMode = pModes[d.pwr] || "OFFLINE";
  if (d.pendingPwr && d.pendingPwr > 0) curMode = "ENTERING " + (pendingNames[d.pendingPwr] || "?") + "...";
  document.getElementById('statPwr').innerHTML = curMode;
  document.getElementById('statPwr').style.color = (d.pwr === 4) ? "#9ca3ae" : ((d.pendingPwr > 0) ? "#d97706" : (d.pwr > 1 ? "#dc2626" : (d.pwr === 1 ? "#d97706" : "#2563eb")));
  
  document.getElementById('kvPack').innerHTML=pack.toFixed(2)+'<span class="kv-unit">V</span>';
  document.getElementById('kvMin').innerHTML=minV.toFixed(3)+'<span class="kv-unit">V</span>';
  document.getElementById('kvMax').innerHTML=maxV.toFixed(3)+'<span class="kv-unit">V</span>';
  const de=document.getElementById('kvDelta');de.innerHTML=delta+'<span class="kv-unit">mV</span>';de.style.color=parseInt(delta)>30?'#d97706':'#16a34a';
  const cur=d.current;const ce=document.getElementById('pCurrent');ce.innerHTML=(cur>0?'+':'')+cur+'<span class="cell-unit">mA</span>';ce.style.color=cur<0?'#ea580c':cur>0?'#2563eb':'#9ca3ae';
  const cs=document.getElementById('pCurState');cs.textContent=cur>0?'CHARGING':cur<0?'DISCHARGING':'IDLE';cs.style.color=cur>0?'#2563eb':cur<0?'#ea580c':'#9ca3ae';
  document.getElementById('pSoc').innerHTML=soc.toFixed(0)+'<span class="cell-unit">%</span>';document.getElementById('pSoc').style.color=soc<20?'#dc2626':soc<40?'#d97706':'#16a34a';
  const sb=document.getElementById('pSocBar');sb.style.width=soc+'%';sb.style.background=soc<20?'#dc2626':soc<40?'#d97706':'#16a34a';
  document.getElementById('pPower').innerHTML=(pack*Math.abs(cur)/1000).toFixed(2)+'<span class="cell-unit">W</span>';
  
  // TOSF Delta Logic
  const sumOfCells = Math.round(volts.reduce((a,b)=>a+b,0) * 1000);
  const stackMv = Math.round(d.vStack * 10);
  const tosDelta = Math.abs(stackMv - sumOfCells);
  document.getElementById('pTosDelta').innerHTML = tosDelta + '<span class="cell-unit">mV</span>';
  const tosb = document.getElementById('tosBox');
  const tost = document.getElementById('pTosState');
  if (tosDelta > 1000) {
      tosb.style.borderColor = '#fca5a5'; tosb.style.background = '#fef2f2';
      document.getElementById('pTosDelta').style.color = '#dc2626';
      tost.textContent = 'TRIPPED (>1V)'; tost.style.color = '#dc2626';
  } else if (tosDelta > 200) {
      tosb.style.borderColor = '#fde68a'; tosb.style.background = '#fef3c7';
      document.getElementById('pTosDelta').style.color = '#d97706';
      tost.textContent = 'WARNING'; tost.style.color = '#d97706';
  } else {
      tosb.style.borderColor = '#ebedf0'; tosb.style.background = '#f7f8fa';
      document.getElementById('pTosDelta').style.color = '#1a1d23';
      tost.textContent = 'OK'; tost.style.color = '#16a34a';
  }

  document.getElementById('kvCharge').innerHTML=d.charge.toFixed(1)+'<span class="kv-unit">mAh</span>';
  const tr = document.getElementById('kvTimeL');
  tr.textContent = cur>0?'Charge Time':cur<0?'Dsg Time':'Idle Time';
  document.getElementById('kvChargeTime').innerHTML=d.chargeTime+'<span class="kv-unit">s</span>';
  document.getElementById('kvStack').innerHTML=(d.vStack * 10 / 1000.0).toFixed(2)+'<span class="kv-unit">V</span>';
  document.getElementById('kvPackMv').innerHTML=(d.vPack * 10 / 1000.0).toFixed(2)+'<span class="kv-unit">V</span>';
  if (d.soc_ekf !== undefined) {
    document.getElementById('kvSOC_EKF').innerHTML = d.soc_ekf.toFixed(1) + '<span class="cell-unit">%</span>';
    document.getElementById('kvSOC_Unc').textContent = d.soc_uncertainty.toFixed(2);
  }
  
  let thermalThrottle = d.chipTemp > 60 || d.temp1 > 45 || d.temp2 > 45 || d.temp3 > 45;
  const tw = document.getElementById('thermalWarn');
  if(tw) tw.style.display = thermalThrottle ? 'block' : 'none';

  const br = document.getElementById('badgeRuleState');
  if(br) {
     if(cur > 10) { br.textContent = 'RULE: CHARGE'; br.className = 'badge badge-blue'; }
     else { br.textContent = 'RULE: RELAX'; br.className = 'badge badge-green'; }
  }

  [{id:'tTs1',v:d.temp1},{id:'tHDQ',v:d.temp2},{id:'tTs3',v:d.temp3},{id:'tChip',v:d.chipTemp}].forEach(t=>{const el=document.getElementById(t.id);el.textContent=t.v.toFixed(1)+'\u00B0C';el.style.color=tempColor(t.v);});
  
  const cT=document.getElementById('fetChgToggle'),cBadge=document.getElementById('fetChgBadge');
  if(d.isCharging){cT.className='toggle on';cBadge.textContent='ON';cBadge.className='badge badge-green';}else{cT.className='toggle off';cBadge.textContent='OFF';cBadge.className='badge badge-red';}
  const dT=document.getElementById('fetDsgToggle'),dBadge=document.getElementById('fetDsgBadge');
  if(d.isDischarging){dT.className='toggle on';dBadge.textContent='ON';dBadge.className='badge badge-green';}else{dT.className='toggle off';dBadge.textContent='OFF';dBadge.className='badge badge-red';}
  
  const abDDSG = document.getElementById('badgeDDSG'); if(abDDSG){abDDSG.textContent='DDSG: '+(d.ddsg?'ON':'OFF');abDDSG.className='badge '+(d.ddsg?'badge-blue':'badge-gray');}
  const abDCHG = document.getElementById('badgeDCHG'); if(abDCHG){abDCHG.textContent='DCHG: '+(d.dchg?'ON':'OFF');abDCHG.className='badge '+(d.dchg?'badge-blue':'badge-gray');}
  
  const sT=document.getElementById('autoSleepToggle'),sBadge=document.getElementById('autoSleepBadge');
  if(sT && sBadge) {
      if(d.autoSleep){sT.className='toggle on';sBadge.textContent='ALLOWED';sBadge.className='badge badge-green';}
      else{sT.className='toggle off';sBadge.textContent='DENIED';sBadge.className='badge badge-red';}
  }

  const fetEn = (d.manStat & 0x0010) !== 0; 
  const fmT = document.getElementById('fetMasterToggle'), fmB = document.getElementById('fetMasterBadge');
  if(fmT && fmB) { fmT.className = fetEn ? 'toggle on' : 'toggle off'; fmB.textContent = fetEn ? 'AUTO' : 'TEST MODE'; fmB.className = 'badge ' + (fetEn ? 'badge-green' : 'badge-blue'); }
  
  const testWarn = document.getElementById('testModeWarn');
  if (testWarn) testWarn.style.display = fetEn ? 'block' : 'none';
  ['btnTestPchg', 'btnTestPdsg', 'btnTestChg', 'btnTestDsg'].forEach(id => {
    const b = document.getElementById(id); if (b) b.disabled = fetEn;
  });
  document.getElementById('sbConn').innerHTML='&#x25CF; Connected';document.getElementById('sbConn').style.color='#16a34a';
  for(let i=0;i<CELLS;i++){vHist[i].push(volts[i]);if(vHist[i].length>HMAX)vHist[i].shift();}
  curHist.push(cur);if(curHist.length>HMAX)curHist.shift();
  tHist[0].push(d.temp1);tHist[1].push(d.temp2);tHist[2].push(d.temp3);for(let i=0;i<3;i++){if(tHist[i].length>HMAX)tHist[i].shift();}
  } catch(e) { 
    console.error("UI Update Error:", e);
  }
}
function fetchData(){fetch('/api/data').then(r=>r.text()).then(txt=>{try{const d=JSON.parse(txt);updateUI(d);const el=Math.floor((Date.now()-startTime)/1000);document.getElementById('sbElapsed').textContent=Math.floor(el/60)+':'+String(el%60).padStart(2,'0');document.getElementById('sbTx').textContent='Tx: '+d.txCount;document.getElementById('logTxCount').textContent='('+d.txCount+')';}catch(err){console.error("JSON PARSE ERROR. Raw text:", txt);}}).catch(e=>{console.error("Fetch Data Error:", e);document.getElementById('sbConn').innerHTML='&#x25CF; Disconnected';document.getElementById('sbConn').style.color='#dc2626';});}
function fetchLog(){fetch('/api/log').then(r=>r.json()).then(entries=>{const now=new Date();for(const e of entries){logs.push({ts:now.toLocaleTimeString()+'.'+String(now.getMilliseconds()).padStart(3,'0'),addr:e.reg,val:e.value,wr:e.isWrite,ok:e.ok});}if(logs.length>200)logs=logs.slice(-200);renderLog();}).catch(()=>{});}
function sendCmd(action){
  if(action==='pwrDeep') document.getElementById('sbConn').innerHTML='&#x25CF; DEEP SLEEP (Checking...)';
  if(action==='toggleBal') { document.getElementById('balModeBtn').textContent="UPDATING..."; }
  if(action==='toggleAutoSleep') { document.getElementById('autoSleepBadge').textContent="UPDATING..."; }
  fetch('/api/cmd?action='+action)
    .then(r=>r.json())
    .then(data=>{
      if(data.status==='error'){
        alert(data.message);
        if(action==='pwrDeep') document.getElementById('sbConn').innerHTML='&#x25CF; Connected'; 
      }
      if(data.status==='ok' && data.message){
        const rb = document.getElementById('restartBanner');
        const rm = document.getElementById('restartMsg');
        if(rb && rm) { rm.textContent = data.message; rb.style.display = 'block'; }
      }
      if(data.veto){
        alert("ACTION REJECTED BY BQ76952 HARDWARE!\n\nThe chip refused to turn on the " + data.veto + " FET.\nThis is usually because the chip is in SLEEP mode, or a safety fault is active.");
      }
      const fw = document.getElementById('fetWarn');
      if(fw) fw.style.display='none';
      setTimeout(fetchData,100);
    })
    .catch(()=>{
      const fw = document.getElementById('fetWarn');
      if(fw) fw.style.display='none';
      setTimeout(fetchData,100);
    });
}
function toggleFet(which){
  const fw = document.getElementById('fetWarn');
  fw.style.display='block';
  fw.textContent='[HW Lock] Evaluating BQ76952 safety constraints...';
  fw.style.color='#d97706';
  if(which==='chg'){
    sendCmd(document.getElementById('fetChgToggle').classList.contains('on')?'chgOff':'chgOn');
  }else{
    sendCmd(document.getElementById('fetDsgToggle').classList.contains('on')?'dsgOff':'dsgOn');
  }
}
function drawChart(svgId,title,lines,h){const svg=document.getElementById(svgId);if(!svg)return;const allVals=lines.reduce((acc, l) => acc.concat(l.data), []);if(allVals.length===0)return;const minV=Math.min(...allVals),maxV=Math.max(...allVals),range=maxV-minV||1;const w=700,p={t:26,r:12,b:24,l:48},pw=w-p.l-p.r,ph=h-p.t-p.b;const toX=(i,len)=>p.l+(i/(len-1))*pw,toY=(v)=>p.t+ph-((v-minV)/range)*ph;let s=`<rect x="${p.l}" y="${p.t}" width="${pw}" height="${ph}" fill="#f7f8fa" rx="3"/>`;for(let i=0;i<=4;i++){const v=minV+(range*i)/4,y=toY(v);s+=`<line x1="${p.l}" y1="${y}" x2="${p.l+pw}" y2="${y}" stroke="#e0e3e8" stroke-width="0.5"/><text x="${p.l-6}" y="${y+3}" fill="#9ca3ae" font-size="8" text-anchor="end" font-family="inherit">${v.toFixed(lines[0].dec||1)}</text>`;}['60s','45s','30s','15s','now'].forEach((l,i)=>{s+=`<text x="${p.l+(pw*i)/4}" y="${h-4}" fill="#9ca3ae" font-size="8" text-anchor="middle" font-family="inherit">${l}</text>`;});lines.forEach(line=>{if(line.data.length<2)return;s+=`<polyline points="${line.data.map((v,i)=>`${toX(i,line.data.length)},${toY(v)}`).join(' ')}" fill="none" stroke="${line.color}" stroke-width="1.5" stroke-linejoin="round" opacity="0.85"/>`;});s+=`<text x="${p.l+4}" y="${p.t-8}" fill="#5f6672" font-size="10" font-weight="600" font-family="inherit">${title}</text>`;let lx=p.l+pw-lines.length*72;lines.forEach((l,i)=>{const x=lx+i*72;s+=`<line x1="${x}" y1="${p.t-11}" x2="${x+12}" y2="${p.t-11}" stroke="${l.color}" stroke-width="2"/><text x="${x+16}" y="${p.t-8}" fill="#5f6672" font-size="8" font-family="inherit">${l.label}</text>`;});svg.innerHTML=s;}
function drawCharts(){
  const colors = ['#2563eb', '#16a34a', '#d97706', '#dc2626', '#7c3aed', '#0891b2', '#db2777', '#4f46e5', '#059669', '#ca8a04', '#ea580c', '#c026d3', '#2563eb'];
  let lines = [];
  for (let i = 0; i < CELLS; i++) {
    lines.push({
      label: 'C' + (i+1),
      color: colors[i % colors.length],
      data: vHist[i] || [],
      dec: 3
    });
  }
  drawChart('chartVoltage','Cell Voltages (V)',lines,200);
  drawChart('chartCurrent','Pack Current (mA)',[{label:'Current',color:'#ea580c',data:curHist,dec:1}],160);
  drawChart('chartTemp','Temperatures (\u00B0C)',[{label:'TS1',color:'#dc2626',data:tHist[0]||[],dec:1},{label:'TS2',color:'#7c3aed',data:tHist[1]||[],dec:1},{label:'TS3',color:'#0891b2',data:tHist[2]||[],dec:1}],160);
}
setInterval(()=>{fetchData();fetchLog();},2000);setInterval(drawCharts,2000);fetchData();
</script>
</body>
</html>
)rawliteral";

// ==================== WEB HANDLERS ====================
void handleRoot() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleApiData() {
  float current_A = (float)bmsCurrent / 10000.0f; // CC3 current is in 0.1mA units (100uA)
  float parasitic_correction_mv =
      (fabsf(current_A) > 0.05f) ? (current_A * 2.43f * 1000.0f) : 0.0f;

  String json;
  json.reserve(2048);

  json += "{\"v\":[";
  for (int i = 0; i < 16; i++) {
    float v = (float)cellVoltages[i];
    if (i < TB_CONNECTED_CELLS) {
      v -= parasitic_correction_mv;
      v = max(0.0f, v); // Prevent negative voltages
    }
    json += String((int)v); // Cast to int to ensure clean json numbers
    if (i < 15)
      json += ",";
  }
  json += "],\"vStack\":" + String(vStack);
  json += ",\"vPack\":" + String(vPack);
  json += ",\"current\":" + String((float)bmsCurrent / 10.0f, 1); // Display in mA with 0.1mA precision
  json += ",\"charge\":" + String(software_charge_Ah * 1000.0f, 1);
  json += ",\"chargeTime\":" + String(chargeTime);
  json += ",\"chipTemp\":" + String(chipTemp, 1);
  json += ",\"temp1\":" + String(temp1, 1);
  json += ",\"temp2\":" + String(temp2, 1);
  json += ",\"temp3\":" + String(temp3, 1);
  json += ",\"isCharging\":" + String(isCharging ? 1 : 0);
  json += ",\"isDischarging\":" + String(isDischarging ? 1 : 0);
  json += ",\"autoSleep\":" + String(autoSleepEnabled ? 1 : 0);
  json += ",\"fetEn\":" + String(fetEn ? 1 : 0);
  json += ",\"vLd\":" + String(ldPinVoltage);
  json += ",\"ldWait\":" + String((controlStatus & 0x1000) ? 1 : 0); // Bit 12
  json += ",\"safA\":" + String(ssA_val);
  json += ",\"safB\":" + String(ssB_val);
  json += ",\"safC\":" + String(ssC_val);
  json += ",\"f_pchg\":" + String((lastFetStat & 0x02) ? 1 : 0);
  json += ",\"f_pdsg\":" + String((lastFetStat & 0x08) ? 1 : 0);
  json += ",\"ledState\":" + String(ledState ? 1 : 0);
  json += ",\"prot_sc\":" + String(protStatus.bits.SC_DCHG ? 1 : 0);
  json += ",\"prot_oc2\":" + String(protStatus.bits.OC2_DCHG ? 1 : 0);
  json += ",\"prot_oc1\":" + String(protStatus.bits.OC1_DCHG ? 1 : 0);
  json += ",\"prot_occ\":" + String(protStatus.bits.OC_CHG ? 1 : 0);
  json += ",\"prot_ov\":" + String(protStatus.bits.CELL_OV ? 1 : 0);
  json += ",\"prot_uv\":" + String(protStatus.bits.CELL_UV ? 1 : 0);
  json += ",\"temp_otf\":" + String(tempStatus.bits.OVERTEMP_FET ? 1 : 0);
  json += ",\"temp_oti\":" + String(tempStatus.bits.OVERTEMP_INTERNAL ? 1 : 0);
  json += ",\"temp_otd\":" + String(tempStatus.bits.OVERTEMP_DCHG ? 1 : 0);
  json += ",\"temp_otc\":" + String(tempStatus.bits.OVERTEMP_CHG ? 1 : 0);
  json += ",\"temp_uti\":" + String(tempStatus.bits.UNDERTEMP_INTERNAL ? 1 : 0);
  json += ",\"temp_utd\":" + String(tempStatus.bits.UNDERTEMP_DCHG ? 1 : 0);
  json += ",\"temp_utc\":" + String(tempStatus.bits.UNDERTEMP_CHG ? 1 : 0);

  float cc_soc = 0.0f;
  if (initial_ekf_soc >= 0.0f) {
    cc_soc = constrain(initial_ekf_soc + (software_charge_Ah / 3.0f) * 100.0f,
                       0.0f, 100.0f);
  }
  json += ",\"cc_soc\":" + (isnan(cc_soc) ? String("0.0") : String(cc_soc, 1));
  json +=
      ",\"soc_ekf\":" + (isnan(soc_ekf) ? String("0.0") : String(soc_ekf, 1));
  json += ",\"soc_uncertainty\":" +
          (isnan(soc_uncertainty) ? String("0.0") : String(soc_uncertainty, 2));
  json +=
      ",\"vErr\":" +
      (isnan(voltage_error_ekf) ? String("0.0") : String(voltage_error_ekf, 1));

  json += ",\"hwBalActive\":" + String(isHardwareBalancing ? 1 : 0);
  json += ",\"balMode\":" + String(currentBalMode);
  json += ",\"balSuspended\":" + String(balSuspended ? 1 : 0);
  json += ",\"bal\":" + String(balancingMask);
  json += ",\"balTrig\":" + String(hostBalTriggerMv);
  json += ",\"balDelta\":" + String(hostBalDeltaMv);
  json += ",\"balTime\":" + String(totalBalancingTime);
  json += ",\"cellDelta\":" + String(cellBalancingDelta);
  json += ",\"minV\":" + String(minCellV);
  json += ",\"maxV\":" + String(maxCellV);
  json += ",\"p_i2c\":" + String(perf_i2c_us / 1000.0f, 1);
  json += ",\"p_ekf\":" + String(perf_ekf_us / 1000.0f, 1);
  json += ",\"p_web\":" + String(perf_web_us / 1000.0f, 1);
  json += ",\"cellBalTimes\":[";
  for (int i = 0; i < 16; i++) {
    json += String(cellBalancingTimes[i]);
    if (i < 15)
      json += ",";
  }
  json += "]";
  json += ",\"manStat\":" + String(manufStatus);
  json += ",\"pwr\":" + String(powerMode);
  json += ",\"txCount\":" + String(txCount);
  json += ",\"pendingPwr\":" + String(pendingPwrMode);
  json += ",\"ddsg\":" + String((lastFetStat & 0x20) ? 1 : 0);
  json += ",\"dchg\":" + String((lastFetStat & 0x10) ? 1 : 0);
  json +=
      ",\"balMaster\":" + (balancingEnabled ? String("true") : String("false"));
  json += ",\"wdFault\":" + String(watchdogFaultLocked ? 1 : 0);
  json += "}";

  // DEBUG: Length only (full JSON print overflows UART buffer)
  Serial.printf("[API] JSON %d bytes\n", json.length());

  server.send(200, "application/json", json);
}

void handleApiLog() {
  String json = "[";
  int count = min(logCount, 10);
  int idx = (logHead - count + LOG_SIZE) % LOG_SIZE;
  for (int i = 0; i < count; i++) {
    int pos = (idx + i) % LOG_SIZE;
    if (i > 0)
      json += ",";
    json += "{\"reg\":" + String(i2cLog[pos].reg);
    json += ",\"value\":" + String(i2cLog[pos].value);
    json += ",\"isWrite\":" + String(i2cLog[pos].isWrite ? "1" : "0");
    json += ",\"ok\":" + String(i2cLog[pos].ok ? "1" : "0") + "}";
  }
  json += "]";
  server.send(200, "application/json", json);

  // Prevent sending the same log entries to the UI on the next poll
  logCount = 0;
}

// BQ76952 FET_CONTROL subcommand 0x0097, 1-byte payload (TRM §12.5.6):
//   bit 0 = DSG_OFF, bit 1 = PDSG_OFF, bit 2 = CHG_OFF, bit 3 = PCHG_OFF
// "1" forces the FET off; "0" releases it so BQ autonomous control can enable it.
static uint8_t fetControlMask = 0x00;
static void writeFetControl(uint8_t mask) {
  fetControlMask = mask;
  bms.subCommandWriteData(0x0097, &mask, 1);
}

void handleApiCmd() {
  String action = server.arg("action");
  if (action == "chgOn") {
    Serial.println("=== CHG ON REQUEST ===");
    bms.CommandOnlysubCommand(0x001D); // 1. Clear Permanent Failures FIRST
    delay(20);
    writeFetControl(fetControlMask & ~0x04); // 2. Release CHG_OFF bit (TRM §12.5.6)

    uint16_t stat = 0;
    for (int i = 0; i < 5; i++) {
      delay(20);
      stat = bms.directCommandRead(0x7F);
      if ((stat & 0x01) != 0)
        break;
    }

    isCharging = (stat & 0x01) != 0;
    Serial.printf("[FET] CHG_ON complete: FETStatus=0x%04X, CHG_FET=%s\n", stat,
                  isCharging ? "ON" : "OFF");
    if (!isCharging)
      Serial.printf("[FET] BQ Vetoed CHG! Safety Status A: 0x%02X\n",
                    bms.directCommandRead(0x03));

  } else if (action == "chgOff") {
    Serial.println("=== CHG OFF REQUEST ===");
    writeFetControl(fetControlMask | 0x04); // Force CHG_OFF
    delay(50);

    uint16_t stat = bms.directCommandRead(0x7F);
    isCharging = (stat & 0x01) != 0; // SYNC UI INSTANTLY
    Serial.printf("[FET] CHG_OFF complete: FETStatus=0x%04X, CHG_FET=%s\n",
                  stat, isCharging ? "ON" : "OFF");

  } else if (action == "dsgOn") {
    Serial.println("=== DSG ON REQUEST ===");
    bms.CommandOnlysubCommand(0x001D); // 1. Clear Permanent Failures FIRST
    delay(20);
    writeFetControl(fetControlMask & ~0x01); // 2. Release DSG_OFF bit

    uint16_t stat = 0;
    for (int i = 0; i < 5; i++) {
      delay(20);
      stat = bms.directCommandRead(0x7F);
      if ((stat & 0x04) != 0)
        break;
    }

    isDischarging = (stat & 0x04) != 0; // SYNC UI INSTANTLY
    Serial.printf("[FET] DSG_ON complete: FETStatus=0x%04X, DSG_FET=%s\n", stat,
                  isDischarging ? "ON" : "OFF");
    if (!isDischarging)
      Serial.printf("[FET] BQ Vetoed DSG! Safety Status A: 0x%02X\n",
                    bms.directCommandRead(0x03));

  } else if (action == "dsgOff") {
    Serial.println("=== DSG OFF REQUEST ===");
    writeFetControl(fetControlMask | 0x01); // Force DSG_OFF
    delay(50);

    uint16_t stat = bms.directCommandRead(0x7F);
    isDischarging = (stat & 0x04) != 0; // SYNC UI INSTANTLY
    Serial.printf("[FET] DSG_OFF complete: FETStatus=0x%04X, DSG_FET=%s\n",
                  stat, isDischarging ? "ON" : "OFF");

  } else if (action == "allFetsOn") {
    // EXIT MAINTENANCE MODE: Wake FETs & purge EKF noise
    Serial.println("=== ALL FETS ON (EXIT MAINTENANCE) ===");
    bms.CommandOnlysubCommand(0x001D);
    delay(20);
    writeFetControl(0x00); // Release all FET force-off bits
    delay(100);
    uint16_t stat = bms.directCommandRead(0x7F);
    isCharging = (stat & 0x01) != 0;
    isDischarging = (stat & 0x04) != 0;

    // PURGE EKF: Pack was disconnected, voltage will bounce.
    float V_cell = (float)cellVoltages[0] / 1000.0f;
    float I_current = (float)bmsCurrent / 10000.0f;
    float soc_est = smartSOCInit(V_cell, I_current);
    initial_ekf_soc = soc_est;
    software_charge_Ah = 0.0f;
    ekf.begin(soc_est);
    Serial.printf("[EKF] Post-Maintenance Reset. SOC snapped to %.1f%%\n",
                  soc_est);

  } else if (action == "allFetsOff") {
    Serial.println("=== ALL FETS OFF (ENTER MAINTENANCE) ===");
    writeFetControl(0x05); // CHG_OFF | DSG_OFF
    delay(100);
    uint16_t stat = bms.directCommandRead(0x7F);
    isCharging = (stat & 0x01) != 0;
    isDischarging = (stat & 0x04) != 0;

  } else if (action == "clearFaults") {
    Serial.println("=== MANUAL FAULT CLEAR ===");
    clearBmsAlarms();
    watchdogFaultLocked = false;
    addLog(0x62, 0x00FF, true, true);
    Serial.println("[WATCHDOG] Faults cleared. ALERT LED unlocked.");
  } else if (action == "resetCharge") {
    bms.ResetAccumulatedCharge();
    addLog(0x3E, 0x0082, true, true);
  } else if (action == "toggleBal") {
    // RESTART TO APPLY: Save intention to NVS, don't touch BQ hardware
    int newMode = (currentBalMode == BAL_MODE_AUTONOMOUS) ? BAL_MODE_HOST_ALGO
                                                          : BAL_MODE_AUTONOMOUS;
    prefs.putInt("bal_mode", newMode);
    Serial.printf("[UI] Balancing mode saved to NVS: %d. Pending restart.\n",
                  newMode);
    String resp =
        "{\"status\":\"ok\",\"message\":\"Balancing mode updated!\\n\\nPlease "
        "click Reset BMS IC to apply this configuration safely.\"}";
    server.send(200, "application/json", resp);
    return;

  } else if (action == "toggleBalMaster") {
    balancingEnabled = !balancingEnabled;
    prefs.putBool("bal_master", balancingEnabled);
    Serial.printf("[UI] Master Balancing saved to NVS: %d.\n",
                  balancingEnabled);

    // If turned off, immediately kill hardware balancing
    if (!balancingEnabled) {
      if (powerMode == 1) {
        wakeBms();
        delay(10);
      } // Wake to turn OFF!
      bms.setBalancingMask(0);
    }

    String resp;
    if (currentBalMode == BAL_MODE_AUTONOMOUS) {
      resp = "{\"status\":\"ok\",\"message\":\"Master Balancing saved!\\n\\n"
             "Since you are in Autonomous Mode, you MUST click Reset BMS IC to "
             "safely apply this change.\"}";
    } else {
      resp = "{\"status\":\"ok\",\"message\":\"Master Balancing updated "
             "instantly!\"}";
    }
    server.send(200, "application/json", resp);
    return;

  } else if (action == "reset") {
    Serial.println("=== FULL SYSTEM RESET INITIATED ===");
    bms.reset();
    addLog(0x3E, 0x0012, true, true);
    server.send(200, "application/json",
                "{\"status\":\"ok\",\"message\":\"System rebooting... Please "
                "wait 5 seconds.\"}");
    delay(1000);
    ESP.restart();
  } else if (action == "toggleAutoSleep") {
    autoSleepEnabled = !autoSleepEnabled;
    prefs.putBool("auto_sleep", autoSleepEnabled);
    Serial.printf("[UI] Auto Sleep toggled: %d\n", autoSleepEnabled);
    if (autoSleepEnabled) {
      bms.CommandOnlysubCommand(0x0099); // SLEEP_ENABLE
    } else {
      bms.CommandOnlysubCommand(0x009A); // SLEEP_DISABLE
    }
    String resp =
        "{\"status\":\"ok\",\"message\":\"Sleep mode toggled successfully!\"}";
    server.send(200, "application/json", resp);
    return;
  } else if (action == "pwrDeep") {
    Serial.println("============================================");
    Serial.println("[PWR] >>> DEEP SLEEP command received from dashboard");

    // 1. Read LD Pin Voltage (Direct Command 0x38 = VLD)
    int16_t vld = (int16_t)bms.directCommandRead(0x38); // returns mV
    if (vld > 1500) {
      Serial.printf("[PWR] DEEP SLEEP VETO: LD pin is HIGH (%d mV)\n", vld);
      String resp =
          "{\"status\":\"error\",\"message\":\"DEEP SLEEP WARNING:\\n\\nThe LD "
          "(Load Detect) pin on this testboard is currently reading HIGH (" +
          String(vld) +
          " mV).\\n\\nThe BQ76952 cannot stay in Deep Sleep while the LD pin "
          "is driven high (e.g. charger attached).\"}";
      server.send(200, "application/json", resp);
      return;
    }

    // 2. Check for active safety faults
    uint16_t ssA = bms.directCommandRead(0x03);
    uint16_t ssB = bms.directCommandRead(0x05);
    uint16_t ssC = bms.directCommandRead(0x07);
    if (ssA > 0 || ssB > 0 || ssC > 0) {
      Serial.printf(
          "[PWR] DEEP SLEEP VETO: Faults active (A:%02X B:%02X C:%02X)\n", ssA,
          ssB, ssC);
      String resp =
          "{\"status\":\"error\",\"message\":\"DEEP SLEEP WARNING:\\n\\nThe "
          "BQ76952 cannot enter Deep Sleep mode while safety faults are "
          "active.\\n\\nPlease clear all protections first!\"}";
      server.send(200, "application/json", resp);
      return;
    }

    pendingPwrMode = 2;

    // 1. MUST wake the chip first, or it ignores balancing commands!
    bms.CommandOnlysubCommand(0x009A); // SLEEP_DISABLE
    delay(50);

    // 2. MUST kill balancing, or Deep Sleep is permanently blocked!
    bms.writeByteToMemory(0x9335, 0x00); // HARD KILL Autonomous Balancing
    bms.setBalancingMask(0);             // HARD KILL Host Balancing
    delay(150); // Allow physical balancing FETs time to turn off

    // 3. Clear leftover alarms so FULLSCAN doesn't trigger a false
    // readBMSData()
    bms.directCommandWrite(0x62, 0xFF);
    bms.directCommandWrite(0x63, 0xFF);
    delay(20);

    // 4. Send DEEPSLEEP (x2)
    bms.CommandOnlysubCommand(0x000F);
    delay(500);
    bms.CommandOnlysubCommand(0x000F);

    lastRead = millis();       // Prevent immediate race-condition read
    pwrCommandTime = millis(); // Timestamp for DEEPSLEEP detection in loop()
    Serial.println("[PWR] DEEPSLEEP sent (x2). I2C dying...");
    Serial.println("============================================");

  } else if (action == "pwrWake") {
    Serial.println("============================================");
    Serial.println("[PWR] >>> WAKE command received from dashboard");

    bms.directCommandRead(
        0x12); // DUMMY READ: Wake up the I2C communication engine!
    delay(20);

    bms.CommandOnlysubCommand(0x009A); // SLEEP_DISABLE
    delay(50);
    bms.CommandOnlysubCommand(
        0x009A); // DOUBLE SEND: Guarantee receipt even if it was deep asleep
    delay(50);
    bms.CommandOnlysubCommand(0x000E); // EXIT_DEEPSLEEP
    delay(1000); // Wait 1 second for BQ to complete its measurement loop!

    // Restore balancing configuration to what the dashboard currently expects
    bms.writeByteToMemory(
        0x9335, (balancingEnabled && currentBalMode == BAL_MODE_AUTONOMOUS)
                    ? 0x0F
                    : 0x00);
    delay(50);

    // Kickstart the ALERT heartbeat in case it hung during sleep
    bms.directCommandWrite(0x66, 0xFF);
    bms.directCommandWrite(0x67, 0xFF);
    bms.directCommandWrite(0x62, 0xFF);
    bms.directCommandWrite(0x63, 0xFF);

    // Don't set powerMode=0 yet — wait for BatStatus SLEEP bit to confirm!
    pendingPwrMode =
        3; // WAKE PENDING: readBMSData will confirm when bit 15 = 0
    pwrCommandTime = millis();
    lastRead = millis(); // Allow polling immediately to catch the confirmation
    Serial.println(
        "[PWR] Wake routine complete. Waiting for BatStatus confirmation...");
    Serial.println("============================================");
  } else if (action == "pchgTog") {
    // Manual PCHG utilizing TEST bit (Bit 2 of 0x7E)
    uint16_t current = bms.directCommandRead(0x7E);
    bms.directCommandWrite(0x7E, current ^ 0x04); 
    Serial.printf("[TEST] PCHG (TEST bit) Toggle. Reg 0x7E: 0x%02X\n", current ^ 0x04);
  } else if (action == "pdsgTog") {
    // Manual PDSG utilizing TEST bit (Bit 3 of 0x7E)
    uint16_t current = bms.directCommandRead(0x7E);
    bms.directCommandWrite(0x7E, current ^ 0x08); 
    Serial.printf("[TEST] PDSG (TEST bit) Toggle. Reg 0x7E: 0x%02X\n", current ^ 0x08);
  } else if (action == "chgTog") {
    // Manual CHG utilizing TEST bit (Bit 0 of 0x7E)
    uint16_t current = bms.directCommandRead(0x7E);
    bms.directCommandWrite(0x7E, current ^ 0x01);
    Serial.printf("[TEST] CHG (TEST bit) Toggle. Reg 0x7E: 0x%02X\n", current ^ 0x01);
  } else if (action == "dsgTog") {
    // Manual DSG utilizing TEST bit (Bit 1 of 0x7E)
    uint16_t current = bms.directCommandRead(0x7E);
    bms.directCommandWrite(0x7E, current ^ 0x02);
    Serial.printf("[TEST] DSG (TEST bit) Toggle. Reg 0x7E: 0x%02X\n", current ^ 0x02);
  } else if (action == "fetMasterToggle") {
    bms.CommandOnlysubCommand(0x0022);
    Serial.println("[TB] FET Master Toggle (0x0022)");
  } else if (action == "setHostBalParams") {
    hostBalTriggerMv = server.arg("trigger").toInt();
    hostBalDeltaMv = server.arg("delta").toInt();
    prefs.putUShort("trig", hostBalTriggerMv);
    prefs.putUShort("delta", hostBalDeltaMv);
    Serial.printf("[HOST-BAL] Settings Updated: Trigger=%dmV, Delta=%dmV\n",
                  hostBalTriggerMv, hostBalDeltaMv);
  } else {
    server.send(400, "application/json",
                "{\"status\":\"error\",\"message\":\"Unknown\"}");
    return;
  }
  String resp = "{\"status\":\"ok\"";
  if (action == "chgOn" && !isCharging)
    resp += ",\"veto\":\"CHG\"";
  resp += "}";
  server.send(200, "application/json", resp);
}

void handleApiEKFReset() {
  if (server.method() != HTTP_POST && server.method() != HTTP_GET) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }

  float V_cell = (float)cellVoltages[0] / 1000.0f;
  float I_current = (float)bmsCurrent / 10000.0f;
  float soc_est = smartSOCInit(V_cell, I_current);

  initial_ekf_soc = soc_est;
  software_charge_Ah = 0.0f; // Reset our robust software CC!
  bms.ResetAccumulatedCharge();
  ekf.begin(soc_est);

  String response = "{\"status\":\"ok\",\"new_soc\":" + String(soc_est) + "}";
  server.send(200, "application/json", response);

  Serial.printf("[EKF] Manual Reset to %.1f%%\n", soc_est);
}
