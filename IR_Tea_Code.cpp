
// --- Libraries ---
#include <WiFiS3.h>
#include "PubSubClient.h"
#include <ArduinoJson.h>
#include "ArduinoGraphics.h"
#include "Arduino_LED_Matrix.h"

// --- WiFi and ThingsBoard Configuration ---
const char* WLAN_SSID = "";
const char* WLAN_PASS = "";
#define TOKEN ""
#define THINGSBOARD_SERVER ""
#define THINGSBOARD_SERVERPORT 1883

// --- Matrix Display Class ---
class MatrixDisplay {
public:
    MatrixDisplay() {}
    void begin(const char* staticText = "UNO r4") {
        matrix.begin();
        matrix.beginDraw();
        matrix.stroke(0xFFFFFFFF);
        matrix.textFont(Font_4x6);
        matrix.beginText(0, 1, 0xFFFFFF);
        matrix.println(staticText);
        matrix.endText();
        matrix.endDraw();
        delay(2000);
    }
    void updateScroll(const char* message, unsigned long scrollSpeed = 70) {
        matrix.beginDraw();
        matrix.stroke(0xFFFFFFFF);
        matrix.textScrollSpeed(scrollSpeed);
        matrix.textFont(Font_5x7);
        matrix.beginText(0, 1, 0xFFFFFF);
        matrix.println(message);
        matrix.endText(SCROLL_LEFT);
        matrix.endDraw();
    }
    void clear() {
        matrix.beginDraw();
        matrix.clear();
        matrix.endDraw();
    }
private:
    ArduinoLEDMatrix matrix;
};

// --- Global Objects ---
WiFiClient wifiClient;
PubSubClient client(wifiClient);
MatrixDisplay ledDisplay;

// --- Pin Definitions ---
const byte irSensorPin_Tea = 11;
const byte irSensorPin_Honey = 12;
const byte switchPin = 8;
const byte relayPin_TeaMaterial = 2;
const byte relayPin_HoneyMaterial = 4;
const byte relayPin_SealBag = 3;
const byte Gate = 10;

// --- Operation Mode ---
const bool SIMULTANEOUS_MODE = true;
volatile bool teaSensorTriggered = false;
volatile bool honeySensorTriggered = false;
volatile unsigned long lastTeaInterrupt = 0;
volatile unsigned long lastHoneyInterrupt = 0;
const unsigned long debounceDelay = 50;

bool teaRelayRunning = true;
bool honeyRelayRunning = true;

int TeaState = 0;
int HoneyState = 0;

unsigned int count = 0;
unsigned long total_Count = 0;
unsigned int batch = 0;
bool connectionOk = false;
bool sequenceInProgress = false;

// --- Interrupts ---
void teaSensorISR() {
    if (millis() - lastTeaInterrupt > debounceDelay) {
        teaSensorTriggered = true;
        lastTeaInterrupt = millis();
    }
}
void honeySensorISR() {
    if (millis() - lastHoneyInterrupt > debounceDelay) {
        honeySensorTriggered = true;
        lastHoneyInterrupt = millis();
    }
}

// --- Telemetry ---
void publishTelemetry() {
    if (!client.connected()) return;
    StaticJsonDocument<128> doc;
    //doc["count"] = count;
    doc["total_Count"] = total_Count;
    doc["batch"] = batch;
    char telemetry[128];
    serializeJson(doc, telemetry);
    client.publish("v1/devices/me/telemetry", telemetry);
}

// --- MQTT Connect ---
void MQTT_connect() {
    if (client.connected()) return;
    Serial.print("Connecting to ThingsBoard... ");
    uint8_t retries = 3;
    while (!client.connected() && retries > 0) {
        if (client.connect("arduino-client", TOKEN, NULL)) {
            Serial.println("Connected!");
            connectionOk = true;
        } else {
            Serial.print("MQTT Failed, rc = ");
            Serial.println(client.state());
            delay(5000);
            retries--;
        }
    }
    connectionOk = client.connected();
}

// --- Setup ---
void setup() {
    Serial.begin(9600);
    delay(10);
    ledDisplay.begin("Starting...");

    pinMode(irSensorPin_Tea, INPUT_PULLUP);
    pinMode(irSensorPin_Honey, INPUT_PULLUP);
    pinMode(switchPin, INPUT_PULLUP);
    pinMode(relayPin_TeaMaterial, OUTPUT);
    pinMode(relayPin_HoneyMaterial, OUTPUT);
    pinMode(relayPin_SealBag, OUTPUT);
    pinMode(Gate, OUTPUT);

    digitalWrite(relayPin_TeaMaterial, HIGH);
    digitalWrite(relayPin_HoneyMaterial, HIGH);
    digitalWrite(relayPin_SealBag, HIGH);
    digitalWrite(Gate, HIGH);

    attachInterrupt(digitalPinToInterrupt(irSensorPin_Tea), teaSensorISR, FALLING);
    attachInterrupt(digitalPinToInterrupt(irSensorPin_Honey), honeySensorISR, FALLING);

    WiFi.begin(WLAN_SSID, WLAN_PASS);
    int wifi_retries = 0;
    while (WiFi.status() != WL_CONNECTED && wifi_retries < 20) {
        delay(500);
        Serial.print(".");
        wifi_retries++;
    }
    connectionOk = WiFi.status() == WL_CONNECTED;
    if (connectionOk) {
        Serial.println("WiFi connected!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
    }
    client.setServer(THINGSBOARD_SERVER, THINGSBOARD_SERVERPORT);
    Serial.println("Setup complete!");
}

// --- Loop ---
void loop() {
    if (!connectionOk || WiFi.status() != WL_CONNECTED) {
        connectionOk = false;
        ledDisplay.updateScroll("   Connection Failed   ");
        delay(500);
        WiFi.begin(WLAN_SSID, WLAN_PASS);
        return;
    }
    client.loop();
    MQTT_connect();
    if (!client.connected()) {
        ledDisplay.updateScroll("   MQTT Failed   ");
        return;
    }
    ledDisplay.clear();

    if (!sequenceInProgress) {
        sequenceInProgress = true;
        Serial.println("Starting material dispensing sequence...");

        teaRelayRunning = (TeaState == 0);
        honeyRelayRunning = (HoneyState == 0);

        // Step 1: Activate only relays that still need to run
        if (teaRelayRunning) digitalWrite(relayPin_TeaMaterial, LOW);
        if (honeyRelayRunning) digitalWrite(relayPin_HoneyMaterial, LOW);
        delay(500); // run for 1 second

        // Step 2: Turn off any that triggered during that second
        if (teaSensorTriggered && teaRelayRunning) {
            digitalWrite(relayPin_TeaMaterial, HIGH);
            teaRelayRunning = false;
            TeaState = 1;
            Serial.println("✅ Tea detected during initial run - relay OFF");
        }
        if (honeySensorTriggered && honeyRelayRunning) {
            digitalWrite(relayPin_HoneyMaterial, HIGH);
            honeyRelayRunning = false;
            HoneyState = 1;
            Serial.println("✅ Honey detected during initial run - relay OFF");
        }

        // Step 3: Monitor until both detected or timeout
        unsigned long startTime = millis();
        unsigned long timeout = 5000;
        while ((TeaState == 0 || HoneyState == 0) && (millis() - startTime < timeout)) {
            if (teaSensorTriggered && teaRelayRunning) {
                digitalWrite(relayPin_TeaMaterial, HIGH);
                teaRelayRunning = false;
                TeaState = 1;
                Serial.println("✅ Tea detected - relay OFF");
            }
            if (honeySensorTriggered && honeyRelayRunning) {
                digitalWrite(relayPin_HoneyMaterial, HIGH);
                honeyRelayRunning = false;
                HoneyState = 1;
                Serial.println("✅ Honey detected - relay OFF");
            }
            delay(10);
        }

        // Step 4: Safety shutoff
        if (teaRelayRunning) {
            digitalWrite(relayPin_TeaMaterial, HIGH);
            Serial.println("❌ Tea not detected — relay OFF after timeout");
        }
        if (honeyRelayRunning) {
            digitalWrite(relayPin_HoneyMaterial, HIGH);
            Serial.println("❌ Honey not detected — relay OFF after timeout");
        }

        // Step 5: Seal if both detected
        if (TeaState == 1 && HoneyState == 1) {
            Serial.println("Both materials confirmed - sealing bag...");
            digitalWrite(relayPin_SealBag, LOW);
            delay(300);
            digitalWrite(relayPin_SealBag, HIGH);

            ++count;
            ++total_Count;
            publishTelemetry();
            if (count >= 24) {
                count = 0;
                ++batch;
                publishTelemetry();
            }
            // Reset states after sealing
            TeaState = 0;
            HoneyState = 0;
        } else {
            Serial.println("❌ Package incomplete - materials missing");
        }

        teaSensorTriggered = false;
        honeySensorTriggered = false;
        sequenceInProgress = false;
        delay(2500);
    }

    static unsigned long lastDebugPrint = 0;
    if (millis() - lastDebugPrint > 2000) {
        Serial.print("Count: ");
        Serial.print(count);
        Serial.print(" | Total: ");
        Serial.print(total_Count);
        Serial.print(" | Batch: ");
        Serial.print(batch);
        Serial.print(" | TeaState: ");
        Serial.print(TeaState);
        Serial.print(" | HoneyState: ");
        Serial.println(HoneyState);
        lastDebugPrint = millis();
    }

    delay(50); // small loop delay
}
