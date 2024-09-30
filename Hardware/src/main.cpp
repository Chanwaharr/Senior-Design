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
#include <ESP32Firebase.h>
#include <TimeLib.h>

#define WIFI_SSID  "WhiteSky-332"
#define WIFI_PASSWORD "74976532"
#define API_KEY "AIzaSyCs7OKUVsWg3hRFSYSiHQGc5xeTp1RyTv8"
#define DATABASE_URL "https://urban-hotspots-1-default-rtdb.firebaseio.com/"

Firebase firebase(DATABASE_URL);

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
const char* fileName = "/Data.txt"; // File name

// Global counter for people count
volatile int PeopleCounter = 0;  // Use volatile because it's modified in an ISR
const unsigned long sensor1Interval = 500;  // 500 milliseconds (0.5 seconds)
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

  GPSSerial.begin(115200, SERIAL_8N1, RXPin, TXPin);
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

  float latitude = gps.location.isValid() ? gps.location.lat() : 0;
  float longitude = gps.location.isValid() ? gps.location.lng() : 0;

  char timeStr[10] = "00:00:00";
  char dateStr[11] = "00-00-0000";

  if (gps.time.isValid()) {
    sprintf(timeStr, "%02d:%02d:%02d", gps.time.hour(), gps.time.minute(), gps.time.second());
  }

  if (gps.date.isValid()) {
    sprintf(dateStr, "%02d-%02d-%04d", gps.date.day(), gps.date.month(), gps.date.year());
  }

  myFile = SD.open(fileName, FILE_WRITE);
  if (myFile) {
    myFile.seek(myFile.size());
    myFile.print(dateStr);
    myFile.print(" ");
    myFile.print(timeStr);

    if (gps.location.isValid()) {
      myFile.print(",");
      myFile.print(latitude, 6);
      myFile.print(",");
      myFile.print(longitude, 6);
    } else {
      myFile.print(",No GPS Data");
    }

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
  float latitude = gps.location.isValid() ? gps.location.lat() : 0;
  float longitude = gps.location.isValid() ? gps.location.lng() : 0;

  firebase.setFloat("Environment/Temperature", temperature);
  firebase.setFloat("Environment/Humidity", humidity);
  firebase.setFloat("GPS/Latitude", latitude);
  firebase.setFloat("GPS/Longitude", longitude);
}

void updateDisplay() {
  int BatLife = analogRead(VOLTAGE_DIVIDER);
  float temperature = dht.readTemperature(true);
  float humidity = dht.readHumidity();
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);

  if (BatLife >= 2445) {
    display.println("Battery level: 76% - 100%");
  } else if (BatLife >= 2283) {
    display.println("Battery level: 51% - 75%");
  } else if (BatLife >= 2120) {
    display.println("Battery level: 26% - 50%");
  } else {
    display.println("Battery level: 0% - 25%");
  }

  if (gps.time.isValid()) {
    char timeStr[10];
    sprintf(timeStr, "%02d:%02d:%02d", gps.time.hour(), gps.time.minute(), gps.time.second());
    display.print("Time: ");
    display.println(timeStr);
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
    display.println("WiFi connection failed");
  }

  display.display();
}

void loop() {
  unsigned long currentMillis = millis();

  // Sensor 1 - Record data every 5 seconds
  if (currentMillis - previousMillisSensor1 >= sensor1Interval) {
    previousMillisSensor1 = currentMillis;  // Update last read time
    updateDisplay();  // Update the OLED display
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