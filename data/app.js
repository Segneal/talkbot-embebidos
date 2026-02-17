// Talkbot Web Control

const $ = (id) => document.getElementById(id);

// Polling de estado
function fetchStatus() {
  fetch('/api/status')
    .then(r => r.json())
    .then(data => {
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
    })
    .catch(() => {
      $('status-dot').className = 'dot error';
      $('status-text').textContent = 'Sin conexión';
    });
}

// Volumen
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
      this.textContent = 'Guardado!';
      setTimeout(() => { this.textContent = 'Guardar URL'; }, 1500);
    })
    .catch(() => {
      this.textContent = 'Error';
      setTimeout(() => { this.textContent = 'Guardar URL'; }, 1500);
    });
});

fetchBackendUrl();

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

// Polling cada 2 segundos
fetchStatus();
setInterval(fetchStatus, 2000);
