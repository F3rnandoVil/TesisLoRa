#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include "SSD1306.h"
#include "images.h"
#include <BluetoothSerial.h>

#define SCK     5    // GPIO5  -- SX1278's SCK
#define MISO    19   // GPIO19 -- SX1278's MISO
#define MOSI    27   // GPIO27 -- SX1278's MOSI
#define SS      18   // GPIO18 -- SX1278's CS
#define RST     14   // GPIO14 -- SX1278's RESET
#define DI0     26   // GPIO26 -- SX1278's IRQ(Interrupt Request)
#define BAND    915E6

// Device ID - Change this for each device (1, 2, 3, etc.)
const int DEVICE_ID = 1; // Change to 2 for the second device

SSD1306 display(0x3c, 21, 22);
BluetoothSerial SerialBT;

String rssi = "RSSI --";
String packSize = "--";
String lastReceivedPacket = "";
String lastSentPacket = "";
unsigned int messageCounter = 0;
bool isTransmitting = false;

// Message structure: "FROM:TO:MESSAGE"
struct LoRaMessage {
  int fromDevice;
  int toDevice;
  String message;
  unsigned long timestamp;
};

void setup() {
  pinMode(16, OUTPUT);
  pinMode(2, OUTPUT);
  
  digitalWrite(16, LOW);    // set GPIO16 low to reset OLED
  delay(50); 
  digitalWrite(16, HIGH);   // while OLED is running, must set GPIO16 in high
  
  Serial.begin(115200);
  while (!Serial);
  
  // Initialize Bluetooth
  SerialBT.begin("LoRa_Device_" + String(DEVICE_ID)); // Bluetooth device name
  Serial.println("Bluetooth Started! Ready to pair...");
  
  Serial.println("LoRa Transceiver Test - Device " + String(DEVICE_ID));
  
  SPI.begin(SCK, MISO, MOSI, SS);
  LoRa.setPins(SS, RST, DI0);
  
  if (!LoRa.begin(915E6)) {
    Serial.println("Starting LoRa failed!");
    while (1);
  }
  
  // Set LoRa parameters for better communication
  LoRa.setTxPower(20);          // Set transmission power
  LoRa.setSpreadingFactor(7);   // Set spreading factor (6-12)
  LoRa.setSignalBandwidth(125E3); // Set bandwidth
  LoRa.setCodingRate4(5);       // Set coding rate
  LoRa.setPreambleLength(8);    // Set preamble length
  LoRa.setSyncWord(0xF3);       // Set sync word
  
  Serial.println("LoRa init ok");
  
  display.init();
  display.flipScreenVertically();  
  display.setFont(ArialMT_Plain_10);
  
  // Start in receive mode
  startReceiving();
  
  updateDisplay();
  delay(1500);
}

void loop() {
  // Check for Bluetooth messages from phone
  if (SerialBT.available()) {
    String bluetoothMessage = SerialBT.readString();
    bluetoothMessage.trim();
    
    if (bluetoothMessage.length() > 0) {
      // Parse message format: "TO_DEVICE:MESSAGE" or just "MESSAGE" (broadcast)
      int colonIndex = bluetoothMessage.indexOf(':');
      int targetDevice = 0; // 0 means broadcast to all
      String message = bluetoothMessage;
      
      if (colonIndex > 0) {
        targetDevice = bluetoothMessage.substring(0, colonIndex).toInt();
        message = bluetoothMessage.substring(colonIndex + 1);
      }
      
      sendLoRaMessage(targetDevice, message);
      SerialBT.println("Message sent via LoRa: " + message);
    }
  }
  
  // Check for Serial messages (for testing without Bluetooth)
  if (Serial.available()) {
    String serialMessage = Serial.readString();
    serialMessage.trim();
    
    if (serialMessage.length() > 0) {
      sendLoRaMessage(0, serialMessage); // Broadcast message
    }
  }
  
  // Check for incoming LoRa messages
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    handleIncomingMessage(packetSize);
  }
  
  // Update display every 2 seconds
  static unsigned long lastDisplayUpdate = 0;
  if (millis() - lastDisplayUpdate > 2000) {
    updateDisplay();
    lastDisplayUpdate = millis();
  }
  
  delay(10);
}

void sendLoRaMessage(int targetDevice, String message) {
  isTransmitting = true;
  
  // Stop receiving to send
  LoRa.idle();
  
  String fullMessage = String(DEVICE_ID) + ":" + String(targetDevice) + ":" + message;
  
  LoRa.beginPacket();
  LoRa.print(fullMessage);
  LoRa.endPacket();
  
  lastSentPacket = message;
  messageCounter++;
  
  // LED indication for transmission
  digitalWrite(2, HIGH);
  delay(100);
  digitalWrite(2, LOW);
  
  Serial.println("Sent: " + fullMessage);
  
  // Return to receive mode
  startReceiving();
  isTransmitting = false;
  
  updateDisplay();
}

void handleIncomingMessage(int packetSize) {
  String packet = "";
  packSize = String(packetSize, DEC);
  
  for (int i = 0; i < packetSize; i++) {
    packet += (char)LoRa.read();
  }
  
  rssi = "RSSI " + String(LoRa.packetRssi(), DEC);
  
  // Parse message format: "FROM:TO:MESSAGE"
  LoRaMessage msg = parseMessage(packet);
  
  // Check if message is for this device or broadcast
  if (msg.toDevice == DEVICE_ID || msg.toDevice == 0) {
    lastReceivedPacket = msg.message;
    
    Serial.println("Received from Device " + String(msg.fromDevice) + ": " + msg.message);
    Serial.println(rssi);
    
    // Send to Bluetooth device (phone)
    SerialBT.println("From Device " + String(msg.fromDevice) + ": " + msg.message);
    
    updateDisplay();
    
    // LED indication for received message
    for (int i = 0; i < 3; i++) {
      digitalWrite(2, HIGH);
      delay(50);
      digitalWrite(2, LOW);
      delay(50);
    }
  }
}

LoRaMessage parseMessage(String packet) {
  LoRaMessage msg;
  msg.timestamp = millis();
  
  int firstColon = packet.indexOf(':');
  int secondColon = packet.indexOf(':', firstColon + 1);
  
  if (firstColon > 0 && secondColon > firstColon) {
    msg.fromDevice = packet.substring(0, firstColon).toInt();
    msg.toDevice = packet.substring(firstColon + 1, secondColon).toInt();
    msg.message = packet.substring(secondColon + 1);
  } else {
    // Fallback for malformed messages
    msg.fromDevice = 0;
    msg.toDevice = DEVICE_ID;
    msg.message = packet;
  }
  
  return msg;
}

void startReceiving() {
  LoRa.receive();
}

void updateDisplay() {
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_10);
  
  // Device info
  display.drawString(0, 0, "Device ID: " + String(DEVICE_ID));
  display.drawString(70, 0, rssi);
  
  // Status
  String status = isTransmitting ? "SENDING" : "LISTENING";
  display.drawString(0, 12, "Status: " + status);
  
  // Last sent message
  if (lastSentPacket.length() > 0) {
    display.drawString(0, 24, "Sent: " + lastSentPacket.substring(0, 15));
  }
  
  // Last received message
  if (lastReceivedPacket.length() > 0) {
    display.drawString(0, 36, "Recv: " + lastReceivedPacket.substring(0, 15));
  }
  
  // Message counter
  display.drawString(0, 48, "Msgs: " + String(messageCounter));
  
  // Bluetooth status
  String btStatus = SerialBT.hasClient() ? "BT: Connected" : "BT: Waiting";
  display.drawString(0, 60, btStatus);
  
  display.display();
}

// Function to send test message (can be called from Serial or Bluetooth)
void sendTestMessage() {
  String testMsg = "Test message " + String(messageCounter + 1);
  sendLoRaMessage(0, testMsg); // Broadcast test message
}