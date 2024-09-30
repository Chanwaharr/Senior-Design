#include <Arduino.h>
// Pin definitions
const int buttonPin1 = 27;  // Button 1 for increment
const int buttonPin2 = 9;  // Button 2 for decrement

// Variables for button states and counter
int button1_State = 0, button2_State = 0;
int count_value = 0;
int prestate = 0;  // Used to detect button press transitions

const unsigned long sensor1Interval = 500;  // 5 seconds
unsigned long previousMillisSensor1 = 0;     // to store last time sensor1 was read

// Debouncing variables
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;  // 50 milliseconds debounce delay

void setup() {
  // Initialize the pushbutton pins as inputs
  pinMode(buttonPin1, INPUT);
  pinMode(buttonPin2, INPUT);
  
  // Start serial communication at 115200 baud rate
  Serial.begin(115200);
  Serial.println("Counter Initialized.");
}

void loop() {

  unsigned long currentMillis = millis(); // Get current time

  // Sensor 1 - Record data every 5 seconds
  if (currentMillis - previousMillisSensor1 >= sensor1Interval) {
    previousMillisSensor1 = currentMillis;  // Update last read time
   Serial.println("HI");
  }
 
  // Read the state of the pushbuttons
  button1_State = digitalRead(buttonPin1);
  button2_State = digitalRead(buttonPin2);

  // Check if button 1 is pressed and the prestate is 0 (button was not previously pressed)
  if (button1_State == HIGH && prestate == 0) {
    count_value++;  // Increment the counter
    Serial.print("Counter incremented: ");
    Serial.println(count_value);  // Print the updated counter value
    prestate = 1;  // Set prestate to 1 to avoid counting multiple times for a single press
  }

  // Check if button 2 is pressed and the prestate is 0
  else if (button2_State == HIGH && prestate == 0) {
    count_value--;  // Decrement the counter
    Serial.print("Counter decremented: ");
    Serial.println(count_value);  // Print the updated counter value
    prestate = 1;  // Set prestate to 1 to avoid counting multiple times for a single press
  }

  // Reset the prestate if both buttons are not pressed
  else if (button1_State == LOW && button2_State == LOW) {
    prestate = 0;
  }
}