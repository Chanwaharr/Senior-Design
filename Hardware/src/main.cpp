#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <TinyGPS++.h>
#include <WiFi.h>
#include <ESP32Firebase.h>

#define WIFI_SSID  "Eddie"
#define WIFI_PASSWORD "Mopp3tt!"
#define API_KEY "AIzaSyCs7OKUVsWg3hRFSYSiHQGc5xeTp1RyTv8"
#define DATABASE_URL "https://urban-hotspots-1-default-rtdb.firebaseio.com/"
 
Firebase firebase(DATABASE_URL);
 
unsigned long sendDataPrevMillis = 0;

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define OLED_RESET 4
 
#define RXD2 3
#define TXD2 1

HardwareSerial neogps(1);
TinyGPSPlus gps;
 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
 
static const unsigned char PROGMEM logo_bmp[] =
{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};
 
DHT dht(35, DHT22); //This is for the temp/humidity sensor
const int CS_PIN = 5; //This is for the SD card
const int SENSOR_PIN = 39; // This if for the light sensor
const int SOUND_PIN = 36;
const int VOLTAGE_DIVIDER = 34;
 
File myFile;
const char* fileName = "/test.txt"; // File name
 
 
// Constants for sound sensor sensitivity and reference voltage
const float SOUND_SENSITIVITY = 0.1; 
const float REFERENCE_VOLTAGE = 5.0; 
 
// Function to convert sound intensity (analog reading) to decibels
float convertToDecibels(float soundVoltage) {
  
  float voltageInMillivolts = soundVoltage * (5000.0 / 1023.0); // Convert to millivolts
  float soundPressureLevel = voltageInMillivolts / SOUND_SENSITIVITY; // Convert to sound pressure level
  // Convert SPL to decibels (dB)
  return 20 * log10(soundPressureLevel / REFERENCE_VOLTAGE);
}
 
// Function to convert light frequency (Hz) to Lux
float convertToLux(float lightHz) {
  float slope = 10.0; // Example slope, adjust based on calibration
  float intercept = 50.0; // Example intercept, adjust based on calibration
  return slope * lightHz + intercept;
}

void initializeCard() {
  Serial.print("Beginning initialization of SD card: ");
   while(true) {
    if (SD.begin(CS_PIN)) {
      Serial.println("Initialization done.");
      break; // Exit the loop once the SD card initializes successfully
    } else {
      Serial.println("Initialization failed, retrying...");
      delay(1000); // Wait for a bit before retrying
    }
  }
 
 
}
 
void writeHeader() {
  myFile = SD.open(fileName, FILE_WRITE);
  if (myFile) {
    myFile.println("Lat,Long, Time, Light, Temp, Humidity, Sound");
    myFile.close();
  } else {
    Serial.println("Error opening " + String(fileName));
  }
}

void connectToWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("Attempting to connect to WiFi: ");
  if(WiFi.status() == WL_CONNECTED){
    Serial.println("WiFi connected");
  }
  else{
    Serial.println("WiFi connection failed");
  }
}
 
void setup() {
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.display();
  delay(2000);

  Serial.begin(115200);

  // Attempt to connect to WiFi, but don't block the rest of the code if it fails
  connectToWiFi();

  neogps.begin(9600, SERIAL_8N1, RXD2, TXD2);
  dht.begin();
  delay(1000);
  initializeCard();

  if (SD.exists(fileName)) {
    Serial.println("\nFile exists. Will append to it.\n");
  } else {
    writeHeader(); // Only write header if file does not exist
  }
}

void logSensorDataToSD() {
  // Logic for reading sensor data and logging to the SD card
  int lightLux = analogRead(SENSOR_PIN); 
  float lightHz = lightLux / 1000.0;
  float temperature = dht.readTemperature(true);
  float humidity = dht.readHumidity();
  float soundVoltage = analogRead(SOUND_PIN) * (5.0 / 1023.0); 
  float sounddB = convertToDecibels(soundVoltage);

  boolean newData = false;
  for (unsigned long start = millis(); millis() - start < 1000;) {
    while (neogps.available()) {
      if (gps.encode(neogps.read())) {
        newData = true;
      }
    }
  }

  float latitude = gps.location.isValid() ? gps.location.lat() : 0;
  float longitude = gps.location.isValid() ? gps.location.lng() : 0;

  // SD Card logging
  myFile = SD.open(fileName, FILE_WRITE);
  if (myFile && newData) {
    myFile.seek(myFile.size());
    myFile.print(millis());
    if (gps.location.isValid()) {
      myFile.print(",");
      myFile.print(latitude, 6);
      myFile.print(",");
      myFile.println(longitude, 6);
    } else {
      myFile.println(",No GPS Data");
    }
    myFile.print(",");
    myFile.print(lightLux);
    myFile.print(",");
    myFile.print(temperature, 2);
    myFile.print(",");
    myFile.print(humidity, 2);
    myFile.print(",");
    myFile.print(sounddB);
    myFile.close();
  } else if (!myFile) {
    Serial.println("Error opening " + String(fileName));
  }
}

void sendDataToFirebase() {
  // Logic for sending data to Firebase
  float temperature = dht.readTemperature(true);
  float humidity = dht.readHumidity();
  float latitude = gps.location.isValid() ? gps.location.lat() : 0;
  float longitude = gps.location.isValid() ? gps.location.lng() : 0;

  firebase.setFloat("Environment/Temperature", temperature);
  firebase.setFloat("Environment/Humidity", humidity);
  firebase.setFloat("GPS/Latitude", latitude);
  firebase.setFloat("GPS/Longitude", longitude);
}

void updateDisplay() {
  // Read sensor data
  int BatLife = analogRead(VOLTAGE_DIVIDER);
  float temperature = dht.readTemperature(true);
  float humidity = dht.readHumidity();

  // Begin by clearing the display
  display.clearDisplay();
  display.setTextSize(1);      // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE); // Draw white text
  display.setCursor(0, 0);     // Start at top-left corner

  // Determine battery level in quarters
  if (BatLife >= 2445) {
    display.println("Battery level: 76% - 100%");
  } else if (BatLife >= 2283) {
    display.println("Battery level: 51% - 75%");
  } else if (BatLife >= 2120) {
    display.println("Battery level: 26% - 50%");
  } else {
    display.println("Battery level: 0% - 25%");
  }

  // Print time since start
  display.print("Time: ");
  display.println(millis() / 1000);

  // Print temperature
  display.print("Temp: ");
  display.print(temperature, 1);
  display.println(" F");

  // Print humidity
  display.print("Humidity: ");
  display.print(humidity, 1);
  display.println("%");

  // Display the content
  display.display();
}

void loop() {
  static unsigned long lastReconnectAttempt = 0;

  // Check if enough time has passed to log data
  if (millis() - sendDataPrevMillis > 5000 || sendDataPrevMillis == 0) {
    sendDataPrevMillis = millis(); // Update the last sent time

    // Log data from sensors to SD card
    logSensorDataToSD();

    // Update the OLED display with the latest sensor data
    updateDisplay();

    // Check WiFi status
    if (WiFi.status() == WL_CONNECTED) {
      // If WiFi is connected, send data to Firebase
      sendDataToFirebase();
    } else {
      // If WiFi is not connected, attempt to reconnect periodically
      if (millis() - lastReconnectAttempt > 5000) { // Attempt to reconnect every 10 seconds
        lastReconnectAttempt = millis();
        connectToWiFi();
      }
    }
  }
}