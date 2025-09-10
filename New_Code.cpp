// --- Libraries ---
#include <WiFiS3.h>
#include "PubSubClient.h"
#include <ArduinoJson.h>
#include "config.h" // Includes all settings from your config.h file

// =================================================================
// --- ⚙️ CONFIGURATION ---
// =================================================================

// --- Pin Definitions ---
const byte IR_SENSOR_PIN_TEA = 11;
const byte IR_SENSOR_PIN_HONEY = 12;
const byte START_SWITCH_PIN = 8;
const byte RELAY_PIN_TEA = 2;
const byte RELAY_PIN_HONEY = 4;
const byte RELAY_PIN_SEAL = 3;

// --- Operational Parameters ---
const unsigned long SEALING_DURATION = 500; // ms: How long to run the sealer
const unsigned long DEBOUNCE_DELAY = 50;    // ms for ISR
const int BATCH_SIZE = 24;

// --- Relay States (assuming Active-LOW relays) ---
#define RELAY_ON LOW
#define RELAY_OFF HIGH

// =================================================================
// --- Global Objects & Variables ---
// =================================================================

WiFiClient wifiClient;
PubSubClient client(wifiClient);

// Simplified state machine
enum MachineState { IDLE, DISPENSING, SEALING };
MachineState currentState = IDLE;

// --- State Tracking Variables ---
bool isTeaDispensed = false;
bool isHoneyDispensed = false;
// unsigned long stateTimer = 0; // Timer variable has been removed

// --- Interrupt Flags (must be volatile) ---
volatile bool teaSensorTriggered = false;
volatile bool honeySensorTriggered = false;
volatile unsigned long lastTeaInterrupt = 0;
volatile unsigned long lastHoneyInterrupt = 0;

// --- Counters ---
unsigned int currentBatchCount = 0;
unsigned long totalCount = 0;
unsigned int batchNumber = 0;

/*
=================================================================
--- ISRs, Network Functions, and Setup ---
(No changes to these sections from the previous version)
=================================================================
*/
/*
=================================================================
--- ISRs, Network Functions, and Setup ---
(This is the corrected version of the previously garbled text)
=================================================================
*/



// --- ⚡ Interrupt Service Routines (ISRs) ---
void teaSensorISR() {
    if (millis() - lastTeaInterrupt > DEBOUNCE_DELAY) {
        teaSensorTriggered = true;
        lastTeaInterrupt = millis();
    }
}

void honeySensorISR() {
    if (millis() - lastHoneyInterrupt > DEBOUNCE_DELAY) {
        honeySensorTriggered = true;
        lastHoneyInterrupt = millis();
    }
}

// --- 📡 Network & Telemetry Functions ---
void connectWiFi() {
    Serial.print("Connecting to WiFi...");
    WiFi.begin(WLAN_SSID, WLAN_PASS);
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 20) {
        delay(500);
        Serial.print(".");
        retries++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi connected!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nWiFi connection FAILED");
    }
}

void connectMQTT() {
    if (client.connected()) return;
    Serial.print("Connecting to ThingsBoard as '");
    Serial.print(MACHINE_ID);
    Serial.print("'... ");
    if (client.connect(MACHINE_ID, THINGSBOARD_TOKEN, NULL)) {
        Serial.println("Connected!");
    } else {
        Serial.print("MQTT Failed, rc=");
        Serial.println(client.state());
        delay(2000);
    }
}

void checkConnections() {
    if (WiFi.status() != WL_CONNECTED) {
        connectWiFi();
        return;
    }
    if (!client.connected()) {
        connectMQTT();
    }
    client.loop();
}

void publishTelemetry() {
    if (!client.connected()) return;
    StaticJsonDocument<128> doc;
    doc["machine_id"] = MACHINE_ID;
    doc["total_Count"] = totalCount;
    doc["batch"] = batchNumber;
    char telemetry[128];
    serializeJson(doc, telemetry);
    client.publish("v1/devices/me/telemetry", telemetry);
    Serial.println("Telemetry Published.");
}

// --- ⚙️ Main Setup ---
void setup() {
    Serial.begin(9600);
    while (!Serial);
    Serial.println("--- Automated Packager Initializing ---");
    Serial.print("Machine ID: ");
    Serial.println(MACHINE_ID);
    pinMode(IR_SENSOR_PIN_TEA, INPUT_PULLUP);
    pinMode(IR_SENSOR_PIN_HONEY, INPUT_PULLUP);
    pinMode(START_SWITCH_PIN, INPUT_PULLUP);
    pinMode(RELAY_PIN_TEA, OUTPUT);
    pinMode(RELAY_PIN_HONEY, OUTPUT);
    pinMode(RELAY_PIN_SEAL, OUTPUT);
    digitalWrite(RELAY_PIN_TEA, RELAY_OFF);
    digitalWrite(RELAY_PIN_HONEY, RELAY_OFF);
    digitalWrite(RELAY_PIN_SEAL, RELAY_OFF);
    attachInterrupt(digitalPinToInterrupt(IR_SENSOR_PIN_TEA), teaSensorISR, FALLING);
    attachInterrupt(digitalPinToInterrupt(IR_SENSOR_PIN_HONEY), honeySensorISR, FALLING);
    connectWiFi();
    if (WiFi.status() == WL_CONNECTED) {
        client.setServer(THINGSBOARD_SERVER, THINGSBOARD_SERVERPORT);
        connectMQTT();
    }
    Serial.println("Setup Complete. System is IDLE.");
}
// =================================================================
// --- 🔄 Main Loop ---
// =================================================================
void loop() {
    // ⚠️ This function will NOT be called during the sealing delay
    checkConnections();
    digitalWrite(RELAY_PIN_SEAL, LOW); //stop the seal from keep going

    switch (currentState) {
        case IDLE:
            if (true) {
                Serial.println("Start signal received. Starting dispense cycle.");
                
                isTeaDispensed = false;
                isHoneyDispensed = false;
                teaSensorTriggered = false;
                honeySensorTriggered = false;
                

                
                digitalWrite(RELAY_PIN_TEA, RELAY_ON);
                digitalWrite(RELAY_PIN_HONEY, RELAY_ON);
                Serial.println("Dispensing relays turned ON.");

                currentState = DISPENSING;
            }
            break;

        case DISPENSING:
            //Relay_function();

            if (teaSensorTriggered && !isTeaDispensed) {
                while (digitalRead(IR_SENSOR_PIN_TEA) == LOW) { delay(10); }
                isTeaDispensed = true;
                digitalWrite(RELAY_PIN_TEA, RELAY_OFF);
                teaSensorTriggered = false;
                Serial.println("✅ Tea dispensed. Relay OFF.");
            }

            if (honeySensorTriggered && !isHoneyDispensed) {
                while (digitalRead(IR_SENSOR_PIN_HONEY) == LOW) { delay(10); }
                isHoneyDispensed = true;
                digitalWrite(RELAY_PIN_HONEY, RELAY_OFF);
                honeySensorTriggered = false;
                Serial.println("✅ Honey dispensed. Relay OFF.");
            }

            if (isTeaDispensed && isHoneyDispensed) {
                Serial.println("Both dispensed. Moving to SEALING.");
                currentState = SEALING;
            }

            //delay(1000);

            break;

        case SEALING:
            // The entire sealing process now happens here, in one blocking step.
            Serial.println("Sealing...");
            delay(300);
            digitalWrite(RELAY_PIN_SEAL, RELAY_ON);

            
            delay(500);
            
            digitalWrite(RELAY_PIN_SEAL, RELAY_OFF);
            Serial.println("Sealing complete.");
            
            // Increment counters and publish telemetry
            totalCount++;
            currentBatchCount++;
            if (currentBatchCount >= BATCH_SIZE) {
                currentBatchCount = 0;
                batchNumber++;
                pinMode(6, HIGH);
                delay(3000);
                pinMode(6, LOW);
            }
            publishTelemetry();
            
            // Go directly back to IDLE
            Serial.println("Cycle finished. Returning to IDLE.");
            currentState = IDLE;
            break;
    }
}
