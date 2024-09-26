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
#include <TimeLib.h>

#define WIFI_SSID  "WhiteSky-332"
#define WIFI_PASSWORD "74976532"
#define API_KEY "AIzaSyCs7OKUVsWg3hRFSYSiHQGc5xeTp1RyTv8"
#define DATABASE_URL "https://urban-hotspots-1-default-rtdb.firebaseio.com/"
 
Firebase firebase(DATABASE_URL);

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define OLED_RESET 4
 
static const int RXPin = 25;
static const int TXPin = 26;

TinyGPSPlus gps;
HardwareSerial GPSSerial(2);
 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
 
static const unsigned char PROGMEM logo_bmp[] =
{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};
 
DHT dht(13, DHT22); //This is for the temp/humidity sensor
const int CS_PIN = 5; //This is for the SD card
const int SENSOR_PIN = 39; // This is for the light sensor
const int SOUND_PIN = 36; //for the sound sensor
const int VOLTAGE_DIVIDER = 34; //Voltage divider for bat life
const int PEOPLE_COUNT_UP = 27; //Increment Push Button
const int PEOPLE_COUNT_DOWN = 9; //Decrement Push Button
 
File myFile;
const char* fileName = "/Data.txt"; // File name

// Variables for button states and counter
int button1_State = 0, button2_State = 0;
int PeopleCounter = 0;
int prestate = 0;  // Used to detect button press transitions

// Other global variables
unsigned long sendDataPrevMillis = 0;
unsigned long lastReconnectAttempt = 0;

// Constants for sound sensor sensitivity and reference voltage
const float SOUND_SENSITIVITY = 0.1; 
const float REFERENCE_VOLTAGE = 5.0;
bool userChangedCounter = false; 
 
 
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

  GPSSerial.begin(9600, SERIAL_8N1, RX, TX);
  dht.begin();
  delay(1000);
  initializeCard();

  if (SD.exists(fileName)) {
    Serial.println("\nFile exists. Will append to it.\n");
  } else {
    writeHeader(); // Only write header if file does not exist
  }
  pinMode(PEOPLE_COUNT_UP, INPUT);
  pinMode(PEOPLE_COUNT_DOWN, INPUT);
}

void logSensorDataToSD() {
  // Logic for reading sensor data and logging to the SD card
  int lightLux = analogRead(SENSOR_PIN); 
  float lightHz = lightLux / 1000.0;
  float temperature = dht.readTemperature(true);
  float humidity = dht.readHumidity();
  float soundVoltage = analogRead(SOUND_PIN) * (5.0 / 1023.0); 

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

  // Prepare time and date strings
  char timeStr[10] = "00:00:00";  // Default if no GPS data
  char dateStr[11] = "00-00-0000"; // Default if no GPS data

  if (gps.time.isValid()) {
    sprintf(timeStr, "%02d:%02d:%02d", gps.time.hour(), gps.time.minute(), gps.time.second());
  }

  if (gps.date.isValid()) {
    sprintf(dateStr, "%02d-%02d-%04d", gps.date.day(), gps.date.month(), gps.date.year());
  }

  // SD Card logging
  myFile = SD.open(fileName, FILE_WRITE);
  if (myFile && newData) {
    myFile.seek(myFile.size());
    myFile.print(millis());

    // Print date and time before latitude and longitude
    myFile.print(",");
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
    myFile.close();
    myFile.print(",");
    myFile.print(PeopleCounter);
  } else if (!myFile) {
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
  // Read sensor data
  int BatLife = analogRead(VOLTAGE_DIVIDER);
  float temperature = dht.readTemperature(true);
  float humidity = dht.readHumidity();
  float light = analogRead(SENSOR_PIN);

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

  // Print time from GPS
  if (gps.time.isValid()) {
    char timeStr[10];
    sprintf(timeStr, "%02d:%02d:%02d", gps.time.hour(), gps.time.minute(), gps.time.second());
    display.print("Time: ");
    display.println(timeStr);
  } else {
    display.println("Time: No GPS Data");
  }

  // Print temperature
  display.print("Temp: ");
  display.print(temperature, 1);
  display.println(" F");

  // Print humidity
  display.print("Humidity: ");
  display.print(humidity, 1);
  display.println("%");
  
  display.print("People: ");
  display.println(PeopleCounter);

  // Display the content
  display.display();
}

void loop() {
  // *** Pushbutton logic ***
  // Read the state of the pushbuttons
  button1_State = digitalRead(PEOPLE_COUNT_UP);
  button2_State = digitalRead(PEOPLE_COUNT_DOWN);

  // Check if increment button is pressed and prestate is 0
  if (button1_State == HIGH && prestate == 0) {
    PeopleCounter++;  // Increment the counter
    userChangedCounter = true;  // Flag that the user changed the PeopleCounter
    Serial.print("Counter incremented: ");
    Serial.println(PeopleCounter);  // Debugging
    // Immediately update the display after incrementing
    updateDisplay();

    // Send data to Firebase if connected
    if (WiFi.status() == WL_CONNECTED) {
      sendDataToFirebase();
    }

    prestate = 1;  // Set prestate to 1 to avoid counting multiple times for a single press
  }

  // Check if decrement button is pressed and prestate is 0
  else if (button2_State == HIGH && prestate == 0) {
    if (PeopleCounter > 0) {  // Prevent counter from going below 0
      PeopleCounter--;  // Decrement the counter
      userChangedCounter = true;  // Flag that the user changed the PeopleCounter
      Serial.print("Counter decremented: ");
      Serial.println(PeopleCounter);  // Debugging
      // Immediately update the display after decrementing
      updateDisplay();

      // Send data to Firebase if connected
      if (WiFi.status() == WL_CONNECTED) {
        sendDataToFirebase();
      }

      prestate = 1;  // Set prestate to avoid counting multiple times for a single press
    }
  }

  // Reset the prestate if both buttons are released
  else if (button1_State == LOW && button2_State == LOW) {
    prestate = 0;  // Reset prestate when both buttons are released
  }

  // *** Logging data to SD card and updating display every 5 seconds ***
  if (millis() - sendDataPrevMillis > 5000 || sendDataPrevMillis == 0) {
    sendDataPrevMillis = millis(); // Update the last sent time

    // Log data from sensors to SD card
    logSensorDataToSD();

    // Update the display with the latest information
    updateDisplay();

    // Check WiFi status
    if (WiFi.status() == WL_CONNECTED) {
      sendDataToFirebase();
    } else {
      // Attempt to reconnect every 5 seconds
      if (millis() - lastReconnectAttempt > 5000) {
        lastReconnectAttempt = millis();
        connectToWiFi();
      }
    }
  }
}