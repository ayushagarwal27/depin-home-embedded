#include "pins_arduino.h"
#include <Arduino.h>
#include <WiFiManager.h> // tzapu WiFiManager library
#include <HTTPClient.h>
#include <DHT.h>

// Your Solana wallet (Base58 format)
const String PUBLIC_KEY_BASE58 = "AHYic562KhgtAEkb1rSesqS87dFYRcfXb4WwWus3Zc9C";

// -------------------- DHT SETUP --------------------
#define DHTPIN 26 // GPIO for DHT11
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// -------------------- SERVER --------------------
const char *serverUrl = "http://<YOUR_SERVER_IP>:3000/api/data"; // change this!

// -------------------- FUNCTIONS --------------------
void connectWiFiManager();
void readAndSendTemperature();

// Time tracking variables
struct tm timeinfo;
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;     // Adjust for your timezone (0 for UTC)
const int daylightOffset_sec = 0; // Adjust for daylight saving time

// Schedule configuration (24-hour format)
const int MORNING_HOUR = 8;    // 8:00 AM
const int AFTERNOON_HOUR = 14; // 2:00 PM
const int NIGHT_HOUR = 20;     // 8:00 PM

// State tracking
bool morningDone = false;
bool afternoonDone = false;
bool nightDone = false;
int lastDay = -1;

void connectToWiFi();
void setupTime();
void checkAndSendTemp();
void readAndSendTemperature(); // Forward declaration added
void transferToVault();
void setTemp(float temperature);
void resetDailyFlags();

void setup()
{
  // Initialize serial communication for debugging
  Serial.begin(115200);

  // Wait a moment for serial to initialize
  delay(1000);

  // Print startup message
  Serial.println("Booting...");
  Serial.println("Built-in LED will start blinking...");

  setupTime();

  // Configure the LED pin as an output
  pinMode(2, OUTPUT);

  // Initialize DHT sensor
  dht.begin();
  delay(1000);

  // Start WiFiManager (AutoConnect portal if not connected)
  connectWiFiManager();

  // getSolBalance();
  Serial.println("Setup complete.");
}

void loop()
{
  // Turn the LED on
  digitalWrite(2, HIGH);
  Serial.println("LED ON");

  // Wait for 1 second
  delay(1000);

  // Turn the LED off
  digitalWrite(2, LOW);
  Serial.println("LED OFF");

  if (WiFi.status() == WL_CONNECTED)
  {
    // Check if it's time to send temperature
    checkAndSendTemp();
  }
  else
  {
    Serial.println("WiFi lost! Re-entering WiFiManager...");
    connectWiFiManager();
  }

  // Wait for 10 seconds before next check
  delay(10000); // Check every 10 seconds instead of every 2 seconds
}

// -------------------- WIFI MANAGER --------------------
void connectWiFiManager()
{
  WiFiManager wm;

  // Try to connect, else start AP named "ESP32-Setup"
  if (!wm.autoConnect("ESP32-Setup"))
  {
    Serial.println("Failed to connect and hit timeout");
    ESP.restart(); // reboot if failed
  }

  Serial.println("WiFi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

void setupTime()
{
  // Configure time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("Waiting for time synchronization...");

  while (!getLocalTime(&timeinfo))
  {
    delay(1000);
    Serial.print(".");
  }

  Serial.println("\nTime synchronized!");
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");

  // Initialize lastDay
  lastDay = timeinfo.tm_mday;
}

void checkAndSendTemp()
{
  // // Get current time
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
    return;
  }

  int currentHour = timeinfo.tm_hour;
  int currentDay = timeinfo.tm_mday;

  // // Check if it's a new day - reset flags
  if (currentDay != lastDay)
  {
    resetDailyFlags();
    lastDay = currentDay;
    Serial.println("New day detected - resetting temperature reading flags");
  }

  // // Check for morning reading (8:00 AM)
  if (currentHour >= MORNING_HOUR && !morningDone)
  {
    Serial.println("=== MORNING TEMPERATURE READING ===");
    readAndSendTemperature();
    morningDone = true;
  }
  // Check for afternoon reading (2:00 PM)
  else if (currentHour >= AFTERNOON_HOUR && !afternoonDone)
  {
    Serial.println("=== AFTERNOON TEMPERATURE READING ===");
    readAndSendTemperature();
    afternoonDone = true;
  }
  // Check for night reading (8:00 PM)
  else if (currentHour >= NIGHT_HOUR && !nightDone)
  {
    Serial.println("=== NIGHT TEMPERATURE READING ===");
    nightDone = true;
  }
  readAndSendTemperature();

  // Print current status
  Serial.print("Current time: ");
  Serial.println(&timeinfo, "%H:%M:%S");
  Serial.print("Readings today - Morning: ");
  Serial.print(morningDone ? "✓" : "✗");
  Serial.print(", Afternoon: ");
  Serial.print(afternoonDone ? "✓" : "✗");
  Serial.print(", Night: ");
  Serial.println(nightDone ? "✓" : "✗");
}

void readAndSendTemperature()
{
  // Reading temperature or humidity takes about 250 milliseconds!
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  float f = dht.readTemperature(true);

  Serial.println(h);
  Serial.println(t);
  Serial.println(f);

  // Check if any reads failed
  if (isnan(h) || isnan(t) || isnan(f))
  {
    Serial.println(F("Failed to read from DHT sensor!"));
    return;
  }

  // Compute heat index
  float hif = dht.computeHeatIndex(f, h);
  float hic = dht.computeHeatIndex(t, h, false);

  Serial.print(F("Humidity: "));
  Serial.print(h);
  Serial.print(F("%  Temperature: "));
  Serial.print(t);
  Serial.print(F("°C "));
  Serial.print(f);
  Serial.print(F("°F  Heat index: "));
  Serial.print(hic);
  Serial.print(F("°C "));
  Serial.print(hif);
  Serial.println(F("°F"));

  // Send temperature to backend
  // Build JSON payload
  String jsonPayload = "{\"temp\":" + String(t) + ",\"humidity\":" + String(h) + "}";

  HTTPClient http;
  http.begin(serverUrl);
  http.addHeader("Content-Type", "application/json");

  int httpResponseCode = http.POST(jsonPayload);

  if (httpResponseCode > 0)
  {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    Serial.println("Response: " + http.getString());
  }
  else
  {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
  }

  http.end();

  Serial.print("Temperature ");
  Serial.print(t);
  Serial.println("°C sent to backend!");
}

void resetDailyFlags()
{
  morningDone = false;
  afternoonDone = false;
  nightDone = false;
}
