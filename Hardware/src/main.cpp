#include <Arduino.h>
#include <HardwareSerial.h>
#include <SD.h>
#include <SPI.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <TinyGPS++.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP32Firebase.h>
#include <TimeLib.h>
#include <Secrets.h> //the API's and passwords

Firebase firebase(FIREBASE_DATABASE_URL);

// Google Geolocation API credentials
const char* Host = "www.googleapis.com";
String apiKey =  GOOGLE_API_KEY;

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET 4

static const int RXPin = 16;
static const int TXPin = 17;

TinyGPSPlus gps;
HardwareSerial GPSSerial(1);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

DHT dht(13, DHT22); // Temperature/humidity sensor
const int CS_PIN = 5; // SD card pin
const int SENSOR_PIN = 39; // Light sensor
const int SOUND_PIN = 36; // Sound sensor
const int VOLTAGE_DIVIDER = 34; // Voltage divider for battery life
const int PEOPLE_COUNT_UP = 27; // Increment push button
const int PEOPLE_COUNT_DOWN = 9; // Decrement push button

File myFile;
String fileName;

// Global latitude and longitude variables
double latitude = 0.0;
double longitude = 0.0;

// Global counter for people count
volatile int PeopleCounter = 0;  // Use volatile because it's modified in an ISR
const unsigned long sensor1Interval = 30000;  // 30 seconds
unsigned long previousMillisSensor1 = 0;    // Store last time sensor1 was read
bool userChangedCounter = false;            // Flag to track if the user changed the PeopleCounter
// Debounce time
const unsigned long debounceDelay = 50;  // 50 milliseconds debounce delay
volatile unsigned long lastInterruptTime = 0;  // Last time an interrupt occurred

// Flags for debouncing using interrupts
volatile bool button1Pressed = false;
volatile bool button2Pressed = false;

// Function to handle button 1 press (increment)
void IRAM_ATTR handleButton1Press() {
  unsigned long interruptTime = millis();
  if (interruptTime - lastInterruptTime > debounceDelay) {
    button1Pressed = true;  // Set the flag
    lastInterruptTime = interruptTime;
  }
}

// Function to handle button 2 press (decrement)
void IRAM_ATTR handleButton2Press() {
  unsigned long interruptTime = millis();
  if (interruptTime - lastInterruptTime > debounceDelay) {
    button2Pressed = true;  // Set the flag
    lastInterruptTime = interruptTime;
  }
}

// Convert 24-hour time to 12-hour time with AM/PM
String formatTime12Hour(int hour, int minute, int second) {
  int timeZoneOffset = -5;
  hour += timeZoneOffset;
   // Adjust for wrapping around midnight (24-hour to 12-hour conversion)
  if (hour < 0) {
    hour += 24;  // Wrap around if negative
  } else if (hour >= 24) {
    hour -= 24;  // Wrap around if over 24
  }
  String period = "AM";
  if (hour >= 12) {
    period = "PM";
    if (hour > 12) hour -= 12;  // Convert 24-hour time to 12-hour time
  } else if (hour == 0) {
    hour = 12;  // Midnight should be 12 AM
  }
  
  char timeStr[12];
  sprintf(timeStr, "%02d:%02d:%02d %s", hour, minute, second, period.c_str());
  return String(timeStr);
}

void getLatLongFromGoogle() {
  // Scan for WiFi networks
  int n = WiFi.scanNetworks();

  if (n > 0) {
    // Create JSON with WiFi networks
    String jsonString = "{\"wifiAccessPoints\":[";
    for (int i = 0; i < n; ++i) {
      jsonString += "{";
      jsonString += "\"macAddress\":\"" + WiFi.BSSIDstr(i) + "\",";
      jsonString += "\"signalStrength\":" + String(WiFi.RSSI(i));
      jsonString += "}";
      if (i < n - 1) jsonString += ",";
    }
    jsonString += "]}";

    // Send data to Google Geolocation API
    WiFiClientSecure client;
    client.setInsecure();  // No SSL certificate needed
    if (client.connect(Host, 443)) {
      client.println("POST /geolocation/v1/geolocate?key=" + apiKey + " HTTP/1.1");
      client.println("Host: " + String(Host));
      client.println("Connection: close");
      client.println("Content-Type: application/json");
      client.print("Content-Length: ");
      client.println(jsonString.length());
      client.println();
      client.print(jsonString);
    } else {
      Serial.println("Failed to connect to Google API.");
      return;
    }

    // Read the API response
    String payload = "";
    while (client.connected() || client.available()) {
      if (client.available()) {
        payload += client.readString();
      }
    }
    client.stop();

    // Extract latitude and longitude from the response
    int latIndex = payload.indexOf("\"lat\":");
    int lngIndex = payload.indexOf("\"lng\":");

    if (latIndex != -1 && lngIndex != -1) {
      latitude = payload.substring(latIndex + 6, payload.indexOf(',', latIndex)).toDouble();
      longitude = payload.substring(lngIndex + 6, payload.indexOf(',', lngIndex)).toDouble();
    } else {
      Serial.println("Failed to parse latitude and longitude from Google API.");
    }
  }
}

String generateNewFileName() {
  int fileIndex = 1;
  String newFileName = "/Data" + String(fileIndex) + ".txt";

  while (SD.exists(newFileName)) {
    fileIndex++;
    newFileName = "/Data" + String(fileIndex) + ".txt";
  }

  return newFileName;
}

void writeHeader() {
  myFile = SD.open(fileName, FILE_WRITE);
  if (myFile) {
    myFile.println("Date,Time,Lat,Long,Light,Temp,Humidity,Sound,People");
    myFile.close();
  } else {
    Serial.println("Error opening " + String(fileName));
  }
}

void initializeCard() {
  Serial.print("Beginning initialization of SD card: ");
  while (true) {
    if (SD.begin(CS_PIN)) {
      Serial.println("Initialization done.");

      //Generate new file name if the file exists
      fileName = generateNewFileName();
      writeHeader();
      break;
    } else {
      Serial.println("Initialization failed, retrying...");
      delay(1000);
    }
  }
}

void connectToWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("Attempting to connect to WiFi: ");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected");
  } else {
    Serial.println("WiFi connection failed");
  }
}

void setup() {
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.display();

  Serial.begin(115200);

  // Attempt to connect to WiFi
  connectToWiFi();

  GPSSerial.begin(9600, SERIAL_8N1, RXPin, TXPin);
  dht.begin();
  initializeCard();

  if (SD.exists(fileName)) {
    Serial.println("\nFile exists. Will append to it.\n");
  } else {
    writeHeader();
  }

  // Set up pins and attach interrupts
  pinMode(PEOPLE_COUNT_UP, INPUT);
  pinMode(PEOPLE_COUNT_DOWN, INPUT);
  
  // Attach interrupts to buttons
  attachInterrupt(digitalPinToInterrupt(PEOPLE_COUNT_UP), handleButton1Press, FALLING);
  attachInterrupt(digitalPinToInterrupt(PEOPLE_COUNT_DOWN), handleButton2Press, FALLING);
}

void logSensorDataToSD() {
  int lightLux = analogRead(SENSOR_PIN);
  float lightHz = lightLux / 1000.0;
  float temperature = dht.readTemperature(true);
  float humidity = dht.readHumidity();
  float soundVoltage = analogRead(SOUND_PIN);

  boolean newData = false;
  for (unsigned long start = millis(); millis() - start < 1000;) {
    while (GPSSerial.available()) {
      if (gps.encode(GPSSerial.read())) {
        newData = true;
      }
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    // GPS is not valid, try to get location from Google
    getLatLongFromGoogle();
  } else if (gps.location.isValid()) {
    latitude = gps.location.lat();
    longitude = gps.location.lng();
  } else {
    latitude = 0.0;
    longitude = 0.0;
  }

  // Format time in 12-hour format
  String formattedTime = "No GPS Time";
  if (gps.time.isValid()) {
    int hour = gps.time.hour();
    int minute = gps.time.minute();
    int second = gps.time.second();
    formattedTime = formatTime12Hour(hour, minute, second);
  }

  char dateStr[11] = "00-00-0000";
  if (gps.date.isValid()) {
    sprintf(dateStr, "%02d-%02d-%04d", gps.date.day(), gps.date.month(), gps.date.year());
  }

  myFile = SD.open(fileName, FILE_WRITE);
  if (myFile) {
    myFile.seek(myFile.size());
    myFile.print(dateStr);
    myFile.print(" ");
    myFile.print(formattedTime);

    myFile.print(",");
    myFile.print(latitude, 6);
    myFile.print(",");
    myFile.print(longitude, 6);

    myFile.print(",");
    myFile.print(lightLux);
    myFile.print(",");
    myFile.print(temperature, 2);
    myFile.print(",");
    myFile.print(humidity, 2);
    myFile.print(",");
    myFile.print(soundVoltage);
    myFile.print(",");
    myFile.println(PeopleCounter); // Log PeopleCounter to SD

    myFile.close();
    Serial.println("Data logged to SD card");
  } else {
    Serial.println("Error opening " + String(fileName));
  }
}

void sendDataToFirebase() {
  // Receive the current PeopleCounter from Firebase
  int firebase_count = firebase.getInt("PeopleCounter");

  // Logic for comparing the Firebase and local values
  if (firebase_count != PeopleCounter) {
    if (userChangedCounter) {
      // The user changed the PeopleCounter via buttons, so we update Firebase
      firebase.setInt("PeopleCounter", PeopleCounter);
    } else {
      // No user change, so we sync the local counter to Firebase value
      PeopleCounter = firebase_count;
    }
  }

  // After synchronization, reset the userChangedCounter flag
  userChangedCounter = false;

  // Send other sensor data to Firebase
  float temperature = dht.readTemperature(true);
  float humidity = dht.readHumidity();
  // Update latitude and longitude using either GPS or Google
  if (WiFi.status() == WL_CONNECTED) {
    getLatLongFromGoogle();  // Get from Google if WiFi is connected and GPS is not valid
  } else if (gps.location.isValid()) {
    latitude = gps.location.lat();
    longitude = gps.location.lng();
  }

  String latStr = String(latitude, 6);  // Convert latitude to string with 6 decimal places
  String lngStr = String(longitude, 6); // Convert longitude to string with 6 decimal places
  firebase.setString("GPS/Latitude", latStr);
  firebase.setString("GPS/Longitude", lngStr);
  firebase.setFloat("Environment/Temperature", temperature);
  firebase.setFloat("Environment/Humidity", humidity);
}

void updateDisplay() {
  int BatLife = analogRead(VOLTAGE_DIVIDER);
  float voltage = (BatLife / 4095.0) * 3.3 * 2;
  float temperature = dht.readTemperature(true);
  float humidity = dht.readHumidity();
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);

  if (BatLife >= 4) {
    display.println("Battery level: 76% - 100%");
  } else if (BatLife >= 3.8) {
    display.println("Battery level: 51% - 75%");
  } else if (BatLife >= 3.6) {
    display.println("Battery level: 26% - 50%");
  } else {
    display.println("Battery level: 0% - 25%");
  }

  if (gps.time.isValid()) {
    int hour = gps.time.hour();
    int minute = gps.time.minute();
    int second = gps.time.second();
    String formattedTime = formatTime12Hour(hour, minute, second);
    
    display.print("Time: ");
    display.println(formattedTime);
  } else {
    display.println("Time: No GPS Data");
  }

  display.print("Temp: ");
  display.print(temperature, 1);
  display.println(" F");

  display.print("Humidity: ");
  display.print(humidity, 1);
  display.println("%");
  
  display.print("People: ");
  display.println(PeopleCounter);

  if (WiFi.status() == WL_CONNECTED) {
    display.println("WiFi connected");
  } else {
    display.println("No WiFi");
  }

  display.display();
}

void loop() {
  unsigned long currentMillis = millis();

  // Update display every 2 seconds
  static unsigned long previousMillisDisplay = 0;
  const unsigned long displayInterval = 2000;  // 2 seconds
  if (currentMillis - previousMillisDisplay >= displayInterval) {
    previousMillisDisplay = currentMillis;  // Update last display update time
    updateDisplay();  // Update the OLED display
  }

  // Sensor 1 - Record data every 30 seconds
  if (currentMillis - previousMillisSensor1 >= sensor1Interval) {
    previousMillisSensor1 = currentMillis;  // Update last read time
    logSensorDataToSD();  // Log data to SD card

    // If WiFi is connected, send data to Firebase
    if (WiFi.status() == WL_CONNECTED) {
      sendDataToFirebase();
    } else {
      connectToWiFi();  // Attempt to reconnect to WiFi
    }
  }

  // Handle increment button press
  if (button1Pressed) {
    PeopleCounter++;  // Increment PeopleCounter
    button1Pressed = false;  // Reset the flag
    userChangedCounter = true;  // Set the flag that the user manually changed the counter
  }

  // Handle decrement button press
  if (button2Pressed) {
    PeopleCounter--;  // Decrement PeopleCounter
    button2Pressed = false;  // Reset the flag
    userChangedCounter = true;  // Set the flag that the user manually changed the counter
  }
}
