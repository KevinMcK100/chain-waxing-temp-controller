#ifndef WAX_WEBSERVER_H
#define WAX_WEBSERVER_H

#include <WebServer.h>
#include <Preferences.h>

// External declarations from wax_controller.ino
extern WebServer server;
extern float tempC;
extern float targetTemp;
extern float bulkCoastMargin;
extern float holdReheatTrigger;
extern float holdCoastMargin;
extern bool heaterState;
extern bool isCoasting;
extern bool isHolding;
extern Preferences preferences;

#define HISTORY_SIZE 2000
#define CHART_POINTS 300
extern float tempHistory[];
extern uint8_t phaseHistory[];
extern int historyIndex;
extern bool historyFull;

inline void handleData() {
  // Static buffer — zero heap allocation per request.
  // Sized for ~300 points: history (~1500 chars) + phases (~600) + header (~120).
  static char buf[4096];

  int available = historyFull ? HISTORY_SIZE : historyIndex;
  int count = min(available, CHART_POINTS);
  int startIdx;
  if (historyFull) {
    startIdx = (historyIndex + HISTORY_SIZE - count) % HISTORY_SIZE;
  } else {
    startIdx = historyIndex - count;
  }

  int pos = 0;
  pos += snprintf(buf + pos, sizeof(buf) - pos,
    "{\"temp\":%.1f,\"target\":%.1f,\"heater\":%s,\"coasting\":%s,\"holding\":%s,\"history\":[",
    tempC, targetTemp,
    heaterState ? "true" : "false",
    isCoasting ? "true" : "false",
    isHolding ? "true" : "false");

  for (int i = 0; i < count && pos < (int)sizeof(buf) - 10; i++) {
    int idx = (startIdx + i) % HISTORY_SIZE;
    pos += snprintf(buf + pos, sizeof(buf) - pos, "%.1f%s",
      tempHistory[idx], (i < count - 1) ? "," : "");
  }

  pos += snprintf(buf + pos, sizeof(buf) - pos, "],\"phases\":[");

  for (int i = 0; i < count && pos < (int)sizeof(buf) - 10; i++) {
    int idx = (startIdx + i) % HISTORY_SIZE;
    pos += snprintf(buf + pos, sizeof(buf) - pos, "%d%s",
      (int)phaseHistory[idx], (i < count - 1) ? "," : "");
  }

  pos += snprintf(buf + pos, sizeof(buf) - pos, "]}");

  server.send(200, "application/json", buf);
}

inline void handleUpdate() {
  if (server.hasArg("target")) targetTemp = server.arg("target").toFloat();
  if (server.hasArg("bulkcoast")) bulkCoastMargin = server.arg("bulkcoast").toFloat();
  if (server.hasArg("holdtrig")) holdReheatTrigger = server.arg("holdtrig").toFloat();
  if (server.hasArg("holdcoast")) holdCoastMargin = server.arg("holdcoast").toFloat();

  // Safety validation guard
  if (holdCoastMargin >= holdReheatTrigger) {
    holdCoastMargin = holdReheatTrigger / 2.0f;
  }

  preferences.begin("wax-config", false);
  preferences.putFloat("target", targetTemp);
  preferences.putFloat("bulkcoast", bulkCoastMargin);
  preferences.putFloat("holdtrig", holdReheatTrigger);
  preferences.putFloat("holdcoast", holdCoastMargin);
  preferences.end();

  server.sendHeader("Location", "/", true);
  server.send(303);
}

inline void handleRoot() {
  String html;
  html.reserve(12288);
  html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Chain Waxer Dashboard</title>";
  html += "<script src='https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js'></script>";
  html += "<style>";
  html += "body { font-family: sans-serif; background: #121212; color: #e0e0e0; margin: 0; padding: 20px; }";
  html += ".container { max-width: 600px; margin: auto; background: #1e1e1e; padding: 20px; border-radius: 8px; box-shadow: 0 4px 10px rgba(0,0,0,0.5); }";
  html += "h1 { text-align: center; color: #ff9800; }";
  html += ".card { background: #2d2d2d; padding: 15px; margin-bottom: 15px; border-radius: 6px; }";
  html += "label { display: block; margin-top: 10px; font-weight: bold; }";
  html += "input[type='number'] { width: 100%; padding: 8px; margin-top: 5px; background: #333; color: #fff; border: 1px solid #444; border-radius: 4px; box-sizing: border-box; }";
  html += "button { background: #ff9800; color: #121212; border: none; padding: 10px 15px; font-weight: bold; width: 100%; margin-top: 15px; border-radius: 4px; cursor: pointer; }";
  html += "button:hover { background: #e68900; }";
  html += ".tooltip { display: inline-block; background: #444; color: #ff9800; border-radius: 50%; width: 18px; height: 18px; text-align: center; font-size: 12px; line-height: 18px; cursor: help; margin-left: 5px; }";
  html += "#chartWrap { position: relative; height: 220px; width: 100%; }";
  html += "</style></head><body>";
  html += "<div class='container'>";
  html += "<h1>Chain Waxer Control</h1>";

  html += "<div class='card'>";
  html += "<h3>Live Status</h3>";
  html += "<p>Temperature: <span id='lblTemp'>--</span> &deg;C</p>";
  html += "<p>Heater State: <span id='lblHeater'>--</span></p>";
  html += "<p>Mode: <span id='lblMode'>--</span></p>";
  html += "</div>";

  html += "<div class='card'>";
  html += "<div id='chartWrap'><canvas id='tempChart'></canvas></div>";
  html += "</div>";

  html += "<div class='card'>";
  html += "<form action='/update' method='POST'>";
  html += "<label>Target Temp (&deg;C) <span class='tooltip' title='Target holding temperature for the wax.'>i</span></label>";
  html += "<input type='number' step='0.1' name='target' value='" + String(targetTemp, 1) + "'>";

  html += "<label>Bulk Coast Margin (&deg;C) <span class='tooltip' title='Degrees below target to cut power during initial heat-up to catch thermal momentum.'>i</span></label>";
  html += "<input type='number' step='0.1' name='bulkcoast' value='" + String(bulkCoastMargin, 1) + "'>";

  html += "<label>Hold Reheat Trigger (&deg;C) <span class='tooltip' title='Drops below target to turn heater back on during holding phase.'>i</span></label>";
  html += "<input type='number' step='0.1' name='holdtrig' value='" + String(holdReheatTrigger, 1) + "'>";

  html += "<label>Hold Coast Margin (&deg;C) <span class='tooltip' title='Cuts power early during holding phase to coast back to target.'>i</span></label>";
  html += "<input type='number' step='0.1' name='holdcoast' value='" + String(holdCoastMargin, 1) + "'>";

  html += "<button type='submit'>Save Settings</button>";
  html += "</form>";
  html += "</div>";

  html += "</div>";
  html += "<script>";
  html += "const ctx = document.getElementById('tempChart').getContext('2d');";

  // Kill point rendering globally (works regardless of per-dataset overrides)
  html += "Chart.defaults.elements.point.radius = 0;";
  html += "Chart.defaults.elements.point.hitRadius = 0;";
  html += "Chart.defaults.elements.point.hoverRadius = 0;";
  html += "Chart.defaults.animation = false;";
  html += "Chart.defaults.font.family = 'sans-serif';";

  // Latest phase array from /data — drives segment colouring
  html += "let lastPhases = [];";

  html += "const chart = new Chart(ctx, {";
  html += "  type: 'line',";
  html += "  data: {";
  html += "    datasets: [";
  // Dataset 0: temperature trace, coloured per-segment by phase
  html += "      {";
  html += "        label: 'Temperature',";
  html += "        data: [],";
  html += "        borderColor: '#4fc3f7',";
  html += "        backgroundColor: '#4fc3f7',";
  html += "        borderWidth: 1,";
  html += "        pointRadius: 0,";
  html += "        pointHoverRadius: 0,";
  html += "        pointHitRadius: 0,";
  html += "        pointStyle: false,";
  html += "        tension: 0.1,";
  html += "        fill: false,";
  html += "        spanGaps: true,";
  html += "        parsing: false,";
  html += "        normalized: true,";
  html += "        segment: {";
  html += "          borderColor: function(ctx) {";
  html += "            var p = lastPhases[ctx.p1DataIndex];";
  html += "            if (p === 2) return '#ef5350';";   // red  (overshoot / cooling)
  html += "            if (p === 1) return '#66bb6a';";   // green (holding)
  html += "            return '#4fc3f7';";                 // blue (heating / coasting up)
  html += "          }";
  html += "        }";
  html += "      },";
  // Dataset 1: horizontal dashed target line
  html += "      {";
  html += "        label: 'Target',";
  html += "        data: [],";
  html += "        borderColor: '#ff9800',";
  html += "        backgroundColor: '#ff9800',";
  html += "        borderWidth: 2,";
  html += "        borderDash: [6, 6],";
  html += "        borderCapStyle: 'butt',";
  html += "        pointRadius: 0,";
  html += "        pointHoverRadius: 0,";
  html += "        pointHitRadius: 0,";
  html += "        pointStyle: false,";
  html += "        tension: 0,";
  html += "        fill: false,";
  html += "        spanGaps: true,";
  html += "        parsing: false,";
  html += "        normalized: true";
  html += "      }";
  html += "    ]";
  html += "  },";
  html += "  options: {";
  html += "    responsive: true,";
  html += "    maintainAspectRatio: false,";
  html += "    animation: false,";
  html += "    interaction: { mode: 'index', intersect: false },";
  html += "    plugins: {";
  html += "      legend: {";
  html += "        display: true,";
  html += "        labels: {";
  html += "          color: '#aaa',";
  html += "          boxWidth: 12,";
  html += "          boxHeight: 12,";
  html += "          usePointStyle: true,";
  html += "          generateLabels: function(chart) {";
  html += "            return chart.data.datasets.map(function(ds, i) {";
  html += "              return {";
  html += "                text: ds.label,";
  html += "                fillStyle: ds.borderColor,";
  html += "                strokeStyle: ds.borderColor,";
  html += "                fontColor: '#aaa',";
  html += "                lineWidth: 0,";
  html += "                pointStyle: 'rect',";
  html += "                hidden: !chart.isDatasetVisible(i),";
  html += "                datasetIndex: i";
  html += "              };";
  html += "            });";
  html += "          }";
  html += "        }";
  html += "      }";
  html += "    },";
  html += "    scales: {";
  html += "      x: { type: 'linear', display: false, grid: { display: false } },";
  html += "      y: {";
  html += "        suggestedMin: 20,";
  html += "        suggestedMax: 100,";
  html += "        ticks: { color: '#aaa' },";
  html += "        grid: { color: '#333' }";
  html += "      }";
  html += "    }";
  html += "  }";
  html += "});";

  html += "async function fetchData() {";
  html += "  try {";
  html += "    const res = await fetch('/data');";
  html += "    const data = await res.json();";
  html += "    document.getElementById('lblTemp').innerText = data.temp.toFixed(1);";
  html += "    document.getElementById('lblHeater').innerText = data.heater ? 'ON' : 'OFF';";
  html += "    let mode = 'Holding';";
  html += "    if (data.coasting) mode = 'Coasting';";
  html += "    else if (data.heater) mode = 'Heating';";
  html += "    document.getElementById('lblMode').innerText = mode;";

  // Build {x,y} pairs from history; parsing:false trusts the format directly.
  html += "    const tempPoints = data.history.map((val, i) => ({ x: i, y: val }));";
  html += "    const targetPoints = data.history.map((_, i) => ({ x: i, y: data.target }));";

  // Refresh the phase array used by the segment colour callback
  html += "    lastPhases = data.phases;";

  html += "    chart.data.datasets[0].data = tempPoints;";
  html += "    chart.data.datasets[1].data = targetPoints;";
  html += "    chart.update('none');";
  html += "  } catch (e) { console.error(e); }";
  html += "}";
  html += "setInterval(fetchData, 5000);";
  html += "fetchData();";
  html += "</script>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

#endif