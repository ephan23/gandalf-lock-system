#include <Servo.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WebServer.h>

const char* ssid = "xxxxx";
const char* password = "xxxxx";

/* Power Automate flow endpoint */
String ENDPOINT_URL = "https://prod.westus.logic.azure.com:443/workflows/xxxxxx";

 #define SERVO_PIN 32
 #define YELLOW_STANDBY_PIN 2
 #define RED_DENIED_PIN 13
 #define GREEN_GRANTED_PIN 15
 
#define RST_PIN 17
#define SS_PIN 33

Servo lock;
MFRC522 rfid (SS_PIN, RST_PIN);

WebServer server(80);
StaticJsonDocument<250> jsonDoc;

void initalStart() {
  pinMode(YELLOW_STANDBY_PIN, OUTPUT); 
  pinMode(RED_DENIED_PIN, OUTPUT);
  pinMode(GREEN_GRANTED_PIN, OUTPUT);
  lock.attach(SERVO_PIN);

  lock.write(0);
  digitalWrite(RED_DENIED_PIN, LOW);
  digitalWrite(GREEN_GRANTED_PIN, LOW);
  digitalWrite(YELLOW_STANDBY_PIN, HIGH);
}

void grantAccess() {
  /* Turn off standby LED and turn on granted LED */
  digitalWrite(YELLOW_STANDBY_PIN, LOW);
  digitalWrite(GREEN_GRANTED_PIN, HIGH);
  lock.write(180);
  delay(2000);

  /* Turn off granted LED, reset lock position, and turn on standby LED */
  digitalWrite(YELLOW_STANDBY_PIN, HIGH);
  digitalWrite(GREEN_GRANTED_PIN, LOW);
  lock.write(0);
  delay(2000);
}

void denyAccess() {
  digitalWrite(YELLOW_STANDBY_PIN, LOW);
  digitalWrite(RED_DENIED_PIN, HIGH);
  delay(3000);
  digitalWrite(YELLOW_STANDBY_PIN, HIGH);
  digitalWrite(RED_DENIED_PIN, LOW);
}

/* When server receives POST request, handle action and response */
void handleUnlock() {
  Serial.println("Received POST request.");
  if (server.hasArg("plain") == false) {
  }
  String body = server.arg("plain");
  deserializeJson(jsonDoc, body);
  
  if (jsonDoc["unlock"] == true) {
    server.send(200, "application/json", "{\"success\":\"true\"}");
    grantAccess();
  }
}

/* Create routing for HTTP server */
void setupRouting() {
  server.on("/lock", HTTP_POST, handleUnlock);
  server.begin();
}

void setup() {
  Serial.begin(9600);
  initalStart();
  
  /* Establish WiFi connection */
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.print("Connected. IP address: ");
  Serial.println(WiFi.localIP());

  /* Initialize RFID reader */
  pinMode(RST_PIN, OUTPUT);
  digitalWrite(RST_PIN, HIGH);
  SPI.begin(25, 27, 26);
  rfid.PCD_Init();
  delay(10);
  rfid.PCD_DumpVersionToSerial();

  setupRouting();
}

void loop() {
server.handleClient();

if ( ! rfid.PICC_IsNewCardPresent()) {
  return;
}
/* Detect read card */
if ( ! rfid.PICC_ReadCardSerial()) {
    return;
}

// Show UID on serial monitor
String content = "";
for (byte i=0; i < rfid.uid.size; i++) {
  content.concat(String(rfid.uid.uidByte[i] < 0x10 ? " 0" : " "));
  content.concat(String(rfid.uid.uidByte[i], HEX));
  }

/* Normalize and remove spaces from UID */
content.toUpperCase();
content.replace(" ", "");
Serial.println(content);

if (WiFi.status() == WL_CONNECTED) {
  HTTPClient http;
  DynamicJsonDocument doc(1024);

  /* Send POST containing RFID data to HTTP Endpoint URL */
  http.begin(ENDPOINT_URL);
  http.addHeader("Content-Type", "application/json");
  int httpResponseCode = http.POST("{\"RFIDTag\":\"" + content + "\"}");
  
  /* Receive and decode response */
  String response = http.getString();
  deserializeJson(doc, response);

  const char* message = doc["message"];

  /* Debug UID and HTTP response */
  Serial.println(message);
  Serial.println(httpResponseCode);
  
  switch(httpResponseCode) {
    case 200 : {
      grantAccess();
      break;
    }
    case 406 : {
      denyAccess();
    }
  }
}
}