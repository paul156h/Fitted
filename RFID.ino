/*
  UNO R4 WiFi + MFRC522 -> send UID to server 
  - Uses WiFiS3 library for UNO R4 WiFi
  - Uses MFRC522 library for RC522 reader (SPI)
  - Sends JSON POST: { "uid": "AA:BB:CC:DD" } to http://ip
*/

// Include SPI (for RC522) and MFRC522
#include <SPI.h>
#include <MFRC522.h>

// Include WiFiS3 for UNO R4 WiFi networking
#include <WiFiS3.h>

// ---- Configuration ----
const char SSID[] = "SSID";      // replace with your WiFi SSID
const char PASS[] = "PASS";  // replace with your WiFi password

// Server configuration
const char SERVER_IP[] = "IP";   // server IP
const uint16_t SERVER_PORT = 80;           // HTTP port
const char SERVER_PATH[] = "/rfid";        // endpoint path to POST to

// MFRC522 pins (change if you wired different pins)
const uint8_t RDR_CS_PIN = 10; // SDA (SS) pin for RC522
const uint8_t RDR_RST_PIN = 9; // RST pin for RC522

// Instantiate MFRC522
MFRC522 mfrc522(RDR_CS_PIN, RDR_RST_PIN);  // Create MFRC522 instance

// Buffer to hold last seen UID to avoid duplicate posts
String lastUID = "";

// WiFi client
WiFiClient client;

void setup() {
  // Start serial for debugging
  Serial.begin(115200);
  // Wait a short while for serial
  delay(100);

  // Initialize SPI bus
  SPI.begin();
  // Initialize RC522
  mfrc522.PCD_Init();

  // Initialize WiFi module
  if (WiFi.status() == WL_NO_MODULE) {
    // Print and halt if WiFi module not found
    Serial.println("WiFi module not found. Check board/package.");
    while (true) {
      delay(500);
    }
  }

  // Attempt to connect to WiFi
  connectToWiFi();
}

void loop() {
  // Look for new cards
  if (!mfrc522.PICC_IsNewCardPresent()) {
    // No new card present, short delay
    delay(50);
    return;
  }

  // Select one of the cards
  if (!mfrc522.PICC_ReadCardSerial()) {
    // Couldn't read card serial, skip
    delay(50);
    return;
  }

  // Build UID string (hex bytes separated with colons)
  String uidStr = uidToString(mfrc522.uid.uidByte, mfrc522.uid.size);

  // Debug print
  Serial.print("Card UID: ");
  Serial.println(uidStr);

  // If UID same as last seen, ignore (simple debounce)
  if (uidStr == lastUID) {
    Serial.println("UID same as last read — ignoring to avoid duplicate POST.");
    // Halt PICC so it can be read again later
    mfrc522.PICC_HaltA();
    delay(200);
    return;
  }

  // Try to send UID to server
  bool sent = sendUidToServer(uidStr);

  if (sent) {
    // Store as lastUID only if successfully sent
    lastUID = uidStr;
    Serial.println("UID sent and stored as lastUID.");
  } else {
    Serial.println("Failed to send UID to server.");
  }

  // Halt PICC (recommended)
  mfrc522.PICC_HaltA();
  delay(200);
}

// ---------------- Helper functions ----------------

// Connect to the WiFi network (blocks until connected)
void connectToWiFi() {
  Serial.print("Connecting to WiFi SSID: ");
  Serial.println(SSID);

  int status = WL_IDLE_STATUS;
  while (status != WL_CONNECTED) {
    status = WiFi.begin(SSID, PASS);
    Serial.print("Attempting connection...");
    delay(5000);
    Serial.println(WiFi.status());
  }

  Serial.println("WiFi connected.");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Signal strength (RSSI): ");
  Serial.println(WiFi.RSSI());
}


String uidToString(byte *uidBytes, byte uidSize) {
  String s = "";
  for (byte i = 0; i < uidSize; i++) {
    if (uidBytes[i] < 0x10) s += "0"; // leading zero
    s += String(uidBytes[i], HEX);
    if (i != uidSize - 1) s += ":";
  }
  s.toUpperCase();
  return s;
}

// Send UID to server as JSON via simple HTTP POST
bool sendUidToServer(const String &uid) {
  // Ensure WiFi connected, attempt reconnect otherwise
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected. Attempting reconnect...");
    connectToWiFi();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Reconnect failed.");
      return false;
    }
  }

  // Prepare JSON payload
  String payload = "{\"uid\":\"" + uid + "\"}";

  // Attempt to connect to server IP and port
  Serial.print("Connecting to ");
  Serial.print(SERVER_IP);
  Serial.print(":");
  Serial.println(SERVER_PORT);

  if (!client.connect(SERVER_IP, SERVER_PORT)) {
    Serial.println("Connection to server failed.");
    return false;
  }

  // Build HTTP POST request
  String req = "";
  req += "POST ";
  req += SERVER_PATH;
  req += " HTTP/1.1\r\n";
  req += "Host: ";
  req += SERVER_IP;
  req += "\r\n";
  req += "Content-Type: application/json\r\n";
  req += "Content-Length: ";
  req += String(payload.length());
  req += "\r\n";
  req += "Connection: close\r\n";
  req += "\r\n";
  req += payload;

  // Send request
  client.print(req);

  // Read response status line (with timeout)
  unsigned long timeout = millis();
  while (!client.available()) {
    if (millis() - timeout > 5000) {
      Serial.println("No response from server (timeout).");
      client.stop();
      return false;
    }
    delay(10);
  }

  // Read first line of response (status)
  String statusLine = client.readStringUntil('\n');
  statusLine.trim();
  Serial.print("Server response status: ");
  Serial.println(statusLine);

  // Simple success check: HTTP/1.1 200 or 201
  bool ok = (statusLine.indexOf("200") >= 0) || (statusLine.indexOf("201") >= 0);

  // Optionally print rest of response (debug)
  while (client.available()) {
    String line = client.readStringUntil('\n');
    Serial.println(line);
  }

  // Close connection
  client.stop();
  return ok;
}
