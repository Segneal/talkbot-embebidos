// Talkbot Web Control

const $ = (id) => document.getElementById(id);

// Volumen (slider step=10)
let sliderActive = false;
let volumeTimeout = null;

$('volume-slider').addEventListener('input', function() {
  sliderActive = true;
  $('volume-value').textContent = this.value + '%';

  clearTimeout(volumeTimeout);
  volumeTimeout = setTimeout(() => {
    fetch('/api/volume', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ volume: parseInt(this.value) })
    }).then(() => {
      sliderActive = false;
    }).catch(() => {
      sliderActive = false;
    });
  }, 200);
});

$('volume-slider').addEventListener('change', function() {
  sliderActive = false;
});

// Agente
function fetchConfig() {
  fetch('/api/config')
    .then(r => r.json())
    .then(data => {
      document.querySelectorAll('.agent-card').forEach(card => {
        card.classList.toggle('selected', card.dataset.agent === data.agent);
      });
    })
    .catch(() => {});
}

document.querySelectorAll('.agent-card').forEach(card => {
  card.addEventListener('click', function() {
    const agent = this.dataset.agent;
    document.querySelectorAll('.agent-card').forEach(c => c.classList.remove('selected'));
    this.classList.add('selected');

    fetch('/api/config', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ agent: agent })
    }).catch(() => {});
  });
});

fetchConfig();

// Tabs
document.querySelectorAll('.tab').forEach(tab => {
  tab.addEventListener('click', function() {
    document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
    document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
    this.classList.add('active');
    document.getElementById('tab-' + this.dataset.tab).classList.add('active');
  });
});

// Backend URL
function fetchBackendUrl() {
  fetch('/api/config')
    .then(r => r.json())
    .then(data => {
      if (data.backendUrl) $('backend-url').value = data.backendUrl;
    })
    .catch(() => {});
}

$('save-backend').addEventListener('click', function() {
  const url = $('backend-url').value.trim();
  if (!url) return;

  fetch('/api/config', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ backendUrl: url })
  }).then(r => r.json())
    .then(() => {
      cachedBackendUrl = url;
      this.textContent = 'Guardado!';
      setTimeout(() => { this.textContent = 'Guardar URL'; }, 1500);
    })
    .catch(() => {
      this.textContent = 'Error';
      setTimeout(() => { this.textContent = 'Guardar URL'; }, 1500);
    });
});

fetchBackendUrl();

// WiFi Reset
$('wifi-reset-btn').addEventListener('click', function() {
  if (!confirm('Cambiar WiFi? El dispositivo se reiniciará en modo configuración.')) return;
  fetch('/api/wifi-reset', { method: 'POST' })
    .then(() => {
      this.textContent = 'Reiniciando...';
      this.disabled = true;
    })
    .catch(() => {});
});

// Reboot
$('reboot-btn').addEventListener('click', function() {
  if (!confirm('Reiniciar Talkbot?')) return;
  fetch('/api/reboot', { method: 'POST' })
    .then(() => {
      this.textContent = 'Reiniciando...';
      this.disabled = true;
    })
    .catch(() => {});
});

// === Speaker: reproducir audio del backend ===
let speakerCount = 0;
let cachedBackendUrl = '';

function addChatMsg(role, text) {
  const log = $('chat-log');
  const msg = document.createElement('div');
  msg.className = 'chat-msg ' + (role === 'user' ? 'chat-user' : 'chat-bot');
  const label = document.createElement('div');
  label.className = 'chat-label';
  label.textContent = role === 'user' ? 'Tú' : 'Talkbot';
  const content = document.createElement('div');
  content.className = 'chat-text';
  content.textContent = text;
  msg.appendChild(label);
  msg.appendChild(content);
  log.appendChild(msg);
  log.scrollTop = log.scrollHeight;
}

// Obtener backend URL una sola vez (no depender del ESP32 en cada poll)
function initBackendUrl() {
  fetch('/api/config')
    .then(r => r.json())
    .then(data => {
      if (data.backendUrl) cachedBackendUrl = data.backendUrl;
    })
    .catch(function(){});
}

function pollSpeaker() {
  if (!cachedBackendUrl) { initBackendUrl(); return; }
  fetch(cachedBackendUrl + '/api/response-count')
    .then(r => r.json())
    .then(data => {
      if (data.count > speakerCount) {
        speakerCount = data.count;

        if (data.lastUser) addChatMsg('user', data.lastUser);
        if (data.lastBot) addChatMsg('bot', data.lastBot);

        $('speaker-dot').className = 'dot speaking';
        $('speaker-status').textContent = 'Reproduciendo...';

        var audio = new Audio(cachedBackendUrl + '/api/last-audio?' + Date.now());
        audio.onended = function() {
          $('speaker-dot').className = 'dot idle';
          $('speaker-status').textContent = 'Esperando respuesta...';
        };
        audio.onerror = function() {
          $('speaker-dot').className = 'dot idle';
          $('speaker-status').textContent = 'Esperando respuesta...';
        };
        audio.play().catch(function(){});
      }
    })
    .catch(function(){});
}

// === TFT Display Virtual ===
let tftScreen = 0;
const TFT_SCREENS = 6;
const screenNames = ['ESTADO', 'VOLUMEN', 'WIFI', 'CHAT', 'STATS', 'VU METER'];

function renderTftDots() {
  var dots = $('tft-dots');
  dots.textContent = '';
  for (var i = 0; i < TFT_SCREENS; i++) {
    var d = document.createElement('span');
    d.className = 'tft-dot' + (i === tftScreen ? ' active' : '');
    dots.appendChild(d);
  }
}

function stateToColor(state) {
  var colors = { idle:'#00ffff', listening:'#00e676', processing:'#ffeb3b', speaking:'#2196f3', error:'#f44336' };
  return colors[state] || '#fff';
}

function stateToEmoji(state) {
  var emojis = { idle:'\u23F8', listening:'\uD83C\uDFA4', processing:'\u23F3', speaking:'\uD83D\uDD0A', error:'\u2716' };
  return emojis[state] || '?';
}

function stateToLabel(state) {
  var labels = { idle:'LISTO', listening:'ESCUCHANDO', processing:'PROCESANDO', speaking:'HABLANDO', error:'ERROR' };
  return labels[state] || state;
}

function escapeText(str) {
  var div = document.createElement('div');
  div.textContent = str;
  return div.textContent;
}

function buildTftScreen(data) {
  var el = $('tft-screen');
  el.textContent = '';

  // Header
  var header = document.createElement('div');
  header.className = 'tft-header';
  header.textContent = screenNames[tftScreen];
  el.appendChild(header);

  var body = document.createElement('div');
  body.className = 'tft-body';

  switch (tftScreen) {
    case 0: { // ESTADO
      var col = stateToColor(data.state);
      var icon = document.createElement('div');
      icon.className = 'tft-state-icon';
      icon.style.color = col;
      icon.textContent = stateToEmoji(data.state);
      body.appendChild(icon);
      var label = document.createElement('div');
      label.className = 'tft-state-label';
      label.style.color = col;
      label.textContent = stateToLabel(data.state);
      body.appendChild(label);
      el.appendChild(body);
      var info = document.createElement('div');
      info.className = 'tft-info-dim';
      info.textContent = 'WiFi: ' + data.rssi + 'dBm  Heap: ' + Math.round(data.freeHeap/1024) + 'KB';
      el.appendChild(info);
      break;
    }
    case 1: { // VOLUMEN
      var pct = data.volume;
      var barCol = pct > 80 ? '#f44336' : (pct > 50 ? '#ffeb3b' : '#00e676');
      var big = document.createElement('div');
      big.className = 'tft-vol-big';
      big.textContent = pct + '%';
      body.appendChild(big);
      var barC = document.createElement('div');
      barC.className = 'tft-bar-container';
      var barF = document.createElement('div');
      barF.className = 'tft-bar-fill';
      barF.style.width = pct + '%';
      barF.style.background = barCol;
      barC.appendChild(barF);
      body.appendChild(barC);
      var labels = document.createElement('div');
      labels.className = 'tft-bar-labels';
      var l0 = document.createElement('span'); l0.textContent = '0';
      var l1 = document.createElement('span'); l1.textContent = '100';
      labels.appendChild(l0); labels.appendChild(l1);
      body.appendChild(labels);
      el.appendChild(body);
      break;
    }
    case 2: { // WIFI
      var rssi = data.rssi || -100;
      var bars = rssi > -50 ? 4 : (rssi > -60 ? 3 : (rssi > -70 ? 2 : (rssi > -80 ? 1 : 0)));
      var ssid = document.createElement('div');
      ssid.className = 'tft-wifi-ssid';
      ssid.textContent = data.ssid || '?';
      body.appendChild(ssid);
      var barsDiv = document.createElement('div');
      barsDiv.className = 'tft-signal-bars';
      for (var i = 0; i < 4; i++) {
        var b = document.createElement('div');
        b.className = 'tft-signal-bar' + (i < bars ? ' active' : '');
        b.style.height = (12 + i * 12) + 'px';
        barsDiv.appendChild(b);
      }
      body.appendChild(barsDiv);
      var det = document.createElement('div');
      det.className = 'tft-wifi-detail';
      det.textContent = rssi + ' dBm';
      body.appendChild(det);
      var ip = document.createElement('div');
      ip.className = 'tft-wifi-ip';
      ip.textContent = data.ip || '?';
      body.appendChild(ip);
      el.appendChild(body);
      break;
    }
    case 3: { // CHAT
      var q = data.lastQuestion || '';
      var a = data.lastAnswer || '';
      if (!q) {
        body.style.textAlign = 'center';
        body.style.color = '#777';
        body.style.paddingTop = '60px';
        body.textContent = 'Sin conversacion';
      } else {
        var ql = document.createElement('div');
        ql.className = 'tft-chat-label'; ql.textContent = 'Tu:';
        body.appendChild(ql);
        var qt = document.createElement('div');
        qt.className = 'tft-chat-text tft-chat-user'; qt.textContent = q;
        body.appendChild(qt);
        var bl = document.createElement('div');
        bl.className = 'tft-chat-label'; bl.style.marginTop = '8px'; bl.textContent = 'Bot:';
        body.appendChild(bl);
        var bt = document.createElement('div');
        bt.className = 'tft-chat-text tft-chat-bot'; bt.textContent = a;
        body.appendChild(bt);
      }
      el.appendChild(body);
      break;
    }
    case 4: { // STATS
      var upH = Math.floor(data.uptime / 3600);
      var upM = Math.floor((data.uptime % 3600) / 60);
      var stats = [
        ['Conversaciones:', (data.conversations || 0)],
        ['Latencia prom:', data.avgLatency ? data.avgLatency + 'ms' : '--'],
        ['Uptime:', upH + 'h ' + upM + 'm'],
        ['Heap libre:', Math.round(data.freeHeap/1024) + 'KB'],
        ['Heap min:', Math.round((data.minHeap||0)/1024) + 'KB']
      ];
      stats.forEach(function(s) {
        var row = document.createElement('div');
        row.className = 'tft-stat-row';
        var lb = document.createElement('span');
        lb.className = 'tft-stat-label'; lb.textContent = s[0];
        var vl = document.createElement('span');
        vl.className = 'tft-stat-value'; vl.textContent = s[1];
        row.appendChild(lb); row.appendChild(vl);
        body.appendChild(row);
      });
      el.appendChild(body);
      break;
    }
    case 5: { // VU METER
      var peak = data.peakLevel || 0;
      var vuPct = Math.round(peak * 100);
      var vuCol = peak > 0.8 ? '#f44336' : (peak > 0.5 ? '#ffeb3b' : '#00e676');
      var vuC = document.createElement('div');
      vuC.className = 'tft-vu-container';
      var sideL = document.createElement('div');
      sideL.className = 'tft-vu-side';
      var sideR = document.createElement('div');
      sideR.className = 'tft-vu-side';
      for (var i = 0; i < 10; i++) {
        var thr = i / 10;
        var sc = '#222';
        if (peak > thr) sc = thr > 0.8 ? '#f44336' : (thr > 0.5 ? '#ffeb3b' : '#00e676');
        var segL = document.createElement('div');
        segL.className = 'tft-vu-seg'; segL.style.background = sc;
        sideL.appendChild(segL);
        var segR = document.createElement('div');
        segR.className = 'tft-vu-seg'; segR.style.background = sc;
        sideR.appendChild(segR);
      }
      var main = document.createElement('div');
      main.className = 'tft-vu-main';
      main.style.height = '150px';
      var fill = document.createElement('div');
      fill.className = 'tft-vu-fill';
      fill.style.height = vuPct + '%';
      fill.style.background = vuCol;
      main.appendChild(fill);
      vuC.appendChild(sideL); vuC.appendChild(main); vuC.appendChild(sideR);
      body.appendChild(vuC);
      var pctDiv = document.createElement('div');
      pctDiv.className = 'tft-vu-pct'; pctDiv.textContent = vuPct + '%';
      body.appendChild(pctDiv);
      el.appendChild(body);
      break;
    }
  }
}

$('tft-prev').addEventListener('click', function() {
  tftScreen = (tftScreen + TFT_SCREENS - 1) % TFT_SCREENS;
  renderTftDots();
});

$('tft-next').addEventListener('click', function() {
  tftScreen = (tftScreen + 1) % TFT_SCREENS;
  renderTftDots();
});

renderTftDots();

// Polling
var lastStatusData = {};
function fetchStatus() {
  fetch('/api/status')
    .then(r => r.json())
    .then(data => {
      lastStatusData = data;

      // Estado
      const dot = $('status-dot');
      dot.className = 'dot ' + data.state;
      const labels = {
        idle: 'Esperando',
        listening: 'Escuchando...',
        processing: 'Procesando...',
        speaking: 'Hablando...',
        error: 'Error'
      };
      $('status-text').textContent = labels[data.state] || data.state;

      // Volumen (solo actualizar si el usuario no está tocando el slider)
      if (!sliderActive) {
        $('volume-slider').value = data.volume;
        $('volume-value').textContent = data.volume + '%';
      }

      // Info
      $('info-ip').textContent = data.ip;
      $('info-rssi').textContent = data.rssi + ' dBm';
      $('info-heap').textContent = Math.round(data.freeHeap / 1024) + ' KB';

      const mins = Math.floor(data.uptime / 60);
      const secs = data.uptime % 60;
      $('info-uptime').textContent = mins + 'm ' + secs + 's';

      // TFT Display virtual
      buildTftScreen(data);
    })
    .catch(() => {
      $('status-dot').className = 'dot error';
      $('status-text').textContent = 'Sin conexión';
    });
}

fetchStatus();
initBackendUrl();
setInterval(fetchStatus, 2000);
setInterval(pollSpeaker, 1000);
