// Camera configuration
#define CAMERA_MODEL_AI_THINKER

#include "mbedtls/base64.h"
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ESPmDNS.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include "esp_camera.h"
#include "camera_pins.h"
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#define BIG_SEND

#define LED_GPIO 4  // ESP32-CAM built-in flash LED
int Y_GRAP = 14;
int Y_SCAN = 7;
int Y_BEGIN = 0;

// Replace with your network credentials
const char* ssid = "wanhome";
const char* password = "all4Christ";

// Processing server URL (Change this to your server URL)
String data_server = "http://lsf.little-shepherd.org";
String controlProcess = "/nameTagControl.php";
String qrProcess = "/process_qrImage.php";
String grblProcess = "/GrblConfig.php";

// Create a web server on port 80
WebServer server(80);
WebSocketsServer webSocket(81);  // WebSocket server on port 81
WiFiClient grblClient;
WiFiServer telnetServer(23);
WiFiClient telnetClient;

int cropWidth = 480;
int cropHeight = 300;

int control_id;
int grbl_id = -1;
bool grblStatus = false;
String grblIp;
const uint16_t grbl_port = 23;
String grblHttp;
String buttonInfo;
bool needButtonInfo = false;
float xMpos, yMpos, zMpos;

class TelnetStream : public Stream {
  public:
    void begin(unsigned long baud = 115200) {}
    size_t write(uint8_t c) override {
      if (telnetClient && telnetClient.connected()) {
        return telnetClient.write(c);
      }
      return Serial.write(c);
    }

    size_t write(const uint8_t *buffer, size_t size) override {
      if (telnetClient && telnetClient.connected()) {
        return telnetClient.write(buffer, size);
      }
      return Serial.write(buffer, size);
    }

    int available() override {
      if (telnetClient && telnetClient.connected()) {
        return telnetClient.available();
      }
      return Serial.available();
    }

    int read() override {
      if (telnetClient && telnetClient.connected()) {
        return telnetClient.read();
      }
      return Serial.read();
    }

    int peek() override {
      if (telnetClient && telnetClient.connected()) {
        return telnetClient.peek();
      }
      return Serial.peek();
    }

    void flush() override {
      if (telnetClient && telnetClient.connected()) {
        telnetClient.flush();
      } else {
        Serial.flush();
      }
    }
};

// Create global instance
TelnetStream TelnetSerial;

// Optionally redirect Serial to TelnetSerial
#define Serial TelnetSerial

void startCamera() {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 10;
    config.fb_count = 1;

    // Initialize the camera
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x", err);
        return;
    }
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_brightness(s, 1);  // Increase brightness (+2 max)
        s->set_contrast(s, 2);    // Higher contrast (0 to 2)
        s->set_saturation(s, 1);  // Improve color saturation
        s->set_sharpness(s, 1);   // Sharpen edges
        s->set_gainceiling(s, (gainceiling_t)6); // Boost gain for better light
        s->set_special_effect(s, 0); // No special effect
        s->set_whitebal(s, 1);  // Enable white balance
        s->set_awb_gain(s, 1);  // Auto white balance gain
        s->set_hmirror(s, 0);  // No mirror effect
        s->set_vflip(s, 0);    // No vertical flip
        s->set_aec2(s, 1);     // Auto exposure control
        s->set_dcw(s, 1);      // De-warp correction (recommended)
    } else
        Serial.println("failed to get the sensor");
}
void handleRoot() {
    StaticJsonDocument<200> doc; // Adjust size based on your JSON complexity
    String buttonValues;
    // buttons set to default
    if (buttonInfo == "") {
      // make default obj
      doc["capture"] = true;
      doc["light"] = false;
      doc["qrcode"] = false;
      doc["map"] = false;
      doc["pos"] = false;
      buttonValues = "\n\
        let captureOn = true;\n\
        let lightOn = false;\n\
        let qrcodeOn = false;\n\
        let isMapping = false;\n\
        let posOn = false;";
    } else {
        DeserializationError error = deserializeJson(doc, buttonInfo);
        if (error) {
            Serial.print("deserializeJson() failed: ");
            Serial.println(error.c_str());
            return;
        }
        buttonValues = "\
          let captureOn = " + doc["capture"].as<String>() + ";\n\
          let lightOn = " + doc["light"].as<String>() + ";\n\
          let qrcodeOn = " + doc["qrcode"].as<String>() + ";\n\
          let isMapping = " + doc["map"].as<String>() + ";\n\
          let posOn = " + doc["pos"].as<String>() + ";";
    }
    String html = R"rawliteral(
<!DOCTYPE html>
<html lang='en'>
<head>
    <meta charset='UTF-8'>
    <meta name='viewport' content='width=device-width, initial-scale=1.0'>
    <title>ESP32 Nametag Storage Control</title>
    <style>
        body { text-align: center; font-family: Arial, sans-serif; }
        #cameraFeed { display: block; margin: auto; }
    .status-indicator {
        width: 15px;
        height: 15px;
        border-radius: 50%;
        display: inline-block;
        background-color: gray; /* default/offline */
    }
    .online {
        background-color: green;
    }
    .offline {
      background-color: red;
    }
    </style>
    <script>
        let curPos = -1;
)rawliteral" + buttonValues + R"rawliteral(
        function refreshImage() {
            if (captureOn) {
                document.getElementById('cameraFeed').src = '/capture?' + new Date().getTime();
            }
        }
        setInterval(refreshImage, 5000);  // Refresh image every 5 second
        function updateStatus() {
          fetch('/grblStatus')
            .then(response => response.json())
            .then(data => {
              const statusCircle = document.getElementById('statusCircle');
              statusCircle.className = 'status-indicator ' + (data.online ? 'online' : 'offline');
            })
            .catch(error => {
              console.error('Status check failed:', error);
              document.getElementById('statusCircle').className = 'status-indicator offline';
            });
        }
        // Call once immediately
        updateStatus();

        // Then repeat every 10 seconds
        setInterval(updateStatus, 10000);

        function toggleCapture() {
            captureOn = !captureOn;
            updButton();
        }

        function toggleMap() {
          const msgDisplay = document.getElementById('MsgResult');

          if (!isMapping) {
            console.log("toggleMap(): isMapping is false, going to start");
            if (captureOn)
              toggleCapture();
            isMapping = true;
            msgDisplay.innerText = "Starting Setup Map...";

            fetch('/setNametagMap')
              .then(response => response.text())
              .then(text => {
                msgDisplay.innerText = text;
              });
          } else {
            console.log("toggleMap(): isMapping is true, going to stop");
            isMapping = false;
            // Stop mapping
            fetch('/stopMap')
              .then(response => response.text())
              .then(text => {
                msgDisplay.innerText = text;
              })
              .catch(error => {
                document.getElementById('MsgResult').innerText = 'Error: ' + error;
                // Clear the error message after 5 seconds
                setTimeout(() => {
                  location.reload();
                }, 5000);
              });
          }
          updButton();
        }

        function toggleLight() {
            lightOn = !lightOn;
            let url = lightOn ? '/lighton' : '/lightoff';
            fetch(url)
              .catch(error => {
                  document.getElementById('MsgResult').innerText = 'Error: ' + error;
              });
            updButton();
        }

        function processQR() {
            qrcodeOn = true;
            updButton();
            document.getElementById('MsgResult').innerText = 'Processing...';
            fetch('/processQR')
              .then(response => response.text())
              .then(qrCode => {
                  document.getElementById('MsgResult').innerText = 'UID: ' + qrCode;
              })
              .catch(error => {
                  document.getElementById('MsgResult').innerText = 'Error: ' + error;
              })
              .finally(() => {
                  qrcodeOn = false;
                  updButton();
              });
        }
        function homePos() {
            const btn = document.getElementById('homeBtn');
            btn.disabled = true;
            document.getElementById('MsgResult').innerText = 'Going Home...';
            fetch('/homePos')
              .then(response => response.text())
              .then(text => {
                document.getElementById('MsgResult').innerText = text;
              })
              .catch(error => {
                document.getElementById('MsgResult').innerText = 'Error: ' + error;
              })
              .finally(() => {
                  btn.disabled = false;
              });
            curPos = -1;
        }
        function initGrbl() {
            document.getElementById('MsgResult').innerText = 'Initial GRBL controllor...';
            fetch('/grblInit')
              .then(response => response.text())
              .then(text => {
                document.getElementById('MsgResult').innerText = text;
              })
              .catch(error => {
                document.getElementById('MsgResult').innerText = 'Error: ' + error;
              });
        }
        function position(delta) {
            posOn = true;
            updButton();
            curPos += delta;
            if (curPos < 0)
              curPos = 0;
            else if (curPos > 159)
              curPos = 159;
            document.getElementById('MsgResult').innerText = 'Setup NameTag Position ' + curPos;
            fetch('/position?pos=' + curPos + '&dir=' + delta)
              .then(response => response.text())
              .then(text => {
                document.getElementById('MsgResult').innerText = text;
              })
              .catch(error => {
                document.getElementById('MsgResult').innerText = 'Error: ' + error;
              })
              .finally(() => {
                posOn = false;
                updButton();
              });
        }
        function updButton() {
            const data = {
              capture: captureOn,
              light: lightOn,
              qrcode: qrcodeOn,
              map: isMapping,
              pos: posOn,
            };

            fetch('/updButton', {
              method: 'POST',
              headers: {
                'Content-Type': 'application/json'
              },
              body: JSON.stringify(data)
            })
              .catch(err => console.error("updButton Request failed:", err));
        }
        function register() {
            console.log("register");
            let url = '/register';
            fetch(url)
              .catch(error => {
                  document.getElementById('MsgResult').innerText = 'Error: ' + error;
              })
              .finally(() => {
                  location.reload();
              });
        }
        function initButtons() {
            console.log("initButtons: capture=" + 
              captureOn + ", light=" +
              lightOn + ", qrCode=" +
              qrcodeOn + ", map=" +
              isMapping + ", pos=" +
              posOn
            );        
            document.getElementById('captureBtn').innerText = captureOn ? 'Stop Capture' : 'Start Capture';
            document.getElementById('lightBtn').innerText = lightOn ? 'Turn Light Off' : 'Turn Light On';
            document.getElementById('qrBtn').disabled = qrcodeOn == 1;
            document.getElementById('mapBtn').innerText = isMapping ? "Stop Mapping" : "Setup Map";
            document.getElementById('fwPos').disabled = posOn == 1;
            document.getElementById('bwPos').disabled = posOn == 1;
        }

        let base64Image = "";
        let receiving = false;
        const ws = new WebSocket(`ws://${window.location.hostname}:81/`);
        ws.onmessage = (event) => {
            const data = event.data;
            if (data === "IMG_START") {
              base64Image = "";
              receiving = true;
            } else if (data === "IMG_END") {
              receiving = false;
              console.log("Image size=" + base64Image.length);        
              document.getElementById("cameraFeed").src = "data:image/jpeg;base64," + base64Image;
            } else if (receiving) {
              base64Image += data;
            } else {
              console.log("WebSocket received: " + data);        
              // try parse as json string
              try {
                  const buttons = JSON.parse(data);
                  for (let key in buttons) {
                      if (buttons.hasOwnProperty(key)) {
                          switch(key) {
                            case "capture":
                              captureOn = buttons[key];
                              break;
                            case "light":
                              lightOn = buttons[key];
                              break;
                            case "qrcode":
                              qrcodeOn = buttons[key];
                              break;
                            case "map":
                              isMapping = buttons[key];
                              break;
                            case "pos":
                              posOn = buttons[key];
                              break;
                            default:
                              console.log(`Unhandled button: ${key}, Value: ${buttons[key]}`);
                          }
                      }
                  }
                  initButtons();
              } catch (e) {
                  document.getElementById("MsgResult").innerText = data;
              }
            }
        };
        ws.onerror = (error) => {
            console.error("WebSocket Error:", error);
        };
        ws.onclose = () => {
            console.log("WebSocket closed");
        };
    </script>
</head>

<body>
    <h1>Nametag Storage Controllor</h1>
    <h2>Grbl IP: )rawliteral" + grblIp + R"rawliteral( - Status: <span id="statusCircle" class="status-indicator"></span></h2>
    <br><br>
    <img id='cameraFeed' src='/capture' width='480' height='300' alt='Camera Feed'>
    <br><br>
    <button id='captureBtn' onclick='toggleCapture()'>Stop Capture</button>
    <button id='lightBtn' onclick='toggleLight()'>Turn Light On</button>
    <button id="qrBtn" onclick="processQR()">Get QR Code</button>
    <button id="mapBtn" onclick="toggleMap()">Setup Map</button>
    <button id="homeBtn" onclick='homePos()'>Go Home</button>
    <button id="initBtn" onclick='initGrbl()'>Init Grbl</button>
    <button id="regBtn" onclick='register()'>Register</button>
    <button id="fwPos" onclick='position(1)'>NameTag Pos (fw)</button>
    <button id="bwPos" onclick='position(-1)'>NameTag Pos (bw)</button>
    <p id='MsgResult'></p>
</body>
    <script>
        initButtons();
    </script>
</html>
)rawliteral";

    server.send(200, "text/html", html);
    if (buttonInfo == "") {
      digitalWrite(LED_GPIO, LOW); // Turn LED OFF
      serializeJson(doc, buttonInfo);
      Serial.println("default buttons: "  + buttonInfo);
    } else {
      needButtonInfo = true;
      Serial.println("needButtonInfo set to true");
    }
}

bool sendRequest(String &url, String &response, int maxTry = 3) {
  if (WiFi.status() == WL_CONNECTED) {
    for (int i = 0; i < maxTry; i++) {
      HTTPClient http;
      http.begin(url);  // Specify URL
      // Force HTTP/1.1 (helps with cache issues)
      http.useHTTP10(false);  

      // Add Cache-Control headers
      http.addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
      http.addHeader("Pragma", "no-cache");
      http.addHeader("Expires", "-1");
      int httpCode = http.GET();  // Send GET request
      //Serial.printf("%d. %s, httpCode=%d\n", i, url.c_str(), httpCode);
      if (httpCode == HTTP_CODE_OK) {
        response = http.getString();  // Get the response to the request
        //Serial.print("Response from server:");
        //Serial.println(response);
        http.end();  // Free resources
        return true;
      }
      Serial.printf("sendRequest failed. Error: (%d) %s\n", httpCode, http.errorToString(httpCode).c_str());
      http.end();  // Free resources
      delay(500);
    }
    response = url + " failed";
  } else {
    Serial.println("sendRequest(): WiFi is not connected");
  }
  return false;
}

bool sendRequest(String &url, JsonDocument &doc, int maxTry = 3) {
  String response;
  if (sendRequest(url, response, maxTry)) {
    DeserializationError error = deserializeJson(doc, response);
    if (!error)
      return true;
    Serial.println("deserializeJson failed: " + response);
  }
  return false;
}

String getState() {
  if (!grblClient.connected() && grblClient.connect(grblIp.c_str(), grbl_port))
    Serial.println("getState: Connected to GRBL via Telnet");

  String state = "NA";
  String line;
  if (grblClient.connected()) {
    // cleanup leftover in the queue
    while (grblClient.available()) {
      line = grblClient.readStringUntil('\n');
#ifdef DEBUG
      Serial.println("before getState: " + line);
#endif
      if (line.substring(0, 5) == "<Idle") 
        return "Idle";
    }
    String cmd = "?";
#ifdef DEBUG
    Serial.println("getState: sending " + cmd);
#endif
    grblClient.println(cmd);
    delay(200);
    unsigned long now = millis();
    while (!grblClient.available() && millis() - now < 1000) {
      delay(50);
    }
    while (grblClient.available()) {
      line = grblClient.readStringUntil('\n');
#ifdef DEBUG
      Serial.println ("getState: " + line);
#endif
      if (line.substring(0, 5) == "ALARM") {
        return ("Alarm");
        break;
      }
      if (line.charAt(0) == '<') {
        // Extract state (between '<' and first '|')
        int startIdx = 1;
        int endIdx = line.indexOf('|');
        if (endIdx > 1) {
          state = line.substring(startIdx, endIdx);
          //Serial.println ("state=" + state);
          // get the current mpos
          const char* cstr = line.c_str();
          const char* mposPtr = strstr(cstr, "MPos:");
          if (mposPtr) {
            mposPtr += 5;  // Skip "MPos:"
            if (sscanf(mposPtr, "%f,%f,%f", &xMpos, &yMpos, &zMpos) == 3) {
              //Serial.printf("X=%.3f, Y=%.3f, Z=%.3f\n", xMpos, yMpos, zMpos);
            } else {
              Serial.println("Failed to parse MPos values");
            }
          } else {
              Serial.println("MPos not found");
          }
        }
      }
    }
  }
  //Serial.println ("return=" + state);
  return state;
}

bool sendCmd(String cmd, int ms = 100) {
  bool wait4Idle(int ms);
  bool getResp(String &lines, int ms);

  uint32_t start = millis();
  bool rv = false;
  if (!grblClient.connected() && grblClient.connect(grblIp.c_str(), grbl_port)) {
    delay(200);
    Serial.println("sendCmd: Connecting to GRBL via Telnet");
  }

  if (grblClient.connected()) {
    // cleanup leftover in the queue
    String line;
    while (grblClient.available()) {
      line = grblClient.readStringUntil('\n');
#ifdef DEBUG
      Serial.println("before sendCmd: " + line);
#endif
    }
#ifdef DEBUG
    Serial.println("sendCmd: " + cmd);
#endif
    grblClient.println(cmd);
    delay(200);
    rv = getResp(line, ms);
    // wait for idle state (if see the Idle already, don't need to wait)
    if (line.indexOf("Idle") == -1 && ms > 0 && grblClient.connected()) {
      rv = wait4Idle(ms);
    }
    Serial.printf("sendCmd: %s=%d, elape %d msec\n", cmd.c_str(), rv, millis() - start);
  } else
    Serial.println("failed to connect for cmd=" + cmd);
  return rv;
}

bool wait4Idle(int msec = 1000) {
  if (!grblStatus)
    return false;
  // wait for idle state
  bool rv = false;
  if (msec > 0 && grblClient.connected()) {
    unsigned long start = millis();
    while (millis() - start < msec) {
      String state = getState();
      if (state == "Idle")
        return true;
      else if (state == "Alarm") {
        Serial.println("Alarm: send $X to unlock");
        return sendCmd("$X");
      }
      delay(500);
    }
    if (getState() == "Idle")
      return true;
  }
  return rv;
}

bool getResp(String &lines, int ms = 100) {
  bool rv = false;
  if (ms > 0) {
    uint32_t end = millis() + ms;
    // wait for any response
    while (millis() < end && !grblClient.available()) {
      delay(10);
    }
  }
  bool alarm = false;
  String line;
  lines = "";
  while (grblClient.available()) {
    line = grblClient.readStringUntil('\n');
#ifdef DEBUG
    Serial.println("getResp: " + line);
#endif
    lines += line;
    if (line.substring(0, 5) == "ALARM" || line.substring(0, 6) == "<Alarm") {
      Serial.println("got ALARM: " + line + ", will send $X to unlock");
      alarm = true;
    } else if (line.substring(0, 2) == "ok") {
      rv = true;
    }
  }
  if (alarm)
    return sendCmd("$X");
  return rv;
}

void registerController() {
  String macAddress = WiFi.macAddress();
  String ipAddress = WiFi.localIP().toString();
  StaticJsonDocument<32> doc;
  String url = data_server + controlProcess + "?action=register&mac=" + macAddress + "&ip=" + ipAddress;
  Serial.println(url);
  if (!sendRequest(url, doc))
    return;
  if (doc["success"].as<int>() == 1) {
    control_id = doc["id"].as<int>();
    grblIp = doc["grbl_ip"].as<String>();
    grbl_id = doc["grbl_id"].as<int>();
    grblHttp = "http://" + grblIp;
    Y_BEGIN = doc["y0"].as<int>();
    Y_SCAN = doc["y1"].as<int>();
    Y_GRAP = doc["y2"].as<int>();
    
    Serial.printf("registerController(): id=%d, grblId=%d, grblIp=%s, Y_BEGIN=%d, Y_SCAN=%d, Y_GRAP=%d\n", control_id, grbl_id, grblIp.c_str(), Y_BEGIN, Y_SCAN, Y_GRAP);
  } else {
    Serial.println(doc["msg"].as<String>());
  }
}

void handleCapture() {
    static int errorCnt = 0;
    //Serial.println("Starting image capture...");
    WiFiClient client = server.client();
    if (!client) {
      Serial.println("handleCapture() failed on client");
      return;
    }
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        char msg[80];
        sprintf(msg, "Camera capture failed (%d).", ++errorCnt);
        Serial.println(msg);
        server.send(500, "text/plain", msg);
        if (errorCnt > 2) {
          Serial.println("restart to fix camera issue");
          delay(100);
          ESP.restart();
        }
        return;
    }
    if (errorCnt)
      errorCnt = 0;
    Serial.printf("Image captured successfully. width=%d height=%d, Size: %d bytes.\n", fb->width, fb->height, fb->len);
    if (client.connected()) {
        // Send headers manually
        client.println("HTTP/1.1 200 OK");
        client.print("Content-Type: ");
        client.println( "image/jpeg");
        client.print("Content-Length: ");
        client.println(fb->len);
        //client.println("Connection: close");
        client.println();

#ifdef BIG_SEND
        // Send the image data 
        size_t bytesSent = client.write(fb->buf, fb->len);
        if (bytesSent == fb->len) {
            //Serial.println("Image sent successfully to the client.");
        } else {
            Serial.printf("Failed to send the entire image. Bytes sent: %d of %d\n", bytesSent, fb->len);
        }
#else
        size_t bytesSent = 0;
        size_t chunkSize = 1024; // Adjust chunk size based on network stability

        while (bytesSent < fb->len) {
            size_t remaining = fb->len - bytesSent;
            size_t sendSize = (remaining < chunkSize) ? remaining : chunkSize;

            client.write(fb->buf + bytesSent, sendSize);

            bytesSent += sendSize;
            delay(10);  // Prevent blocking
        }
        client.stop();
        //Serial.println("Image sent successfully.");
#endif
    } else {
        Serial.println("Client disconnected before image could be sent.");
    }
    esp_camera_fb_return(fb);
}

void sendImageOverWebSocket(camera_fb_t *fb) {
  const size_t chunkSize = 3 * 400;
  unsigned char encoded[4 * 400 + 1];

  webSocket.broadcastTXT("IMG_START");

  for (size_t i = 0; i < fb->len; i += chunkSize) {
      size_t len = min(chunkSize, fb->len - i);
      size_t outLen;
      int res = mbedtls_base64_encode(encoded, sizeof(encoded), &outLen, fb->buf + i, len);
      if (res == 0) {
          encoded[outLen] = '\0';
          webSocket.broadcastTXT((char *)encoded);
      } else {
          Serial.printf("Base64 encode failed: %d\n", res);
          break;
      }
  }
  webSocket.broadcastTXT("IMG_END");
}

// send to server to do OCR
bool getUidFromQR(int &uid, String &msg) {
    delay(700);
    //Serial.println("getUidFromQR()");
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        msg = "Camera capture failed";
        return false;
    }
    sendImageOverWebSocket(fb);

    // send the image to server to decode the qrcode
    HTTPClient http;
    String url = data_server + qrProcess;
    http.begin(url);

    http.addHeader("Content-Type", "image/jpg");
    int httpResponseCode = http.POST(fb->buf, fb->len);
    String response = http.getString();
    //Serial.printf("getUidFromQR: length=%d, response=%s\n", fb->len, response.c_str());
    http.end();
    esp_camera_fb_return(fb);
    // retrieve the uid
    String paramName = "uid";
    if (httpResponseCode == 200) {
      int startIndex = response.indexOf(paramName + "=");
      if (startIndex == -1)
        return false; // Parameter not found
      startIndex += paramName.length() + 1; // Move index past 'paramName='
      int endIndex = response.indexOf('&', startIndex); // Find the next parameter delimiter
      if (endIndex == -1)
        endIndex = response.length(); // No more parameters, take rest of the string
      uid = response.substring(startIndex, endIndex).toInt();
      msg = "Success";
      Serial.printf("getUidFromQR: uid=%d\n", uid);
      return true;
    }
    msg = "No QRCode or decode failure";
    return false;
}

bool moveArm(int steps) {
  if (!grblStatus)
    return false;
  return sendCmd("G90G0Y" + String(steps), 3000);
}

bool moveWheel(int pos) {
  if (!grblStatus)
    return false;
  return sendCmd("G99X" + String(pos), 12000);
}

bool clamp(bool on) {
  if (!grblStatus)
    return false;
  sendCmd(on ? "M3S500" : "M5", 0);
  delay(300);
  return true;
}

int totSlot;
volatile bool isMapping = false;
volatile bool stopMapping = false;
int mapIndex = 0;
int mapStep = 0;
unsigned long mapStartTime;
bool autoLight(bool chgLightFirst = false);

bool initMapProcess() {
  String url;
  StaticJsonDocument<200> doc;
  if (grbl_id != -1) {
    url = data_server + grblProcess + "?ip="+ grblIp;
    // get total position for the grbl device
    if (!sendRequest(url, doc)) {
      Serial.printf("%s failed\n", url.c_str());
      return false;
    }
    totSlot = doc["xpos"].as<int>();
    Y_BEGIN = doc["y0"].as<int>();
    Y_SCAN = doc["y1"].as<int>();
    Y_GRAP = doc["y2"].as<int>();

    Serial.printf("%s, xpos=%d y0=%d y1=%d y2=%d\n", url.c_str(), totSlot, Y_BEGIN, Y_SCAN, Y_GRAP);
    // clean up the map
    url = data_server + controlProcess + "?action=cleanup&control_id=" + control_id;
    if (!sendRequest(url,  doc)) {
      Serial.printf("%s failed\n", url.c_str());
      return false;
    }
    initGrbl();
    isMapping = true;
    stopMapping = false;
    mapIndex = 0;
    mapStep = 0;
    Serial.printf("initMapProcess: totSlot=%d\n", totSlot);
    autoLight();
    return true;
  }
  Serial.println("grblId not set");
  return false;
}

bool processMapping() {
  static int uid = -1;
  static unsigned long start = millis();
  bool rv = false;
  switch(mapStep) {
    case 0:
      start = millis();
      if (mapIndex == 0)
        mapStartTime = start;
      uid = -1;
      if (yMpos > (float)Y_SCAN)
        rv = moveArm(Y_SCAN);
      else
        rv = true;
      Serial.printf("start to process nametag map at position %d\n", mapIndex);
      break;
    case 1:
      // move to position mapIndex
      rv = moveWheel(mapIndex);
      break;
    case 2:
      // move Y axis to grap position
      rv = moveArm(Y_GRAP);
      break;
    case 3:
      // clamp the nametag
      rv = clamp(true);
      delay(200);
      break;
    case 4:
      // move Y axis to scan position
      rv = moveArm(Y_SCAN);
      break;
    case 5:
      // wait until all queued command done before scan QR
      rv = wait4Idle();
      break;
    case 6:
      // scan QRCode
      {
      String msg;
      int cnt = 0;
      while (!getUidFromQR(uid, msg)) {
        if (cnt++ > 3) {
          if (!autoLight(true))
            break;
        }
        Serial.println(msg);
        yield(); // Allow WiFi and HTTP handling
      }
      // set the map
      if (uid != -1) {
        StaticJsonDocument<32> doc;
        String url = data_server + controlProcess + "?action=setupmap&control_id=" + control_id + "&uid=" + String(uid) + "&pos=" + String(mapIndex);
        if (!sendRequest(url,  doc))
          Serial.printf("%s failed\n", url.c_str());
        yield(); // Allow WiFi and HTTP handling
        server.handleClient();
      }
      }
      break;
    case 7:
      // move Y axis to finish position
      rv = moveArm(Y_GRAP);
      delay(200);
      break;
    case 8:
      // release the nametag
      rv = clamp(false);
      break;
    case 9:
      rv = moveArm(Y_SCAN);
      break;
    default:
      mapStep = -1;
      // done the current slot
      sendProgress(uid, millis() - start);
      if (++mapIndex >= totSlot) {
        // in case of missing final step, do it
        rv = clamp(false) && moveArm(Y_BEGIN);
        isMapping = false;
        mapIndex = 0;
      }
      grblClient.stop();
      break;
  }
  mapStep++;
  return rv;
}

void handleProcessQR() {
    Serial.println("handleProcessQR()");
    moveArm(Y_GRAP);
    clamp(true);
    delay(200);
    moveArm(Y_SCAN);
    wait4Idle(2000);
    int uid;
    String msg;
    if (!getUidFromQR(uid, msg)) {
        Serial.println(msg);
        server.send(500, "text/plain", msg);
    } else {
        server.send(200, "text/plain", String(uid));
    }
    moveArm(Y_GRAP);
    clamp(false);
    moveArm(Y_BEGIN);
    grblClient.stop();
}

void handlesetupNametagMap() {
  if (!isMapping) {
    if (initMapProcess())
      server.send(200, "text/plain", "Ready to go...");
    else
      server.send(500, "text/plain", "Failed to start nametage controllor");
  } else {
    server.send(200, "text/plain", "Already running");
  }
}

void handleStopMap() {
  Serial.printf("handleStopMap: stopMapping=%d isMapping=%d\n", stopMapping, isMapping);
  stopMapping = true;
  server.send(200, "text/plain", "Stopping...");
}

bool getNametag(int pos) {
 uint32_t start = millis();
  clamp(false);
  // if current Y is over SCAN location, move Y to half way (SCAN) to let the wheel to turn
  if (yMpos > (float)Y_SCAN)
    moveArm(Y_SCAN);
  // move to position
  moveWheel(pos);
  // move Y axis to grap position
  moveArm(Y_GRAP);
  // clamp the nametag
  clamp(true);
  delay(200);
  // move Y axis to beginning position
  moveArm(Y_BEGIN);
  // drop the nametag
  clamp(false);
  delay(100);
  bool rv = grblClient.connected();
  grblClient.stop();
  Serial.printf("getNametag: elape %d msec\n", millis() - start);
  return rv;
}

bool initGrbl() {
  if (getState() == "Alarm")
    sendCmd("$X");
  sendCmd("$H", 10000);
  moveArm(Y_BEGIN);
  moveWheel(0);
  clamp(false);
  bool rv = grblClient.connected();
  grblClient.stop();
  return rv;
}

bool autoLight(bool chgLightFirst) {
  String msg;
  int uid = -1;
  if (yMpos > (float)Y_SCAN)
    moveArm(Y_SCAN);
  wait4Idle(2000);
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, buttonInfo);
  if (error) {
      Serial.printf("autoLight(): deserializeJson() %s failed %s\n", buttonInfo.c_str(), error.c_str());
      return false;
  }
  int cnt = 0;
  bool lightOn = doc["light"].as<bool>();
  if (chgLightFirst) {
    lightOn = !lightOn;
    digitalWrite(LED_GPIO, lightOn ? HIGH : LOW); // change the light and try
    doc["light"] = lightOn;
  }
  while(cnt++ < 4) {
    if (getUidFromQR(uid, msg) && uid == -1) {
      Serial.printf("autoLight done, %s, light is %s\n", msg.c_str(), lightOn ? "ON" : "OFF");
      if (cnt > 1) {
        serializeJson(doc, buttonInfo);
        Serial.println("new buttons: " + buttonInfo);
        needButtonInfo = true;
      }
      return true;
    }
    lightOn = !lightOn;
    digitalWrite(LED_GPIO, lightOn ? HIGH : LOW); // change the light and try
    doc["light"] = lightOn;
  }
  return false;
}

void handleGetNametag() {
  if (server.hasArg("uid")) {
    String uid = server.arg("uid");
    Serial.println("handleGetNametag(): uid=" + uid);
    int pos = -1;
    // get the position first
    StaticJsonDocument<32> doc;
    String url = data_server + controlProcess + "?action=lookup&uid=" + uid;
    Serial.println(url);
    if (sendRequest(url, doc) && doc["success"].as<int>() == 1) {
      // found on the other nametag controllor, forward the request
      if (doc["control_id"].as<int>() != control_id) {
        String ip = doc["ip"].as<String>();
        Serial.println("handlGetNameTag(): redirect to " + ip);
        url = ip + "/getNametag?uid=" + uid;
        if (sendRequest(url, doc) && doc["success"].as<int>() == 1) {
          server.send(200, "text/plain", "Got nametag for " + uid + " at " + ip);
          return;
        }
      } else {
        pos = doc["pos"].as<int>();
        Serial.printf("found position %d for uid %s\n", pos, uid.c_str());
      }
    } else {
      Serial.printf("uid %s not found\n", uid.c_str());
      String msg = doc["msg"].as<String>();
      Serial.println(msg);
      server.send(500, "text/plain", msg);
      return;
    }
    if (grbl_id == -1 || pos == -1 || !getNametag(pos)) {
        server.send(500, "text/plain", "Failed to get nametag for " + uid);
    } else {
        server.send(200, "text/plain", "Got nametag for " + uid + " at " + String(pos));
    }
  } else {
    server.send(500, "text/plain", "Missing argument");
  }
}
void handleLightOn() {
    digitalWrite(LED_GPIO, HIGH); // Turn LED ON
    server.send(200, "text/plain", "light ON");
}
void handleLightOff() {
    digitalWrite(LED_GPIO, LOW); // Turn LED OFF
    server.send(200, "text/plain", "light OFF");
}
void handleHomePos() {
    if (moveArm(Y_BEGIN) && moveWheel(0) && clamp(false))
      server.send(200, "text/plain", "Done");
    else
      server.send(500, "text/plain", "Failed to go to Home position");
    grblClient.stop();
}
void handleGrblStatus() {
  String json = "{\"online\":";
  json += (grblStatus ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}
void handleGrblInit() {
  if (initGrbl())
    server.send(200, "text/plain", "GRBL is initialized");
  else
    server.send(500, "text/plain", "Failed to go to initial Grbl");
}
void sendProgress(int uid, int duration) {
  int elaps = (millis() - mapStartTime) / 1000;
  String message = "Progress: " + String(mapIndex+1) + "/" + String(totSlot) + ", ID " + String(uid) + ", duration " + String(duration/1000.0) + " sec (" + String(elaps / 60) + ":" + String(elaps%60) + ")";
  Serial.println(message);
  webSocket.broadcastTXT(message);  // Send update to all connected clients
}

void handlePosition() {
  if (server.hasArg("pos") && server.hasArg("dir")) {
    int pos = server.arg("pos").toInt();
    int dir = server.arg("dir").toInt();
    Serial.printf("handlgetPosition(): pos=%d dir=%d\n", pos, dir);
    if (pos == 0 && dir == 1) { // grep one nametag for line up, clamp one in 10 sec
      clamp(false);
      moveArm(Y_GRAP);
      delay(10000);
    }
    clamp(true);
    moveArm(Y_BEGIN);
    moveWheel(pos);
    moveArm(Y_GRAP);
    server.send(200, "text/plain", "Position " + String(pos) + " is in place");
  } else {
    server.send(500, "text/plain", "Missing argument for NameTag position");
  }
}

void handleButton() {
  if (server.hasArg("plain")) {
    buttonInfo = server.arg("plain");
    Serial.println("handleButton:broadcast: " + buttonInfo);
    webSocket.broadcastTXT(buttonInfo);  // Send update to all connected clients (includes ourself)
  }
  server.send(200, "text/plain", "OK");
}

void handleRegister() {
  registerController();
  server.send(200, "text/plain", "OK");
}

unsigned long lastHeartbeat = 0;
const unsigned long heartbeatInterval = 60000;  // 60 seconds

void sendHeartbeat() {
  StaticJsonDocument<32> doc;
  String url = data_server + controlProcess + "?action=heartbeat&control_id=" + control_id;
  if (sendRequest(url, doc) && doc["success"].as<int>() == 1) {
    grblStatus = doc["grblStatus"].as<int>();
    //Serial.printf("Heartbeat sent. got grbl: %d in response\n", grblStatus);
  } else {
    Serial.println("Failed to send heartbeat: " + url);
  }
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_GPIO, OUTPUT);
    digitalWrite(LED_GPIO, LOW); // LED OFF initially

    // Connect to Wi-Fi
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    ArduinoOTA.onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH) {
        type = "sketch";
      } else { // U_FS
        type = "filesystem";
      }
      // NOTE: if updating FS this would be the place to unmount FS using FS.end()
      Serial.println("Start OTA updating " + type);
    });
    /*
    ArduinoOTA.onEnd([]() {
      Serial.println("\nOTA End");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
    });
    */
    ArduinoOTA.onError([](ota_error_t error) {
      Serial.printf("OTA Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) {
        Serial.println("Auth Failed");
      } else if (error == OTA_BEGIN_ERROR) {
        Serial.println("Begin Failed");
      } else if (error == OTA_CONNECT_ERROR) {
        Serial.println("Connect Failed");
      } else if (error == OTA_RECEIVE_ERROR) {
        Serial.println("Receive Failed");
      } else if (error == OTA_END_ERROR) {
        Serial.println("End Failed");
      }
    });
    ArduinoOTA.setHostname("nametag");
    ArduinoOTA.begin();
    Serial.println("OTA Ready");

    // Start the camera
    startCamera();
    // start Telnet server
    telnetServer.begin();
    telnetServer.setNoDelay(true);
    // Setup web server routes
    server.on("/", HTTP_GET, handleRoot);
    server.on("/capture", HTTP_GET, handleCapture);
    server.on("/processQR", HTTP_GET, handleProcessQR);
    server.on("/setNametagMap", HTTP_GET, handlesetupNametagMap);
    server.on("/stopMap", HTTP_GET, handleStopMap);
    server.on("/getNametag", HTTP_GET, handleGetNametag);
    server.on("/lighton", HTTP_GET, handleLightOn);
    server.on("/lightoff", HTTP_GET, handleLightOff);
    server.on("/homePos", HTTP_GET, handleHomePos);
    server.on("/grblStatus", HTTP_GET, handleGrblStatus);
    server.on("/grblInit", HTTP_GET, handleGrblInit);
    server.on("/position", HTTP_GET, handlePosition);
    server.on("/updButton", HTTP_POST, handleButton);
    server.on("/register", HTTP_GET, handleRegister);
    server.onNotFound([]() {
      Serial.printf("Unhandled request: %s\n", server.uri().c_str());
      server.send(404, "text/plain", "Not Found");
    });

    server.begin();
    webSocket.begin();

    Serial.println("HTTP server started");
    registerController();
    sendHeartbeat();
    initGrbl();
}

void loop() {
    if (telnetServer.hasClient()) {
      if (telnetClient && telnetClient.connected()) {
        Serial.println("Drop this session and connect to new client");
        Serial.flush();
        telnetClient.stop(); // Kick old client
      }
      telnetClient = telnetServer.available();
      Serial.println("New Telnet client connected");
    }
    if (Serial.available()) {
      char c = Serial.read();
      Serial.print(c);
    }
    ArduinoOTA.handle();
    server.handleClient();
    if (isMapping && !stopMapping) {
      processMapping();
    } else if (stopMapping) {
      grblClient.stop();
      isMapping = false;
      stopMapping = false;
      webSocket.broadcastTXT("");
    } 
    webSocket.loop();
    unsigned long now = millis();
    if (now - lastHeartbeat >= heartbeatInterval) {
      lastHeartbeat = now;
      sendHeartbeat();
    }
    String line;
    while (getResp(line, 0));
    if (needButtonInfo) {
      needButtonInfo = false;
      Serial.println("needButtonInfo/broadcast: " + buttonInfo);
      webSocket.broadcastTXT(buttonInfo);  // Send update to all connected clients (includes ourself)
    }  
}
