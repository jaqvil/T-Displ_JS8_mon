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
    int offset;
    int snr;
    String text;
    unsigned long timestamp;
};

const int maxNrMessages = 30;
LastMessage lastMessages[maxNrMessages + 1];
int currentMsgIndex = 0;
int totalMessages = 0;

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

  // Setup buttons for next and previous message
  pinMode(0, INPUT_PULLUP); // Button for next message
  pinMode(35, INPUT_PULLUP); // Button for previous message
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
  unsigned long messageAge = currentTime - lastMessages[currentMsgIndex].timestamp;
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
    if (lastMessages[currentMsgIndex].timestamp == 0) return;

    setBrightness();

    // Clear the screen
    tft.fillRect(0, 0, tft.width(), tft.height(), TFT_BLACK);
    
    tft.setCursor(tft.width()-60, 0);
    tft.printf("id:%i", currentMsgIndex+1);
    // Draw from text left aligned
    tft.setCursor(0, 0);
    tft.printf("%s > %s\n", lastMessages[currentMsgIndex].from.c_str(), lastMessages[currentMsgIndex].to.c_str());
    tft.print("SNR: ");
    tft.print(lastMessages[currentMsgIndex].snr);
    tft.print(" @");
    tft.print(lastMessages[currentMsgIndex].offset);
    tft.print("\n");
    tft.print(lastMessages[currentMsgIndex].text);


}

void nextMessage() {
    if (totalMessages > 0) {
        currentMsgIndex = (currentMsgIndex + 1) % maxNrMessages;
        displayMessage();
    }
}

void prevMessage() {
    if (totalMessages > 0) {
        currentMsgIndex = (currentMsgIndex - 1 + maxNrMessages) % maxNrMessages;
        displayMessage();
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
                        // Store message in lastMessages array
                        lastMessages[totalMessages % maxNrMessages] = {
                            params["FROM"] | "N/A",
                            params["TO"] | "N/A",
                            params["OFFSET"] | -1,
                            params["SNR"] | -1,
                            params["TEXT"] | "N/A",
                            millis()
                        };
                        totalMessages++;
                        currentMsgIndex = (totalMessages - 1) % maxNrMessages;
                        //Serial.printf("totalMessages: %i\tcurrentMsgIndex: %i\n", totalMessages, currentMsgIndex);

                        displayMessage();
                    }
                }
            }
        }
        
        // Update display periodically and refresh clock
        static unsigned long lastUpdate = 0;
        if (millis() - lastUpdate >= 1000) {  // Update every second
            lastUpdate = millis();
            setBrightness();  // dim the display as the message ages
            
            // Display date and time in a subtler color
            // tft.setTextColor(TFT_NAVY, TFT_BLACK);
            tft.setCursor(0, tft.height() - 20);
            tft.printf("%s", getCurrentDateTime().c_str());
        }

        // Check button states for next and previous message
        if (digitalRead(35) == LOW) {
            nextMessage();
            delay(270); // Debounce delay
        }
        if (digitalRead(0) == LOW) {
            prevMessage();
            delay(270); // Debounce delay
        }
    } else {
        Serial.println("Disconnected from server, attempting to reconnect...");
        delay(5000);
        if (client.connect(host, port)) {
            Serial.println("Reconnected to server");
        }
    }
    
    delay(5);  // Small delay to prevent loop from running too fast
}
