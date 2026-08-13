/**
 * Puppeteer harness for local_jpeg_stream.
 * Opens the ESP-served HTTPS test page, drives connect/accept, and reports
 * browser-side JPEG frame / DC / ICE status as JSON lines for test.py.
 *
 * Usage:
 *   node harness.mjs --url https://192.168.x.x/webrtc/test?autoconnect=1
 * Env:
 *   JPEG_MIN_FRAMES (default 3)
 *   JPEG_DURATION_MS (stream hold time after Accept, default 60000)
 *   JPEG_WAIT_MS (overall budget including connect; default duration+30s)
 *   JPEG_SEND_VIDEO (1=send camera over DC, 0=receive-only; default 1)
 *   JPEG_REAL_CAMERA (1=real webcam, 0=Chromium fake device; default 0)
 *                 Keeps --use-fake-ui-for-media-stream so permission stays automatic
 *   BROWSER_HEADLESS (default 1; set 0 to show browser)
 */
import puppeteer from 'puppeteer';

function arg(name, fallback) {
  const i = process.argv.indexOf(name);
  if (i >= 0 && process.argv[i + 1]) return process.argv[i + 1];
  return fallback;
}

const pageUrl = arg('--url', process.env.JPEG_PAGE_URL || '');
const minFrames = Number(process.env.JPEG_MIN_FRAMES || arg('--min-frames', '3'));
const durationMs = Number(
  process.env.JPEG_DURATION_MS || arg('--duration-ms', process.env.JPEG_WAIT_MS || '60000')
);
const waitMs = Number(process.env.JPEG_WAIT_MS || String(durationMs + 30000));
const sendVideo = process.env.JPEG_SEND_VIDEO !== '0';
const realCamera = process.env.JPEG_REAL_CAMERA === '1';
const exitEarly = process.env.JPEG_EXIT_EARLY === '1';

function emit(obj) {
  console.log(JSON.stringify(obj));
}

async function main() {
  if (!pageUrl) {
    emit({ type: 'result', ok: false, error: 'missing --url' });
    process.exit(2);
  }

  const chromiumArgs = [
    '--no-sandbox',
    '--disable-setuid-sandbox',
    '--ignore-certificate-errors',
    '--allow-insecure-localhost',
    // Auto-grant getUserMedia permission dialog (no manual click)
    '--use-fake-ui-for-media-stream',
    '--autoplay-policy=no-user-gesture-required',
    '--disable-features=Translate,MediaRouter,WebRtcHideLocalIpsWithMdns',
  ];
  // Default: fake patterned camera for CI/headless. Real camera when requested.
  if (!realCamera) {
    chromiumArgs.push('--use-fake-device-for-media-stream');
  }

  // Real webcam frames are often black/frozen in headless; prefer headed unless overridden
  const headlessEnv = process.env.BROWSER_HEADLESS;
  const headless = realCamera
    ? (headlessEnv === '1') // real camera: headed unless explicitly forced headless
    : (headlessEnv !== '0');

  const launchOpts = {
    headless,
    args: chromiumArgs,
    ignoreHTTPSErrors: true,
  };
  if (process.env.PUPPETEER_EXECUTABLE_PATH) {
    launchOpts.executablePath = process.env.PUPPETEER_EXECUTABLE_PATH;
  }
  emit({ type: 'launch', real_camera: realCamera, send_camera: sendVideo, headless: launchOpts.headless });
  const browser = await puppeteer.launch(launchOpts);

  const page = await browser.newPage();
  page.on('console', (msg) => emit({ type: 'browser_log', text: msg.text() }));

  try {
    emit({ type: 'nav', url: pageUrl });
    await page.goto(pageUrl, { waitUntil: 'domcontentloaded', timeout: 30000 });

    // Ensure signaling connected
    await page.waitForFunction(() => {
      const s = document.getElementById('txtStatus');
      return s && /Signaling Connected|Idle \(Signaling/.test(s.textContent || '');
    }, { timeout: 20000 }).catch(() => null);

    // Connect if not auto
    const status = await page.$eval('#txtStatus', (el) => el.textContent);
    if (!/Signaling Connected|Idle \(Signaling/.test(status || '')) {
      await page.click('#btnConnect');
      await page.waitForFunction(() => {
        const s = document.getElementById('txtStatus');
        return s && /Signaling Connected|Idle \(Signaling|Incoming/.test(s.textContent || '');
      }, { timeout: 20000 });
    }

    // Apply send-camera preference before Accept (ensureLocalMedia reads the checkbox)
    await page.$eval('#chkSendVideo', (el, on) => { el.checked = !!on; }, sendVideo);
    emit({ type: 'config', send_camera: sendVideo, real_camera: realCamera });

    // Inject send-path probes (works even if firmware HTML not rebuilt yet)
    await page.evaluate(() => {
      window.__jpeg_sent_frames = 0;
      window.__jpeg_sent_bytes = 0;
      window.__jpeg_sent_sizes = [];
      window.__jpeg_canvas_avg = 0;
      const v = document.getElementById('localVideo');
      if (v) {
        v.muted = true;
        v.playsInline = true;
      }
      // Hook DC send to count outbound JPEG frames and full reassembled size
      const _send = RTCDataChannel.prototype.send;
      let _frameBytes = 0;
      RTCDataChannel.prototype.send = function (data) {
        try {
          let u8 = null;
          if (data instanceof ArrayBuffer) u8 = new Uint8Array(data);
          else if (ArrayBuffer.isView(data)) u8 = new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
          if (u8 && u8.length >= 5) {
            const chunkId = u8[0];
            const seq = chunkId & 0x7f;
            const isEnd = (chunkId & 0x80) !== 0;
            const plen = (u8[1] << 24) | (u8[2] << 16) | (u8[3] << 8) | u8[4];
            if (seq === 0) _frameBytes = 0; // BOS
            _frameBytes += plen;
            window.__jpeg_sent_bytes = (window.__jpeg_sent_bytes || 0) + u8.length;
            if (isEnd) {
              window.__jpeg_sent_frames = (window.__jpeg_sent_frames || 0) + 1;
              const sizes = window.__jpeg_sent_sizes || (window.__jpeg_sent_sizes = []);
              if (sizes.length < 32) sizes.push(_frameBytes);
              _frameBytes = 0;
            }
          }
        } catch (_) {}
        return _send.call(this, data);
      };
    });

    emit({ type: 'ready' });
    // Tell test.py signaling is up so it can send `cmd ring` now (RING before SSE is lost)
    emit({ type: 'signaling_ready' });

    // Wait for ESP RING (Accept enabled). Fallback: browser RING then ACCEPT_CALL.
    const acceptReady = await page.waitForFunction(() => {
      const b = document.getElementById('btnAccept');
      return b && !b.disabled;
    }, { timeout: Math.min(waitMs, 20000) }).then(() => true).catch(() => false);

    if (!acceptReady) {
      await page.click('#btnRing');
      emit({ type: 'action', name: 'ring' });
      await new Promise((r) => setTimeout(r, 500));
    }

    // Force Accept even if button still disabled (browser-initiated RING path)
    await page.evaluate(() => {
      const b = document.getElementById('btnAccept');
      if (b) {
        b.disabled = false;
        b.click();
      }
    });
    emit({ type: 'action', name: 'accept' });
    emit({ type: 'stream_start', durationMs });

    // Confirm local camera path after Accept (getUserMedia + send loop)
    await new Promise((r) => setTimeout(r, 2000));
    const mediaInfo = await page.evaluate(async () => {
      const v = document.getElementById('localVideo');
      const chk = document.getElementById('chkSendVideo');
      const canvas = document.getElementById('sendCanvas');
      if (v && v.srcObject) {
        try { await v.play(); } catch (_) {}
      }
      let canvasAvg = 0;
      if (v && canvas && v.videoWidth) {
        const ctx = canvas.getContext('2d');
        if (canvas.width !== 64) { canvas.width = 64; canvas.height = 48; }
        ctx.drawImage(v, 0, 0, 64, 48);
        const sample = ctx.getImageData(0, 0, 64, 48).data;
        let sum = 0;
        for (let i = 0; i < sample.length; i += 4) sum += sample[i] + sample[i + 1] + sample[i + 2];
        canvasAvg = sum / ((sample.length / 4) * 3);
        window.__jpeg_canvas_avg = canvasAvg;
      }
      const stream = v && v.srcObject;
      const tracks = stream ? stream.getVideoTracks().map(t => ({
        label: t.label, enabled: t.enabled, muted: t.muted, readyState: t.readyState,
      })) : [];
      return {
        sendChecked: !!(chk && chk.checked),
        videoWidth: v ? v.videoWidth : 0,
        videoHeight: v ? v.videoHeight : 0,
        readyState: v ? v.readyState : 0,
        paused: v ? v.paused : true,
        canvasAvg,
        trackLabels: tracks.map(t => t.label),
        tracks,
      };
    });
    emit({ type: 'local_media', ...mediaInfo });

    const deadline = Date.now() + durationMs;
    let last = { frames: 0, dc: false, ice: 'new' };
    let sawOk = false;
    let sendStats = { sentFrames: 0, sentBytes: 0, sizes: [], canvasAvg: 0 };
    while (Date.now() < deadline) {
      last = await page.evaluate(() => ({
        frames: window.__jpeg_frames || 0,
        dc: !!window.__jpeg_dc_open,
        ice: window.__jpeg_ice || 'unknown',
        status: (document.getElementById('txtStatus') || {}).textContent || '',
        sentFrames: window.__jpeg_sent_frames || 0,
        sentBytes: window.__jpeg_sent_bytes || 0,
        sizes: (window.__jpeg_sent_sizes || []).slice(),
        canvasAvg: window.__jpeg_canvas_avg || 0,
      }));
      sendStats = {
        sentFrames: last.sentFrames,
        sentBytes: last.sentBytes,
        sizes: last.sizes,
        canvasAvg: last.canvasAvg,
      };
      emit({
        type: 'progress',
        frames: last.frames,
        dc: last.dc,
        ice: last.ice,
        status: last.status,
        sentFrames: last.sentFrames,
        sentBytes: last.sentBytes,
        canvasAvg: Math.round(last.canvasAvg),
        remainMs: Math.max(0, deadline - Date.now()),
      });
      if (last.dc && last.frames >= minFrames &&
          (last.ice === 'connected' || last.ice === 'completed' || last.status.includes('CONNECTED'))) {
        sawOk = true;
        if (exitEarly) {
          emit({ type: 'result', ok: true, ...last, minFrames, durationMs, sendStats });
          await browser.close();
          process.exit(0);
        }
      }
      await new Promise((r) => setTimeout(r, 1000));
    }

    // Liveness heuristic: real camera should not be near-black and JPEG sizes should vary
    const sizes = sendStats.sizes || [];
    const sizeMin = sizes.length ? Math.min(...sizes) : 0;
    const sizeMax = sizes.length ? Math.max(...sizes) : 0;
    const likelyLive = sendStats.sentFrames > 0 && sendStats.canvasAvg > 5 && (sizeMax - sizeMin > 200 || sizeMax > 5000);
    emit({
      type: 'send_stats',
      ...sendStats,
      sizeMin,
      sizeMax,
      likelyLive,
    });

    emit({
      type: 'result',
      ok: sawOk,
      error: sawOk ? undefined : 'timeout waiting for frames',
      frames: last.frames,
      dc: last.dc,
      ice: last.ice,
      status: last.status,
      minFrames,
      durationMs,
      sendStats: { ...sendStats, sizeMin, sizeMax, likelyLive },
    });
    await browser.close();
    process.exit(sawOk ? 0 : 1);
  } catch (e) {
    emit({ type: 'result', ok: false, error: String(e && e.message ? e.message : e) });
    try { await browser.close(); } catch (_) {}
    process.exit(1);
  }
}

main();
