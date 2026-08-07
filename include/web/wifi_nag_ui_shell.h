#pragma once

#if defined(ESP32_DASHBOARD) && !defined(NATIVE_BUILD)

static const char WIFI_NAG_UI_SHELL[] PROGMEM = R"HTML(
<!doctype html>
<html lang="zh-CN" data-theme="light">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>WiFi-NAG</title>
<style>
*{box-sizing:border-box}
:root{
  --shell-bg:#f2f5f9;--shell-card:#ffffff;--shell-border:#dfe6ef;
  --shell-text:#1b2735;--shell-muted:#728094;--shell-blue:#3478f6;
  --shell-blue-soft:#eaf2ff;--shell-danger:#d74646;--shell-shadow:0 10px 28px rgba(35,55,82,.09)
}
html[data-theme="dark"]{
  --shell-bg:#11161d;--shell-card:#1b222c;--shell-border:#303b49;
  --shell-text:#f3f6fa;--shell-muted:#9ba8b8;--shell-blue:#72a7ff;
  --shell-blue-soft:#1b3151;--shell-danger:#ff7474;--shell-shadow:0 12px 32px rgba(0,0,0,.28)
}
html,body{height:100%;margin:0;background:var(--shell-bg);color:var(--shell-text);font-family:-apple-system,BlinkMacSystemFont,"Segoe UI","PingFang SC","Microsoft YaHei",sans-serif}
body{display:flex;flex-direction:column;overflow:hidden}
.shell-header{min-height:88px;padding:18px 22px 14px;display:flex;align-items:center;justify-content:space-between;gap:14px;background:var(--shell-bg);border-bottom:1px solid var(--shell-border)}
.brand{display:flex;align-items:center;gap:12px;min-width:0}
.brand-mark{width:42px;height:42px;border-radius:13px;display:flex;align-items:center;justify-content:center;flex:0 0 42px;background:linear-gradient(145deg,#4c91ff,#2867db);color:#fff;font-size:19px;font-weight:900;box-shadow:0 8px 18px rgba(52,120,246,.24)}
.brand-copy{min-width:0}
.brand-title{font-size:22px;font-weight:850;line-height:1.15;letter-spacing:.1px;white-space:nowrap}
.brand-sub{margin-top:4px;font-size:11px;color:var(--shell-muted);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.shell-actions{display:flex;align-items:center;gap:8px;flex:0 0 auto}
.shell-btn{min-height:38px;padding:8px 12px;border:1px solid var(--shell-border);border-radius:10px;background:var(--shell-card);color:var(--shell-text);font:700 12px inherit;cursor:pointer;box-shadow:0 2px 8px rgba(35,55,82,.04)}
.shell-btn:active{transform:translateY(1px)}
.shell-btn.theme{color:var(--shell-blue);border-color:rgba(52,120,246,.28);background:var(--shell-blue-soft)}
.shell-btn.reboot{color:var(--shell-danger)}
#legacy-dashboard{display:block;width:100%;height:calc(100dvh - 88px);border:0;background:var(--shell-bg)}
@media(max-width:560px){
  .shell-header{min-height:76px;padding:14px 12px 10px}
  .brand-mark{width:36px;height:36px;flex-basis:36px;border-radius:11px;font-size:16px}
  .brand-title{font-size:19px}
  .brand-sub{display:none}
  .shell-btn{min-height:36px;padding:7px 9px;font-size:11px}
  #legacy-dashboard{height:calc(100dvh - 76px)}
}
</style>
</head>
<body>
<header class="shell-header">
  <div class="brand">
    <div class="brand-mark">W</div>
    <div class="brand-copy">
      <div class="brand-title">WiFi-NAG</div>
      <div class="brand-sub">ESP32-S3 · CAN 智能控制器</div>
    </div>
  </div>
  <div class="shell-actions">
    <button class="shell-btn theme" id="theme-toggle" type="button">夜间模式</button>
    <button class="shell-btn reboot" id="reboot-btn" type="button">重启</button>
  </div>
</header>
<iframe id="legacy-dashboard" title="WiFi-NAG 控制面板" src="/legacy-dashboard"></iframe>
<script>
(function(){
  const frame=document.getElementById('legacy-dashboard');
  const themeButton=document.getElementById('theme-toggle');
  const rebootButton=document.getElementById('reboot-btn');
  const themeKey='wifiNagTheme';
  let theme=localStorage.getItem(themeKey)==='dark'?'dark':'light';
  let accordionTouched=false;

  function applyTheme(next){
    theme=next==='dark'?'dark':'light';
    document.documentElement.setAttribute('data-theme',theme);
    localStorage.setItem(themeKey,theme);
    localStorage.setItem('theme',theme);
    localStorage.setItem('themeMode','manual');
    themeButton.textContent=theme==='dark'?'日间模式':'夜间模式';
    try{
      const doc=frame.contentDocument;
      if(doc&&doc.documentElement)doc.documentElement.setAttribute('data-theme',theme);
      const win=frame.contentWindow;
      if(win&&typeof win.applyTheme==='function')win.applyTheme(theme,false);
    }catch(e){}
  }

  themeButton.addEventListener('click',()=>applyTheme(theme==='dark'?'light':'dark'));
  rebootButton.addEventListener('click',async()=>{
    if(!window.confirm('确认重启 WiFi-NAG 设备？'))return;
    rebootButton.disabled=true;
    rebootButton.textContent='重启中…';
    try{await fetch('/reboot',{method:'POST'});}catch(e){}
    setTimeout(()=>{rebootButton.disabled=false;rebootButton.textContent='重启';},4000);
  });

  function cardIcon(kind){
    if(kind==='nag')return '<svg viewBox="0 0 24 24"><path d="M12 3l7 3v5c0 5-3 8-7 10-4-2-7-5-7-10V6l7-3z"/><path d="M9 12l2 2 4-5"/></svg>';
    if(kind==='wifi')return '<svg viewBox="0 0 24 24"><path d="M4.5 10.5a12 12 0 0 1 15 0"/><path d="M8 14a7 7 0 0 1 8 0"/><path d="M12 18h.01"/></svg>';
    if(kind==='system')return '<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="3"/><path d="M12 2v3M12 19v3M4.9 4.9L7 7M17 17l2.1 2.1M2 12h3M19 12h3M4.9 19.1L7 17M17 7l2.1-2.1"/></svg>';
    return '<svg viewBox="0 0 24 24"><path d="M12 3v11"/><path d="M8 10l4 4 4-4"/><path d="M5 19h14"/></svg>';
  }

  function iframePatchCss(){return `
    html,body{background:var(--bg)!important}
    body.ui-shell{width:100%!important;max-width:920px!important;margin:0 auto!important;padding:20px 0 36px!important;font-size:14px!important;line-height:1.5!important}
    body.ui-shell .hdr,body.ui-shell .ui-mode-strip,body.ui-shell .fps-bar,body.ui-shell .car-side{display:none!important}
    body.ui-shell .title-help{display:none!important}
    body.ui-shell>.stat-grid{display:none!important}
    body.ui-shell .card.ui-main-card{margin:0 14px 14px!important;padding:0!important;border:1px solid var(--bd)!important;border-radius:17px!important;background:var(--card)!important;box-shadow:0 10px 28px rgba(35,55,82,.08)!important;overflow:hidden!important}
    [data-theme="dark"] body.ui-shell .card.ui-main-card{box-shadow:0 12px 30px rgba(0,0,0,.25)!important}
    body.ui-shell .ui-main-card>.card-hdr{min-height:72px;margin:0!important;padding:14px 17px!important;display:grid!important;grid-template-columns:minmax(0,1fr) auto 28px!important;gap:10px!important;align-items:center!important;cursor:pointer!important;border-bottom:1px solid transparent!important;background:var(--card)!important}
    body.ui-shell .ui-main-card:not(.collapsed)>.card-hdr{border-bottom-color:var(--bd)!important}
    body.ui-shell .ui-main-card>.card-hdr .card-title{display:flex!important;align-items:center!important;gap:11px!important;min-width:0!important;font-size:17px!important;font-weight:800!important;letter-spacing:0!important;text-transform:none!important;color:var(--tx)!important}
    body.ui-shell .ui-card-icon{width:38px;height:38px;border-radius:12px;display:inline-flex;align-items:center;justify-content:center;flex:0 0 38px;border:1px solid transparent}
    body.ui-shell .ui-card-icon svg{width:20px;height:20px;display:block;fill:none;stroke:currentColor;stroke-width:2;stroke-linecap:round;stroke-linejoin:round}
    body.ui-shell [data-ui-kind="nag"] .ui-card-icon{color:#12a56f;background:rgba(18,165,111,.1);border-color:rgba(18,165,111,.18)}
    body.ui-shell [data-ui-kind="wifi"] .ui-card-icon{color:#3478f6;background:rgba(52,120,246,.1);border-color:rgba(52,120,246,.18)}
    body.ui-shell [data-ui-kind="system"] .ui-card-icon{color:#7c63e6;background:rgba(124,99,230,.1);border-color:rgba(124,99,230,.18)}
    body.ui-shell [data-ui-kind="firmware"] .ui-card-icon{color:#e09025;background:rgba(224,144,37,.11);border-color:rgba(224,144,37,.2)}
    body.ui-shell .ui-main-card>.card-hdr .card-meta{font-size:11px!important;color:var(--tx3)!important;white-space:nowrap!important;overflow:hidden!important;text-overflow:ellipsis!important;max-width:250px!important}
    body.ui-shell .ui-chevron{width:28px;height:28px;border:0;background:transparent;color:var(--tx3);font-size:18px;line-height:1;transition:transform .18s ease;cursor:pointer}
    body.ui-shell .ui-main-card:not(.collapsed) .ui-chevron{transform:rotate(180deg)}
    body.ui-shell .ui-main-card.collapsed>:not(.card-hdr){display:none!important}
    body.ui-shell .card-min-btn,body.ui-shell .subsec-btn{display:none!important}
    body.ui-shell .subsec{margin:12px 16px!important;padding:15px!important;border:1px solid var(--bd)!important;border-radius:14px!important;background:var(--bg2)!important}
    body.ui-shell .subsec:first-of-type{margin-top:16px!important}
    body.ui-shell .subsec:last-child{margin-bottom:16px!important}
    body.ui-shell .subsec-head{margin:0 0 12px!important;padding:0 0 10px!important;border-bottom:1px solid var(--bd)!important;display:grid!important;grid-template-columns:minmax(0,1fr) auto!important;gap:8px!important;align-items:center!important}
    body.ui-shell .subsec-title{font-size:15px!important;font-weight:750!important;color:var(--tx)!important;word-break:normal!important}
    body.ui-shell .subsec-meta{font-size:10px!important;color:var(--tx3)!important;max-width:260px!important;overflow:hidden!important;text-overflow:ellipsis!important;white-space:nowrap!important}
    body.ui-shell .subsec.collapsed .subsec-body{display:block!important}
    body.ui-shell #status-panel{margin:16px!important;grid-template-columns:repeat(3,minmax(0,1fr))!important;gap:9px!important}
    body.ui-shell #status-panel .stat,body.ui-shell #status-panel>.btn{border-radius:12px!important;background:var(--bg2)!important;border:1px solid var(--bd)!important;box-shadow:none!important}
    body.ui-shell #system-card>.sys-grid{margin:0 16px 16px!important}
    body.ui-shell .sys-item{border-radius:12px!important;background:var(--bg2)!important}
    body.ui-shell #firmware-update-card>.sys-grid{margin:16px 16px 12px!important}
    body.ui-shell #firmware-update-card>div:last-child{margin:0 16px 16px!important}
    body.ui-shell .ota-drop{border-radius:14px!important;background:var(--bg2)!important}
    body.ui-shell .sniff-input,body.ui-shell .sniff-btn,body.ui-shell .btn,body.ui-shell .hw-btn{border-radius:10px!important}
    body.ui-shell #ota-reset-btn{display:none!important}
    body.ui-shell .warn-bar{margin:2px 14px 16px!important;border-radius:12px!important}
    body.ui-shell #config-card #config-hardware-section{border-top:0!important}
    body.ui-shell #config-card #config-hardware-section>.subsec-head{padding-top:0!important}
    body.ui-shell .sys-monitor{max-width:280px!important}
    @media(max-width:620px){
      body.ui-shell{padding-top:12px!important}
      body.ui-shell .card.ui-main-card{margin-left:10px!important;margin-right:10px!important;border-radius:15px!important}
      body.ui-shell .ui-main-card>.card-hdr{min-height:64px;padding:12px 13px!important;grid-template-columns:minmax(0,1fr) auto 24px!important}
      body.ui-shell .ui-main-card>.card-hdr .card-title{font-size:15px!important;gap:9px!important}
      body.ui-shell .ui-card-icon{width:34px;height:34px;flex-basis:34px;border-radius:10px}
      body.ui-shell .ui-card-icon svg{width:18px;height:18px}
      body.ui-shell .ui-main-card>.card-hdr .card-meta:not(.sys-monitor){display:none!important}
      body.ui-shell .sys-monitor span{display:none!important}
      body.ui-shell .subsec{margin:10px 11px!important;padding:13px!important}
      body.ui-shell #status-panel{margin:12px!important;grid-template-columns:repeat(2,minmax(0,1fr))!important}
      body.ui-shell #system-card>.sys-grid,body.ui-shell #firmware-update-card>.sys-grid{margin-left:11px!important;margin-right:11px!important}
      body.ui-shell #firmware-update-card>div:last-child{margin-left:11px!important;margin-right:11px!important}
      body.ui-shell #wifi-hotspot-section .subsec-body>div[style*="display:flex"],
      body.ui-shell #wifi-internet-section .subsec-body>div[style*="display:flex"]{flex-wrap:wrap!important}
    }
  `;}

  function setSubsectionTitle(section,text){
    if(!section)return;
    section.classList.remove('collapsed');
    section.querySelectorAll('.subsec-btn').forEach(el=>el.remove());
    const title=section.querySelector('.subsec-title');
    if(title)title.textContent=text;
  }

  function buildCardHeader(doc,card,kind,title,metaText){
    card.classList.add('ui-main-card','collapsed');
    card.dataset.uiKind=kind;
    let header=card.querySelector(':scope > .card-hdr');
    if(!header){header=doc.createElement('div');header.className='card-hdr';card.prepend(header);}
    header.querySelectorAll('.card-min-btn,.ui-chevron').forEach(el=>el.remove());
    let meta=header.querySelector('.card-meta');
    if(!meta){meta=doc.createElement('div');meta.className='card-meta';header.appendChild(meta);}
    if(metaText!==null)meta.textContent=metaText||'';
    let titleNode=header.querySelector('.card-title');
    if(!titleNode){titleNode=doc.createElement('div');titleNode.className='card-title';header.prepend(titleNode);}
    titleNode.innerHTML='<span class="ui-card-icon">'+cardIcon(kind)+'</span><span>'+title+'</span>';
    const chevron=doc.createElement('button');
    chevron.type='button';chevron.className='ui-chevron';chevron.setAttribute('aria-label','展开或收起');chevron.textContent='⌄';
    header.appendChild(chevron);
    return header;
  }

  function installAccordion(doc,cards){
    cards.forEach(card=>{
      card.classList.add('collapsed');
      const header=card.querySelector(':scope > .card-hdr');
      if(!header||header.dataset.uiAccordion==='1')return;
      header.dataset.uiAccordion='1';
      header.addEventListener('click',ev=>{
        if(ev.target.closest('.sys-monitor input,.sys-monitor label'))return;
        accordionTouched=true;
        const shouldOpen=card.classList.contains('collapsed');
        cards.forEach(item=>item.classList.add('collapsed'));
        if(shouldOpen)card.classList.remove('collapsed');
      });
    });
  }

  function installPasswordlessOta(win,doc){
    try{win.localStorage.removeItem('otaU');win.localStorage.removeItem('otaP');}catch(e){}
    const reset=doc.getElementById('ota-reset-btn');if(reset)reset.remove();
    win.uploadFirmware=function(){
      const input=doc.getElementById('ota-file');
      const file=input&&input.files&&input.files[0];
      if(!file)return;
      const progress=doc.getElementById('ota-progress');
      const fill=doc.getElementById('ota-fill');
      const status=doc.getElementById('ota-status');
      const button=doc.getElementById('ota-upload-btn');
      if(progress)progress.style.display='block';
      if(button){button.disabled=true;button.textContent='刷写中…';}
      const pad=value=>String(value).padStart(2,'0');
      const now=new Date();
      const stamp=now.getFullYear()+'-'+pad(now.getMonth()+1)+'-'+pad(now.getDate())+' '+pad(now.getHours())+':'+pad(now.getMinutes())+':'+pad(now.getSeconds());
      const xhr=new win.XMLHttpRequest();
      xhr.upload.onprogress=event=>{
        if(!event.lengthComputable)return;
        const percent=Math.round(event.loaded/event.total*100);
        if(fill)fill.style.width=percent+'%';
        if(status)status.textContent='上传中… '+percent+'%';
      };
      xhr.onload=()=>{
        if(xhr.status===200){
          if(status)status.textContent='完成，设备正在重启…';
          if(fill)fill.style.width='100%';
          setTimeout(()=>win.location.reload(),5000);
        }else if(status){status.textContent='上传失败：'+xhr.status;status.style.color='var(--err)';}
        if(button){button.disabled=false;button.textContent='刷写固件';}
      };
      xhr.onerror=()=>{
        if(status){status.textContent='连接错误';status.style.color='var(--err)';}
        if(button){button.disabled=false;button.textContent='刷写固件';}
      };
      xhr.open('POST','/update?ota_time='+encodeURIComponent(stamp),true);
      xhr.setRequestHeader('Content-Type','application/octet-stream');
      xhr.setRequestHeader('X-File-Name',file.name);
      xhr.setRequestHeader('X-File-Size',String(file.size));
      xhr.send(file);
    };
  }

  function patchDashboard(){
    let doc,win;
    try{doc=frame.contentDocument;win=frame.contentWindow;}catch(e){return;}
    if(!doc||!doc.body||!win)return;
    if(doc.documentElement)doc.documentElement.setAttribute('data-theme',theme);
    try{win.localStorage.setItem('themeMode','manual');win.localStorage.setItem('theme',theme);}catch(e){}
    if(typeof win.applyTheme==='function')try{win.applyTheme(theme,false);}catch(e){}
    try{
      win.expandWifiNagDefaults=function(){};
      win.expandCarEssentials=function(){};
      if(typeof win.setCollapsedPanel==='function'&&!win.__wifiNagCollapsePatched){
        const originalSetCollapsedPanel=win.setCollapsedPanel;
        win.setCollapsedPanel=function(element,collapsed,persist){
          if(element&&element.classList&&element.classList.contains('ui-main-card'))return;
          return originalSetCollapsedPanel(element,collapsed,persist);
        };
        win.__wifiNagCollapsePatched=true;
      }
    }catch(e){}

    let style=doc.getElementById('wifi-nag-shell-style');
    if(!style){style=doc.createElement('style');style.id='wifi-nag-shell-style';style.textContent=iframePatchCss();doc.head.appendChild(style);}
    doc.body.classList.remove('ui-car');
    doc.body.classList.add('ui-phone','wifi-nag','ui-shell');

    const config=doc.getElementById('config-card');
    const system=doc.getElementById('system-card');
    const firmware=doc.getElementById('firmware-update-card');
    const nag=doc.getElementById('config-hardware-section');
    const hotspot=doc.getElementById('wifi-hotspot-section');
    const internet=doc.getElementById('wifi-internet-section');
    const gateway=doc.getElementById('gateway-section');
    const debug=doc.querySelector('[data-subkey="config-dashboard-log"]');
    const status=doc.getElementById('status-panel');
    const warning=doc.querySelector('.warn-bar');
    if(!config||!system||!firmware||!nag||!hotspot||!internet||!gateway)return;

    setSubsectionTitle(nag,'NAG/CAN 写入');
    setSubsectionTitle(hotspot,'Wi-Fi 热点');
    setSubsectionTitle(internet,'Wi-Fi 上网');
    setSubsectionTitle(gateway,'SPA-AP 网关');
    if(debug)setSubsectionTitle(debug,'调试日志');

    let wifiCard=doc.getElementById('wifi-config-card');
    if(!wifiCard){wifiCard=doc.createElement('div');wifiCard.id='wifi-config-card';wifiCard.className='card';}
    [hotspot,internet,gateway].forEach(section=>wifiCard.appendChild(section));
    if(debug)system.appendChild(debug);
    if(status){
      status.classList.add('embedded-status-grid');
      const sysGrid=system.querySelector(':scope > .sys-grid');
      system.insertBefore(status,sysGrid||system.children[1]||null);
    }

    buildCardHeader(doc,config,'nag','NAG 配置','NAG/CAN 写入');
    buildCardHeader(doc,wifiCard,'wifi','Wi-Fi 配置','热点 · 上网 · 网关');
    buildCardHeader(doc,system,'system','系统状态',null);
    buildCardHeader(doc,firmware,'firmware','固件更新',null);

    const parent=config.parentNode;
    const before=warning||null;
    [config,wifiCard,system,firmware].forEach(card=>parent.insertBefore(card,before));
    const cards=[config,wifiCard,system,firmware];
    installAccordion(doc,cards);
    if(!accordionTouched)cards.forEach(card=>card.classList.add('collapsed'));

    const apSsid=doc.getElementById('ap-ssid');
    if(apSsid&&!apSsid.value)apSsid.value='T1CAN';
    const apPass=doc.getElementById('ap-pass');
    if(apPass)apPass.placeholder='默认密码：12345678（留空保持当前密码）';
    installPasswordlessOta(win,doc);
    doc.querySelectorAll('.subsec').forEach(sec=>{sec.classList.remove('collapsed');sec.querySelectorAll('.subsec-btn').forEach(el=>el.remove());});
    doc.querySelectorAll('.card-min-btn').forEach(el=>el.remove());
  }

  frame.addEventListener('load',()=>{
    patchDashboard();
    setTimeout(patchDashboard,250);
    setTimeout(patchDashboard,1000);
    setTimeout(patchDashboard,2500);
    setTimeout(patchDashboard,6000);
  });

  applyTheme(theme);
})();
</script>
</body>
</html>
)HTML";

static void handleWifiNagUiShell()
{
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server.sendHeader("Pragma", "no-cache");
    server.send_P(200, "text/html", WIFI_NAG_UI_SHELL);
}

static void wifiNagDashboardSetup(CarManagerBase *handler, CanDriver *driver)
{
    // Register first: the compatibility WebServer resolves the earliest matching route.
    server.on("/", HTTP_GET, handleWifiNagUiShell);
    server.on("/legacy-dashboard", HTTP_GET, handleRoot);
    mcpDashboardSetup(handler, driver);
}

#endif
