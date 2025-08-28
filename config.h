// config.h

#pragma once // Prevents the file from being included multiple times

// --- Machine Identification ---
// Change this for each new machine, e.g., "machine_1", "machine_2"
const char* MACHINE_ID = "machine_1";

// --- WiFi Credentials ---
const char* WLAN_SSID = "- - -";
const char* WLAN_PASS = "- - -";

// --- ThingsBoard Configuration ---
const char* THINGSBOARD_SERVER = " - - -";
const int   THINGSBOARD_SERVERPORT = 1883; // Default MQTT port
// ❗️ IMPORTANT: Get this unique token from your ThingsBoard device dashboard
const char* THINGSBOARD_TOKEN = "- - -";
