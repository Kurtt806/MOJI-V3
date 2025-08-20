const W = 128,
  H = 64;
const cvs = document.getElementById("c");
const ctx = cvs.getContext("2d");
ctx.imageSmoothingEnabled = false;
ctx.fillStyle = "#000";
ctx.fillRect(0, 0, W, H);

let drawing = false;
let last = null;
let brushSize = 2;
let erasing = false;

function applyBrushStyle() {
  ctx.fillStyle = erasing ? "#000" : "#fff";
}
applyBrushStyle();

// Mouse events
cvs.addEventListener("mousedown", (e) => {
  e.preventDefault();
  drawing = true;
  const p = getCanvasCoords(e.clientX, e.clientY);
  last = p;
  plot(p.x, p.y);
});
cvs.addEventListener("mouseup", () => {
  drawing = false;
  last = null;
});
cvs.addEventListener("mouseleave", () => {
  drawing = false;
  last = null;
});
cvs.addEventListener("mousemove", (e) => {
  e.preventDefault();
  if (!drawing) return;
  const p = getCanvasCoords(e.clientX, e.clientY);
  if (last) drawLine(last.x, last.y, p.x, p.y);
  last = p;
});

// Touch events
cvs.addEventListener("touchstart", (e) => {
  e.preventDefault();
  drawing = true;
  if (e.touches.length > 0) {
    const t = e.touches[0];
    const p = getCanvasCoords(t.clientX, t.clientY);
    last = p;
    plot(p.x, p.y);
  }
});
cvs.addEventListener("touchend", (e) => {
  e.preventDefault();
  drawing = false;
  last = null;
});
cvs.addEventListener("touchcancel", (e) => {
  e.preventDefault();
  drawing = false;
  last = null;
});
cvs.addEventListener("touchmove", (e) => {
  e.preventDefault();
  if (!drawing || e.touches.length === 0) return;
  const t = e.touches[0];
  const p = getCanvasCoords(t.clientX, t.clientY);
  if (last) drawLine(last.x, last.y, p.x, p.y);
  last = p;
});

function getCanvasCoords(clientX, clientY) {
  const rect = cvs.getBoundingClientRect();
  const x = Math.floor(((clientX - rect.left) / rect.width) * W);
  const y = Math.floor(((clientY - rect.top) / rect.height) * H);
  return {
    x: Math.max(0, Math.min(W - 1, x)),
    y: Math.max(0, Math.min(H - 1, y)),
  };
}

function plot(x, y) {
  const s = Math.max(1, Math.min(10, brushSize));
  const r = Math.floor(s / 2);
  ctx.fillRect(x - r, y - r, s, s);
}

function drawLine(x0, y0, x1, y1) {
  let dx = Math.abs(x1 - x0),
    sx = x0 < x1 ? 1 : -1;
  let dy = -Math.abs(y1 - y0),
    sy = y0 < y1 ? 1 : -1;
  let err = dx + dy,
    e2;
  for (;;) {
    plot(x0, y0);
    if (x0 === x1 && y0 === y1) break;
    e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

document.getElementById("clear").onclick = () => {
  ctx.fillStyle = "#000";
  ctx.fillRect(0, 0, W, H);
  applyBrushStyle();
  st("Canvas cleared");
};

// Mode buttons
const btnDraw = document.getElementById("modeDraw");
const btnErase = document.getElementById("modeErase");
btnDraw.onclick = () => {
  erasing = false;
  btnDraw.classList.add("active");
  btnErase.classList.remove("active");
  applyBrushStyle();
};
btnErase.onclick = () => {
  erasing = true;
  btnErase.classList.add("active");
  btnDraw.classList.remove("active");
  applyBrushStyle();
};

// Brush size slider
const sizeInput = document.getElementById("size");
const sizeVal = document.getElementById("sizeVal");
sizeInput.addEventListener("input", () => {
  brushSize = parseInt(sizeInput.value) || 1;
  sizeVal.textContent = brushSize;
});

function canvasTo1bpp() {
  const img = ctx.getImageData(0, 0, W, H).data;
  const out = new Uint8Array((W * H) / 8);
  for (let y = 0; y < H; y++) {
    for (let bx = 0; bx < W / 8; bx++) {
      let b = 0;
      for (let bit = 0; bit < 8; bit++) {
        const x = bx * 8 + bit;
        const idx = (y * W + x) * 4;
        const lum = img[idx];
        const on = lum > 127 ? 1 : 0;
        b |= on << bit;
      }
      out[y * (W / 8) + bx] = b;
    }
  }
  return out;
}

function rleEncode(bytes) {
  const out = [];
  let i = 0;
  const n = bytes.length;
  while (i < n) {
    const v = bytes[i];
    let run = 1;
    i++;
    while (i < n && bytes[i] === v && run < 255) {
      run++;
      i++;
    }
    out.push(run, v);
  }
  return new Uint8Array(out);
}

let ws;
function connect() {
  const url = `ws://${location.hostname}/ws`;
  ws = new WebSocket(url);

  ws.onopen = () => st("✅ Connected");
  ws.onclose = () => {
    st("❌ Disconnected");
    setTimeout(connect, 2000);
  };
  ws.onerror = () => st("⚠️ Connection Error");
  ws.onmessage = (ev) => console.log("Message received:", ev.data);
}

function st(s) {
  document.getElementById("st").textContent = s;
  console.log("Status:", s);
}

connect();

document.getElementById("send").onclick = () => {
  if (!ws || ws.readyState !== 1) {
    st("❌ WebSocket not ready");
    return;
  }
  const bytes = canvasTo1bpp();
  const rle = rleEncode(bytes);
  ws.send(rle);
  st(`✅ Sent ${rle.length} bytes`);

  const btn = document.getElementById("send");
  btn.style.transform = "scale(0.95)";
  setTimeout(() => (btn.style.transform = ""), 150);
};

document.getElementById("save").onclick = () => {
  const bytes = canvasTo1bpp();
  fetch("/save-draw", {
    method: "POST",
    headers: { "Content-Type": "application/octet-stream" },
    body: bytes,
  })
    .then((resp) => {
      if (resp.ok) {
        st("✅ Saved to flash");
        const btn = document.getElementById("save");
        btn.style.transform = "scale(0.95)";
        setTimeout(() => (btn.style.transform = ""), 150);
      } else {
        st("❌ Save error");
      }
    })
    .catch((err) => {
      st("❌ Connection error");
      console.error(err);
    });
};

document.getElementById("load").onclick = () => {
  fetch("/load-draw")
    .then((resp) => {
      if (!resp.ok) throw new Error("No saved file");
      return resp.arrayBuffer();
    })
    .then((buffer) => {
      const bytes = new Uint8Array(buffer);
      ctx.fillStyle = "#000";
      ctx.fillRect(0, 0, W, H);

      for (let y = 0; y < H; y++) {
        for (let bx = 0; bx < W / 8; bx++) {
          const b = bytes[y * (W / 8) + bx];
          for (let bit = 0; bit < 8; bit++) {
            const x = bx * 8 + bit;
            const on = (b >> bit) & 1;
            if (on) {
              ctx.fillStyle = "#fff";
              ctx.fillRect(x, y, 1, 1);
            }
          }
        }
      }
      applyBrushStyle();
      st("✅ Loaded from flash");
      const btn = document.getElementById("load");
      btn.style.transform = "scale(0.95)";
      setTimeout(() => (btn.style.transform = ""), 150);
    })
    .catch((err) => {
      st("❌ No saved file");
      console.error(err);
    });
};

// --- ADDED: fetch system info and update UI ---
async function updateSystemInfo() {
  try {
    const res = await fetch("/system-info", { cache: "no-store" });
    if (!res.ok) return;
    let j;
    try {
      j = await res.json();
      console.log('system-info fetched', j);
    } catch (err) {
      console.error('Failed to parse system-info JSON', err);
      return;
    }
    const fmt = (n) =>
      typeof n === "number"
        ? n >= 1024
          ? (n / 1024).toFixed(1) + " KB"
          : n + " B"
        : "-";

    const freeHeapEl = document.getElementById("freeHeap");
    const heapSizeEl = document.getElementById("heapSize");
    const maxAllocEl = document.getElementById("maxAlloc");
    const fsTotalEl = document.getElementById("fsTotal");
    const fsUsedEl = document.getElementById("fsUsed");
  const flashSizeEl = document.getElementById("flashSize");
  const sketchSizeEl = document.getElementById("sketchSize");
  const freeSketchEl = document.getElementById("freeSketch");

    // tolerate numeric values provided as strings
    const num = (v) => {
      if (v === undefined || v === null) return undefined;
      if (typeof v === 'number') return v;
      const n = Number(v);
      return Number.isFinite(n) ? n : undefined;
    };

    if (freeHeapEl) freeHeapEl.textContent = fmt(num(j.freeHeap));
    if (heapSizeEl) heapSizeEl.textContent = fmt(num(j.heapSize));
    if (maxAllocEl)
      maxAllocEl.textContent = fmt(num(j.maxAllocHeap ?? j.maxAlloc));
    if (fsTotalEl) fsTotalEl.textContent = fmt(num(j.fsTotal));
    if (fsUsedEl) fsUsedEl.textContent = fmt(num(j.fsUsed));
    if (flashSizeEl) flashSizeEl.textContent = fmt(num(j.flashSize));
    if (sketchSizeEl) sketchSizeEl.textContent = fmt(num(j.sketchSize));
    if (freeSketchEl) freeSketchEl.textContent = fmt(num(j.freeSketchSpace ?? j.freeSketch));
  } catch (e) {}
}

updateSystemInfo();
setInterval(updateSystemInfo, 5000);

// Prevent scrolling when touching the canvas
document.body.addEventListener(
  "touchstart",
  (e) => {
    if (e.target === cvs) e.preventDefault();
  },
  { passive: false }
);
document.body.addEventListener(
  "touchend",
  (e) => {
    if (e.target === cvs) e.preventDefault();
  },
  { passive: false }
);
document.body.addEventListener(
  "touchmove",
  (e) => {
    if (e.target === cvs) e.preventDefault();
  },
  { passive: false }
);

// --- Display text UI handlers ---
const displayInput = document.getElementById("displayTextInput");
const displayBtn = document.getElementById("displayTextBtn");
const fetchTextBtn = document.getElementById("fetchTextBtn");
const textFontSelect = document.getElementById("textFontSelect");

async function postDisplayText(txt) {
  try {
    const form = new URLSearchParams();
    form.append("text", txt);
    const res = await fetch("/display-text", {
      method: "POST",
      body: form,
    });
    if (res.ok) {
      st("✅ Text saved");
    } else {
      st("❌ Save failed");
    }
  } catch (e) {
    st("❌ Connection error");
  }
}

displayBtn && displayBtn.addEventListener("click", () => {
  const v = displayInput.value || "";
  postDisplayText(v);
});

fetchTextBtn && fetchTextBtn.addEventListener("click", async () => {
  try {
    const r = await fetch("/display-text", { cache: "no-store" });
    if (!r.ok) throw new Error("no");
    const t = await r.text();
    displayInput.value = t;
    st("✅ Fetched");
  } catch (e) {
    st("❌ Fetch failed");
  }
});

// try load current text on start
(async function () {
  try {
    const r = await fetch("/display-text", { cache: "no-store" });
    if (!r.ok) return;
    const t = await r.text();
    if (t && displayInput) displayInput.value = t;
  } catch (e) {}
})();

// load available text fonts and populate select
async function loadTextFonts() {
  try {
    const r = await fetch('/text-fonts', { cache: 'no-store' });
    if (!r.ok) return;
    const j = await r.json();
    textFontSelect.innerHTML = '';
    for (let i = 0; i < j.count; i++) {
      const opt = document.createElement('option');
      opt.value = i;
      opt.textContent = j.names[i] || ('font' + i);
      textFontSelect.appendChild(opt);
    }
    if (j.current !== undefined) textFontSelect.value = j.current;
  } catch (e) {}
}

textFontSelect && textFontSelect.addEventListener('change', async () => {
  try {
    const idx = textFontSelect.value;
    const form = new URLSearchParams();
    form.append('index', idx);
    const r = await fetch('/text-fonts', { method: 'POST', body: form });
    if (r.ok) st('✅ Font changed'); else st('❌ Font change failed');
  } catch (e) { st('❌ Error'); }
});

loadTextFonts();
