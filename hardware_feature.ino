#include <ThingSpeak.h>
#include "WiFiEsp.h"
#include "ThingSpeak.h"

// Initialize Wifi
char ssid[] = "your network SSID";   // your network SSID (name) 
char pass[] = "your network password";   // your network password
int keyIndex = 0;            // your network key Index number (needed only for WEP)
WiFiEspClient  client;
#ifndef HAVE_HWSERIAL1
#include "SoftwareSerial.h"
SoftwareSerial Serial1(2, 3); // RX, TX
#define ESP_BAUDRATE  19200
#else
#define ESP_BAUDRATE  115200
#endif

//  Initialize Thingspeak
unsigned long myChannelNumber = 0000000;
const char * myWriteAPIKey = "myWriteAPIKey";

// Pin Definitions
const int mode_Red = 13;
const int mode_Yellow = 12;
const int mode_Green = 11;
const int state_Red = 10;
const int state_Green = 9;
const int buzzer = 8;
const int button = 7;

// Variables for Button State and Timing
int buttonState = 0;
int lastButtonState = 0;
int unpauseButton = 0;
int timerLastButtonState = 0;
int timerButtonState = 0;
unsigned long pressStartTime = 0;
unsigned long pressDuration = 0;
unsigned long timerPressStartTime = 0;
unsigned long timerPressDuration = 0;
int currentTime = 0;

// Mode and Timer Settings
String modes[4] = { "Green", "Yellow", "Red", "Custom" };
int modesTimer[4] = {60, 45, 30, 10};
int modeIndex = 0;  // Index for the current mode

// State Flags
bool changeMode = false;
bool sendTele = false;
bool startTimer = false;
bool outOfTime = false;
bool pauseButtonState = false;

// input from python script
String inputString = "";

// ================================ Initial SetUp when Powered On ================================
/**
 * Initializes the setup for the microcontroller.
 * - Initializes serial communication at 9600 baud rate.
 * - Waits until serial communication is established.
 * - Sets the baud rate for ESP8266.
 * - Initializes WiFi and checks for the presence of the WiFi shield.
 * - Initializes ThingSpeak client.
 * - Sets the pin modes for various components (LEDs, buzzer, button).
 * - Sets initial states for LEDs.
 * returns None
 */
void setup() {
  // set up for thingspeak
  Serial.begin(9600);
  while(!Serial){;}
  setEspBaudRate(ESP_BAUDRATE);
  while (!Serial) {;}
  Serial.print("Searching for ESP8266..."); 
  WiFi.init(&Serial1);
  if (WiFi.status() == WL_NO_SHIELD) {
    Serial.println("WiFi shield not present");
    while (true);
  };
  Serial.println("found it!");
  ThingSpeak.begin(client); 

  // Initialize pins
  pinMode(mode_Red, OUTPUT);
  pinMode(mode_Yellow, OUTPUT);
  pinMode(mode_Green, OUTPUT);
  pinMode(state_Red, OUTPUT);
  pinMode(state_Green, OUTPUT);
  pinMode(buzzer, OUTPUT);
  pinMode(button, INPUT);

  // Initial LED states
  digitalWrite(mode_Green, HIGH);
  digitalWrite(state_Green, HIGH);
};


// ================================ Activates the buzzer with specified on/off durations and repeat count ================================ 
/**
 * Activates a buzzer with specified on and off durations for a certain number of repetitions.
 * param onDuration The duration in milliseconds for which the buzzer is on.
 * param offDuration The duration in milliseconds for which the buzzer is off.
 * param repeat The number of times the buzzer pattern should be repeated.
 * returns None
 */
void activateBuzzer(int onDuration, int offDuration, int repeat) {
  for (int i = 0; i < repeat; i++) {
    tone(buzzer, 1000);
    delay(onDuration);
    noTone(buzzer);
    delay(offDuration);
  }
}


// ================================ Updates the mode LED based on the current mode ================================
/**
 * Updates the mode of the system based on the current mode index.
 * The mode can be Green, Yellow, Red, or Custom.
 * It sets the corresponding pins to HIGH based on the mode.
 * returns None
 */
void updateMode() {
  // Turn off all mode LEDs
  digitalWrite(mode_Green, LOW);
  digitalWrite(mode_Yellow, LOW);
  digitalWrite(mode_Red, LOW);

  //  Turn on the appropriate mode LED 
  if (modes[modeIndex] == "Green") {
    digitalWrite(mode_Green, HIGH);
  } else if (modes[modeIndex] == "Yellow") {
    digitalWrite(mode_Yellow, HIGH);
  } else if (modes[modeIndex] == "Red") {
    digitalWrite(mode_Red, HIGH);
  } else if (modes[modeIndex] == "Custom") {
    digitalWrite(mode_Green, HIGH);
    digitalWrite(mode_Yellow, HIGH);
    digitalWrite(mode_Red, HIGH);
  } 
  Serial.print("Mode changed: ");
  Serial.println(modes[modeIndex]);
}


// ================================ Resets the timer state and updates mode ================================
/**
 * Resets the state of the system by updating the mode, setting current time to 0,
 * resetting timer press duration, setting pause button state to false,
 * turning off the red LED and turning on the green LED, and stopping the timer.
 * returns None
 */
void resetState() {
  updateMode();
  currentTime = 0;
  timerPressDuration = 0;
  pauseButtonState = false;
  digitalWrite(state_Red, LOW);
  digitalWrite(state_Green, HIGH);
  startTimer = false;
}


// ================================ Logs the current time to Serial ================================
/**
 * Logs the current time to the Serial monitor.
 * param None
 * returns None
 */
void logCurrentTime() {
  Serial.print("Current Time: ");
  Serial.println(currentTime);
}


// ================================ Blinks the LED corresponding to the current mode ================================
/**
 * Blinks an LED based on the current mode selected.
 * returns None
 */
void blinkModeLED() {
  int blinkDelay = 350;
  if (modes[modeIndex] == "Green") {
    digitalWrite(mode_Green, HIGH);
    delay(blinkDelay);
    digitalWrite(mode_Green, LOW);
    delay(blinkDelay);
  } else if (modes[modeIndex] == "Yellow") {
    digitalWrite(mode_Yellow, HIGH);
    delay(blinkDelay);
    digitalWrite(mode_Yellow, LOW);
    delay(blinkDelay);
  } else if (modes[modeIndex] == "Red") {
    digitalWrite(mode_Red, HIGH);
    delay(blinkDelay);
    digitalWrite(mode_Red, LOW);
    delay(blinkDelay);
  } else if (modes[modeIndex] == "Custom") {
    digitalWrite(mode_Green, HIGH);
    digitalWrite(mode_Yellow, HIGH);
    digitalWrite(mode_Red, HIGH);
    delay(blinkDelay);
    digitalWrite(mode_Green, LOW);
    digitalWrite(mode_Yellow, LOW);
    digitalWrite(mode_Red, LOW);
    delay(blinkDelay);
  }
}


// ================================ Handles the pause functionality ================================
/**
 * Handles the pause functionality by changing LED states and waiting for a button press to resume.
 * returns None
 */
void handlePause() {
  digitalWrite(state_Red, HIGH);
  digitalWrite(state_Green, LOW);
  unpauseButton = digitalRead(button);
  Serial.println("Paused! Click to resume.");
  while (unpauseButton == LOW) {
    unpauseButton = digitalRead(button);
    delay(100);
  }
  digitalWrite(state_Red, LOW);
  digitalWrite(state_Green, HIGH);
}


// ================================ Allows changing the custom mode time ================================
/**
 * Changes the custom mode settings based on user input.
 * The function sets the custom mode LEDs and buzzer, waits for user input to change the custom time,
 * and updates the custom time value accordingly.
 * returns None
 */
void changingCustomMode() {
  // Telegram portion
  digitalWrite(mode_Red, HIGH);
  digitalWrite(mode_Yellow, HIGH);
  digitalWrite(mode_Green, HIGH);
  digitalWrite(state_Green, HIGH);
  digitalWrite(state_Red, HIGH);
  activateBuzzer(1000, 1000, 1);
  Serial.println("Changing Custom Time");

  while (true) {
    if (digitalRead(button) == LOW) {
      delay(50); // Debounce delay
      // Wait for the button to be released
      while (digitalRead(button) == LOW){
        if (Serial.available() > 0){
          inputString = Serial.readStringUntil('\n');
        }
        if (inputString.indexOf("NewCustomTime_") != -1) {

          int delimiterIndex = inputString.indexOf('_');
          String valueStr = inputString.substring(delimiterIndex + 1);

          int newValue = valueStr.toInt();
          if (newValue || valueStr == "0") { // Valid number
            modesTimer[3] = newValue;
          };
        };
      };
      
      delay(50); // Debounce delay
      break;  // Break out of the while loop when the button is pressed and released
    };
  };
  digitalWrite(mode_Red, LOW);
  digitalWrite(mode_Yellow, LOW);
  digitalWrite(mode_Green, LOW);
  digitalWrite(state_Green, LOW);
  digitalWrite(state_Red, LOW);
  activateBuzzer(1000, 1000, 1);
  resetState();
}


// ================================ Runs the timer for the current mode ================================
/**
 * Runs a timer until the pause button is pressed for a long duration.
 * Handles button presses, time tracking, and updating ThingSpeak channel.
 * returns None
 */
void runTimer() {
  while (!pauseButtonState) {
    currentTime++;

    timerButtonState = digitalRead(button);
    while (timerButtonState == HIGH) {
      timerButtonState = digitalRead(button);
      if (timerButtonState != timerLastButtonState) {
        timerLastButtonState = timerButtonState;
        if (timerButtonState == HIGH) {
          timerPressStartTime = millis();
        } else {
          timerPressDuration = millis() - timerPressStartTime;
          if (timerPressDuration > 2000) {
            pauseButtonState = true;
          } else {
            handlePause();
          }
        }
      }
    }

    if (currentTime > modesTimer[modeIndex]) {   // Time ran out
      outOfTime = true;
      break;
    }
    logCurrentTime();

    blinkModeLED();

    delay(1000);  // Delay for timer increment
  }

  if (outOfTime) {
    activateBuzzer(1000, 1000, 3);
  } else {
    activateBuzzer(3000, 3000, 1);
  }

  // Update ThingSpeak
  ThingSpeak.setField(modeIndex+1, currentTime);
  ThingSpeak.setField(modeIndex+5, 1);
  int x = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
  if(x == 200){
    Serial.println("Channel update successful.");
  }
  else{
    Serial.println("Problem updating channel. HTTP error code " + String(x));
  }

  // Print Summary
  Serial.println("Summary: (" + modes[modeIndex] + "|" + currentTime + ")");
  // Reset state
  resetState();
}


// ================================ Sets the ESP8266 baud rate ================================
/**
 * Sets the baud rate for ESP8266 communication.
 * param baudrate The baud rate to be set for ESP8266.
 * returns None
 */
void setEspBaudRate(unsigned long baudrate){
  long rates[6] = {115200,74880,57600,38400,19200,9600};

  Serial.print("Setting ESP8266 baudrate to ");
  Serial.print(baudrate);
  Serial.println("...");

  for(int i = 0; i < 6; i++){
    Serial1.begin(rates[i]);
    delay(100);
    Serial1.print("AT+UART_DEF=");
    Serial1.print(baudrate);
    Serial1.print(",8,1,0,0\r\n");
    delay(100);  
  }
    
  Serial1.begin(baudrate);
}


// ================================ Main loop function ================================
/**
 * Main loop function that handles WiFi connection and button press events.
 * If WiFi is not connected, it attempts to connect to the specified SSID.
 * Monitors the button state and triggers different actions based on the duration of the button press.
 * Actions include sending telemetry, changing modes, starting a timer, or executing a custom mode.
 */
void loop() {
  if(WiFi.status() != WL_CONNECTED){
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    while(WiFi.status() != WL_CONNECTED){
      WiFi.begin(ssid, pass); 
      Serial.print(".");
      delay(5000);     
    } 
    Serial.println("\nConnected.");
  }


  buttonState = digitalRead(button);

  // Check for change of button state
  if (buttonState != lastButtonState) {
    lastButtonState = buttonState;
    if (buttonState == HIGH) {
      pressStartTime = millis();
    } else {
      pressDuration = millis() - pressStartTime;
      if (pressDuration > 5000) {
        sendTele = true;
      } else if (pressDuration > 1000) {
        changeMode = true;
        modeIndex = (modeIndex + 1) % 4;  // Increment modeIndex and wrap around
      } else {
        startTimer = true;
      }
      Serial.print("Button was pressed for ");
      Serial.print(pressDuration);
      Serial.println(" milliseconds");

      if (changeMode) {
        updateMode();
        changeMode = false;
      } else if (startTimer) {
        runTimer();
      } else {
        changingCustomMode();
      }
    }
    delay(100);
  }
}

// This error is due to thingspeak. 
// [WiFiEsp] >>> TIMEOUT >>>
// [WiFiEsp] Data packet send error (2)
// [WiFiEsp] Failed to write to socket 3
// [WiFiEsp] Disconnecting  3
// It only allows you to upload once every 15-30sec
