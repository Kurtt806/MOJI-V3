const fileInput = document.getElementById("fileInput");
const fileButton = document.getElementById("fileButton");
const form = document.querySelector("form");
const submitBtn = document.getElementById("submitBtn");
const messageDiv = document.getElementById("message");

fileButton.addEventListener("click", () => {
  fileInput.click();
});

fileInput.addEventListener("change", (e) => {
  const file = e.target.files[0];
  if (file) {
    fileButton.classList.add("has-file");
    fileButton.innerHTML = `
          ✅ ${file.name}<br>
          <small style="opacity: 0.7; text-transform: none;">Size: ${(
            file.size / 1024
          ).toFixed(1)} KB</small>
        `;
  } else {
    fileButton.classList.remove("has-file");
    fileButton.innerHTML = `
          📁 Choose GIF File<br>
          <small style="opacity: 0.7; text-transform: none;">Drag & drop or click to browse</small>
        `;
  }
});

// Drag and drop
fileButton.addEventListener("dragover", (e) => {
  e.preventDefault();
  fileButton.style.borderColor = "var(--retro-cyan)";
  fileButton.style.background = "rgba(0,255,255,0.1)";
});

fileButton.addEventListener("dragleave", (e) => {
  e.preventDefault();
  fileButton.style.borderColor = "var(--retro-green)";
  fileButton.style.background = "var(--darker-bg)";
});

fileButton.addEventListener("drop", (e) => {
  e.preventDefault();
  fileButton.style.borderColor = "var(--retro-green)";
  fileButton.style.background = "var(--darker-bg)";

  const files = e.dataTransfer.files;
  if (files.length > 0 && files[0].type === "image/gif") {
    fileInput.files = files;
    fileInput.dispatchEvent(new Event("change"));
  }
});

const uploadForm = document.getElementById("uploadForm");
const uploadProgress = document.getElementById("uploadProgress");
const progressText = document.getElementById("progressText");

uploadForm.addEventListener("submit", (e) => {
  e.preventDefault();
  const file = fileInput.files[0];
  if (!file) {
    messageDiv.innerHTML = '<div class="message error">Please choose a GIF file</div>';
    return;
  }

  submitBtn.disabled = true;
  submitBtn.textContent = "🔄 Uploading...";
  messageDiv.innerHTML = "";

  const xhr = new XMLHttpRequest();
  xhr.open("POST", "/save-gif");

  xhr.upload.onprogress = (ev) => {
    if (ev.lengthComputable) {
      const pct = Math.round((ev.loaded / ev.total) * 100);
      uploadProgress.value = pct;
      progressText.textContent = pct + "%";
    }
  };

  xhr.onload = () => {
    submitBtn.disabled = false;
    submitBtn.textContent = "🚀 Upload to OLED";
    if (xhr.status >= 200 && xhr.status < 300) {
      messageDiv.innerHTML = '<div class="message success">✅ Upload Successful!</div>';
      uploadProgress.value = 100;
      progressText.textContent = "100%";
    } else {
      messageDiv.innerHTML = '<div class="message error">❌ Upload Failed</div>';
    }
  };

  xhr.onerror = () => {
    submitBtn.disabled = false;
    submitBtn.textContent = "🚀 Upload to OLED";
    messageDiv.innerHTML = '<div class="message error">❌ Network Error</div>';
  };

  const formData = new FormData();
  formData.append("file", file);
  xhr.send(formData);
});

// Check for success/error messages from server redirect
const urlParams = new URLSearchParams(window.location.search);
if (urlParams.get("success") === "1") {
  messageDiv.innerHTML =
    '<div class="message success">✅ Upload Successful!</div>';
} else if (urlParams.get("error") === "1") {
  messageDiv.innerHTML = '<div class="message error">❌ Upload Failed</div>';
}

// Poll system-info to update SPIFFS stats
async function updateFsInfo() {
  try {
    const res = await fetch('/system-info', { cache: 'no-store' });
    if (!res.ok) return;
    const j = await res.json();
    const fmt = (n) =>
      typeof n === 'number'
        ? n >= 1024
          ? (n / 1024).toFixed(1) + ' KB'
          : n + ' B'
        : '-';
    const fsTotalEl = document.getElementById('fsTotal');
    const fsUsedEl = document.getElementById('fsUsed');
  const flashSizeEl = document.getElementById('flashSize');
  const sketchSizeEl = document.getElementById('sketchSize');
  const freeSketchEl = document.getElementById('freeSketch');
    if (fsTotalEl) fsTotalEl.textContent = fmt(j.fsTotal);
    if (fsUsedEl) fsUsedEl.textContent = fmt(j.fsUsed);
  if (flashSizeEl) flashSizeEl.textContent = fmt(j.flashSize);
  if (sketchSizeEl) sketchSizeEl.textContent = fmt(j.sketchSize);
  if (freeSketchEl) freeSketchEl.textContent = fmt(j.freeSketchSpace || j.freeSketch || '-');
  } catch (e) {}
}

updateFsInfo();
setInterval(updateFsInfo, 5000);
