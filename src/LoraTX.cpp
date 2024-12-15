// #include <SPI.h>
// #include <LoRa.h>
// #include <Wire.h>
 
// #define ss 15
// #define rst 16
// #define dio0 2
// #define onboardLED 2 // Onboard LED (GPIO2)
// int counter = 0;

// // Frequency
// #define LORA_FREQUENCY 433E6
// unsigned long lastSendTime = 0;
// const int SEND_INTERVAL = 5000;  // Send every 5 seconds
// const int TICK_ACK_TIMEOUT = 2000;    // Wait for ACK for 2 seconds
// bool ackReceived = false;

// // Timeout for no acknowledgment (in milliseconds)
// const unsigned long ACK_TIMEOUT = 30000;
// unsigned long lastAckTime = 0;

 
// void setup() 
// {
//   Serial.begin(115200);
//   while (!Serial);

//   pinMode(onboardLED, OUTPUT);
//   digitalWrite(onboardLED, HIGH); // Turn off LED initially

//   Serial.println("LoRa Sender");
//   LoRa.setPins(ss, rst, dio0);
//     if (!LoRa.begin(LORA_FREQUENCY)) {
//     Serial.println("Starting LoRa failed!");
//     delay(100);
//     while (1);
//   }

//     // Configure LoRa parameters for maximum range
//   LoRa.setTxPower(20, PA_OUTPUT_PA_BOOST_PIN);      // Max TX power: 20 dBm
//   LoRa.setSpreadingFactor(12); // Spreading Factor: 12 (max range, slower data rate)
//   LoRa.setSignalBandwidth(125E3); // Bandwidth: 125 kHz (low bandwidth, higher range)
//   LoRa.setCodingRate4(5);   // Coding Rate: 4/5 (robust against interference)

//   lastAckTime = millis(); // Initialize last acknowledgment time
// }

// void loop() {
//   // Send message every SEND_INTERVAL
//   if (millis() - lastSendTime > SEND_INTERVAL) {
//     lastSendTime = millis();
//     sendMessage("Hello LoRa", counter);
//     counter++;
//     // Wait for acknowledgment
//     if (waitForAck(TICK_ACK_TIMEOUT)) { // Wait for ACK for up to 2 seconds
//       Serial.println("ACK received!");
//       lastAckTime = millis(); // Update last acknowledgment time
//       blinkLED();             // Blink LED for successful ACK
//     } else {
//       Serial.println("No ACK received.");
//     }

//     // Check for timeout
//     if (millis() - lastAckTime > ACK_TIMEOUT) {
//       Serial.println("No ACK received for more than 30 seconds. Turning on LED.");
//       digitalWrite(onboardLED, LOW); // Turn ON LED (active low)
//     } else {
//       digitalWrite(onboardLED, HIGH); // Turn OFF LED
//     }
//   }
// }

// // Function to send a message with a text and a counter
// void sendMessage(String message, int counter) {
//   String fullMessage = message + " #" + String(counter); // Combine message and counter
//   Serial.print("Sending message: ");
//   Serial.println(fullMessage);

//   LoRa.beginPacket();
//   LoRa.print(fullMessage);
//   LoRa.endPacket();
// }

// // Function to wait for an acknowledgment
// bool waitForAck(unsigned long timeout) {
//   unsigned long startTime = millis();
//   while (millis() - startTime < timeout) {
//     int packetSize = LoRa.parsePacket();
//     if (packetSize) {
//       String receivedMessage = "";
//       while (LoRa.available()) {
//         receivedMessage += (char)LoRa.read();
//       }
//       if (receivedMessage == "ACK") {
//         return true; // ACK received
//       }
//     }
//   }
//   return false; // Timeout reached, no ACK
// }

// // Function to blink onboard LED for 1 second
// void blinkLED() {
//   digitalWrite(onboardLED, LOW);  // Turn LED ON
//   delay(1000);                    // Wait for 1 second
//   digitalWrite(onboardLED, HIGH); // Turn LED OFF
// }


#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <ESP8266WiFi.h>
#include <WebSocketsServer.h>
#include <ESP8266WebServer.h>

// Pin definitions
#define ss 15
#define rst 16
#define dio0 2
#define onboardLED 2 // Onboard LED (GPIO2)

// LoRa parameters
#define LORA_FREQUENCY 433E6
#define ACK_TIMEOUT 30000 // Timeout for acknowledgment (30 seconds)

// Wi-Fi parameters
const char *ssid = "LoRaTx_Hotspot";  // Hotspot SSID
const char *password = "";         // Open network (no password)

// Global variables
ESP8266WebServer server(80);       // Web server on port 80
WebSocketsServer webSocket(81);    // WebSocket server on port 81
unsigned long lastAckTime = 0;     // Last time acknowledgment was received
int messageCounter = 0;            // Counter for sent packets
String latestRSSI = "";            // Last received RSSI
bool ackReceived = false;          // Flag for acknowledgment status

void sendMessage(String message);
bool waitForAck(unsigned long timeout);
void blinkLED();
String prepareWebSocketMessage(String message, bool ack, String rssi);
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length);
void handleRoot();

void setup() {
  Serial.begin(115200);
  while (!Serial);

  pinMode(onboardLED, OUTPUT);
  digitalWrite(onboardLED, HIGH); // Turn off LED initially

  LoRa.setSpreadingFactor(12);           // Maximum range
  LoRa.setSignalBandwidth(125E3);        // Default bandwidth
  LoRa.setCodingRate4(5);                // Improved robustness
  LoRa.setTxPower(20, PA_OUTPUT_PA_BOOST_PIN); // Max power with PA_BOOST

  Serial.println("LoRa Initialized");

  // Initialize Wi-Fi hotspot
  WiFi.softAP(ssid, password);
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("Hotspot IP: ");
  Serial.println(myIP);

  // Initialize web server
  server.on("/", handleRoot);
  server.begin();
  Serial.println("HTTP server started");

  // Initialize WebSocket server
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  // Initialize LoRa
  LoRa.setPins(ss, rst, dio0);
  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("Starting LoRa failed!");
    while (1);
  }

  lastAckTime = millis(); // Initialize last acknowledgment time
}

void loop() {
  server.handleClient();      // Handle HTTP requests
  webSocket.loop();           // Handle WebSocket connections

  // Send LoRa message
  String message = "Hello LoRa #" + String(messageCounter);
  sendMessage(message);
  ackReceived = waitForAck(2000); // Wait for acknowledgment (2 seconds)
  
  // Update acknowledgment and RSSI
  if (ackReceived) {
    Serial.println("ACK received!");
    lastAckTime = millis();
    blinkLED();
  } else {
    Serial.println("No ACK received.");
  }

  // Check for timeout
  if (millis() - lastAckTime > ACK_TIMEOUT) {
    digitalWrite(onboardLED, LOW); // Turn on LED if no ACK in 30 seconds
  } else {
    digitalWrite(onboardLED, HIGH); // Turn off LED
  }

  // Update WebSocket clients with latest data
  String webSocketData = prepareWebSocketMessage(message, ackReceived, latestRSSI);
  webSocket.broadcastTXT(webSocketData);

  messageCounter++; // Increment message counter
  delay(5000);      // Wait before sending the next message
}

void sendMessage(String message) {
  Serial.print("Sending message: ");
  Serial.println(message);

  LoRa.beginPacket();
  LoRa.print(message);
  LoRa.endPacket();
}

// Wait for acknowledgment
bool waitForAck(unsigned long timeout) {
  unsigned long startTime = millis();
  while (millis() - startTime < timeout) {
    int packetSize = LoRa.parsePacket();
    if (packetSize) {
      String receivedMessage = "";
      while (LoRa.available()) {
        receivedMessage += (char)LoRa.read();
      }
      latestRSSI = String(LoRa.packetRssi()); // Update RSSI
      if (receivedMessage == "ACK") {
        return true; // ACK received
      }
    }
  }
  return false; // Timeout
}

// Blink onboard LED for 1 second
void blinkLED() {
  digitalWrite(onboardLED, LOW);  // Turn LED ON
  delay(1000);
  digitalWrite(onboardLED, HIGH); // Turn LED OFF
}

// Prepare WebSocket message
String prepareWebSocketMessage(String message, bool ack, String rssi) {
  return "Message: " + message + "\nACK: " + (ack ? "Yes" : "No") + "\nRSSI: " + rssi;
}

// Handle WebSocket events
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
  if (type == WStype_TEXT) {
    Serial.printf("WebSocket message from client: %s\n", payload);
    String finalData = "Server received: " + String((char *)payload);
    webSocket.sendTXT(num, finalData);
  }
}

// Handle root HTTP page
void handleRoot() {
  String html = R"rawliteral(
  <!DOCTYPE html>
  <html>
  <head>
    <title>LoRa TX</title>
    <style>
      body {
        background-color: #121212; /* Dark background */
        color: #ffffff; /* Light text */
        font-family: Arial, sans-serif;
        margin: 0;
        padding: 0;
      }
      h1 {
        text-align: center;
        margin: 20px 0;
      }
      #data {
        background-color: #1e1e1e; /* Slightly lighter than background */
        color: #00ff00; /* Green text for messages */
        border: 1px solid #333; /* Subtle border */
        padding: 10px;
        margin: 20px auto;
        width: 90%;
        max-width: 800px;
        height: 300px; /* Fixed height */
        overflow-y: auto; /* Enable scrolling */
        white-space: pre-wrap; /* Wrap text properly */
        border-radius: 5px; /* Rounded corners */
      }
    </style>
    <script>
      var socket = new WebSocket("ws://" + location.hostname + ":81/");
      socket.onmessage = function(event) {
        const dataBox = document.getElementById("data");
        dataBox.innerText += event.data + "\n"; // Append received data
        dataBox.scrollTop = dataBox.scrollHeight; // Auto-scroll to the bottom
      };
    </script>
  </head>
  <body>
    <h1>LoRa Transmitter Dashboard</h1>
    <div id="data">Waiting for data...</div>
  </body>
  </html>
  )rawliteral";
  server.send(200, "text/html", html);
}
