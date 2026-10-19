/**
 * StreamMix 29 — app.js
 * Pixel-Faithful, Broadcast-Grade vMix 29 Studio Engine
 * Full switching, hardware T-Bar fader, multi-bus audio DSP, PTZ robotic cameras,
 * Instant Replay, MultiCorder ISO, 60fps canvas video synthesis & WebSocket synchronization.
 */

// ═══════════════════════════════════════════════════════════════════
// 1. STATE & ENGINE INITIALIZATION
// ═══════════════════════════════════════════════════════════════════
const state = {
  preview: 4,                 // Input 4: vMix Call Multiview (in Preview with Orange Outline)
  program: 1,                 // Input 1: Camera 1 (in Output with Green Outline)
  transition: 'fade',         // Active transition ('cut', 'fade', 'wipe', 'fly', 'zoom')
  duration: 500,              // Transition duration in ms
  faderPos: 0.0,              // Hardware T-Bar position (0.0 = Preview, 1.0 = Program)
  isTransitioning: false,     // Currently animating a transition
  transitionProgress: 0.0,    // 0.0 to 1.0
  ftbActive: false,           // Fade to Black state
  activeOverlays: new Set([8]), // Overlay 8 (Lower Third) active by default (matching reference)
  streaming: true,            // Stream active (Red badge)
  recording: true,            // Record active (Red badge)
  external: false,            // External output
  multicorder: false,         // MultiCorder ISO
  activeCategory: 'all',      // Category filter ('all', 'cam', 'media', 'call', 'title')
  streamSeconds: 1104,        // 00:18:24
  recordSeconds: 1104,        // 00:18:24 (matching golden screenshot)
  elapsedSeconds: 673.2,      // 11:13.2 (matching golden screenshot green clock)
  frameCount: 143218,
  inputs: [
    { id: 1, name: 'Camera 1', type: 'cam', catColor: '#2e7d32', res: '1080p59.94', audio: true, volume: 85, muted: false, solo: false, afv: true },
    { id: 2, name: 'Camera 2', type: 'cam', catColor: '#1565c0', res: '1080p59.94', audio: true, volume: 88, muted: false, solo: false, afv: true },
    { id: 3, name: 'Camera 3', type: 'cam', catColor: '#1565c0', res: '1080p59.94', audio: true, volume: 80, muted: false, solo: false, afv: false },
    { id: 4, name: 'vMix Call Multiview', type: 'call', catColor: '#e65100', res: '1080p30 Call', audio: true, volume: 75, muted: false, solo: false, afv: false },
    { id: 5, name: 'Camera | Setup', type: 'cam', catColor: '#2e7d32', res: '1080p59.94', audio: false, volume: 0, muted: true, solo: false, afv: false },
    { id: 6, name: 'Intro Video', type: 'media', catColor: '#1565c0', res: 'H.264 MP4', audio: true, volume: 80, muted: false, solo: false, afv: true, playing: true },
    { id: 7, name: 'Social Media Title', type: 'title', catColor: '#1565c0', res: 'XAML Title', audio: false, volume: 0, muted: true, solo: false, afv: false },
    { id: 8, name: 'Title Live Show', type: 'title', catColor: '#1565c0', res: 'GT Title', audio: false, volume: 0, muted: true, solo: false, afv: false },
    { id: 9, name: 'Call Sandra', type: 'call', catColor: '#1565c0', res: 'vMix Call WebRTC', audio: true, volume: 75, muted: false, solo: false, afv: false },
    { id: 10, name: 'Call Phil', type: 'call', catColor: '#1565c0', res: 'vMix Call WebRTC', audio: true, volume: 72, muted: false, solo: false, afv: false },
    { id: 11, name: 'Call Amanda', type: 'call', catColor: '#1565c0', res: 'vMix Call WebRTC', audio: true, volume: 70, muted: false, solo: false, afv: false },
    { id: 12, name: 'Call Chris', type: 'call', catColor: '#1565c0', res: 'vMix Call WebRTC', audio: true, volume: 68, muted: false, solo: false, afv: false }
  ]
};

// DOM Utilities
const $ = id => document.getElementById(id);
const $$ = sel => document.querySelectorAll(sel);

// ═══════════════════════════════════════════════════════════════════
// 2. REAL-TIME BROADCAST VIDEO CANVAS ENGINE (60 FPS)
// ═══════════════════════════════════════════════════════════════════
const cardCanvases = {};
state.inputs.forEach(inp => {
  const c = $(`canvas-card-${inp.id}`);
  if (c) {
    cardCanvases[inp.id] = {
      canvas: c,
      ctx: c.getContext('2d')
    };
  }
});

const previewCanvas = $('preview-canvas');
const previewCtx = previewCanvas.getContext('2d');
const programCanvas = $('program-canvas');
const programCtx = programCanvas.getContext('2d');

let videoTick = 0;

/**
 * Synthesizes realistic 60fps moving broadcast video feeds matching the golden reference
 */
function renderSynthesizedVideoFrame(id, ctx, width, height, t) {
  ctx.save();
  ctx.clearRect(0, 0, width, height);

  switch (id) {
    case 1: { 
      // Camera 1 — Studio Wide: Brick wall with purple/warm ambient lighting, two hosts at broadcast desk
      const bgGrad = ctx.createLinearGradient(0, 0, width, height);
      bgGrad.addColorStop(0, '#1c1524');
      bgGrad.addColorStop(0.5, '#2e1c36');
      bgGrad.addColorStop(1, '#110b14');
      ctx.fillStyle = bgGrad;
      ctx.fillRect(0, 0, width, height);

      // Studio brick wall texture simulation
      ctx.fillStyle = 'rgba(255, 255, 255, 0.03)';
      for (let y = 10; y < height * 0.7; y += 12) {
        ctx.fillRect(0, y, width, 1);
      }

      // Overhead studio softbox lamp
      const lampX = width * 0.78;
      const lampGrad = ctx.createRadialGradient(lampX, 30, 5, lampX, 30, 90);
      lampGrad.addColorStop(0, 'rgba(180, 230, 255, 0.7)');
      lampGrad.addColorStop(1, 'rgba(180, 230, 255, 0)');
      ctx.fillStyle = lampGrad;
      ctx.beginPath();
      ctx.arc(lampX, 30, 90, 0, Math.PI * 2);
      ctx.fill();

      // Studio Desk
      ctx.fillStyle = '#0a0a0d';
      ctx.fillRect(width * 0.1, height * 0.72, width * 0.8, height * 0.28);
      ctx.fillStyle = '#3e2723';
      ctx.fillRect(width * 0.1, height * 0.72, width * 0.8, 6);

      // Host 1 (Left - woman in beige)
      const h1Bob = Math.sin(t * 0.03) * 2;
      ctx.fillStyle = '#d7ccc8'; // Beige shirt
      ctx.beginPath();
      ctx.ellipse(width * 0.32, height * 0.65 + h1Bob, 26, 32, 0, 0, Math.PI * 2);
      ctx.fill();
      ctx.fillStyle = '#ffccbc'; // Face
      ctx.beginPath();
      ctx.arc(width * 0.32, height * 0.44 + h1Bob, 16, 0, Math.PI * 2);
      ctx.fill();
      ctx.fillStyle = '#4e342e'; // Hair
      ctx.beginPath();
      ctx.arc(width * 0.32, height * 0.40 + h1Bob, 18, Math.PI, Math.PI * 2);
      ctx.fill();
      ctx.fillRect(width * 0.32 - 18, height * 0.40 + h1Bob, 36, 16);
      // Headphones
      ctx.strokeStyle = '#212121';
      ctx.lineWidth = 4;
      ctx.beginPath();
      ctx.arc(width * 0.32, height * 0.42 + h1Bob, 19, Math.PI * 0.9, Math.PI * 2.1);
      ctx.stroke();

      // Host 2 (Right - man in grey hoodie with headphones)
      const h2Bob = Math.cos(t * 0.028) * 2;
      ctx.fillStyle = '#cfd8dc'; // Grey hoodie
      ctx.beginPath();
      ctx.ellipse(width * 0.68, height * 0.64 + h2Bob, 28, 34, 0, 0, Math.PI * 2);
      ctx.fill();
      ctx.fillStyle = '#8d6e63'; // Skin tone
      ctx.beginPath();
      ctx.arc(width * 0.68, height * 0.43 + h2Bob, 17, 0, Math.PI * 2);
      ctx.fill();
      ctx.fillStyle = '#1c1c1c'; // Hair
      ctx.beginPath();
      ctx.arc(width * 0.68, height * 0.39 + h2Bob, 17, Math.PI * 1.1, Math.PI * 1.9);
      ctx.fill();
      // Headphones
      ctx.strokeStyle = '#111';
      ctx.lineWidth = 5;
      ctx.beginPath();
      ctx.arc(width * 0.68, height * 0.41 + h2Bob, 20, Math.PI * 0.9, Math.PI * 2.1);
      ctx.stroke();

      // Broadcast Condenser Microphone on arm
      ctx.fillStyle = '#111';
      ctx.fillRect(width * 0.48, height * 0.52, 10, 24);
      ctx.fillStyle = '#555';
      ctx.fillRect(width * 0.49, height * 0.48, 8, 8);
      break;
    }

    case 2: { 
      // Camera 2 — Host 1 Medium Close-Up
      const bgGrad = ctx.createLinearGradient(0, 0, width, height);
      bgGrad.addColorStop(0, '#2e1c36');
      bgGrad.addColorStop(1, '#110b14');
      ctx.fillStyle = bgGrad;
      ctx.fillRect(0, 0, width, height);

      const bob = Math.sin(t * 0.035) * 3;
      ctx.fillStyle = '#d7ccc8';
      ctx.beginPath();
      ctx.ellipse(width * 0.5, height * 0.85 + bob, 48, 55, 0, 0, Math.PI * 2);
      ctx.fill();

      ctx.fillStyle = '#ffccbc';
      ctx.beginPath();
      ctx.arc(width * 0.5, height * 0.45 + bob, 32, 0, Math.PI * 2);
      ctx.fill();

      ctx.fillStyle = '#4e342e';
      ctx.beginPath();
      ctx.arc(width * 0.5, height * 0.38 + bob, 36, Math.PI, Math.PI * 2);
      ctx.fill();
      ctx.fillRect(width * 0.5 - 36, height * 0.38 + bob, 72, 34);

      // Headphones
      ctx.strokeStyle = '#212121';
      ctx.lineWidth = 6;
      ctx.beginPath();
      ctx.arc(width * 0.5, height * 0.42 + bob, 36, Math.PI * 0.85, Math.PI * 2.15);
      ctx.stroke();
      break;
    }

    case 3: { 
      // Camera 3 — Host 2 Medium Close-Up
      const bgGrad = ctx.createLinearGradient(0, 0, width, height);
      bgGrad.addColorStop(0, '#2a1830');
      bgGrad.addColorStop(1, '#0e0b12');
      ctx.fillStyle = bgGrad;
      ctx.fillRect(0, 0, width, height);

      const bob = Math.cos(t * 0.032) * 3;
      ctx.fillStyle = '#cfd8dc';
      ctx.beginPath();
      ctx.ellipse(width * 0.5, height * 0.84 + bob, 52, 58, 0, 0, Math.PI * 2);
      ctx.fill();

      ctx.fillStyle = '#8d6e63';
      ctx.beginPath();
      ctx.arc(width * 0.5, height * 0.44 + bob, 32, 0, Math.PI * 2);
      ctx.fill();

      // Glasses
      ctx.strokeStyle = '#000';
      ctx.lineWidth = 2.5;
      ctx.strokeRect(width * 0.5 - 24, height * 0.42 + bob, 20, 12);
      ctx.strokeRect(width * 0.5 + 4, height * 0.42 + bob, 20, 12);
      ctx.beginPath();
      ctx.moveTo(width * 0.5 - 4, height * 0.46 + bob);
      ctx.lineTo(width * 0.5 + 4, height * 0.46 + bob);
      ctx.stroke();

      // Headphones
      ctx.strokeStyle = '#111';
      ctx.lineWidth = 7;
      ctx.beginPath();
      ctx.arc(width * 0.5, height * 0.40 + bob, 37, Math.PI * 0.85, Math.PI * 2.15);
      ctx.stroke();
      break;
    }

    case 4: { 
      // Input 4 — vMix Call Multiview (Exact layout from the reference screenshot!)
      // Digital tech background with 4 caller windows on left and 1 large window on right
      const techGrad = ctx.createLinearGradient(0, 0, width, height);
      techGrad.addColorStop(0, '#041021');
      techGrad.addColorStop(1, '#02070e');
      ctx.fillStyle = techGrad;
      ctx.fillRect(0, 0, width, height);

      // Cyan digital circuitry lines
      ctx.strokeStyle = 'rgba(0, 150, 255, 0.15)';
      ctx.lineWidth = 1;
      for (let x = 15; x < width; x += 30) {
        ctx.beginPath();
        ctx.moveTo(x, 0);
        ctx.lineTo(x, height);
        ctx.stroke();
      }

      const boxPad = 6;
      const leftW = width * 0.58;
      const colW = (leftW - boxPad * 3) / 2;
      const rowH = (height - boxPad * 3) / 2;

      // 4 boxes on left
      const callers = [
        { name: 'Sandra M.', col: '#d7ccc8', skin: '#ffccbc' },
        { name: 'Phil K.', col: '#37474f', skin: '#8d6e63' },
        { name: 'Amanda T.', col: '#b0bec5', skin: '#ffab91' },
        { name: 'Chris B.', col: '#eceff1', skin: '#d7ccc8' }
      ];

      for (let i = 0; i < 4; i++) {
        const col = i % 2;
        const row = Math.floor(i / 2);
        const bx = boxPad + col * (colW + boxPad);
        const by = boxPad + row * (rowH + boxPad);

        ctx.fillStyle = '#0a0d14';
        ctx.fillRect(bx, by, colW, rowH);
        ctx.strokeStyle = '#ffffff';
        ctx.lineWidth = 1.5;
        ctx.strokeRect(bx, by, colW, rowH);

        // Caller avatar simulation
        const cBob = Math.sin(t * 0.04 + i) * 2;
        ctx.fillStyle = callers[i].col;
        ctx.beginPath();
        ctx.ellipse(bx + colW / 2, by + rowH * 0.8 + cBob, colW * 0.32, rowH * 0.4, 0, 0, Math.PI * 2);
        ctx.fill();
        ctx.fillStyle = callers[i].skin;
        ctx.beginPath();
        ctx.arc(bx + colW / 2, by + rowH * 0.42 + cBob, rowH * 0.22, 0, Math.PI * 2);
        ctx.fill();

        // Audio tally dot
        ctx.fillStyle = '#00e676';
        ctx.beginPath();
        ctx.arc(bx + 10, by + 10, 3, 0, Math.PI * 2);
        ctx.fill();
      }

      // 1 Large window on right (Main Speaker)
      const rX = leftW + boxPad;
      const rY = boxPad;
      const rW = width - rX - boxPad;
      const rH = height - boxPad * 2;

      ctx.fillStyle = '#060a12';
      ctx.fillRect(rX, rY, rW, rH);
      ctx.strokeStyle = '#ffffff';
      ctx.lineWidth = 1.5;
      ctx.strokeRect(rX, rY, rW, rH);

      const rBob = Math.cos(t * 0.035) * 2;
      ctx.fillStyle = '#eceff1'; // White hoodie
      ctx.beginPath();
      ctx.ellipse(rX + rW / 2, rY + rH * 0.82 + rBob, rW * 0.34, rH * 0.42, 0, 0, Math.PI * 2);
      ctx.fill();
      ctx.fillStyle = '#8d6e63'; // Skin
      ctx.beginPath();
      ctx.arc(rX + rW / 2, rY + rH * 0.42 + rBob, rH * 0.24, 0, Math.PI * 2);
      ctx.fill();

      // Large mic
      ctx.fillStyle = '#111';
      ctx.fillRect(rX + rW * 0.45, rY + rH * 0.65, 14, 28);
      break;
    }

    case 5: { 
      // Input 5 — Camera | Setup
      ctx.fillStyle = '#0f172a';
      ctx.fillRect(0, 0, width, height);

      ctx.strokeStyle = '#38bdf8';
      ctx.lineWidth = 1;
      ctx.setLineDash([3, 3]);
      ctx.strokeRect(width * 0.15, height * 0.15, width * 0.7, height * 0.7);
      ctx.setLineDash([]);

      ctx.fillStyle = '#38bdf8';
      ctx.font = 'bold 11px "Segoe UI", sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText('PTZ CAMERA SETUP', width / 2, height * 0.45);
      ctx.fillStyle = '#94a3b8';
      ctx.font = '9px "Segoe UI", sans-serif';
      ctx.fillText('Stage Center • 4K UHD 59.94p', width / 2, height * 0.62);
      break;
    }

    case 6: { 
      // Input 6 — Intro Video
      const grad = ctx.createLinearGradient(0, 0, width, height);
      grad.addColorStop(0, '#d84315');
      grad.addColorStop(0.5, '#4a148c');
      grad.addColorStop(1, '#0d47a1');
      ctx.fillStyle = grad;
      ctx.fillRect(0, 0, width, height);

      ctx.save();
      ctx.translate(width / 2, height / 2);
      ctx.rotate(t * 0.02);
      ctx.strokeStyle = 'rgba(255, 255, 255, 0.7)';
      ctx.lineWidth = 3;
      ctx.strokeRect(-35, -35, 70, 70);
      ctx.restore();

      ctx.fillStyle = '#ffffff';
      ctx.font = 'bold 14px "Segoe UI", sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText('STREAMMIX 29 INTRO', width / 2, height / 2 + 50);
      break;
    }

    case 7: { 
      // Input 7 — Social Media Title
      ctx.fillStyle = '#ffffff';
      ctx.fillRect(0, 0, width, height);

      ctx.fillStyle = '#1da1f2';
      ctx.fillRect(0, 0, width, 24);

      ctx.fillStyle = '#ffffff';
      ctx.font = 'bold 10px "Segoe UI", sans-serif';
      ctx.fillText('💬 Live Chat Highlight', 10, 16);

      ctx.fillStyle = '#333333';
      ctx.font = '10px "Segoe UI", sans-serif';
      ctx.fillText('@StreamViewer: Loving the new vMix 29 UI update!', 10, 48);
      ctx.fillText('#StreamMix #Broadcast #vMix29', 10, 68);
      break;
    }

    case 8: { 
      // Input 8 — Title Live Show (Exact Lower Third from the golden screenshot!)
      // Transparent background, slanted purple ribbon "TAYLOR PRESENTS", white banner "THE LIVE SHOW"
      ctx.clearRect(0, 0, width, height);

      const bY = height - 52;
      const bW = width * 0.7;
      const bX = width * 0.15;

      // Slanted purple upper badge
      ctx.fillStyle = '#6a1b9a';
      ctx.beginPath();
      ctx.moveTo(bX + 20, bY);
      ctx.lineTo(bX + 180, bY);
      ctx.lineTo(bX + 165, bY + 16);
      ctx.lineTo(bX + 10, bY + 16);
      ctx.closePath();
      ctx.fill();

      ctx.fillStyle = '#ffffff';
      ctx.font = 'bold 9px "Segoe UI", sans-serif';
      ctx.fillText('TAYLOR PRESENTS', bX + 24, bY + 12);

      // White lower slanted banner
      ctx.fillStyle = '#ffffff';
      ctx.beginPath();
      ctx.moveTo(bX + 10, bY + 16);
      ctx.lineTo(bX + bW, bY + 16);
      ctx.lineTo(bX + bW - 18, bY + 46);
      ctx.lineTo(bX - 5, bY + 46);
      ctx.closePath();
      ctx.fill();

      // Inner purple triangle accents
      ctx.fillStyle = '#8e24aa';
      ctx.beginPath();
      ctx.moveTo(bX - 5, bY + 46);
      ctx.lineTo(bX + 15, bY + 16);
      ctx.lineTo(bX + 30, bY + 46);
      ctx.closePath();
      ctx.fill();

      ctx.fillStyle = '#1a1a1a';
      ctx.font = '900 18px "Segoe UI", sans-serif';
      ctx.fillText('THE LIVE SHOW', bX + 38, bY + 39);
      break;
    }

    case 9: { 
      // Input 9 — Call Sandra
      const bgGrad = ctx.createLinearGradient(0, 0, width, height);
      bgGrad.addColorStop(0, '#1a2332');
      bgGrad.addColorStop(1, '#0d131a');
      ctx.fillStyle = bgGrad;
      ctx.fillRect(0, 0, width, height);

      const bob = Math.sin(t * 0.04) * 2;
      ctx.fillStyle = '#b0bec5';
      ctx.beginPath();
      ctx.ellipse(width * 0.5, height * 0.85 + bob, 46, 52, 0, 0, Math.PI * 2);
      ctx.fill();

      ctx.fillStyle = '#ffccbc';
      ctx.beginPath();
      ctx.arc(width * 0.5, height * 0.44 + bob, 30, 0, Math.PI * 2);
      ctx.fill();

      ctx.fillStyle = '#4e342e';
      ctx.beginPath();
      ctx.arc(width * 0.5, height * 0.38 + bob, 34, Math.PI, Math.PI * 2);
      ctx.fill();
      ctx.fillRect(width * 0.5 - 34, height * 0.38 + bob, 68, 30);
      break;
    }

    case 10: { 
      // Input 10 — Call Phil
      const bgGrad = ctx.createLinearGradient(0, 0, width, height);
      bgGrad.addColorStop(0, '#1b2838');
      bgGrad.addColorStop(1, '#0e1724');
      ctx.fillStyle = bgGrad;
      ctx.fillRect(0, 0, width, height);

      const bob = Math.cos(t * 0.038) * 2;
      ctx.fillStyle = '#37474f';
      ctx.beginPath();
      ctx.ellipse(width * 0.5, height * 0.85 + bob, 48, 54, 0, 0, Math.PI * 2);
      ctx.fill();

      ctx.fillStyle = '#8d6e63';
      ctx.beginPath();
      ctx.arc(width * 0.5, height * 0.44 + bob, 30, 0, Math.PI * 2);
      ctx.fill();
      break;
    }

    case 11: { 
      // Input 11 — Call Amanda
      const bgGrad = ctx.createLinearGradient(0, 0, width, height);
      bgGrad.addColorStop(0, '#241b2e');
      bgGrad.addColorStop(1, '#130e1a');
      ctx.fillStyle = bgGrad;
      ctx.fillRect(0, 0, width, height);

      const bob = Math.sin(t * 0.035) * 2;
      ctx.fillStyle = '#b0bec5';
      ctx.beginPath();
      ctx.ellipse(width * 0.5, height * 0.85 + bob, 45, 50, 0, 0, Math.PI * 2);
      ctx.fill();

      ctx.fillStyle = '#ffab91';
      ctx.beginPath();
      ctx.arc(width * 0.5, height * 0.44 + bob, 29, 0, Math.PI * 2);
      ctx.fill();
      break;
    }

    case 12: { 
      // Input 12 — Call Chris
      const bgGrad = ctx.createLinearGradient(0, 0, width, height);
      bgGrad.addColorStop(0, '#192620');
      bgGrad.addColorStop(1, '#0c1410');
      ctx.fillStyle = bgGrad;
      ctx.fillRect(0, 0, width, height);

      const bob = Math.cos(t * 0.042) * 2;
      ctx.fillStyle = '#eceff1';
      ctx.beginPath();
      ctx.ellipse(width * 0.5, height * 0.85 + bob, 47, 52, 0, 0, Math.PI * 2);
      ctx.fill();

      ctx.fillStyle = '#d7ccc8';
      ctx.beginPath();
      ctx.arc(width * 0.5, height * 0.44 + bob, 29, 0, Math.PI * 2);
      ctx.fill();
      break;
    }
  }

  ctx.restore();
}

/**
 * Composites active inputs into Preview & Program monitors
 */
function compositeMonitors() {
  const pWidth = programCanvas.width;
  const pHeight = programCanvas.height;

  // 1. Preview Monitor: strictly mirrors the Preview source
  const prvCanvasObj = cardCanvases[state.preview];
  if (prvCanvasObj) {
    previewCtx.drawImage(prvCanvasObj.canvas, 0, 0, previewCanvas.width, previewCanvas.height);
  }

  // 2. Program Monitor: renders Program source, or blends during Transition / T-Bar
  const pgmCanvasObj = cardCanvases[state.program];
  
  if (state.faderPos > 0.0 && prvCanvasObj && pgmCanvasObj) {
    const blendAlpha = state.faderPos;
    programCtx.drawImage(pgmCanvasObj.canvas, 0, 0, pWidth, pHeight);
    programCtx.save();
    programCtx.globalAlpha = blendAlpha;
    programCtx.drawImage(prvCanvasObj.canvas, 0, 0, pWidth, pHeight);
    programCtx.restore();
  } else if (state.isTransitioning && prvCanvasObj && pgmCanvasObj) {
    const t = state.transitionProgress;
    renderTransition(programCtx, pgmCanvasObj.canvas, prvCanvasObj.canvas, pWidth, pHeight, state.transition, t);
  } else if (pgmCanvasObj) {
    programCtx.drawImage(pgmCanvasObj.canvas, 0, 0, pWidth, pHeight);
  }

  // 3. Composite Active Overlays on Program (e.g. Overlay 8 / Lower Third Title 8)
  if (state.activeOverlays.has(8) && cardCanvases[8]) {
    programCtx.drawImage(cardCanvases[8].canvas, 0, 0, pWidth, pHeight);
  }
}

/**
 * Broadcast transitions (Fade, Wipe, Zoom, Fly, Cut)
 */
function renderTransition(ctx, fromCanvas, toCanvas, w, h, type, progress) {
  ctx.save();
  switch (type) {
    case 'cut':
      ctx.drawImage(progress >= 0.5 ? toCanvas : fromCanvas, 0, 0, w, h);
      break;

    case 'fade':
      ctx.drawImage(fromCanvas, 0, 0, w, h);
      ctx.globalAlpha = progress;
      ctx.drawImage(toCanvas, 0, 0, w, h);
      break;

    case 'wipe': {
      ctx.drawImage(fromCanvas, 0, 0, w, h);
      const splitX = w * progress;
      ctx.save();
      ctx.beginPath();
      ctx.rect(0, 0, splitX, h);
      ctx.clip();
      ctx.drawImage(toCanvas, 0, 0, w, h);
      ctx.restore();
      ctx.strokeStyle = '#ffffff';
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.moveTo(splitX, 0);
      ctx.lineTo(splitX, h);
      ctx.stroke();
      break;
    }

    case 'zoom': {
      ctx.drawImage(fromCanvas, 0, 0, w, h);
      ctx.save();
      const scale = 0.2 + progress * 0.8;
      ctx.translate(w / 2, h / 2);
      ctx.scale(scale, scale);
      ctx.globalAlpha = progress;
      ctx.drawImage(toCanvas, -w / 2, -h / 2, w, h);
      ctx.restore();
      break;
    }

    case 'fly': {
      ctx.drawImage(fromCanvas, 0, 0, w, h);
      ctx.save();
      const flyX = (1.0 - progress) * w;
      const flyY = (1.0 - progress) * h;
      const scale = progress;
      ctx.translate(flyX * 0.5, flyY * 0.5);
      ctx.scale(scale, scale);
      ctx.drawImage(toCanvas, 0, 0, w, h);
      ctx.restore();
      break;
    }

    default:
      ctx.drawImage(fromCanvas, 0, 0, w, h);
      ctx.globalAlpha = progress;
      ctx.drawImage(toCanvas, 0, 0, w, h);
      break;
  }
  ctx.restore();
}

/**
 * Main 60fps render animation loop
 */
function videoLoop() {
  videoTick++;

  state.inputs.forEach(inp => {
    const obj = cardCanvases[inp.id];
    if (obj) {
      renderSynthesizedVideoFrame(inp.id, obj.ctx, obj.canvas.width, obj.canvas.height, videoTick);
    }
  });

  compositeMonitors();
  requestAnimationFrame(videoLoop);
}

requestAnimationFrame(videoLoop);

// ═══════════════════════════════════════════════════════════════════
// 3. MASTER SWITCHING & TRANSITION CONTROLS
// ═══════════════════════════════════════════════════════════════════
function setPreview(inputId) {
  if (state.isTransitioning) return;
  const oldTile = $(`tile-input-${state.preview}`);
  if (oldTile) oldTile.classList.remove('preview-active');

  state.preview = inputId;
  const newTile = $(`tile-input-${state.preview}`);
  if (newTile) newTile.classList.add('preview-active');

  const inp = state.inputs.find(i => i.id === inputId);
  if (inp) {
    const prvName = $('preview-source-name');
    if (prvName) prvName.textContent = inp.name;
  }
}

function setProgram(inputId) {
  const oldTile = $(`tile-input-${state.program}`);
  if (oldTile) oldTile.classList.remove('output-active');

  state.program = inputId;
  const newTile = $(`tile-input-${state.program}`);
  if (newTile) newTile.classList.add('output-active');

  const inp = state.inputs.find(i => i.id === inputId);
  if (inp) {
    const pgmName = $('program-source-name');
    if (pgmName) pgmName.textContent = inp.name;
  }
}

/**
 * Direct Instant CUT Take
 */
function executeCut() {
  if (state.isTransitioning) return;

  const btn = $('btn-center-cut');
  if (btn) {
    btn.style.filter = 'brightness(1.5)';
    setTimeout(() => { btn.style.filter = ''; }, 90);
  }

  const prevPgm = state.program;
  const prevPrv = state.preview;

  setProgram(prevPrv);
  setPreview(prevPgm);

  callVmixApi(`Cut&Input=${state.program}`);
}

/**
 * Automated TAKE / Quick Play Transition
 */
function executeTake() {
  if (state.isTransitioning) return;

  const btn = $('btn-center-quickplay');
  if (btn) {
    btn.style.filter = 'brightness(1.5)';
    setTimeout(() => { btn.style.filter = ''; }, 100);
  }

  state.isTransitioning = true;
  state.transitionProgress = 0.0;
  const duration = state.duration;
  const startTime = performance.now();

  function animate(now) {
    const elapsed = now - startTime;
    const progress = Math.min(1.0, elapsed / duration);
    state.transitionProgress = progress;

    if (progress < 1.0) {
      requestAnimationFrame(animate);
    } else {
      state.isTransitioning = false;
      state.transitionProgress = 0.0;
      const prevPgm = state.program;
      const prevPrv = state.preview;
      setProgram(prevPrv);
      setPreview(prevPgm);
      callVmixApi(`Transition&Type=${state.transition}&Duration=${duration}`);
    }
  }

  requestAnimationFrame(animate);
}

// ═══════════════════════════════════════════════════════════════════
// 4. HARDWARE T-BAR FADER INTERACTIVITY
// ═══════════════════════════════════════════════════════════════════
const tbarWrap = $('center-tbar-wrap');
const tbarFill = $('center-tbar-fill');
const tbarThumb = $('center-tbar-thumb');
let isDraggingTBar = false;

function updateTBarPosition(posNorm) {
  posNorm = Math.max(0.0, Math.min(1.0, posNorm));
  state.faderPos = posNorm;

  if (tbarFill) tbarFill.style.width = `${posNorm * 100}%`;
  if (tbarThumb) tbarThumb.style.left = `${posNorm * 100}%`;

  callVmixApi(`SetFader&Value=${posNorm.toFixed(3)}`);

  if (posNorm >= 0.99) {
    const prevPgm = state.program;
    const prevPrv = state.preview;
    setProgram(prevPrv);
    setPreview(prevPgm);
    setTimeout(resetTBar, 120);
  }
}

function resetTBar() {
  state.faderPos = 0.0;
  if (tbarFill) tbarFill.style.width = '0%';
  if (tbarThumb) tbarThumb.style.left = '0%';
  callVmixApi('SetFader&Value=0.0');
}

if (tbarWrap) {
  tbarWrap.addEventListener('mousedown', (e) => {
    isDraggingTBar = true;
    handleTBarMove(e);
  });

  window.addEventListener('mousemove', (e) => {
    if (!isDraggingTBar) return;
    handleTBarMove(e);
  });

  window.addEventListener('mouseup', () => {
    isDraggingTBar = false;
  });

  function handleTBarMove(e) {
    const rect = tbarWrap.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const norm = x / rect.width;
    updateTBarPosition(norm);
  }
}

// ═══════════════════════════════════════════════════════════════════
// 5. CENTER SWITCHER BUTTONS (CUT, QUICKPLAY, FTB, TRANSITIONS)
// ═══════════════════════════════════════════════════════════════════
$('btn-center-cut')?.addEventListener('click', executeCut);
$('btn-center-quickplay')?.addEventListener('click', executeTake);

// FTB
$('btn-center-ftb')?.addEventListener('click', () => {
  state.ftbActive = !state.ftbActive;
  $('btn-center-ftb').classList.toggle('active', state.ftbActive);
  $('ftb-curtain')?.classList.toggle('active', state.ftbActive);
  callVmixApi(`FadeToBlack&Active=${state.ftbActive}`);
});

// Transition Selector Buttons
$$('.trans-select-btn').forEach(btn => {
  btn.addEventListener('click', () => {
    $$('.trans-select-btn').forEach(b => b.classList.remove('active'));
    btn.classList.add('active');
    state.transition = btn.dataset.trans;
    executeTake();
  });
});

// Center Overlay Buttons 1-8
$$('.center-ov-btn').forEach(btn => {
  btn.addEventListener('click', () => {
    const ch = parseInt(btn.dataset.ov);
    if (state.activeOverlays.has(ch)) {
      state.activeOverlays.delete(ch);
      btn.classList.remove('active');
    } else {
      state.activeOverlays.add(ch);
      btn.classList.add('active');
    }
    callVmixApi(`OverlayInput${ch}&Input=7`);
  });
});

// ═══════════════════════════════════════════════════════════════════
// 6. INPUT TILES INTERACTION
// ═══════════════════════════════════════════════════════════════════
$$('.vmix-tile').forEach(tile => {
  const id = parseInt(tile.dataset.id);

  tile.addEventListener('click', (e) => {
    if (e.target.closest('button') || e.target.closest('input')) return;
    if (id !== state.program) {
      setPreview(id);
    }
  });

  tile.addEventListener('dblclick', (e) => {
    if (e.target.closest('button') || e.target.closest('input')) return;
    setPreview(id);
    setTimeout(executeTake, 30);
  });
});

// Tile GO Buttons (vMix 29 Programmable Option Gateway)
$$('.tile-btn.go-btn').forEach(btn => {
  btn.addEventListener('click', (e) => {
    e.stopPropagation();
    const tile = e.target.closest('.vmix-tile');
    const id = parseInt(tile.dataset.id);
    setPreview(id);
    setTimeout(executeTake, 40);
  });
});

// Tile Cut Buttons
$$('.tile-btn.cut-btn').forEach(btn => {
  btn.addEventListener('click', (e) => {
    e.stopPropagation();
    const tile = e.target.closest('.vmix-tile');
    const id = parseInt(tile.dataset.id);
    setPreview(id);
    setTimeout(executeCut, 20);
  });
});

// Tile Audio Toggle
$$('.tile-btn.audio-btn').forEach(btn => {
  btn.addEventListener('click', (e) => {
    e.stopPropagation();
    btn.classList.toggle('active');
    btn.classList.toggle('muted');
  });
});

// Tile Close (X in top right of tile)
$$('.tile-close-btn').forEach(btn => {
  btn.addEventListener('click', (e) => {
    e.stopPropagation();
    const tile = e.target.closest('.vmix-tile');
    const id = parseInt(tile.dataset.id);
    if (id === state.program || id === state.preview) {
      alert("Cannot close an input currently live on Output or Preview.");
      return;
    }
    tile.style.opacity = '0';
    tile.style.transform = 'scale(0.92)';
    tile.style.transition = 'all 0.18s';
    setTimeout(() => {
      tile.remove();
      const idx = state.inputs.findIndex(i => i.id === id);
      if (idx !== -1) state.inputs.splice(idx, 1);
    }, 180);
  });
});

// Tile Mini Overlays
$$('.m-ov').forEach(btn => {
  btn.addEventListener('click', (e) => {
    e.stopPropagation();
    btn.classList.toggle('active');
  });
});

// ═══════════════════════════════════════════════════════════════════
// 7. CATEGORY TABS & FILTERING
// ═══════════════════════════════════════════════════════════════════
$$('.cat-square').forEach(sq => {
  sq.addEventListener('click', () => {
    $$('.cat-square').forEach(s => s.classList.remove('active'));
    sq.classList.add('active');
    const cat = sq.dataset.cat;
    state.activeCategory = cat;
    filterInputTiles();
  });
});

const searchInput = $('cat-search-input');
if (searchInput) {
  searchInput.addEventListener('input', filterInputTiles);
}

function filterInputTiles() {
  const query = searchInput ? searchInput.value.toLowerCase().trim() : '';
  const cat = state.activeCategory;

  $$('.vmix-tile').forEach(tile => {
    const tileCat = tile.dataset.type;
    const name = tile.querySelector('.tile-name')?.textContent.toLowerCase() || '';
    const id = tile.dataset.id;

    const matchesCat = (cat === 'all' || tileCat === cat);
    const matchesQuery = (!query || name.includes(query) || id.includes(query));

    tile.style.display = (matchesCat && matchesQuery) ? 'flex' : 'none';
  });
}

// ═══════════════════════════════════════════════════════════════════
// 8. DOCKED AUDIO MIXER CONTROLS & INTERACTIVE FADERS
// ═══════════════════════════════════════════════════════════════════
$$('.strip-icon-btn.mute-btn').forEach(btn => {
  btn.addEventListener('click', (e) => {
    e.stopPropagation();
    btn.classList.toggle('active');
    const strip = btn.closest('.vmix-strip');
    const ch = strip ? strip.dataset.channel : '';
    callVmixApi(`SetAudioMute&Channel=${ch}&Muted=${!btn.classList.contains('active')}`);
  });
});

$$('.strip-icon-btn.solo-btn').forEach(btn => {
  btn.addEventListener('click', (e) => {
    e.stopPropagation();
    btn.classList.toggle('active');
  });
});

$$('.strip-icon-btn.afv-btn').forEach(btn => {
  btn.addEventListener('click', (e) => {
    e.stopPropagation();
    btn.classList.toggle('active');
  });
});

$$('.strip-icon-btn.route-btn').forEach(btn => {
  btn.addEventListener('click', (e) => {
    e.stopPropagation();
    btn.classList.toggle('active');
  });
});

$$('.strip-icon-btn.hp-btn').forEach(btn => {
  btn.addEventListener('click', (e) => {
    e.stopPropagation();
    btn.classList.toggle('active');
  });
});

$$('.strip-icon-btn.info-btn').forEach(btn => {
  btn.addEventListener('click', (e) => {
    e.stopPropagation();
    const strip = btn.closest('.vmix-strip');
    const ch = strip ? strip.dataset.channel : '';
    console.log(`Audio Settings requested for Channel: ${ch}`);
  });
});

// Interactive Fader Slider Dragging on Tracks
let activeFaderSlot = null;

function setFaderPosition(slot, clientY) {
  const rect = slot.getBoundingClientRect();
  const knob = slot.querySelector('.fader-thumb-knob');
  if (!knob) return;
  
  // Calculate position relative to bottom of track
  const offsetY = clientY - rect.top;
  let norm = 1.0 - (offsetY / rect.height);
  norm = Math.max(0.0, Math.min(1.0, norm));
  
  knob.style.bottom = `${norm * 100}%`;
  
  const faderName = slot.dataset.fader;
  callVmixApi(`SetVolume&Channel=${faderName}&Value=${(norm * 100).toFixed(0)}`);
}

$$('.fader-track-slot').forEach(slot => {
  slot.addEventListener('mousedown', (e) => {
    activeFaderSlot = slot;
    setFaderPosition(slot, e.clientY);
  });
});

window.addEventListener('mousemove', (e) => {
  if (activeFaderSlot) {
    setFaderPosition(activeFaderSlot, e.clientY);
  }
});

window.addEventListener('mouseup', () => {
  activeFaderSlot = null;
});

// Interactive Pan Slider Bars
$$('.strip-pan-bar').forEach(panBar => {
  panBar.addEventListener('click', (e) => {
    const rect = panBar.getBoundingClientRect();
    const knob = panBar.querySelector('.pan-knob');
    const zeroLabel = panBar.querySelector('.pan-zero');
    if (!knob) return;
    
    const clickX = e.clientX - rect.left;
    let norm = clickX / rect.width;
    norm = Math.max(0.1, Math.min(0.9, norm));
    knob.style.left = `${norm * 100}%`;
    
    const panVal = Math.round((norm - 0.5) * 200);
    if (zeroLabel) zeroLabel.textContent = panVal === 0 ? '0' : (panVal > 0 ? `R${panVal}` : `L${Math.abs(panVal)}`);
  });
  
  panBar.addEventListener('dblclick', () => {
    const knob = panBar.querySelector('.pan-knob');
    const zeroLabel = panBar.querySelector('.pan-zero');
    if (knob) knob.style.left = '50%';
    if (zeroLabel) zeroLabel.textContent = '0';
  });
});


// ═══════════════════════════════════════════════════════════════════
// 9. FOOTER ACTIONS & MODALS
// ═══════════════════════════════════════════════════════════════════
const addInputModal = $('modal-add-input');

function openAddInputModal() {
  if (addInputModal) addInputModal.style.display = 'flex';
}

function closeAddInputModal() {
  if (addInputModal) addInputModal.style.display = 'none';
}

$('btn-master-add-input')?.addEventListener('click', openAddInputModal);
$('btn-close-add-modal')?.addEventListener('click', closeAddInputModal);
$('btn-cancel-add-input')?.addEventListener('click', closeAddInputModal);

$$('.sidebar-cat-btn').forEach(btn => {
  btn.addEventListener('click', () => {
    $$('.sidebar-cat-btn').forEach(b => b.classList.remove('active'));
    btn.classList.add('active');
  });
});

$('btn-confirm-add-input')?.addEventListener('click', () => {
  const name = $('input-friendly-name').value || 'New Camera Feed';
  const newId = state.inputs.length + 1;

  const newInp = {
    id: newId,
    name: name,
    type: 'cam',
    catColor: '#2e7d32',
    res: '1080p59.94',
    audio: true,
    volume: 80,
    muted: false,
    solo: false,
    afv: true
  };
  state.inputs.push(newInp);

  const tile = document.createElement('article');
  tile.className = 'vmix-tile';
  tile.id = `tile-input-${newId}`;
  tile.dataset.id = newId;
  tile.dataset.type = 'cam';
  tile.innerHTML = `
    <header class="tile-header-bar green-banner">
      <span class="tile-num">${newId}</span>
      <span class="tile-name">${name}</span>
      <button class="tile-close-btn" title="Close (X)">✕</button>
    </header>
    <div class="tile-screen">
      <canvas class="tile-canvas" id="canvas-card-${newId}" width="320" height="180"></canvas>
    </div>
    <footer class="tile-footer-bar">
      <div class="tile-ov-grid-8">
        <div class="ov-mini-row"><span class="m-ov" data-ov="1">1</span><span class="m-ov" data-ov="2">2</span><span class="m-ov" data-ov="3">3</span><span class="m-ov" data-ov="4">4</span></div>
        <div class="ov-mini-row"><span class="m-ov" data-ov="5">5</span><span class="m-ov" data-ov="6">6</span><span class="m-ov" data-ov="7">7</span><span class="m-ov" data-ov="8">8</span></div>
      </div>
      <button class="tile-btn go-btn" title="GO Action">GO</button>
      <button class="tile-btn cut-btn" title="Cut">Cut</button>
      <button class="tile-btn audio-btn active" title="Audio">Audio</button>
      <button class="tile-btn gear-btn" title="Settings">⚙</button>
    </footer>
  `;

  $('input-tiles-grid').appendChild(tile);
  const c = $(`canvas-card-${newId}`);
  if (c) {
    cardCanvases[newId] = { canvas: c, ctx: c.getContext('2d') };
  }

  tile.addEventListener('click', () => setPreview(newId));
  tile.querySelector('.tile-btn.go-btn').addEventListener('click', (e) => {
    e.stopPropagation();
    setPreview(newId);
    setTimeout(executeTake, 40);
  });
  tile.querySelector('.tile-btn.cut-btn').addEventListener('click', (e) => {
    e.stopPropagation();
    setPreview(newId);
    setTimeout(executeCut, 20);
  });
  tile.querySelector('.tile-close-btn').addEventListener('click', (e) => {
    e.stopPropagation();
    tile.remove();
  });

  closeAddInputModal();
});

// Master Record Toggle
$('btn-master-record')?.addEventListener('click', () => {
  state.recording = !state.recording;
  $('btn-master-record').classList.toggle('record-active', state.recording);
  $('badge-top-rec')?.classList.toggle('active', state.recording);
  callVmixApi(`Record&Active=${state.recording}`);
});

// Master Stream Toggle
$('btn-master-stream')?.addEventListener('click', () => {
  state.streaming = !state.streaming;
  $('btn-master-stream').classList.toggle('stream-active', state.streaming);
  $('badge-top-stream')?.classList.toggle('active', state.streaming);
  callVmixApi(`Stream&Active=${state.streaming}`);
});

// ═══════════════════════════════════════════════════════════════════
// 10. CLOCKS, AUDIO METERS & BROADCAST TELEMETRY
// ═══════════════════════════════════════════════════════════════════
// Clocks tick
setInterval(() => {
  state.elapsedSeconds += 0.1;
  const m = Math.floor(state.elapsedSeconds / 60);
  const s = Math.floor(state.elapsedSeconds % 60);
  const d = Math.floor((state.elapsedSeconds * 10) % 10);
  const elapsedStr = `${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}.${d}`;
  const clockElapsed = $('clock-elapsed');
  if (clockElapsed) clockElapsed.textContent = elapsedStr;

  // Real Time of Day
  const now = new Date();
  let hours = now.getHours();
  const ampm = hours >= 12 ? 'PM' : 'AM';
  hours = hours % 12 || 12;
  const minutes = String(now.getMinutes()).padStart(2, '0');
  const todStr = `${String(hours).padStart(2, '0')}:${minutes} ${ampm}`;
  const clockTod = $('clock-tod');
  if (clockTod) clockTod.textContent = todStr;
}, 100);

// Audio dBFS meter animation
setInterval(() => {
  // Monitor Side Meters
  const prvFillL = $('prv-meter-l');
  const prvFillR = $('prv-meter-r');
  const pgmFillL = $('pgm-meter-l');
  const pgmFillR = $('pgm-meter-r');

  if (prvFillL) prvFillL.style.height = `${55 + Math.random() * 25}%`;
  if (prvFillR) prvFillR.style.height = `${58 + Math.random() * 24}%`;
  if (pgmFillL) pgmFillL.style.height = `${68 + Math.random() * 26}%`;
  if (pgmFillR) pgmFillR.style.height = `${70 + Math.random() * 25}%`;

  // Docked Audio Mixer LED Meters
  $$('.led-meter-fill').forEach(fill => {
    const base = parseFloat(fill.style.height) || 60;
    const jitter = (Math.random() - 0.5) * 20;
    const next = Math.max(15, Math.min(94, base + jitter));
    fill.style.height = `${next}%`;
  });
}, 80);

// Broadcast engine telemetry jitter
setInterval(() => {
  const fps = $('telem-fps');
  const render = $('telem-render');
  const cpu = $('telem-cpu');
  const gpu = $('telem-gpu');
  const total = $('telem-total');

  if (fps) fps.textContent = (29.98 + (Math.random() * 0.05)).toFixed(0);
  if (render) render.textContent = (1.0 + (Math.random() * 0.2)).toFixed(0) + ' ms';
  if (cpu) cpu.textContent = (14.8 + (Math.random() * 0.5)).toFixed(0) + '%';
  if (gpu) gpu.textContent = (0 + Math.floor(Math.random() * 2)) + '%';
  if (total) total.textContent = (24 + Math.floor(Math.random() * 3)) + '%';
}, 1800);

// ═══════════════════════════════════════════════════════════════════
// 11. WEBSOCKET & REST API SYNCHRONIZATION WITH C++ BACKEND
// ═══════════════════════════════════════════════════════════════════
function callVmixApi(queryStr) {
  fetch(`/api?Function=${queryStr}`)
    .then(res => res.text())
    .catch(() => {});
}

function initWebSocket() {
  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  const wsUrl = `${protocol}//${window.location.host}/ws`;
  try {
    const ws = new WebSocket(wsUrl);
    ws.onmessage = (event) => {
      try {
        const msg = JSON.parse(event.data);
        if (msg.type === 'STATE_UPDATE') {
          if (msg.preview) setPreview(msg.preview);
          if (msg.program) setProgram(msg.program);
        }
      } catch (e) {}
    };
    ws.onclose = () => {
      setTimeout(initWebSocket, 3000);
    };
  } catch (e) {}
}

initWebSocket();
setPreview(4);
setProgram(1);
console.log('StreamMix 29 Studio Broadcast Engine Initialized.');
