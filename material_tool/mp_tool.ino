// LIBRARIES
// Used for SPI communication for RFID (included in the Arduino IDE)
#include <SPI.h>
// RFID library
#include <MFRC522.h>
// Handle Wi-Fi connection for ESP8266
#include <ESP8266WiFi.h>
// MQTT client - utilizes Wi-Fi library
#include <PubSubClient.h>

/* BUTTONS 
Multiple buttons with voltage division use one analog pin 
Analog button value is read in 10 bits (range 0-1024)
The different buttons will have analog values within this range
Listed from left to right (closer to further away from input):

BUTTON    ANALOG VALUE IN RANGE
Input     900-1024
Color     600-750
Mode      400-500
Output    300-390
*/
#define BUTTONS   A0
// RGB LED pins
#define LED_RED   D0            
#define LED_GREEN D2
#define LED_BLUE  D1
// RFID pins
#define RST_PIN   D3
#define SS_PIN    D8

// VARIABLES FOR FUNCTIONALITY FLOW CONTROL
// Variables for reading and comparing new and old analog button values
int newAnalogValue;
int oldAnalogValue;
// newTime and oldTime control flow based on an interval timing system
int newTime;
int oldTime;
const int INTERVAL = 200;
boolean inputPressed = false;
boolean outputPressed = false;
// Illumination feedback - LED will stop flashing for
// x ms to emulate tactile feedback upon button press
const int tactileFlash = 50;

// RFID VARIABLES
// Create MFRC522 instance (RFID reader)
MFRC522 rfid(SS_PIN, RST_PIN);
// Initialize RFID authentication key variable
MFRC522::MIFARE_Key key;
// Location of sector of material node ID
byte sector         = 1;
// Location of block containing the material node ID (first 8 bytes)
byte blockAddr      = 4;
// Location of sector keys
byte trailerBlock   = 7;

// MQTT/WIFI VARIABLES
const char* ssid = "WiFiName";
const char* password = "WiFiPassword";
// IP/host running your MQTT broker
const char* mqttServer = "127.0.0.1";
// Create WiFi client instance
WiFiClient espClient;
// Create MQTT client instance with WiFi client
PubSubClient client(espClient);
// Initialize variable for last received data
long lastMsg = 0;
// Initialize variable for publishing data
char msg[50];

// MAPPING RELATIONSHIP VARIABLES
int inputDevice = 0;
int outputDevice = 0;
char outputColor = 'g';
boolean blinkHigh = true;
boolean invertedDistance = false;


void setup() {
  Serial.begin(115200);
  // All buttons as one analog input
  pinMode(BUTTONS, INPUT);

  oldTime = millis();
  
  // Initialize SPI bus to communicate with RFID reader
  SPI.begin();
  // Initialize MFRC522 (RFID reader)
  rfid.PCD_Init();
  delay(200);
  
  // Run helper function to setup WiFi
  wifiSetup();
  // Set server with given variable and default MQTT port
  client.setServer(mqttServer, 1883);
  // Callback invoked when message on subscribed topic is received
  client.setCallback(callback);
  

  // Initialize RFID hex key to FFFFFFFFFFFF (factory default)
  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }
  
  // RGB LED pin modes
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  // Set idle/awaiting command state to color white
  setColor(200, 200, 200); 
  delay(200);
  Serial.println("\nMaterial programming tool ready");
}


void loop() {
  // Check if MQTT client is connected, (re)connect if not
  if (! client.connected()) {
    reconnect();
  }

  // Allow the client to process incoming messages and maintain its connection to the server
  client.loop(); 
  
  newTime = millis();
  
  newAnalogValue = analogRead(BUTTONS);
  // Read analog button value every loop - disregard values under 100
  if (newAnalogValue > 100) readButtons(newAnalogValue);
  
  //if (newAnalogValue != oldAnalogValue) {
  //if (newAnalogValue != oldAnalogValue  & newTime > (oldTime + INTERVAL)) {
  //if (newTime < oldTime + INTERVAL) return;

  // Run in every loop to blink LED if inverse mode is active
  blinkColor();

  
  // Look for new tags
  if ( ! rfid.PICC_IsNewCardPresent()) {
    delay(50);
    return;
  }
  // Select one of the tags
  if ( ! rfid.PICC_ReadCardSerial()) {
    delay(50);
    return;
  }

  //oldAnalogValue = newAnalogValue;
  //oldTime = newTime;
  
  // Initialize RFID reading variables
  MFRC522::StatusCode status;
  byte buffer[18];
  byte size = sizeof(buffer);

  // Reading RFID tags
  //Serial.println(F("Authenticating using key A..."));
  status = (MFRC522::StatusCode) rfid.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &key, &(rfid.uid));
  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("PCD_Authenticate() failed: "));
    Serial.println(rfid.GetStatusCodeName(status));
    return;
  }
  //Serial.print(F("Card UID:"));
  //dumpByteArray(rfid.uid.uidByte, rfid.uid.size);

  //Serial.println(F("\nCurrent data in sector:"));
  //rfid.PICC_DumpMifareClassicSectorToSerial(&(rfid.uid), &key, sector);

  status = (MFRC522::StatusCode) rfid.MIFARE_Read(blockAddr, buffer, &size);
    if (status != MFRC522::STATUS_OK) {
      Serial.print(F("MIFARE_Read() failed: "));
      Serial.println(rfid.GetStatusCodeName(status));
    }

  //Serial.print(F("Data in block ")); Serial.print(blockAddr); Serial.println(F(":"));
  String identification = returnByteString(buffer, 8);
  //Serial.println("identification: " + identification);
  int deviceNumber = identification.toInt();
  Serial.println("deviceNumber: " + (String) deviceNumber);
  
  if (inputPressed && !outputPressed) {
    
    if (inputDevice != 0) { // Input device already set - cannot read again
      Serial.println("Input device has already been set (device " + (String) inputDevice + ")");
      failureRead();
    } else { // Reading input device
      inputDevice = deviceNumber;
      Serial.println("Input set to device " + (String) inputDevice);
      successfulRead();
    }
  } else if (inputPressed && outputPressed) {
    // TODO: 
    // Input and output devices are set - send command
    if (inputDevice != 0) {
      outputDevice = deviceNumber;
      // TODO: proceed to send mapping over mqtt
      printVariables();
      publishCommand();
      successfulCommand();
      Serial.println("Resetting all settings");
      resetVariables();
    } else { // Both input and output pressed without any tags read - unoutput device
      outputDevice = deviceNumber;
      Serial.println("UnOutput device " + (String) outputDevice + " with color " + (String) outputColor);
      publishUnOutput();
      successfulUnOutput();
      Serial.println("Resetting all settings");
      resetVariables();
    }
    
  } else {
    Serial.println("RFID tag was scanned (device: " + (String) deviceNumber + ") but no command was run ");
    failureRead();
    setColor(200, 0, 200);
  }

  // Halt PICC
  rfid.PICC_HaltA();
  // Stop encryption on PCD
  rfid.PCD_StopCrypto1();
}

// Dump RFID hexadecimal data to serial
void dumpByteArray(byte *buffer, byte bufferSize) {
  for (byte i = 0; i < bufferSize; i++) {
    Serial.print(buffer[i] < 0x10 ? " 0" : " ");
    Serial.print(buffer[i], HEX);
  }
}

// Return RFID hexadecimal data as a String
String returnByteString(byte *buffer, byte bufferSize) {
  String hexString = "";
  for (byte i = 0; i < bufferSize; i++) {
    hexString += (String) buffer[i];
  } return hexString;
}

// Reset all mapping relationship variables
void resetVariables(){
  outputColor = 'g';
  inputPressed = false;
  outputPressed = false;
  inputDevice = 0;
  outputDevice = 0;
  invertedDistance = false;
}

// Helper function for debug
void printVariables() {
  Serial.println();
  Serial.println("Input device: " + (String) inputDevice);
  Serial.println("Output device: " + (String) outputDevice);
  Serial.println("Color: " + (String) outputColor);
  Serial.println("Inverted distance: " + (String) invertedDistance);
  Serial.println();
}

// Helper function for reading analog button voltage values
boolean inInterval(int compare, int low, int high) {
  return !(compare < low) && !(high < compare);
}


void readButtons(int analogValue) {
  if (inInterval(newAnalogValue, 300, 390)) {
    Serial.println("OUTPUT BUTTON PRESSED");
    if (inputPressed && inputDevice != 0) { // Ready to read output
      outputPressed = true;
      setColor(0, 0, 0);
      delay(tactileFlash);
      changeColor(outputColor, 250);
    } else if (inputPressed && inputDevice == 0) { // Ready to read unOutput
      outputPressed = true;
      setColor(0, 0, 0);
      delay(tactileFlash);
      setColor(0, 0, 250);
      outputColor = 'b';
    } else { // input button is not pressed or input device is not set
      failureRead();
      resetVariables();
      setColor(200, 200, 200);
    }
    //return 4;
  } else if (inInterval(newAnalogValue, 400, 500)) {
    Serial.println("INVERT DISTANCE BUTTON PRESSED");
    if (inputDevice != 0) {
      invertedDistance = !invertedDistance;
      Serial.println("invertedDistance = " + (String) invertedDistance);
      // If user turns off inverted distance while LED is LOW (while blinking)
      // it can be stuck there. This is to make sure it's on HIGH
      changeColor(outputColor, 250);
    } else if (inputPressed && outputPressed) {
      failureRead();
      changeColor(outputColor, 250);
    } else {
      failureRead();
      setColor(200, 200, 200);
    }
    
  } else if (inInterval(newAnalogValue, 600, 750)) {
    Serial.println("COLOR CHANGE BUTTON PRESSED");
    if (inputDevice != 0 || (inputPressed && outputPressed)) {
      alternateColors();
    } else {
      failureRead();
      setColor(200, 200, 200);
    }
    
  } else if (inInterval(newAnalogValue, 900, 1025)) {
    Serial.println("INPUT BUTTON PRESSED");
    // Input button is alread pressed
    if (inputPressed) {
      Serial.println("Input was already set. Reseting Variables");
      resetVariables();
    }
    setColor(0, 0, 0);
    delay(tactileFlash);
    setColor(200, 200, 200);
    // Change from purple to red
    inputPressed = true;
    //return 1;
  }
  delay(400);
}


void alternateColors() {
  if (outputColor == 'r') {
    outputColor = 'g';
    setColor(0, 250, 0);
  } else if (outputColor == 'g') {
    outputColor = 'b';
    setColor(0, 0, 250);
  } else if (outputColor == 'b') {
    outputColor = 'r';
    setColor(250, 0, 0);
  }
  Serial.println("Color is now set to " + (String) outputColor);
}


void turnOffLed() {
  analogWrite(LED_RED, 0);
  analogWrite(LED_GREEN, 0);
  analogWrite(LED_BLUE, 0);
}

// Blink RGB led if inverse mode is active
void blinkColor() {
  if (!invertedDistance) return;
  if (newTime < oldTime + INTERVAL) return;
  
  if (blinkHigh) changeColor(outputColor, 250);
  else changeColor(outputColor, 0);
  
  // Alternate on/off blinking
  blinkHigh = !blinkHigh;
  oldTime = millis();
}


void failureRead() {
  // Fade in and out twice with red
  turnOffLed();
  for (int num = 0; num < 3; num++) {
    analogWrite(LED_RED, 250);
    delay(100);
    analogWrite(LED_RED, 0);
    delay(100);
  } changeColor(outputColor, 250);
}

void successfulUnOutput() {
  // Fade in and out twice with green
  for (int num = 0; num < 2; num++) {
    for (int i = 0; i < 255; i += 2) {
      setColor(0, 0, i);
      delay(3);
    } for (int n = 255; n > 0; n -= 2) {
      setColor(0, 0, n);
      delay(3);
    }
  } setColor(200, 0, 200);
}

void successfulRead() {
  // Fade in and out twice with green
  for (int num = 0; num < 1; num++) {
    for (int i = 0; i < 255; i += 2) {
      setColor(0, i, 0);
      delay(3);
    } for (int n = 255; n > 0; n -= 2) {
      setColor(0, n, 0);
      delay(3);
    }
  } for (int i = 0; i < 255; i += 4) {
      setColor(0, i, 0);
      delay(3);
  }
}

void successfulCommand() {
  // Fade in and out trice with all colors
  for (int num = 0; num < 3; num++) {
    for (int i = 0; i < 255; i += 2) {
      setColor(i, i, i);
      delay(3);
    } for (int n = 255; n > 0; n -= 2) {
      setColor(n, n, n);
      delay(3);
    }
  } setColor(200, 0, 200);
}


void setColor(int colors[]){
  analogWrite(LED_RED, colors[0]);
  analogWrite(LED_GREEN, colors[1]);
  analogWrite(LED_BLUE, colors[2]);
}

void setColor(int red, int green, int blue) {
  analogWrite(LED_RED, red);
  analogWrite(LED_GREEN, green);
  analogWrite(LED_BLUE, blue);
}

void changeColor(char color, int ledValue) {
  /*if (ledValue < 0 || ledValue > 255) {
    Serial.println("Cannot change led intensity: Input (" + (String) ledValue + ") is negativ or larger than 255");
    return;
  }*/
  if (color  == 'r') {        // RED
    analogWrite(LED_RED, ledValue);
  } else if (color == 'g') {  // GREEN
    analogWrite(LED_GREEN, ledValue);
  } else if (color == 'b') {  // BLUE
    analogWrite(LED_BLUE, ledValue);
  }
}



void wifiSetup() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  // Start connecting to WiFi
  WiFi.begin(ssid, password);
  
  // Print loading text while connecting 
  boolean waitingWifi = true;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (waitingWifi) {
      setColor(100, 100, 0);
      waitingWifi = !waitingWifi;
    } else {
      turnOffLed();
    }
  }
  turnOffLed();

  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("Local IP address: ");
  Serial.println(WiFi.localIP());
}


void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("]: ");
  String message = "";
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
    message += (char) payload[i];
  }
  Serial.println();  

  // Material programming tool should not subscribe to any topics
  
}



void reconnect() {
  // Loop until reconnected
  while (! client.connected()) {
    Serial.println("Attempting MQTT connection...");
    // Attempt to connect
    String clientName = "Tool";
    //Serial.println("Client name: " + clientName);
    if (client.connect(clientName.c_str())) { //clientName.c_str())) {
      Serial.println("Connected to MQTT Broker at " + (String) mqttServer);
      // One client can subscribe to many topics at the same time
      // If you wish to subscribe to a topic, use the method below
      //client.subscribe("distance");

      // Listen to devices assigned as input
      // Data will be in format "ID-COLOR", i.e. "3-B" (meaning device 3 is input on color blue")

      // Listen to if this device is used as output
      //client.subscribe("2-distance");
      
      // If we loose connection we will reconnect to topics used as input
      // resubscribe();
      
      // If you wish, publish a test announcement when connected
      // client.publish("test", "hello broker");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}


void publishUnOutput() {
  String unOutputMessage = (String) outputColor;
  String unOutputTopic = (String) outputDevice + "-unoutput";
  Serial.println("UnOutput [" + unOutputTopic + "]: " + unOutputMessage);
  publishMessage(unOutputMessage, unOutputTopic);
}

void publishCommand() {
  char invert;
  if (invertedDistance) invert = 't';
  else invert = 'f';
  String outputMessage = (String) inputDevice + "-" + (String) outputColor + invert;
  String outputTopic = (String) outputDevice + "-output";
  String inputMessage = (String) outputDevice;
  String inputTopic = (String) inputDevice + "-input";
  Serial.println("Output message [" + outputTopic + "]: " + outputMessage);
  Serial.println("Input message [" + inputTopic + "]: " + inputMessage);
  publishMessage(outputMessage, outputTopic);
  publishMessage(inputMessage, inputTopic);
}

void publishMessage(String message, String topic) {
  /* We start by writing our data as formatted output to sized buffer (using snprintf)
   * In this case we use an integer, and we must format it as such (%d)
   * There are different formatting options based on the data type:
   * 
   * %lu unsigned long
   * %ld signed long
   * %d integer
   * %f float
   * %s string
   * %c char
   */
  snprintf (msg, 75, "%s", message.c_str()); // Store our data 'tagId' in the sized buffer 'msg'
  Serial.print("Publish message: ");
  Serial.println(msg);
  client.publish(topic.c_str(), msg); 
}


