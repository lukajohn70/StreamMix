/**
 * StreamMix — app.js
 * Interactive logic: switching, overlays, transitions, timers, meters
 */

/* ══════════════════════════════════════════════════════════
   STATE
══════════════════════════════════════════════════════════ */
const state = {
  preview: 2,         // input card currently in preview
  program: 1,         // input card currently in program
  transition: 'cut',  // active transition type
  duration: 500,      // transition duration ms
  streaming: true,    // stream active
  recording: false,
  external: false,
  multicorder: false,
  overlays: new Set(), // active overlay channels
  streamSeconds: 1686, // 28:06
};

/* ══════════════════════════════════════════════════════════
   DOM REFS
══════════════════════════════════════════════════════════ */
const $ = id => document.getElementById(id);
const $$ = sel => document.querySelectorAll(sel);

/* ══════════════════════════════════════════════════════════
   TIMECODES & CLOCK
══════════════════════════════════════════════════════════ */
let frameCount = 0;
let streamSeconds = state.streamSeconds;

function formatTC(totalFrames, fps = 30) {
  const total = Math.floor(totalFrames / fps);
  const h  = String(Math.floor(total / 3600)).padStart(2, '0');
  const m  = String(Math.floor((total % 3600) / 60)).padStart(2, '0');
  const s  = String(total % 60).padStart(2, '0');
  const fr = String(totalFrames % fps).padStart(2, '0');
  return `${h}:${m}:${s}:${fr}`;
}

function formatHMS(seconds) {
  const h = String(Math.floor(seconds / 3600)).padStart(2, '0');
  const m = String(Math.floor((seconds % 3600) / 60)).padStart(2, '0');
  const s = String(seconds % 60).padStart(2, '0');
  return `${h}:${m}:${s}`;
}

setInterval(() => {
  frameCount++;
  $('preview-timecode').textContent = formatTC(frameCount + 420);
  $('program-timecode').textContent = formatTC(frameCount);
  if (state.streaming) {
    streamSeconds++;
    $('ctrl-stream-timer').textContent = formatHMS(streamSeconds);
    $('stream-duration').textContent   = formatHMS(streamSeconds);
  }
}, 1000 / 30);

/* ══════════════════════════════════════════════════════════
   AUDIO METERS — animated simulation
══════════════════════════════════════════════════════════ */
const meterFills = document.querySelectorAll('.meter-fill');
const miniBars   = document.querySelectorAll('.mini-bar');

function animateMeters() {
  meterFills.forEach((fill, i) => {
    const base   = parseFloat(fill.style.height) || 30;
    const jitter = (Math.random() - 0.5) * 20;
    const next   = Math.min(95, Math.max(0, base + jitter));
    fill.style.height = next + '%';
  });

  miniBars.forEach((bar, i) => {
    const base   = parseFloat(bar.style.height) || 20;
    const jitter = (Math.random() - 0.5) * 30;
    const next   = Math.min(90, Math.max(0, base + jitter));
    bar.style.height = next + '%';
  });
}

setInterval(animateMeters, 80);

/* ══════════════════════════════════════════════════════════
   STATS SIMULATION
══════════════════════════════════════════════════════════ */
setInterval(() => {
  $('stat-fps').textContent    = (29 + Math.round(Math.random())).toString();
  $('stat-render').textContent = (1 + Math.round(Math.random())).toString() + 'ms';
  $('stat-gpu').textContent    = (13 + Math.round(Math.random() * 5)) + '%';
  $('stat-cpu').textContent    = (3  + Math.round(Math.random() * 4)) + '%';
  $('stat-total').textContent  = (7  + Math.round(Math.random() * 5)) + '%';
}, 2000);

/* ══════════════════════════════════════════════════════════
   INPUT CARD SWITCHING
══════════════════════════════════════════════════════════ */
function getCard(inputId) {
  return document.querySelector(`[data-input="${inputId}"]`);
}

function getInputName(inputId) {
  const card = getCard(inputId);
  return card ? card.querySelector('.card-title').textContent : `Input ${inputId}`;
}

function setProgram(inputId) {
  // Clear old program
  getCard(state.program)?.classList.remove('program-active');
  // Set new
  state.program = inputId;
  getCard(state.program)?.classList.add('program-active');
  $('program-source-name').textContent = getInputName(inputId);
}

function setPreview(inputId) {
  getCard(state.preview)?.classList.remove('preview-active');
  state.preview = inputId;
  getCard(state.preview)?.classList.add('preview-active');
  $('preview-source-name').textContent = getInputName(inputId);
}

function doTransition() {
  // Flash the GO button
  const goBtn = $('go-btn');
  goBtn.style.background = 'linear-gradient(180deg, #3a88d8 0%, #2a68b8 100%)';
  setTimeout(() => {
    goBtn.style.background = '';
  }, 120);

  // Swap preview → program
  const oldPreview = state.preview;
  const oldProgram = state.program;
  setProgram(oldPreview);
  setPreview(oldProgram);
}

function doCut() {
  const goBtn = $('cut-direct-btn');
  goBtn.style.background = '#666';
  setTimeout(() => { goBtn.style.background = ''; }, 80);
  const oldPreview = state.preview;
  const oldProgram = state.program;
  setProgram(oldPreview);
  setPreview(oldProgram);
}

// GO button
$('go-btn').addEventListener('click', doTransition);
$('cut-direct-btn').addEventListener('click', doCut);

// Click on card → set to preview; double-click → send to program
$$('.input-card').forEach(card => {
  card.addEventListener('click', (e) => {
    if (e.target.closest('.card-close')) return;
    if (e.target.closest('.card-action-btn')) return;
    if (e.target.closest('.card-ov-btn')) return;
    const id = parseInt(card.dataset.input);
    if (id === state.program) return;
    setPreview(id);
  });

  card.addEventListener('dblclick', (e) => {
    if (e.target.closest('.card-close')) return;
    const id = parseInt(card.dataset.input);
    doTransition();
  });
});

// GO button on individual cards
$$('.go-btn-card').forEach(btn => {
  btn.addEventListener('click', (e) => {
    e.stopPropagation();
    const card = e.target.closest('.input-card');
    const id = parseInt(card.dataset.input);
    setPreview(id);
    setTimeout(doTransition, 50);
  });
});

// Cut button on individual cards
$$('.cut-btn-card').forEach(btn => {
  btn.addEventListener('click', (e) => {
    e.stopPropagation();
    const card = e.target.closest('.input-card');
    const id = parseInt(card.dataset.input);
    setPreview(id);
    setTimeout(doCut, 50);
  });
});

// Close / remove input card
$$('.card-close').forEach(btn => {
  btn.addEventListener('click', (e) => {
    e.stopPropagation();
    const card = e.target.closest('.input-card');
    const id = parseInt(card.dataset.id);
    if (id === state.program || id === state.preview) return; // don't remove live inputs
    card.style.opacity = '0';
    card.style.transform = 'scale(0.95)';
    card.style.transition = 'all 0.2s';
    setTimeout(() => card.remove(), 200);
  });
});

/* ══════════════════════════════════════════════════════════
   TRANSITION TYPE BUTTONS
══════════════════════════════════════════════════════════ */
$$('.trans-type-btn').forEach(btn => {
  btn.addEventListener('click', () => {
    $$('.trans-type-btn').forEach(b => b.classList.remove('active'));
    btn.classList.add('active');
    state.transition = btn.dataset.trans;
  });
});

// Duration input
$('trans-duration').addEventListener('change', (e) => {
  state.duration = parseInt(e.target.value) || 500;
});

/* ══════════════════════════════════════════════════════════
   OVERLAY CHANNEL BUTTONS
══════════════════════════════════════════════════════════ */
$$('.overlay-btn').forEach(btn => {
  btn.addEventListener('click', () => {
    const ch = btn.dataset.overlay;
    if (state.overlays.has(ch)) {
      state.overlays.delete(ch);
      btn.classList.remove('active');
    } else {
      state.overlays.add(ch);
      btn.classList.add('active');
    }
  });
});

$$('.card-ov-btn').forEach(btn => {
  btn.addEventListener('click', (e) => {
    e.stopPropagation();
    btn.classList.toggle('ov-active');
    const ch = btn.dataset.ov;
    const globalOvBtn = document.querySelector(`.overlay-btn[data-overlay="${ch}"]`);
    if (globalOvBtn) globalOvBtn.classList.toggle('active');
  });
});

/* ══════════════════════════════════════════════════════════
   STINGER BUTTONS
══════════════════════════════════════════════════════════ */
$$('.stinger-slot-btn').forEach(btn => {
  btn.addEventListener('click', () => {
    $$('.stinger-slot-btn').forEach(b => b.classList.remove('active'));
    btn.classList.add('active');
    setTimeout(() => btn.classList.remove('active'), 300);
  });
});

/* ══════════════════════════════════════════════════════════
   RECORD / STREAM / EXTERNAL / MULTICORDER
══════════════════════════════════════════════════════════ */
function setupToggleBtn(btnId, ledId, stateKey, activeColor = 'var(--record-on)') {
  const btn = $(btnId);
  const led = $(ledId);
  btn.addEventListener('click', () => {
    state[stateKey] = !state[stateKey];
    if (state[stateKey]) {
      led.classList.replace('led-off', 'led-on');
      btn.style.background = activeColor;
      btn.style.borderColor = '#ff4444';
      btn.style.color = '#fff';
    } else {
      led.classList.replace('led-on', 'led-off');
      btn.style.background = '';
      btn.style.borderColor = '';
      btn.style.color = '';
    }
  });
}

setupToggleBtn('ctrl-record',     'led-record',     'recording');
setupToggleBtn('ctrl-external',   'led-external',   'external');
setupToggleBtn('ctrl-multicorder','led-multicorder','multicorder');

// Stream is pre-active
const streamBtn = $('ctrl-stream');
const streamLed = $('led-stream');
streamBtn.addEventListener('click', () => {
  state.streaming = !state.streaming;
  if (state.streaming) {
    streamLed.classList.replace('led-off', 'led-on');
    streamBtn.style.background = 'var(--stream-on)';
    streamBtn.style.borderColor = '#ff4444';
    streamBtn.style.color = '#fff';
  } else {
    streamLed.classList.replace('led-on', 'led-off');
    streamBtn.style.background = '';
    streamBtn.style.borderColor = '';
    streamBtn.style.color = '';
    streamSeconds = 0;
  }
});
// Init stream as active
streamLed.classList.replace('led-off', 'led-on');
streamBtn.style.background = 'var(--stream-on)';
streamBtn.style.borderColor = '#ff4444';
streamBtn.style.color = '#fff';

/* ══════════════════════════════════════════════════════════
   AUDIO MIXER — BUS TABS
══════════════════════════════════════════════════════════ */
$$('.bus-tab').forEach(tab => {
  tab.addEventListener('click', () => {
    $$('.bus-tab').forEach(t => t.classList.remove('active'));
    tab.classList.add('active');
  });
});

/* ══════════════════════════════════════════════════════════
   AUDIO MUTE / SOLO
══════════════════════════════════════════════════════════ */
$$('.ch-mute-btn').forEach(btn => {
  btn.addEventListener('click', () => btn.classList.toggle('muted'));
});
$$('.ch-solo-btn').forEach(btn => {
  btn.addEventListener('click', () => btn.classList.toggle('soloed'));
});

/* ══════════════════════════════════════════════════════════
   INPUT FILTER BAR — COLOR FILTERS
══════════════════════════════════════════════════════════ */
$$('.color-filter-btn').forEach(btn => {
  btn.addEventListener('click', () => {
    $$('.color-filter-btn').forEach(b => b.classList.remove('active'));
    btn.classList.add('active');
    const filter = btn.dataset.filter;
    $$('.input-card').forEach(card => {
      if (filter === 'all' || card.dataset.type === filter) {
        card.style.display = '';
      } else {
        card.style.display = 'none';
      }
    });
  });
});

// Search filter
$('input-search').addEventListener('input', (e) => {
  const q = e.target.value.toLowerCase();
  $$('.input-card').forEach(card => {
    const title = card.querySelector('.card-title').textContent.toLowerCase();
    card.style.display = title.includes(q) ? '' : 'none';
  });
});

/* ══════════════════════════════════════════════════════════
   KEYBOARD SHORTCUTS
══════════════════════════════════════════════════════════ */
document.addEventListener('keydown', (e) => {
  // Space = GO
  if (e.code === 'Space' && !e.target.matches('input')) {
    e.preventDefault();
    doTransition();
  }
  // Ctrl+R = Record
  if (e.code === 'KeyR' && e.ctrlKey) {
    e.preventDefault();
    $('ctrl-record').click();
  }
  // Number keys 1-8 for quick preview select
  if (e.code.startsWith('Digit') && !e.ctrlKey && !e.altKey && !e.target.matches('input')) {
    const n = parseInt(e.key);
    if (n >= 1 && n <= 8) {
      const card = document.querySelector(`[data-input="${n}"]`);
      if (card) setPreview(n);
    }
  }
  // C = cut
  if (e.code === 'KeyC' && !e.ctrlKey && !e.target.matches('input')) {
    doCut();
  }
});

/* ══════════════════════════════════════════════════════════
   PLAYBACK CONTROLS ON MEDIA CARDS
══════════════════════════════════════════════════════════ */
$$('.pb-btn').forEach(btn => {
  btn.addEventListener('click', (e) => {
    e.stopPropagation();
    if (btn.classList.contains('play-btn')) {
      btn.textContent = btn.textContent === '▶' ? '⏸' : '▶';
    }
    if (btn.classList.contains('loop-btn')) {
      btn.classList.toggle('active');
    }
  });
console.log('StreamMix initialized. Press Space=GO, C=Cut, 1-8=Preview select');

/* ══════════════════════════════════════════════════════════
   STREAMMIX REAL-TIME WEBSOCKET & TELESTRATOR CLIENT (vMix 29)
══════════════════════════════════════════════════════════ */
let ws = null;

function initWebSocket() {
  const host = window.location.hostname || 'localhost';
  const port = window.location.port || '8088';
  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  const url = `${protocol}//${host}:${port}/ws`;

  try {
    ws = new WebSocket(url);
    ws.onopen = () => {
      console.log('[StreamMix] Connected to C++ Web Controller Engine on ' + url);
      const logo = $('app-logo');
      if (logo) logo.style.color = '#2ecc71';
    };
    ws.onmessage = (evt) => {
      try {
        const msg = JSON.parse(evt.data);
        if (msg.type === 'state') {
          if (msg.program && msg.program !== state.program) setProgram(msg.program);
          if (msg.preview && msg.preview !== state.preview) setPreview(msg.preview);
        }
      } catch (err) {}
    };
    ws.onclose = () => {
      const logo = $('app-logo');
      if (logo) logo.style.color = '';
      setTimeout(initWebSocket, 3000);
    };
  } catch (e) {}
}

function sendWebCommand(cmd) {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(cmd));
  }
}

// Hook doCut & doTransition to send commands over WebSocket
const nativeDoCut = doCut;
doCut = function() {
  nativeDoCut();
  sendWebCommand({ action: 'cut' });
};

const nativeDoTransition = doTransition;
doTransition = function() {
  nativeDoTransition();
  sendWebCommand({ action: 'fade', duration: state.duration });
};

// Hook overlay buttons
$$('.overlay-btn').forEach(btn => {
  btn.addEventListener('click', () => {
    const ch = parseInt(btn.dataset.overlay);
    sendWebCommand({ action: 'overlay', channel: ch });
  });
});

// Hook FTB button
const ftbBtn = $('ftb-btn');
if (ftbBtn) {
  ftbBtn.addEventListener('click', () => {
    sendWebCommand({ action: 'ftb' });
  });
}

initWebSocket();

