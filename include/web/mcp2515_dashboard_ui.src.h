#pragma once
#ifdef ESP_PLATFORM
#include "platform/espidf_runtime.h"
#else
#include <Arduino.h>
#endif

static const char DASH_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="zh-CN" data-theme="dark">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>ev-open-can-tools</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
[data-theme="dark"]{
  --bg:#101113;--bg2:#15171a;--card:#1b1e23;--card2:#242832;
  --bd:#303641;--bd2:#45505e;
  --tx:#f7f4ec;--tx2:#b9c0cb;--tx3:#7f8997;
  --acc:#78a8ff;--accBg:rgba(120,168,255,.13);--accBd:rgba(120,168,255,.36);
  --ok:#3dba72;--okBg:rgba(61,186,114,.1);
  --err:#ff4f4f;--errBg:rgba(255,79,79,.08);--errBd:rgba(255,79,79,.2);
  --warn:#f5a623;--gold:#d8b45f;--goldBg:rgba(216,180,95,.12);--goldBd:rgba(216,180,95,.28);
  --shadow:0 14px 34px rgba(0,0,0,.25);
}
[data-theme="light"]{
  --bg:#fbf7ed;--bg2:#f6eedf;--card:#fffdfa;--card2:#f3ead9;
  --bd:#e5dac7;--bd2:#cabda8;
  --tx:#151922;--tx2:#5f6975;--tx3:#9098a3;
  --acc:#2563eb;--accBg:rgba(37,99,235,.08);--accBd:rgba(37,99,235,.22);
  --ok:#16a34a;--okBg:rgba(22,163,74,.08);
  --err:#dc2626;--errBg:rgba(220,38,38,.06);--errBd:rgba(220,38,38,.18);
  --warn:#d97706;--gold:#a66b13;--goldBg:rgba(166,107,19,.1);--goldBd:rgba(166,107,19,.22);
  --shadow:0 12px 28px rgba(86,68,38,.09);
}
html{scroll-behavior:smooth}
body{background:var(--bg);color:var(--tx);font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;
  min-height:100vh;max-width:480px;margin:0 auto;font-size:14px;line-height:1.5;
  transition:background .2s,color .2s}
@media (min-width:700px){
  body{width:90vw;max-width:1180px}
}

/* Header */
.hdr{padding:20px 16px 0;display:flex;flex-direction:column;gap:4px}
.hdr-top{display:flex;align-items:center;justify-content:space-between}
.hdr-left{display:flex;align-items:center;gap:8px;flex-wrap:wrap;min-width:0}
.hdr-title{font-size:20px;font-weight:700;color:var(--tx)}
.hw-badge{padding:3px 8px;border-radius:7px;font-size:11px;font-weight:700;
  background:var(--accBg);border:1px solid var(--accBd);color:var(--acc)}
.gtw-badge{padding:3px 8px;border-radius:7px;font-size:11px;font-weight:700;
  background:var(--card);border:1px solid var(--bd2);color:var(--tx2)}
.gtw-badge.known{color:var(--ok);border-color:rgba(61,186,114,.25);background:var(--okBg)}
.theme-btn{padding:6px 10px;border:1px solid var(--bd2);border-radius:8px;
  background:var(--card);color:var(--tx2);font-size:12px;cursor:pointer;
  display:flex;align-items:center;gap:4px;transition:all .2s}
.theme-btn:hover{border-color:var(--acc);color:var(--acc)}
.hdr-status{display:flex;align-items:center;gap:6px;font-size:12px;color:var(--tx2)}
.sdot{width:7px;height:7px;border-radius:50%;flex-shrink:0;transition:all .4s}
.dot-on{background:var(--ok);box-shadow:0 0 8px var(--ok)}
.dot-off{background:var(--err)}
.dot-warn{background:var(--warn)}

/* FPS bar */
.fps-bar{margin:14px 16px 0;height:3px;background:var(--bd);border-radius:2px;overflow:hidden}
.fps-fill{height:100%;background:var(--ok);border-radius:2px;transition:width .5s,background .3s;width:0%}

/* Status grid */
.stat-grid{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px;margin:14px 16px 0}
.stat{background:var(--card);border:1px solid var(--bd);border-radius:10px;padding:10px 12px;box-shadow:0 1px 0 rgba(255,255,255,.035) inset}
.stat-lbl{font-size:10px;color:var(--tx3);text-transform:uppercase;letter-spacing:.8px;margin-bottom:3px}
.stat-val{font-size:14px;font-weight:600;color:var(--tx)}
.v-ok{color:var(--ok)}.v-err{color:var(--err)}.v-acc{color:var(--acc)}.v-dim{color:var(--tx3)}.v-warn{color:var(--warn)}
.stat-wide{grid-column:span 3}
.sys-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}
.sys-item{background:var(--bg2);border:1px solid var(--bd);border-radius:8px;padding:8px 10px;min-width:0}
.sys-lbl{font-size:10px;color:var(--tx3);text-transform:uppercase;letter-spacing:.5px;margin-bottom:2px}
.sys-val{font-size:12px;font-weight:600;color:var(--tx);word-break:break-word}
.sys-wide{grid-column:span 2}
.sys-full{grid-column:span 2}
.sys-bar{height:4px;background:var(--bd);border-radius:2px;overflow:hidden;margin-top:6px}
.sys-fill{height:100%;background:var(--ok);border-radius:2px;transition:width .3s,background .3s;width:0}
.sys-fill.warn{background:var(--warn)}
.sys-fill.err{background:var(--err)}
.sys-fill.dim{background:var(--tx3)}
.sys-mini{display:flex;align-items:center;gap:6px;min-width:0}
.sys-mini-bar{height:4px;flex:1;background:var(--bd);border-radius:2px;overflow:hidden}
.sys-mini-fill{height:100%;background:var(--ok);border-radius:2px;width:0;transition:width .3s,background .3s}
.sys-mini-fill.warn{background:var(--warn)}
.sys-mini-fill.err{background:var(--err)}
@media (min-width:900px){
  .sys-grid{grid-template-columns:repeat(4,minmax(0,1fr))}
  .sys-full{grid-column:span 4}
}
.sys-monitor{display:flex;align-items:center;justify-content:flex-end;gap:8px}
.sys-monitor span{white-space:nowrap}
.sys-monitor .tgl{margin-left:0}
/* Divider */
hr{border:none;border-top:1px solid var(--bd);margin:16px}

/* Cards */
.card{background:var(--card);border:1px solid var(--bd);border-radius:10px;padding:16px;margin:0 16px 12px;overflow:hidden;box-shadow:var(--shadow)}
.card-hdr{display:grid;grid-template-columns:minmax(0,1fr) auto auto;align-items:center;column-gap:8px;margin-bottom:14px}
.card-title{font-size:13px;font-weight:600;color:var(--tx);text-transform:uppercase;letter-spacing:.5px;min-width:0}
.card-meta{font-size:11px;color:var(--tx3);justify-self:end;text-align:right;min-width:0}
.card-min-btn{padding:4px 8px;font-size:10px;justify-self:end}
.card.collapsed{padding-bottom:12px}
.card.collapsed .card-hdr{margin-bottom:0}
.card.collapsed>:not(.card-hdr){display:none !important}
.subsec{margin-top:14px;padding-top:12px;border-top:1px solid var(--bd)}
.subsec:first-child{margin-top:0;padding-top:0;border-top:none}
.subsec-head{display:grid;grid-template-columns:minmax(110px,1fr) auto auto;align-items:center;column-gap:8px;margin-bottom:8px}
.subsec-title{font-size:13px;font-weight:600;color:var(--tx);min-width:0;word-break:keep-all}
.title-help{display:inline-flex;align-items:center;justify-content:center;width:16px;height:16px;margin-left:6px;border:1px solid var(--bd2);border-radius:50%;font-size:10px;font-weight:700;color:var(--tx3);cursor:pointer;vertical-align:middle;line-height:1;background:transparent}
.title-help:hover{border-color:var(--accBd);color:var(--acc);background:var(--accBg)}
.info-box{margin-bottom:10px;padding:10px 12px;background:var(--bg2);border:1px solid var(--bd);border-radius:9px;font-size:12px;color:var(--tx3);line-height:1.6}
.info-box a{color:var(--acc);text-decoration:none}
.inline-help-panel{display:none;margin:8px 0 0;padding:10px 12px;background:var(--bg2);border:1px solid var(--bd);border-radius:9px;font-size:12px;color:var(--tx3);line-height:1.6}
.inline-help-panel.show{display:block}
.subsec-meta{font-size:11px;color:var(--tx3);justify-self:end;text-align:right;min-width:0}
.subsec-btn{padding:4px 8px;font-size:10px;justify-self:end}
.subsec.collapsed .subsec-head{margin-bottom:0}
.subsec.collapsed .subsec-body{display:none}

/* HW seg */
.hw-seg{display:flex;background:var(--bg2);border:1px solid var(--bd);border-radius:10px;padding:3px;gap:2px}
.hw-btn{flex:1;padding:8px;border:none;border-radius:7px;font-size:12px;font-weight:600;
  cursor:pointer;background:transparent;color:var(--tx2);transition:all .18s;font-family:inherit}
.hw-btn.active{background:var(--card);color:var(--acc);border:1px solid var(--accBd);
  box-shadow:0 1px 8px rgba(0,0,0,.10)}
.hw-btn:hover:not(.active){background:var(--card2);color:var(--tx)}

/* Speed pills */
.pills{display:flex;gap:6px;flex-wrap:wrap}
/* Settings rows */
.setting-row{display:flex;align-items:center;justify-content:space-between;
  padding:12px 0;border-bottom:1px solid var(--bd)}
.setting-row:last-of-type{border-bottom:none;padding-bottom:0}
.setting-row:first-of-type{padding-top:0}
.setting-info{flex:1;min-width:0}
.setting-name{font-size:13px;font-weight:500;color:var(--tx)}
.setting-desc{font-size:11px;color:var(--tx3);margin-top:2px}

/* Toggle */
.tgl{position:relative;width:44px;height:24px;flex-shrink:0;margin-left:12px}
.tgl input{opacity:0;width:0;height:0;position:absolute}
.tgl-track{position:absolute;inset:0;background:var(--bd2);border-radius:24px;cursor:pointer;transition:all .22s}
.tgl-thumb{position:absolute;top:3px;left:3px;width:18px;height:18px;background:#fff;
  border-radius:50%;transition:all .22s;box-shadow:0 1px 3px rgba(0,0,0,.3)}
.tgl input:checked~.tgl-track{background:var(--acc)}
.tgl input:checked~.tgl-track .tgl-thumb{transform:translateX(20px)}
.tgl input:disabled~.tgl-track{opacity:.35;cursor:not-allowed}

/* Form controls */
.sniff-input{flex:1;background:var(--bg);border:1px solid var(--bd);border-radius:8px;
  padding:7px 10px;color:var(--tx);font-size:12px;font-family:inherit;transition:border .2s}
.sniff-input{width:100%;min-width:0;box-sizing:border-box;} 
.sniff-input:focus{outline:none;border-color:var(--acc);box-shadow:0 0 0 3px var(--accBg)}
.sniff-input::placeholder{color:var(--tx3)}
.sniff-btn{padding:7px 12px;background:var(--card);border:1px solid var(--bd);border-radius:8px;
  color:var(--tx2);font-size:11px;font-weight:600;cursor:pointer;transition:all .18s;font-family:inherit}
.sniff-btn:hover{border-color:var(--bd2);color:var(--tx)}
.nag-mode-control{width:168px;flex:0 0 168px}
.nag-range-grid{display:grid;grid-template-columns:1fr 1fr auto;gap:6px;width:260px;max-width:100%}
.nag-range-grid .sniff-input{text-align:right}
.nag-range-grid .sniff-btn{white-space:nowrap}
.nag-torque-status{display:inline-flex;flex-wrap:wrap;gap:5px;margin-top:5px}
.nag-status-pill{display:inline-flex;padding:2px 6px;border:1px solid var(--bd);border-radius:6px;background:var(--bg2);color:var(--tx2);line-height:1.4}
.can-diag-row{align-items:stretch}
.can-diag-row .setting-info{width:100%}
.can-diag-grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:6px;margin-top:9px}
.can-diag-item{min-width:0;padding:7px 8px;border:1px solid var(--bd);border-radius:7px;background:var(--bg2)}
.can-diag-label{font-size:10px;color:var(--tx3);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.can-diag-value{margin-top:2px;font-size:12px;font-weight:700;color:var(--tx2);font-family:monospace;word-break:break-word}
.can-diag-actions{display:flex;justify-content:flex-end;margin-top:8px}
.gateway-profile-btn.active,.gateway-upstream-btn.active{background:var(--accBg);border-color:var(--acc);color:var(--acc);box-shadow:0 0 0 1px var(--accBd) inset}
/* Buttons */
.btn-row{display:flex;gap:8px;margin-top:14px}
.btn{flex:1;padding:10px;border:1px solid;border-radius:9px;background:transparent;
  font-family:inherit;font-size:12px;font-weight:600;cursor:pointer;transition:all .18s;letter-spacing:.3px}
.btn-stop{border-color:var(--errBd);color:var(--err)}
.btn-stop:hover{background:var(--errBg)}
.btn-reboot{border-color:var(--bd2);color:var(--tx2)}
.btn-reboot:hover{border-color:var(--acc);color:var(--acc)}
.stat-grid>.btn{min-height:auto;padding:10px 12px;border-radius:10px;background:var(--card);text-align:left;
  display:flex;align-items:flex-start;justify-content:flex-start;font-size:14px;font-weight:600;letter-spacing:0;line-height:1.35}
.stat-grid>.btn:hover{background:var(--card2)}
body.wifi-nag .stat-grid>.btn{min-height:48px;padding:8px 12px}
body.wifi-nag .stat-grid>.btn-reboot{align-items:center;justify-content:center;text-align:center}

/* Confirm modal */
.modal-backdrop{position:fixed;inset:0;display:none;align-items:center;justify-content:center;
  padding:16px;background:rgba(0,0,0,.55);z-index:9999}
.modal-card{width:min(100%,360px);background:var(--card);border:1px solid var(--bd2);
  border-radius:12px;padding:16px;box-shadow:0 16px 40px rgba(0,0,0,.35)}
.modal-title{font-size:14px;font-weight:700;color:var(--tx)}
.modal-msg{margin-top:8px;font-size:12px;color:var(--tx2);line-height:1.6;white-space:pre-wrap}
.modal-actions{display:flex;justify-content:flex-end;gap:8px;margin-top:14px}
.modal-btn-primary{background:var(--accBg);border-color:var(--accBd);color:var(--acc)}
.modal-btn-primary:hover{background:var(--acc);color:#fff}
.safety-modal-card{width:min(100%,460px)}
.safety-body{margin-top:10px;max-height:68vh;overflow:auto;font-size:12px;color:var(--tx2);line-height:1.7}
.safety-body p{margin:0 0 10px}
.safety-body p:last-child{margin-bottom:0}
.safety-strong{display:block;margin:8px 0;color:var(--err);font-size:2em;font-weight:900;line-height:1.35;word-break:break-word}
.safety-actions{justify-content:center}
.safety-actions .sniff-btn{min-width:140px}
.dns-modal-card{width:min(100%,640px)}
.dns-modal-list{margin-top:10px;max-height:60vh;overflow:auto;border:1px solid var(--bd);border-radius:9px;padding:8px;background:var(--bg)}
.dns-row{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:10px;align-items:center;padding:10px 8px;border-bottom:1px solid var(--bd)}
.dns-row:last-child{border-bottom:none}
.dns-domain{min-width:0;overflow:hidden;text-overflow:ellipsis;color:var(--tx);font-family:'SF Mono','Courier New',monospace;font-size:12px}
.dns-count{color:var(--tx3);font-size:10px;margin-left:6px}
.dns-state{font-size:11px;font-weight:600;white-space:nowrap}
.dns-state.err{color:var(--err)}
.dns-state.ok{color:var(--ok)}
.dns-state.dim{color:var(--tx3)}

/* OTA upload */
.ota-drop{border:2px dashed var(--bd2);border-radius:10px;padding:24px 16px;
  text-align:center;cursor:pointer;transition:all .2s;background:var(--bg)}
.ota-drop:hover,.ota-drop.drag{border-color:var(--acc);background:var(--accBg)}
.ota-drop input{display:none}
.ota-icon{font-size:24px;margin-bottom:8px}
.ota-text{font-size:13px;font-weight:500;color:var(--tx2);margin-bottom:3px}
.ota-sub{font-size:11px;color:var(--tx3)}
.ota-progress{margin-top:12px;display:none}
.ota-bar{height:4px;background:var(--bd);border-radius:2px;overflow:hidden;margin-bottom:6px}
.ota-fill{height:100%;background:var(--ok);border-radius:2px;transition:width .3s,background .3s;width:0%}
.ota-status{font-size:11px;color:var(--acc);text-align:center}
.ota-btn{width:100%;margin-top:10px;padding:10px;border:1px solid var(--accBd);border-radius:9px;
  background:var(--accBg);color:var(--acc);font-family:inherit;font-size:13px;font-weight:600;
  cursor:pointer;transition:all .2s;display:none}
.ota-btn:hover{background:var(--acc);color:#fff}

/* Log */
.log-box{background:var(--bg);border:1px solid var(--bd);border-radius:9px;padding:10px 12px;
  font-family:'SF Mono','Courier New',monospace;font-size:11px;color:var(--tx2);
  max-height:180px;overflow-y:auto;line-height:1.9;white-space:pre-wrap;word-break:break-all}
.log-box::-webkit-scrollbar{width:4px}
.log-box::-webkit-scrollbar-thumb{background:var(--bd2);border-radius:4px}
.lf{color:var(--ok)}.lh{color:var(--acc)}.le{color:var(--err)}.lc{color:var(--warn)}.lo{color:var(--tx2)}

/* Warning */
.warn-bar{margin:0 16px 14px;padding:10px 14px;border-radius:9px;
  background:var(--errBg);border:1px solid var(--errBd);font-size:11px;color:var(--err);line-height:1.7}
.foot{text-align:center;padding:8px 16px 20px;font-size:11px;color:var(--tx3)}
.ui-mode-strip{margin:10px 16px 0;padding:8px;border:1px solid var(--bd);border-radius:10px;background:var(--card);
  display:flex;align-items:center;gap:8px;flex-wrap:wrap}
.ui-mode-label{font-size:10px;text-transform:uppercase;letter-spacing:.8px;color:var(--tx3);font-weight:700}
.ui-mode-buttons{display:flex;gap:4px;flex-wrap:wrap}
.ui-mode-btn{padding:6px 10px;border:1px solid var(--bd);border-radius:8px;background:var(--bg);
  color:var(--tx2);font-size:11px;font-weight:700;font-family:inherit;cursor:pointer}
.ui-mode-btn.active{background:var(--accBg);border-color:var(--acc);color:var(--acc);box-shadow:0 0 0 1px var(--accBd) inset}
.ui-mode-detected{font-size:10px;color:var(--tx3);margin-left:auto}
.car-side{display:none}
.nag-nav-only{display:none !important}
body.ui-car{width:100vw;max-width:none;margin:0;padding-left:204px;font-size:16px;line-height:1.55}
body.ui-car .car-side{position:fixed;left:0;top:0;bottom:0;width:188px;display:flex;flex-direction:column;gap:9px;
  padding:16px 12px;background:linear-gradient(180deg,var(--card),var(--bg2));border-right:1px solid var(--bd);z-index:1000;box-shadow:8px 0 28px rgba(0,0,0,.05)}
body.ui-car .car-side-title{font-size:17px;font-weight:900;color:var(--tx);margin:4px 8px 2px;letter-spacing:.2px}
body.ui-car .car-side-sub{font-size:10px;color:var(--tx3);margin:0 8px 10px;line-height:1.35}
body.ui-car .car-nav-btn{min-height:50px;padding:10px 12px;border:1px solid var(--bd);border-radius:12px;background:var(--card);
  color:var(--tx2);font-size:13px;font-weight:800;text-align:left;font-family:inherit;cursor:pointer;display:flex;align-items:center;gap:10px;
  box-shadow:0 1px 0 rgba(255,255,255,.04) inset;transition:border .16s,background .16s,color .16s,transform .16s}
body.ui-car .car-nav-icon{width:28px;height:28px;border-radius:9px;display:inline-flex;align-items:center;justify-content:center;
  flex:0 0 28px;border:1px solid var(--bd);background:var(--bg2);color:var(--gold)}
body.ui-car .car-nav-icon svg{width:16px;height:16px;display:block;stroke:currentColor;stroke-width:2;fill:none;stroke-linecap:round;stroke-linejoin:round}
body.ui-car .car-nav-btn:active,body.ui-car .car-nav-btn:hover{border-color:var(--accBd);color:var(--acc);background:var(--accBg);transform:translateX(1px)}
body.ui-car .car-nav-btn:active .car-nav-icon,body.ui-car .car-nav-btn:hover .car-nav-icon{border-color:var(--accBd);color:var(--acc);background:var(--card)}
body.ui-car .car-nav-btn:focus-visible{outline:none;box-shadow:0 0 0 3px var(--accBg),0 0 0 1px var(--accBd) inset}
body.ui-car .hdr{padding:18px 24px 0}
body.ui-car .hdr-title{font-size:24px}
body.ui-car .theme-btn,body.ui-car .sniff-btn,body.ui-car .btn,body.ui-car .hw-btn,body.ui-car .ui-mode-btn{min-height:44px;font-size:14px;padding:10px 14px;border-radius:11px}
body.ui-car .ui-mode-strip{margin:12px 24px 0;padding:10px 12px;gap:10px}
body.ui-car .stat-grid{margin:16px 24px 0;grid-template-columns:repeat(6,minmax(0,1fr));gap:10px}
body.ui-car .stat{padding:12px 14px;border-radius:12px;box-shadow:0 1px 0 rgba(255,255,255,.04) inset}
body.ui-car .stat-lbl{font-size:11px}
body.ui-car .stat-val{font-size:16px}
body.ui-car .card{margin:0 24px 14px;padding:18px;border-radius:12px}
body.ui-car .card-title{font-size:15px}
body.ui-car .card-meta,body.ui-car .subsec-meta{font-size:12px}
body.ui-car .subsec{margin-top:18px;padding-top:16px}
body.ui-car .subsec-head{grid-template-columns:minmax(180px,1fr) auto auto}
body.ui-car .subsec-title{font-size:15px}
body.ui-car .setting-row{padding:16px 0;gap:14px}
body.ui-car .setting-name{font-size:15px}
body.ui-car .setting-desc{font-size:12px}
body.ui-car .sniff-input{min-height:44px;font-size:15px;padding:10px 12px;border-radius:11px}
body.ui-car textarea.sniff-input{min-height:120px}
body.ui-car .sys-grid{grid-template-columns:repeat(4,minmax(0,1fr));gap:10px}
body.ui-car .sys-wide{grid-column:span 2}
body.ui-car .sys-full{grid-column:span 4}
body.ui-car .modal-card{width:min(100%,560px);border-radius:16px}
body.ui-car *{transition:none !important;animation:none !important;scroll-behavior:auto !important}
@media (max-width:900px){
  body.ui-car{padding-left:0}
  body.ui-car .car-side{display:none}
  body.ui-car .stat-grid{grid-template-columns:repeat(3,1fr);margin-left:16px;margin-right:16px}
  body.ui-car .card,body.ui-car .hdr,body.ui-car .ui-mode-strip{margin-left:16px;margin-right:16px}
}
.nag-only{display:none !important}
body.wifi-nag .nag-only.setting-row{display:flex !important}
body.wifi-nag .nag-nav-only{display:flex !important}
body.wifi-nag #hw-badge{font-size:0}
body.wifi-nag #hw-badge::after{content:'WIFI-NAG';font-size:11px}
body.wifi-nag .hdr-title{font-weight:800;letter-spacing:.2px}
body.wifi-nag .hw-badge{border-color:var(--goldBd);background:var(--goldBg);color:var(--gold)}
body.wifi-nag #config-hardware-section{padding-top:2px;border-top:0}
body.wifi-nag #config-hardware-section .subsec-head{padding:10px 0 8px;border-bottom:1px solid var(--bd)}
body.wifi-nag #config-card>.card-hdr .card-min-btn,
body.wifi-nag #config-hardware-section>.subsec-head .subsec-btn{display:none !important}
body.wifi-nag #can-write-row{padding-top:14px}
body.wifi-nag #can-write-row .setting-name,
body.wifi-nag #nag-mode-row .setting-name,
body.wifi-nag #nag-av2-row .setting-name,
body.wifi-nag #can-diag-row .setting-name{font-weight:700}
body.wifi-nag #can-write-row .setting-desc,
body.wifi-nag #nag-mode-row .setting-desc,
body.wifi-nag #nag-av2-row .setting-desc,
body.wifi-nag #can-diag-row .setting-desc{line-height:1.55}
body.wifi-nag #nag-echo-meta{display:inline-flex;margin-top:4px;padding:2px 6px;border:1px solid var(--bd);border-radius:6px;background:var(--bg2);color:var(--tx2)}
body.wifi-nag #nag-mode-seg .hw-btn.active{color:var(--gold);border-color:var(--goldBd);background:var(--goldBg)}
body.wifi-nag #can-write-tgl input:checked~.tgl-track{background:var(--ok)}
@media (max-width:560px){
  body{font-size:13px}
  .hdr{padding:16px 12px 0}
  .stat-grid{grid-template-columns:repeat(2,minmax(0,1fr));gap:7px;margin:12px 12px 0}
  .stat-grid>.btn{grid-column:span 1;justify-content:center;text-align:center}
  .card{margin-left:12px;margin-right:12px;padding:14px;border-radius:9px}
  .card-hdr{grid-template-columns:minmax(0,1fr) minmax(0,auto) auto;row-gap:8px}
  .card-meta{font-size:10px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
  .card-min-btn{grid-column:3;grid-row:1}
  .setting-row{gap:10px}
  body.wifi-nag #can-write-row,
  body.wifi-nag #nag-mode-row,
  body.wifi-nag #nag-av2-row,
  body.wifi-nag #can-diag-row{flex-direction:column;align-items:stretch}
  body.wifi-nag #can-write-row .tgl{align-self:flex-end;margin-left:0;margin-top:-4px}
  .nag-mode-control{width:100% !important;flex:0 0 auto !important}
  .nag-mode-control .hw-btn{min-height:40px;font-size:13px}
  .nag-range-grid{width:100%;grid-template-columns:minmax(0,1fr) minmax(0,1fr);gap:8px}
  .nag-range-grid .sniff-btn{grid-column:1 / -1;min-height:40px}
  .nag-range-grid .sniff-input{min-height:40px;font-size:14px}
  .can-diag-grid{grid-template-columns:repeat(2,minmax(0,1fr))}
}
</style>
</head>
<body>

<nav class="car-side" aria-label="Car quick navigation">
  <div class="car-side-title">EVtools</div>
  <div class="car-side-sub" id="car-side-mode">Auto UI</div>
  <button class="car-nav-btn" onclick="scrollCarSection('status-panel')"><span class="car-nav-icon" aria-hidden="true"><svg viewBox="0 0 24 24"><path d="M4 13h4l2-6 4 10 2-4h4"/></svg></span><span>状态显示</span></button>
  <button class="car-nav-btn nag-nav-only" onclick="scrollCarSection('config-hardware-section')"><span class="car-nav-icon" aria-hidden="true"><svg viewBox="0 0 24 24"><path d="M12 3l7 3v5c0 5-3 8-7 10-4-2-7-5-7-10V6l7-3z"/><path d="M9 12l2 2 4-5"/></svg></span><span>NAG KILLER</span></button>
  <button class="car-nav-btn" onclick="scrollCarSection('wifi-hotspot-section')"><span class="car-nav-icon" aria-hidden="true"><svg viewBox="0 0 24 24"><path d="M5 12.5a11 11 0 0 1 14 0"/><path d="M8.5 16a6 6 0 0 1 7 0"/><path d="M12 19h.01"/></svg></span><span>WIFI设置</span></button>
  <button class="car-nav-btn" onclick="scrollCarSection('gateway-section')"><span class="car-nav-icon" aria-hidden="true"><svg viewBox="0 0 24 24"><path d="M4 7h16"/><path d="M4 12h10"/><path d="M4 17h7"/><path d="M17 15l3 3"/><path d="M20 15l-3 3"/></svg></span><span>DNS过滤</span></button>
  <button class="car-nav-btn" onclick="scrollCarSection('system-card')"><span class="car-nav-icon" aria-hidden="true"><svg viewBox="0 0 24 24"><path d="M12 3v3"/><path d="M12 18v3"/><path d="M4.6 7.5l2.6 1.5"/><path d="M16.8 15l2.6 1.5"/><path d="M19.4 7.5L16.8 9"/><path d="M7.2 15l-2.6 1.5"/><circle cx="12" cy="12" r="4"/></svg></span><span>系统状态</span></button>
</nav>

<div class="hdr">
  <div class="hdr-top">
    <div class="hdr-left">
      <div class="hdr-title">EVtools WIFI-NAG</div>
      <span class="hw-badge" id="hw-badge">WIFI-NAG</span>
    </div>
    <button class="theme-btn" onclick="toggleLanguage()" id="lang-btn">中文</button>
    <button class="theme-btn" onclick="toggleTheme()" id="theme-btn">&#9788; Light</button>
  </div>
  <div class="hdr-status">
    <span class="sdot dot-off" id="dot"></span>
    <span id="hdr-desc">Waiting for CAN frames</span>
  </div>
</div>

<div class="ui-mode-strip" id="ui-mode-strip">
  <span class="ui-mode-label">UI Mode</span>
  <div class="ui-mode-buttons">
    <button type="button" class="ui-mode-btn" data-ui-mode="auto" onclick="setUiMode('auto',true)">Auto</button>
    <button type="button" class="ui-mode-btn" data-ui-mode="car" onclick="setUiMode('car',true)">Car</button>
    <button type="button" class="ui-mode-btn" data-ui-mode="phone" onclick="setUiMode('phone',true)">Phone</button>
  </div>
  <span class="ui-mode-detected" id="ui-mode-detected">Detected: Phone</span>
</div>

<div class="fps-bar"><div class="fps-fill" id="fps-fill"></div></div>

<div class="stat-grid" id="status-panel">
  <div class="stat can-only"><div class="stat-lbl">CAN Bus</div><div class="stat-val" id="s-can">Offline</div></div>
  <div class="stat can-only"><div class="stat-lbl" id="s-inj-lbl">CAN TX</div><div class="stat-val v-dim" id="s-inj">--</div></div>
  <div class="stat can-only"><div class="stat-lbl" title="Frames received per second / total RX">CAN Frames</div><div class="stat-val v-dim" id="s-fps">0.0 Hz</div></div>
  <div class="stat can-only"><div class="stat-lbl">RX</div><div class="stat-val v-acc" id="s-rx">0</div></div>
  <div class="stat can-only"><div class="stat-lbl">TX</div><div class="stat-val v-acc" id="s-tx">0</div></div>
  <div class="stat can-only"><div class="stat-lbl">TX Errors</div><div class="stat-val v-dim" id="s-txerr">0</div></div>
  <div class="stat"><div class="stat-lbl">Uptime</div><div class="stat-val v-dim" id="s-up">0s</div></div>
  <button class="btn can-only" id="btn-can-toggle" onclick="toggleCanWriteTopButton()">CAN Write On</button>
  <button class="btn btn-reboot" onclick="reboot()">Reboot</button>
</div>

<div style="height:12px"></div>

<div class="card" id="system-card">
  <div class="card-hdr">
    <div class="card-title">System Status <span class="title-help" aria-label="Help" onclick="return toggleHelp(this,event)" title="Hardware and runtime health reported by the ESP32 firmware.">i</span></div>
    <div class="card-meta sys-monitor"><span id="sys-summary">Monitoring off</span><label class="tgl" title="Enable live hardware status sampling"><input type="checkbox" id="sys-monitor-tgl" onchange="toggleSystemMonitor()"><div class="tgl-track"><div class="tgl-thumb"></div></div></label></div>
  </div>
  <div class="sys-grid">
    <div class="sys-item"><div class="sys-lbl">Chip</div><div class="sys-val" id="sys-chip">--</div></div>
    <div class="sys-item"><div class="sys-lbl">CPU</div><div class="sys-val" id="sys-cpu">--</div></div>
    <div class="sys-item sys-wide"><div class="sys-lbl">Clock / Bus</div><div class="sys-val" id="sys-clocks">--</div></div>
    <div class="sys-item sys-wide">
      <div class="sys-lbl">CPU Load</div><div class="sys-val" id="sys-cpu-load">--</div>
      <div class="sys-bar"><div class="sys-fill" id="sys-cpu0-fill"></div></div>
      <div class="sys-bar" style="margin-top:4px"><div class="sys-fill" id="sys-cpu1-fill"></div></div>
    </div>
    <div class="sys-item"><div class="sys-lbl">Temperature</div><div class="sys-val" id="sys-temp">--</div></div>
    <div class="sys-item"><div class="sys-lbl">Reset</div><div class="sys-val" id="sys-reset">--</div></div>
    <div class="sys-item sys-wide"><div class="sys-lbl">Board Specs</div><div class="sys-val" id="sys-board">--</div></div>
    <div class="sys-item"><div class="sys-lbl">Uptime / Core</div><div class="sys-val" id="sys-runtime">--</div></div>
    <div class="sys-item"><div class="sys-lbl">Tasks</div><div class="sys-val" id="sys-tasks">--</div></div>
    <div class="sys-item sys-wide">
      <div class="sys-lbl">Heap RAM</div><div class="sys-val" id="sys-heap">--</div>
      <div class="sys-bar"><div class="sys-fill" id="sys-heap-fill"></div></div>
    </div>
    <div class="sys-item sys-wide">
      <div class="sys-lbl">Internal RAM</div><div class="sys-val" id="sys-internal">--</div>
      <div class="sys-bar"><div class="sys-fill" id="sys-internal-fill"></div></div>
    </div>
    <div class="sys-item"><div class="sys-lbl">Largest Block</div><div class="sys-val" id="sys-largest">--</div></div>
    <div class="sys-item"><div class="sys-lbl">Min Free Heap</div><div class="sys-val" id="sys-minheap">--</div></div>
    <div class="sys-item sys-wide">
      <div class="sys-lbl">PSRAM</div><div class="sys-val" id="sys-psram">--</div>
      <div class="sys-bar"><div class="sys-fill" id="sys-psram-fill"></div></div>
    </div>
    <div class="sys-item sys-wide">
      <div class="sys-lbl">Flash / App</div><div class="sys-val" id="sys-flash">--</div>
      <div class="sys-bar"><div class="sys-fill" id="sys-app-fill"></div></div>
    </div>
    <div class="sys-item sys-wide">
      <div class="sys-lbl">SPIFFS</div><div class="sys-val" id="sys-spiffs">--</div>
      <div class="sys-bar"><div class="sys-fill" id="sys-spiffs-fill"></div></div>
    </div>
    <div class="sys-item"><div class="sys-lbl">WiFi RSSI</div><div class="sys-val" id="sys-rssi">--</div></div>
    <div class="sys-item"><div class="sys-lbl">WiFi Mode</div><div class="sys-val" id="sys-wifi-mode">--</div></div>
    <div class="sys-item"><div class="sys-lbl">AP Clients</div><div class="sys-val" id="sys-apclients">--</div></div>
    <div class="sys-item"><div class="sys-lbl">Bluetooth LE</div><div class="sys-val" id="sys-ble">--</div></div>
    <div class="sys-item sys-wide"><div class="sys-lbl">Wireless</div><div class="sys-val" id="sys-wireless">--</div></div>
    <div class="sys-item sys-wide"><div class="sys-lbl">MAC / Firmware</div><div class="sys-val" id="sys-fw">--</div></div>
  </div>
</div>

<div class="card" id="config-card">
  <div class="card-hdr">
    <div class="card-title">Configuration <span class="title-help" aria-label="Help" onclick="return toggleHelp(this,event)" title="Device settings for Nag, WiFi, DNS and logging.">i</span></div>
    <div class="card-meta">Device settings</div>
  </div>

  <div class="subsec" id="config-hardware-section" data-subkey="config-hardware">
    <div class="subsec-head">
      <div class="subsec-title">Nag / CAN Write <span class="title-help" aria-label="Help" onclick="return toggleHelp(this,event)" title="Read-only monitoring when off; Nag 0x370 echo writes when on.">i</span></div>
      <div class="subsec-meta">WIFI-NAG</div>
    </div>
    <div class="subsec-body">
      <div class="setting-row" id="can-write-row">
        <div class="setting-info">
          <div class="setting-name">CAN Write</div>
          <div class="setting-desc">OFF = read-only CAN monitoring. ON allows Nag 880 (0x370) counter+1 echo writes. <span id="nag-echo-meta">echo: --</span></div>
        </div>
        <label class="tgl"><input type="checkbox" id="can-write-tgl" onchange="saveCanWrite()"><div class="tgl-track"><div class="tgl-thumb"></div></div></label>
      </div>
      <div class="setting-row nag-only" id="nag-mode-row">
        <div class="setting-info">
          <div class="setting-name">Nag Mode</div>
          <div class="setting-desc" id="nag-mode-meta">A is idle by default and outputs fixed +1.80 Nm only during a new BLE-triggered 10 s window. A_V2 keeps its manual random sweep.</div>
        </div>
        <div class="hw-seg nag-mode-control" id="nag-mode-seg">
          <button class="hw-btn active" data-v="0" onclick="setNagMode(0)">A</button>
          <button class="hw-btn" data-v="4" onclick="setNagMode(4)">A_V2</button>
        </div>
      </div>
      <div class="setting-row nag-only" id="nag-av2-row">
        <div class="setting-info">
          <div class="setting-name">A_V2 Range</div>
          <div class="setting-desc">Nm endpoints are clamped to -1.80 .. +1.80 and auto-swapped if reversed.
            <span class="nag-torque-status">
              <span class="nag-status-pill" id="nag-live-meta">实时: --</span>
              <span class="nag-status-pill" id="nag-write-meta">写入: --</span>
              <span class="nag-status-pill" id="nag-av2-meta">skip: --</span>
            </span>
          </div>
        </div>
        <div class="nag-range-grid">
          <input class="sniff-input" id="nag-av2-min" type="number" min="-1.8" max="1.8" step="0.01" value="1.50" onchange="saveNagAv2()">
          <input class="sniff-input" id="nag-av2-max" type="number" min="-1.8" max="1.8" step="0.01" value="1.80" onchange="saveNagAv2()">
          <button class="sniff-btn" onclick="saveNagAv2()">Save</button>
        </div>
      </div>
      <div class="setting-row nag-only" id="nag-amode-row">
        <div class="setting-info">
          <div class="setting-name">A Mode Live (BLE Trigger)</div>
          <div class="setting-desc">Mode A remains idle until a validated BLE FSD TEST_ACTIVE rising edge opens one 10 s output window (only when CAN Write is ON).
            <span class="nag-torque-status">
              <span class="nag-status-pill" id="nag-amode-state">A mode: --</span>
              <span class="nag-status-pill" id="nag-amode-live">实时: --</span>
              <span class="nag-status-pill" id="nag-amode-write">写入: --</span>
              <span class="nag-status-pill" id="nag-amode-src">source: --</span>
              <span class="nag-status-pill" id="nag-amode-echo">echo: --</span>
            </span>
          </div>
        </div>
      </div>
      <div class="setting-row nag-only can-diag-row" id="can-diag-row">
        <div class="setting-info">
          <div class="setting-name">CAN Diagnostics</div>
          <div class="setting-desc">Read-only TWAI health counters. Only one echo may wait for TX; newer echoes are dropped while it is pending. BUS-OFF or excessive errors automatically lock CAN writing.</div>
          <div class="can-diag-grid">
            <div class="can-diag-item"><div class="can-diag-label">TWAI State</div><div class="can-diag-value" id="can-diag-state">--</div></div>
            <div class="can-diag-item"><div class="can-diag-label">TEC / REC</div><div class="can-diag-value" id="can-diag-errors">--</div></div>
            <div class="can-diag-item"><div class="can-diag-label">TXQ / RXQ</div><div class="can-diag-value" id="can-diag-queues">--</div></div>
            <div class="can-diag-item"><div class="can-diag-label">Queue Reject / Stale Drop</div><div class="can-diag-value" id="can-diag-reject">--</div></div>
            <div class="can-diag-item"><div class="can-diag-label">TX Failed / Bus Error</div><div class="can-diag-value" id="can-diag-txbus">--</div></div>
            <div class="can-diag-item"><div class="can-diag-label">Arbitration Total / s</div><div class="can-diag-value" id="can-diag-arb">--</div></div>
            <div class="can-diag-item"><div class="can-diag-label">RX Miss / Overrun</div><div class="can-diag-value" id="can-diag-rxloss">--</div></div>
            <div class="can-diag-item"><div class="can-diag-label">BUS-OFF / Recovered</div><div class="can-diag-value" id="can-diag-busoff">--</div></div>
            <div class="can-diag-item"><div class="can-diag-label">Warning / Passive</div><div class="can-diag-value" id="can-diag-warning">--</div></div>
            <div class="can-diag-item"><div class="can-diag-label">Safety Protection</div><div class="can-diag-value" id="can-diag-safety">--</div></div>
          </div>
          <div class="can-diag-actions"><button class="sniff-btn" id="can-diag-reset" onclick="resetCanDiagnostics()">Reset Diagnostics</button></div>
        </div>
      </div>
      <div class="setting-row ble-rx-row" id="ble-rx-row">
        <div class="setting-info">
          <div class="setting-name">BLE FSD Diagnostic Receiver</div>
          <div class="setting-desc">Receives and validates the LILYGO FSD_ACTIVE protocol, then opens a visible diagnostic-only timer. It does not send or modify CAN frames.</div>
          <div class="can-diag-grid">
            <div class="can-diag-item"><div class="can-diag-label">Link / RSSI</div><div class="can-diag-value" id="ble-rx-link">--</div></div>
            <div class="can-diag-item"><div class="can-diag-label">Connected Device</div><div class="can-diag-value" id="ble-rx-device">--</div></div>
            <div class="can-diag-item"><div class="can-diag-label">GATT / Last Packet</div><div class="can-diag-value" id="ble-rx-session">--</div></div>
            <div class="can-diag-item"><div class="can-diag-label">State / Left</div><div class="can-diag-value" id="ble-rx-state">--</div></div>
            <div class="can-diag-item"><div class="can-diag-label">Last Sequence</div><div class="can-diag-value" id="ble-rx-seq">--</div></div>
            <div class="can-diag-item"><div class="can-diag-label">CRC / Repeat</div><div class="can-diag-value" id="ble-rx-errors">--</div></div>
            <div class="can-diag-item"><div class="can-diag-label">Windows / Timeout</div><div class="can-diag-value" id="ble-rx-windows">--</div></div>
            <div class="can-diag-item"><div class="can-diag-label">Last Reject</div><div class="can-diag-value" id="ble-rx-reject">--</div></div>
          </div>
        </div>
        <label class="tgl"><input type="checkbox" id="ble-rx-enabled" onchange="saveBleFsdConfig()"><div class="tgl-track"><div class="tgl-thumb"></div></div></label>
      </div>
      <div style="display:flex;align-items:center;justify-content:space-between;gap:10px;margin-top:9px;flex-wrap:wrap">
        <label style="display:flex;align-items:center;gap:7px;font-size:11px;color:var(--tx3)">
          <input type="checkbox" id="ble-rx-auto-collapse" onchange="saveBleFsdCollapsePreference()"> Auto-collapse after connection
        </label>
        <button class="sniff-btn" id="ble-rx-controls-toggle" onclick="toggleBleFsdControls()">Collapse settings</button>
      </div>
      <div id="ble-rx-controls">
        <div class="ble-rx-form" style="display:grid;grid-template-columns:minmax(0,1fr) 74px 94px auto;gap:6px;margin-top:9px">
          <input class="sniff-input" id="ble-rx-mac" placeholder="LILYGO MAC (AA:BB:CC:DD:EE:FF)">
          <input class="sniff-input" id="ble-rx-rssi" type="number" min="-100" max="-20" value="-90" title="RSSI threshold dBm">
          <input class="sniff-input" id="ble-rx-window" type="number" min="1000" max="60000" step="1000" value="10000" title="Diagnostic window ms">
          <button class="sniff-btn" onclick="saveBleFsdConfig()">Save BLE</button>
        </div>
        <div style="display:flex;gap:6px;align-items:center;margin-top:8px">
          <button class="sniff-btn" id="ble-rx-scan-btn" onclick="scanBleFsd()">Scan LILYGO (10s)</button>
          <span class="setting-desc" id="ble-rx-scan-status" style="margin:0"></span>
        </div>
        <div id="ble-rx-scan-results" style="display:none;margin-top:8px;border:1px solid var(--bd);border-radius:8px;background:var(--bg2);overflow:hidden"></div>
        <div class="setting-desc" id="ble-rx-reason" style="margin-top:7px">BLE receiver disabled.</div>
      </div>
    </div>
  </div>

  <div class="subsec" id="wifi-hotspot-section" data-subkey="config-wifi-hotspot">
    <div class="subsec-head">
      <div class="subsec-title">WiFi Hotspot <span class="title-help" aria-label="Help" onclick="return toggleHelp(this,event)" data-help-target="ap-info" title="Configure the device hotspot name, password and visibility. Saved in NVS.">i</span></div>
      <div class="subsec-meta"><span id="ap-stored" style="margin-right:8px"></span><span id="ap-clients">0 clients</span></div>
    </div>
    <div class="subsec-body">
      <div id="ap-info" class="info-box" style="display:none">
        Stored in NVS (non-volatile storage). The SSID and password survive firmware updates and reboots. Only a full factory erase via USB clears them.
      </div>
      <div class="setting-desc" style="margin-bottom:8px">Change the WiFi hotspot name and password</div>
      <div style="display:flex;gap:6px;margin-bottom:6px">
        <input class="sniff-input" id="ap-ssid" placeholder="Hotspot Name" style="flex:1">
        <input class="sniff-input" id="ap-pass" placeholder="New Password (min 8)" type="password" style="flex:1">
      </div>
      <div class="setting-row" style="padding:8px 0">
        <div class="setting-info">
          <div class="setting-name">Hide SSID</div>
          <div class="setting-desc">Don't broadcast the hotspot name &mdash; clients must enter it manually</div>
        </div>
        <label class="tgl"><input type="checkbox" id="ap-hidden"><div class="tgl-track"><div class="tgl-thumb"></div></div></label>
      </div>
      <div style="display:flex;gap:6px;align-items:center">
        <button class="sniff-btn" onclick="saveAP()">Save</button>
        <span style="font-size:11px;color:var(--tx3)" id="ap-status"></span>
      </div>
      <div style="font-size:10px;color:var(--tx3);margin-top:6px">Changes take effect after reboot. Leave password empty to keep current.</div>
    </div>
  </div>

  <div class="subsec" id="wifi-internet-section" data-subkey="config-wifi-internet">
    <div class="subsec-head">
      <div class="subsec-title">WiFi Internet <span class="title-help" aria-label="Help" onclick="return toggleHelp(this,event)" title="Up to 4 saved networks. The device tries each in turn until one connects.">i</span></div>
      <div class="subsec-meta"><span id="wifi-status">Not configured</span></div>
    </div>
    <div class="subsec-body">
      <div class="setting-desc" style="margin-bottom:8px">Save up to 4 networks (e.g. home + phone hotspot). Device tries each in turn. Stored in NVS &mdash; survives firmware updates.</div>
      <div id="wifi-saved-list" style="margin-bottom:8px"></div>
      <div id="wifi-add-wrap">
        <div class="setting-desc" style="margin-bottom:6px"><b>Add network</b> <span id="wifi-slot-count" style="color:var(--tx3)">(0/4)</span></div>
        <div style="display:flex;gap:6px;margin-bottom:6px">
          <input class="sniff-input" id="wifi-ssid" placeholder="WiFi SSID" style="flex:1">
          <button class="sniff-btn" onclick="scanWifi()" id="scan-btn">Scan</button>
        </div>
        <div id="wifi-nets" style="display:none;margin-bottom:6px;max-height:140px;overflow-y:auto;border:1px solid var(--bd);border-radius:6px;background:var(--bg2)"></div>
        <div style="display:flex;gap:6px;margin-bottom:6px">
          <input class="sniff-input" id="wifi-pass" placeholder="Password" type="password" style="flex:1">
          <button class="sniff-btn" onclick="saveWifi()" id="wifi-save-btn">Save &amp; Connect</button>
        </div>
        <details style="margin-top:4px">
          <summary style="font-size:11px;color:var(--acc);cursor:pointer;user-select:none">Static IP (optional) <span class="title-help" aria-label="Help" onclick="return toggleHelp(this,event)" title="Set a fixed IP configuration instead of using DHCP.">i</span></summary>
          <div style="margin-top:6px">
            <label style="font-size:11px;color:var(--tx3);display:flex;align-items:center;gap:6px;margin-bottom:6px">
              <input type="checkbox" id="wifi-static" onchange="toggleStaticIP()"> Use static IP
            </label>
            <div id="static-fields" style="display:none">
              <div style="display:grid;grid-template-columns:1fr 1fr;gap:4px">
                <input class="sniff-input" id="wifi-ip" placeholder="IP (e.g. 192.168.1.100)">
                <input class="sniff-input" id="wifi-gw" placeholder="Gateway (e.g. 192.168.1.1)">
                <input class="sniff-input" id="wifi-mask" placeholder="Mask (255.255.255.0)" value="255.255.255.0">
                <input class="sniff-input" id="wifi-dns" placeholder="DNS (e.g. 8.8.8.8)">
              </div>
            </div>
          </div>
        </details>
        <input type="hidden" id="wifi-edit-idx" value="-1">
      </div>
    </div>
  </div>

  <div class="subsec" id="gateway-section" data-subkey="config-gateway">
    <div class="subsec-head">
      <div class="subsec-title">STA-AP Gateway <span class="title-help" aria-label="Help" onclick="return toggleHelp(this,event)" title="Routes hotspot clients through the configured WiFi Internet uplink, with DNS filtering.">i</span></div>
      <div class="subsec-meta"><span id="gw-status">Gateway status unavailable</span></div>
    </div>
    <div class="subsec-body">
      <div class="setting-row" style="padding:8px 0">
        <div class="setting-info">
          <div class="setting-name">Gateway</div>
          <div class="setting-desc">Enable STA-AP NAT routing for hotspot clients when WiFi Internet is connected</div>
        </div>
        <label class="tgl"><input type="checkbox" id="gw-enabled" onchange="saveGatewayDns()"><div class="tgl-track"><div class="tgl-thumb"></div></div></label>
      </div>
      <div class="setting-row" style="padding:8px 0">
        <div class="setting-info">
          <div class="setting-name">Network Performance Mode</div>
          <div class="setting-desc">Reduce WebUI polling while AP+STA+NAPT is forwarding traffic</div>
          <div id="net-perf-status" style="font-size:10px;color:var(--tx3);margin-top:3px">ON: status 5s, network diagnostics 30s, heavy lists manual only</div>
        </div>
        <label class="tgl"><input type="checkbox" id="net-perf-tgl" onchange="setNetworkPerformanceMode(this.checked,true)"><div class="tgl-track"><div class="tgl-thumb"></div></div></label>
      </div>
      <div id="gw-diag" style="margin:2px 0 10px;padding:8px;border:1px solid var(--bd);border-radius:8px;background:var(--bg2);display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:6px;font-size:11px">
        <div><span style="color:var(--tx3)">AP</span> <span id="gw-diag-ap">--</span></div>
        <div><span style="color:var(--tx3)">STA</span> <span id="gw-diag-sta">--</span></div>
        <div><span style="color:var(--tx3)">NAT</span> <span id="gw-diag-nat">--</span></div>
        <div><span style="color:var(--tx3)">Radio</span> <span id="gw-diag-radio">--</span></div>
        <div><span style="color:var(--tx3)">DNS</span> <span id="gw-diag-dns">--</span></div>
        <div><span style="color:var(--tx3)">DNS Slow</span> <span id="gw-diag-slow">--</span></div>
        <div><span style="color:var(--tx3)">Pending</span> <span id="gw-diag-pending">--</span></div>
        <div><span style="color:var(--tx3)">Upstream</span> <span id="gw-diag-upstream">--</span></div>
        <div><span style="color:var(--tx3)">AP Clients</span> <span id="gw-diag-clients">--</span></div>
      </div>
      <div style="margin:4px 0 10px;padding:8px;border:1px solid var(--bd);border-radius:8px;background:var(--bg2)">
        <div style="font-size:12px;font-weight:600;color:var(--tx2);margin-bottom:6px">Upstream DNS</div>
        <input type="hidden" id="gw-upstream-mode" value="0">
        <div style="display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:6px;margin-bottom:6px">
          <button type="button" class="sniff-btn gateway-upstream-btn" data-mode="0" onclick="setGatewayUpstreamMode(0,true)">Auto</button>
          <button type="button" class="sniff-btn gateway-upstream-btn" data-mode="1" onclick="setGatewayUpstreamMode(1,true)">223.5.5.5 Ali</button>
          <button type="button" class="sniff-btn gateway-upstream-btn" data-mode="2" onclick="setGatewayUpstreamMode(2,true)">119.29.29.29 Tencent</button>
          <button type="button" class="sniff-btn gateway-upstream-btn" data-mode="3" onclick="setGatewayUpstreamMode(3,true)">Custom</button>
        </div>
        <div style="display:grid;grid-template-columns:minmax(0,1fr) auto auto;gap:6px;align-items:center">
          <input class="sniff-input" id="gw-upstream-custom" placeholder="Custom DNS, e.g. 8.8.8.8">
          <button class="sniff-btn modal-btn-primary" onclick="saveGatewayDns()">Save DNS</button>
          <button class="sniff-btn" onclick="resetGatewayDnsStats()">Reset DNS Stats</button>
        </div>
        <div id="gw-upstream-hint" style="font-size:10px;color:var(--tx3);margin-top:5px">Auto uses DHCP DNS from the connected WiFi; public DNS can avoid stale slow/fail counters from a bad router DNS.</div>
      </div>
      <div style="margin:4px 0 10px;padding:8px;border:1px solid var(--bd);border-radius:8px;background:var(--bg2)">
        <div style="display:grid;grid-template-columns:1fr 1fr;gap:6px;margin-bottom:6px">
          <button class="sniff-btn gateway-profile-btn" id="gw-profile-safe" onclick="applyGatewayProfile('safe')">Conservative Mode</button>
          <button class="sniff-btn gateway-profile-btn" id="gw-profile-aggressive" onclick="applyGatewayProfile('aggressive')">Aggressive Mode</button>
        </div>
        <div id="gw-profile-desc" style="font-size:11px;color:var(--tx3);line-height:1.45">Conservative Mode: WiFi access / offline navigation / online navigation / China maps / WeChat notifications / Bluetooth music / voice assistant.</div>
      </div>
      <div style="margin-bottom:10px">
        <div style="font-size:12px;font-weight:600;color:var(--tx2);margin-bottom:4px">Blacklist</div>
        <textarea class="sniff-input" id="gw-blacklist" rows="5" placeholder="Blocked domains, one per line" style="width:100%;resize:vertical"></textarea>
      </div>
      <div style="margin-bottom:10px">
        <div style="font-size:12px;font-weight:600;color:var(--tx2);margin-bottom:4px">Whitelist</div>
        <textarea class="sniff-input" id="gw-whitelist" rows="5" placeholder="Allowed domains, one per line" style="width:100%;resize:vertical"></textarea>
      </div>
      <div style="display:flex;gap:6px;align-items:center;flex-wrap:wrap;margin-bottom:10px">
        <button class="sniff-btn modal-btn-primary" onclick="saveGatewayDns()">Save DNS</button>
        <span style="font-size:11px;color:var(--tx3)" id="gw-msg"></span>
        <span id="gw-list-counts" style="font-size:11px;color:var(--tx3);margin-left:auto"></span>
      </div>
      <div style="margin-bottom:10px">
        <div style="display:flex;align-items:center;justify-content:space-between;margin-bottom:4px">
          <div style="font-size:12px;font-weight:600;color:var(--tx2)">Filter List</div>
          <div style="display:flex;gap:6px">
            <button class="sniff-btn" onclick="loadGatewayBlocked()" style="padding:3px 8px;font-size:10px">Refresh</button>
            <button class="sniff-btn" onclick="clearGatewayBlocked()" style="padding:3px 8px;font-size:10px">Clear</button>
          </div>
        </div>
        <div id="gw-blocked-list" class="dns-modal-list" style="margin-top:0;max-height:240px"></div>
        <div id="gw-blocked-summary" style="font-size:10px;color:var(--tx3);margin-top:4px"></div>
        <div id="gw-blocked-msg" style="font-size:10px;color:var(--tx3);margin-top:2px"></div>
      </div>
      <div style="display:flex;gap:6px;margin-bottom:6px">
        <input class="sniff-input" id="gw-test-domain" placeholder="Test domain">
        <button class="sniff-btn" onclick="testGatewayDns()">Test DNS</button>
      </div>
      <div id="gw-test-result" style="font-size:11px;color:var(--tx3);margin-bottom:8px"></div>
      <div style="display:flex;gap:6px;align-items:center;flex-wrap:wrap">
        <button class="sniff-btn" onclick="saveGatewayDns()">Save DNS</button>
      </div>
    </div>
  </div>

  <div class="subsec" data-subkey="config-dashboard-log" style="margin-top:14px">
    <div class="subsec-head">
      <div class="subsec-title">Debug Log <span class="title-help" aria-label="Help" onclick="return toggleHelp(this,event)" title="Shows recent WebUI and firmware log lines.">i</span></div>
      <div class="subsec-meta">Recent debug output</div>
    </div>
    <div class="subsec-body">
      <div class="setting-row" style="padding-top:0">
        <div class="setting-info">
          <div class="setting-name">Debug logging <span class="title-help" aria-label="Help" onclick="return toggleHelp(this,event)" title="Turns WebUI debug log output on or off.">i</span></div>
          <div class="setting-desc">Toggle WebUI and firmware debug output</div>
        </div>
        <label class="tgl"><input type="checkbox" id="tgl-eprn" onchange="pushLogging()">
          <div class="tgl-track"><div class="tgl-thumb"></div></div></label>
      </div>
      <div class="log-box" id="log">Waiting...</div>
    </div>
  </div>
</div>

<div class="card" id="firmware-update-card">
  <div class="card-hdr">
    <div class="card-title">Firmware Update <span class="title-help" aria-label="Help" onclick="return toggleHelp(this,event)" title="Manual firmware upload only. Select a local .bin and flash it to the device.">i</span></div>
    <div class="card-meta" id="fw-ver">Manual OTA</div>
  </div>
  <div style="margin-top:4px">
    <div class="ota-drop" id="ota-drop" onclick="$('ota-file').click()" ondragover="event.preventDefault();this.classList.add('drag')" ondragleave="this.classList.remove('drag')" ondrop="handleDrop(event)">
      <input type="file" id="ota-file" accept=".bin" onchange="fileSelected(this.files[0])">
      <div class="ota-icon">&#8679;</div>
      <div class="ota-text">Tap to select firmware .bin</div>
      <div class="ota-sub">Or drag and drop a file here</div>
    </div>
    <div class="ota-progress" id="ota-progress">
      <div class="ota-bar"><div class="ota-fill" id="ota-fill"></div></div>
      <div class="ota-status" id="ota-status">Uploading...</div>
    </div>
    <button class="ota-btn" id="ota-upload-btn" onclick="uploadFirmware()">Flash Firmware</button>
    <button class="sniff-btn" id="ota-reset-btn" onclick="resetOtaCredentials()" style="width:100%;margin-top:6px">Reset OTA Credentials</button>
    <div style="margin-top:10px;font-size:11px;color:var(--tx3);line-height:1.7">
      Use the generated PlatformIO firmware.bin for this board.<br>
      Current build path: <span style="color:var(--acc);font-family:monospace">.pio/build/wifi_nag_ESP32_S3_CAN/firmware.bin</span>
    </div>
  </div>
</div>
<div class="warn-bar">CAN bus writes affect vehicle behavior. Remove device immediately if unexpected behavior occurs. Not affiliated with any vehicle manufacturer.</div>

<div class="modal-backdrop" id="safety-modal">
  <div class="modal-card safety-modal-card" role="dialog" aria-modal="true" aria-labelledby="safety-title">
    <div class="modal-title" id="safety-title">安全提示与使用声明</div>
    <div class="safety-body">
      <p>本固件仅供开源学习、研究与测试使用。</p>
      <p><span class="safety-strong">禁止任何形式的售卖、转售或商业化分发。</span></p>
      <p>本固件涉及 CAN 通讯、FSD/AP 相关信号测试、免打扰等功能。相关功能可能带来法律、合规及行车安全风险。使用前请确认你已充分理解功能作用、适用场景和潜在后果，并自行承担全部责任。</p>
      <p><span class="safety-strong">驾驶过程中，请始终保持清醒并专注驾驶，目视前方，双手随时准备接管方向盘。任何辅助驾驶功能都不能替代驾驶员对车辆和道路环境的持续观察与控制。</span></p>
      <p>点击确认即表示你已阅读并理解以上提示。</p>
    </div>
    <div class="modal-actions safety-actions">
      <button class="sniff-btn modal-btn-primary" id="safety-ok" onclick="acceptSafetyNotice()">确认</button>
    </div>
  </div>
</div>

<div class="modal-backdrop" id="confirm-modal" onclick="dashConfirmBackdrop(event)">
  <div class="modal-card" role="dialog" aria-modal="true" aria-labelledby="confirm-title">
    <div class="modal-title" id="confirm-title">Confirm</div>
    <div class="modal-msg" id="confirm-msg"></div>
    <div class="modal-actions">
      <button class="sniff-btn" id="confirm-cancel" onclick="dashConfirmResolve(false)">Cancel</button>
      <button class="sniff-btn modal-btn-primary" id="confirm-ok" onclick="dashConfirmResolve(true)">Continue</button>
    </div>
  </div>
</div>

<script>
const $=id=>document.getElementById(id);
let dashLang=localStorage.getItem('dashLang')||'zh';
const I18N_ZH={
  'Light':'浅色','Dark':'深色','Help':'帮助','Show':'展开','Hide':'收起','Waiting...':'等待中...','Error':'错误','Download':'下载',
  'UI Mode':'界面模式','Auto UI':'自动界面','Auto':'自动','Manual':'手动','Car':'车机','Phone':'手机','Detected: Phone':'识别：手机','Detected: Car':'识别：车机','Manual: Phone':'手动：手机','Manual: Car':'手动：车机',
  'Waiting for CAN frames':'等待 CAN 帧','Dashboard disconnected':'仪表盘已断开','Dashboard reconnecting':'仪表盘正在重连','CAN running':'CAN 正常','CAN OK':'CAN 正常','CAN waiting':'等待 CAN','No frames':'无帧','Offline':'离线',
  'CAN Bus':'CAN 总线','CAN Frames':'CAN 帧','CAN TX':'CAN 发送','RX':'接收','TX':'发送','TX Errors':'发送错误','Uptime':'运行时间','Reboot':'重启','READ ONLY':'只读模式','CAN WRITE ON':'CAN 写入开启','Read Only':'只读模式',
  'Frames received per second / total RX':'每秒接收帧数 / 总接收数','CAN Write':'CAN 写入','CAN Write On':'开启 CAN 写入','CAN Write Off':'关闭 CAN 写入','CAN write is enabled. Nag echo can transmit.':'CAN 写入已开启，Nag echo 可发送。','Read-only mode. CAN frames are monitored but not written.':'只读模式：只监听 CAN 帧，不写入。',
  'Configuration':'配置','Device settings':'设备设置','Device settings for Nag, WiFi, DNS and logging.':'Nag、WiFi、DNS 和日志设置。','Nag / CAN Write':'Nag / CAN 写入','Nag Mode':'Nag 模式','A_V2 Range':'A_V2 范围','CAN Diagnostics':'CAN 诊断',
  'Read-only monitoring when off; Nag 0x370 echo writes when on.':'关闭时仅监听；开启时发送 Nag 0x370 echo。','OFF = read-only CAN monitoring. ON allows Nag 880 (0x370) counter+1 echo writes.':'关闭 = 只读 CAN 监听。开启 = 允许 Nag 880 (0x370) 计数器 +1 echo 写入。','A is idle by default and outputs fixed +1.80 Nm only during a new BLE-triggered 10 s window. A_V2 keeps its manual random sweep.':'A 模式默认空闲，仅在新的 BLE 触发后开启 10 秒固定 +1.80 Nm 输出；A_V2 保留手动随机扫动。','Mode A remains idle until a validated BLE FSD TEST_ACTIVE rising edge opens one 10 s output window (only when CAN Write is ON).':'A 模式默认不启动；仅当有效 BLE FSD TEST_ACTIVE 上升沿到来时开启一次 10 秒输出窗口（且 CAN 写入必须开启）。','Nm endpoints are clamped to -1.80 .. +1.80 and auto-swapped if reversed.':'Nm 端点限制在 -1.80 到 +1.80；如果填反会自动交换。','A_V2: random sweep':'A_V2：随机扫动','A: waiting for BLE trigger':'A：等待 BLE 触发','echo':'echo','skip':'跳过','drop':'丢弃',
  'Read-only TWAI health counters. Only one echo may wait for TX; newer echoes are dropped while it is pending. BUS-OFF or excessive errors automatically lock CAN writing.':'只读 TWAI 健康诊断。发送区只允许一个 echo 等待；未完成时新的过期 echo 会被丢弃。出现 BUS-OFF 或错误过多时自动锁止 CAN 写入。','TWAI State':'TWAI 状态','Queue Reject / Stale Drop':'入队失败 / 过期丢弃','TX Failed / Bus Error':'发送失败 / 总线错误','Arbitration Total / s':'仲裁竞争累计 / 秒','RX Miss / Overrun':'接收丢失 / FIFO 溢出','BUS-OFF / Recovered':'BUS-OFF / 恢复','Warning / Passive':'错误警告 / 被动','Safety Protection':'安全保护','Reset Diagnostics':'清零诊断','RUNNING':'运行中','STOPPED':'已停止','RECOVERING':'恢复中','UNAVAILABLE':'不可用','READY':'就绪','LOCKED':'已锁止','BUS_OFF':'BUS-OFF','TEC_LIMIT':'TEC 超限','REC_LIMIT':'REC 超限','BUS_ERROR_BURST':'总线错误激增','TX_FAILURE_BURST':'发送失败激增',
  'Save':'保存','Saved':'已保存','Saving...':'保存中...','Save failed':'保存失败','CAN write save failed':'CAN 写入保存失败','CAN write blocked by safety protection':'CAN 写入被安全保护阻止','Nag mode save failed':'Nag 模式保存失败','A_V2 range save failed':'A_V2 范围保存失败','CAN diagnostics reset failed':'CAN 诊断清零失败',
  'System Status':'系统状态','Hardware and runtime health reported by the ESP32 firmware.':'ESP32 固件上报的硬件与运行状态。','Monitoring off':'监测关闭','Enable live hardware status sampling':'启用实时硬件状态采样','Chip':'芯片','CPU':'CPU','Clock / Bus':'时钟 / 总线','CPU Load':'CPU 负载','Board Specs':'板载规格','Temperature':'温度','Reset':'重启原因','Uptime / Core':'运行时间 / 核心','Heap RAM':'堆内存','Internal RAM':'内部 RAM','Largest Block':'最大连续内存块','Min Free Heap':'历史最低空闲内存','PSRAM':'PSRAM','Tasks':'任务','Flash':'Flash','Flash / App':'Flash / 应用','SPIFFS':'SPIFFS','WiFi RSSI':'WiFi 信号','WiFi Mode':'WiFi 模式','AP Clients':'AP 客户端','Bluetooth LE':'蓝牙 LE','Wireless':'无线','MAC / Firmware':'MAC / 固件','System status unavailable':'系统状态不可用','warming up':'采样中','unavailable':'不可用','offline':'离线','not enabled':'未启用','enabled':'已启用','supported':'支持','not supported':'不支持','firmware disabled':'固件未启用','STA online':'STA 在线','STA offline':'STA 离线','on':'开启','off':'关闭','unknown':'未知','fixed':'固定',
  'WiFi Hotspot':'WiFi 热点','Configure the device hotspot name, password and visibility. Saved in NVS.':'配置设备热点名称、密码和可见性，保存到 NVS。','Stored in NVS (non-volatile storage). The SSID and password survive firmware updates and reboots. Only a full factory erase via USB clears them.':'保存在 NVS（非易失存储）中。SSID 和密码在固件更新、重启后仍保留，只有通过 USB 完整恢复出厂才会清除。','Change the WiFi hotspot name and password':'修改 WiFi 热点名称和密码','Hotspot Name':'热点名称','New Password (min 8)':'新密码（至少 8 位）','Hide SSID':'隐藏 SSID','Don\'t broadcast the hotspot name \u2014 clients must enter it manually':'不广播热点名称，客户端需要手动输入','Changes take effect after reboot. Leave password empty to keep current.':'修改将在重启后生效。密码留空则保持当前密码。','Enter hotspot name':'请输入热点名称','Password min 8 chars':'密码至少 8 位','Saved! AP starts on CH1 and auto matches STA after WiFi connects.':'已保存！AP 从 CH1 启动，WiFi 连接后自动匹配 STA 信道。','firmware default':'固件默认值','sync':'同步','ok':'成功',
  'WiFi Internet':'WiFi 上网','Up to 4 saved networks. The device tries each in turn until one connects.':'最多保存 4 个网络，设备会按顺序尝试直到连接成功。','Not configured':'未配置','Save up to 4 networks (e.g. home + phone hotspot). Device tries each in turn. Stored in NVS \u2014 survives firmware updates.':'最多保存 4 个网络（例如家里 WiFi + 手机热点）。设备会按顺序尝试，配置保存在 NVS 中，固件更新后仍保留。','Add network':'添加网络','WiFi SSID':'WiFi SSID','Scan':'扫描','Scanning...':'扫描中...','Scan failed':'扫描失败','No networks found':'未发现网络','Password':'密码','Save & Connect':'保存并连接','Static IP (optional)':'静态 IP（可选）','Set a fixed IP configuration instead of using DHCP.':'使用固定 IP 配置，而不是 DHCP。','Use static IP':'使用静态 IP','IP (e.g. 192.168.1.100)':'IP（如 192.168.1.100）','Gateway (e.g. 192.168.1.1)':'网关（如 192.168.1.1）','Mask (255.255.255.0)':'掩码（255.255.255.0）','DNS (e.g. 8.8.8.8)':'DNS（如 8.8.8.8）','No networks saved.':'未保存网络。','connected':'已连接','trying':'尝试中','saved':'已保存','[static]':'[静态]','[connected]':'[已连接]','[trying]':'[连接中]','Reconnect':'重新连接','Connect':'连接','Edit':'编辑','Delete':'删除','Save Changes':'保存修改','Leave empty to keep current':'留空则保持当前密码','Delete WiFi':'删除 WiFi','Delete failed':'删除失败','Enter SSID':'请输入 SSID','Connect failed':'连接失败','connect failed':'连接失败','save failed':'保存失败','retry in':'后重试','switch to that WiFi and open this IP':'切换到该 WiFi 后打开此 IP',
  'STA-AP Gateway':'STA-AP 网关','Routes hotspot clients through the configured WiFi Internet uplink, with DNS filtering.':'通过已配置的 WiFi 上网链路转发热点客户端流量，并进行 DNS 过滤。','Gateway':'网关','Gateway status unavailable':'网关状态不可用','Enable STA-AP NAT routing for hotspot clients when WiFi Internet is connected':'WiFi 上网连接后，为热点客户端启用 STA-AP NAT 路由','Network Performance Mode':'网络性能模式','Reduce WebUI polling while AP+STA+NAPT is forwarding traffic':'AP+STA+NAPT 转发流量时降低 WebUI 轮询频率','ON: status 5s, network diagnostics 30s, heavy lists manual only':'开启：状态 5 秒，网络诊断 30 秒，重列表仅手动刷新','OFF: status 2s, network diagnostics 10s, DNS/filter lists auto refresh':'关闭：状态 2 秒，网络诊断 10 秒，DNS/过滤列表自动刷新','Car UI: status 7s, network diagnostics 45s, heavy lists manual only':'车机界面：状态 7 秒，网络诊断 45 秒，重列表仅手动刷新','Radio':'信道','DNS Slow':'DNS 慢请求','Pending':'待处理','Upstream':'上游','Upstream DNS':'上游 DNS','Custom':'自定义','223.5.5.5 Ali':'223.5.5.5 阿里','119.29.29.29 Tencent':'119.29.29.29 腾讯','Custom DNS, e.g. 8.8.8.8':'自定义 DNS，如 8.8.8.8','Save DNS':'保存 DNS','Reset DNS Stats':'清零 DNS 统计','Auto uses DHCP DNS from the connected WiFi; public DNS can avoid stale slow/fail counters from a bad router DNS.':'自动模式使用已连接 WiFi 的 DHCP DNS；公共 DNS 可避免路由器 DNS 异常导致的慢/失败计数。','Using Ali DNS 223.5.5.5.':'使用阿里 DNS 223.5.5.5。','Using Tencent DNS 119.29.29.29.':'使用腾讯 DNS 119.29.29.29。','Enter a custom upstream DNS IPv4 address.':'输入自定义上游 DNS IPv4 地址。','Conservative Mode':'保守模式','Aggressive Mode':'激进模式','Conservative Mode: WiFi access / offline navigation / online navigation / China maps / WeChat notifications / Bluetooth music / voice assistant.':'保守模式：WiFi 上网 / 离线导航 / 在线导航 / 中国地图 / 微信通知 / 蓝牙音乐 / 语音助手。','Aggressive Mode: WiFi access / offline navigation / online navigation / China maps / WeChat notifications / Bluetooth music / voice assistant / app vehicle control.':'激进模式：WiFi 上网 / 离线导航 / 在线导航 / 中国地图 / 微信通知 / 蓝牙音乐 / 语音助手 / App 车辆控制。','Custom DNS profile':'自定义 DNS 方案','Blacklist':'黑名单','Whitelist':'白名单','Blocked domains, one per line':'拦截域名，每行一个','Allowed domains, one per line':'放行域名，每行一个','Filter List':'过滤清单','Refresh':'刷新','Clear':'清空','Test domain':'测试域名','Test DNS':'测试 DNS','Gateway ON':'网关开启','Gateway OFF':'网关关闭','READY':'就绪','WAITING':'等待中','blocked':'已拦截','pending FULL':'待处理已满','DNS cache':'DNS 缓存','compiled':'已编译','not compiled':'未编译','same':'同信道','cross':'跨信道','task':'任务运行','no task':'无任务','bind ok':'绑定正常','bind wait':'等待绑定','last':'最近','avg':'平均','max':'最大','full':'已满','timeout':'超时','fail':'失败','none':'无','custom':'自定义','Gateway not available':'网关不可用','Remote DNS list changed. Finish editing or save to overwrite.':'远端 DNS 列表已变化。请完成编辑或保存以覆盖。','Resetting DNS stats...':'正在清零 DNS 统计...','DNS stats reset':'DNS 统计已清零','Whitelist allows specific subdomain exceptions; blocked root domains cannot be reopened.':'白名单允许特定子域例外；已拦截的根域不能重新放行。','items':'项','No blocked domains recorded':'暂无被拦截域名记录','Already in blacklist':'已在黑名单','Already whitelisted':'已在白名单','Not allowed':'不允许','Add to Whitelist':'加入白名单','DNS filter list unavailable':'DNS 过滤列表不可用','empty domain':'域名为空','would be blocked':'将被拦截','would be allowed':'将被放行','gateway disabled':'网关未启用','DNS test failed':'DNS 测试失败','cannot add domain':'无法添加域名','Cleared':'已清空','matched whitelist':'命中白名单','matched blacklist':'命中黑名单','not in blacklist':'不在黑名单','domain is blocked root':'该域名是被拦截根域','whitelist full (max 200)':'白名单已满（最多 200）',
  'Debug Log':'调试日志','Debug logging':'调试日志','Recent debug output':'最近调试输出','Shows recent WebUI and firmware log lines.':'显示最近的 WebUI 和固件日志。','Turns WebUI debug log output on or off.':'开启或关闭 WebUI 调试日志输出。','Toggle WebUI and firmware debug output':'开启或关闭 WebUI 与固件调试日志输出',
  'Firmware Update':'固件更新','Manual OTA':'手动 OTA','Manual firmware upload only. Select a local .bin and flash it to the device.':'仅保留手动固件上传。选择本地 .bin 并刷写到设备。','Tap to select firmware .bin':'点击选择 firmware .bin','Or drag and drop a file here':'或将文件拖到这里','Uploading...':'上传中...','Flash Firmware':'刷写固件','Reset OTA Credentials':'重置 OTA 凭据','OTA Credentials Reset':'OTA 凭据已重置','OTA Username:':'OTA 用户名：','OTA Password:':'OTA 密码：','Flashing...':'刷写中...','Done! Device is rebooting...':'完成！设备正在重启...','Upload failed:':'上传失败：','Connection error':'连接错误','Use the generated PlatformIO firmware.bin for this board.':'请使用为这块板生成的 PlatformIO firmware.bin。','Current build path:':'当前构建路径：',
  'Confirm':'确认','Continue':'继续','Cancel':'取消','Reboot device?':'重启设备？','CAN bus writes affect vehicle behavior. Remove device immediately if unexpected behavior occurs. Not affiliated with any vehicle manufacturer.':'CAN 写入会影响车辆行为。如出现异常请立即拔除设备。与任何车厂无关联。'
};
Object.assign(I18N_ZH,{'Ali':'阿里','Tencent':'腾讯','fetch error':'获取失败','network':'网络错误','scan failed':'扫描失败'});
const I18N_EN={};Object.keys(I18N_ZH).forEach(k=>I18N_EN[I18N_ZH[k]]=k);
Object.assign(I18N_EN,{});
const I18N_RX=[
  [/^Connection to (.+) lost\. Reload after reconnecting\.$/,'与 $1 的连接已断开。重连后请刷新页面。'],
  [/^Connection to (.+) lost\. Switch to your normal WiFi and open (.+)$/,'与 $1 的连接已断开。请切回常用 WiFi 并打开 $2'],
  [/^Connected: (.+) \u2022 (.+) \u2022 switch to that WiFi and open this IP$/,'已连接：$1 \u2022 $2 \u2022 切换到该 WiFi 后打开此 IP'],
  [/^Connected: (.+) \u2022 (.+)$/,'已连接：$1 \u2022 $2'],
  [/^Connecting to (.+)\.\.\.$/,'正在连接 $1...'],
  [/^Connecting to (.+)$/,'正在连接 $1'],
  [/^(.+) saved \u2022 retry in ([0-9]+)s(.*)$/,'已保存 $1 个 \u2022 $2s 后重试$3'],
  [/^(.+) saved(.*)$/,'已保存 $1 个$2'],
  [/^Max ([0-9]+) networks$/,'最多保存 $1 个网络'],
  [/^Delete network "(.+)"\?$/,'删除网络“$1”？'],
  [/^AP CH(.+) \u2022 auto match STA(.*)$/,'AP 信道$1 \u2022 自动匹配 STA$2'],
  [/^([0-9]+) clients?$/,'$1 个客户端'],
  [/^Whitelist ([0-9]+)\/([0-9]+) \u2022 Blacklist ([0-9]+)\/([0-9]+)$/,'白名单 $1/$2 \u2022 黑名单 $3/$4'],
  [/^Gateway (ON|OFF) \u2022 NAT (READY|WAITING) \u2022 AP clients ([0-9]+) \u2022 blocked ([0-9]+)(.*)$/,'网关$1 \u2022 NAT $2 \u2022 AP 客户端 $3 \u2022 已拦截 $4$5'],
  [/^(.+) free \/ (.+) total \u2022 used ([0-9]+)%$/,'$1 可用 / 总计 $2 \u2022 已用 $3%'],
  [/^(.+) used \/ (.+) \u2022 ([0-9]+)%$/,'$1 已用 / $2 \u2022 $3%'],
  [/^(.+) tasks$/,'$1 个任务'],
  [/^(.+) cores \u2022 (.+) MHz now$/,'$1 核 \u2022 当前 $2 MHz'],
  [/^(.+) cores \u2022 (.+) \u2022 max (.+) MHz$/,'$1 核 \u2022 $2 \u2022 最高 $3 MHz'],
  [/^running on core (.+)$/,'运行于核心 $1'],
  [/^Uploading\.\.\. ([0-9]+)%$/,'上传中... $1%'],
  [/^Upload failed: (.+)$/,'上传失败：$1']
];
function trText(value){
  let s=String(value);
  if(dashLang!=='zh')return I18N_EN[s]||s;
  if(I18N_ZH[s])return I18N_ZH[s];
  for(const r of I18N_RX){if(r[0].test(s))return s.replace(r[0],r[1]);}
  return s;
}
const setText=(id,value)=>{const el=$(id);if(el)el.textContent=trText(value);};
const setClass=(id,value)=>{const el=$(id);if(el)el.className=value;};
function clientCountText(n){
  n=Number(n)||0;
  return dashLang==='zh'?(n+' 个客户端'):(n+' client'+(n===1?'':'s'));
}
function injectionStatusLabel(armed){
  return armed?(dashLang==='zh'?'\u5199\u5165\u5f00\u542f':'CAN WRITE ON'):(dashLang==='zh'?'\u53ea\u8bfb\u6a21\u5f0f':'READ ONLY');
}
let state={can:true,nagMode:0,nagAv2Min:1.5,nagAv2Max:1.8};
let otaFile=null;
let otaUser=localStorage.getItem('otaU')||'',otaPass=localStorage.getItem('otaP')||'';
let logSince=0;
let dashConfirmState=null;
let dashboardPollTimers=[];
let dashboardPollFailures=0;
let dashboardStatusOk=false;
let dashboardInitialLoaded=false;
let dashboardPollStopped=false;
let systemStatusTimer=null;
let systemStatusEnabled=false;
let wifiNagInitialUiApplied=false;
let dashboardStaIp='';
let networkPerformanceMode=localStorage.getItem('netPerfMode')!=='0';
let uiModeSetting=localStorage.getItem('uiMode')||'auto';
let uiModeEffective='phone';
const pollLocks={};

function normalizeUiMode(v){
  v=String(v||'auto').toLowerCase();
  return (v==='car'||v==='phone'||v==='auto')?v:'auto';
}
function detectAutoUiMode(){
  const ua=navigator.userAgent||'';
  const w=Math.max(window.innerWidth||0,screen.width||0);
  const h=Math.max(window.innerHeight||0,screen.height||0);
  const touch=(navigator.maxTouchPoints||0)>0||('ontouchstart' in window);
  const landscape=w>h;
  const wide=w>=900;
  const shortPanel=h<=900;
  const android=/Android/i.test(ua);
  const webview=/\bwv\b|Version\/4\.0/i.test(ua);
  if(touch&&landscape&&wide&&(shortPanel||android||webview))return 'car';
  return 'phone';
}
function queryUiMode(){
  const m=String(location.search||'').match(/[?&]ui=(auto|car|phone)\b/i);
  return m?m[1].toLowerCase():'';
}
function resolveUiMode(){
  const forced=normalizeUiMode(queryUiMode()||uiModeSetting);
  return forced==='auto'?detectAutoUiMode():forced;
}
function isCarUiActive(){
  return uiModeEffective==='car';
}
function setCollapsedPanel(el,collapsed,persist){
  if(!el)return;
  el.classList.toggle('collapsed',!!collapsed);
  const btn=el.querySelector('.card-min-btn,.subsec-btn');
  if(btn)btn.textContent=trText(collapsed?'Show':'Hide');
  if(persist&&el.dataset.collapseKey)localStorage.setItem(el.dataset.collapseKey,collapsed?'1':'0');
}
function expandCarEssentials(){
  ['system-card','config-card'].forEach(id=>setCollapsedPanel($(id),false,true));
  ['config-hardware-section','wifi-internet-section','gateway-section'].forEach(id=>setCollapsedPanel($(id),false,true));
}
function expandWifiNagDefaults(){
  ['config-card','config-hardware-section','wifi-hotspot-section','wifi-internet-section','gateway-section'].forEach(id=>setCollapsedPanel($(id),false,true));
}
function updateUiModeUi(){
  document.querySelectorAll('.ui-mode-btn').forEach(btn=>{
    const active=(btn.dataset.uiMode||'auto')===uiModeSetting;
    btn.classList.toggle('active',active);
    btn.setAttribute('aria-pressed',active?'true':'false');
  });
  const label=trText(uiModeEffective==='car'?'Detected: Car':'Detected: Phone');
  const el=$('ui-mode-detected');if(el){el.textContent=(uiModeSetting==='auto'?label:(trText('Manual')+': '+trText(uiModeEffective==='car'?'Car':'Phone')));}
  const side=$('car-side-mode');if(side){side.textContent=trText(uiModeSetting==='auto'?'Auto':'Manual')+' / '+trText(uiModeEffective==='car'?'Car':'Phone');}
}
function applyWifiNagMode(){
  document.body.classList.add('wifi-nag');
  const title=document.querySelector('.hdr-title');if(title)title.textContent='EVtools WIFI-NAG';
  setText('hw-badge','WIFI-NAG');
  setText('s-inj-lbl','CAN Write');
  if(!wifiNagInitialUiApplied){
    wifiNagInitialUiApplied=true;
    setCollapsedPanel($('system-card'),false,false);
    expandWifiNagDefaults();
  }
}
function applyUiMode(){
  uiModeSetting=normalizeUiMode(uiModeSetting);
  uiModeEffective=resolveUiMode();
  if(document.body){
    document.body.classList.toggle('ui-car',uiModeEffective==='car');
    document.body.classList.toggle('ui-phone',uiModeEffective!=='car');
  }
  updateUiModeUi();
}
function setUiMode(mode,persist){
  uiModeSetting=normalizeUiMode(mode);
  if(persist)localStorage.setItem('uiMode',uiModeSetting);
  applyUiMode();
  if(isCarUiActive())expandCarEssentials();
  startDashboardPolling();
}
function scrollCarSection(id){
  const el=$(id);if(!el)return;
  const card=el.closest&&el.closest('.card');
  if(card)setCollapsedPanel(card,false,true);
  if(el.classList&&el.classList.contains('subsec'))setCollapsedPanel(el,false,true);
  el.scrollIntoView({behavior:isCarUiActive()?'auto':'smooth',block:'start'});
}

function stopDashboardPolling(){
  if(dashboardPollStopped)return;
  dashboardPollStopped=true;
  dashboardPollTimers.forEach(clearInterval);
  dashboardPollTimers=[];
  if(systemStatusTimer){clearInterval(systemStatusTimer);systemStatusTimer=null;}
  $('dot').className='sdot dot-off';
  $('hdr-desc').textContent=trText('Dashboard disconnected');
  let msg='Connection to '+location.hostname+' lost. Reload after reconnecting.';
  if(dashboardStaIp&&dashboardStaIp!==location.hostname)msg='Connection to '+location.hostname+' lost. Switch to your normal WiFi and open http://'+dashboardStaIp;
  $('wifi-status').textContent=trText(msg);
  $('wifi-status').style.color='var(--err)';
}

function dashboardVisible(){
  return !document.hidden&&!dashboardPollStopped;
}
function intervalVisible(fn,ms){
  return setInterval(()=>{if(dashboardVisible())fn();},ms);
}

function updateNetworkPerformanceUi(){
  const t=$('net-perf-tgl');if(t)t.checked=networkPerformanceMode;
  const s=$('net-perf-status');
  if(s){
    s.textContent=isCarUiActive()
      ?'Car UI: status 7s, network diagnostics 45s, heavy lists manual only'
      :(networkPerformanceMode
        ?'ON: status 5s, network diagnostics 30s, heavy lists manual only'
        :'OFF: status 2s, network diagnostics 10s, DNS/filter lists auto refresh');
    s.style.color=(networkPerformanceMode||isCarUiActive())?'var(--ok)':'var(--warn)';
    applyDashboardI18n(s);
  }
}
function clearDashboardPollingIntervals(){
  dashboardPollTimers.forEach(clearInterval);
  dashboardPollTimers=[];
}
function startDashboardPolling(){
  clearDashboardPollingIntervals();
  const car=isCarUiActive();
  const fast=!networkPerformanceMode&&!car;
  dashboardPollTimers.push(intervalVisible(poll,car?7000:(fast?2000:5000)));
  dashboardPollTimers.push(intervalVisible(loadWifiStatus,car?45000:(fast?10000:30000)));
  dashboardPollTimers.push(intervalVisible(loadApStatus,car?45000:(fast?10000:30000)));
  dashboardPollTimers.push(intervalVisible(loadGatewayStatus,car?45000:(fast?10000:30000)));
  dashboardPollTimers.push(intervalVisible(pollLog,5000));
  if(fast){
    dashboardPollTimers.push(intervalVisible(loadWifiNetworks,30000));
    dashboardPollTimers.push(intervalVisible(loadGatewayBlocked,5000));
    dashboardPollTimers.push(intervalVisible(()=>loadGatewayDns(true),15000));
  }
  updateNetworkPerformanceUi();
}
function setNetworkPerformanceMode(enabled,persist){
  networkPerformanceMode=!!enabled;
  if(persist)localStorage.setItem('netPerfMode',networkPerformanceMode?'1':'0');
  startDashboardPolling();
  if(dashboardVisible()){
    poll();loadWifiStatus();loadApStatus();loadGatewayStatus();
    if(!networkPerformanceMode&&!isCarUiActive()){loadWifiNetworks();loadGatewayBlocked();loadGatewayDns(true);}
  }
}

function noteDashboardPoll(ok){
  if(ok){dashboardPollFailures=0;dashboardStatusOk=true;return;}
  if(dashboardPollStopped)return;
  dashboardStatusOk=false;
  dashboardPollFailures++;
  $('dot').className='sdot dot-off';
  $('hdr-desc').textContent=trText('Dashboard reconnecting');
}

async function fetchPollJson(url,timeoutMs,trackConnection){
  const ctrl=new AbortController();
  const timer=setTimeout(()=>ctrl.abort(),timeoutMs||2500);
  try{
    const r=await fetch(url,{signal:ctrl.signal});
    if(!r.ok)throw new Error('HTTP '+r.status);
    const d=await r.json();
    if(trackConnection)noteDashboardPoll(true);
    return d;
  }catch(e){
    if(trackConnection)noteDashboardPoll(false);
    throw e;
  }finally{
    clearTimeout(timer);
  }
}

async function runPoll(name,fn){
  if(document.hidden)return;
  if(dashboardPollStopped||pollLocks[name])return;
  pollLocks[name]=true;
  try{return await fn();}finally{pollLocks[name]=false;}
}

function waitMs(ms){return new Promise(resolve=>setTimeout(resolve,ms));}

function orderDashboardCards(){
  const stat=document.querySelector('.stat-grid');
  if(!stat||!stat.parentNode)return;
  const cards=Array.from(document.querySelectorAll('.card'));
  const findCard=label=>cards.find(c=>{const t=c.querySelector('.card-title');return t&&t.textContent.trim().toLowerCase().startsWith(label);});
  [findCard('configuration'),findCard('system status')].filter(Boolean).reverse().forEach(c=>{
    stat.parentNode.insertBefore(c,stat.nextSibling);
  });
}
function initCardMinimizers(){
  document.querySelectorAll('.card').forEach((card,i)=>{
    const hdr=card.querySelector('.card-hdr');if(!hdr||hdr.querySelector('.card-min-btn'))return;
    const title=card.querySelector('.card-title');
    const key='cardCollapse:v2:'+i+':'+((title?title.textContent:'card').trim().toLowerCase().replace(/[^a-z0-9]+/g,'-'));
    card.dataset.collapseKey=key;
    const btn=document.createElement('button');
    btn.type='button';
    btn.className='sniff-btn card-min-btn';
    btn.onclick=()=>{
      const collapsed=!card.classList.contains('collapsed');
      card.classList.toggle('collapsed',collapsed);
      localStorage.setItem(key,collapsed?'1':'0');
      btn.textContent=trText(collapsed?'Show':'Hide');
    };
    hdr.appendChild(btn);
    const stored=localStorage.getItem(key);
    const titleText=(title?title.textContent:'').trim().toLowerCase();
    const carDefaultOpen=isCarUiActive()&&(titleText.startsWith('configuration')||titleText.startsWith('system status'));
    const collapsed=stored===null?!carDefaultOpen:stored==='1';
    card.classList.toggle('collapsed',collapsed);
    btn.textContent=trText(collapsed?'Show':'Hide');
  });
}
function initSubsectionMinimizers(){
  document.querySelectorAll('.subsec').forEach((sec,i)=>{
    const hdr=sec.querySelector('.subsec-head');if(!hdr||hdr.querySelector('.subsec-btn'))return;
    const explicitKey=sec.dataset.subkey||'';
    const title=sec.querySelector('.subsec-title');
    const safe=((title?title.textContent:'section').trim().toLowerCase().replace(/[^a-z0-9]+/g,'-'));
    const key='subCollapse:v2:'+(explicitKey||i+':'+safe);
    sec.dataset.collapseKey=key;
    const btn=document.createElement('button');
    btn.type='button';
    btn.className='sniff-btn subsec-btn';
    btn.onclick=()=>{
      const collapsed=!sec.classList.contains('collapsed');
      sec.classList.toggle('collapsed',collapsed);
      localStorage.setItem(key,collapsed?'1':'0');
      btn.textContent=trText(collapsed?'Show':'Hide');
    };
    hdr.appendChild(btn);
    const stored=localStorage.getItem(key);
    const carDefaultOpen=isCarUiActive()&&['config-hardware','config-wifi-internet','config-gateway'].includes(explicitKey);
    const collapsed=stored===null?!carDefaultOpen:stored==='1';
    sec.classList.toggle('collapsed',collapsed);
    btn.textContent=trText(collapsed?'Show':'Hide');
  });
}

function actionErrorMessage(e,fallback){
  if(!e)return fallback;
  if(e.name==='AbortError'||e.name==='SyntaxError'||e.message==='Failed to fetch'||e.message==='Empty response')return fallback;
  return e.message||fallback;
}

async function fetchJsonWithTimeout(url,options,timeoutMs){
  const ctrl=new AbortController();
  const timer=setTimeout(()=>ctrl.abort(),timeoutMs||2500);
  try{
    const opts=Object.assign({},options||{});
    opts.signal=ctrl.signal;
    const r=await fetch(url,opts);
    const text=await r.text();
    if(!text||!text.trim())throw new Error(r.ok?'Empty response':('HTTP '+r.status));
    const d=JSON.parse(text);
    if(!r.ok)throw new Error(d.error||('HTTP '+r.status));
    return d;
  }finally{
    clearTimeout(timer);
  }
}


function dashConfirmResolve(ok){
  if(!dashConfirmState)return;
  const resolve=dashConfirmState.resolve;
  dashConfirmState=null;
  $('confirm-modal').style.display='none';
  document.body.style.overflow='';
  resolve(!!ok);
}

function dashConfirmBackdrop(ev){
  if(ev.target===$('confirm-modal'))dashConfirmResolve(false);
}

function dashConfirm(message,title,okText,cancelText){
  if(dashConfirmState)dashConfirmResolve(false);
  return new Promise(resolve=>{
    dashConfirmState={resolve};
    $('confirm-title').textContent=trText(title||'Confirm');
    $('confirm-msg').textContent=trText(message||'');
    $('confirm-ok').textContent=trText(okText||'Continue');
    $('confirm-cancel').textContent=trText(cancelText||'Cancel');
    $('confirm-modal').style.display='flex';
    document.body.style.overflow='hidden';
    setTimeout(()=>{$('confirm-ok').focus();},0);
  });
}

document.addEventListener('keydown',e=>{
  if(e.key==='Escape'){
    if(dashConfirmState)dashConfirmResolve(false);
    closeHelpPanels(document);
  }
});
document.addEventListener('click',e=>{
  if(!e.target.closest('.title-help')&&!e.target.closest('.inline-help-panel')){
    closeHelpPanels(document);
  }
});

function toggleTheme(){
  const html=document.documentElement;
  const isDark=html.getAttribute('data-theme')==='dark';
  applyTheme(isDark?'light':'dark',true);
}
function autoThemeByTime(){
  const h=new Date().getHours();
  return h>=7&&h<19?'light':'dark';
}
function applyTheme(theme,manual){
  const t=theme==='light'?'light':'dark';
  document.documentElement.setAttribute('data-theme',t);
  const btn=$('theme-btn');
  if(btn)btn.innerHTML=t==='dark'?'&#9788; '+trText('Light'):'&#9790; '+trText('Dark');
  localStorage.setItem('theme',t);
  if(manual)localStorage.setItem('themeMode','manual');
}
function refreshAutoTheme(){
  const mode=localStorage.getItem('themeMode')||'auto';
  if(mode==='manual')return;
  applyTheme(autoThemeByTime(),false);
}
function i18nSkip(el){
  return !el||['SCRIPT','STYLE','TEXTAREA','INPUT','OPTION'].includes(el.nodeName);
}
function i18nNodeText(node){
  if(!node||!node.nodeValue||!node.nodeValue.trim()||i18nSkip(node.parentElement))return;
  const raw=node.nodeValue;
  const lead=(raw.match(/^\s*/)||[''])[0],tail=(raw.match(/\s*$/)||[''])[0];
  const mid=raw.trim();
  const out=trText(mid);
  if(out!==mid)node.nodeValue=lead+out+tail;
}
function i18nElementAttrs(el){
  if(!el||['SCRIPT','STYLE'].includes(el.nodeName))return;
  ['placeholder','title','aria-label'].forEach(a=>{const v=el.getAttribute&&el.getAttribute(a);if(v){const t=trText(v);if(t!==v)el.setAttribute(a,t);}});
}
function applyDashboardI18n(root){
  root=root||document.body;
  if(!root)return;
  if(root.nodeType===Node.TEXT_NODE){i18nNodeText(root);return;}
  i18nElementAttrs(root);
  const walker=document.createTreeWalker(root,NodeFilter.SHOW_TEXT,{acceptNode:n=>i18nSkip(n.parentElement)?NodeFilter.FILTER_REJECT:NodeFilter.FILTER_ACCEPT});
  let n;while((n=walker.nextNode()))i18nNodeText(n);
  root.querySelectorAll&&root.querySelectorAll('[placeholder],[title],[aria-label]').forEach(i18nElementAttrs);
  updateLanguageButton();
}
function updateLanguageButton(){
  document.documentElement.setAttribute('lang',dashLang==='zh'?'zh-CN':'en');
  const b=$('lang-btn');if(b)b.textContent=dashLang==='zh'?'English':'\u4e2d\u6587';
}
function toggleLanguage(){
  dashLang=dashLang==='zh'?'en':'zh';
  localStorage.setItem('dashLang',dashLang);
  applyDashboardI18n(document.body);
  const t=document.documentElement.getAttribute('data-theme')||'dark';
  $('theme-btn').innerHTML=t==='dark'?'&#9788; '+trText('Light'):'&#9790; '+trText('Dark');
}
function showSafetyNotice(){
  const m=$('safety-modal');
  if(!m)return;
  m.style.display='flex';
  document.body.style.overflow='hidden';
  setTimeout(()=>{const b=$('safety-ok');if(b)b.focus();},0);
}
function acceptSafetyNotice(){
  const m=$('safety-modal');
  if(m)m.style.display='none';
  if(!dashConfirmState)document.body.style.overflow='';
}
(function(){
  const mode=localStorage.getItem('themeMode')||'auto';
  const t=mode==='manual'?(localStorage.getItem('theme')||autoThemeByTime()):autoThemeByTime();
  document.documentElement.setAttribute('data-theme',t);
  // will be updated after DOM ready
  window.addEventListener('DOMContentLoaded',()=>{
    applyTheme(t,false);
    setInterval(refreshAutoTheme,60000);
    updateLanguageButton();
    applyDashboardI18n(document.body);
    showSafetyNotice();
    const obs=new MutationObserver(muts=>{
      if(dashLang!=='zh')return;
      muts.forEach(m=>{
        m.addedNodes&&m.addedNodes.forEach(n=>applyDashboardI18n(n));
        if(m.type==='characterData')i18nNodeText(m.target);
      });
    });
    obs.observe(document.body,{childList:true,subtree:true,characterData:true});
  });
})();

function updSeg(el,v,cls){
  if(!el)return;
  el.querySelectorAll('.'+cls).forEach(b=>b.classList.toggle('active',parseInt(b.dataset.v)===v));
}

function updateInjectButtons(active){
  const btn=$('btn-can-toggle');
  if(btn){
    btn.textContent=trText(active?'CAN Write Off':'CAN Write On');
    btn.classList.toggle('btn-stop',!!active);
    if(!active){
      btn.style.background='var(--accBg)';
      btn.style.color='var(--acc)';
      btn.style.borderColor='var(--accBd)';
    }else{
      btn.style.background='';
      btn.style.color='';
      btn.style.borderColor='';
    }
  }
}

function updateFsdControl(d){
  const enabled=!!d.ci;
  state.can=enabled;
  const writeTgl=$('can-write-tgl');if(writeTgl)writeTgl.checked=enabled;
  const nagMeta=$('nag-echo-meta');if(nagMeta&&typeof d.nagEcho!=='undefined')nagMeta.textContent=trText('echo')+': '+d.nagEcho;
  updateNagControl(d);
  updateCanDiagnostics(d);
  updateBleFsd(d,false);
}

function bleFsdValue(d,name,fallback){
  return d[name]!==undefined?d[name]:fallback;
}
let bleFsdAutoCollapsePending=false;
let bleFsdAutoCollapseTarget='';
function setBleFsdControlsCollapsed(collapsed,persist){
  const controls=$('ble-rx-controls'),btn=$('ble-rx-controls-toggle');
  if(controls)controls.style.display=collapsed?'none':'block';
  if(btn)btn.textContent=collapsed?'Expand settings':'Collapse settings';
  if(persist)localStorage.setItem('bleFsdControlsCollapsed',collapsed?'1':'0');
}
function toggleBleFsdControls(){
  const controls=$('ble-rx-controls');
  setBleFsdControlsCollapsed(!controls||controls.style.display!=='none',true);
}
function saveBleFsdCollapsePreference(){
  const auto=$('ble-rx-auto-collapse');
  localStorage.setItem('bleFsdAutoCollapse',auto&&auto.checked?'1':'0');
}
function initBleFsdControls(){
  const auto=$('ble-rx-auto-collapse');
  if(auto)auto.checked=localStorage.getItem('bleFsdAutoCollapse')!=='0';
  setBleFsdControlsCollapsed(localStorage.getItem('bleFsdControlsCollapsed')==='1',false);
}
function updateBleFsd(d,includeConfig){
  const enabled=!!bleFsdValue(d,'enabled',d.bleRxEnabled);
  const connected=!!bleFsdValue(d,'connected',d.bleRxConnected);
  const scanning=!!bleFsdValue(d,'scanning',d.bleRxDiscoveryActive);
  const state=String(bleFsdValue(d,'state',d.bleRxState)||'DISABLED');
  const remaining=Number(bleFsdValue(d,'remainingMs',d.bleRxRemainingMs)||0);
  const rssi=Number(bleFsdValue(d,'rssi',d.bleRxRssi)||0);
  const peerMac=String(bleFsdValue(d,'peerMac',d.bleRxPeerMac)||'');
  const peerName=String(bleFsdValue(d,'peerName',d.bleRxPeerName)||'');
  const subscribed=!!bleFsdValue(d,'subscribed',d.bleRxSubscribed);
  const lastPacket=Number(bleFsdValue(d,'lastPacketAtMs',0)||0);
  const seq=Number(bleFsdValue(d,'lastSequence',d.bleRxLastSeq)||0);
  const reject=String(bleFsdValue(d,'lastReject',d.bleRxReject)||'none');
  const reason=String(bleFsdValue(d,'reason',d.bleRxReason)||'--');
  const windows=Number(bleFsdValue(d,'windows',d.bleRxWindows)||0);
  const crc=Number(bleFsdValue(d,'crcErrors',d.bleRxCrcErrors)||0);
  const dup=Number(bleFsdValue(d,'duplicates',d.bleRxDuplicates)||0);
  const timeout=Number(bleFsdValue(d,'timeouts',d.bleRxTimeouts)||0);
  const t=$('ble-rx-enabled');if(t)t.checked=enabled;
  const scanBtn=$('ble-rx-scan-btn');if(scanBtn)scanBtn.textContent=connected?'Scan nearby (keep connection)':'Scan LILYGO (10s)';
  if(includeConfig){
    const mac=$('ble-rx-mac'),r=$('ble-rx-rssi'),w=$('ble-rx-window');
    if(mac&&document.activeElement!==mac)mac.value=d.mac||'';
    if(r&&document.activeElement!==r)r.value=Number(d.rssiThreshold===undefined?-90:d.rssiThreshold);
    if(w&&document.activeElement!==w)w.value=Number(d.testWindowMs===undefined?10000:d.testWindowMs);
  }
  const link=connected?'CONNECTED':(scanning?'SCANNING':(enabled?'WAITING':'OFF'));
  setCanDiag('ble-rx-link',link+(rssi?(' / '+rssi+' dBm'):''),connected?'ok':enabled?'warn':'dim');
  const deviceText=peerMac?((peerName||'Unnamed')+' / '+peerMac):(connected?'Connecting device…':'--');
  setCanDiag('ble-rx-device',deviceText,connected?'ok':peerMac?'warn':'dim');
  setCanDiag('ble-rx-session',(subscribed?'SUBSCRIBED':'NOT SUBSCRIBED')+' / '+(lastPacket?'RX':'NO RX'),
             connected&&subscribed?'ok':connected?'warn':'dim');
  setCanDiag('ble-rx-state',state+(state==='TEST_ACTIVE'?(' / '+Math.ceil(remaining/1000)+'s'):''),
             state==='TEST_ACTIVE'?'ok':state==='AWAIT_CLEAR'?'warn':'dim');
  setCanDiag('ble-rx-seq',seq||'--',seq?'ok':'dim');
  setCanDiag('ble-rx-errors',crc+' / '+dup,(crc||dup)?'warn':'ok');
  setCanDiag('ble-rx-windows',windows+' / '+timeout,timeout?'warn':'ok');
  setCanDiag('ble-rx-reject',reject,reject==='none'?'ok':'warn');
  const reasonEl=$('ble-rx-reason');if(reasonEl){
    reasonEl.textContent='Receiver: '+reason+' — diagnostic-only; no BLE-to-CAN control link.';
    reasonEl.style.color=state==='TEST_ACTIVE'?'var(--ok)':reject!=='none'?'var(--warn)':'var(--tx3)';
  }
  const auto=$('ble-rx-auto-collapse');
  const targetMatches=!bleFsdAutoCollapseTarget||
    peerMac.toUpperCase()===bleFsdAutoCollapseTarget;
  if(bleFsdAutoCollapsePending&&connected&&subscribed&&!scanning&&targetMatches){
    if(!auto||auto.checked)setBleFsdControlsCollapsed(true,true);
    bleFsdAutoCollapsePending=false;
    bleFsdAutoCollapseTarget='';
  }
}
async function loadBleFsdConfig(){
  return runPoll('ble_fsd',async()=>{
    try{const d=await fetchPollJson('/ble_fsd',2000);updateBleFsd(d,true);}catch(e){}
  });
}
async function saveBleFsdConfig(){
  const enabled=$('ble-rx-enabled')&&$('ble-rx-enabled').checked?1:0;
  const mac=$('ble-rx-mac')?$('ble-rx-mac').value.trim():'';
  const rssi=$('ble-rx-rssi')?$('ble-rx-rssi').value:'-90';
  const windowMs=$('ble-rx-window')?$('ble-rx-window').value:'10000';
  bleFsdAutoCollapsePending=!!enabled&&!!mac;
  bleFsdAutoCollapseTarget=mac.toUpperCase();
  try{
    const body='enabled='+enabled+'&mac='+encodeURIComponent(mac)+'&rssi='+encodeURIComponent(rssi)+'&window='+encodeURIComponent(windowMs);
    const r=await fetch('/ble_fsd',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
    const d=await r.json();
    if(!r.ok||d.ok===false)throw new Error(d.error||'save failed');
    updateBleFsd(d,true);
  }catch(e){
    bleFsdAutoCollapsePending=false;
    bleFsdAutoCollapseTarget='';
    const reason=$('ble-rx-reason');if(reason){reason.textContent='BLE save failed: '+(e.message||'error');reason.style.color='var(--err)';}
  }
}
let bleFsdScanTimer=null;
function renderBleFsdScan(d){
  const list=$('ble-rx-scan-results'),status=$('ble-rx-scan-status'),btn=$('ble-rx-scan-btn');
  if(status){status.textContent=d.scanning?'Scanning nearby BLE devices...':((d.devices||[]).length+' device(s) found');status.style.color=d.scanning?'var(--acc)':'var(--tx3)';}
  if(btn)btn.disabled=!!d.scanning;
  if(!list)return;
  const devices=d.devices||[];
  if(!devices.length){list.style.display=d.scanning?'block':'none';list.innerHTML=d.scanning?'<div style="padding:10px;color:var(--tx3);font-size:11px">Scanning…</div>':'';return;}
  list.style.display='block';
  list.innerHTML=devices.map(x=>{
    const mac=escapeHtml(x.mac||'');
    const name=escapeHtml(x.name||'Unnamed BLE device');
    const service=x.fsdService?'<span style="color:var(--ok)">FSD service</span>':'<span style="color:var(--tx3)">other service</span>';
    const relation=x.connected?'<span style="color:var(--ok)">connected</span>':(x.saved?'<span style="color:var(--acc)">saved</span>':'');
    return '<div style="display:grid;grid-template-columns:minmax(0,1fr) auto;gap:8px;align-items:center;padding:8px 10px;border-bottom:1px solid var(--bd)">'+
      '<div style="min-width:0"><div style="font-size:12px;font-weight:600;white-space:nowrap;overflow:hidden;text-overflow:ellipsis">'+name+'</div>'+
      '<div style="font:11px monospace;color:var(--tx3)">'+mac+' • '+(x.rssi||0)+' dBm • '+service+(relation?' • '+relation:'')+'</div></div>'+
      '<button class="sniff-btn" data-ble-mac="'+mac+'" onclick="useBleFsdDevice(this.dataset.bleMac)">Use</button></div>';
  }).join('');
}
async function pollBleFsdScan(){
  try{
    const d=await fetchPollJson('/ble_fsd_scan',2000);
    renderBleFsdScan(d);
    if(!d.scanning&&bleFsdScanTimer){clearInterval(bleFsdScanTimer);bleFsdScanTimer=null;}
  }catch(e){}
}
async function scanBleFsd(){
  if(bleFsdScanTimer){clearInterval(bleFsdScanTimer);bleFsdScanTimer=null;}
  setBleFsdControlsCollapsed(false,true);
  try{
    const d=await fetchPollJson('/ble_fsd_scan?start=1',2000);
    renderBleFsdScan(d);
    bleFsdScanTimer=setInterval(pollBleFsdScan,900);
  }catch(e){
    const status=$('ble-rx-scan-status');if(status){status.textContent='BLE scan failed';status.style.color='var(--err)';}
  }
}
function useBleFsdDevice(mac){
  const input=$('ble-rx-mac');if(input)input.value=mac||'';
  const enabled=$('ble-rx-enabled');if(enabled)enabled.checked=true;
  bleFsdAutoCollapsePending=true;
  bleFsdAutoCollapseTarget=String(mac||'').toUpperCase();
  saveBleFsdConfig();
}

async function saveCanWrite(){
  const t=$('can-write-tgl');
  if(!t)return;
  const requested=!!t.checked;
  try{
    const r=await fetch('/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'can='+(requested?'1':'0')});
    if(!r.ok)throw new Error('HTTP '+r.status);
    const d=await r.json();
    state.can=typeof d.can==='boolean'?d.can:requested;
    t.checked=state.can;
    updateInjectButtons(state.can);
    if(requested&&!state.can)addLog(trText('CAN write blocked by safety protection'),'le');
    poll();
  }catch(e){
    addLog(trText('CAN write save failed'),'le');
    poll();
  }
}

function updateNagControl(d){
  const mode=Number(d.nagMode===undefined?state.nagMode:d.nagMode)||0;
  const min=Number(d.nagAv2MinNm===undefined?state.nagAv2Min:d.nagAv2MinNm);
  const max=Number(d.nagAv2MaxNm===undefined?state.nagAv2Max:d.nagAv2MaxNm);
  state.nagMode=mode;state.nagAv2Min=isNaN(min)?1.5:min;state.nagAv2Max=isNaN(max)?1.8:max;
  const seg=$('nag-mode-seg');if(seg)updSeg(seg,mode,'hw-btn');
  const minInp=$('nag-av2-min');if(minInp&&document.activeElement!==minInp)minInp.value=state.nagAv2Min.toFixed(2);
  const maxInp=$('nag-av2-max');if(maxInp&&document.activeElement!==maxInp)maxInp.value=state.nagAv2Max.toFixed(2);
  const meta=$('nag-mode-meta');if(meta)meta.textContent=trText(mode===4?'A_V2: random sweep':'A: waiting for BLE trigger');
  const live=Number(d.nagLiveTorqueNm||0);
  const last=Number(d.nagLastTorqueNm||0);
  const liveMeta=$('nag-live-meta');if(liveMeta)liveMeta.textContent=(dashLang==='zh'?'\u5b9e\u65f6: ':'live: ')+live.toFixed(2)+' Nm';
  const writeMeta=$('nag-write-meta');if(writeMeta)writeMeta.textContent=(dashLang==='zh'?'\u5199\u5165: ':'write: ')+last.toFixed(2)+' Nm';
  const av2=$('nag-av2-meta');if(av2)av2.textContent=trText('skip')+': '+(d.nagOwnEchoSkip||0)+' \u00b7 '+trText('drop')+': '+(d.nagTxDrop||0);
  const amActive=!!d.nagAModeActive;
  const amLeft=Math.ceil((Number(d.nagAModeRemainingMs)||0)/1000);
  const amState=$('nag-amode-state');
  if(amState){amState.textContent=(dashLang==='zh'?'A\u6a21\u5f0f: ':'A mode: ')+(amActive?(dashLang==='zh'?'\u6fc0\u6d3b '+amLeft+'s':'active '+amLeft+'s'):(dashLang==='zh'?'\u672a\u6fc0\u6d3b':'idle'));amState.classList.toggle('on',amActive);}
  const amLive=$('nag-amode-live');if(amLive)amLive.textContent=(dashLang==='zh'?'\u5b9e\u65f6: ':'live: ')+live.toFixed(2)+' Nm';
  const amWrite=$('nag-amode-write');if(amWrite)amWrite.textContent=(dashLang==='zh'?'\u5199\u5165: ':'write: ')+last.toFixed(2)+' Nm';
  const amSrc=$('nag-amode-src');if(amSrc){const src=String(d.bleRxPeerName||d.bleRxPeerMac||d.bleRxState||'--');amSrc.textContent=(dashLang==='zh'?'\u6765\u6e90: ':'source: ')+src;}
  const amEcho=$('nag-amode-echo');if(amEcho)amEcho.textContent=(dashLang==='zh'?'\u56de\u58f0: ':'echo: ')+(d.nagEcho||0)+' \u00b7 '+(dashLang==='zh'?'\u7a97\u53e3: ':'win: ')+(d.bleRxWindows||0);
}

function setCanDiag(id,text,level){
  setText(id,text);
  setClass(id,'can-diag-value '+(level==='err'?'v-err':level==='warn'?'v-warn':level==='ok'?'v-ok':'v-dim'));
}

function canDiagCounterLevel(value,error){
  value=Number(value)||0;
  return value>0?(error?'err':'warn'):'dim';
}

function updateCanDiagnostics(d){
  const available=!!d.twaiAvailable;
  const stateName=String(d.twaiState||'UNAVAILABLE');
  const stateLevel=stateName==='RUNNING'?'ok':stateName==='BUS-OFF'?'err':stateName==='RECOVERING'||stateName==='STOPPED'?'warn':'dim';
  setCanDiag('can-diag-state',trText(stateName),stateLevel);

  const tec=Number(d.twaiTec)||0,rec=Number(d.twaiRec)||0;
  const errMax=Math.max(tec,rec);
  setCanDiag('can-diag-errors',available?(tec+' / '+rec):'--',errMax>=96?'err':errMax>0?'warn':'ok');

  const txq=Number(d.twaiTxQueue)||0,rxq=Number(d.twaiRxQueue)||0;
  setCanDiag('can-diag-queues',available?(txq+' / '+rxq):'--',(txq>0||rxq>32)?'warn':available?'ok':'dim');

  const reject=Number(d.txerr)||0,stale=Number(d.twaiStaleDrop)||0;
  setCanDiag('can-diag-reject',reject+' / '+stale,canDiagCounterLevel(reject+stale,false));

  const txFailed=Number(d.twaiTxFailed)||0,busError=Number(d.twaiBusError)||0;
  setCanDiag('can-diag-txbus',txFailed+' / '+busError,canDiagCounterLevel(txFailed+busError,true));

  const arb=Number(d.twaiArbLost)||0,arbRate=Number(d.twaiArbRate)||0;
  setCanDiag('can-diag-arb',arb+' / '+arbRate.toFixed(1),arbRate>200?'warn':available?'ok':'dim');

  const missed=Number(d.twaiRxMissed)||0,overrun=Number(d.twaiRxOverrun)||0;
  setCanDiag('can-diag-rxloss',missed+' / '+overrun,canDiagCounterLevel(missed+overrun,true));

  const busOff=Number(d.twaiBusOff)||0,recovered=Number(d.twaiRecovered)||0;
  setCanDiag('can-diag-busoff',busOff+' / '+recovered,canDiagCounterLevel(busOff,true));

  const warning=Number(d.twaiErrWarn)||0,passive=Number(d.twaiErrPassive)||0;
  setCanDiag('can-diag-warning',warning+' / '+passive,canDiagCounterLevel(warning+passive,true));

  const safetyTripped=!!d.twaiSafetyTripped;
  const safetyReason=String(d.twaiSafetyReason||'NONE');
  const safetyTrips=Number(d.twaiSafetyTrips)||0;
  setCanDiag('can-diag-safety',
    safetyTripped?(trText('LOCKED')+' \u00b7 '+trText(safetyReason)):(trText('READY')+' \u00b7 '+safetyTrips),
    safetyTripped?'err':'ok');
}

async function resetCanDiagnostics(){
  const btn=$('can-diag-reset');
  if(btn)btn.disabled=true;
  try{
    const r=await fetch('/can_diag_reset',{method:'POST'});
    if(!r.ok)throw new Error('HTTP '+r.status);
    poll();
  }catch(e){addLog(trText('CAN diagnostics reset failed'),'le');}
  finally{if(btn)btn.disabled=false;}
}

async function setNagMode(mode){
  mode=mode===4?4:0;
  state.nagMode=mode;
  const seg=$('nag-mode-seg');if(seg)updSeg(seg,mode,'hw-btn');
  try{
    const r=await fetch('/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'nagMode='+mode});
    if(!r.ok)throw new Error('HTTP '+r.status);
    poll();
  }catch(e){addLog(trText('Nag mode save failed'),'le');}
}

async function saveNagAv2(){
  const minInp=$('nag-av2-min'),maxInp=$('nag-av2-max');
  if(!minInp||!maxInp)return;
  let min=Number(minInp.value),max=Number(maxInp.value);
  if(isNaN(min))min=-1.8;if(isNaN(max))max=1.8;
  min=Math.max(-1.8,Math.min(1.8,min));
  max=Math.max(-1.8,Math.min(1.8,max));
  if(min>max){const t=min;min=max;max=t;}
  minInp.value=min.toFixed(2);maxInp.value=max.toFixed(2);
  try{
    const body='av2MinNm='+encodeURIComponent(min.toFixed(2))+'&av2MaxNm='+encodeURIComponent(max.toFixed(2));
    const r=await fetch('/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
    if(!r.ok)throw new Error('HTTP '+r.status);
    state.nagAv2Min=min;state.nagAv2Max=max;
    poll();
  }catch(e){addLog(trText('A_V2 range save failed'),'le');}
}

async function pushLogging(){
  const body='eprn='+($('tgl-eprn').checked?'1':'0');
  try{await fetch('/logging',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});}catch(e){}
  if($('tgl-eprn').checked)pollLog();
  poll();
}

async function emergencyStop(){
  try{
    updateInjectButtons(false);
    state.can=false;
    setText('s-inj','READ ONLY');
    setClass('s-inj','stat-val v-err');
    await fetch('/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'can=0'});
  }catch(e){}
  poll();
}
async function resumeInj(){try{const r=await fetch('/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'can=1'});if(!r.ok)throw new Error('HTTP '+r.status);const d=await r.json();state.can=!!d.can;updateInjectButtons(state.can);if(!state.can)addLog(trText('CAN write blocked by safety protection'),'le');}catch(e){addLog(trText('CAN write save failed'),'le');}poll();}
async function toggleCanWriteTopButton(){if(state.can)await emergencyStop();else await resumeInj();}
async function reboot(){if(!await dashConfirm('Reboot device?','Reboot','Reboot'))return;try{await fetch('/reboot',{method:'POST'});}catch(e){}}

function fmtUp(s){
  if(s<60)return s+'s';
  if(s<3600)return Math.floor(s/60)+'m '+String(s%60).padStart(2,'0')+'s';
  return Math.floor(s/3600)+'h '+Math.floor((s%3600)/60)+'m';
}
function fmtBytes(n){
  n=Number(n)||0;
  if(n>=1048576)return (n/1048576).toFixed(n>=10485760?1:2)+' MB';
  if(n>=1024)return (n/1024).toFixed(n>=10240?0:1)+' KB';
  return n+' B';
}
function pct(used,total){
  total=Number(total)||0;used=Number(used)||0;
  return total>0?Math.max(0,Math.min(100,used*100/total)):0;
}
function clampPct(value){
  return Math.max(0,Math.min(100,Number(value)||0));
}
function mixColor(a,b,t){
  t=Math.max(0,Math.min(1,t));
  const r=Math.round(a[0]+(b[0]-a[0])*t);
  const g=Math.round(a[1]+(b[1]-a[1])*t);
  const bl=Math.round(a[2]+(b[2]-a[2])*t);
  return 'rgb('+r+','+g+','+bl+')';
}
function progressColor(value){
  const v=clampPct(value);
  const green=[22,163,74],yellow=[245,166,35],red=[220,38,38];
  if(v<=30)return 'rgb('+green.join(',')+')';
  if(v<=60)return mixColor(green,yellow,(v-30)/30);
  if(v<80)return mixColor(yellow,red,(v-60)/20);
  return 'rgb('+red.join(',')+')';
}
function setFillElement(el,value){
  if(!el)return;
  const v=Math.max(0,Math.min(100,Number(value)||0));
  el.style.width=v+'%';
  el.style.background=progressColor(v);
}
function setFill(id,value){
  setFillElement($(id),value);
}
function fmtAddr(n){
  n=Number(n)||0;
  return n?'0x'+n.toString(16).toUpperCase():'--';
}
function resetSystemStatusUi(){
  ['sys-chip','sys-cpu','sys-clocks','sys-board','sys-temp','sys-reset','sys-runtime','sys-heap','sys-internal','sys-largest','sys-minheap','sys-psram','sys-tasks','sys-flash','sys-spiffs','sys-rssi','sys-wifi-mode','sys-apclients','sys-ble','sys-wireless','sys-fw'].forEach(id=>setText(id,'--'));
  setText('sys-summary',trText('Monitoring off'));
  setText('sys-cpu-load',trText('off'));
  ['sys-cpu0-fill','sys-cpu1-fill','sys-heap-fill','sys-internal-fill','sys-psram-fill','sys-app-fill','sys-spiffs-fill'].forEach(id=>setFill(id,0));
}
function startSystemMonitor(){
  if(systemStatusEnabled)return;
  systemStatusEnabled=true;
  const t=$('sys-monitor-tgl');if(t)t.checked=true;
  loadSystemStatus();
  systemStatusTimer=setInterval(loadSystemStatus,1000);
}
function stopSystemMonitor(){
  systemStatusEnabled=false;
  const t=$('sys-monitor-tgl');if(t)t.checked=false;
  if(systemStatusTimer){clearInterval(systemStatusTimer);systemStatusTimer=null;}
  resetSystemStatusUi();
}
function toggleSystemMonitor(){
  const t=$('sys-monitor-tgl');
  if(t&&t.checked)startSystemMonitor();else stopSystemMonitor();
}
function initSystemMonitor(){
  stopSystemMonitor();
}
function escapeHtml(s){
  return String(s===undefined?'':s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
}
async function loadSystemStatus(){
  if(!systemStatusEnabled)return;
  return runPoll('system_status',async()=>{
    try{
      const d=await fetchPollJson('/system_status',2500);
      const heapUsed=(d.heap_total||0)-(d.heap_free||0);
      const internalTotal=d.internal_total||d.sram_bytes||0;
      const internalFree=d.internal_free||0;
      const internalUsed=Math.max(0,internalTotal-internalFree);
      const psramUsed=(d.psram_total||0)-(d.psram_free||0);
      const appUsed=d.app_used||0;
      const spiffsUsed=d.spiffs_used||0;
      setText('sys-summary',(d.module||d.chip||'ESP32')+' \u2022 '+(d.cores||'?')+' cores \u2022 '+(d.cpu_mhz||'?')+' MHz now');
      setText('sys-chip',(d.module||d.chip||'?')+' rev '+(d.revision===undefined?'?':d.revision)+' / '+(d.target||''));
      setText('sys-cpu',(d.cores||'?')+' cores \u2022 '+(d.cpu_policy||'fixed')+' \u2022 max '+(d.cpu_max_mhz||240)+' MHz');
      setText('sys-clocks','CPU '+(d.cpu_mhz||'?')+' MHz \u2022 APB '+(d.apb_mhz||'?')+' MHz \u2022 XTAL '+(d.xtal_mhz||'?')+' MHz');
      if(d.cpu_load_valid){
        setText('sys-cpu-load','CPU0 '+(d.cpu0_load||0)+'% \u2022 CPU1 '+(d.cpu1_load||0)+'%');
        setFill('sys-cpu0-fill',d.cpu0_load||0,60,85);setFill('sys-cpu1-fill',d.cpu1_load||0,60,85);
      }else{
        setText('sys-cpu-load',trText('warming up'));
        setFill('sys-cpu0-fill',0);setFill('sys-cpu1-fill',0);
      }
      setText('sys-board','SRAM '+fmtBytes(d.sram_bytes)+' + RTC '+fmtBytes(d.rtc_sram_bytes)+' \u2022 ROM '+fmtBytes(d.rom_bytes));
      setText('sys-temp',d.temp_c===null||d.temp_c===undefined?trText('unavailable'):(Number(d.temp_c).toFixed(1)+' \u00B0C'));
      setText('sys-reset',d.reset||'?');
      setText('sys-runtime',fmtUp(d.uptime||0)+' \u2022 running on core '+(d.core===undefined?'?':d.core));
      setText('sys-heap',fmtBytes(d.heap_free)+' free / '+fmtBytes(d.heap_total)+' total \u2022 used '+Math.round(pct(heapUsed,d.heap_total))+'%');
      setText('sys-internal',internalTotal?(fmtBytes(internalFree)+' free / '+fmtBytes(internalTotal)+' total \u2022 used '+Math.round(pct(internalUsed,internalTotal))+'%'):trText('unavailable'));
      setText('sys-largest',fmtBytes(d.heap_largest));
      setText('sys-minheap',fmtBytes(d.heap_min));
      setText('sys-psram',(d.psram_total||0)?(fmtBytes(d.psram_free)+' free / '+fmtBytes(d.psram_total)+' total \u2022 used '+Math.round(pct(psramUsed,d.psram_total))+'%'):trText('not enabled'));
      setText('sys-tasks',(d.tasks||'?')+' tasks');
      setText('sys-flash',fmtBytes(d.flash_size)+' flash \u2022 '+((d.flash_speed||0)/1000000||80)+' MHz \u2022 '+(d.app_label||'?')+' '+fmtBytes(appUsed)+' / '+fmtBytes(d.app_size)+' @ '+fmtAddr(d.app_addr));
      setText('sys-spiffs',d.spiffs_ok?(fmtBytes(spiffsUsed)+' used / '+fmtBytes(d.spiffs_total)+' \u2022 '+Math.round(pct(spiffsUsed,d.spiffs_total))+'%'):'SPIFFS '+trText('unavailable'));
      setText('sys-rssi',d.wifi_rssi===null||d.wifi_rssi===undefined?(d.wifi_connected?'?':'offline'):(d.wifi_rssi+' dBm'));
      setText('sys-wifi-mode',(d.wifi_mode||'?')+' \u2022 '+(d.wifi_connected?trText('STA online'):trText('STA offline'))+' \u2022 sleep '+(d.wifi_sleep?trText('on'):trText('off')));
      setText('sys-apclients',(d.ap_clients||0)+' client'+((d.ap_clients||0)===1?'':'s'));
      setText('sys-ble',(d.ble_supported?trText('supported'):trText('not supported'))+' \u2022 '+(d.ble_enabled?trText('enabled'):trText('firmware disabled')));
      setText('sys-wireless',(d.wifi_standard||'2.4GHz Wi-Fi')+' \u2022 '+(d.wifi_max_mbps||150)+' Mbps max \u2022 BLE 5 LE');
      setText('sys-fw',(d.mac||'--')+' \u2022 '+(d.firmware||'unknown')+' \u2022 IDF '+(d.idf||'?'));
      setFill('sys-heap-fill',pct(heapUsed,d.heap_total),70,85);
      setFill('sys-internal-fill',pct(internalUsed,internalTotal),70,85);
      setFill('sys-psram-fill',pct(psramUsed,d.psram_total),70,85);
      setFill('sys-app-fill',pct(appUsed,d.app_size),70,90);
      setFill('sys-spiffs-fill',pct(spiffsUsed,d.spiffs_total),70,90);
    }catch(e){
      setText('sys-summary','System status unavailable');
    }
  });
}
// OTA upload
function fileSelected(file){
  if(!file)return;
  otaFile=file;
  const drop=$('ota-drop');
  drop.querySelector('.ota-text').textContent=file.name;
  drop.querySelector('.ota-sub').textContent=(file.size/1024).toFixed(0)+' KB';
  $('ota-upload-btn').style.display='block';
}

function handleDrop(e){
  e.preventDefault();
  $('ota-drop').classList.remove('drag');
  const file=e.dataTransfer.files[0];
  if(file&&file.name.endsWith('.bin'))fileSelected(file);
}

function resetOtaCredentials(){
  localStorage.removeItem('otaU');
  localStorage.removeItem('otaP');
  otaUser='';
  otaPass='';
  const btn=$('ota-reset-btn');
  if(btn){
    btn.textContent=trText('OTA Credentials Reset');
    setTimeout(()=>{btn.textContent=trText('Reset OTA Credentials');},1500);
  }
}

async function uploadFirmware(){
  if(!otaFile)return;
  if(!otaUser){otaUser=prompt('OTA Username:')||'';localStorage.setItem('otaU',otaUser);}
  if(!otaPass){otaPass=prompt('OTA Password:')||'';localStorage.setItem('otaP',otaPass);}
  if(!otaUser||!otaPass)return;
  const prog=$('ota-progress');
  const fill=$('ota-fill');
  const status=$('ota-status');
  prog.style.display='block';
  $('ota-upload-btn').disabled=true;
  $('ota-upload-btn').textContent=trText('Flashing...');

  const xhr=new XMLHttpRequest();
  xhr.upload.onprogress=e=>{
    if(e.lengthComputable){
      const pct=Math.round(e.loaded/e.total*100);
      setFillElement(fill,pct);
      status.textContent=trText('Uploading... '+pct+'%');
    }
  };
  xhr.onload=()=>{
    if(xhr.status===200){
      status.textContent=trText('Done! Device is rebooting...');
      setFillElement(fill,100);
      setTimeout(()=>window.location.reload(),5000);
    } else {
      status.textContent=trText('Upload failed: '+xhr.status);
      status.style.color='var(--err)';
    }
    $('ota-upload-btn').disabled=false;
    $('ota-upload-btn').textContent=trText('Flash Firmware');
  };
  xhr.onerror=()=>{
    status.textContent=trText('Connection error');
    status.style.color='var(--err)';
    $('ota-upload-btn').disabled=false;
  };
  xhr.open('POST','/update',true,otaUser,otaPass);
  xhr.setRequestHeader('Content-Type','application/octet-stream');
  xhr.setRequestHeader('X-File-Name',otaFile.name);
  xhr.setRequestHeader('X-File-Size',otaFile.size);
  xhr.send(otaFile);
}

async function poll(){
  return runPoll('status',async()=>{
    try{
      const d=await fetchPollJson('/status',5000,true);
      applyWifiNagMode();
      const on=!!d.can,armed=!!d.ci,fpsVal=Number(d.fps||0);
      const hdrDesc=$('hdr-desc');
      const rxTotal=Number(d.rx||0);
      if(hdrDesc)hdrDesc.textContent=on?(trText('CAN running')+' \u2022 '+fpsVal.toFixed(1)+' Hz \u2022 RX '+rxTotal):trText('Waiting for CAN frames');
      state.can=armed;
      updateFsdControl(d);
      updateInjectButtons(armed);
      setClass('dot','sdot '+(d.txerr>5?'dot-warn':on?'dot-on':'dot-off'));
      setText('s-can',on?trText('CAN OK'):trText('CAN waiting'));
      setClass('s-can','stat-val '+(on?'v-ok':'v-err'));
      setText('s-inj',injectionStatusLabel(armed));
      setClass('s-inj','stat-val '+(armed?'v-ok':'v-err'));
      setText('s-fps',on?(fpsVal.toFixed(1)+' Hz / RX '+rxTotal):(fpsVal.toFixed(1)+' Hz / '+trText('No frames')));
      setClass('s-fps','stat-val '+(fpsVal>5?'v-acc':'v-dim'));
      setText('s-rx',d.rx);
      setText('s-tx',d.tx);
      setText('s-txerr',d.txerr);
      setClass('s-txerr','stat-val '+(d.txerr>0?'v-warn':'v-dim'));
      setText('s-up',fmtUp(d.up));
      setFill('fps-fill',Math.min(fpsVal/20*100,100));
      setText('hw-badge','WIFI-NAG');
      const eprn=$('tgl-eprn');if(eprn&&typeof d.eprn!=='undefined')eprn.checked=d.eprn;
      if(!dashboardInitialLoaded){
        dashboardInitialLoaded=true;
        loadWifiNetworks();loadWifiStatus();loadApStatus();loadBleFsdConfig();loadGatewayDns();loadGatewayStatus();if(!isCarUiActive())loadGatewayBlocked();
      }
    }catch(e){}
  });
}

function colorLog(l){
  if(l.includes('ERR')||l.includes('FAIL'))return'<span class="le">'+l+'</span>';
  if(l.includes('[CFG]'))return'<span class="lc">'+l+'</span>';
  if(l.includes('[OK]')||l.includes('[BOOT]'))return'<span class="lf">'+l+'</span>';
  if(l.includes('[OTA]'))return'<span class="lo">'+l+'</span>';
  return l;
}
async function pollLog(){
  return runPoll('log',async()=>{
    if(!$('tgl-eprn').checked||!dashboardStatusOk)return;
    try{
      const d=await fetchPollJson('/log?since='+logSince,2000);
    if(d.seq)logSince=d.seq;
    if(!d.lines.length)return;
    const el=$('log');
    const newHtml=d.lines.map(colorLog).join('\n');
    if(el.textContent==='Waiting...'||el.textContent==='绛夊緟涓?..'||!el.dataset.logSeen)el.innerHTML=newHtml,el.dataset.logSeen='1';
    else el.innerHTML+='\n'+newHtml;
    // trim to 100 lines
    const lines=el.innerHTML.split('\n');
    if(lines.length>100)el.innerHTML=lines.slice(-100).join('\n');
    el.scrollTop=el.scrollHeight;
    }catch(e){}
  });
}

// AP Hotspot management
async function saveAP(){
  const ssid=$('ap-ssid').value,pass=$('ap-pass').value,hidden=$('ap-hidden').checked?'1':'0';
  if(!ssid){$('ap-status').textContent=trText('Enter hotspot name');$('ap-status').style.color='var(--err)';return;}
  if(pass&&pass.length<8){$('ap-status').textContent=trText('Password min 8 chars');$('ap-status').style.color='var(--err)';return;}
  try{const r=await fetch('/ap_config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'ssid='+encodeURIComponent(ssid)+'&pass='+encodeURIComponent(pass)+'&hidden='+hidden});
    const d=await r.json();
    if(d.ok){$('ap-status').textContent=trText(d.msg||'Saved! AP starts on CH1 and auto matches STA after WiFi connects.');$('ap-status').style.color='var(--ok)';$('ap-pass').value='';}
    else{$('ap-status').textContent=trText(d.error||'Error');$('ap-status').style.color='var(--err)';}
  }catch(e){$('ap-status').textContent=trText('Error');$('ap-status').style.color='var(--err)';}
}
async function loadApStatus(){
  return runPoll('ap_status',async()=>{
    if(!dashboardStatusOk)return;
    try{const d=await fetchPollJson('/ap_status',2000);
    if(d.ssid)$('ap-ssid').value=d.ssid;
    $('ap-clients').textContent=clientCountText(d.clients);
    if(typeof d.hidden!=='undefined')$('ap-hidden').checked=!!d.hidden;
    if($('ap-status')){
      const sync=d.last_channel_sync_ms?(' \u2022 '+trText('sync')+' '+trText(d.last_channel_sync_ok?'ok':'fail')+' CH'+(d.last_channel_sync_target||'?')):'';
      $('ap-status').textContent=trText('AP CH'+(d.channel||'?')+' \u2022 auto match STA'+sync);
      $('ap-status').style.color='var(--tx3)';
    }
    if(d.stored){$('ap-stored').textContent=trText('saved');$('ap-stored').style.color='var(--ok)';}
    else{$('ap-stored').textContent=trText('firmware default');$('ap-stored').style.color='var(--tx3)';}
    }catch(e){}
  });
}
// 鈹€鈹€ WiFi management 鈹€鈹€
function toggleStaticIP(){
  $('static-fields').style.display=$('wifi-static').checked?'block':'none';
}
function rssiIcon(r){
  if(r>=-50) return '\u2587\u2587\u2587\u2587';
  if(r>=-60) return '\u2587\u2587\u2587\u2581';
  if(r>=-70) return '\u2587\u2587\u2581\u2581';
  return '\u2587\u2581\u2581\u2581';
}
function wifiAuthLabel(a){
  const n=Number(a);
  if(n===0)return 'OPEN';
  if(n===1)return 'WEP';
  if(n===2)return 'WPA';
  if(n===3)return 'WPA2';
  if(n===4)return 'WPA/WPA2';
  if(n===5)return 'ENT';
  if(n===6)return 'WPA3';
  if(n===7)return 'WPA2/WPA3';
  if(n===8)return 'WAPI';
  if(n===9)return 'WPA3-ENT';
  return 'AUTH'+(Number.isFinite(n)?n:'?');
}
async function scanWifi(){
  $('scan-btn').textContent=trText('Scanning...');$('scan-btn').disabled=true;
  try{
    const r=await fetch('/wifi_scan?force=1');const d=await r.json();
    if(!r.ok)throw new Error(d.error||'scan failed');
    const el=$('wifi-nets');
    if(!d.networks.length){el.innerHTML='<div style="padding:8px;font-size:11px;color:var(--tx3);text-align:center">'+trText('No networks found')+'</div>';el.style.display='block';}
    else{el.innerHTML=d.networks.map(n=>'<div data-wifi-ssid="'+escapeHtml(n.ssid)+'" style="padding:6px 10px;cursor:pointer;display:flex;justify-content:space-between;align-items:center;border-bottom:1px solid var(--bd);font-size:12px" onmouseover="this.style.background=\'var(--bg)\'" onmouseout="this.style.background=\'\'"><span>'+(n.enc?'\uD83D\uDD12 ':'')+escapeHtml(n.ssid)+'</span><span style="color:var(--tx3);font-size:10px">'+rssiIcon(n.rssi)+' '+n.rssi+'dBm CH'+n.ch+' '+wifiAuthLabel(n.auth)+'</span></div>').join('');el.querySelectorAll('[data-wifi-ssid]').forEach(row=>row.onclick=()=>pickWifi(row.dataset.wifiSsid||''));el.style.display='block';}
  }catch(e){$('wifi-status').textContent=trText('Scan failed');$('wifi-status').style.color='var(--err)';}
  $('scan-btn').textContent=trText('Scan');$('scan-btn').disabled=false;
}
function pickWifi(ssid){
  $('wifi-ssid').value=ssid;$('wifi-nets').style.display='none';$('wifi-pass').focus();
}
let wifiSlotCache={count:0,max:4,active:-1,networks:[]};
let wifiStatusCache={};
function escapeHtml(s){return String(s||'').replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;').replace(/'/g,'&#39;');}
function renderWifiSlots(){
  const list=$('wifi-saved-list'),wrap=$('wifi-add-wrap'),cnt=$('wifi-slot-count');
  if(!list)return;
  const nets=wifiSlotCache.networks||[];
  const max=wifiSlotCache.max||4;
  const active=wifiSlotCache.active;
  const connectedSsid=wifiStatusCache.connected?String(wifiStatusCache.ssid||''):'';
  const tryingIdx=(!wifiStatusCache.connected&&wifiStatusCache.connecting)?active:-1;
  cnt.textContent='('+nets.length+'/'+max+')';
  if(!nets.length){
    list.innerHTML='<div style="font-size:11px;color:var(--tx3);padding:6px 0">'+trText('No networks saved.')+'</div>';
  }else{
    list.innerHTML=nets.map(n=>{
      const isConnected=connectedSsid&&n.ssid===connectedSsid;
      const isTrying=n.idx===tryingIdx;
      const dotColor=isConnected?'var(--ok)':(isTrying?'var(--warn)':'var(--tx3)');
      const dot='<span title="'+trText(isConnected?'connected':(isTrying?'trying':'saved'))+'" style="display:inline-block;width:8px;height:8px;border-radius:50%;background:'+dotColor+';margin-right:6px"></span>';
      const tag=n.static?'<span style="font-size:10px;color:var(--tx3);margin-left:6px">'+trText('[static]')+'</span>':'';
      const state=isConnected?'<span style="font-size:10px;color:var(--ok);margin-left:6px">'+trText('[connected]')+'</span>':(isTrying?'<span style="font-size:10px;color:var(--warn);margin-left:6px">'+trText('[trying]')+'</span>':'');
      const connectLabel=isConnected?'Reconnect':'Connect';
      return '<div style="display:flex;align-items:center;gap:6px;padding:6px 0;border-bottom:1px solid var(--bd);font-size:12px">'+
        '<div style="flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap">'+dot+escapeHtml(n.ssid)+tag+state+'</div>'+
        '<button class="sniff-btn" onclick="connectWifiSlot('+n.idx+')" style="padding:4px 8px;font-size:11px;border-color:var(--accBd);color:var(--acc)">'+trText(connectLabel)+'</button>'+
        '<button class="sniff-btn" onclick="editWifiSlot('+n.idx+')" style="padding:4px 8px;font-size:11px">'+trText('Edit')+'</button>'+
        '<button class="sniff-btn" onclick="deleteWifiSlot('+n.idx+')" style="padding:4px 8px;font-size:11px;background:var(--errBg);border-color:var(--errBd);color:var(--err)">'+trText('Delete')+'</button>'+
      '</div>';
    }).join('');
  }
  const editIdx=parseInt($('wifi-edit-idx').value,10);
  const canAdd=nets.length<max||editIdx>=0;
  wrap.style.display=canAdd?'':'none';
    $('wifi-save-btn').textContent=trText(editIdx>=0?'Save Changes':'Save & Connect');
}
async function loadWifiNetworks(){
  return runPoll('wifi_networks',async()=>{
    try{
      const d=await fetchPollJson('/wifi_networks',2000);
      wifiSlotCache=d;
      renderWifiSlots();
    }catch(e){}
  });
}
async function loadWifiStatus(){
  return runPoll('wifi_status',async()=>{
    try{const d=await fetchPollJson('/wifi_status',2000);
    wifiStatusCache=d;
    dashboardStaIp=d.connected&&d.ip?d.ip:'';
    if(typeof d.active==='number')wifiSlotCache.active=d.active;
    renderWifiSlots();
    const stName=d.wifi_status_name||('status '+(d.wifi_status===undefined?'?':d.wifi_status));
    const stCode=d.wifi_status===undefined?'?':d.wifi_status;
    const age=d.attempt_age_s===undefined?'':(' \u2022 '+d.attempt_age_s+'s');
    const reason=(d.disconnect_reason_name&&d.disconnect_reason_name!=='none')?(' \u2022 '+d.disconnect_reason_name+'('+d.disconnect_reason+')'):'';
    if(d.connected){
      setText('wifi-status',(d.ip&&d.ip!==location.hostname)?('Connected: '+(d.ssid||'')+' \u2022 '+d.ip+' \u2022 switch to that WiFi and open this IP'):('Connected: '+(d.ssid||'')+' \u2022 '+d.ip));
      $('wifi-status').style.color='var(--ok)';
    }
    else if(d.connecting&&d.ssid){
      setText('wifi-status','Connecting to '+d.ssid+age+' \u2022 '+stName+'('+stCode+')'+reason);$('wifi-status').style.color='var(--warn)';
    }
    else if(d.count>0){
      const retry=d.retry_in_s!==undefined?(' \u2022 retry in '+d.retry_in_s+'s'):'';
      setText('wifi-status',d.count+' saved'+retry+' \u2022 '+stName+'('+stCode+')'+reason);
      $('wifi-status').style.color='var(--tx3)';
    }
    else{
      setText('wifi-status','Not configured');
      $('wifi-status').style.color='var(--tx3)';
    }
    }catch(e){}
  });
}
async function connectWifiSlot(idx){
  const n=(wifiSlotCache.networks||[]).find(x=>x.idx===idx);
  if(!n)return;
  try{
    $('wifi-status').textContent=trText('Connecting to '+n.ssid+'...');
    $('wifi-status').style.color='var(--acc)';
    const r=await fetch('/wifi_connect',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'idx='+idx});
    const d=await r.json().catch(()=>({}));
    if(!r.ok||d.ok===false)throw new Error(d.error||'connect failed');
    wifiSlotCache.active=idx;
    wifiStatusCache={connected:false,connecting:true,ssid:n.ssid,active:idx};
    renderWifiSlots();
    setTimeout(loadWifiStatus,500);
    setTimeout(loadWifiStatus,2500);
    setTimeout(loadWifiStatus,6500);
  }catch(e){
    $('wifi-status').textContent=trText(e.message||'Connect failed');
    $('wifi-status').style.color='var(--err)';
  }
}
function editWifiSlot(idx){
  const n=(wifiSlotCache.networks||[]).find(x=>x.idx===idx);
  if(!n)return;
  $('wifi-edit-idx').value=idx;
  $('wifi-ssid').value=n.ssid;
  $('wifi-pass').value='';
  $('wifi-pass').placeholder=trText('Leave empty to keep current');
  $('wifi-static').checked=!!n.static;
  toggleStaticIP();
  if(n.static){
    $('wifi-ip').value=n.ip||'';
    $('wifi-gw').value=n.gw||'';
    $('wifi-mask').value=n.mask||'255.255.255.0';
    $('wifi-dns').value=n.dns||'';
  }
  renderWifiSlots();
  $('wifi-add-wrap').scrollIntoView({behavior:'smooth',block:'nearest'});
}
function clearWifiForm(){
  $('wifi-edit-idx').value=-1;
  $('wifi-ssid').value='';$('wifi-pass').value='';
  $('wifi-pass').placeholder=trText('Password');
  $('wifi-static').checked=false;toggleStaticIP();
  $('wifi-ip').value='';$('wifi-gw').value='';$('wifi-mask').value='255.255.255.0';$('wifi-dns').value='';
}
async function deleteWifiSlot(idx){
  const n=(wifiSlotCache.networks||[]).find(x=>x.idx===idx);
  if(!n)return;
  if(!await dashConfirm('Delete network "'+n.ssid+'"?','Delete WiFi','Delete'))return;
  try{
    await fetch('/wifi_delete',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'idx='+idx});
    if(parseInt($('wifi-edit-idx').value,10)===idx)clearWifiForm();
    loadWifiNetworks();loadWifiStatus();
  }catch(e){$('wifi-status').textContent=trText('Delete failed');$('wifi-status').style.color='var(--err)';}
}
async function saveWifi(){
  const ssid=$('wifi-ssid').value,pass=$('wifi-pass').value;
  if(!ssid){$('wifi-status').textContent=trText('Enter SSID');$('wifi-status').style.color='var(--err)';return;}
  const editIdx=parseInt($('wifi-edit-idx').value,10);
  const isEdit=editIdx>=0;
  if(!isEdit&&(wifiSlotCache.count||0)>=(wifiSlotCache.max||4)){
    $('wifi-status').textContent=trText('Max '+(wifiSlotCache.max||4)+' networks');$('wifi-status').style.color='var(--err)';return;
  }
  let effectivePass=pass;
  if(isEdit&&!pass){
    effectivePass='';
  }
  let body='ssid='+encodeURIComponent(ssid)+'&pass='+encodeURIComponent(effectivePass);
  if(isEdit)body+='&idx='+editIdx;
  if($('wifi-static').checked){
    body+='&static=1&ip='+encodeURIComponent($('wifi-ip').value)+'&gw='+encodeURIComponent($('wifi-gw').value)+'&mask='+encodeURIComponent($('wifi-mask').value)+'&dns='+encodeURIComponent($('wifi-dns').value);
  }
  try{
    const r=await fetch('/wifi_config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
    const d=await r.json();
    if(!d.ok)throw new Error(d.error||'save failed');
    $('wifi-status').textContent=trText('Connecting to '+ssid+'...');$('wifi-status').style.color='var(--acc)';
    clearWifiForm();
    loadWifiNetworks();
    setTimeout(loadWifiStatus,500);
    setTimeout(loadWifiStatus,2500);
    setTimeout(loadWifiStatus,5500);
  }catch(e){$('wifi-status').textContent=trText(e.message||'Error');$('wifi-status').style.color='var(--err)';}
}
// 鈹€鈹€ STA-AP Gateway / DNS 鈹€鈹€
let gatewayDnsSaving=false;
let gatewayDnsBlackDirty=false,gatewayDnsWhiteDirty=false;
let gatewayDnsLastBlack=null,gatewayDnsLastWhite=null;
const gatewayDnsCacheKey='dashGatewayDnsStateV1';
const gatewayTeslaBlacklist='tesla.cn\ntesla.com\nteslamotors.com\ntesla.services';
const gatewayProfileSafeWhitelist='connman.vn.cloud.tesla.cn\nnav-prd-maps.tesla.cn\nmaps-cn-prd.go.tesla.services\nsignaling.vn.cloud.tesla.cn\napi-prd.vn.cloud.tesla.cn\nmedia-server-me.tesla.cn';
const gatewayProfileAggressiveWhitelist='connman.vn.cloud.tesla.cn\nnav-prd-maps.tesla.cn\nmaps-cn-prd.go.tesla.services\nsignaling.vn.cloud.tesla.cn\napi-prd.vn.cloud.tesla.cn\nmedia-server-me.tesla.cn\nhermes-prd.vn.cloud.tesla.cn\nhermes-stream-prd.vn.cloud.tesla.cn';
const gatewayProfileDescSafe='Conservative Mode: WiFi access / offline navigation / online navigation / China maps / WeChat notifications / Bluetooth music / voice assistant.';
const gatewayProfileDescAggressive='Aggressive Mode: WiFi access / offline navigation / online navigation / China maps / WeChat notifications / Bluetooth music / voice assistant / app vehicle control.';
function gatewayDnsEditing(id){
  const el=$(id);
  return el&&document.activeElement===el;
}
function initGatewayDnsEditing(){
  const black=$('gw-blacklist'),white=$('gw-whitelist');
  if(black&&!black.dataset.dirtyHooked){
    black.dataset.dirtyHooked='1';
    black.addEventListener('input',()=>{gatewayDnsBlackDirty=true;updateGatewayProfileButtons();});
  }
  if(white&&!white.dataset.dirtyHooked){
    white.dataset.dirtyHooked='1';
    white.addEventListener('input',()=>{gatewayDnsWhiteDirty=true;updateGatewayProfileButtons();});
  }
}
function updateGatewayTextarea(id,next,last,dirty,forceApply){
  const el=$(id);
  if(!el)return last;
  next=next||'';
  const remoteChanged=last!==null&&next!==last;
  if(!forceApply&&(dirty||gatewayDnsEditing(id))&&remoteChanged){
    const msg=$('gw-msg');
    if(msg){msg.textContent='Remote DNS list changed. Finish editing or save to overwrite.';msg.style.color='var(--warn)';applyDashboardI18n(msg);}
    return last;
  }
  if(!dirty&&el.value!==next)el.value=next;
  return next;
}
function normalizeGatewayList(v){
  return gatewayListItems(v).join('\n');
}
function gatewayListItems(v){
  const seen=new Set();
  const out=[];
  String(v||'').split(/[\s,;]+/).forEach(x=>{
    const item=x.trim().toLowerCase();
    if(item&&!seen.has(item)){seen.add(item);out.push(item);}
  });
  return out;
}
function gatewayListHasAll(current,template){
  const cur=new Set(gatewayListItems(current));
  return gatewayListItems(template).every(x=>cur.has(x));
}
function gatewayListHasAny(current,template){
  const cur=new Set(gatewayListItems(current));
  return gatewayListItems(template).some(x=>cur.has(x));
}
function mergeGatewayList(current,template){
  return gatewayListItems((current||'')+'\n'+(template||'')).join('\n');
}
function removeGatewayList(current,template){
  const drop=new Set(gatewayListItems(template));
  return gatewayListItems(current).filter(x=>!drop.has(x)).join('\n');
}
function updateGatewayProfileButtons(){
  const white=$('gw-whitelist')?$('gw-whitelist').value:'';
  const safe=gatewayListHasAll(white,gatewayProfileSafeWhitelist);
  const aggressive=gatewayListHasAll(white,gatewayProfileAggressiveWhitelist);
  const sb=$('gw-profile-safe'),ab=$('gw-profile-aggressive'),desc=$('gw-profile-desc');
  if(sb)sb.classList.toggle('active',safe&&!aggressive);
  if(ab)ab.classList.toggle('active',aggressive);
  if(desc){
    desc.textContent=aggressive?gatewayProfileDescAggressive:(safe?gatewayProfileDescSafe:'Custom DNS profile');
    applyDashboardI18n(desc);
  }
}
function readGatewayDnsCache(){
  try{
    const raw=localStorage.getItem(gatewayDnsCacheKey);
    return raw?JSON.parse(raw):null;
  }catch(e){return null;}
}
function writeGatewayDnsCache(d){
  try{
    if(!d||d.ok===false)return;
    localStorage.setItem(gatewayDnsCacheKey,JSON.stringify({
      blacklist:d.blacklist||'',whitelist:d.whitelist||'',
      upstream_mode:(d.upstream_mode!==undefined?d.upstream_mode:0),
      upstream_custom:d.upstream_custom||'',
      upstream_dhcp:d.upstream_dhcp||'',
      upstream_effective:d.upstream_effective||'',
      black_count:d.black_count||0,white_count:d.white_count||0,
      black_max:d.black_max||100,white_max:d.white_max||200
    }));
  }catch(e){}
}
function applyGatewayProfile(profile){
  initGatewayDnsEditing();
  const aggressive=profile==='aggressive';
  if($('gw-enabled'))$('gw-enabled').checked=true;
  if($('gw-blacklist'))$('gw-blacklist').value=gatewayTeslaBlacklist;
  if($('gw-whitelist')){
    let current=$('gw-whitelist').value;
    if(!aggressive&&gatewayListHasAny(current,'hermes-prd.vn.cloud.tesla.cn\nhermes-stream-prd.vn.cloud.tesla.cn'))
      current=removeGatewayList(current,'hermes-prd.vn.cloud.tesla.cn\nhermes-stream-prd.vn.cloud.tesla.cn');
    $('gw-whitelist').value=mergeGatewayList(current,aggressive?gatewayProfileAggressiveWhitelist:gatewayProfileSafeWhitelist);
  }
  gatewayDnsBlackDirty=true;gatewayDnsWhiteDirty=true;
  updateGatewayProfileButtons();
  saveGatewayDns().catch(()=>{});
}
function applyGatewayDnsState(d,opts){
  if(!d||!$('gw-enabled'))return;
  opts=opts||{};
  initGatewayDnsEditing();
  $('gw-enabled').checked=!!d.enabled;
  if($('gw-upstream-mode'))$('gw-upstream-mode').value=String(d.upstream_mode!==undefined?d.upstream_mode:0);
  if($('gw-upstream-custom')&&d.upstream_custom!==undefined)$('gw-upstream-custom').value=d.upstream_custom||'';
  toggleGatewayUpstreamCustom(d);
  if(d.blacklist!==undefined)gatewayDnsLastBlack=updateGatewayTextarea('gw-blacklist',d.blacklist,gatewayDnsLastBlack,opts.saved?false:gatewayDnsBlackDirty,!!opts.saved);
  if(d.whitelist!==undefined)gatewayDnsLastWhite=updateGatewayTextarea('gw-whitelist',d.whitelist,gatewayDnsLastWhite,opts.saved?false:gatewayDnsWhiteDirty,!!opts.saved);
  if(opts.saved){gatewayDnsBlackDirty=false;gatewayDnsWhiteDirty=false;}
  updateGatewayProfileButtons();
  var ce=$('gw-list-counts');
  if(ce){
    var bc=d.black_count||0,bm=d.black_max||100,wc=d.white_count||0,wm=d.white_max||200;
    ce.textContent=trText('Whitelist '+wc+'/'+wm+' \u2022 Blacklist '+bc+'/'+bm);
    ce.style.color=(wc>=wm||bc>=bm)?'var(--err)':'var(--tx3)';
  }
  if(!opts.cached)writeGatewayDnsCache(d);
}
function setGatewayDiag(id,text,color){
  const el=$(id);if(!el)return;
  el.textContent=text;
  el.style.color=color||'var(--tx)';
}
function gatewayDnsSlowColor(d){
  if((d.dns_slow_2000ms||0)>0)return 'var(--err)';
  if((d.dns_slow_1000ms||0)>0||(d.dns_slow_500ms||0)>0)return 'var(--warn)';
  return 'var(--ok)';
}
function gatewayUpstreamModeLabel(v){
  v=String(v||'auto').toLowerCase();
  if(v==='ali')return 'Ali';
  if(v==='tencent')return 'Tencent';
  if(v==='custom')return trText('Custom');
  return trText('Auto');
}
function toggleGatewayUpstreamCustom(d){
  const sel=$('gw-upstream-mode'),inp=$('gw-upstream-custom'),hint=$('gw-upstream-hint');
  const mode=sel?Number(sel.value||0):0;
  document.querySelectorAll('.gateway-upstream-btn').forEach(btn=>{
    const active=Number(btn.dataset.mode||0)===mode;
    btn.classList.toggle('active',active);
    btn.setAttribute('aria-pressed',active?'true':'false');
  });
  if(inp){
    const custom=mode===3;
    inp.disabled=!custom;
    inp.style.opacity=custom?'1':'0.55';
  }
  if(hint){
    let text='Auto uses DHCP DNS from the connected WiFi; public DNS can avoid stale slow/fail counters from a bad router DNS.';
    if(mode===1)text='Using Ali DNS 223.5.5.5.';
    else if(mode===2)text='Using Tencent DNS 119.29.29.29.';
    else if(mode===3)text='Enter a custom upstream DNS IPv4 address.';
    hint.textContent=text;
    applyDashboardI18n(hint);
  }
}
function setGatewayUpstreamMode(mode,persist){
  const sel=$('gw-upstream-mode');
  if(sel)sel.value=String(mode);
  toggleGatewayUpstreamCustom();
  if(persist)saveGatewayDns().catch(()=>{});
}
async function loadGatewayStatus(){
  return runPoll('gateway_status',async()=>{
    try{
      const d=await fetchPollJson('/gateway_status',2000);
      if(!$('gw-status'))return;
      const clients=d.ap_clients||0;
      var statusText=trText(d.enabled?'Gateway ON':'Gateway OFF')+' \u2022 NAT '+trText(d.nat?'READY':'WAITING')+' \u2022 '+trText('AP Clients')+' '+clients+' \u2022 '+trText('blocked')+' '+(d.blocked||0);
      if((d.dns_pending_full||0)>0)statusText+=' \u2022 '+trText('pending FULL')+' '+d.dns_pending_full;
      if(d.dns_resp_cache)statusText+=' \u2022 '+trText('DNS cache')+' '+(d.dns_resp_hits||0)+'/'+((d.dns_resp_hits||0)+(d.dns_resp_misses||0));
      $('gw-status').textContent=statusText;
      $('gw-status').style.color=!d.enabled?'var(--tx3)':((d.dns_pending_full||0)>0?'var(--err)':(d.nat?'var(--ok)':'var(--warn)'));
      const apCh=d.ap_channel?('CH'+d.ap_channel):'CH?';
      const staCh=d.sta_channel?('CH'+d.sta_channel):'CH?';
      const staRssi=(d.sta_rssi===null||d.sta_rssi===undefined)?'RSSI ?':('RSSI '+d.sta_rssi+' dBm');
      setGatewayDiag('gw-diag-ap',(d.ap_ip||'0.0.0.0')+' \u2022 '+apCh+' \u2022 '+clientCountText(clients),clients?'var(--ok)':'var(--tx)');
      setGatewayDiag('gw-diag-sta',d.sta_connected?((d.sta_ip||'0.0.0.0')+' \u2022 '+staRssi+' \u2022 '+staCh):trText('offline'),''+(d.sta_connected?'var(--ok)':'var(--tx3)'));
      setGatewayDiag('gw-diag-nat',trText(d.napt_compiled?'compiled':'not compiled')+' / '+trText(d.nat?'READY':'WAITING'),d.nat?'var(--ok)':(d.enabled?'var(--warn)':'var(--tx3)'));
      setGatewayDiag('gw-diag-radio',apCh+' / STA '+staCh+' \u2022 '+trText(d.same_channel?'same':'cross'),d.same_channel?'var(--ok)':(d.sta_connected?'var(--warn)':'var(--tx3)'));
      setGatewayDiag('gw-diag-dns',trText(d.dns_task_active?'task':'no task')+' / '+trText(d.dns_bind_ok?'bind ok':'bind wait')+' / fd '+(d.dns_sock===undefined?'--':d.dns_sock),d.dns_task_active&&d.dns_bind_ok?'var(--ok)':'var(--warn)');
      setGatewayDiag('gw-diag-slow',trText('last')+' '+(d.dns_latency_last_ms||0)+' ms \u2022 '+trText('avg')+' '+(d.dns_latency_avg_ms||0)+' ms \u2022 >500/'+(d.dns_slow_500ms||0)+' >1s/'+(d.dns_slow_1000ms||0)+' >2s/'+(d.dns_slow_2000ms||0),gatewayDnsSlowColor(d));
      setGatewayDiag('gw-diag-pending',(d.dns_pending||0)+'/'+(d.dns_pending_capacity||64)+' \u2022 '+trText('max')+' '+(d.dns_pending_max||0)+' \u2022 '+trText('full')+' '+(d.dns_pending_full||0)+' \u2022 '+trText('timeout')+' '+(d.dns_timeouts||0),((d.dns_pending_full||0)>0||(d.dns_timeouts||0)>0)?'var(--err)':'var(--ok)');
      const upModeName=String(d.upstream_dns_mode_name||'auto').toLowerCase();
      const upMode=gatewayUpstreamModeLabel(upModeName);
      var upText=trText(d.upstream_dns||'none')+' \u2022 '+upMode;
      if(upModeName==='auto')upText+=' \u2022 DHCP '+trText(d.upstream_dns_dhcp||'none');
      else if(upModeName==='custom')upText+=' \u2022 '+trText('custom')+' '+trText(d.upstream_dns_custom||'none');
      upText+=' \u2022 '+trText('fail')+' '+(d.dns_upstream_fails||0);
      setGatewayDiag('gw-diag-upstream',upText,(d.dns_upstream_fails||0)>0?'var(--warn)':'var(--tx)');
      setGatewayDiag('gw-diag-clients',clientCountText(clients),clients?'var(--ok)':'var(--tx3)');
    }catch(e){
      if($('gw-status')){$('gw-status').textContent=trText('Gateway not available');$('gw-status').style.color='var(--tx3)';}
      ['gw-diag-ap','gw-diag-sta','gw-diag-nat','gw-diag-radio','gw-diag-dns','gw-diag-slow','gw-diag-pending','gw-diag-upstream','gw-diag-clients'].forEach(id=>setGatewayDiag(id,'--','var(--tx3)'));
    }
  });
}
async function loadGatewayDns(force){
  if(gatewayDnsSaving)return;
  try{
    const r=await fetch('/gateway_dns');if(!r.ok)throw new Error('unavailable');
    const d=await r.json();
    applyGatewayDnsState(d);
  }catch(e){}
}
function loadGatewayDnsCached(){
  const d=readGatewayDnsCache();
  if(d)applyGatewayDnsState(d,{cached:true,saved:true});
}
async function resetGatewayDnsStats(){
  const msg=$('gw-msg');
  try{
    if(msg){msg.textContent='Resetting DNS stats...';msg.style.color='var(--tx3)';applyDashboardI18n(msg);}
    const r=await fetch('/gateway_dns_stats_reset',{method:'POST'});
    if(!r.ok)throw new Error('HTTP '+r.status);
    await r.json().catch(()=>({}));
    if(msg){msg.textContent='DNS stats reset';msg.style.color='var(--ok)';applyDashboardI18n(msg);}
    loadGatewayStatus();
  }catch(e){
    if(msg){msg.textContent=trText(e&&e.message?e.message:'Error');msg.style.color='var(--err)';}
  }
}
async function saveGatewayDns(){
  const msg=$('gw-msg');
  try{
    gatewayDnsSaving=true;
    if(msg){msg.textContent=trText('Saving...');msg.style.color='var(--tx3)';}
    const upstreamMode=$('gw-upstream-mode')?$('gw-upstream-mode').value:'0';
    const upstreamCustom=$('gw-upstream-custom')?$('gw-upstream-custom').value:'';
    const body='enabled='+($('gw-enabled').checked?1:0)+'&blacklist='+encodeURIComponent($('gw-blacklist').value)+'&whitelist='+encodeURIComponent($('gw-whitelist').value)+'&upstream_mode='+encodeURIComponent(upstreamMode)+'&upstream_custom='+encodeURIComponent(upstreamCustom);
    const r=await fetch('/gateway_dns',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
    const d=await r.json().catch(()=>({}));
    if(!r.ok||d.ok===false)throw new Error(d.error||'save failed');
    applyGatewayDnsState(d,{saved:true});
    if(msg){msg.textContent=trText('Saved');msg.style.color='var(--ok)';}
    loadGatewayStatus();setTimeout(loadGatewayBlocked,250);
  }catch(e){if(msg){msg.textContent=trText(e.message||'Error');msg.style.color='var(--err)';}}
  finally{gatewayDnsSaving=false;}
}
function openGwBlockedModal(){loadGatewayBlocked();}
function closeGwBlockedModal(){}
function gwBlockedBackdrop(e){}
async function loadGatewayBlocked(){
  const list=$('gw-blocked-list');
  const sum=$('gw-blocked-summary');
  if(!list)return;
  try{
    const r=await fetch('/gateway_blocked');if(!r.ok)throw new Error('HTTP '+r.status);
    const d=await r.json();
    if(sum)sum.textContent=trText('Whitelist allows specific subdomain exceptions; blocked root domains cannot be reopened.')+' - '+(d.length||0)+' '+trText('items');
    if(!d.length){list.innerHTML='<div style="color:var(--tx3);text-align:center;padding:20px">'+trText('No blocked domains recorded')+'</div>';return;}
    list.innerHTML=d.map(x=>{
      const dom=escapeHtml(x.domain||'');
      const btn=x.blacklisted?'<span class="dns-state err">'+trText('Already in blacklist')+'</span>':
        x.whitelisted?'<span class="dns-state ok">'+trText('Already whitelisted')+'</span>':
        x.canWhitelist===false?'<span class="dns-state dim">'+trText('Not allowed')+'</span>':
        '<button class="sniff-btn modal-btn-primary" style="padding:4px 10px;font-size:11px" data-gw-domain="'+dom+'" onclick="addGatewayWhitelist(this.dataset.gwDomain)">'+trText('Add to Whitelist')+'</button>';
      return '<div class="dns-row">'+
        '<div class="dns-domain" title="'+dom+'">'+dom+'<span class="dns-count">x'+(x.count||0)+'</span></div><div>'+btn+'</div></div>';
    }).join('');
  }catch(e){
    list.innerHTML='<div style="color:var(--tx3);text-align:center;padding:20px">'+trText('DNS filter list unavailable')+': '+(e&&e.message?e.message:'fetch error')+'</div>';
  }
}
async function testGatewayDns(){
  const el=$('gw-test-result'),input=$('gw-test-domain');
  const domain=(input&&input.value?input.value:'').trim();
  if(!domain){if(el){el.textContent=trText('empty domain');el.style.color='var(--err)';}return;}
  try{
    const r=await fetch('/gateway_dns_test?domain='+encodeURIComponent(domain));
    if(!r.ok)throw new Error('HTTP '+r.status);
    const d=await r.json();
    const verdict=d.blocked?trText('would be blocked'):trText('would be allowed');
    const mode=trText('Blacklist');
    const reason=trText(d.reason||'');
    const gwState=d.enabled?'':' ('+trText('gateway disabled')+')';
    if(el){
      el.textContent=(d.domain||domain)+' - '+verdict+' - '+mode+' - '+reason+gwState;
      el.style.color=d.blocked?'var(--err)':'var(--ok)';
    }
  }catch(e){
    if(el){el.textContent=trText('DNS test failed')+': '+(e&&e.message?e.message:'network');el.style.color='var(--err)';}
  }
}
async function addGatewayWhitelist(domain){
  const msg=$('gw-blocked-msg')||$('gw-msg');
  try{
    gatewayDnsSaving=true;
    const r=await fetch('/gateway_whitelist_add',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'domain='+encodeURIComponent(domain)});
    const d=await r.json();
    if(!r.ok||!d.ok)throw new Error(d.error||'cannot add domain');
    applyGatewayDnsState(d);
    if(msg){msg.textContent=d.already?trText('Already whitelisted'):trText('Saved')+': '+domain;msg.style.color='var(--ok)';}
    setTimeout(loadGatewayBlocked,250);
  }catch(e){if(msg){msg.textContent=trText(e.message||'cannot add domain');msg.style.color='var(--err)';}}
  finally{gatewayDnsSaving=false;}
}
async function clearGatewayBlocked(){
  try{
    await fetch('/gateway_blocked_clear',{method:'POST'});
    const list=$('gw-blocked-list');if(list)list.innerHTML='<div style="color:var(--tx3);text-align:center;padding:20px">'+trText('Cleared')+'</div>';
    const sum=$('gw-blocked-summary');if(sum)sum.textContent='';
    loadGatewayStatus();
  }catch(e){}
}
applyUiMode();
startDashboardPolling();
window.addEventListener('resize',()=>{
  const prev=uiModeEffective;
  applyUiMode();
  if(prev!==uiModeEffective)startDashboardPolling();
});
document.addEventListener('visibilitychange',()=>{
  if(!dashboardVisible())return;
  poll();loadWifiStatus();loadApStatus();loadGatewayStatus();
  if(!networkPerformanceMode&&!isCarUiActive()){loadWifiNetworks();loadGatewayBlocked();loadGatewayDns(true);}
  pollLog();
});
orderDashboardCards();initCardMinimizers();initSubsectionMinimizers();initBleFsdControls();if(isCarUiActive())expandCarEssentials();initSystemMonitor();loadGatewayDnsCached();loadGatewayDns(true);loadGatewayStatus();if(!networkPerformanceMode&&!isCarUiActive())loadGatewayBlocked();poll();
</script>
</body>
</html>
)HTML";
