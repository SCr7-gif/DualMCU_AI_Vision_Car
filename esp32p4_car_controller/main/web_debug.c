#include "web_debug.h"

#include "audio_codec.h"
#include "behavior_manager.h"
#include "board_diagnostics.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network_manager.h"
#include "qwen_realtime_client.h"
#include "robot_camera.h"
#include "robot_tools.h"
#include "sensor_cache.h"
#include "stm32_link.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "web_debug";
static httpd_handle_t s_server;
static bool s_manual_mode;

bool web_debug_is_manual_mode(void)
{
    return s_manual_mode;
}

static const char *INDEX_HTML_OLD __attribute__((unused)) =
"<!doctype html><html><head><meta charset=\"utf-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>AI小车调试台</title>"
"<style>"
"body{margin:0;font-family:Arial,'Microsoft YaHei',sans-serif;background:#f5f7fa;color:#17202a}"
"header{height:48px;background:#233142;color:white;display:flex;align-items:center;padding:0 14px;font-weight:700}"
"main{padding:12px;display:grid;gap:12px;grid-template-columns:repeat(auto-fit,minmax(280px,1fr))}"
"section{background:white;border:1px solid #d8dee8;border-radius:6px;padding:12px}"
"h2{font-size:16px;margin:0 0 10px}pre{background:#eef2f6;padding:8px;overflow:auto;min-height:130px}"
"#cam{width:100%;max-width:420px;background:#111;aspect-ratio:4/3;object-fit:contain;border-radius:4px}"
"button{height:36px;margin:4px 4px 4px 0;border:1px solid #9aa4b2;background:#fff;border-radius:6px;font-weight:600}"
"button.danger{background:#b91c1c;color:white;border-color:#b91c1c}"
"input{height:32px;width:70px;margin:4px;border:1px solid #9aa4b2;border-radius:4px;padding:0 6px}"
"</style></head><body><header>AI小车调试台</header><main>"
"<section><h2>状态</h2><button onclick=\"diag()\">诊断</button><pre id=\"status\">加载中...</pre></section>"
"<section><h2>摄像头</h2>"
"<img id=\"cam\" src=\"/api/camera/stream\" alt=\"camera stream\">"
"<br><button onclick=\"camStatus()\">相机状态</button>"
"<button onclick=\"snapshot()\">截图</button>"
"<button onclick=\"restartCam()\">重连预览</button>"
"<pre id=\"camera\">等待相机状态</pre></section>"
"<section><h2>电机</h2>"
"<button onclick=\"stop()\" class=\"danger\">急停</button><br>"
"<button onclick=\"move('forward')\">前进</button><button onclick=\"move('backward')\">后退</button>"
"<button onclick=\"move('left')\">左转</button><button onclick=\"move('right')\">右转</button><br>"
"速度<input id=\"speed\" value=\"30\"> 时长ms<input id=\"duration\" value=\"1800\"></section>"
"<section><h2>云台</h2>"
"水平<input id=\"pan\" value=\"90\"> 俯仰<input id=\"tilt\" value=\"90\">"
"<button onclick=\"gimbal()\">设置</button></section>"
"<section><h2>灯光和行为</h2>"
"<button onclick=\"api('/api/led?effect=idle')\">空闲</button>"
"<button onclick=\"api('/api/led?effect=listening')\">聆听</button>"
"<button onclick=\"api('/api/led?effect=speaking')\">说话</button>"
"<button onclick=\"api('/api/led?effect=thinking')\">思考</button>"
"<button onclick=\"api('/api/behavior?name=patrol')\">巡逻</button>"
"<button onclick=\"api('/api/behavior?name=dance')\">跳舞</button></section>"
"<section><h2>智能行为</h2>"
"<button onclick=\"api('/api/behavior?name=dance')\">跳舞</button>"
"<button onclick=\"api('/api/behavior?name=patrol')\">开始巡逻</button>"
"<button onclick=\"api('/api/behavior?name=stop_patrol')\" class=\"danger\">停止巡逻</button></section>"
"<section><h2>音频</h2>"
"<button onclick=\"api('/api/audio?action=tone')\">播放测试音</button>"
"<button onclick=\"api('/api/audio?action=tone_long')\">30秒测试音</button>"
"<button onclick=\"audioMic()\">麦克风峰值</button>"
"<pre id=\"audio\">未测试</pre></section>"
"<section><h2>Qwen AI</h2>"
"<input id=\"qwenText\" style=\"width:190px\" value=\"报告当前小车状态\">"
"<button onclick=\"qwen()\">发送</button>"
"<button onclick=\"api('/api/capture')\">拍照给AI</button><br>"
"<button onclick=\"api('/api/qwen_control?action=start')\">启动</button>"
"<button onclick=\"api('/api/qwen_control?action=voice_start')\">语音开</button>"
"<button onclick=\"api('/api/qwen_control?action=voice_stop')\">语音关</button>"
"<button onclick=\"api('/api/qwen_control?action=stop')\">停止</button>"
"<button onclick=\"api('/api/qwen_control?action=restart')\">重启</button></section>"
"</main><script>"
"async function api(u){let r=await fetch(u,{method:'POST'});let t=await r.text();alert(t);load();return t}"
"function stop(){api('/api/stop')}"
"function move(d){api('/api/move?direction='+d+'&speed='+speed.value+'&duration='+duration.value)}"
"function gimbal(){api('/api/gimbal?pan='+pan.value+'&tilt='+tilt.value+'&duration=700')}"
"function qwen(){api('/api/qwen?text='+encodeURIComponent(qwenText.value))}"
"async function audioMic(){let r=await fetch('/api/audio?action=mic',{method:'POST'});audio.textContent=JSON.stringify(await r.json(),null,2)}"
"async function diag(){let r=await fetch('/api/diagnostics');status.textContent=JSON.stringify(await r.json(),null,2)}"
"async function camStatus(){let r=await fetch('/api/camera/status');camera.textContent=JSON.stringify(await r.json(),null,2)}"
"async function snapshot(){let w=open('/api/camera/snapshot?ts='+Date.now(),'_blank');if(!w)alert('浏览器拦截了弹窗')}"
"function restartCam(){cam.src='/api/camera/stream?ts='+Date.now()}"
"let refreshDelay=1000;"
"async function load(){let r=await fetch('/api/status');let j=await r.json();status.textContent=JSON.stringify(j,null,2);refreshDelay=(j.qwen&&j.qwen.audio_active)?5000:1000}"
"async function loop(){await load();await camStatus();setTimeout(loop,refreshDelay)}loop();"
"</script></body></html>";

static const char *INDEX_HTML_DESKTOP_OLD __attribute__((unused)) =
"<!doctype html><html><head><meta charset=\"utf-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no\">"
"<title>eagle fly 调试台</title>"
"<style>"
":root{font-family:Arial,'Microsoft YaHei',sans-serif;color:#17202a;background:#f4f6f8}"
"*{box-sizing:border-box}body{margin:0}header{height:48px;background:#1f2937;color:#fff;display:flex;align-items:center;justify-content:space-between;padding:0 14px;font-weight:700}"
"main{padding:12px;display:grid;gap:12px}.panel{background:#fff;border:1px solid #d7dee8;border-radius:6px;padding:12px}h2{font-size:16px;margin:0 0 10px}"
"pre{margin:0;background:#eef2f6;padding:10px;min-height:130px;overflow:auto;white-space:pre-wrap}.cam{width:100%;background:#111;aspect-ratio:4/3;object-fit:contain;border-radius:4px}"
"button{height:38px;margin:4px 4px 4px 0;border:1px solid #9aa4b2;background:#fff;border-radius:6px;font-weight:700;color:#111}.danger{background:#b91c1c;color:white;border-color:#b91c1c}.primary{background:#2563eb;color:white;border-color:#2563eb}"
".row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}.grow{flex:1}.kv{display:grid;grid-template-columns:82px 1fr;gap:6px;font-size:14px}.value{font-weight:700}"
"input[type=range]{width:100%}.joys{display:grid;grid-template-columns:1fr 1fr;gap:14px}.joy{height:160px;border-radius:50%;border:2px solid #94a3b8;background:#e8eef5;position:relative;touch-action:none}.knob{width:54px;height:54px;border-radius:50%;background:#2563eb;position:absolute;left:calc(50% - 27px);top:calc(50% - 27px);box-shadow:0 4px 12px #0004}.joylabel{text-align:center;font-weight:700;margin:6px 0}"
".mobileOnly{display:none}@media (min-width:760px){main{grid-template-columns:360px minmax(420px,760px)}.desktopOnly{display:block}.mobileOnly{display:none!important}.panel.controls{display:none}.cam{max-width:720px}}"
"@media (max-width:759px){header{position:sticky;top:0;z-index:2}.desktopOnly{display:none!important}.mobileOnly{display:block}main{display:block;padding:8px}.panel{margin-bottom:10px}.joy{height:42vw;min-height:145px;max-height:190px}.cam{max-height:42vh}.compactStatus{font-size:13px}}"
"</style></head><body><header><span>eagle fly</span><span id=\"net\">未连接</span></header><main>"
"<section class=\"panel\"><h2>传感器状态</h2><div id=\"compact\" class=\"kv compactStatus\"></div><pre id=\"status\" class=\"desktopOnly\">加载中...</pre></section>"
"<section class=\"panel\"><h2>摄像头</h2><img id=\"cam\" class=\"cam\" src=\"/api/camera/snapshot\" alt=\"camera stream\"><div class=\"row\"><button onclick=\"restartCam()\">重连预览</button><button onclick=\"snapshot()\">截图</button><button onclick=\"camStatus()\">相机状态</button></div><pre id=\"camera\" class=\"desktopOnly\">等待相机状态...</pre></section>"
"<section class=\"panel controls mobileOnly\"><h2>移动控制</h2><div class=\"row\"><span>速度</span><input id=\"speed\" class=\"grow\" type=\"range\" min=\"0\" max=\"60\" value=\"30\"><b id=\"speedText\">30</b><button class=\"danger\" onclick=\"stopAll()\">急停</button></div><div class=\"joys\"><div><div class=\"joylabel\">电机</div><div id=\"driveJoy\" class=\"joy\"><div id=\"driveKnob\" class=\"knob\"></div></div></div><div><div class=\"joylabel\">云台</div><div id=\"gimbalJoy\" class=\"joy\"><div id=\"gimbalKnob\" class=\"knob\"></div></div></div></div></section>"
"<section class=\"panel controls mobileOnly\"><h2>常用动作</h2><button onclick=\"behavior('dance')\">跳舞</button><button onclick=\"behavior('patrol')\" class=\"primary\">巡逻</button><button onclick=\"behavior('stop_patrol')\" class=\"danger\">停止巡逻</button></section>"
"<section class=\"panel controls mobileOnly\"><h2>AI</h2><div class=\"row\"><input id=\"qwenText\" class=\"grow\" value=\"报告当前小车状态\"><button onclick=\"qwen()\">发送</button></div><button onclick=\"api('/api/qwen_control?action=start')\">连接 AI</button><button onclick=\"api('/api/qwen_control?action=voice_start')\">开启语音</button><button onclick=\"api('/api/qwen_control?action=voice_stop')\">关闭语音</button><button onclick=\"api('/api/capture')\">拍照给 AI</button></section>"
"</main><script>"
"const $=id=>document.getElementById(id);let latest={};let driveTimer=null,driveDir=null;"
"async function api(u){let r=await fetch(u,{method:'POST'});let t=await r.text();load();return t}"
"function behavior(n){api('/api/behavior?name='+encodeURIComponent(n))}function qwen(){api('/api/qwen?text='+encodeURIComponent($('qwenText').value))}function stopAll(){api('/api/stop')}"
"function restartCam(){$('cam').src='/api/camera/snapshot?ts='+Date.now()}function snapshot(){open('/api/camera/snapshot?ts='+Date.now(),'_blank')}"
"async function camStatus(){let r=await fetch('/api/camera/status');$('camera').textContent=JSON.stringify(await r.json(),null,2)}"
"function setKnob(knob,x,y){knob.style.transform='translate('+x+'px,'+y+'px)'}function resetKnob(knob){knob.style.transform='translate(0,0)'}"
"function joyPoint(el,e){let r=el.getBoundingClientRect();let x=e.clientX-r.left-r.width/2;let y=e.clientY-r.top-r.height/2;let m=Math.min(r.width,r.height)/2-30;let d=Math.hypot(x,y);if(d>m){x=x/d*m;y=y/d*m}return{x,y,m}}"
"function driveFrom(x,y){if(Math.max(Math.abs(x),Math.abs(y))<22)return null;return Math.abs(y)>=Math.abs(x)?(y<0?'forward':'backward'):(x<0?'left':'right')}"
"function sendDrive(){if(!driveDir)return;api('/api/move?direction='+driveDir+'&speed='+$('speed').value+'&duration=700')}"
"function setupDrive(){let el=$('driveJoy'),knob=$('driveKnob');el.onpointerdown=e=>{el.setPointerCapture(e.pointerId);let p=joyPoint(el,e);setKnob(knob,p.x,p.y);driveDir=driveFrom(p.x,p.y);sendDrive();driveTimer=setInterval(sendDrive,300)};el.onpointermove=e=>{if(!driveTimer)return;let p=joyPoint(el,e);setKnob(knob,p.x,p.y);driveDir=driveFrom(p.x,p.y)};let up=e=>{if(driveTimer){clearInterval(driveTimer);driveTimer=null}driveDir=null;resetKnob(knob);api('/api/stop')};el.onpointerup=up;el.onpointercancel=up}"
"function setupGimbal(){let el=$('gimbalJoy'),knob=$('gimbalKnob');let last=0;el.onpointerdown=e=>el.setPointerCapture(e.pointerId);el.onpointermove=e=>{let now=Date.now();let p=joyPoint(el,e);setKnob(knob,p.x,p.y);if(now-last<180)return;last=now;let pan=Math.round(90+(p.x/p.m)*70);let tilt=Math.round(90+(p.y/p.m)*55);pan=Math.max(0,Math.min(180,pan));tilt=Math.max(30,Math.min(150,tilt));api('/api/gimbal?pan='+pan+'&tilt='+tilt+'&duration=220')};let up=e=>resetKnob(knob);el.onpointerup=up;el.onpointercancel=up}"
"$('speed').oninput=()=>{$('speedText').textContent=$('speed').value};"
"function compact(j){$('net').textContent=(j.network&&j.network.ip)?j.network.ip:'未联网';$('compact').innerHTML='<span>STM32</span><span class=value>'+j.stm32_online+'</span><span>距离</span><span class=value>'+j.distance_cm_x10+'</span><span>温湿度</span><span class=value>'+j.temperature+'/'+j.humidity+'</span><span>电机</span><span class=value>'+j.motor.state+' / '+j.motor.current_speed+'</span><span>云台</span><span class=value>'+j.gimbal.pan+' / '+j.gimbal.tilt+'</span><span>行为</span><span class=value>'+j.high_behavior.mode+' #'+j.high_behavior.step+'</span>'}"
"async function load(){let r=await fetch('/api/status');let j=await r.json();latest=j;compact(j);$('status').textContent=JSON.stringify(j,null,2)}"
"setupDrive();setupGimbal();setInterval(load,1000);setInterval(camStatus,3000);setInterval(restartCam,500);load();camStatus();"
"</script></body></html>";

static const char *INDEX_HTML =
"<!doctype html><html lang='zh-CN'><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>eagle fly 上位机</title>"
"<style>"
"*{box-sizing:border-box}body{margin:0;font-family:Arial,'Microsoft YaHei',sans-serif;background:#f3f5f7;color:#17202a}"
"header{height:56px;background:#18212f;color:#fff;display:flex;align-items:center;justify-content:space-between;padding:0 18px;font-weight:700}"
"main{height:calc(100vh - 56px);padding:12px;display:grid;grid-template-columns:310px minmax(440px,1fr) 330px;gap:12px;align-items:stretch}"
".panel{background:#fff;border:1px solid #d7dee8;border-radius:8px;padding:12px;min-width:0}.panel h2{font-size:16px;margin:0 0 12px}"
".stack{display:grid;gap:12px}.metric{border:1px solid #e0e6ee;border-radius:7px;padding:10px;background:#fbfcfd}.label{font-size:12px;color:#5f6b7a}.value{font-size:20px;font-weight:800;margin-top:4px}.small{font-size:13px;white-space:pre-wrap;word-break:break-word}"
".cameraPanel{display:grid;grid-template-rows:auto 1fr auto;gap:10px}.camBox{background:#111;border-radius:8px;display:grid;place-items:center;overflow:hidden}.cam{width:100%;height:100%;object-fit:contain;background:#111}"
".row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}button{height:38px;border:1px solid #9aa4b2;background:#fff;border-radius:7px;font-weight:700;color:#111;padding:0 12px}"
"button.primary{background:#0f766e;color:#fff;border-color:#0f766e}button.danger{background:#b91c1c;color:#fff;border-color:#b91c1c}button.active{background:#243447;color:#fff;border-color:#243447}button:disabled{opacity:.42}"
".seg{display:grid;grid-template-columns:1fr 1fr;gap:8px}.speed{width:100%}.modeLine{font-size:13px;color:#d7dee8}.manualOnly,.autoOnly{transition:opacity .15s}body.auto .manualOnly{opacity:.42;pointer-events:none}body.manual .autoOnly{opacity:.42;pointer-events:none}"
".keys{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin:10px 0}.key{height:54px;font-size:18px}.key.on{background:#0f766e;color:#fff;border-color:#0f766e}.empty{height:54px}"
"input[type=text]{height:38px;border:1px solid #9aa4b2;border-radius:7px;padding:0 10px;min-width:0;flex:1}pre{margin:0;background:#eef2f6;border-radius:7px;padding:10px;overflow:auto;max-height:180px;white-space:pre-wrap}"
"@media(max-width:980px){main{height:auto;grid-template-columns:1fr}.camBox{height:58vh}}"
"@media(min-width:981px){.camBox{min-height:0}}"
"</style></head><body class='auto'><header><div>eagle fly 上位机</div><div><span id='modeBadge'>智能模式</span><span class='modeLine'> ｜ </span><span id='ip'>未联网</span></div></header><main>"
"<section class='panel stack'><h2>车辆状态</h2>"
"<div class='metric'><div class='label'>GPS</div><div id='gps' class='value small'>--</div></div>"
"<div class='metric'><div class='label'>温度 / 湿度</div><div id='env' class='value'>--</div></div>"
"<div class='metric'><div class='label'>前方距离</div><div id='dist' class='value'>--</div></div>"
"<div class='metric'><div class='label'>电机</div><div id='motor' class='value small'>--</div></div>"
"<div class='metric'><div class='label'>云台</div><div id='gimbal' class='value small'>--</div></div>"
"<div class='metric'><div class='label'>AI / 行为</div><div id='ai' class='value small'>--</div></div>"
"</section>"
"<section class='panel cameraPanel'><h2>摄像头画面</h2><div class='camBox'><img id='cam' class='cam' src='/api/camera/snapshot' alt='camera'></div>"
"<div class='row'><button onclick='refreshCam()'>刷新画面</button><button onclick='snapshot()'>打开截图</button><button onclick='camStatus()'>相机状态</button></div><pre id='camera'>等待相机状态...</pre></section>"
"<section class='panel stack'><h2>控制</h2><div class='seg'><button id='autoBtn' onclick='setMode(\"auto\")'>智能</button><button id='manualBtn' onclick='setMode(\"manual\")'>手动</button></div>"
"<div class='autoOnly stack'><div class='row'><button class='primary' onclick='behavior(\"patrol\")'>巡逻</button><button onclick='behavior(\"dance\")'>跳舞</button><button class='danger' onclick='behavior(\"stop_patrol\")'>停止巡逻</button></div>"
"<div class='row'><input id='qwenText' type='text' value='报告当前小车状态'><button onclick='qwen()'>发送给 AI</button></div><div class='row'><button onclick='api(\"/api/qwen_control?action=start\")'>连接 AI</button><button onclick='api(\"/api/qwen_control?action=voice_start\")'>开启语音</button><button onclick='api(\"/api/qwen_control?action=voice_stop\")'>关闭语音</button></div></div>"
"<div class='manualOnly stack'><div><div class='row'><span>速度</span><input id='speed' class='speed' type='range' min='0' max='60' value='25'><b id='speedText'>25</b></div></div>"
"<div class='keys'><div></div><button class='key' data-k='w'>W</button><div></div><button class='key' data-k='a'>A</button><button class='key' data-k='s'>S</button><button class='key' data-k='d'>D</button></div>"
"<div class='row'><button class='danger' onclick='manualStop()'>手动停车</button><button onclick='manualMove(\"forward\")'>前进</button><button onclick='manualMove(\"backward\")'>后退</button><button onclick='manualMove(\"rotate_left\")'>左旋</button><button onclick='manualMove(\"rotate_right\")'>右旋</button></div></div>"
"<pre id='raw'>等待状态...</pre></section>"
"</main><script>"
"const $=id=>document.getElementById(id);let mode='auto';let pressed=new Set();let driveTimer=null;let lastDir=null;"
"async function api(u){let r=await fetch(u,{method:'POST'});let t=await r.text();await load();return t}"
"async function setMode(m){let r=await fetch('/api/control_mode?mode='+m,{method:'POST'});let j=await r.json();mode=j.mode||m;applyMode();await load()}"
"function applyMode(){document.body.className=mode;$('modeBadge').textContent=mode==='manual'?'手动模式':'智能模式';$('autoBtn').classList.toggle('active',mode==='auto');$('manualBtn').classList.toggle('active',mode==='manual')}"
"function behavior(n){api('/api/behavior?name='+encodeURIComponent(n))}function qwen(){api('/api/qwen?text='+encodeURIComponent($('qwenText').value))}"
"function refreshCam(){$('cam').src='/api/camera/snapshot?ts='+Date.now()}function snapshot(){open('/api/camera/snapshot?ts='+Date.now(),'_blank')}"
"async function camStatus(){let r=await fetch('/api/camera/status');$('camera').textContent=JSON.stringify(await r.json(),null,2)}"
"function fmtDist(v){return v===65535?'--':(v/10).toFixed(1)+' cm'}function gpsText(raw){if(!raw)return '无定位数据';return raw.replace(/\\r/g,'').trim()||'无定位数据'}"
"function updateKeyLights(){document.querySelectorAll('.key').forEach(b=>b.classList.toggle('on',pressed.has(b.dataset.k)))}"
"function direction(){let w=pressed.has('w'),a=pressed.has('a'),s=pressed.has('s'),d=pressed.has('d');if(w&&a)return 'left';if(w&&d)return 'right';if(a&&!w&&!s)return 'rotate_left';if(d&&!w&&!s)return 'rotate_right';if(w)return 'forward';if(s)return 'backward';return null}"
"function sendDrive(){if(mode!=='manual')return;let dir=direction();if(!dir){if(lastDir){fetch('/api/stop',{method:'POST'});lastDir=null}return}lastDir=dir;fetch('/api/move?direction='+dir+'&speed='+$('speed').value+'&duration=500',{method:'POST'})}"
"function startDrive(){if(!driveTimer)driveTimer=setInterval(sendDrive,260);sendDrive()}function stopDrive(){if(driveTimer){clearInterval(driveTimer);driveTimer=null}sendDrive()}"
"function manualMove(d){if(mode!=='manual')return;fetch('/api/move?direction='+d+'&speed='+$('speed').value+'&duration=500',{method:'POST'})}function manualStop(){pressed.clear();updateKeyLights();lastDir=null;fetch('/api/stop',{method:'POST'});stopDrive()}"
"window.addEventListener('keydown',e=>{let k=e.key.toLowerCase();if(!['w','a','s','d'].includes(k)||mode!=='manual')return;e.preventDefault();pressed.add(k);updateKeyLights();startDrive()});"
"window.addEventListener('keyup',e=>{let k=e.key.toLowerCase();if(!['w','a','s','d'].includes(k))return;e.preventDefault();pressed.delete(k);updateKeyLights();if(!direction())stopDrive();else sendDrive()});"
"document.querySelectorAll('.key').forEach(b=>{let k=b.dataset.k;b.onpointerdown=e=>{pressed.add(k);updateKeyLights();startDrive();b.setPointerCapture(e.pointerId)};b.onpointerup=e=>{pressed.delete(k);updateKeyLights();if(!direction())stopDrive();else sendDrive()};b.onpointercancel=b.onpointerup});"
"$('speed').oninput=()=>{$('speedText').textContent=$('speed').value};"
"async function load(){let r=await fetch('/api/status');let j=await r.json();mode=j.control_mode||mode;applyMode();$('ip').textContent=(j.network&&j.network.ip)?j.network.ip:'未联网';$('gps').textContent=gpsText(j.gps);$('env').textContent=j.temperature+'°C / '+j.humidity+'%';$('dist').textContent=fmtDist(j.distance_cm_x10);$('motor').textContent='状态 '+j.motor.state+'  目标 '+j.motor.target_speed+'  当前 '+j.motor.current_speed;$('gimbal').textContent='pan '+j.gimbal.pan+'  tilt '+j.gimbal.tilt;$('ai').textContent=j.qwen.state_name+'  语音 '+(j.qwen.voice?'开':'关')+'\\n行为 '+j.high_behavior.mode+' #'+j.high_behavior.step;$('raw').textContent=JSON.stringify(j,null,2)}"
"setInterval(load,1000);setInterval(camStatus,3000);setInterval(refreshCam,900);load();camStatus();"
"</script></body></html>";

static esp_err_t send_text(httpd_req_t *req, const char *type, const char *text)
{
    httpd_resp_set_type(req, type);
    return httpd_resp_send(req, text, HTTPD_RESP_USE_STRLEN);
}

static int query_int(httpd_req_t *req, const char *key, int default_value)
{
    char query[192];
    char value[32];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return default_value;
    }
    if (httpd_query_key_value(query, key, value, sizeof(value)) != ESP_OK) {
        return default_value;
    }
    return atoi(value);
}

static int hex_value(char c)
{
    if ((c >= '0') && (c <= '9')) {
        return c - '0';
    }
    if ((c >= 'a') && (c <= 'f')) {
        return c - 'a' + 10;
    }
    if ((c >= 'A') && (c <= 'F')) {
        return c - 'A' + 10;
    }
    return -1;
}

static void url_decode_in_place(char *text)
{
    char *src = text;
    char *dst = text;

    if (text == NULL) {
        return;
    }

    while (*src != '\0') {
        if ((src[0] == '%') && (hex_value(src[1]) >= 0) && (hex_value(src[2]) >= 0)) {
            *dst++ = (char)((hex_value(src[1]) << 4) | hex_value(src[2]));
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static void query_text(httpd_req_t *req, const char *key, char *out, size_t out_size, const char *default_value)
{
    char query[256];
    if (out_size == 0) {
        return;
    }
    strncpy(out, default_value, out_size - 1);
    out[out_size - 1] = '\0';

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return;
    }
    if (httpd_query_key_value(query, key, out, out_size) == ESP_OK) {
        url_decode_in_place(out);
    }
}

static esp_err_t send_simple_result(httpd_req_t *req, esp_err_t ret)
{
    char json[96];
    snprintf(json, sizeof(json), "{\"ok\":%s,\"esp_err\":%d}", (ret == ESP_OK) ? "true" : "false", ret);
    return send_text(req, "application/json", json);
}

static esp_err_t set_control_mode(bool manual)
{
    s_manual_mode = manual;

    if (manual) {
        behavior_manager_stop();
        qwen_realtime_client_set_voice_enabled(false);
        qwen_realtime_client_stop();
        audio_codec_play_silence(80);
        audio_codec_set_speaker_enabled(false);
        ESP_LOGI(TAG, "control mode changed to manual");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "control mode changed to auto");
    if (network_manager_is_connected()) {
        esp_err_t ret = qwen_realtime_client_start();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "qwen start on auto mode failed: %s", esp_err_to_name(ret));
        }
    }
    return ESP_OK;
}

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return send_text(req, "text/html; charset=utf-8", INDEX_HTML);
}

static esp_err_t status_handler(httpd_req_t *req)
{
    sensor_cache_snapshot_t st = robot_tools_get_status();
    behavior_manager_status_t bs;
    behavior_manager_get_status(&bs);
    char json[1280];
    snprintf(json, sizeof(json),
             "{\"control_mode\":\"%s\",\"stm32_online\":%s,\"flags\":%u,\"distance_cm_x10\":%u,"
             "\"network\":{\"backend\":\"%s\",\"started\":%s,\"connected\":%s,\"ip\":\"%s\"},"
             "\"qwen\":{\"state\":%u,\"state_name\":\"%s\",\"ready\":%s,\"voice\":%s,\"audio_active\":%s},"
             "\"temperature\":%u,\"humidity\":%u,\"dht_ok\":%s,"
             "\"motor\":{\"state\":%u,\"target_speed\":%u,\"current_speed\":%u},"
             "\"gimbal\":{\"pan\":%u,\"tilt\":%u,\"moving\":%s,\"action_id\":%u},"
             "\"behavior\":{\"id\":%u,\"running\":%s,\"step\":%u},"
             "\"high_behavior\":{\"mode\":\"%s\",\"running\":%s,\"step\":%u,\"step_elapsed_ms\":%lu,"
             "\"loops\":%lu,\"vision_failures\":%u,\"last_decision\":\"%s\",\"last_error\":\"%s\"},"
             "\"gps\":\"%s\"}",
             s_manual_mode ? "manual" : "auto",
             st.basic.stm32_online ? "true" : "false",
             st.basic.flags,
             st.sensor.distance_cm_x10,
             network_manager_backend_name(),
             network_manager_is_started() ? "true" : "false",
             network_manager_is_connected() ? "true" : "false",
             network_manager_ip_string(),
             (unsigned)qwen_realtime_client_get_state(),
             qwen_realtime_client_state_name(),
             qwen_realtime_client_is_connected() ? "true" : "false",
             qwen_realtime_client_is_voice_enabled() ? "true" : "false",
             qwen_realtime_client_is_audio_output_active() ? "true" : "false",
             st.sensor.temperature,
             st.sensor.humidity,
             st.sensor.dht_ok ? "true" : "false",
             st.motor.state,
             st.motor.target_speed,
             st.motor.current_speed,
             st.gimbal.pan,
             st.gimbal.tilt,
             st.gimbal.moving ? "true" : "false",
             st.gimbal.action_id,
             st.behavior.behavior_id,
             st.behavior.running ? "true" : "false",
             st.behavior.step,
             bs.mode_name,
             bs.running ? "true" : "false",
             bs.step,
             (unsigned long)bs.step_elapsed_ms,
             (unsigned long)bs.loops,
             bs.vision_failures,
             bs.last_decision,
             bs.last_error,
             st.gps_nmea);
    return send_text(req, "application/json", json);
}

static esp_err_t control_mode_handler(httpd_req_t *req)
{
    char mode[16];
    query_text(req, "mode", mode, sizeof(mode), s_manual_mode ? "manual" : "auto");

    esp_err_t ret;
    if (strcmp(mode, "manual") == 0) {
        ret = set_control_mode(true);
    } else if (strcmp(mode, "auto") == 0) {
        ret = set_control_mode(false);
    } else {
        ret = ESP_ERR_INVALID_ARG;
    }

    char json[128];
    snprintf(json, sizeof(json),
             "{\"ok\":%s,\"mode\":\"%s\",\"esp_err\":%d}",
             (ret == ESP_OK) ? "true" : "false",
             s_manual_mode ? "manual" : "auto",
             ret);
    return send_text(req, "application/json", json);
}

static esp_err_t manual_direction(const char *direction, stm32_link_move_t *out)
{
    if ((direction == NULL) || (out == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strcmp(direction, "forward") == 0) {
        *out = STM32_MOVE_FORWARD;
    } else if (strcmp(direction, "backward") == 0) {
        *out = STM32_MOVE_BACKWARD;
    } else if (strcmp(direction, "left") == 0) {
        *out = STM32_MOVE_TURN_LEFT;
    } else if (strcmp(direction, "right") == 0) {
        *out = STM32_MOVE_TURN_RIGHT;
    } else if (strcmp(direction, "rotate_left") == 0) {
        *out = STM32_MOVE_ROTATE_LEFT;
    } else if (strcmp(direction, "rotate_right") == 0) {
        *out = STM32_MOVE_ROTATE_RIGHT;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static esp_err_t diagnostics_handler(httpd_req_t *req)
{
    char json[1280];
    board_diagnostics_write_json(json, sizeof(json));
    return send_text(req, "application/json", json);
}

static esp_err_t camera_status_handler(httpd_req_t *req)
{
    robot_camera_status_t cam;
    robot_camera_get_status(&cam);
    char fourcc[5] = {0};
    char json[384];
    snprintf(json, sizeof(json),
             "{\"initialized\":%s,\"ready\":%s,\"device\":\"%s\",\"width\":%lu,\"height\":%lu,"
             "\"format\":\"%s\",\"fps\":%lu,\"last_frame_ms\":%lu,\"frames\":%lu,"
             "\"dropped_frames\":%lu,\"stream_clients\":%lu,\"last_error\":\"%s\"}",
             cam.initialized ? "true" : "false",
             cam.ready ? "true" : "false",
             cam.device,
             (unsigned long)cam.width,
             (unsigned long)cam.height,
             robot_camera_fourcc_string(cam.pixfmt, fourcc),
             (unsigned long)cam.fps,
             (unsigned long)cam.last_frame_ms,
             (unsigned long)cam.frames,
             (unsigned long)cam.dropped_frames,
             (unsigned long)cam.stream_clients,
             esp_err_to_name(cam.last_error));
    return send_text(req, "application/json", json);
}

static esp_err_t camera_snapshot_handler(httpd_req_t *req)
{
    uint8_t *jpeg = NULL;
    size_t jpeg_len = 0;
    esp_err_t ret = robot_camera_capture_jpeg(&jpeg, &jpeg_len, 1500);
    if (ret != ESP_OK) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_simple_result(req, ret);
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    ret = httpd_resp_send(req, (const char *)jpeg, jpeg_len);
    robot_camera_release_jpeg(jpeg);
    return ret;
}

static bool camera_stream_slow_mode(void)
{
    return qwen_realtime_client_is_audio_output_active();
}

static esp_err_t camera_stream_handler(httpd_req_t *req)
{
    (void)camera_stream_slow_mode;
    return camera_snapshot_handler(req);
}

static esp_err_t stop_handler(httpd_req_t *req)
{
    return send_simple_result(req, robot_tools_stop());
}

static esp_err_t move_handler(httpd_req_t *req)
{
    char direction[24];
    query_text(req, "direction", direction, sizeof(direction), "forward");
    uint8_t speed = (uint8_t)query_int(req, "speed", 30);
    uint16_t duration = (uint16_t)query_int(req, "duration", 1800);

    if (s_manual_mode) {
        stm32_link_move_t dir;
        esp_err_t ret = manual_direction(direction, &dir);
        if (ret != ESP_OK) {
            return send_simple_result(req, ret);
        }
        return send_simple_result(req, stm32_link_move(dir, speed, duration));
    }

    return send_simple_result(req, robot_tools_move(direction, speed, duration));
}

static esp_err_t gimbal_handler(httpd_req_t *req)
{
    uint8_t pan = (uint8_t)query_int(req, "pan", 90);
    uint8_t tilt = (uint8_t)query_int(req, "tilt", 90);
    uint16_t duration = (uint16_t)query_int(req, "duration", 700);
    return send_simple_result(req, robot_tools_gimbal_set(pan, tilt, duration));
}

static esp_err_t led_handler(httpd_req_t *req)
{
    char effect[24];
    query_text(req, "effect", effect, sizeof(effect), "idle");
    return send_simple_result(req, robot_tools_led_effect(effect, 0, 0, 0, 0));
}

static esp_err_t behavior_handler(httpd_req_t *req)
{
    if (s_manual_mode) {
        return send_simple_result(req, ESP_ERR_INVALID_STATE);
    }

    char name[32];
    query_text(req, "name", name, sizeof(name), "patrol");
    return send_simple_result(req, robot_tools_behavior(name));
}

static esp_err_t audio_handler(httpd_req_t *req)
{
    if (s_manual_mode) {
        audio_codec_set_speaker_enabled(false);
        return send_simple_result(req, ESP_ERR_INVALID_STATE);
    }

    char action[24];
    query_text(req, "action", action, sizeof(action), "status");

    if (strcmp(action, "tone") == 0) {
        audio_codec_play_test_tone();
        return send_text(req, "application/json", "{\"ok\":true,\"action\":\"tone\"}");
    }
    if (strcmp(action, "tone_long") == 0) {
        audio_codec_play_1khz_test_tone(30000);
        return send_text(req, "application/json", "{\"ok\":true,\"action\":\"tone_long\",\"duration_ms\":30000}");
    }

    int16_t peak = 0;
    size_t samples_read = 0;
    esp_err_t ret = audio_codec_measure_mic_peak(&peak, &samples_read, 300);
    char json[128];
    snprintf(json, sizeof(json),
             "{\"ok\":%s,\"action\":\"mic\",\"peak\":%d,\"samples\":%u,\"esp_err\":%d}",
             (ret == ESP_OK) ? "true" : "false",
             peak,
             (unsigned)samples_read,
             ret);
    return send_text(req, "application/json", json);
}

static esp_err_t qwen_handler(httpd_req_t *req)
{
    if (s_manual_mode) {
        return send_simple_result(req, ESP_ERR_INVALID_STATE);
    }

    char text[192];
    query_text(req, "text", text, sizeof(text), "你好");
    return send_simple_result(req, qwen_realtime_client_send_text(text));
}

static esp_err_t qwen_control_handler(httpd_req_t *req)
{
    char action[24];
    query_text(req, "action", action, sizeof(action), "status");

    esp_err_t ret = ESP_OK;
    if (s_manual_mode && (strcmp(action, "stop") != 0) && (strcmp(action, "voice_stop") != 0)) {
        ret = ESP_ERR_INVALID_STATE;
    } else if (strcmp(action, "start") == 0) {
        ret = qwen_realtime_client_start();
    } else if (strcmp(action, "voice_start") == 0) {
        ret = qwen_realtime_client_set_voice_enabled(true);
    } else if (strcmp(action, "voice_stop") == 0) {
        ret = qwen_realtime_client_set_voice_enabled(false);
    } else if (strcmp(action, "audio_test") == 0) {
        ret = qwen_realtime_client_play_local_audio_test(3000);
    } else if (strcmp(action, "stop") == 0) {
        ret = qwen_realtime_client_stop();
    } else if (strcmp(action, "restart") == 0) {
        ret = qwen_realtime_client_stop();
        if (ret == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(300));
            ret = qwen_realtime_client_start();
        }
    }

    char json[160];
    snprintf(json, sizeof(json),
             "{\"ok\":%s,\"state\":%u,\"state_name\":\"%s\",\"ready\":%s,\"voice\":%s,\"esp_err\":%d}",
             (ret == ESP_OK) ? "true" : "false",
             (unsigned)qwen_realtime_client_get_state(),
             qwen_realtime_client_state_name(),
             qwen_realtime_client_is_connected() ? "true" : "false",
             qwen_realtime_client_is_voice_enabled() ? "true" : "false",
             ret);
    return send_text(req, "application/json", json);
}

static esp_err_t capture_handler(httpd_req_t *req)
{
    if (s_manual_mode) {
        return send_simple_result(req, ESP_ERR_INVALID_STATE);
    }

    return send_simple_result(req, robot_tools_capture_image());
}

static void add_uri(httpd_handle_t server, const char *uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *))
{
    httpd_uri_t cfg = {
        .uri = uri,
        .method = method,
        .handler = handler,
        .user_ctx = NULL,
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &cfg));
}

esp_err_t web_debug_start(void)
{
    if (s_server != NULL) {
        return ESP_OK;
    }

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.max_uri_handlers = 24;
    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &cfg), TAG, "httpd_start failed");

    add_uri(s_server, "/", HTTP_GET, index_handler);
    add_uri(s_server, "/api/status", HTTP_GET, status_handler);
    add_uri(s_server, "/api/control_mode", HTTP_POST, control_mode_handler);
    add_uri(s_server, "/api/diagnostics", HTTP_GET, diagnostics_handler);
    add_uri(s_server, "/api/camera/status", HTTP_GET, camera_status_handler);
    add_uri(s_server, "/api/camera/snapshot", HTTP_GET, camera_snapshot_handler);
    add_uri(s_server, "/api/camera/stream", HTTP_GET, camera_stream_handler);
    add_uri(s_server, "/api/stop", HTTP_POST, stop_handler);
    add_uri(s_server, "/api/move", HTTP_POST, move_handler);
    add_uri(s_server, "/api/gimbal", HTTP_POST, gimbal_handler);
    add_uri(s_server, "/api/led", HTTP_POST, led_handler);
    add_uri(s_server, "/api/behavior", HTTP_POST, behavior_handler);
    add_uri(s_server, "/api/audio", HTTP_POST, audio_handler);
    add_uri(s_server, "/api/qwen", HTTP_POST, qwen_handler);
    add_uri(s_server, "/api/qwen_control", HTTP_POST, qwen_control_handler);
    add_uri(s_server, "/api/capture", HTTP_POST, capture_handler);

    ESP_LOGI(TAG, "web debug server started");
    return ESP_OK;
}
