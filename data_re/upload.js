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

form.addEventListener("submit", (e) => {
  submitBtn.disabled = true;
  submitBtn.textContent = "🔄 Uploading...";
  messageDiv.innerHTML = "";

  // Reset button after 5 seconds in case of no response
  setTimeout(() => {
    if (submitBtn.disabled) {
      submitBtn.disabled = false;
      submitBtn.textContent = "🚀 Upload to OLED";
    }
  }, 5000);
});

// Check for success/error messages from server redirect
const urlParams = new URLSearchParams(window.location.search);
if (urlParams.get("success") === "1") {
  messageDiv.innerHTML =
    '<div class="message success">✅ Upload Successful!</div>';
} else if (urlParams.get("error") === "1") {
  messageDiv.innerHTML = '<div class="message error">❌ Upload Failed</div>';
}
