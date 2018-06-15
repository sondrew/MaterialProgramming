#include <NewPing.h>        // Library for simplified distance measuring
#include <ESP8266WiFi.h>    // Handles Wi-Fi connection for ESP8266
#include <PubSubClient.h>   // MQTT client - uses Wi-Fi library

// RGB LED pins
#define RED   D1
#define GREEN D2
#define BLUE  D3

//  Ultrasonic sensor pins
#define TRIGGER D6
#define ECHO    D7
// Ultrasonic variables
#define MAX_DISTANCE 40
NewPing sonar(TRIGGER, ECHO, MAX_DISTANCE);

// Variables for controlling input publishing
int inputCounter = 0;        // Number of devices using this device as input - publishes data if > 0
int newTime = millis();     
int oldTime = millis();     
const int INTERVAL = 200;    // Distance measuring interval (millisec)
// Variables for input reading and publishing
int distance;                // Store distance as centimeters int in range 0-40 (MAX_DISTANCE)
int convertedDistance;       // Maps distance from 0-40 to 0-255 (8 bits)
boolean publishZero = false; // Don't publish distance 0 multiple times if not changed

const char deviceId = '1';   // ID of this device and the ID on the RFID tag
// Output color variables, two for each RGB color
// redInput store deviceId of input node and redInverse store output mode
int redInput = 0;
boolean redInverse = false;
int greenInput = 0;
boolean greenInverse = false;
int blueInput = 0;
boolean blueInverse = false;


/// MQTT variables
const char* ssid = "WifiName";          // WiFi name
const char* password = "WiFiPassword";  // WiFi password
const char* mqttServer = "127.0.0.1";   // IP/host running your MQTT broker
WiFiClient espClient;                   // Create WiFi client instance
PubSubClient client(espClient);         // Create MQTT client instance with WiFi client
long lastMsg = 0;                       // Initialize variable for last received data
char msg[50];                           // Initialize variable for publishing data
// Topics subscribed to
const String topicInput = (String) deviceId + "-input";
const String topicOutput = (String) deviceId + "-output";
const String topicUnInput = (String) deviceId + "-uninput";
const String topicUnOutput = (String) deviceId + "-unoutput";
// Name for topic to listen/publish to for input data
const String listeningTopicName = "distance";
const String publishTopicName = (String) deviceId + "-" + listeningTopicName;


void setup() {
  
  Serial.begin(115200);
  // Set pin modes for ultrasonic sensor and RGB LED
  pinMode(TRIGGER, OUTPUT);
  pinMode(ECHO, INPUT);
  pinMode(RED, OUTPUT);
  pinMode(GREEN, OUTPUT);
  pinMode(BLUE, OUTPUT);

  wifiSetup();                                    // Run helper function to setup WiFi
  client.setServer(mqttServer, 1883);             // Set server with given variable and default MQTT port
  client.setCallback(callback);                   // Callback invoked when message on subscribed topic is received

  // Flash green to indicate system is initializd 
  analogWrite(GREEN, 250);
  delay(1000);
  turnOffLed();
  
  Serial.println("Material " + (String) deviceId + " setup complete");
}

void loop() { 
  newTime = millis();

  // Check if MQTT client is connected, (re)connect if not
  if (! client.connected()) {
    reconnect();
  }
  // Allow the client to process incoming messages and maintain its connection to the server
  client.loop(); 

  // Return if time interval is not reached
  if (newTime < INTERVAL + oldTime) return;

  // Publish distance if node is used as input
  if (inputCounter > 0) {
    distance = sonar.ping_cm(); // Distance is in range 0 -> 40 (MAX_DISTANCE)
    // Output can be written to with 8 bits
    // Convert distance to a range of 0-255
    convertedDistance = distance * 6.25;
    //Serial.println("cm: " + (String) distance);
    if (distance != 0 ) {
      Serial.println("distance: " + (String) distance + "\t converted: " + (String) convertedDistance);
      publishMessage(convertedDistance, publishTopicName);
      publishZero = true;
      oldTime = millis();
    } else if (publishZero) {
      Serial.println("Publish zero");
      publishMessage(0, publishTopicName);
      publishZero = false;
      oldTime = millis();
    } 
  }
}


void turnOffLed() {
  analogWrite(RED, 0);
  analogWrite(GREEN, 0);
  analogWrite(BLUE, 0);
}

// ESP8266 WiFi setup
void wifiSetup() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password); // Start connecting to WiFi
  
  // Print loading text while connecting 
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("Local IP address: ");
  Serial.println(WiFi.localIP());
}

// MQTT function invoked when message is received
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
  String splitTopicName = splitString(topic, '-', 1);
  // Check and compare which topic a message is received on
  // E.g. topic == "1-input"
  int compareInputTopic = strcmp(topic, topicInput.c_str());
  // E.g. topic == "1-output"
  int compareOutputTopic = strcmp(topic, topicOutput.c_str());
  // E.g. topic == "1-uninput"
  int compareUnInputTopic = strcmp(topic, topicUnInput.c_str());
  // E.g. topic == "1-unoutput"
  int compareUnOutputTopic = strcmp(topic, topicUnOutput.c_str());
  // E.g. topic == "3-distance"
  int compareListeningTopic = strcmp(listeningTopicName.c_str(), splitTopicName.c_str());
  
  
  if (compareListeningTopic == 0) {
    // Receiving data from input node
    outputControl(topic, message);
  } else if (compareOutputTopic == 0) {
    // Use this device as output
    outputSubscription(message);
  } else if (compareInputTopic == 0) {
    // Use this device as input
    inputSubscription(message);
  } else if (compareUnOutputTopic == 0) {
    // Stop this device from being used as output
    unOutputSubscription(message);
  } else if (compareUnInputTopic == 0) {
    // Stop this device from being used as input
    unInputSubscription(message);
  }
}


void reconnect() {
  // Loop until reconnected
  Serial.println("Device ID as string: " + (String) deviceId);
  Serial.print("Standalone char: ");
  Serial.println(deviceId);
  while (! client.connected()) {
    Serial.println("Attempting MQTT connection...");
    // Attempt to connect
    String clientName = "Material " + (String) deviceId;
    Serial.println("Client name: " + clientName);
    if (client.connect(clientName.c_str())) { //clientName.c_str())) {
      Serial.println("Connected to MQTT Broker at " + (String) mqttServer);

      // Subscribe to MQTT topics used for coordinating mapping relationships
      client.subscribe(topicInput.c_str());
      client.subscribe(topicOutput.c_str());
      client.subscribe(topicUnInput.c_str());
      client.subscribe(topicUnOutput.c_str());
      
      // If connection is lost, reconnect to topics used as input
      resubscribe();
      
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

// Handle all data from an input node in a mapped relationship
// E.g. topic = "3-distance" and message = "142"
void outputControl(String topic, String message) {
  int inputDevice = charToInt(topic[0]);
  int data = message.toInt();

  // Change multiple colors if input node is used for several outputs
  if (inputDevice == redInput) changeColor('r', data); 
  if (inputDevice == greenInput) changeColor('g', data);
  if (inputDevice == blueInput) changeColor('b', data); 
}

// Use this device as an input node in a mapped relationship 
// If inputCounter is > 0, publish input data to listening topic
void inputSubscription(String message) {
  inputCounter++;
  // String outputDevice = (String) message[0];
  // Serial.println("This device (" + (String) deviceId + ") is now an input for device " + outputDevice);
}


// Use this device as an output node in a mapped relationship
// Starts listening to the input node's distance topic and uses this
// data to alter the output (color and mode) given the behaviour attributes
void outputSubscription(String message){
  int inputDevice = charToInt(message[0]); // deviceID of input node
  char color = message[2]; // I.e. 'r', 'g' or 'b'
  char inverse = message[3]; // I.e. 't' (inverse mode) or 'f' (normal mode)
  
  //Serial.println("inputDevice: '" + (String) inputDevice + "'\ncolor: '" + color + "'\ninverse: '" + inverse + "'");

  // No need to set inverse to false, as it's instatiated to false and reset if unsubscribed
  if (color == 'r') {
    redInput = inputDevice;
    if (inverse == 't') {
      redInverse = true;
      changeColor('r', 255);
    }
  } else if (color == 'g') {
    greenInput = inputDevice;
    if (inverse == 't') greenInverse = true;
  } else if (color == 'b') {
    blueInput = inputDevice;
    if (inverse == 't') blueInverse = true;
  } else {
    Serial.println("Error in outputSubscription: Color not valid [" + message + "] ");
  }

  // Subscribe to input node's distance topic
  String topic = (String) inputDevice + "-" + listeningTopicName;
  client.subscribe(topic.c_str());
  Serial.println("This device ("+  (String) deviceId + ") is now linked to device " + (String) inputDevice + " with color " + color);
  // printInputState();
}


// Remove mapping using this device as input
void unInputSubscription(String message) {
  if (inputCounter > 0) inputCounter--;
}


// Remove mapping using this device as output
void unOutputSubscription(String message) {
    char color = message[0];
    Serial.println("Unsubscribing input. Color '" + (String) color + "'. Printing state");
    printInputState();
    int inputDevice = 0;

    if (color == 'r' ) {
      Serial.println("Remove mapping to color red");
      inputDevice = redInput;
      redInput = 0;
      redInverse = false;
      changeColor('r', 0);
    } else if (color == 'g') {
      Serial.println("Remove mapping to color green");
      inputDevice = greenInput;
      greenInput = 0;
      greenInverse = false;
      changeColor('g', 0);
    } else if (color == 'b') {
      Serial.println("Remove mapping to color blue");
      inputDevice = blueInput;
      blueInput = 0;
      blueInverse = false;
      changeColor('b', 0);
    }

    // Print state after mapping relationship removal
    // printInputState();
    
    // Stop subscribing to input node's distance topic
    String unsubscribeTopic = (String) inputDevice + "-" + listeningTopicName;
    client.unsubscribe(unsubscribeTopic.c_str());
    //Serial.println("Unsubscribed topic [" + unsubscribeTopic + "]");
    
    // Tell input device to remove this mapping
    String unInputTopic = (String) inputDevice + "-uninput";
    publishMessage(charToInt(deviceId), unInputTopic);
    // Serial.println("Sent unInput signal to topic [" + unInputTopic + "]: '" + (String) deviceId + "'");
    // turnOffLed();
}

// Helper function for debug
void printInputState() {
  Serial.println("COLOR \t\t DEVICE \t INVERSE");
  Serial.println("Red \t\t " + (String) redInput + " \t\t " + (String) redInverse);
  Serial.println("Green \t\t " + (String) greenInput + " \t\t " + (String) greenInverse);
  Serial.println("Blue \t\t " + (String) blueInput + " \t\t " + (String) blueInverse);
}


// If MQTT connection breaks, this will make sure your 
// client resubscribes to all devices used as input
void resubscribe() {
  turnOffLed(); // Make sure a color doesn't get stuck displaying a color
  String topic;
  
  if (redInput != 0) {
    topic = (String) redInput + "-" + listeningTopicName;
    Serial.println("Resubscribing red LED to topic " + topic);
    client.subscribe(topic.c_str());
  }
  if (greenInput != 0) {
    topic = (String) greenInput + "-" + listeningTopicName;
    Serial.println("Resubscribing green LED to topic " + topic);
    client.subscribe(topic.c_str());
  }
  if (blueInput != 0) {
    topic = (String) blueInput + "-" + listeningTopicName;
    Serial.println("Resubscribing blue LED to topic " + topic);
    client.subscribe(topic.c_str());
  }
}


void changeColor(char color, int ledValue) {
  // color = 'r' || 'g' || 'b'
  // 0 <= ledValue <= 255
  
  int inverseMinValue = 26; // Have a threshold for low values, as the sensor is bad at reading short distances
  int ledStrength;

  if (color  == 'r') {
    if  (!redInverse && ledValue == 0) ledStrength = 0;
    else if (!redInverse && ledValue != 0) ledStrength = 255 - ledValue;
    else if (redInverse && ledValue == 0) ledStrength = 255;
    else if (redInverse && ledValue < inverseMinValue) ledStrength = 0;
    else if (redInverse && ledValue != 0 ) ledStrength = ledValue;
    Serial.println("Set color red to value " + (String) ledStrength);
    analogWrite(RED, ledStrength);
  } else if (color == 'g') {
    if (!greenInverse && ledValue == 0) ledStrength = 0;
    else if (!greenInverse && ledValue != 0) ledStrength = 255 - ledValue;
    else if (greenInverse && ledValue == 0) ledStrength = 255;
    else if (greenInverse && ledValue < inverseMinValue) ledStrength = 0;
    else if (greenInverse && ledValue != 0) ledStrength = ledValue;
    Serial.println("Set color green to value " + (String) ledStrength);
    analogWrite(GREEN, ledStrength);
  } else if (color == 'b') {  
    if  (!blueInverse && ledValue == 0) ledStrength = 0;
    else if (!blueInverse && ledValue != 0) ledStrength = 255 - ledValue;
    else if (blueInverse && ledValue == 0) ledStrength = 255;
    else if (blueInverse && ledValue < inverseMinValue) ledStrength = 0;
    else if (blueInverse && ledValue != 0) ledStrength = ledValue;
    Serial.println("Set color blue to value " + (String) ledStrength);
    analogWrite(BLUE, ledStrength);
  }
}


// Helper function of returning a substring before or after a (char) seperator
String splitString(String data, char separator, int index) {
    int found = 0;
    int strIndex[] = { 0, -1 };
    int maxIndex = data.length() - 1;

    for (int i = 0; i <= maxIndex && found <= index; i++) {
        if (data.charAt(i) == separator || i == maxIndex) {
            found++;
            strIndex[0] = strIndex[1] + 1;
            strIndex[1] = (i == maxIndex) ? i+1 : i;
        }
    }
    return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

void publishMessage(String data, String topic) {
  /* We start by writing our data as formatted output to sized buffer (using snprintf)
   * In this case we use a String, and we must format it as such (%s)
   * There are different formatting options based on the data type:
   * 
   * %lu unsigned long
   * %ld signed long
   * %d integer
   * %f float
   * %s string
   * %c char
   */
  const char* top = topic.c_str();
  snprintf (msg, 75, "%s", data.c_str()); // Store our data 'tagId' in the sized buffer 'msg'
  Serial.print("Publish message: ");
  Serial.println(msg);
  client.publish(top, msg); 
}

void publishMessage(int data, String topic) {
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
  const char* top = topic.c_str();
  snprintf (msg, 75, "%d", data); // Store our data 'tagId' in the sized buffer 'msg'
  Serial.print("Publish message: ");
  Serial.println(msg);
  client.publish(top, msg); 
}
