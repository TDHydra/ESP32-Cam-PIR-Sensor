/**
 * ESP32-Cam PIR Sensor
 * ====================
 * Hardware : AI Thinker ESP32-Cam (ESP32-S module + OV2640 / OV3660 / OV5640 camera)
 * Sensor   : HC-SR501 Passive Infrared (PIR) motion sensor
 *
 * What it does
 * ------------
 * 1. Enters deep sleep immediately after setup (or after the WiFi viewing window).
 * 2. Wakes up the instant the HC-SR501 pulls its OUT pin HIGH (motion detected).
 * 3. Auto-detects whichever camera module is fitted (OV2640, OV2660, OV3660, OV5640).
 * 4. Captures a JPEG photo and saves it to SPIFFS (internal flash, no SD card required).
 * 5. Starts a WiFi Access Point + tiny web server so you can browse to the device IP
 *    and view / download every saved image—without unplugging the USB cable.
 * 6. After WIFI_ACTIVE_DURATION_MS the AP shuts down and the board goes back to sleep.
 *
 * All key events are printed on the serial monitor (115200 baud) with a clear tag so
 * you can follow exactly what is happening at each stage.
 *
 * Wiring (HC-SR501 → AI Thinker ESP32-Cam)
 * -----------------------------------------
 *   PIR VCC  →  5 V  (or 3.3 V — check your module; HC-SR501 prefers 5 V)
 *   PIR GND  →  GND
 *   PIR OUT  →  GPIO 13
 *
 * Required Arduino libraries / board support
 * -------------------------------------------
 *   • "ESP32" board package by Espressif (tested ≥ 2.x)
 *     Install via: Arduino IDE → Boards Manager → search "esp32"
 *   • esp_camera  — bundled with the ESP32 board package
 *   • SPIFFS       — bundled with the ESP32 board package
 *   • WiFi         — bundled with the ESP32 board package
 *   • WebServer    — bundled with the ESP32 board package
 *
 * Board / partition settings in Arduino IDE
 * ------------------------------------------
 *   Board         : AI Thinker ESP32-CAM
 *   Partition     : Huge APP (3MB No OTA / 1MB SPIFFS)  ← gives 1 MB for images
 *   Upload speed  : 115200 (or 921600 for faster uploads)
 *   Flash freq    : 80MHz
 */

#include "esp_camera.h"
#include "esp_sleep.h"
#include "SPIFFS.h"
#include <WiFi.h>
#include <WebServer.h>

// ============================================================
//  User-configurable settings — edit these to suit your setup
// ============================================================

// GPIO connected to the HC-SR501 OUT pin.
// GPIO 13 is an RTC-capable GPIO on the AI Thinker board and is free
// when no SD card is fitted.
#define PIR_PIN  GPIO_NUM_13

// Onboard white flash LED (GPIO 4 on AI Thinker).
#define FLASH_LED_PIN  4

// WiFi Access Point credentials (change as you like).
#define AP_SSID      "ESP32-Cam-PIR"
#define AP_PASSWORD  "12345678"

// TCP port for the embedded web server.
#define WEB_SERVER_PORT  80

// How many milliseconds to keep the WiFi AP alive after each wakeup.
// Increase this if you need more time to browse / download images.
#define WIFI_ACTIVE_DURATION_MS  30000UL  // 30 seconds

// Rolling image store: at most this many JPEG files are kept in SPIFFS.
// Oldest file is overwritten once the limit is reached.
#define MAX_IMAGES  5

// How long (ms) the flash LED stays on during each capture.
// Increase in very dark environments; decrease if over-exposure occurs.
#define FLASH_DURATION_MS  150

// How long (ms) to wait for the PIR OUT pin to return LOW before entering
// deep sleep.  Extend if your HC-SR501 hold-time pot is turned up high.
#define PIR_SETTLE_TIMEOUT_MS  5000

// Brief pause after Serial.begin() to let the host-side terminal connect.
#define SERIAL_INIT_DELAY_MS  300

// ============================================================
//  AI Thinker ESP32-Cam camera pin map
//  Do NOT change these unless you are using a different board.
// ============================================================
#define PWDN_GPIO_NUM   32
#define RESET_GPIO_NUM  -1
#define XCLK_GPIO_NUM    0
#define SIOD_GPIO_NUM   26
#define SIOC_GPIO_NUM   27
#define Y9_GPIO_NUM     35
#define Y8_GPIO_NUM     34
#define Y7_GPIO_NUM     39
#define Y6_GPIO_NUM     36
#define Y5_GPIO_NUM     21
#define Y4_GPIO_NUM     19
#define Y3_GPIO_NUM     18
#define Y2_GPIO_NUM      5
#define VSYNC_GPIO_NUM  25
#define HREF_GPIO_NUM   23
#define PCLK_GPIO_NUM   22

// ============================================================
//  RTC memory — survives deep sleep
// ============================================================

// Rolling counter used to name files img_1.jpg … img_N.jpg.
RTC_DATA_ATTR int rtcImageIndex = 0;

// ============================================================
//  Module-level objects
// ============================================================
WebServer server(WEB_SERVER_PORT);
String    latestImagePath = "";

// ============================================================
//  Camera initialisation
// ============================================================

/**
 * Initialise the camera peripheral with settings appropriate for whichever
 * OmniVision sensor is fitted.  After init the sensor PID register is read
 * and the model name is printed to Serial.
 *
 * @return true on success, false if esp_camera_init() fails.
 */
bool initCamera() {
  camera_config_t cfg;
  cfg.ledc_channel = LEDC_CHANNEL_0;
  cfg.ledc_timer   = LEDC_TIMER_0;
  cfg.pin_d0       = Y2_GPIO_NUM;
  cfg.pin_d1       = Y3_GPIO_NUM;
  cfg.pin_d2       = Y4_GPIO_NUM;
  cfg.pin_d3       = Y5_GPIO_NUM;
  cfg.pin_d4       = Y6_GPIO_NUM;
  cfg.pin_d5       = Y7_GPIO_NUM;
  cfg.pin_d6       = Y8_GPIO_NUM;
  cfg.pin_d7       = Y9_GPIO_NUM;
  cfg.pin_xclk     = XCLK_GPIO_NUM;
  cfg.pin_pclk     = PCLK_GPIO_NUM;
  cfg.pin_vsync    = VSYNC_GPIO_NUM;
  cfg.pin_href     = HREF_GPIO_NUM;
  cfg.pin_sscb_sda = SIOD_GPIO_NUM;
  cfg.pin_sscb_scl = SIOC_GPIO_NUM;
  cfg.pin_pwdn     = PWDN_GPIO_NUM;
  cfg.pin_reset    = RESET_GPIO_NUM;
  cfg.xclk_freq_hz = 20000000;
  cfg.pixel_format = PIXFORMAT_JPEG;

  // Use higher resolution / double buffering when PSRAM is available.
  if (psramFound()) {
    cfg.frame_size   = FRAMESIZE_UXGA;   // 1600×1200
    cfg.jpeg_quality = 10;               // 0 (best) – 63 (worst)
    cfg.fb_count     = 2;
    Serial.println("[Camera] PSRAM detected → UXGA (1600×1200), quality 10");
  } else {
    cfg.frame_size   = FRAMESIZE_SVGA;   // 800×600
    cfg.jpeg_quality = 12;
    cfg.fb_count     = 1;
    Serial.println("[Camera] No PSRAM → SVGA (800×600), quality 12");
  }

  esp_err_t err = esp_camera_init(&cfg);
  if (err != ESP_OK) {
    Serial.printf("[Camera] INIT FAILED  error=0x%x\n", err);
    return false;
  }

  // Identify the sensor and apply any model-specific tweaks.
  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    const uint16_t pid = s->id.PID;
    Serial.printf("[Camera] Sensor PID: 0x%04X  →  ", pid);

    switch (pid) {
      case OV2640_PID:   // 0x2641
        Serial.println("OV2640");
        s->set_vflip(s, 1);    // lens mounted upside-down on AI Thinker
        s->set_hmirror(s, 0);
        break;

      case 0x2660:               // OV2660 — not in all SDK versions
        Serial.println("OV2660");
        break;

      case OV3660_PID:   // 0x3660
        Serial.println("OV3660");
        s->set_vflip(s, 1);
        s->set_brightness(s, 1);
        s->set_saturation(s, -2);
        break;

      case OV5640_PID:   // 0x5640
        Serial.println("OV5640");
        s->set_vflip(s, 1);
        break;

      default:
        Serial.printf("Unknown (PID 0x%04X)\n", pid);
        break;
    }
  }

  Serial.println("[Camera] INIT OK");
  return true;
}

// ============================================================
//  SPIFFS initialisation
// ============================================================

/**
 * Mount SPIFFS and print available / used space.
 * @return true on success.
 */
bool initSPIFFS() {
  if (!SPIFFS.begin(true)) {   // true = format if mount fails
    Serial.println("[SPIFFS] Mount FAILED");
    return false;
  }
  Serial.printf("[SPIFFS] OK  total=%u B  used=%u B\n",
                SPIFFS.totalBytes(), SPIFFS.usedBytes());
  return true;
}

// ============================================================
//  Image capture
// ============================================================

/**
 * Briefly lights the flash LED, grabs a frame from the camera, and writes
 * the JPEG to SPIFFS using a rolling filename (img_1.jpg … img_N.jpg).
 *
 * @return The SPIFFS path of the saved file, or "" on failure.
 */
String captureAndSave() {
  // Momentary flash so the capture is well-lit in low light.
  digitalWrite(FLASH_LED_PIN, HIGH);
  delay(FLASH_DURATION_MS);
  camera_fb_t *fb = esp_camera_fb_get();
  digitalWrite(FLASH_LED_PIN, LOW);

  if (!fb) {
    Serial.println("[Capture] FAILED — could not get frame buffer");
    return "";
  }
  Serial.printf("[Capture] Frame: %u bytes  format=%d\n", fb->len, fb->format);

  // Advance rolling index (1 … MAX_IMAGES).
  rtcImageIndex = (rtcImageIndex % MAX_IMAGES) + 1;
  String path = "/img_" + String(rtcImageIndex) + ".jpg";

  File f = SPIFFS.open(path, FILE_WRITE);
  if (!f) {
    Serial.printf("[Capture] FAILED — could not open %s for writing\n", path.c_str());
    esp_camera_fb_return(fb);
    return "";
  }

  size_t written = f.write(fb->buf, fb->len);
  f.close();
  esp_camera_fb_return(fb);

  if (written == 0) {
    Serial.printf("[Capture] FAILED — wrote 0 bytes to %s\n", path.c_str());
    return "";
  }

  Serial.printf("[Capture] SUCCESS — saved %s (%u bytes)\n", path.c_str(), written);
  return path;
}

// ============================================================
//  Web server — HTML helpers
// ============================================================

/** Shared page header with inline CSS (dark theme). */
static String pageHeader(const String &title) {
  String h;
  h += "<!DOCTYPE html><html lang='en'><head>";
  h += "<meta charset='UTF-8'>";
  h += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  h += "<title>" + title + "</title>";
  h += "<style>";
  h += "body{font-family:Arial,sans-serif;background:#1a1a2e;color:#eee;margin:0;padding:20px}";
  h += "h1,h2{color:#e94560}";
  h += "a.btn{display:inline-block;padding:9px 18px;background:#e94560;color:#fff;";
  h += "text-decoration:none;border-radius:5px;margin:4px}";
  h += "a.btn:hover{background:#c73652}";
  h += ".card{background:#16213e;padding:15px;border-radius:8px;margin:10px 0}";
  h += "img.thumb{max-width:100%;border:2px solid #e94560;border-radius:6px;display:block;margin:8px 0}";
  h += ".grid{display:flex;flex-wrap:wrap;gap:12px}";
  h += ".grid-item{flex:1 1 220px;background:#16213e;padding:10px;border-radius:8px}";
  h += "</style></head><body>";
  return h;
}

// ============================================================
//  Web server — route handlers
// ============================================================

/** GET /  — index page showing the latest capture and navigation links. */
void handleRoot() {
  String page = pageHeader("ESP32-Cam PIR");
  page += "<h1>ESP32-Cam PIR Sensor</h1>";
  page += "<div class='card'>";
  page += "<p><b>Latest image:</b> " + (latestImagePath.isEmpty() ? "none yet" : latestImagePath) + "</p>";
  page += "</div>";

  if (!latestImagePath.isEmpty()) {
    page += "<img class='thumb' src='/image' alt='Latest capture'><br>";
    page += "<a class='btn' href='/image' download='capture.jpg'>⬇ Download</a> ";
  } else {
    page += "<p>No image captured yet.  Trigger the PIR sensor or use the button below.</p>";
  }

  page += "<a class='btn' href='/gallery'>🖼 Gallery</a> ";
  page += "<a class='btn' href='/capture'>📷 Capture now</a>";
  page += "</body></html>";
  server.send(200, "text/html", page);
}

/** GET /image  — serves the latest captured JPEG. */
void handleImage() {
  if (latestImagePath.isEmpty()) {
    server.send(404, "text/plain", "No image available");
    return;
  }
  File f = SPIFFS.open(latestImagePath, FILE_READ);
  if (!f) {
    server.send(500, "text/plain", "Failed to open image file");
    return;
  }
  server.streamFile(f, "image/jpeg");
  f.close();
}

/** GET /gallery  — shows thumbnails of all stored images. */
void handleGallery() {
  String page = pageHeader("Gallery – ESP32-Cam PIR");
  page += "<h1>Image Gallery</h1>";
  page += "<a class='btn' href='/'>← Home</a><br><br>";
  page += "<div class='grid'>";

  File root = SPIFFS.open("/");
  File entry = root.openNextFile();
  bool found = false;

  while (entry) {
    String name = entry.name();
    // Only show JPEG files; skip the leading slash added on some SDK versions.
    if (!name.startsWith("/")) {
      name = "/" + name;
    }
    if (name.endsWith(".jpg")) {
      found = true;
      page += "<div class='grid-item'>";
      page += "<a href='/file?name=" + name + "'>";
      page += "<img class='thumb' src='/file?name=" + name + "' alt='" + name + "'>";
      page += "</a>";
      page += "<p>" + name + "<br><small>" + String(entry.size()) + " bytes</small></p>";
      page += "</div>";
    }
    entry = root.openNextFile();
  }

  if (!found) {
    page += "<p>No images stored yet.</p>";
  }

  page += "</div></body></html>";
  server.send(200, "text/html", page);
}

/** GET /file?name=/img_N.jpg  — serves a specific stored JPEG. */
void handleFile() {
  if (!server.hasArg("name")) {
    server.send(400, "text/plain", "Missing 'name' query parameter");
    return;
  }

  String name = server.arg("name");

  // Basic path sanitisation: must start with '/', end with '.jpg', no traversal.
  if (!name.startsWith("/") || !name.endsWith(".jpg") || name.indexOf("..") != -1) {
    server.send(400, "text/plain", "Invalid filename");
    return;
  }

  File f = SPIFFS.open(name, FILE_READ);
  if (!f) {
    server.send(404, "text/plain", "File not found: " + name);
    return;
  }
  server.streamFile(f, "image/jpeg");
  f.close();
}

/** GET /capture  — triggers an immediate photo and redirects to root. */
void handleCapture() {
  String path = captureAndSave();
  if (!path.isEmpty()) {
    latestImagePath = path;
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "Captured: " + path);
  } else {
    server.send(500, "text/plain", "Capture failed — check serial monitor");
  }
}

/** Catch-all 404. */
void handleNotFound() {
  server.send(404, "text/plain", "Not found: " + server.uri());
}

// ============================================================
//  WiFi Access Point
// ============================================================

/**
 * Starts the SoftAP and registers all HTTP routes.
 * After calling this, call server.handleClient() in loop().
 */
void startWiFiAP() {
  Serial.printf("[WiFi] Starting AP  SSID='%s'  password='%s'\n", AP_SSID, AP_PASSWORD);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("[WiFi] AP IP: %s\n", ip.toString().c_str());

  server.on("/",        handleRoot);
  server.on("/image",   handleImage);
  server.on("/gallery", handleGallery);
  server.on("/file",    handleFile);
  server.on("/capture", handleCapture);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.printf("[WiFi] Web server up → http://%s\n", ip.toString().c_str());
  Serial.printf("[WiFi] Connect your phone/laptop to WiFi '%s' (password: '%s')\n",
                AP_SSID, AP_PASSWORD);
  Serial.printf("[WiFi] AP will stay active for %lu s then board returns to deep sleep\n",
                WIFI_ACTIVE_DURATION_MS / 1000UL);
}

// ============================================================
//  Deep sleep
// ============================================================

/**
 * Cleanly shuts down the camera and WiFi, then puts the ESP32 into deep
 * sleep.  It will wake again as soon as GPIO PIR_PIN goes HIGH.
 */
void goToDeepSleep() {
  Serial.printf("[Sleep] Configuring EXT0 wakeup on GPIO %d (HIGH = motion)\n",
                static_cast<int>(PIR_PIN));

  // Give the PIR sensor time to return LOW so we do not wake immediately.
  const uint32_t pirWaitMs = PIR_SETTLE_TIMEOUT_MS;
  Serial.printf("[Sleep] Waiting up to %u ms for PIR to go LOW...\n", pirWaitMs);
  uint32_t t0 = millis();
  while (digitalRead(PIR_PIN) == HIGH && (millis() - t0) < pirWaitMs) {
    delay(100);
  }
  if (digitalRead(PIR_PIN) == HIGH) {
    Serial.println("[Sleep] PIR still HIGH — sleeping anyway (will wake immediately if still triggered)");
  } else {
    Serial.println("[Sleep] PIR is LOW — safe to sleep");
  }

  // Release the camera before powering down.
  esp_camera_deinit();

  // Stop WiFi.
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);

  esp_sleep_enable_ext0_wakeup(PIR_PIN, HIGH);

  Serial.println("[Sleep] Entering deep sleep now.  ZZZ...");
  Serial.flush();
  esp_deep_sleep_start();
  // Execution does not reach here.
}

// ============================================================
//  Arduino setup()
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(SERIAL_INIT_DELAY_MS);

  Serial.println();
  Serial.println("============================================================");
  Serial.println("  ESP32-Cam PIR Sensor  —  starting up");
  Serial.println("============================================================");

  // Print the reason this boot/wakeup occurred.
  switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_EXT0:
      Serial.println("[Boot] Wakeup reason: EXT0 — PIR motion detected!");
      break;
    case ESP_SLEEP_WAKEUP_UNDEFINED:
      Serial.println("[Boot] Wakeup reason: Power-on or hard reset");
      break;
    default:
      Serial.printf("[Boot] Wakeup reason: %d\n",
                    static_cast<int>(esp_sleep_get_wakeup_cause()));
      break;
  }

  // Configure I/O.
  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);
  pinMode(PIR_PIN, INPUT);

  // Mount internal flash filesystem.
  if (!initSPIFFS()) {
    Serial.println("[Setup] SPIFFS unavailable — images cannot be saved");
  }

  // Initialise the camera.
  if (!initCamera()) {
    Serial.println("[Setup] Camera init FAILED — sleeping and retrying on next PIR trigger");
    delay(2000);
    // Skip to sleep; camera will be re-initialised on next wakeup.
    esp_sleep_enable_ext0_wakeup(PIR_PIN, HIGH);
    Serial.flush();
    esp_deep_sleep_start();
    return;
  }

  // Take the photo.
  Serial.println("[Setup] Capturing image...");
  String path = captureAndSave();
  if (!path.isEmpty()) {
    latestImagePath = path;
    Serial.println("[Setup] Capture: SUCCESS ✓");
  } else {
    Serial.println("[Setup] Capture: FAILED ✗");
  }

  // Start the WiFi viewing window.
  startWiFiAP();
}

// ============================================================
//  Arduino loop()
// ============================================================

static unsigned long wifiStartMs = 0;
static bool          timerStarted = false;

void loop() {
  // Start the viewing-window timer on the first loop iteration.
  if (!timerStarted) {
    wifiStartMs  = millis();
    timerStarted = true;
  }

  server.handleClient();

  if (millis() - wifiStartMs >= WIFI_ACTIVE_DURATION_MS) {
    Serial.println("[Loop] WiFi viewing window expired — going to deep sleep");
    goToDeepSleep();
  }
}
