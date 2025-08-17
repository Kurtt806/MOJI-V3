// Cập nhật trạng thái kết nối WiFi
function updateStatus() {
  fetch("/wifi-status")
    .then((response) => response.json())
    .then((data) => {
      const statusBox = document.getElementById("wifiStatus");
      const textEl = document.getElementById("statusText");

      document.getElementById("currentSsid").textContent =
        data.ssid || "Not connected";
      document.getElementById("currentIp").textContent = data.ip || "None";
      document.getElementById("currentRssi").textContent = data.rssi
        ? data.rssi + " dBm"
        : "None";

      if (data.connected) {
        statusBox.className = "status-box connected";
        textEl.textContent = "Connected";
      } else {
        statusBox.className = "status-box disconnected";
        textEl.textContent = "Disconnected";
      }
    })
    .catch(() => {
      document.getElementById("statusText").textContent = "Connection Error";
    });
}

// Quét mạng WiFi với polling liên tục cho đến khi xong
function scanWifi() {
  const resultsDiv = document.getElementById("scanResults");
  resultsDiv.style.display = "block";
  resultsDiv.innerHTML = `
      <div class="loading">
        <div class="spinner"></div>
        <div>Scanning networks...</div>
      </div>
    `;

  function pollScan() {
    fetch("/wifi-scan")
      .then((response) => response.json())
      .then((data) => {
        // Nếu server báo đang quét thì tiếp tục chờ
        if (
          data.status &&
          (data.status === "started" || data.status === "scanning")
        ) {
          setTimeout(pollScan, 1000);
          return;
        }

        // Khi đã có kết quả quét
        resultsDiv.innerHTML = "";
        if (data.networks && data.networks.length > 0) {
          data.networks.forEach((network) => {
            const div = document.createElement("div");
            div.className = "network-item";
            div.innerHTML = `
                <div>
                  <div class="network-name">${network.ssid}</div>
                  <div class="network-details">${
                    network.encryption ? "🔒 Secured" : "🔓 Open"
                  }</div>
                </div>
                <div class="network-details">${network.rssi} dBm</div>
              `;
            div.onclick = () => {
              document
                .querySelectorAll(".network-item")
                .forEach((el) => el.classList.remove("selected"));
              div.classList.add("selected");
              document.getElementById("ssid").value = network.ssid;
            };
            resultsDiv.appendChild(div);
          });
        } else {
          resultsDiv.innerHTML = `
              <div class="loading">
                <div>No networks found</div>
              </div>
            `;
        }
      })
      .catch(() => {
        resultsDiv.innerHTML = `
            <div class="loading">
              <div>Scan failed</div>
            </div>
          `;
      });
  }

  pollScan(); // Bắt đầu polling
}

// Gửi cấu hình WiFi
document.getElementById("wifiForm").addEventListener("submit", function (e) {
  e.preventDefault();

  const ssid = document.getElementById("ssid").value;
  const password = document.getElementById("password").value;

  if (!ssid.trim()) {
    alert("Please enter a network name");
    return;
  }

  const submitBtn = e.target.querySelector('button[type="submit"]');
  submitBtn.textContent = "💾 Saving...";
  submitBtn.disabled = true;

  fetch("/wifi-config", {
    method: "POST",
    headers: {
      "Content-Type": "application/x-www-form-urlencoded",
    },
    body: `ssid=${encodeURIComponent(ssid)}&password=${encodeURIComponent(
      password
    )}`,
  })
    .then((response) => response.json())
    .then((data) => {
      if (data.success) {
        alert(
          "✅ WiFi configuration saved successfully!\nDevice will attempt to connect..."
        );
        setTimeout(updateStatus, 3000);
      } else {
        alert("❌ Error: " + (data.message || "Failed to save configuration"));
      }
    })
    .catch((err) => {
      alert("❌ Connection error: " + err.message);
    })
    .finally(() => {
      submitBtn.textContent = "💾 Save & Connect";
      submitBtn.disabled = false;
    });
});

updateStatus();
setInterval(updateStatus, 5000);
