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
const char *password = "";            // Open network (no password)

// Global variables
ESP8266WebServer server(80);       // Web server on port 80
WebSocketsServer webSocket(81);    // WebSocket server on port 81
unsigned long lastAckTime = 0;     // Last time acknowledgment was received
int messageCounter = 0;            // Counter for sent packets
String latestRSSI = "";            // Last received RSSI
bool ackReceived = false;          // Flag for acknowledgment status
bool ackInProgress = false;
unsigned long lastMessageTime = 0; // For tracking the interval between messages
unsigned long ackWaitStart = 0;    // For tracking ack wait time
unsigned long lastNoAckMsgTime = 0;

void sendMessage(String message);
bool waitForAck(unsigned long timeout);
void blinkLED();
void sendWebSocketMessage(String message);
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length);
void handleRoot();

void setup() {
  Serial.begin(115200);
  while (!Serial);

  pinMode(onboardLED, OUTPUT);
  digitalWrite(onboardLED, HIGH); // Turn off LED initially

  // Initialize Wi-Fi hotspot
  WiFi.softAP(ssid, password);
  IPAddress myIP = WiFi.softAPIP();
  while (myIP[0] == 0) {  // Check if the IP is assigned
    delay(100);
    myIP = WiFi.softAPIP();
  }
  Serial.print("Hotspot IP: ");
  Serial.println(myIP);

  // Initialize web server
  server.on("/", handleRoot);
  server.begin();
  Serial.println("HTTP server started");

  // Initialize WebSocket server
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("WebSocket server started");

  // Initialize LoRa
  Serial.println("LoRa Transmitter");
  LoRa.setPins(ss, rst, dio0);
  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("Starting LoRa failed!");
    sendWebSocketMessage("Starting LoRa failed!");
    while (1);
  }

  LoRa.setSpreadingFactor(12);           // Maximum range
  LoRa.setSignalBandwidth(125E3);        // Default bandwidth
  LoRa.setCodingRate4(5);                // Improved robustness
  LoRa.setTxPower(20, PA_OUTPUT_PA_BOOST_PIN); // Max power with PA_BOOST

  Serial.println("LoRa Initialized");

  lastAckTime = millis(); // Initialize last acknowledgment time
}

void loop() {
  server.handleClient();      // Handle HTTP requests
  webSocket.loop();           // Handle WebSocket connections

  // Check if it's time to send the next message
  if (millis() - lastMessageTime >= 5000 && !ackInProgress) {  // 5 seconds between messages
    lastMessageTime = millis(); // Reset time for next message

    // Send LoRa message
    String message = "Hello LoRa #" + String(messageCounter);
    sendMessage(message);
    ackReceived = false;  // Reset ack flag
    ackInProgress = true; // Indicate that we're waiting for ack
    ackWaitStart = millis(); // Start waiting for acknowledgment

    // Continuously update WebSocket clients with latest data
    sendWebSocketMessage("Current message #" + String(messageCounter) + 
    "\nACK: " + (ackReceived ? "Yes" : "No") + "\nRSSI: " + latestRSSI);

    // Update WebSocket message with status
    sendWebSocketMessage(message + "\nWaiting for ACK...");

    messageCounter++; // Increment message counter
  }

  // Check if we should process acknowledgment
  if (ackInProgress && millis() - ackWaitStart >= 2000) { // 2 seconds for ACK
    ackReceived = waitForAck(0);  // Check for ack (but don't block for too long)
    ackInProgress = false; // Done waiting for ack

    // Update LED and WebSocket based on ack status
    if (ackReceived) {
      lastAckTime = millis();
      blinkLED();
      sendWebSocketMessage("ACK received");
      digitalWrite(onboardLED, HIGH); // Turn off LED (ack received)
    } else {
      digitalWrite(onboardLED, LOW);  // Turn on LED (no ack)
    }
  }

  // Check for timeout in receiving ack (longer than 30 sec)
  if (millis() - lastAckTime > ACK_TIMEOUT && !ackInProgress && millis() > lastNoAckMsgTime + ACK_TIMEOUT) { //show message only once in every 30sec is connection is lost
    lastNoAckMsgTime = millis();
    sendWebSocketMessage("Not received ACK for more than 30 sec");
    digitalWrite(onboardLED, LOW); // Turn on LED if no ACK in 30 seconds
  }
}

void sendMessage(String message) {
  String tempMessage = "Sending Message: " + message;
  sendWebSocketMessage(tempMessage);
  Serial.println("Sending Message: " + message);

  LoRa.beginPacket();
  LoRa.print(message);
  LoRa.endPacket();

  Serial.println("Message Sent: " + message);
}

bool waitForAck(unsigned long timeout) {
  unsigned long startTime = millis();
  sendWebSocketMessage("Waiting for ACK");
  while (millis() - startTime < timeout) {
    int packetSize = LoRa.parsePacket();
    if (packetSize) {
      String receivedMessage = "";
      while (LoRa.available()) {
        receivedMessage += (char)LoRa.read();
      }
      latestRSSI = String(LoRa.packetRssi()); // Update RSSI
      if (receivedMessage == "ACK") {
        Serial.println("ACK received");
        sendWebSocketMessage("ACK received");
        return true; // ACK received
      } else {
        Serial.print("Received other than ACK: ");
        Serial.println(receivedMessage);
        sendWebSocketMessage("Received other than ACK: "+receivedMessage);
      }
    }
    yield(); // This will allow the system to do background tasks
  }
  Serial.println("ACK not received");
  sendWebSocketMessage("ACK not received");
  return false; // Timeout
}

void blinkLED() {
  digitalWrite(onboardLED, LOW);  // Turn LED ON
  delay(1000);
  digitalWrite(onboardLED, HIGH); // Turn LED OFF
}

void sendWebSocketMessage(String message) {
  webSocket.broadcastTXT(message);
}

// Handle WebSocket events
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.printf("Client %d disconnected\n", num);
      break;
    case WStype_CONNECTED:
      Serial.printf("Client %d connected\n", num);
      break;
    case WStype_TEXT:
      Serial.printf("Received message from client %d: %s\n", num, payload);
      break;
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
        background-color: #121212;
        color: #ffffff;
        font-family: Arial, sans-serif;
      }
      #messages {
        background-color: #1e1e1e;
        color: #00ff00;
        padding: 10px;
        margin: 20px auto;
        width: 90%;
        height: 300px;
        overflow-y: auto;
        white-space: pre-wrap;
        border-radius: 5px;
      }
    </style>
    <script>
      var socket = new WebSocket("ws://" + location.hostname + ":81/");
      socket.onmessage = function(event) {
        const messageDiv = document.getElementById("messages");
        console.info("New Message: " + event.data);
        const newMessage = document.createElement("p");
        newMessage.textContent = event.data;
        messageDiv.appendChild(newMessage);
      };

      socket.onerror = function(error) {
        const messageDiv = document.getElementById("messages");
        const newMessage = document.createElement("p");
        newMessage.textContent = error;
        messageDiv.appendChild("Error:" + newMessage);
      };
    </script>
  </head>
  <body>
    <h1>LoRa Transmitter Dashboard</h1>
    <div id="messages">Waiting for data...</div>
  </body>
  </html>
  )rawliteral";
  server.send(200, "text/html", html);
}
