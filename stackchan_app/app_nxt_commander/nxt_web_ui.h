#pragma once

static const char NXT_INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
  <title>Stack-chan NXT Commander</title>
  <style>
    * { box-sizing: border-box; touch-action: manipulation; user-select: none; -webkit-user-select: none; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
      background: #121217; color: #f0f0f5; margin: 0; padding: 16px;
      display: flex; flex-direction: column; align-items: center; justify-content: center; min-height: 100vh;
    }
    .header { text-align: center; margin-bottom: 12px; }
    h1 { font-size: 1.4rem; margin: 0 0 6px 0; }
    .status-badge {
      display: inline-block; padding: 5px 14px; border-radius: 20px; font-size: 0.85rem;
      background: #2a2a35; color: #888; font-weight: 500; transition: all 0.3s;
    }
    .status-badge.connected { background: #133926; color: #4ade80; border: 1px solid #22c55e; }
    .slider-card {
      background: #1c1c24; padding: 14px 20px; border-radius: 16px; width: 100%; max-width: 300px;
      margin: 12px 0; border: 1px solid #2e2e3d;
    }
    .slider-card label { display: flex; justify-content: space-between; font-size: 0.9rem; margin-bottom: 8px; color: #bbb; }
    input[type=range] { width: 100%; height: 6px; border-radius: 3px; accent-color: #00adb5; }
    .dpad {
      display: grid; grid-template-columns: repeat(3, 85px); grid-template-rows: repeat(3, 85px);
      gap: 12px; margin: 12px 0;
    }
    .btn {
      background: #22222d; border: 2px solid #333344; border-radius: 20px; color: #fff;
      font-size: 1.8rem; display: flex; align-items: center; justify-content: center;
      cursor: pointer; transition: transform 0.08s, background 0.08s;
    }
    .btn:active { background: #00adb5; border-color: #00fff5; transform: scale(0.95); }
    .btn-stop { background: #7f1d1d; border-color: #991b1b; font-size: 1.1rem; font-weight: bold; }
    .btn-stop:active { background: #dc2626; border-color: #ef4444; }
    .actions { display: flex; gap: 10px; width: 100%; max-width: 300px; margin-top: 10px; }
    .action-btn {
      flex: 1; padding: 12px; border-radius: 14px; border: 1px solid #3d3d4e; background: #22222d;
      color: #fff; font-size: 0.95rem; font-weight: 600; cursor: pointer;
    }
    .action-btn:active { background: #00adb5; border-color: #00fff5; }
    .hint { font-size: 0.75rem; color: #666; margin-top: 14px; text-align: center; }
  </style>
</head>
<body>
  <div class="header">
    <h1>🤖 Stack-chan NXT Commander</h1>
    <div id="status" class="status-badge">○ Waiting for NXT USB...</div>
  </div>

  <div class="slider-card">
    <label><span>Motor Speed</span><span id="speedVal" style="color:#00adb5;font-weight:bold;">75%</span></label>
    <input type="range" id="speed" min="30" max="100" value="75" oninput="speedVal.innerText = this.value + '%'">
  </div>

  <div class="dpad">
    <div></div>
    <div class="btn" data-dir="fwd">▲</div>
    <div></div>
    <div class="btn" data-dir="left">◀</div>
    <div class="btn btn-stop" data-dir="stop">STOP</div>
    <div class="btn" data-dir="right">▶</div>
    <div></div>
    <div class="btn" data-dir="rev">▼</div>
    <div></div>
  </div>

  <div class="actions">
    <button class="action-btn" onclick="playBeep()">🔊 Beep</button>
    <button class="action-btn" onclick="playTada()">🎉 Fanfare</button>
  </div>

  <div class="hint">Left: Port B | Right: Port C | Keys: WASD / Arrows / Space</div>

  <script>
    let activeDir = null, timer = null;
    function getSpeed() { return parseInt(document.getElementById('speed').value); }
    function sendCmd(url) { fetch(url).catch(() => {}); }

    function executeMove(dir) {
      const s = getSpeed();
      if (dir === 'fwd') sendCmd(`/api/drive?l=${s}&r=${s}`);
      else if (dir === 'rev') sendCmd(`/api/drive?l=${-s}&r=${-s}`);
      else if (dir === 'left') sendCmd(`/api/drive?l=${-s}&r=${s}`);
      else if (dir === 'right') sendCmd(`/api/drive?l=${s}&r=${-s}`);
      else sendCmd('/api/stop');
    }

    function startDrive(dir) {
      if (activeDir === dir) return;
      activeDir = dir;
      executeMove(dir);
      if (timer) clearInterval(timer);
      if (dir !== 'stop') timer = setInterval(() => executeMove(dir), 180);
    }

    function stopDrive() {
      if (timer) { clearInterval(timer); timer = null; }
      activeDir = null;
      sendCmd('/api/stop');
    }

    document.querySelectorAll('.btn').forEach(btn => {
      const dir = btn.getAttribute('data-dir');
      btn.addEventListener('pointerdown', (e) => { e.preventDefault(); startDrive(dir); });
      btn.addEventListener('pointerup', (e) => { e.preventDefault(); if (dir !== 'stop') stopDrive(); });
      btn.addEventListener('pointercancel', stopDrive);
    });

    window.addEventListener('keydown', (e) => {
      if (e.repeat) return;
      if (e.key === 'ArrowUp' || e.key === 'w' || e.key === 'W') startDrive('fwd');
      else if (e.key === 'ArrowDown' || e.key === 's' || e.key === 'S') startDrive('rev');
      else if (e.key === 'ArrowLeft' || e.key === 'a' || e.key === 'A') startDrive('left');
      else if (e.key === 'ArrowRight' || e.key === 'd' || e.key === 'D') startDrive('right');
      else if (e.key === ' ') stopDrive();
    });

    window.addEventListener('keyup', (e) => {
      if (['ArrowUp','ArrowDown','ArrowLeft','ArrowRight','w','W','a','A','s','S','d','D'].includes(e.key)) stopDrive();
    });

    function playBeep() { sendCmd('/api/tone?f=523&d=200'); }
    function playTada() {
      sendCmd('/api/tone?f=440&d=120');
      setTimeout(() => sendCmd('/api/tone?f=554&d=120'), 130);
      setTimeout(() => sendCmd('/api/tone?f=659&d=260'), 260);
    }

    setInterval(() => {
      fetch('/api/status').then(r => r.json()).then(d => {
        const el = document.getElementById('status');
        if (d.nxt) { el.innerText = '● Lego NXT Connected'; el.className = 'status-badge connected'; }
        else if (d.host) { el.innerText = '○ Waiting for NXT USB...'; el.className = 'status-badge'; }
        else { el.innerText = '○ Unplug PC USB to start NXT Host'; el.className = 'status-badge'; }
      }).catch(() => {});
    }, 1500);
  </script>
</body>
</html>
)rawliteral";
