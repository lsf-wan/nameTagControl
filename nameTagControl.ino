// Camera configuration
#define CAMERA_MODEL_AI_THINKER

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
const int Y_GRAP = 17;
const int Y_SCAN = 9;
const int Y_DROP = 0;

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

int cropWidth = 480;
int cropHeight = 300;

int control_id;
int grbl_id = -1;
bool grblStatus = false;
String grblIp;
String grblHttp;

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
        let lightOn = false;
        let captureOn = true;
        let isMapping = false;

        function refreshImage() {
            if (captureOn) {
                document.getElementById('cameraFeed').src = '/capture?' + new Date().getTime();
            }
        }
        setInterval(refreshImage, 1000);  // Refresh image every 1/2 second
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
            let btn = document.getElementById('captureBtn');
            btn.innerText = captureOn ? 'Stop Capture' : 'Start Capture';
        }

        function toggleMap() {
          const mapBtn = document.getElementById('mapBtn');
          const msgDisplay = document.getElementById('MsgResult');

          if (!isMapping) {
            console.log("toggleMap(): isMapping is false, going to start");
            if (captureOn)
              toggleCapture();
            isMapping = true;
            mapBtn.innerText = "Stop Mapping";
            msgDisplay.innerText = "Starting setup...";

            fetch('/setNametagMap')
              .then(response => response.text())
              .then(text => {
                msgDisplay.innerText = text;
              });
          } else {
            console.log("toggleMap(): isMapping is true, going to stop");
            isMapping = false;
            mapBtn.innerText = "Setup Map";
            // Stop mapping
            fetch('/stopMap')
              .then(response => response.text())
              .then(text => {
                msgDisplay.innerText = text;
              })
              .catch(error => {
                document.getElementById('MsgResult').innerText = 'Error: ' + error;
              });
          }
        }

        function toggleLight() {
            lightOn = !lightOn;
            let url = lightOn ? '/lighton' : '/lightoff';
            fetch(url)
            .then(response => response.text())
            .then(data => {
                document.getElementById('lightBtn').innerText = lightOn ? 'Turn Light Off' : 'Turn Light On';
            })
            .catch(error => {
                document.getElementById('MsgResult').innerText = 'Error: ' + error;
            });
        }

        function processOCR() {
            document.getElementById('MsgResult').innerText = 'Processing...';
            fetch('/processQR')
            .then(response => response.text())
            .then(qrCode => {
                document.getElementById('MsgResult').innerText = 'UID: ' + qrCode;
            })
            .catch(error => {
                document.getElementById('MsgResult').innerText = 'Error: ' + error;
            });
        }
        function homePos() {
            document.getElementById('MsgResult').innerText = 'Going...';
            fetch('/homePos')
              .then(response => response.text())
              .then(text => {
                document.getElementById('MsgResult').innerText = text;
              })
              .catch(error => {
                document.getElementById('MsgResult').innerText = 'Error: ' + error;
              });
        }
        const ws = new WebSocket(`ws://${window.location.hostname}:81/`);
        ws.onmessage = (event) => {
            document.getElementById("MsgResult").innerText = event.data;
            console.log(event.data);  // Log progress updates
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
    <button onclick='processOCR()'>Get QR Code</button>
    <button id="mapBtn" onclick="toggleMap()">Setup Map</button>
    <button onclick='homePos()'>Go Home</button>
    <p id='MsgResult'></p>
</body>
</html>
)rawliteral";

    server.send(200, "text/html", html);
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
        Serial.print("Response from server:");
        Serial.println(response);
        http.end();  // Free resources
        return true;
      }
      Serial.printf("sendRequest failed. Error: %s\n", http.errorToString(httpCode).c_str());
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

void registerController() {
  String macAddress = WiFi.macAddress();
  String ipAddress = WiFi.localIP().toString();
  StaticJsonDocument<32> doc;
  String url = data_server + controlProcess + "?action=config&mac=" + macAddress + "&ip=" + ipAddress;
  if (!sendRequest(url, doc))
    return;
  if (doc["success"].as<int>() == 1) {
    control_id = doc["id"].as<int>();
    grblIp = doc["grbl_ip"].as<String>();
    grbl_id = doc["grbl_id"].as<int>();
    grblHttp = "http://" + grblIp;

    Serial.printf("registerController(): id=%d grblId=%d grblIp=%s\n", control_id, grbl_id, grblIp.c_str());
  } else {
    Serial.println(doc["msg"].as<String>());
  }
}

void handleCapture() {
    //Serial.println("Starting image capture...");
    WiFiClient client = server.client();
    if (!client) {
      Serial.println("handleCapture() failed on client");
      return;
    }
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed.");
        server.send(500, "text/plain", "Camera capture failed.");
        return;
    }
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

// send to server to do OCR
bool getUidFromQR(int &uid, String &msg) {
    Serial.println("getUidFromQR()");
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        msg = "Camera capture failed";
        return false;
    }
    esp_camera_fb_return(fb);

    // send the image to server to decode the qrcode
    HTTPClient http;
    String url = data_server + qrProcess;
    http.begin(url);

    http.addHeader("Content-Type", "image/jpg");
    int httpResponseCode = http.POST(fb->buf, fb->len);
    String response = http.getString();
    Serial.printf("getUidFromQR: length=%d, response=%s\n", fb->len, response.c_str());
    http.end();
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
      return true;
    }
    msg = "No QRCode or decode failure";
    return false;
}

bool moveArm(int steps) {
  if (!grblStatus)
    return false;
  String response;
  String url = grblHttp + "/send?cmd=G90G0Y" + String(steps);
  if (!sendRequest(url, response)) {
    Serial.printf("%s failed\n", url.c_str());
    return false;
  }
  yield(); // Allow WiFi and HTTP handling
  return true;
}

bool moveWheel(int pos) {
  if (!grblStatus)
    return false;
  String response;
  String url = grblHttp + "/send?cmd=Move:" + String(pos);
  if (!sendRequest(url, response)) {
    Serial.printf("%s failed\n", url.c_str());
    return false;
  }
  Serial.printf("***************move to position %d\n", pos);
  yield(); // Allow WiFi and HTTP handling
  return true;
}

bool clamp(bool on) {
  String response;
  String url = grblHttp + "/send?cmd=";
  url += on ? "M3" : "M5";
  if (!sendRequest(url, response)) {
    Serial.printf("%s failed\n", url.c_str());
    return false;
  }
  return true;
}

bool wait4Idle() {
  if (!grblStatus)
    return false;
  String response;
  String url = grblHttp + "/wait?msec=1000";
  //Serial.printf("wait4Idle: %s\n", url.c_str());
  if (!sendRequest(url, response)) {
    Serial.printf("%s failed\n", url.c_str());
    return false;
  }
  //Serial.printf("wait4Idle: done\n");
  return true;
}

int totSlot;
volatile bool isMapping = false;
volatile bool stopMapping = false;
int mapIndex = 0;
int mapStep = 0;

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
    totSlot = doc["pos"].as<int>();
    // clean up the map
    url = data_server + controlProcess + "?action=cleanup&control_id=" + control_id;
    if (!sendRequest(url,  doc)) {
      Serial.printf("%s failed\n", url.c_str());
      return false;
    }
    isMapping = true;
    stopMapping = false;
    mapIndex = 0;
    mapStep = 0;
    Serial.printf("initMapProcess: totSlot=%d\n", totSlot);
    return true;
  }
  Serial.println("grblId not set");
  return false;
}

bool processMapping() {
  switch(mapStep) {
    case 0:
      Serial.printf("start to process nametag map at position %d\n", mapIndex);
      moveArm(Y_SCAN);
      break;
    case 1:
      // move to position mapIndex
      moveWheel(mapIndex);
      break;
    case 2:
      // move Y axis to grap position
      moveArm(Y_GRAP);
      break;
    case 3:
      // clamp the nametag
      clamp(true);
      break;
    case 4:
      // move Y axis to scan position
      moveArm(Y_SCAN);
      break;
    case 5:
      // wait until all queued command done before scan QR
      wait4Idle();
      break;
    case 6:
      // scan QRCode
      {
      int uid = -1;
      String msg;
      int cnt = 0;
      while (!getUidFromQR(uid, msg)) {
        if (cnt++ > 3)
          break;
        Serial.println(msg);
        yield(); // Allow WiFi and HTTP handling
        delay(500);
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
      // move Y axis to finish position and drop
      moveArm(Y_GRAP);
      break;
    case 8:
      // release the nametag
      clamp(false);
      break;
    case 9:
      moveArm(Y_SCAN);
      break;
    default:
      mapStep = -1;
      // done the current slot
      if (++mapIndex >= totSlot) {
        // in case of missing final step, do it
        clamp(false);
        moveArm(Y_DROP);
        isMapping = false;
        mapIndex = 0;
      }
      sendProgress();
      break;
  }
  mapStep++;
  return true;
}

void handleProcessQR() {
    Serial.println("handleProcessQR()");
    moveArm(Y_SCAN);
    wait4Idle();
    delay(500);
    int uid;
    String msg;
    if (!getUidFromQR(uid, msg)) {
        Serial.println(msg);
        server.send(500, "text/plain", msg);
    } else {
        server.send(200, "text/plain", String(uid));
    }
}

void handlesetupNametagMap() {
  if (!isMapping) {
    if (initMapProcess())
      server.send(200, "text/plain", "Initializing");
    else
      server.send(500, "text/plain", "Failed to setup nametage controllor");
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
  return (
    // move Y to half way (SCAN) to get ready
    moveArm(Y_SCAN) &&
    // move to position
    moveWheel(pos) &&
    // move Y axis to grap position
    moveArm(Y_GRAP) &&
    // clamp the nametag
    clamp(true) &&
    // move Y axis to drop position
    moveArm(Y_DROP) &&
    // drop the nametag
    clamp(false)
  );
}

void handlgetNametag() {
  if (server.hasArg("uid")) {
    String uid = server.arg("uid");
    Serial.printf("handlgetNametag(): %s\n", uid.c_str());
    int pos;
    // get the position first
    StaticJsonDocument<32> doc;
    String url = data_server + controlProcess + "?action=lookup&uid=" + uid;
    Serial.println(url);
    if (sendRequest(url, doc) && doc["success"].as<int>() == 1) {
      pos = doc["id"].as<int>();
      Serial.printf("found position %d for uid %s\n", pos, uid.c_str());
    } else {
      Serial.printf("uid %s not found\n", uid.c_str());
      String msg = doc["msg"].as<String>();
      Serial.println(msg);
      server.send(500, "text/plain", msg);
      return;
    }
    if (grbl_id == -1 || !getNametag(pos)) {
        server.send(500, "text/plain", "Failed to get nametag for " + String(uid));
    } else {
        server.send(200, "text/plain", "Got nametag for " + String(uid));
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
    digitalWrite(LED_GPIO, LOW); // Turn LED ON
    server.send(200, "text/plain", "light OFF");
}
void handleHomePos() {
    if (moveArm(Y_DROP) && moveWheel(0))
      server.send(200, "text/plain", "Back to Home");
    else
      server.send(500, "text/plain", "Failed to go to Home position");
}
void handleGrblStatus() {
  String json = "{\"online\":";
  json += (grblStatus ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}
void sendProgress() {
    String message = "Progress: " + String(mapIndex) + "/" + String(totSlot);
    Serial.println(message);
    webSocket.broadcastTXT(message);  // Send update to all connected clients
}

unsigned long lastHeartbeat = 0;
const unsigned long heartbeatInterval = 10000;  // 10 seconds

void sendHeartbeat() {
  StaticJsonDocument<32> doc;
  String url = data_server + controlProcess + "?action=heartbeat&control_id=" + control_id;
  if (!sendRequest(url, doc))
    return;
  if (doc["success"].as<int>() == 1) {
    grblStatus = doc["grblStatus"].as<int>();
    Serial.printf("Heartbeat sent. got grbl: %d in response\n", grblStatus);
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
    ArduinoOTA.begin();
    Serial.println("OTA Ready");

    // Start the camera
    startCamera();

    // Setup web server routes
    server.on("/", HTTP_GET, handleRoot);
    server.on("/capture", HTTP_GET, handleCapture);
    server.on("/processQR", HTTP_GET, handleProcessQR);
    server.on("/setNametagMap", HTTP_GET, handlesetupNametagMap);
    server.on("/stopMap", HTTP_GET, handleStopMap);
    server.on("/getNametag", HTTP_GET, handlgetNametag);
    server.on("/lighton", HTTP_GET, handleLightOn);
    server.on("/lightoff", HTTP_GET, handleLightOff);
    server.on("/homePos", HTTP_GET, handleHomePos);
    server.on("/grblStatus", HTTP_GET, handleGrblStatus);
    server.onNotFound([]() {
      Serial.printf("Unhandled request: %s\n", server.uri().c_str());
      server.send(404, "text/plain", "Not Found");
    });

    server.begin();
    webSocket.begin();

    Serial.println("HTTP server started");
    registerController();
    sendHeartbeat();
}

void loop() {
    ArduinoOTA.handle();
    server.handleClient();
    if (isMapping && !stopMapping) {
      processMapping();
    } else if (stopMapping) {
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
}
