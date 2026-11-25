/* Author: Malcolm Liew Jia En (ML). Dashboard web UI. Revised 2026-11-25. */
// Dashboard page served by the pillbox. Edit here, then run: python web/build_web.py
// Kept out of the .ino so the Arduino prototype scanner never parses the page text.
#ifndef PHARMAVAULT_DASHBOARD_HTML_H
#define PHARMAVAULT_DASHBOARD_HTML_H
#include <pgmspace.h>
static const char DashboardHtml[] PROGMEM = R"PVHTML(
<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="color-scheme" content="light only">
<title>PharmaVault · Daily care</title>
<style>
/* Shapes: cards and tiles 28px, inputs 16px, buttons, badges and nav are full pills. */
:root{--bg:#c99f62;--bg2:#e6dcc5;--card:#ebe2cc;--ink:#17141b;--muted:#4b3f33;--line:#c4b48f;--accent:#4f8f93;--accent-soft:#d9cfa8;--cta:#e8861b;--cta-ink:#17141b;--peach:#e8861b;--sky:#4f8f93;--butter:#bdb184;--warn-bg:#ebe2cc;--warn-ink:#8a2f2a;--err-bg:#ebe2cc;--err-ink:#a3302a;--ring:#e8861b;--khaki:#c7bd8e;--maroon:#7d3434;--coral:#e0675a;--night:#16141c;--shadow:4px 4px 0 rgb(23 20 27/.85);--display:"DIN Condensed","Avenir Next Condensed","Bahnschrift Condensed","Roboto Condensed","sans-serif-condensed","Arial Narrow",sans-serif}
*{box-sizing:border-box}[hidden]{display:none!important}
body{margin:0;font:16px/1.5 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;color:var(--ink);background:linear-gradient(180deg,var(--bg) 0,var(--bg2) 420px) fixed;min-height:100dvh;-webkit-font-smoothing:antialiased}
h1,h2,h3,p{margin-top:0}
h1{font-size:clamp(2rem,5vw,2.9rem);font-weight:800;letter-spacing:-.04em;line-height:1.08;margin-bottom:10px}
h2{font-size:1.2rem;font-weight:750;letter-spacing:-.02em;margin-bottom:12px}
p{margin-bottom:14px}a{color:var(--accent)}
small,.quiet,.note{color:var(--muted);font-size:.84rem}small{display:block;line-height:1.6}
.eyebrow{font-size:.78rem;font-weight:650;color:var(--muted);margin-bottom:8px}
button,input,select{font:inherit;color:inherit}
button{border:0;border-radius:999px;background:var(--cta);color:var(--cta-ink);padding:12px 22px;font-size:.9rem;font-weight:700;cursor:pointer;min-height:46px;transition:transform .15s ease,opacity .15s ease}
button:active:not(:disabled){transform:scale(.97)}
button:disabled{opacity:.38;cursor:not-allowed}
.secondary{background:var(--accent-soft);color:var(--ink)}
.ghost{background:transparent;color:var(--ink);box-shadow:inset 0 0 0 1.5px var(--line)}
button:focus-visible,a:focus-visible,input:focus-visible,select:focus-visible,summary:focus-visible{outline:3px solid var(--ring);outline-offset:3px}
.shell{max-width:1080px;padding:0 24px;margin:auto}
.topbar{padding-top:18px}
.toprow{display:flex;align-items:center;justify-content:space-between;gap:16px;min-height:64px}
.brand{display:flex;align-items:center;gap:12px;font-weight:800;font-size:1.1rem;letter-spacing:-.03em}
.brand small{font-size:.75rem;font-weight:500;letter-spacing:0;color:var(--muted)}
.brand-mark{display:grid;place-items:center;width:44px;height:44px;border-radius:999px;background:var(--cta);color:var(--cta-ink);font-size:.8rem;font-weight:800}
.connection{white-space:nowrap;display:flex;align-items:center;gap:8px;font-size:.78rem;font-weight:600;color:var(--muted);background:var(--card);border-radius:999px;padding:8px 14px;box-shadow:var(--shadow)}
.connection:before{content:"";width:8px;height:8px;border-radius:50%;background:#a3aaa0}
.connection[data-online=true]:before{background:#3fa35a}
.nav{position:fixed;left:50%;bottom:18px;transform:translateX(-50%);z-index:10;display:flex;gap:6px;padding:6px;border-radius:999px;background:var(--cta);box-shadow:0 14px 40px rgb(20 40 26/.28)}
.nav button{background:transparent;color:var(--cta-ink);opacity:.7;min-width:128px;padding:12px 22px}
.nav button[aria-selected=true]{background:var(--cta-ink);color:var(--cta);opacity:1}
.top-status{display:flex;flex-wrap:wrap;justify-content:flex-end;align-items:center;gap:8px}.nav-note{white-space:nowrap;font-size:.78rem;font-weight:700;border-radius:999px;background:var(--warn-bg);color:var(--warn-ink);padding:8px 14px;box-shadow:var(--shadow)}
.workspace{padding-top:14px;padding-bottom:120px}
.global-notice:empty,.message:empty{display:none}
.global-notice{background:var(--accent-soft);color:var(--ink)}.global-notice,.alert{padding:13px 18px;border-radius:16px;margin-bottom:12px;font-size:.88rem}
.alert{background:var(--warn-bg);color:var(--warn-ink)}.error{background:var(--err-bg);color:var(--err-ink)}.env-alert{background:var(--err-bg);color:var(--err-ink);font-weight:700;font-size:.98rem;padding:14px 14px 14px 20px;display:flex;align-items:center;justify-content:space-between;gap:14px}.env-alert button{flex-shrink:0;background:var(--err-ink);color:#fff}
.test-notice{display:flex;gap:16px;align-items:center;justify-content:space-between;background:var(--cta);color:var(--cta-ink);border-radius:28px;padding:14px 16px 14px 22px;margin-bottom:16px}
.test-notice p{margin:0;font-size:.86rem}.test-notice button{flex-shrink:0;background:var(--cta-ink);color:var(--cta)}
.intro{display:flex;justify-content:space-between;align-items:flex-end;gap:20px;margin:18px 0 24px}
.intro p{margin-bottom:0;color:var(--ink)}
.badge{display:inline-flex;align-items:center;gap:6px;border-radius:999px;padding:7px 13px;background:var(--accent-soft);font-size:.76rem;font-weight:700;white-space:nowrap;color:var(--ink)}
.panel{background:var(--card);border-radius:28px;padding:26px;min-width:0;box-shadow:var(--shadow)}
.hero{position:relative;overflow:hidden;background:linear-gradient(135deg,var(--accent-soft),var(--card) 75%);display:flex;flex-direction:column;gap:10px}
.hero>*{position:relative}.hero{min-height:340px}.mascot{position:absolute!important;right:34px;top:20px;height:232px;width:auto;pointer-events:none;filter:drop-shadow(0 10px 18px rgb(40 70 46/.18))}.hero-copy{max-width:calc(100% - 270px)}

.hero-title{font-size:clamp(1.9rem,4.4vw,3rem);font-weight:800;letter-spacing:-.045em;line-height:1.06;margin:4px 0 12px;max-width:16ch}
.hero p{max-width:46ch;color:var(--muted)}
.hero-bottom{margin-top:auto!important;display:flex;align-items:center;justify-content:space-between;gap:14px;padding-top:16px;margin-top:8px;border-top:1px solid var(--line)}
.sensor-label{font-size:.76rem;color:var(--muted)}.sensor-value{font-weight:750;font-size:1rem}
.cards{display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1.35fr);gap:16px;margin-top:16px}.env{padding:20px 22px}.env-pair{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-top:6px}.env-pair span{font-size:.78rem;opacity:.7}.env-pair strong{display:block;font-size:1.6rem;margin:2px 0 0}.env p{margin:8px 0 0;font-size:.78rem}
.stat{display:flex;flex-direction:column;gap:2px}
.stat strong{font-size:2.3rem;font-weight:800;letter-spacing:-.05em;line-height:1.1;margin:6px 0 2px}
.stat-label{font-size:.86rem;font-weight:700}
.stat .quiet{color:var(--ink);opacity:.62}
.tint-peach{background:var(--peach)}.tint-sky{background:var(--sky)}.tint-butter{background:var(--butter)}
.row{display:flex;gap:10px;flex-wrap:wrap;align-items:center}
.split,.section-head{display:flex;align-items:flex-start;justify-content:space-between;gap:12px}
.time-row,.field-grid{display:grid;grid-template-columns:1fr 1fr;gap:14px}
.time-row{margin-top:12px}.time-row span{font-size:.78rem;opacity:.7}
.time-row strong{display:block;font-size:1.6rem;font-weight:800;letter-spacing:-.04em}
.env-line{margin:12px 6px 0}
.full-row{margin-top:16px}
.grid{display:grid;grid-template-columns:minmax(0,1.55fr) minmax(260px,1fr);gap:16px}
.week-dots{display:grid;grid-template-columns:repeat(7,1fr);gap:10px;margin:8px 0 16px}
.week-dot{display:grid;place-items:center;aspect-ratio:1;max-width:58px;width:100%;justify-self:center;border-radius:999px;background:var(--bg);color:var(--muted);font-weight:750;font-size:.95rem}
.week-dot[data-done=true]{background:var(--accent);color:var(--cta-ink)}
.week-dot[data-missed=true]{background:var(--peach);color:var(--warn-ink)}
.week-dot[data-current=true]{box-shadow:0 0 0 3px var(--ring) inset}
#weekProgress{font-size:1rem;font-weight:650;color:var(--muted);margin:-6px 0 0}.field-grid label{margin:4px 0}
details{margin:14px 0 0;border-top:1px solid var(--line);padding-top:14px}
summary{font-size:.9rem;font-weight:700;cursor:pointer;list-style-position:outside}
details>p:first-of-type{margin-top:14px}details p,details small,details ol{font-size:.86rem}
ol{padding-left:22px;font-size:.9rem}li{margin:8px 0}
#dailyLog{list-style:none;padding:0;margin:16px 0 0;display:grid;gap:8px}
#dailyLog li{margin:0;padding:12px 16px;border-radius:16px;background:var(--bg2);font-size:.84rem;color:var(--muted)}
.step-list{list-style:none;padding:0;margin:18px 0;counter-reset:steps}
.step-list li{display:flex;gap:12px;align-items:flex-start;margin:14px 0;font-size:.9rem;counter-increment:steps}
.step-list li:before{content:counter(steps);display:grid;place-items:center;flex-shrink:0;width:28px;height:28px;border-radius:999px;background:var(--accent-soft);font-size:.76rem;font-weight:800}
.check{display:flex;align-items:flex-start;gap:10px;padding:13px 16px;border-radius:16px;background:var(--bg2);font-size:.86rem;margin:10px 0}
.check input{margin:3px 0 0;width:18px;height:18px;flex-shrink:0;accent-color:var(--accent)}
label{display:block;font-size:.86rem;font-weight:600;margin:14px 0 7px}
input,select{border:1.5px solid var(--line);border-radius:16px;background:var(--card);padding:11px 14px;max-width:100%;min-height:46px}
input:disabled,select:disabled{opacity:.55}
input[type=number]{width:120px}
input[type=text],input[type=password],input[type=time]{display:block;width:100%;margin-top:6px}
.message{font-size:.86rem;color:var(--ink);white-space:pre-wrap;padding:12px 16px;background:var(--accent-soft);border-radius:16px;margin:12px 0 0;overflow-wrap:anywhere}
.status{font-size:.92rem;overflow-wrap:anywhere}
#tag{overflow-wrap:anywhere;font:.84rem ui-monospace,monospace}
.footer{margin-top:28px;padding-top:16px;font-size:.76rem;color:var(--muted);display:flex;justify-content:space-between;gap:14px}
.text-link{background:none;padding:0;min-height:32px;font-size:.86rem;color:var(--accent);border-radius:0;text-decoration:underline;text-underline-offset:4px}
.section-anchor{scroll-margin-top:18px}
.jump{display:flex;flex-wrap:wrap;gap:8px;margin:-8px 0 22px}
.jump a{background:var(--card);border-radius:999px;padding:9px 16px;font-size:.84rem;font-weight:650;color:var(--ink);text-decoration:none;box-shadow:var(--shadow)}
.group-title{margin:34px 0 4px;font-size:1.4rem;font-weight:800;letter-spacing:-.03em}.group-note{margin-bottom:16px}
#alignmentSection{align-self:start}
body{background:radial-gradient(ellipse at 15% 20%,rgb(255 255 230/.18),transparent 45%),radial-gradient(ellipse at 85% 75%,rgb(40 50 10/.16),transparent 50%),radial-gradient(ellipse at 60% 10%,rgb(40 50 10/.10),transparent 35%),radial-gradient(rgb(30 35 10/.08) 1px,transparent 1.4px) 0 0/9px 9px,var(--bg)}h1,.hero-title,.group-title,.brand{font-family:var(--display);font-weight:900;letter-spacing:-.01em}h1,.hero-title{text-shadow:3px 3px 0 var(--cta)}h2{font-family:var(--display);font-weight:900}.panel,.connection,.jump a{border:3px solid var(--ink);box-shadow:var(--shadow)}.panel{border-radius:24px}.global-notice,.alert,.message,.check,#dailyLog li{border:2px solid var(--ink)}.badge,.nav-note{border:2px solid var(--ink);background:var(--card)}button{border:3px solid var(--ink);box-shadow:3px 3px 0 var(--ink);font-family:var(--display);font-weight:900;letter-spacing:.01em}button:active:not(:disabled){transform:translate(3px,3px);box-shadow:0 0 0 var(--ink)}.secondary{background:var(--accent-soft);color:var(--ink)}.ghost{background:var(--card);color:var(--ink);box-shadow:3px 3px 0 var(--ink)}.text-link{border:0;box-shadow:none;font-family:inherit}input,select{border:2px solid var(--ink);background:#fffaf0}.hero{background:radial-gradient(ellipse at 50% 45%,#f6eedb 0,#e9ddc8 60%,#d8bf8e 100%);border-radius:34px;box-shadow:var(--shadow),inset 0 0 40px rgb(90 60 10/.28)}body:after{content:"";position:fixed;inset:0;z-index:50;pointer-events:none;background:repeating-linear-gradient(0deg,rgb(20 16 30/.07) 0 1px,transparent 1px 3px),radial-gradient(ellipse at center,transparent 58%,rgb(30 25 10/.32) 100%)}.hero .badge{background:var(--cta)}.mascot{filter:drop-shadow(4px 4px 0 rgb(26 20 36/.35))}.tint-peach{background:var(--peach)}.tint-butter{background:var(--card)}.stat .quiet,.time-row span,.env-pair span{opacity:.8}.brand-mark{background:var(--cta);color:var(--cta-ink);border:3px solid var(--ink);box-shadow:3px 3px 0 var(--ink)}.connection{background:var(--card);color:var(--ink)}.nav{background:var(--ink);border:3px solid var(--ink);box-shadow:5px 5px 0 rgb(26 20 36/.35)}.nav button{border:0;box-shadow:none;color:var(--card);opacity:.85}.nav button[aria-selected=true]{background:var(--cta);color:var(--cta-ink);opacity:1;border:2px solid var(--card)}.week-dot{border:2px solid var(--ink);background:var(--card);color:var(--ink)}.week-dot[data-done=true]{background:var(--accent);color:#f3ead6}#weekPanel{padding:16px 20px}#weekPanel h2{font-size:1.05rem;margin-bottom:2px}#weekPanel .week-dots{gap:6px;margin:6px 0 8px}#weekPanel .week-dot{max-width:38px;font-size:.8rem;padding:0}#weekPanel #weekSummary{font-size:.8rem}#weekDates{display:none}#weekPanel details{margin-top:8px;padding-top:8px}.week-dot[data-missed=true]{background:#c4532b;color:#f3ead6}.week-dot[data-current=true]{box-shadow:0 0 0 4px var(--cta),0 0 0 6px var(--ink)}.step-list li:before{background:var(--butter);border:2px solid var(--ink)}.env-alert button{background:var(--card);color:var(--err-ink)}body{background:radial-gradient(ellipse at 12% 18%,rgb(255 240 200/.22),transparent 42%),radial-gradient(ellipse at 88% 72%,rgb(90 55 20/.20),transparent 48%),radial-gradient(ellipse at 55% 8%,rgb(90 55 20/.12),transparent 35%),radial-gradient(rgb(70 40 10/.10) 1px,transparent 1.5px) 0 0/7px 7px,var(--bg)}h1,h2,.hero-title,.group-title,.stat-label,button,.badge,.nav-note,.jump a,summary,.connection{font-family:var(--display);font-stretch:condensed;text-transform:uppercase;letter-spacing:.04em}h1,.hero-title{text-shadow:none;font-weight:800;letter-spacing:.02em}h1{font-size:clamp(2.2rem,6vw,3.4rem)}h2{font-weight:800;font-size:1.25rem}.brand{font-family:var(--display);text-transform:uppercase;letter-spacing:.06em}.panel,.connection,.jump a{border-width:2px}.hero{background:var(--night);color:var(--khaki);border-color:var(--night)}.hero .hero-title{color:var(--khaki)}.hero p,.hero .sensor-label{color:#d9d0b4}.hero .sensor-value{color:var(--bg2)}.hero-bottom{border-top:0;padding-top:36px;background:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='240' height='60'%3E%3Cpath d='M0 30 C40 0 80 0 120 30 S200 60 240 30' fill='none' stroke='%234f8f93' stroke-width='9'/%3E%3C/svg%3E") repeat-x 0 0/120px 30px}.hero .badge{background:var(--cta);color:var(--cta-ink);border-color:var(--cta)}.mascot{filter:drop-shadow(3px 3px 0 var(--accent))}.hero button{background:var(--khaki);color:var(--night);border-color:var(--khaki);box-shadow:3px 3px 0 var(--accent)}.tint-peach{background:var(--cta)}.tint-butter{background:var(--butter)}#weekPanel{background:linear-gradient(rgb(235 226 204/.55) 1px,transparent 1px) 0 0/18px 18px,linear-gradient(90deg,rgb(235 226 204/.55) 1px,transparent 1px) 0 0/18px 18px,#d79b54}#weekPanel .quiet,#weekPanel summary{color:var(--ink)}.week-dot{background:var(--bg2)}.week-dot[data-done=true]{background:var(--accent);color:var(--bg2)}.week-dot[data-missed=true]{background:#c8321e;color:var(--bg2)}.week-dot[data-current=true]{box-shadow:0 0 0 3px var(--night),0 0 0 6px var(--cta)}.env-alert,.alert{background:var(--bg2);color:var(--coral);border:0;border-top:8px solid var(--maroon);border-bottom:8px solid var(--maroon);border-radius:4px;font-family:var(--display);font-stretch:condensed;text-transform:uppercase;letter-spacing:.04em;font-size:1.05rem}.env-alert button{background:var(--maroon);color:var(--bg2);border-color:var(--maroon)}.global-notice{background:var(--bg2);color:var(--ink);border:2px solid var(--ink)}button{font-weight:800;border-width:2px;box-shadow:3px 3px 0 var(--ink)}.secondary{background:var(--khaki);color:var(--ink)}.ghost{background:var(--bg2)}.brand-mark{background:var(--night);color:var(--khaki);border-color:var(--night)}.nav{background:var(--night);border-color:var(--night)}.nav button{color:var(--khaki)}.nav button[aria-selected=true]{background:var(--cta);color:var(--night);border-color:var(--cta)}.step-list li:before{background:var(--khaki)}@media(max-width:760px){.shell{padding-left:16px;padding-right:16px}.grid{grid-template-columns:1fr}.cards{grid-template-columns:1fr;gap:12px}.stat strong{font-size:1.9rem}.env-pair strong{font-size:1.5rem}.panel{padding:22px;border-radius:24px}.intro{flex-direction:column;align-items:flex-start;gap:10px;margin-top:10px}.nav button{min-width:118px}.footer{flex-direction:column;gap:3px}.brand small{display:none}.hero{min-height:0}.mascot{position:static!important;height:170px;align-self:center;margin:-4px 0 2px}.hero-copy{max-width:none}.week-dots{gap:6px}}
@media(max-width:440px){.env-alert{flex-direction:column;align-items:flex-start}.toprow:has(.nav-note:not([hidden])) .brand>div{display:none}.nav-note,.connection{padding:8px 12px}}@media(prefers-reduced-motion:reduce){*{transition:none!important;scroll-behavior:auto!important}}
</style></head><body>
<header class="topbar"><div class="shell"><div class="toprow"><div class="brand"><span class="brand-mark" aria-hidden="true">PV</span><div>PharmaVault<small>A little care. Every day.</small></div></div><div class="top-status"><span id="refillDot" class="nav-note" hidden>Refill due</span><span id="testDot" class="nav-note" hidden>Test on</span><span id="connectionState" class="connection" role="status">Connecting</span></div></div>
</div></header>
<nav class="nav" role="tablist" aria-label="Dashboard views"><button id="navToday" role="tab" aria-controls="viewToday" aria-selected="true" tabindex="0">Dashboard</button><button id="navSettings" role="tab" aria-controls="viewSettings" aria-selected="false" tabindex="-1">Settings</button></nav>
<main class="shell workspace">
<div id="connectionNotice" class="global-notice error" role="alert" hidden></div><p id="bridgeStatus" class="global-notice" role="status">Checking healthcare-station link...</p><p id="motionNotice" class="global-notice" role="status" hidden></p><p id="storage" class="global-notice error" role="alert"></p>
<div id="globalTest" class="test-notice" hidden><p id="irTestBanner" role="alert" hidden></p><button id="quickStopTest" type="button" disabled>Stop test</button></div>
<div id="envAlert" class="alert env-alert" role="alert" hidden><span id="envAlertText"></span><button id="envAck" type="button">Got it</button></div><p id="overdueAlert" class="alert" role="alert" hidden></p><p id="weekRefillAlert" class="alert" role="alert" hidden></p><p id="globalActionMessage" class="message" role="status"></p>

<div id="viewToday" role="tabpanel" aria-labelledby="navToday">
<div class="intro"><div><p class="eyebrow" id="overviewDate">YOUR DAILY OVERVIEW</p><h1>Welcome back.</h1><p>Here's your pillbox today.</p></div><span id="modeBadge" class="badge">Connecting</span></div>

<section id="todayPanel" class="section-anchor"><div class="panel hero"><img class="mascot" src="/mascot.webp?v=2" alt="" width="380" height="324"><div class="hero-copy"><span class="badge" id="todayMedicine">TODAY'S MEDICINE</span><h2 id="heroTitle" class="hero-title">Connecting to your dispenser.</h2><p id="todayDetail">Your current status will appear here.</p><button id="nextAction" type="button" disabled>View setup</button></div><div class="hero-bottom"><div><div class="sensor-label">Hand sensor</div><div class="sensor-value" id="irSignal">Unknown</div></div><span class="badge" id="todaySlot">-</span></div></div></section>

<div class="cards">
<section class="panel stat env tint-peach"><span class="stat-label">Inside the pillbox</span><div class="env-pair"><div><span>Temperature</span><strong id="temp">-</strong></div><div><span>Humidity</span><strong id="rh">-</strong></div></div><p id="environment" class="quiet" role="status">Waiting for sensor readings.</p></section>
<section class="panel stat timer tint-butter"><div class="split"><span class="stat-label">Daily timer</span><span id="scheduleBadge" class="badge">-</span></div><div class="time-row"><div><span>Dose ready</span><strong id="summaryDue">-</strong></div><div><span>Overdue at</span><strong id="summaryAlert">-</strong></div></div><div class="row" style="margin-top:16px"><button id="manageSchedule" class="ghost" type="button">Change times</button><button id="quickPause" class="secondary" type="button" disabled>Pause</button></div></section>
</div>


<section id="weekPanel" class="panel full-row section-anchor"><div class="section-head" style="margin-bottom:8px"><div><h2>This week</h2><p id="weekProgress">Not started</p></div><span id="weekShort" class="badge">-</span></div>
<div class="week-dots" id="weekDots" aria-label="Seven scheduled days"></div><p id="weekSummary" class="quiet" style="margin:0">Waiting for weekly status.</p><p id="weekDates" class="quiet" style="margin:6px 0 0"></p>
<details id="refillDetails"><summary>Refill the week</summary><p id="weekStatus" class="status" role="status">Waiting for weekly status.</p><ol class="step-list"><li><div><strong>Pause and clear.</strong><br>Remove old or missed medicine first.</div></li><li><div><strong>Park at empty compartment 8.</strong><br>Don't pass loaded compartments across the outlet.</div></li><li><div><strong>Fill compartments 1 to 7.</strong><br>Each day's medicine goes in its own compartment.</div></li></ol><label class="check"><input id="weekFilledConfirm" type="checkbox" disabled><span>Old medicine removed; compartments 1 to 7 hold the right day's medicine.</span></label><label class="check"><input id="weekParkedConfirm" type="checkbox" disabled><span>Empty compartment 8 is at the outlet.</span></label><button id="refillWeek" type="button" disabled style="margin-top:8px">Start the seven-day pack</button><p id="weekMessage" class="message" role="status"></p><p id="weekNextStart" class="quiet" style="margin:16px 0 0"></p></details>
<details><summary>Which compartment is which day</summary><ol id="weekCompartments"></ol></details>
<button id="openRefill" class="text-link" type="button" hidden>Refill the week</button></section>

<div class="grid full-row"><section id="refillInstructionsPanel" class="panel"><h2>Refill notes</h2><p class="quiet">A short note per day. The healthcare station shows these on its LCD when the card is tapped.</p><form id="refillInstructionForm"><div class="field-grid"><label>Day 1<input id="instructionDay1" type="text" maxlength="48" autocomplete="off" disabled></label><label>Day 2<input id="instructionDay2" type="text" maxlength="48" autocomplete="off" disabled></label><label>Day 3<input id="instructionDay3" type="text" maxlength="48" autocomplete="off" disabled></label><label>Day 4<input id="instructionDay4" type="text" maxlength="48" autocomplete="off" disabled></label><label>Day 5<input id="instructionDay5" type="text" maxlength="48" autocomplete="off" disabled></label><label>Day 6<input id="instructionDay6" type="text" maxlength="48" autocomplete="off" disabled></label><label>Day 7<input id="instructionDay7" type="text" maxlength="48" autocomplete="off" disabled></label></div><small>Up to 48 characters each, no double quotes or backslashes. Blank shows "Not set".</small><p id="instructionsStatus" class="quiet" role="status"></p><button id="saveInstructions" type="submit" disabled>Save notes</button></form><p id="instructionsMessage" class="message" role="status"></p></section>
<section class="panel"><h2>NFC card</h2><p class="quiet">Tap the pillbox's card at the healthcare station to see this week's status and notes. A tap never dispenses medicine.</p><small>Last card</small><p id="tag">No card scanned</p><p id="nfcStatus" class="quiet" style="margin-bottom:0"></p></section></div>

<section class="panel full-row"><div class="section-head"><h2>Dose history</h2><span class="quiet">Last eight days</span></div><p id="alertSummary" class="alert" role="alert" hidden style="margin-top:14px"></p><details><summary>Show history and today's details</summary><p id="todayStatus" class="status"></p><p id="lastAction" class="quiet"></p><p id="doseWindow" class="quiet"></p><ol id="dailyLog"></ol><small>"Completed" means the motor finished turning. Taking the pills is not measured.</small></details></section>
</div>

<div id="viewSettings" role="tabpanel" aria-labelledby="navSettings" hidden>
<div class="intro"><div><h1>Settings</h1><p>Pause the schedule before changing settings or handling the carousel.</p></div><span class="badge">Singapore, UTC+8</span></div>
<nav class="jump" aria-label="Settings sections"><a href="#scheduleSection">Clock &amp; timer</a><a href="#alignmentSection">Carousel</a><a href="#irTestPanel">Test mode</a><a href="#limitsSection">Limits</a><a href="#wifiSection">Wi-Fi</a></nav>

<div class="grid"><section class="panel section-anchor" id="scheduleSection"><h2>Clock &amp; daily timer</h2><p id="clockStatus" class="status" role="status"></p><button id="syncClock" type="button" class="ghost" disabled>Sync clock from this device</button><p id="clockMessage" class="message" role="status"></p><div class="field-grid"><label>Dose ready at<input id="scheduleTime" type="time" value="13:00" required></label><label>Overdue alert at<input id="deadlineTime" type="time" value="14:00" required></label></div><small>The alert must be later the same day. Times repeat every day.</small><label class="check"><input id="scheduleConfirm" type="checkbox"><span>I checked the date, times, carousel position and hand sensor.</span></label><div class="row" style="margin-top:14px"><button id="saveSchedule" type="button" class="secondary" disabled>Save times</button><button id="enableSchedule" type="button" disabled>Enable schedule</button><button id="pauseSchedule" type="button" class="ghost" disabled>Pause schedule</button></div><p id="scheduleStatus" class="quiet" style="margin-top:16px"></p><p id="scheduleMessage" class="message" role="status"></p><details><summary>After a restart or a missed day</summary><p>After a restart, sync the clock, confirm the carousel position, then enable. Times and past doses stay saved.</p><p>If a day was missed, remove its medicine before aligning the compartment before today's. Don't rotate past a loaded missed compartment.</p><p>At the dose time the dose becomes ready without moving. A fresh hand turns the motor. An overdue dose stays available until midnight. A failed attempt never retries by itself.</p></details></section>

<section class="panel section-anchor" id="alignmentSection"><h2>Carousel</h2><details id="alignmentDetails"><summary>Confirm which compartment is at the outlet</summary><p id="alignmentStatus" class="status" role="status"></p><label>Compartment at the outlet now<select id="alignedSlot"><option value="">Choose after checking</option><option>1</option><option>2</option><option>3</option><option>4</option><option>5</option><option>6</option><option>7</option><option>8</option></select></label><label class="check"><input id="alignmentConfirm" type="checkbox"><span>I removed any missed medicine and checked the position, without moving loaded missed compartments past the outlet.</span></label><button id="confirmAlignment" type="button" disabled>Confirm position</button><p id="alignmentMessage" class="message" role="status"></p><small>Before day N, align compartment N−1 (empty 8 before day 1). This does not move the motor.</small></details>
<details id="motorDetails"><summary>Motor steps and manual jog</summary><p>Use an empty carousel. Saving or jogging cancels a pending dose and needs the position confirmed again.</p><label>Half-steps per compartment <input id="stepsPerSlot" type="number" min="1" max="4096" step="1" value="512"></label><label>Direction <select id="stepDirection"><option value="1">Forward</option><option value="-1">Reverse</option></select></label><label class="check"><input id="stepperConfirm" type="checkbox"><span>Carousel empty; step count and direction checked.</span></label><button id="saveStepper" type="button" disabled>Save motor settings</button><small style="margin-top:12px">512 is the starting value for eight compartments.</small><label>Jog half-steps <input id="jogSteps" type="number" min="1" max="512" step="1" value="16"></label><label class="check"><input id="jogConfirm" type="checkbox"><span>The carousel is empty and clear; a jog can release anything left inside.</span></label><div class="row"><button id="jogBack" type="button" class="secondary" disabled>Jog backward</button><button id="jogForward" type="button" class="secondary" disabled>Jog forward</button></div><p id="stepperMessage" class="message" role="status"></p><p class="quiet" style="margin-top:14px">ULN2003 IN1-4 → GPIO5, 6, 7, 8. Driver on 5 V and common ground.</p></details></section></div>

<h2 class="group-title">Test mode</h2><p class="quiet group-note">Use an empty carousel or dummy contents. Starting a test pauses the daily schedule.</p>
<div class="grid"><section id="irTestPanel" class="panel section-anchor"><div class="split"><h2>Hand and motor test</h2><span class="badge"><span id="irTestCount">0</span>&nbsp;cycles</span></div><p id="irTestStatus" class="status" role="status"></p><p id="irTestGesture" class="quiet" role="status"></p><label class="check"><input id="irTestConfirm" type="checkbox"><span>The carousel is empty or holds dummy contents, and nothing is in the way.</span></label><div class="row" style="margin-top:16px"><button id="startIrTest" type="button" disabled>Start test mode</button><button id="stopIrTest" type="button" class="secondary" disabled>Stop test mode</button></div><p id="irTestMessage" class="message" role="status"></p><details><summary>How to test</summary><ol class="step-list"><li><div><strong>Keep clear for 1 second.</strong></div></li><li><div><strong>Hold a hand for 1.5 seconds.</strong><br>The carousel turns one compartment.</div></li><li><div><strong>Remove your hand.</strong><br>Wait 3 seconds, keep clear for 1 second, then repeat.</div></li></ol><small>Stop test mode and confirm the position before enabling the daily schedule. Closing this page does not stop test mode.</small></details></section>
<section id="timerTestPanel" class="panel"><h2>Timer and overdue test</h2><p class="quiet">A quick run of the daily timing: ready after 10 s, overdue after 30 s. It doesn't change saved times or the week. Tick the confirmation in the hand test first.</p><div class="field-grid"><p>Ready in<br><strong id="timerTestReady">Not started</strong></p><p>Overdue in<br><strong id="timerTestDeadline">Not started</strong></p></div><p id="timerTestStatus" class="status" role="status"></p><p id="timerTestAlert" class="overdue" role="alert" hidden></p><button id="startTimerTest" type="button" disabled>Start timer test</button><p id="timerTestMessage" class="message" role="status"></p><small>Use Stop test mode to finish. Test results are kept apart from the daily history.</small></section></div>
<section id="irDetails" class="panel full-row"><h2>Hand sensor</h2><p id="irStatus" class="quiet" role="status"></p><details><summary>Sensor diagnostics and wiring</summary><p id="irDiagnostic" class="quiet"></p><p>IR VCC → 3.3 V, GND → ground, OUT → GPIO14. LOW means hand, HIGH means clear.</p><small>If it always detects a hand, turn the sensitivity screw or re-aim the sensor.</small></details></section>

<section id="limitsSection" class="panel full-row section-anchor"><h2>Storage limits</h2><p class="quiet">The dashboard alerts you when the pillbox is colder than 20 °C, warmer than 30 °C, or more humid than this limit.</p><label>Humidity alert above (%)<input id="rhMaxInput" type="number" min="30" max="99" step="1" value="95"></label><button id="saveLimits" type="button" class="secondary" disabled>Save limit</button><p id="limitsMessage" class="message" role="status"></p></section><section id="wifiSection" class="panel full-row section-anchor"><h2>Wi-Fi</h2><p id="networkStatus" class="quiet"></p><button id="forgetWifi" type="button" class="ghost" hidden>Forget saved network</button><details><summary>Join another network</summary><form id="wifiForm"><label>Wi-Fi name<input id="ssid" type="text" maxlength="32" autocomplete="off" required></label><label>Password<input id="wifiPassword" type="password" maxlength="63" autocomplete="new-password"></label><small>2.4 GHz home network. The PharmaVault Wi-Fi stays on as well.</small><button id="joinWifi" disabled style="margin-top:14px">Save &amp; connect</button></form><p id="wifiMessage" class="message" role="status"></p></details></section>
</div>
<footer class="footer"><span>PharmaVault · Seven days of care</span><span>Motor moves are logged. Pill pickup is not measured.</span></footer>
</main><script>
'use strict';
const el = id => document.getElementById(id);
let state = null, token = '', busy = false, firstState = true, connected = false, clockAutoSent = false;
/* autoSyncClock: once per connection, sends this device's time to /clock/auto if the pillbox
 * clock is unset or more than 2 s off. No arguments. Author: Malcolm Liew Jia En (ML) */
async function autoSyncClock(){
  const now=Math.floor(Date.now()/1000);
  if(!state||!token||busy||(state.clockSynced&&Math.abs(now-Number(state.epoch))<=2))return;
  const usedToken=token;token='';
  try{const r=await fetchTimed('/clock/auto',{method:'POST',body:new URLSearchParams({epoch:now,token:usedToken})});
    if(!r.ok||!state.clockSynced)el('clockMessage').textContent=await r.text();}catch(e){}
}
let irDisplayTimer = null, connectionFailed = false;
let instructionsDirty=false;
const irDisplayMaxAgeMs=2000;
const confirmations = ['scheduleConfirm','alignmentConfirm','stepperConfirm','jogConfirm','irTestConfirm','weekFilledConfirm','weekParkedConfirm'];
function clearConfirmations() { confirmations.forEach(id => {el(id).checked=false;}); }
function integer(value,min,max) {const n=Number(value);return Number.isInteger(n)&&n>=min&&n<=max?n:null;}
function minuteValue(value) {
  if(!/^\d{2}:\d{2}$/.test(value))return null;
  const [h,m]=value.split(':').map(Number);return h<=23&&m<=59?h*60+m:null;
}
function timeInput(minute) {return Number.isInteger(minute)&&minute>=0&&minute<=1439?String(Math.floor(minute/60)).padStart(2,'0')+':'+String(minute%60).padStart(2,'0'):'';}
function clockText(epoch) {
  if(!Number.isFinite(epoch)||epoch<=0)return 'Clock not set';
  return new Intl.DateTimeFormat('en-SG',{timeZone:'Asia/Singapore',year:'numeric',month:'short',day:'2-digit',hour:'2-digit',minute:'2-digit',second:'2-digit',hour12:false}).format(new Date(epoch*1000))+' SGT';
}
function dayText(day) {return Number.isInteger(day)&&day>0?new Date(day*86400000).toISOString().slice(0,10):'Date unknown';}
function controls() {
  const available=connected&&Boolean(token)&&!busy;
  const idle=available&&!state?.motionActive;
  const setup=idle&&state?.storageOk&&!state?.scheduleEnabled&&!state?.irTestMode;
  const due=minuteValue(el('scheduleTime').value),deadline=minuteValue(el('deadlineTime').value);
  const timesValid=due!==null&&deadline!==null&&due<deadline;
  const timesSaved=timesValid&&due===state?.dueMinute&&deadline===state?.alertMinute;
  el('startIrTest').disabled=!idle||!state?.storageOk||!state?.canEnableIrTest||Boolean(state?.irTestMode)||!el('irTestConfirm').checked;
  el('stopIrTest').disabled=!idle||!state?.irTestMode;
  el('irTestConfirm').disabled=(Boolean(state?.irTestMode)&&state?.timerTestMode!==true)||busy;
  el('startTimerTest').disabled=!timerTestFieldsValid()||!idle||!state?.storageOk||!state?.stepperVerified||!state?.canTest||
    (Boolean(state?.irTestMode)&&state?.timerTestMode!==true)||!el('irTestConfirm').checked;
  el('syncClock').disabled=!setup;
  el('saveSchedule').disabled=!setup||!timesValid||!el('scheduleConfirm').checked;
  el('enableSchedule').disabled=!setup||!state?.canEnableSchedule||!timesSaved||!el('scheduleConfirm').checked;
  el('pauseSchedule').disabled=!idle||!state?.scheduleEnabled;
  el('scheduleTime').disabled=Boolean(state?.scheduleEnabled)||Boolean(state?.irTestMode)||busy;
  el('deadlineTime').disabled=Boolean(state?.scheduleEnabled)||Boolean(state?.irTestMode)||busy;
  const pendingTarget=state?.today?.status==='READY'?integer(state?.today?.slot,1,8):null;
  const aligned=integer(el('alignedSlot').value,1,8);
  el('confirmAlignment').disabled=!setup||aligned===null||(pendingTarget!==null&&aligned%8+1!==pendingTarget)||!el('alignmentConfirm').checked;
  el('saveStepper').disabled=!setup||integer(el('stepsPerSlot').value,1,4096)===null||!['1','-1'].includes(el('stepDirection').value)||!el('stepperConfirm').checked;
  const jog=setup&&state?.canTest&&integer(el('jogSteps').value,1,512)!==null&&el('jogConfirm').checked;
  el('jogBack').disabled=el('jogForward').disabled=!jog;
  el('refillWeek').disabled=!idle||!state?.storageOk||state?.irTestMode||!state?.canRefillWeek||!timesSaved||!el('weekFilledConfirm').checked||!el('weekParkedConfirm').checked;
  el('weekFilledConfirm').disabled=el('weekParkedConfirm').disabled=!idle||Boolean(state?.irTestMode)||!state?.canRefillWeek;
  el('joinWifi').disabled=!setup;
  instructionControls(idle);
  el('scheduleStatus').textContent=!connected?'Connection unavailable. Check the device before making changes.':
    state?.irTestMode?'IR TEST MODE ACTIVE. Daily dispensing is paused. Stop testing and confirm alignment before enabling the schedule.':
    state?.scheduleEnabled?'Daily schedule enabled. Pause before changing settings. '+(state.systemStatus||''):
    !timesValid?'Choose a valid scheduled time and a later overdue time on the same day.':
    !timesSaved?'Times have been edited. Save them before enabling the daily schedule.':
    'Daily schedule paused. '+(state?.systemStatus||'Complete setup before enabling.');
  renderOverview();
}
async function fetchTimed(url,options={}) {
  const controller=new AbortController();const timer=setTimeout(()=>controller.abort(),3500);
  try{return await fetch(url,{...options,signal:controller.signal});}finally{clearTimeout(timer);}
}
async function post(url,fields,target) {
  if(busy||!token||!connected)return;
  busy=true;const usedToken=token;token='';
  if(['/stepper/jog','/schedule/mode','/test/mode'].includes(url))invalidateIr('Waiting for fresh sensor readings after this operation.');
  clearConfirmations();controls();el(target).textContent='Sending…';
  try {
    const response=await fetchTimed(url,{method:'POST',body:new URLSearchParams({...fields,confirm:'1',token:usedToken})});
    el(target).textContent=await response.text();
  } catch {
    el(target).textContent='Response lost. Check the displayed device state and mechanism before retrying.';
  } finally {busy=false;controls();}
}
function list(id,values) {el(id).replaceChildren(...values.map(value=>{const li=document.createElement('li');li.textContent=value;return li;}));}

function instructionTextValid(value) {
  return typeof value==='string'&&value.length<=48&&!/[^\x20-\x21\x23-\x5b\x5d-\x7e]/.test(value);
}
function instructionsAvailable() {
  return Array.isArray(state?.refillInstructions)&&state.refillInstructions.length===7&&state.refillInstructions.every(instructionTextValid);
}
function instructionValues() {return Array.from({length:7},(_,i)=>el('instructionDay'+(i+1)).value);}
function instructionControls(idle) {
  const available=instructionsAvailable();
  const invalid=instructionValues().findIndex(value=>!instructionTextValid(value));
  for(let day=1;day<=7;day++)el('instructionDay'+day).disabled=!available||busy;
  el('saveInstructions').disabled=!idle||!state?.storageOk||!available||invalid!==-1;
  el('instructionsStatus').textContent=!available?'Refill instructions unavailable in this firmware.':
    invalid!==-1?'Day '+(invalid+1)+': use at most 48 printable ASCII characters, excluding double quotes and backslashes.':
    instructionsDirty?'Unsaved changes. Polling will keep your draft until you save.':
    'Instructions are saved on the pillbox. Blank days appear as Not set on the healthcare LCD.';
}
function renderRefillInstructions() {
  if(!instructionsAvailable()||instructionsDirty)return;
  state.refillInstructions.forEach((value,i)=>{el('instructionDay'+(i+1)).value=value;});
}
async function saveRefillInstructions(event) {
  event.preventDefault();
  if(el('saveInstructions').disabled||busy||!token||!connected)return;
  const values=instructionValues();if(!values.every(instructionTextValid))return;
  const usedToken=token;token='';busy=true;clearConfirmations();controls();
  el('instructionsMessage').textContent='Saving instructions...';
  try {
    const fields={confirm:'1',token:usedToken};values.forEach((value,i)=>{fields['day'+(i+1)]=value;});
    const response=await fetchTimed('/refill/instructions',{method:'POST',body:new URLSearchParams(fields)});
    el('instructionsMessage').textContent=await response.text();
    if(response.ok){
      state.refillInstructions=values;instructionsDirty=false;renderRefillInstructions();
    }
  } catch {
    el('instructionsMessage').textContent='Response lost. Your draft is preserved. Reconnect and check the saved instructions before retrying.';
  } finally {busy=false;controls();}
}

function clearIrTimer() {
  if(irDisplayTimer!==null)clearTimeout(irDisplayTimer);
  irDisplayTimer=null;
}
function irInstruction() {
  if(state?.timerTestMode===true) {
    const instructions={
      WAITING:'TIMER TEST: keep your hand clear until the 10-second ready time. An early hand cannot dispense.',
      READY:'TIMER TEST READY: clear the sensor for one second, then hold a fresh hand for 1.5 seconds for one test rotation.',
      OVERDUE:'TEST OVERDUE: a fresh hand can still request this test cycle. Clear for one second, then hold for 1.5 seconds.',
      MOVING:'TIMER TEST: the carousel is moving. Wait for completion.',
      COMPLETED:'TIMER TEST COMPLETED: another hand will not repeat this cycle. After the motor pause, confirm empty/dummy contents and press Restart timer test.'
    };
    return instructions[state.timerTestPhase]||'TIMER TEST: check the current test status before presenting a hand.';
  }
  if(state?.irTestMode)return 'IR TEST MODE: after the three-second motor pause, clear the sensor for one second, then hold a new hand for 1.5 seconds. Repeat only with an empty dispenser or dummy contents.';
  return 'Automatic hand detection: keep the receiving area clear for one second, then hold your hand for 1.5 seconds when today’s dose is ready.';
}
function invalidateIr(message) {
  clearIrTimer();el('irSignal').textContent='Unknown';el('irStatus').textContent=message;
  el('irDiagnostic').textContent='No recent IR reading is available.';controls();
}
function renderIr() {
  clearIrTimer();
  const age=state.irAgeMs,rawValid=state.irRaw===0||state.irRaw===1;
  const directMode=state.irActiveLevel===0&&state.handHoldMs===1500;
  const reported=Number.isFinite(age)&&age>=0&&age<=irDisplayMaxAgeMs&&rawValid&&!state.motionActive&&directMode;
  const steady=reported&&state.irStable===true;
  // This is a last-reported display only; the ESP32 samples and times the hand independently.
  el('irSignal').textContent=steady&&typeof state.irHandDetected==='boolean'?(state.irHandDetected?'Hand detected':'Clear'):reported?'Changing signal':'Unknown';
  el('irStatus').textContent=state.motionActive?'Movement in progress; waiting for new hand-sensor readings.':
    !directMode?'Waiting for the direct IR firmware with fixed 1.5-second hand detection.':irInstruction()+(reported?'':' Waiting for a sensor update.');
  const level=value=>value===0?'LOW (hand)':value===1?'HIGH (clear)':'unavailable';
  el('irDiagnostic').textContent='Last received GPIO14: '+(rawValid?level(state.irRaw):'unavailable')+'. Reported steady interval '+(Number.isFinite(state.irStableMs)?state.irStableMs:0)+' ms. Sample age on receipt '+(Number.isFinite(age)&&age>=0?age+' ms':'unknown');
  if(reported)irDisplayTimer=setTimeout(()=>invalidateIr('No recent sensor update. '+irInstruction()),irDisplayMaxAgeMs+1-age);
}
function timerTestFieldsValid() {
  return typeof state?.timerTestMode==='boolean'&&
    ['OFF','WAITING','READY','OVERDUE','MOVING','COMPLETED'].includes(state?.timerTestPhase)&&
    Number.isInteger(state?.timerTestDueIn)&&state.timerTestDueIn>=0&&
    Number.isInteger(state?.timerTestOverdueIn)&&state.timerTestOverdueIn>=0;
}
function renderTimerTest() {
  const valid=timerTestFieldsValid(),active=valid&&state.timerTestMode===true;
  const phase=valid?state.timerTestPhase:'OFF';
  el('startTimerTest').textContent=active?'Restart timer test':'Start timer test';
  el('timerTestStatus').textContent=!valid?'Timer test unavailable in this firmware.':
    active?(state.irTestStatus||'Timer test active.'):'Ready after 10 seconds; overdue after 30 seconds from Start.';
  el('timerTestReady').textContent=!valid?'Unavailable':!active?'Not started':state.motionActive?'Moving':phase==='WAITING'?state.timerTestDueIn+' s':
    phase==='COMPLETED'?'Test completed':phase==='MOVING'?'Moving':'Ready now';
  el('timerTestDeadline').textContent=!valid?'Unavailable':!active?'Not started':phase==='COMPLETED'?'Test completed':
    phase==='OVERDUE'?'Overdue':state.timerTestOverdueIn+' s';
  el('timerTestAlert').hidden=!active||phase!=='OVERDUE';
  el('timerTestAlert').textContent=active&&phase==='OVERDUE'?(state.motionActive?
    'TEST OVERDUE: the late test rotation is in progress. Wait for completion.':
    'TEST OVERDUE: no test cycle completed within 30 seconds. A fresh hand can still request this test cycle.') :'';
  if(active)el('irTestBanner').textContent=(phase==='OVERDUE'?'TEST OVERDUE: no test cycle completed within 30 seconds. ':'Timer + overdue test active. ')+
    'Daily dispensing is paused. Closing this page does not stop the test. Use Stop test mode to finish.';
}
function renderIrTest() {
  const active=state.irTestMode===true;
  el('irTestBanner').hidden=!active;
  el('irTestBanner').textContent=active?'Test mode active. Daily dispensing is paused. Closing this page does not stop the motor tests.':'';
  el('irTestStatus').textContent=state.irTestStatus||'Waiting for test-mode status.';
  el('irTestGesture').textContent=active?(state.motionActive?state.motionText||'Test movement in progress.':state.handStatus||'Clear the sensor, then present a fresh hand.'):'Test mode is off. Starting it requires an empty dispenser or dummy contents.';
  const cycles=Number.isInteger(state.irTestCycles)&&state.irTestCycles>=0?state.irTestCycles:null;
  el('irTestCount').textContent=cycles===null?'Unavailable':String(cycles);
  renderTimerTest();
}
function renderWeek() {
  const configured=state.weekConfigured===true, due=state.weekRefillDue===true;
  const elapsed=integer(state.weekDay,0,7);
  el('weekProgress').textContent=configured&&elapsed!==null?elapsed+' / 7 scheduled days':'Not started';
  el('weekDates').textContent=configured&&integer(state.weekStartDay,1,1000000)!==null?dayText(state.weekStartDay)+' through '+dayText(state.weekStartDay+6)+', Singapore dates':'Seven medicine compartments; compartment 8 stays empty for parking.';
  el('weekStatus').textContent=state.weekStatus||'Waiting for weekly refill status.';
  el('weekRefillAlert').hidden=!due;
  el('weekRefillAlert').textContent=due?'Weekly refill due. Remove remaining medicine, refill all seven days and park at empty 8. Open Refill to confirm.':'';
  el('weekPanel').classList.toggle('overdue',Boolean(due));
  el('weekNextStart').textContent=Number.isInteger(state.weekNextStartDay)&&state.weekNextStartDay>0?'When confirmed, the next week starts '+dayText(state.weekNextStartDay)+' using the saved daily times. If a daily record already exists today, the next week starts tomorrow.':'Set the clock to show the start date for a new week.';
  el('refillWeek').textContent=configured?'Confirm refill & start next week':'Confirm pack & start week';
  list('weekCompartments',configured&&Number.isInteger(state.weekStartDay)&&state.weekStartDay>0?Array.from({length:7},(_,i)=>'Day '+(i+1)+', '+dayText(state.weekStartDay+i)+', compartment '+(i+1)):['Load Day 1 medicine into compartment 1, through Day 7 into compartment 7. Leave compartment 8 empty.']);
}
function renderToday() {
  const today=state.today||{};
  const descriptions={NOT_DUE:'Waiting for today’s scheduled time.',POSITIONING:'Carousel rotation is in progress.',READY:'Today’s day-specific compartment is armed. Check the status below before presenting a hand.',ATTEMPTED:'Today’s stepper release attempt is recorded. Automatic repeat is blocked.',COMPLETED:'Today’s stepper release command is completed. Another automatic cycle today is blocked.',MISSED:'This scheduled day ended without a completed dose.',FAULT:'An interrupted or failed operation needs inspection. No automatic retry.'};
  el('todayStatus').textContent=state.irTestMode?'Daily dispensing is paused during IR test mode. Saved daily records below are not test results.':!state.clockSynced?'Set the clock to identify today’s dose. Saved records remain below.':descriptions[today.status]||'Waiting for today’s status.';
  el('todaySlot').textContent=Number.isInteger(today.slot)&&today.slot>=1&&today.slot<=8?'Compartment '+today.slot:'Not assigned yet';
  el('todayMedicine').textContent=integer(today.slot,1,7)!==null?'Day '+today.slot+' medicine pack':'No medicine pack due';
  el('todayDetail').textContent=state.irTestMode?'Use the test-mode panel above. Test rotations do not complete or reset a daily dose.':state.motionActive?state.motionText||'Mechanism moving.':state.handStatus||state.systemStatus||'';
  el('lastAction').textContent=today.completedAt?'Completion recorded: '+clockText(today.completedAt):today.preparedAt?'Armed at: '+clockText(today.preparedAt):'';
  el('doseWindow').textContent='Singapore time. Ready at '+timeInput(state.dueMinute)+', overdue alert at '+timeInput(state.alertMinute)+', hand hold '+(state.handHoldMs/1000).toFixed(1)+' seconds';
  const overdue=today.overdue===true&&!today.completedAt;
  el('overdueAlert').hidden=!overdue;
  el('overdueAlert').textContent=overdue?'Dose overdue. Check the dispenser. An eligible late dose remains available until midnight.':'';
  el('todayPanel').className=overdue?'overdue':'';
  const history=Array.isArray(state.history)?[...state.history].sort((a,b)=>b.day-a.day):[];
  list('dailyLog',history.length?history.map(item=>dayText(item.day)+', compartment '+(item.slot||'-')+'. '+(integer(item.slot,1,7)!==null?'Day '+item.slot+' pack':'Saved record')+': '+item.status+(item.overdue?', overdue recorded':'')+(item.completedAt?', completed '+clockText(item.completedAt):'')):['No recorded daily events yet.']);
  const alerts=Array.isArray(state.alerts)?state.alerts:[];
  el('alertSummary').hidden=alerts.length===0;
  el('alertSummary').textContent=alerts.length?alerts.length+' retained overdue record'+(alerts.length===1?'':'s')+' without a completed dose. Review the dated entries below.':'No retained overdue records awaiting completion.';
}
async function refresh() {
  if(!busy) {
    try {
      const started=performance.now();const response=await fetchTimed('/state',{cache:'no-store'});
      if(!response.ok)throw new Error('Status unavailable');
      state=await response.json();token=state.token;connected=true;connectionFailed=false;
      if(!clockAutoSent){clockAutoSent=true;autoSyncClock();}  // sync once per connection (ML)
      const latency=Math.max(0,Math.ceil(performance.now()-started));
      if(Number.isFinite(state.irAgeMs)&&state.irAgeMs>=0)state.irAgeMs+=latency;
      el('temp').textContent=state.temp===null?'Unavailable':(state.temp/100).toFixed(1)+' °C';
      el('rh').textContent=state.rh===null?'Unavailable':(state.rh/100).toFixed(1)+' %';
      el('environment').textContent=state.environment||'';
      el('tag').textContent=state.tag||'No card scanned';
      el('nfcStatus').textContent=state.nfcStatus||'Waiting for the MSP430 reader.';
      el('storage').textContent=state.storageOk?'':'Settings storage unavailable. Inspect and restart before enabling movement.';
      el('clockStatus').textContent=state.clockSynced?clockText(state.epoch):'Clock not set. Pause the schedule, then sync from this device.';
      const pending=state.today?.status==='READY'&&integer(state.today?.slot,1,8)!==null;
      const preceding=pending?(state.today.slot+6)%8+1:0;
      el('alignmentStatus').textContent=state.positionKnown?'Current aligned/last released compartment: '+state.currentSlot+'. Next physical step reaches compartment '+(state.currentSlot%8+1)+'. Calendar-day eligibility is checked separately; no loaded missed compartment is skipped automatically.':
        pending?'Position unknown. Pause, manually remove any missed medicine, and visually confirm the preceding compartment '+preceding+' to release today’s day-specific compartment '+state.today.slot+'. Do not rotate past a loaded missed compartment.':'Position unknown. With the schedule paused, manually remove missed medicine and verify the current compartment. A new week must start at empty parking compartment 8.';
      el('networkStatus').textContent=state.lanIp?'On your home Wi-Fi: http://pharmavault.local or http://'+state.lanIp+'. On the PharmaVault Wi-Fi: http://pharmavault':'On the PharmaVault Wi-Fi: http://pharmavault (or http://192.168.4.1)';
      el('networkStatus').textContent+=state.homeSsid&&!state.lanIp?'. Saved home network "'+state.homeSsid+'" is out of range; the pillbox retries every 2 minutes.':'';el('forgetWifi').hidden=!state.homeSsid;
      el('bridgeStatus').textContent=state.bridgeOnline===true?'Healthcare station connected: pillbox readings/status are sent to the MSP430.':'Healthcare station disconnected. Pillbox sensors and dispensing continue locally.';

      renderIr();renderToday();renderIrTest();renderWeek();renderRefillInstructions();
      if(state.motionActive)clearConfirmations();
      if(firstState) {
        firstState=false;el('scheduleTime').value=timeInput(state.dueMinute);el('deadlineTime').value=timeInput(state.alertMinute);
        el('stepsPerSlot').value=state.stepsPerSlot;el('stepDirection').value=state.stepperDirection;
        el('alignmentDetails').open=!state.positionKnown;

      }
    } catch {
      connected=false;connectionFailed=true;token='';clockAutoSent=false;invalidateIr('Dashboard connection lost. Keep hands and moving parts clear.');
      el('bridgeStatus').textContent='Dashboard unreachable. Healthcare-station link status is unknown.';
      el('irTestStatus').textContent='Connection lost. Test mode may still be active on the ESP32; reconnect to stop it or restart the board.';
      if(state?.irTestMode){el('irTestBanner').hidden=false;el('irTestBanner').textContent='LAST RECEIVED: IR TEST MODE ACTIVE. The page cannot confirm its current state. Reconnect to stop it or restart the board.';}
      el('irTestGesture').textContent='No current gesture state is available.';
      el('timerTestStatus').textContent='Connection lost. The timer test may still be active; reconnect to check or stop it.';
      el('timerTestReady').textContent=el('timerTestDeadline').textContent='Unavailable';
      if(!el('timerTestAlert').hidden)el('timerTestAlert').textContent='LAST RECEIVED TEST ALERT: overdue. Reconnect to verify the current test state.';
      el('weekStatus').textContent='Connection lost. Weekly progress and dates are last received; reconnect before refilling or enabling dispensing.';
      if(!el('weekRefillAlert').hidden)el('weekRefillAlert').textContent='LAST RECEIVED ALERT: weekly refill due. Reconnect to verify the current weekly state.';
      el('todayStatus').textContent='Connection lost. Daily operating state is unknown. Check the device.';
      el('clockStatus').textContent='No current clock reading from the ESP32.';
      if(!el('overdueAlert').hidden)el('overdueAlert').textContent='LAST RECEIVED ALERT: dose overdue. Reconnect to check its current status.';
      if(!el('alertSummary').hidden)el('alertSummary').textContent='LAST RECEIVED ALERT: overdue records remain. Reconnect to check their current status.';
    }
  }
  controls();setTimeout(refresh,document.hidden?5000:1000);
}
for(let day=1;day<=7;day++)el('instructionDay'+day).addEventListener('input',()=>{instructionsDirty=true;controls();});
el('refillInstructionForm').addEventListener('submit',saveRefillInstructions);
confirmations.forEach(id=>el(id).addEventListener('change',controls));
['scheduleTime','deadlineTime'].forEach(id=>el(id).addEventListener('input',()=>{el('scheduleConfirm').checked=false;controls();}));
el('alignedSlot').addEventListener('change',()=>{el('alignmentConfirm').checked=false;controls();});
['stepsPerSlot','stepDirection'].forEach(id=>el(id).addEventListener(id==='stepDirection'?'change':'input',()=>{el('stepperConfirm').checked=false;controls();}));
el('jogSteps').addEventListener('input',()=>{el('jogConfirm').checked=false;controls();});
el('startIrTest').addEventListener('click',()=>{
  if(el('startIrTest').disabled)return;
  if(!confirm('Start repeatable IR motor test mode with an empty dispenser or dummy contents? This pauses daily dispensing and cancels any pending daily dose. Completed history stays saved.'))return;
  post('/test/mode',{enabled:'1',unloaded:'1'},'irTestMessage');
});
el('startTimerTest').addEventListener('click',()=>{
  if(el('startTimerTest').disabled)return;
  if(!confirm('Start or restart the 10-second ready / 30-second overdue test with an empty carousel or dummy contents? This pauses daily dispensing and cancels any pending daily dose. The saved daily times and weekly day count are unchanged.'))return;
  post('/test/mode',{enabled:'1',timed:'1',unloaded:'1'},'timerTestMessage');
});
el('stopIrTest').addEventListener('click',()=>{
  if(el('stopIrTest').disabled)return;post('/test/mode',{enabled:'0'},'irTestMessage');
});
el('refillWeek').addEventListener('click',()=>{
  if(el('refillWeek').disabled)return;
  if(!confirm('Start the seven-day cycle on '+dayText(state.weekNextStartDay)+'? Confirm all seven day-specific compartments are refilled, any old medicine is removed, and empty compartment 8 is physically at the dispensing opening. This retains the saved daily times, records parking position 8 and enables the schedule. It does not move the motor.'))return;
  post('/week/refill',{filled:'1',parked:'1'},'weekMessage');
});
el('syncClock').addEventListener('click',()=>{
  if(el('syncClock').disabled)return;const epoch=Math.floor(Date.now()/1000);
  if(!confirm('Set the ESP32 clock to '+clockText(epoch)+'? Check this device’s date and time first.'))return;
  post('/clock',{epoch},'clockMessage');
});
el('saveSchedule').addEventListener('click',()=>{if(!el('saveSchedule').disabled)post('/schedule/save',{dueMinute:minuteValue(el('scheduleTime').value),alertMinute:minuteValue(el('deadlineTime').value)},'scheduleMessage');});
el('enableSchedule').addEventListener('click',()=>{
  if(el('enableSchedule').disabled)return;
  if(!confirm('Enable the daily schedule? Each calendar day keeps its assigned compartment, even after a missed dose. Enabling does not move the motor; a fresh 1.5-second hand presentation requests an eligible release.'))return;
  post('/schedule/mode',{enabled:'1'},'scheduleMessage');
});
el('pauseSchedule').addEventListener('click',()=>{if(!el('pauseSchedule').disabled)post('/schedule/mode',{enabled:'0'},'scheduleMessage');});
el('confirmAlignment').addEventListener('click',()=>{if(!el('confirmAlignment').disabled)post('/stepper/align',{slot:el('alignedSlot').value,verified:'1',handled:'1'},'alignmentMessage');});
el('saveStepper').addEventListener('click',()=>{if(!el('saveStepper').disabled)post('/stepper/save',{stepsPerSlot:el('stepsPerSlot').value,direction:el('stepDirection').value,verified:'1'},'stepperMessage');});
['jogBack','jogForward'].forEach(id=>el(id).addEventListener('click',()=>{
  if(el(id).disabled)return;const steps=Number(el('jogSteps').value)*(id==='jogBack'?-1:1);
  if(!confirm('Jog the carousel '+steps+' half-steps? It moves directly and can release contents. Confirm it is unloaded. This invalidates the recorded alignment.'))return;
  post('/stepper/jog',{steps,clear:'1'},'stepperMessage');
}));
el('wifiForm').addEventListener('submit',event=>{
  event.preventDefault();if(el('joinWifi').disabled)return;post('/wifi',{ssid:el('ssid').value,password:el('wifiPassword').value},'wifiMessage');el('wifiPassword').value='';
});

const viewNames=['Today','Settings'];
const viewAlias={Refill:['Today','weekPanel'],Test:['Settings','irTestPanel']};
let activeView='Today', nextView='Settings', nextSection='';
function showView(name,section='',focusTab=false) {
  if(viewAlias[name]){section=section||viewAlias[name][1];name=viewAlias[name][0];}
  if(!viewNames.includes(name))return;
  activeView=name;
  viewNames.forEach(item=>{
    const selected=item===name;
    el('view'+item).hidden=!selected;
    el('nav'+item).setAttribute('aria-selected',String(selected));
    el('nav'+item).setAttribute('tabindex',selected?'0':'-1');
  });
  if(focusTab)el('nav'+name).focus();
  if(section) {
    if(section==='alignmentSection')el('alignmentDetails').open=true;
    if(section==='motorDetails')el('motorDetails').open=true;
    if(section==='weekPanel')el('refillDetails').open=true;
    el(section).scrollIntoView({block:'start',behavior:'auto'});
  }
}
let envAlerted=0,envAcked=0,audioCtx=null;
const ChimeEveryMs=6000;
function chime(){
  // Soft sine chime with a gentle attack and fade, so it never clicks or blares.
  try{audioCtx=audioCtx||new (window.AudioContext||window.webkitAudioContext)();
    const t=audioCtx.currentTime,o=audioCtx.createOscillator(),g=audioCtx.createGain();
    o.type='sine';o.frequency.setValueAtTime(660,t);o.frequency.setValueAtTime(880,t+.18);
    g.gain.setValueAtTime(0.0001,t);g.gain.exponentialRampToValueAtTime(0.035,t+.04);g.gain.exponentialRampToValueAtTime(0.0001,t+.6);
    o.connect(g);g.connect(audioCtx.destination);o.start(t);o.stop(t+.62);}catch(e){}
}
document.addEventListener('pointerdown',()=>{try{audioCtx=audioCtx||new (window.AudioContext||window.webkitAudioContext)();audioCtx.resume();}catch(e){}},{once:true});
setInterval(()=>{if(envAlerted&&envAlerted!==envAcked)chime();},ChimeEveryMs);
function renderEnvAlert(){
  const live=connected&&state!==null,f=live?Number(state.envFlags)||0:0,t=live?state.temp:null,h=live?state.rh:null;
  const hot=Boolean(f&1)&&t!==null&&t>state.tempMax,cold=Boolean(f&1)&&!hot&&t!==null,humid=Boolean(f&2)&&h!==null,msgs=[];
  const c=v=>String(Math.round(v/10)/10);
  if(hot)msgs.push('Too hot: '+c(t)+' °C.');if(cold)msgs.push('Too cold: '+c(t)+' °C.');
  if(hot||cold)msgs.push('Keep the medicine at '+c(state.tempMin)+' to '+c(state.tempMax)+' °C.');
  if(humid)msgs.push('Too humid: '+c(h)+' %. Keep it at or below '+c(state.rhMax)+' %.');
  const key=(hot?1:0)|(cold?2:0)|(humid?4:0);
  if(key&&key!==envAlerted){envAcked=0;try{if(navigator.vibrate)navigator.vibrate([250,120,250]);}catch(e){}chime();}
  envAlerted=key;
  el('envAlert').hidden=!msgs.length;
  el('envAlertText').textContent=msgs.length?'Storage alert. '+msgs.join(' ')+' Move the pillbox somewhere cooler and drier.'+(key===envAcked?' Sound off.':''):'';
  el('envAck').hidden=!key||key===envAcked;
  document.title=key?'Storage alert: PharmaVault':'PharmaVault · Daily care';
  const rhIn=el('rhMaxInput');if(live&&!rhIn.dataset.dirty&&document.activeElement!==rhIn)rhIn.value=Math.round(state.rhMax/100);
  const pct=Number(rhIn.value);el('saveLimits').disabled=!live||!token||busy||!Number.isInteger(pct)||pct<30||pct>99;
}
el('envAck').addEventListener('click',()=>{envAcked=envAlerted;renderEnvAlert();});
el('rhMaxInput').addEventListener('input',()=>{el('rhMaxInput').dataset.dirty='1';renderEnvAlert();});
el('saveLimits').addEventListener('click',()=>{if(el('saveLimits').disabled)return;post('/limits',{rhMax:el('rhMaxInput').value},'limitsMessage');delete el('rhMaxInput').dataset.dirty;});
function renderOverview() {
  const live=connected&&state!==null;
  el('connectionState').textContent=live?'Dispenser connected':connectionFailed?'Connection lost':'Connecting';
  el('connectionState').setAttribute('data-online',String(live));
  el('connectionNotice').hidden=!connectionFailed;
  el('connectionNotice').textContent=connectionFailed?'Connection lost. Controls are unavailable; the dispenser may still be running. Reconnect to check its state.':'';
  el('globalTest').hidden=el('irTestBanner').hidden;
  el('quickPause').disabled=el('pauseSchedule').disabled;
  el('quickStopTest').disabled=el('stopIrTest').disabled;
  el('refillDot').hidden=!state?.weekRefillDue;
  el('testDot').hidden=!state?.irTestMode;
  el('motionNotice').hidden=!live||!state.motionActive;
  el('motionNotice').textContent=live&&state.motionActive?'Carousel moving. Wait for this rotation to finish; another action cannot start yet.':'';
  el('nextAction').disabled=!live;
  if(!state) {
    el('heroTitle').textContent=connectionFailed?'Let’s reconnect.':'Connecting to your dispenser.';
    el('todayDetail').textContent='Join the PharmaVault Wi-Fi and keep the dispenser powered on.';
    return;
  }
  el('summaryDue').textContent=timeInput(state.dueMinute);
  el('summaryAlert').textContent=timeInput(state.alertMinute);
  el('scheduleBadge').textContent=!live?'Last received':state.irTestMode?'Test mode':state.scheduleEnabled?'Enabled':'Paused';
  el('modeBadge').textContent=!live?'Offline':state.motionActive?'Moving':state.irTestMode?'Test mode':state.weekRefillDue?'Refill due':state.scheduleEnabled?'Daily mode':'Paused';
  el('overviewDate').textContent=live&&state.clockSynced?new Intl.DateTimeFormat('en-SG',{timeZone:'Asia/Singapore',weekday:'long',day:'numeric',month:'long'}).format(new Date(state.epoch*1000)):'YOUR DAILY OVERVIEW';
  el('weekShort').textContent=state.weekConfigured?(integer(state.weekDay,0,7)??0)+' / 7 days':'Not started';
  el('weekSummary').textContent=!live?'Last received pack status. Reconnect to verify.':state.weekRefillDue?'All seven scheduled days are finished. Refill the whole pack.':!state.weekConfigured?'Seven medicine compartments. One empty parking position.':dayText(state.weekStartDay)+' to '+dayText(state.weekStartDay+6)+'. Missed days still count';
  const localDay=state.clockSynced?Math.floor((state.epoch+28800)/86400):0;
  const slot=state.weekConfigured&&localDay>=state.weekStartDay&&localDay<state.weekStartDay+7?localDay-state.weekStartDay+1:0;
  el('weekDots').replaceChildren(...Array.from({length:7},(_,i)=>{
    const dot=document.createElement('span'),day=state.weekStartDay+i;
    const record=state.weekConfigured?(state.history||[]).find(r=>r.day===day):null;
    const status=record?.status||'Not completed';
    dot.className='week-dot';dot.textContent=String(i+1);
    dot.setAttribute('data-done',String(status==='COMPLETED'));
    dot.setAttribute('data-missed',String(['MISSED','FAULT','ATTEMPTED'].includes(status)));
    dot.setAttribute('data-current',String(slot===i+1));
    dot.setAttribute('aria-label','Day '+(i+1)+': '+status);
    dot.setAttribute('title','Day '+(i+1)+': '+status);
    return dot;
  }));
  let title='', detail=state.systemStatus||'', action='Manage settings';
  nextView='Settings';nextSection='scheduleSection';
  const record=state.today||{};
  const expected=record.status==='COMPLETED'&&record.slot===slot?slot:slot===1?8:slot?slot-1:8;
  const wrongPosition=state.positionKnown&&state.currentSlot!==expected;
  if(!live) {
    title='Let’s reconnect.';detail='The current operating state is unknown. Reconnect to the dispenser before making changes.';
    action='Settings unavailable';
    el('temp').textContent=el('rh').textContent='-';
    el('environment').textContent='Connection lost. Current readings unavailable.';
  } else if(!state.storageOk) {
    title='The dispenser needs attention.';action='View settings';
  } else if(state.motionActive) {
    title='The carousel is turning.';detail='Wait for the motor command to finish. Keep the mechanism clear.';action='';
  } else if(state.irTestMode) {
    title=state.timerTestMode===true?(state.timerTestPhase==='OVERDUE'?'The timer test is overdue.':state.timerTestPhase==='COMPLETED'?'Timer test complete.':'Timer test is running.'):'Test mode is running.';detail=state.handStatus||state.irTestStatus;action='View test controls';nextView='Test';nextSection=state.timerTestMode===true?'timerTestPanel':'';
  } else if(state.weekRefillDue) {
    title='Time for a fresh week.';detail='Clear remaining medicine, refill all seven days and park at empty compartment 8.';action='Prepare weekly refill';nextView='Refill';nextSection='';
  } else if(!state.clockSynced) {
    title='Sync the clock to begin.';detail='Your saved timer is still here. Restore Singapore time after power-up.';action='Set up clock';
  } else if((state.systemStatus||'').includes('Clock is earlier')) {
    title='Check the date and time.';action='Correct clock';
  } else if(!state.stepperVerified) {
    title='Set up the carousel.';detail='Verify one-compartment travel and direction on the unloaded mechanism.';action='Open motor setup';nextSection='motorDetails';
  } else if(!state.weekConfigured) {
    title='Prepare your seven-day pack.';detail='Fill days 1 to 7, leave position 8 empty, then confirm once for the whole week.';action='Set up weekly pack';nextView='Refill';nextSection='';
  } else if(['FAULT','ATTEMPTED','MISSED'].includes(record.status)) {
    title='Today’s dose needs checking.';detail='An unsuccessful or uncertain attempt cannot automatically retry. Inspect the medicine and mechanism.';action='Review alignment';nextSection='alignmentSection';
  } else if(!state.positionKnown||wrongPosition) {
    title='Check carousel alignment.';detail='Pause and remove any missed medicine before manually aligning the correct compartment. Do not rotate past loaded missed doses.';action='Review alignment';nextSection='alignmentSection';
  } else if(!state.scheduleEnabled) {
    title='Your daily schedule is paused.';detail='Check the clock and alignment, then enable the saved timer. Enabling does not move the motor.';action='Enable in settings';
  } else if(record.status==='COMPLETED') {
    title='All set for today.';detail='Today’s motor cycle is recorded. Another daily cycle is blocked until the next scheduled day.';action='';
  } else if(record.status==='READY') {
    title=el('irSignal').textContent==='Unknown'?'Waiting for the hand sensor.':!state.handArmed?'Clear the sensor to get ready.':'Ready for your hand.';
    detail=state.handStatus||'Clear the area for one second, then hold your hand near the sensor for 1.5 seconds.';action='';
  } else {
    title='Your timer is set.';detail='Today’s assigned dose becomes available at '+timeInput(state.dueMinute)+'. Then hold a fresh hand for 1.5 seconds to dispense.';action='';
    if(localDay<state.weekStartDay)detail='Your seven-day pack starts '+dayText(state.weekStartDay)+' at '+timeInput(state.dueMinute)+'. No action needed until then.';
  }
  el('heroTitle').textContent=title;el('todayDetail').textContent=detail;
  el('nextAction').hidden=!action;el('nextAction').textContent=action;renderEnvAlert();
}
viewNames.forEach((name,index)=>{
  el('nav'+name).addEventListener('click',()=>showView(name));
  el('nav'+name).addEventListener('keydown',event=>{
    const offsets={ArrowRight:1,ArrowLeft:-1};
    if(event.key in offsets){event.preventDefault();showView(viewNames[(index+offsets[event.key]+viewNames.length)%viewNames.length],'',true);}
    else if(event.key==='Home'||event.key==='End'){event.preventDefault();showView(viewNames[event.key==='Home'?0:viewNames.length-1],'',true);}
  });
});
el('manageSchedule').addEventListener('click',()=>showView('Settings','scheduleSection',true));
el('openRefill').addEventListener('click',()=>showView('Refill'));
document.querySelectorAll('.jump a').forEach(a=>a.addEventListener('click',event=>{event.preventDefault();showView('Settings',a.getAttribute('href').slice(1));}));
el('nextAction').addEventListener('click',()=>{if(!el('nextAction').disabled)showView(nextView,nextSection,true);});
el('quickPause').addEventListener('click',()=>{
  if(!el('quickPause').disabled)post('/schedule/mode',{enabled:'0'},'globalActionMessage');
});
el('forgetWifi').addEventListener('click',()=>{if(!el('forgetWifi').disabled&&confirm('Forget the saved home network? The pillbox keeps its own PharmaVault Wi-Fi.'))post('/wifi/forget',{},'wifiMessage');});
el('quickStopTest').addEventListener('click',()=>{
  if(!el('quickStopTest').disabled)post('/test/mode',{enabled:'0'},'globalActionMessage');
});
showView('Today');


refresh();

</script></body></html>

)PVHTML";
#endif
