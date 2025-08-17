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
