const speedRange = document.getElementById('speedRange');
const speedInput = document.getElementById('speedInput');
const speedLabel = document.getElementById('speedLabel');
const saveBtn = document.getElementById('saveBtn');
const resetBtn = document.getElementById('resetBtn');
const messageDiv = document.getElementById('message');

function setMessage(text, cls) {
  messageDiv.innerHTML = text ? `<div class="${cls}">${text}</div>` : '';
}

async function fetchSpeed() {
  try {
    const res = await fetch('/gif-speed', { cache: 'no-store' });
    if (!res.ok) throw new Error('Network');
    const j = await res.json();
    const s = parseFloat(j.speed) || 1.0;
    updateUI(s);
  } catch (e) {
    // ignore silently; keep defaults
  }
}

function updateUI(s) {
  const v = Number(s).toFixed(2);
  speedRange.value = v;
  speedInput.value = v;
  speedLabel.textContent = v;
}

speedRange.addEventListener('input', (e) => {
  const v = Number(e.target.value).toFixed(2);
  speedInput.value = v;
  speedLabel.textContent = v;
});

speedInput.addEventListener('change', (e) => {
  let v = Number(e.target.value);
  if (isNaN(v) || v <= 0) v = 1.0;
  v = Math.min(4.0, Math.max(0.1, v));
  const s = v.toFixed(2);
  speedRange.value = s;
  speedInput.value = s;
  speedLabel.textContent = s;
});

saveBtn.addEventListener('click', async () => {
  const v = Number(speedInput.value).toFixed(2);
  setMessage('Saving...', '');
  try {
    const body = new URLSearchParams();
    body.append('speed', v);
    const res = await fetch('/gif-speed', { method: 'POST', body });
    if (!res.ok) throw new Error('Save failed');
    const j = await res.json();
    updateUI(parseFloat(j.speed));
    setMessage('Saved ✓', 'success');
  } catch (e) {
    setMessage('Error saving speed', 'error');
  }
});

resetBtn.addEventListener('click', () => {
  updateUI(1.0);
  setMessage('', '');
});

// Poll current speed every 3s so UI stays in sync if changed elsewhere
fetchSpeed();
setInterval(fetchSpeed, 3000);
