#include <SPI.h>
#include <LoRa.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define SCK     5    // GPIO5  -- SX1278's SCK
#define MISO    19   // GPIO19 -- SX1278's MISO
#define MOSI    27   // GPIO27 -- SX1278's MOSI
#define SS      18   // GPIO18 -- SX1278's CS
#define RST     14   // GPIO14 -- SX1278's RESET
#define DI0     26   // GPIO26 -- SX1278's IRQ(Interrupt Request)
#define BAND    915E6

// Device ID - Change this for each device (1, 2, 3, etc.)
const int DEVICE_ID = 1;

// BLE UUIDs
#define SERVICE_UUID        "12345678-1234-1234-1234-123456789abc"
#define CHARACTERISTIC_UUID_TX "12345678-1234-1234-1234-123456789abd"
#define CHARACTERISTIC_UUID_RX "12345678-1234-1234-1234-123456789abe"

BLEServer* pServer = NULL;
BLECharacteristic* pTxCharacteristic;
BLECharacteristic* pRxCharacteristic;
bool deviceConnected = false;
bool oldDeviceConnected = false;

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

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("BLE Device connected!");
      
      // LED indication for BLE connection
      for (int i = 0; i < 5; i++) {
        digitalWrite(2, HIGH);
        delay(100);
        digitalWrite(2, LOW);
        delay(100);
      }
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("BLE Device disconnected!");
    }
};

// Forward declaration
void sendLoRaMessage(int targetDevice, String message);

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      std::string stdValue = pCharacteristic->getValue();
      String value = String(stdValue.c_str());

      if (value.length() > 0) {
        Serial.println("Received via BLE: " + value);
        
        // Parse message format: "TO_DEVICE:MESSAGE" or just "MESSAGE" (broadcast)
        int colonIndex = value.indexOf(':');
        int targetDevice = 0; // 0 means broadcast to all
        String message = value;
        
        if (colonIndex > 0) {
          targetDevice = value.substring(0, colonIndex).toInt();
          message = value.substring(colonIndex + 1);
        }
        
        sendLoRaMessage(targetDevice, message);
        
        // Send confirmation back to phone
        String confirmation = "LoRa sent: " + message;
        pTxCharacteristic->setValue(confirmation.c_str());
        pTxCharacteristic->notify();
      }
    }
};

void setup() {
  pinMode(2, OUTPUT); // LED pin
  
  Serial.begin(115200);
  Serial.println("Starting BLE LoRa Transceiver - Device " + String(DEVICE_ID));

  // Initialize BLE
  String deviceName = "LoRa_Device_" + String(DEVICE_ID);
  BLEDevice::init(deviceName.c_str());
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  // TX Characteristic (Device to Phone)
  pTxCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID_TX,
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
  pTxCharacteristic->addDescriptor(new BLE2902());

  // RX Characteristic (Phone to Device)
  pRxCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID_RX,
                      BLECharacteristic::PROPERTY_WRITE
                    );
  pRxCharacteristic->setCallbacks(new MyCallbacks());

  pService->start();
  
  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x0);
  BLEDevice::startAdvertising();
  Serial.println("BLE advertising started. Device name: LoRa_Device_" + String(DEVICE_ID));
  
  // Initialize LoRa
  SPI.begin(SCK, MISO, MOSI, SS);
  LoRa.setPins(SS, RST, DI0);
  
  if (!LoRa.begin(915E6)) {
    Serial.println("Starting LoRa failed!");
    while (1);
  }
  
  // Set LoRa parameters for better communication
  LoRa.setTxPower(20);
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setPreambleLength(8);
  LoRa.setSyncWord(0xF3);
  
  Serial.println("LoRa init ok");
  
  // Start in receive mode
  startReceiving();
  
  // LED indication for successful setup
  for (int i = 0; i < 3; i++) {
    digitalWrite(2, HIGH);
    delay(200);
    digitalWrite(2, LOW);
    delay(200);
  }
  
  Serial.println("System ready! Waiting for BLE connection...");
}

void loop() {
  // Handle BLE connection changes
  if (!deviceConnected && oldDeviceConnected) {
    delay(500); // give the bluetooth stack time to get things ready
    pServer->startAdvertising(); // restart advertising
    Serial.println("Start advertising again");
    oldDeviceConnected = deviceConnected;
  }
  
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }

  // Check for Serial messages (for testing)
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
  delay(200);
  digitalWrite(2, LOW);
  
  Serial.println("Sent via LoRa: " + fullMessage);
  
  // Return to receive mode
  startReceiving();
  isTransmitting = false;
}

void handleIncomingMessage(int packetSize) {
  String packet = "";
  
  for (int i = 0; i < packetSize; i++) {
    packet += (char)LoRa.read();
  }
  
  int rssi = LoRa.packetRssi();
  
  // Parse message format: "FROM:TO:MESSAGE"
  LoRaMessage msg = parseMessage(packet);
  
  // Check if message is for this device or broadcast
  if (msg.toDevice == DEVICE_ID || msg.toDevice == 0) {
    lastReceivedPacket = msg.message;
    
    Serial.println("Received from Device " + String(msg.fromDevice) + ": " + msg.message);
    Serial.println("RSSI: " + String(rssi));
    
    // Send to BLE device (phone) if connected
    if (deviceConnected) {
      String bleMessage = "From Device " + String(msg.fromDevice) + ": " + msg.message + " (RSSI: " + String(rssi) + ")";
      pTxCharacteristic->setValue(bleMessage.c_str());
      pTxCharacteristic->notify();
    }
    
    // LED indication for received message (3 quick blinks)
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

// Function to send test message
void sendTestMessage() {
  String testMsg = "Test message " + String(messageCounter + 1);
  sendLoRaMessage(0, testMsg);
}