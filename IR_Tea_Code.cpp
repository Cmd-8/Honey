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
const unsigned long DISPENSE_TIMEOUT = 10000; // ms
const unsigned long SEALING_DURATION = 300;   // ms
const unsigned long POST_CYCLE_DELAY = 1000;  // ms
const unsigned long DEBOUNCE_DELAY = 50;      // ms for ISR
const int BATCH_SIZE = 24;

// =================================================================
// --- Global Objects & Variables ---
// =================================================================

WiFiClient wifiClient;
PubSubClient client(wifiClient);

enum MachineState { IDLE, DISPENSING, SEALING, POST_CYCLE_WAIT, ERROR_STATE };
MachineState currentState = IDLE;

// --- State Tracking Variables ---
bool isTeaDispensed = false;
bool isHoneyDispensed = false;
unsigned long stateTimer = 0;
unsigned long dispenseTimeoutTimer = 0;
unsigned long pulseTimer = 0;
bool isPulsing = false;

// --- Interrupt Flags (must be volatile) ---
volatile bool teaSensorTriggered = false;
volatile bool honeySensorTriggered = false;
volatile unsigned long lastTeaInterrupt = 0;
volatile unsigned long lastHoneyInterrupt = 0;

// --- Counters ---
unsigned int currentBatchCount = 0;
unsigned long totalCount = 0;
unsigned int batchNumber = 0;

// =================================================================
// --- ⚡ Interrupt Service Routines (ISRs) ---
// =================================================================

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

// =================================================================
// --- 📡 Network & Telemetry Functions ---
// =================================================================

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
    if (currentState == ERROR_STATE) return;
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
    doc["machine_id"] = MACHINE_ID; // Add machine ID to telemetry payload
    doc["total_Count"] = totalCount;
    doc["batch"] = batchNumber;

    char telemetry[128];
    serializeJson(doc, telemetry);
    client.publish("v1/devices/me/telemetry", telemetry);
    Serial.println("Telemetry Published.");
}

// =================================================================
// --- ⚙️ Main Setup ---
// =================================================================

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

    digitalWrite(RELAY_PIN_TEA, HIGH);
    digitalWrite(RELAY_PIN_HONEY, HIGH);
    digitalWrite(RELAY_PIN_SEAL, HIGH);

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
// --- 🔁 Main Loop (State Machine) ---
// =================================================================

void loop() {
    checkConnections();
   
   digitalWrite(RELAY_PIN_SEAL, LOW);

    switch (currentState) {
        case IDLE:
            if (digitalRead(START_SWITCH_PIN) == LOW) {  //digitalRead(START_SWITCH_PIN) == LOW
                Serial.println("Start signal received. Preparing to dispense.");

                 
                
                isTeaDispensed = false;
                isHoneyDispensed = false;
                teaSensorTriggered = false;
                honeySensorTriggered = false;
                
                isPulsing = false;
                pulseTimer = millis() - 1001;
                
                dispenseTimeoutTimer = millis();
                
                currentState = DISPENSING;
            }
            break;

        case DISPENSING:
            if (teaSensorTriggered && !isTeaDispensed) {
                isTeaDispensed = true;
                digitalWrite(RELAY_PIN_TEA, HIGH);
                teaSensorTriggered = false;
                Serial.println("✅ Tea dispensed. Relay is now permanently OFF.");
            }
            if (honeySensorTriggered && !isHoneyDispensed) {
                isHoneyDispensed = true;
                digitalWrite(RELAY_PIN_HONEY, HIGH);
                honeySensorTriggered = false;
                Serial.println("✅ Honey dispensed. Relay is now permanently OFF.");
            }

            if (isTeaDispensed && isHoneyDispensed) {
                Serial.println("Both materials dispensed. Moving to SEALING state.");
                digitalWrite(RELAY_PIN_TEA, HIGH);
                digitalWrite(RELAY_PIN_HONEY, HIGH);
                currentState = SEALING;
                stateTimer = millis();
                digitalWrite(RELAY_PIN_SEAL, LOW);
                break;
            }
            
            if (millis() - dispenseTimeoutTimer > DISPENSE_TIMEOUT) {
                Serial.println("❌ DISPENSING TIMEOUT!");
                digitalWrite(RELAY_PIN_TEA, HIGH);
                digitalWrite(RELAY_PIN_HONEY, HIGH);
                currentState = ERROR_STATE;
                break;
            }

            if (millis() - pulseTimer > 1000) {
                pulseTimer = millis();
                isPulsing = !isPulsing;

                if (isPulsing) {
                    Serial.println("PULSE ON");
                    if (!isTeaDispensed) digitalWrite(RELAY_PIN_TEA, LOW);
                    if (!isHoneyDispensed) digitalWrite(RELAY_PIN_HONEY, LOW);
                } else {
                    Serial.println("PULSE OFF (Pause)");
                    digitalWrite(RELAY_PIN_TEA, HIGH);
                    digitalWrite(RELAY_PIN_HONEY, HIGH);
                }
            }
            break;

        case SEALING:
            if (millis() - stateTimer > SEALING_DURATION) {
                digitalWrite(RELAY_PIN_SEAL, HIGH);
                Serial.println("Sealing complete.");
                
                totalCount++;
                currentBatchCount++;
                if (currentBatchCount >= BATCH_SIZE) {
                    currentBatchCount = 0;
                    batchNumber++;
                }
                publishTelemetry();
                
                stateTimer = millis();
                currentState = POST_CYCLE_WAIT;
            }
            break;

        case POST_CYCLE_WAIT:
            if (millis() - stateTimer > POST_CYCLE_DELAY) {
                Serial.println("Post-cycle wait finished. Returning to IDLE.");
                currentState = IDLE;
            }
            break;
            
        case ERROR_STATE:
            static bool errorDisplayed = false;
            if (!errorDisplayed) {
                Serial.println("System Halted in ERROR_STATE. Please reset the Arduino.");
                errorDisplayed = true;
            }
            break;
    }
}
