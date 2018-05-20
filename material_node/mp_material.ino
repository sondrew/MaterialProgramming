#include <NewPing.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#define RED   D1
#define GREEN D2
#define BLUE  D3

#define TRIGGER D6 //D8 not working
#define ECHO    D7 //D5 not working
#define MAX_DISTANCE 40

NewPing sonar(TRIGGER, ECHO, MAX_DISTANCE);

int ledStrength;
int ledInverse;
int distance;

// MQTT VARIABLES
const char* ssid = "WiPi";                        // WiFi name
//const char* ssid = "Sondre Widmark's iPhone";
const char* password = "somethingspecial";        // WiFi password
const char* mqttServer = "192.168.42.1";          // IP/host running your MQTT broker
//const char* mqttServer = "172.20.10.2";
WiFiClient espClient;                             // Create WiFi client instance
PubSubClient client(espClient);                   // Create MQTT client instance with WiFi client
long lastMsg = 0;                                 // Initialize variable for last received data
char msg[50];                                     // Initialize variable for publishing data

const int maxDevices = 4;
const char deviceId = '3';                            // ID of this device

// Topics subscribed to
const String topicInput = (String) deviceId + "-input";
const String topicOutput = (String) deviceId + "-output";
const String topicUnInput = (String) deviceId + "-uninput";
const String topicUnOutput = (String) deviceId + "-unoutput";

const String listeningTopicName = "distance";
const String publishTopicName = (String) deviceId + "-" + listeningTopicName;

// New system
int redInput = 0;
int greenInput = 0;
int blueInput = 0;
boolean redInverse = false;
boolean greenInverse = false;
boolean blueInverse = false;
int numOfOutput = 0;                                    // Number of devices using this device as input
const char* mqttDistanceTopic = deviceId + '-' + listeningTopicName.c_str(); // Name of topic this device will publish distance to

int mqttInput[maxDevices];                                    // List of devices used as input
int numOfInput = 0;                                     // Number of devices used as input
int mqttOutput[maxDevices];                                   // List of devices using this device as input

char colorMapping[maxDevices];
int numOfMapping = 0;


int convertedDistance;
boolean publishOutput = false;                 // Toggle if device should publish distance data (to minimize network abuse)
boolean publishZero = false;                  // Avoid leaving the led with light after distance is 0 after successfull distance publish

int newTime = millis();
int oldTime = millis();
const int INTERVAL = 300;         // Distance measuring interval (millisec)


void setup() {
  
  Serial.begin(115200);
  pinMode(TRIGGER, OUTPUT);
  pinMode(ECHO, INPUT);
  
  pinMode(RED, OUTPUT);
  pinMode(GREEN, OUTPUT);
  pinMode(BLUE, OUTPUT);

  wifiSetup();                                    // Run helper function to setup WiFi
  client.setServer(mqttServer, 1883);             // Set server with given variable and default MQTT port
  client.setCallback(callback);                   // Callback invoked when message on subscribed topic is received

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

  if (newTime < INTERVAL + oldTime) return;

  if (publishOutput) {
    distance = sonar.ping_cm();
    // Distance is in range 0 -> 40 (MAX_DISTANCE). Light intensity is a value from 0 to 255
    //if (distance < 3) convertedDistance = 0;
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
    // delay(200); TODO: LEGG INN 40TIMER
  }
  
  /*
  ledStrength = distance * 6.35; // led strength will be in range 0 - 255
  ledInverse = 255 - ledStrength;
  Serial.println("+led: " + (String) ledStrength);
  if (ledInverse > 40) {
    analogWrite(BLUE, ledInverse);
  } else {
    analogWrite(BLUE, 0);
  }
  
  Serial.println("-led: " + (String) ledInverse);
  analogWrite(GREEN, ledStrength);
  delay(100);

    // Check if MQTT client is connected, (re)connect if not
  if (! client.connected()) {
    reconnect();
  }
  // Allow the client to process incoming messages and maintain its connection to the server
  client.loop(); 
  
  //publishMessage(data, "topic");
  */
}
  


void testColors() {
  for (int color = 0; color < 3; color++) {
    for (int i = 0; i < 255; i += 10) {
      if (color == 0) {
        analogWrite(RED, i);
      } else if (color == 1) {
        analogWrite(GREEN, i);
      } else if (color == 2) {
        analogWrite(BLUE, i);
      } delay(200);
    }
  }
}


void turnOffLed() {
  analogWrite(RED, 0);
  analogWrite(GREEN, 0);
  analogWrite(BLUE, 0);
}


void wifiSetup() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  // Start connecting to WiFi
  WiFi.begin(ssid, password);
  
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
  // TODO: Maybe move remove these variables and just compare inline the if
  int compareInputTopic = strcmp(topic, topicInput.c_str());
  int compareOutputTopic = strcmp(topic, topicOutput.c_str());
  int compareUnInputTopic = strcmp(topic, topicUnInput.c_str());
  int compareUnOutputTopic = strcmp(topic, topicUnOutput.c_str());
  int compareListeningTopic = strcmp(listeningTopicName.c_str(), splitTopicName.c_str());
  
  
  if (compareListeningTopic == 0) {
    // If receiving data on a topic subscribed to (from device used as input)
    // E.g. topic = "3-distance"
    // Serial.println("outputControl");
    outputControl(topic, message);
  } else if (compareOutputTopic == 0) {
    // Mapping is made to this device - use this device as output
    // char[0].isNumeric && char[1] == '-' && char[2].isAlphanumeric && length == 3
    Serial.println("outputSubscription");
    outputSubscription(message);
  } else if (compareInputTopic == 0) {
    // Start publishing distance
    Serial.println("inputSubscription");
    inputSubscription(message);
  } else if (compareUnOutputTopic == 0) {
    // Stop this device from being used as output
    Serial.println("unOutputSubscription");
    unOutputSubscription(message);
  } else if (compareUnInputTopic == 0) {
    // Stop this device from being used as input
    Serial.println("unInputSubscription");
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
      // One client can subscribe to many topics at the same time
      // If you wish to subscribe to a topic, use the method below
      //client.subscribe("distance");

      // Listen to devices assigned as input
      // Data will be in format "ID-COLOR", i.e. "3-B" (meaning device 3 is input on color blue")
      
      Serial.println("topicInput: " + topicInput);
      Serial.println("topicOutput: " + topicOutput);
      Serial.println("topicUnInput: " + topicUnInput);
      Serial.println("topicUnOutput: " + topicUnOutput);
      
      client.subscribe(topicInput.c_str());
      client.subscribe(topicOutput.c_str());
      client.subscribe(topicUnInput.c_str());
      client.subscribe(topicUnOutput.c_str());
      
      // Listen to if this device is used as output
      //client.subscribe("2-distance");
      
      // If we loose connection we will reconnect to topics used as input
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

void outputControl(String topic, String message) {
  // E.g. topic = "3-distance" and input = "142"
  
  //int inputDevice = (int) topic[0];
  int inputDevice = charToInt(topic[0]);
  int data = message.toInt();
  //Serial.println("inputDevice: '" + (String) inputDevice + "'\ndata: " + (String) data);
  

  if (inputDevice == redInput) changeColor('r', data); 
  if (inputDevice == greenInput) changeColor('g', data);
  if (inputDevice == blueInput) changeColor('b', data); 
}

// Tells the material device when to start publishing distance data
void inputSubscription(String message) {
  String outputDevice = (String) message[0];
  //char color = message[2]; // doesn't need to know color

  //mqttOutput[numOfOutput] = outputDevice.toInt(); // TODO: Fix lazy solution
  numOfOutput++;
  publishOutput = true;

  //String topic = inputDevice + "-" + listeningTopicName;
  //Serial.println("Subscribing to topic '" + topic + "'");
  //client.subscribe(topic.c_str());
  Serial.println("This device (" + (String) deviceId + ") is now an input for device " + outputDevice);
}


// Map this device as output to an input device
void outputSubscription(String message){
  //int inputDevice = atoi((String) message[0].c_str());
  //int inputDevice = parseInt(message[0]);
  int inputDevice = charToInt(message[0]);
  char color = message[2];
  char inverse = message[3];
  Serial.println("inputDevice: '" + (String) inputDevice + "'\ncolor: '" + color + "'\ninverse: '" + inverse + "'");
  if (inverse == 't') Serial.println("inverse ('" + (String) inverse + "') == 't' -> true");
  else Serial.println("Not inverse");

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

  String topic = (String) inputDevice + "-" + listeningTopicName;
  client.subscribe(topic.c_str());
  Serial.println("This device ("+  (String) deviceId + ") is now linked to device " + (String) inputDevice + " with color " + color);
  printInputState();
}

// Helper function
int charToInt(char number) {
  String temp = (String) number;
  return temp.toInt();
}


// Remove mapping using this device as input
void unInputSubscription(String message) {
  // int inputDevice = (int) message[0];
  numOfOutput--;
  if (numOfOutput < 1) publishOutput = false;
}


// Remove mapping using this device as output
void unOutputSubscription(String message) {
    //int inputDevice = (int) message[0];
    // Message is just a char with the color to stop using as output
    // int inputDevice = charToInt(message[0]);
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
    
    /*
    if (color == 'r' && redInput == inputDevice) {
      Serial.println("Color is matching color red and input device");
      redInput = 0;
      redInverse = false;
    } else if (color == 'g' && greenInput == inputDevice) {
      Serial.println("Color is matching color green and input device");
      greenInput = 0;
      greenInverse = false;
    } else if (color == 'b' && blueInput == inputDevice) {
      Serial.println("Color is matching color blue and input device");
      blueInput = 0;
      blueInverse = false;
    }
    */

    Serial.println("Set input to 0 (hopefully). Printing new state:");
    printInputState();
    String unsubscribeTopic = (String) inputDevice + "-" + listeningTopicName;
    Serial.println("Unsubscribing topic: '" + unsubscribeTopic + "'");
    client.unsubscribe(unsubscribeTopic.c_str());
    // Tell input device to remove this mapping
    String unInputTopic = (String) inputDevice + "-uninput";
    Serial.println("Sending unInput signal to topic [" + unInputTopic + "]: '" + (String) deviceId + "'");
    publishMessage(charToInt(deviceId), unInputTopic);
    //turnOffLed();
}


void printInputState() {
  Serial.println("COLOR \t\t DEVICE \t INVERSE");
  Serial.println("Red \t\t " + (String) redInput + " \t\t " + (String) redInverse);
  Serial.println("Green \t\t " + (String) greenInput + " \t\t " + (String) greenInverse);
  Serial.println("Blue \t\t " + (String) blueInput + " \t\t " + (String) blueInverse);
}

/* OLD METHOD
void outputSubscription(String input){
  // E.g. input = "3-b"
  String inputDevice = (String) input[0];
  char color = input[2];
  

  mqttInput[numOfInput] = inputDevice.toInt(); // TODO: Fix lazy solution
  colorMapping[numOfInput] = color;
  numOfInput++;

  String topic = inputDevice + "-" + listeningTopicName;
  //Serial.println("Subscribing to topic '" + topic + "'");
  client.subscribe(topic.c_str());
  /*for (int i = 0; i < numOfInput; i++) {
    if (mqttInput[i] == 0) {
      mqttInput[i] = inputDevice.toInt(); // TODO: Fix lazy solution
      colorMapping[i] = color;
    }
  }*/  
  /*
  Serial.println("This device ("+  (String) deviceId + ") is now linked to device " + inputDevice + " with color " + (String) color);
  Serial.print("mqttInput: ");
  for (int i = 0; i < maxDevices; i++) {
    Serial.print((String) mqttInput[i]);
  }
  Serial.println();
  Serial.print("colorMapping: ");
  Serial.println(colorMapping);
  Serial.println("numOfInput: " + (String) numOfInput);
}
*/



/* OLD METHOD
void outputSubscription(String message) {
  String outputDevice = (String) message[0];
  char color = message[2];

  mqttOutput[numOfOutput] = outputDevice.toInt(); // TODO: Fix lazy solution
  numOfOutput++;
  publishOutput = true;

  //String topic = inputDevice + "-" + listeningTopicName;
  //Serial.println("Subscribing to topic '" + topic + "'");
  //client.subscribe(topic.c_str());
  Serial.println("This device ("+  (String) deviceId + ") is now an input for device " + outputDevice + " with color " + (String) color);
  Serial.print("mqttOutput: ");
  for (int i = 0; i < maxDevices; i++) {
    Serial.print((String) mqttOutput[i]);
  }
}*/

// If mqtt connection breaks, this will make sure your 
// client resubscribes to all devices used as input
void resubscribe() {
  
  turnOffLed();
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

/* OLD METHOD
void resubscribe() {
  turnOffLed();
  for (int i = 0; i < numOfInput; i++){
    if (mqttInput[i] != 0) {
      String topic = mqttInput[i] + "-" + listeningTopicName;
      Serial.println("Resubscribing to topic " + topic);
      client.subscribe(topic.c_str());
    }
  }
}
*/
/*
int lastRed = 0;
int lastGreen = 0;
int lastBlue = 0;
*/
void changeColor(char color, int ledValue) {
  //if (ledValue < 0 || ledValue > 255) {

  // 3 cm -> ledValue 18
  // 4 cm -> ledValue 25

  int inverseMinValue = 26;
  int ledStrength; //= ledValue;
  //Serial.println("Reached changeColor(): ledStrength = " + (String) ledStrength);
  if (color  == 'r') {
    //if (!redInverse && ledValue != 0) ledStrength = 255 - ledValue;
    //else if (redInverse && ledValue == 0) ledStrength = 255;
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
  } else if (color == 'b') {  //&& ledValue != 0) {
    if  (!blueInverse && ledValue == 0) ledStrength = 0;
    else if (!blueInverse && ledValue != 0) ledStrength = 255 - ledValue;
    else if (blueInverse && ledValue == 0) ledStrength = 255;
    else if (blueInverse && ledValue < inverseMinValue) ledStrength = 0;
    else if (blueInverse && ledValue != 0) ledStrength = ledValue;
    Serial.println("Set color blue to value " + (String) ledStrength);
    analogWrite(BLUE, ledStrength);
  }
}

/*

  if (color  == 'r') {
    //if (!redInverse && ledValue != 0) ledStrength = 255 - ledValue;
    //else if (redInverse && ledValue == 0) ledStrength = 255;
    if  (!redInverse && ledValue != 0) ledStrength = 255 - ledValue;
    else if (!redInverse && ledValue == 0) ledStrength = 0;
    else if (redInverse && ledValue != 0) ledStrength = ledValue;
    else if (redInverse && ledValue == 0 ) ledStrength = 255;
    Serial.println("Set color red to value " + (String) ledStrength);
    analogWrite(RED, ledStrength);
  } else if (color == 'g') {
    if (!greenInverse && ledValue != 0) ledStrength = 255 - ledValue;
    else if (greenInverse && ledValue == 0) ledStrength = 255;
    Serial.println("Set color green to value " + (String) ledStrength);
    analogWrite(GREEN, ledStrength);
  } else if (color == 'b') {  //&& ledValue != 0) {
    if  (!blueInverse && ledValue == 0) ledStrength = 0;
    else if (!blueInverse && ledValue != 0) ledStrength = 255 - ledValue;
    else if (blueInverse && ledValue == 0) ledStrength = 255;
    else if (blueInverse && ledValue < 26) ledStrength = 0;
    else if (blueInverse && ledValue != 0) ledStrength = ledValue;
    Serial.println("Set color blue to value " + (String) ledStrength);
    analogWrite(BLUE, ledStrength);

 */


/* OLD METHOD
void outputControl(String topic, String input) {
  // E.g. topic = "3-distance" and input = "142"
  String inputDevice = (String) topic[0];
  int data = input.toInt();
  int device = inputDevice.toInt(); // TODO: Fix lazy solution

  // Cheking if input topic is an input on this device
  for (int i = 0; i < numOfInput; i++) {
    if (mqttInput[i] == device) {
      //Serial.println("Input device (" + inputDevice + ") found for color " + (String) colorMapping[i]);
      changeColor(colorMapping[i], data);
    }
  }
}*/

/*
boolean isInputDevice(int inputDeviceId) {
  for (int i = 0; i < numOfInput; i++) {
    if (mqttInput[i] == inputDeviceId) {
      return true;
    }
  } return false;
}

// TODO: Refactor
void unsubscribeTopic(int inputDeviceId) {
  if (isInputDevice(inputDeviceId)) {
    Serial.println("Cannot unsubscribe to device " + (String) inputDeviceId + ": Subscription doesn't exist");
    return;
  }

  // Remove input device from mqttInput and colorMapping, move all elements to left
  for (int i = 0; i < maxDevices; i++) {
    if (mqttInput[i] == inputDeviceId) {
      for (int elem = i; elem < maxDevices - 1; elem++) {
        mqttInput[elem] = mqttInput[elem+1];
        colorMapping[elem] = colorMapping[elem+1];
      } 
      mqttInput[maxDevices-1] = 0;
      colorMapping[maxDevices-1] = '\0';
      numOfInput--;
      String topic = (String) inputDeviceId + "-" + listeningTopicName;
      client.unsubscribe(topic.c_str());
    }
  }
}
*/


String splitString(String data, char separator, int index)
{
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
