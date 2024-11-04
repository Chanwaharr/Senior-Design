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
int fileIndex = 1;

// Global latitude and longitude variables
double latitude = 0.0;
double longitude = 0.0;

// Global counter for people count
volatile int PeopleCounter = 0;  // Use volatile because it's modified in an ISR
const unsigned long sensor1Interval = 30000;  // 25 seconds
unsigned long previousMillisSensor1 = 0;    // Store last time sensor1 was read
bool userChangedCounter = false;            // Flag to track if the user changed the PeopleCounter
// Debounce time
const unsigned long debounceDelay = 0;  // 100 milliseconds debounce delay
volatile unsigned long lastInterruptTime = 0;  // Last time an interrupt occurred

// Flags for debouncing using interrupts
volatile bool button1Pressed = false;
volatile bool button2Pressed = false;

// Function to handle button 1 press (increment)
void IRAM_ATTR handleButton1Press() {
  unsigned long interruptTime = millis();
  if (interruptTime - lastInterruptTime > debounceDelay) {
    PeopleCounter++;  // Set the flag
    userChangedCounter = true;
    lastInterruptTime = interruptTime;
  }
}

// Function to handle button 2 press (decrement)
void IRAM_ATTR handleButton2Press() {
  unsigned long interruptTime = millis();
  if (interruptTime - lastInterruptTime > debounceDelay) {
    PeopleCounter--;  // Set the flag
    userChangedCounter = true;
    lastInterruptTime = interruptTime;
  }
}

// Convert 24-hour time to 12-hour time with AM/PM
String formatTime12Hour(int hour, int minute, int second) {
  int timeZoneOffset = -6;
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
  Serial.println("Initializing SD card...");
  while (!SD.begin(CS_PIN)) {
    Serial.println("SD card initialization failed, retrying...");
    delay(1000);  // Retry every second
  }
  Serial.println("SD card initialized.");
}

void findNextFileName() {
  // Check for existing file and increment the index if it exists
  while (true) {
    fileName = "/Data" + String(fileIndex) + ".txt";
    if (!SD.exists(fileName)) {
      // File doesn't exist, so we can use this filename
      Serial.println("Using file: " + fileName);
      break;
    }
    fileIndex++;  // File exists, so increment index and check again
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

  // Find the next available file name and write the header
  findNextFileName();
  writeHeader();

  // Set up pins and attach interrupts
  pinMode(PEOPLE_COUNT_UP, INPUT);
  pinMode(PEOPLE_COUNT_DOWN, INPUT);
  
  // Attach interrupts to buttons
  attachInterrupt(digitalPinToInterrupt(PEOPLE_COUNT_UP), handleButton1Press, FALLING);
  attachInterrupt(digitalPinToInterrupt(PEOPLE_COUNT_DOWN), handleButton2Press, FALLING);
}

void logSensorDataToSD() {
  float voltage = analogRead(SENSOR_PIN) * 3.3 / 4095;
  float amps = voltage / 10000.0;
  float microamps = amps * 1000000;
  float lux = (microamps * 2.0) - 200.0;
  float temperature = dht.readTemperature(true) - 4.0;
  float humidity = dht.readHumidity();
  float soundVoltage = analogRead(SOUND_PIN) * 3.3 / 4095;
  float soundDecibels = 20 * log10(soundVoltage / 0.001);

  boolean newData = false;
  for (unsigned long start = millis(); millis() - start < 1000;) {
    while (GPSSerial.available()) {
      if (gps.encode(GPSSerial.read())) {
        newData = true;
      }
    }
  }

  // Get latitude and longitude from Firebase instead of Google
  if (WiFi.status() == WL_CONNECTED) {
    String latStr = firebase.getString("GPS/Latitude");
    String lngStr = firebase.getString("GPS/Longitude");

    if (latStr != "" && lngStr != "") {
      latitude = latStr.toDouble();
      longitude = lngStr.toDouble();
    } else {
      Serial.println("Error retrieving location from Firebase");
      latitude = 0.0;
      longitude = 0.0;
    }
  } else if (gps.location.isValid()) {
    latitude = (gps.location.lat());
    longitude = (gps.location.lng());
  } else {
    latitude = 0.0;
    longitude = 0.0;
  }

  // Use WiFi time if connected, else fallback to GPS time
  String formattedTime = "No Time Data";
  if (WiFi.status() == WL_CONNECTED) {
    formattedTime = firebase.getString("UpdatedTime");  // Get time from Firebase
    if (formattedTime == "") {
      formattedTime = "Firebase Error";  // Handle potential error
    }
  } else if (gps.time.isValid()) {
    int hour = gps.time.hour();
    int minute = gps.time.minute();
    int second = gps.time.second();
    formattedTime = formatTime12Hour(hour, minute, second);
  }

  char dateStr[11] = "00-00-0000";
  if (gps.date.isValid()) {
    sprintf(dateStr, "%02d-%02d-%04d", gps.date.month(), gps.date.day(), gps.date.year());
  }

  Serial.print("Free heap memory before: ");
  Serial.println(ESP.getFreeHeap());

  // Reinitialize the SD card before logging data
  if (SD.begin(CS_PIN)) {
    Serial.println("SD card reinitialized successfully.");

    myFile = SD.open(fileName, FILE_WRITE);
  if (myFile) {
    myFile.seek(myFile.size());
    myFile.print(dateStr);
    myFile.print(",");
    myFile.print(formattedTime);

    myFile.print(",");
    myFile.print(latitude,6);
    myFile.print(",");
    myFile.print(longitude,6);

    myFile.print(",");
    myFile.print(lux);
    myFile.print(",");
    myFile.print(temperature, 2);
    myFile.print(",");
    myFile.print(humidity, 2);
    myFile.print(",");
    myFile.print(soundDecibels);
    myFile.print(",");
    myFile.println(PeopleCounter); // Log PeopleCounter to SD

    // Flush the file to ensure data is written to the SD card
    myFile.flush();

    myFile.close();
    Serial.println("Data logged to SD card");
    SD.end();
  } else {
    Serial.println("Error opening " + String(fileName));
  }
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

  firebase.setFloat("Environment/Temperature", temperature);
  firebase.setFloat("Environment/Humidity", humidity);
}

void updateDisplay() {
  int BatLife = analogRead(VOLTAGE_DIVIDER);
  float voltage = (BatLife / 4095.0) * 3.3 * 2;
  float temperature = dht.readTemperature(true) - 4.0;
  float humidity = dht.readHumidity();

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);

  if (BatLife >= 4.2) {
    display.println("Battery: 100%");
  } else if (BatLife >= 4.1) {
      display.println("Battery: 90%");
  } else if (BatLife >= 4.0) {
      display.println("Battery: 80%");
  } else if (BatLife >= 3.9) {
      display.println("Battery: 70%");
  } else if (BatLife >= 3.8) {
      display.println("Battery: 60%");
  } else if (BatLife >= 3.7) {
      display.println("Battery: 50%");
  } else if (BatLife >= 3.6) {
      display.println("Battery: 40%");
  } else if (BatLife >= 3.5) {
      display.println("Battery: 30%");
  } else if (BatLife >= 3.4) {
      display.println("Battery: 20%");
  } else if (BatLife >= 3.3) {
      display.println("Battery: 10%");
  } else {
      display.println("Battery: 0%");
  }

  // Check if connected to WiFi
  if (WiFi.status() == WL_CONNECTED) {
    // Get the time from Firebase
    String firebaseTime = firebase.getString("UpdatedTime");
    if (firebaseTime != "") {
      display.print("Time: ");
      display.println(firebaseTime);
    } else {
      display.println("Time: Firebase Error");
    }
  } else {
    // Fall back to GPS time if WiFi is not connected
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
  const unsigned long displayInterval = 1000;  // 1 seconds
  if (currentMillis - previousMillisDisplay >= displayInterval) {
    previousMillisDisplay = currentMillis;  // Update last display update time
    updateDisplay();  // Update the OLED display
    // If WiFi is connected, send data to Firebase
    if (WiFi.status() == WL_CONNECTED) {
      sendDataToFirebase();
    } else {
      connectToWiFi();  // Attempt to reconnect to WiFi
    }
  }

  // Sensor 1 - Record data every 30 seconds
  if (currentMillis - previousMillisSensor1 >= sensor1Interval) {
    previousMillisSensor1 = currentMillis;  // Update last read time
    logSensorDataToSD();  // Log data to SD card
  }

  // Handle increment button press
  if (button1Pressed) {
    PeopleCounter++;  // Increment PeopleCounter
    lastInterruptTime = millis();  // Reset the flag
    userChangedCounter = true;  // Set the flag that the user manually changed the counter
  }

  // Handle decrement button press
  if (button2Pressed) {
    PeopleCounter--;  // Decrement PeopleCounter
    lastInterruptTime = millis();  // Reset the flag
    userChangedCounter = true;  // Set the flag that the user manually changed the counter
  }
}