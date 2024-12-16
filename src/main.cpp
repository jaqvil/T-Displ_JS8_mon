#include <my_config.h>  // my private settings i.e. WiFi credentials
#include <Arduino.h>
#include <WiFi.h>
#include <TFT_eSPI.h>
#include <ArduinoJson.h>
#include <WiFiClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include "esp_system.h"
#include "esp_spi_flash.h"

#ifndef CONFIG_H
#define CONFIG_H

// WiFi credentials
const char* ssid = "your_ssid";
const char* password = "your_password";

// PC running JS8Call app details
const char* host = "your_pc_running_the_JS8Call_app";
const int port = 42442; // Default port for JS8Call

#endif

TFT_eSPI tft = TFT_eSPI();  // Invoke custom library

WiFiClient client;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000);  // Update every 60 seconds

struct LastMessage {
    String from;
    String to;
    int snr;
    String text;
    unsigned long timestamp;
} lastMsg;

// Function to get the short string representation of the month
const char* monthShortStr(int month) {
    const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    return months[month - 1];
}

// Add this function to get the current date and time
String getCurrentDateTime() {
    timeClient.update();
    unsigned long epochTime = timeClient.getEpochTime();
    struct tm *ptm = gmtime ((time_t *)&epochTime);
    
    char dateBuffer[11];
    char timeBuffer[6];
    snprintf(dateBuffer, sizeof(dateBuffer), "%02d-%s", ptm->tm_mday, monthShortStr(ptm->tm_mon + 1));
    snprintf(timeBuffer, sizeof(timeBuffer), "%02d:%02d", ptm->tm_hour, ptm->tm_min);
    return String(dateBuffer) + " " + String(timeBuffer);
}

void setup() {
  Serial.begin(115200);
  
  // Initialize TFT
  tft.init();
  tft.setRotation(1);  // Adjust based on your display orientation
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(0, 0);
  
  //for backlight control
  ledcSetup(0, 5000, 8); // channel 0, 5000 Hz, 8-bit resolution
  ledcAttachPin(4, 0);   // attach pin 4 to channel 0

  tft.println("Connecting to WiFi...");

  // Connect to WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  tft.println("WiFi Connected!");

  // Connect to server
  if (!client.connect(host, port)) {
    Serial.println("Connection failed.");
    tft.println("Connection failed.");
    return;
  } else {
    Serial.print("Connected to "); Serial.println(host);
    tft.print("Connected to "); tft.println(host);
  }

  // Initialize NTP client
  timeClient.begin();
}

// Replace calculateBrightness with these color functions
uint16_t dimColor(uint8_t brightness) {
    // Convert 0-255 brightness to 0-31 for 5-bit color components
    uint8_t r = (brightness >> 3);
    uint8_t g = (brightness >> 2);
    uint8_t b = (brightness >> 3);
    
    // Combine into 16-bit color (RGB565 format)
    return (r << 11) | (g << 5) | b;
}


void setBrightness()
{
  // Calculate brightness based on time elapsed
  unsigned long currentTime = millis();
  unsigned long messageAge = currentTime - lastMsg.timestamp;
  uint8_t brightness = 220;
  if (messageAge < 30000) { 
    // 30 seconds
    brightness = 220 - (messageAge * 210 / 30000);
  } else{
    brightness = 10;
  }

  // Set text color based on brightness
  ledcWrite(0, brightness); // Set brightness level (pwm'ing the backlight)
}


void displayMessage() {
    if (lastMsg.timestamp == 0) return;

    setBrightness();

    // Get display width
    int16_t width = tft.width();
    
    // Calculate positions
    String arrow = "->";
    int16_t arrowWidth = tft.textWidth(arrow);
    int16_t arrowX = (width - arrowWidth) / 2;
    
    // Clear the screen
    tft.fillRect(0, 0, tft.width(), tft.height(), TFT_BLACK);
    
    // Draw from text left aligned
    tft.setCursor(0, 0);
    tft.print(lastMsg.from);
    
    // Draw arrow in center
    tft.setCursor(arrowX, 0);
    tft.print(arrow);
    
    // Draw to text right aligned
    int16_t toWidth = tft.textWidth(lastMsg.to);
    tft.setCursor(width - toWidth, 0);
    tft.printf("%s\n", lastMsg.to.c_str());
    
    tft.printf("SNR  : %d\n", lastMsg.snr);
    tft.printf("%s\n", lastMsg.text.c_str());

    // Display date and time in a subtler color
    // tft.setTextColor(TFT_NAVY, TFT_BLACK);
    tft.setCursor(0, tft.height() - 20);
    tft.printf("%s", getCurrentDateTime().c_str());

}

void printDebugInfo() {
    static unsigned long lastDebugPrint = 0;
    if (millis() - lastDebugPrint >= 5000) { // Print every 5 seconds
        lastDebugPrint = millis();
        
        Serial.printf("\n=== Debug Info ===\n");
        Serial.printf("Uptime: %lu ms\n", millis());
        Serial.printf("Free Heap: %u bytes\n", ESP.getFreeHeap());
        Serial.printf("CPU Freq: %u MHz\n", ESP.getCpuFreqMHz());
        Serial.printf("CPU Temp: %.2f °C\n", (float)(temperatureRead() - 32) / 1.8);
        Serial.printf("================\n");
    }
}

void loop() {
    if (client.connected()) {
        while (client.available()) {
            String line = client.readStringUntil('\n');
            if (line.length() > 0) {
                // Serial.println("----DEBUG----");
                // Serial.println(line);
                // Serial.println("-------------");
                DynamicJsonDocument doc(2048);  // Adjust size based on your JSON complexity
                DeserializationError error = deserializeJson(doc, line);

                if (error) {
                    Serial.print(F("deserializeJson() failed: "));
                    Serial.println(error.c_str());
                    return;
                }

                // Check if the JSON matches our expected format
                if (doc["type"] == "RX.DIRECTED") {
                    JsonObject params = doc["params"];
                    if (!params.isNull()) {
                        // Store message in lastMsg structure
                        lastMsg.from = params["FROM"] | "N/A";
                        lastMsg.to = params["TO"] | "N/A";
                        lastMsg.snr = params["SNR"] | -1;
                        lastMsg.text = params["TEXT"] | "N/A";
                        lastMsg.timestamp = millis();
                        
                        displayMessage();
                    }
                }
            }
        }
        
        // Update display periodically
        static unsigned long lastUpdate = 0;
        if (millis() - lastUpdate >= 1000) {  // Update every second
            lastUpdate = millis();
            setBrightness();  // dim the display as the message ages
        }
    } else {
        Serial.println("Disconnected from server, attempting to reconnect...");
        delay(5000);
        if (client.connect(host, port)) {
            Serial.println("Reconnected to server");
        }
    }
    printDebugInfo();  // Add before final delay
    delay(100);  // Small delay to prevent loop from running too fast
}