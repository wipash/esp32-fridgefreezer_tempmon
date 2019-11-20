#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#include <Wire.h>
#include <SSD1306Wire.h>

// Azure
#include <WiFi.h>
#include "AzureIotHub.h"
#include "Esp32MQTTClient.h"
#define DEVICE_ID "esp32-1"
#define MESSAGE_MAX_LEN 256

// Please input the SSID and password of WiFi
const char *ssid = "";
const char *password = "";

static const char *connectionString = "";
const char *messageData = "{\"deviceId\":\"%s\", \"FridgeTemp\":%f, \"FridgeHumidity\":%f, \"FreezerTemp\":%f, \"FreezerHumidity\":%f}";

int messageCount = 1;
static bool hasWifi = false;
static bool messageSending = true;
static uint64_t send_interval_ms;

#define FRIDGEPIN 17  // Digital pin connected to the fridge DHT sensor
#define FREEZERPIN 16 // Digital pin connected to the freezer DHT sensor

#define DHTTYPE DHT11 // DHT 11

// Top left corner of graph
#define POS_X_GRAPH 5
#define POS_Y_GRAPH 0
#define HEIGHT_GRAPH 47
#define WIDTH_GRAPH 128

// Where to start drawing data
#define POS_X_DATA 5
#define POS_Y_DATA 48

#define TEMP_MIN -20
#define TEMP_MAX 10

// See guide for details on sensor wiring and usage:
//   https://learn.adafruit.com/dht/overview

DHT_Unified fridgedht(FRIDGEPIN, DHTTYPE);
DHT_Unified freezerdht(FREEZERPIN, DHTTYPE);

uint32_t delayMS;

SSD1306Wire display(0x3c, 21, 22);

int currentReading = 1;

void printDisplayMessage(int line, const char *displayMessage)
{
    display.clear();
    display.drawStringMaxWidth(0, line * 10, 120, displayMessage);
    display.display();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////
// Utilities
static void InitWifi()
{
    Serial.println("Connecting...");
    printDisplayMessage(0, "Connecting WiFi...");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    hasWifi = true;
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    printDisplayMessage(1, "WiFi connected");
    printDisplayMessage(2, "IP address: ");
    printDisplayMessage(3, String(WiFi.localIP()).c_str());
}

static void SendConfirmationCallback(IOTHUB_CLIENT_CONFIRMATION_RESULT result)
{
    if (result == IOTHUB_CLIENT_CONFIRMATION_OK)
    {
        Serial.println("Send Confirmation Callback finished.");
    }
}

static void MessageCallback(const char *payLoad, int size)
{
    Serial.println("Message callback:");
    Serial.println(payLoad);
}

static void DeviceTwinCallback(DEVICE_TWIN_UPDATE_STATE updateState, const unsigned char *payLoad, int size)
{
    char *temp = (char *)malloc(size + 1);
    if (temp == NULL)
    {
        return;
    }
    memcpy(temp, payLoad, size);
    temp[size] = '\0';
    // Display Twin message.
    Serial.println(temp);
    free(temp);
}

static int DeviceMethodCallback(const char *methodName, const unsigned char *payload, int size, unsigned char **response, int *response_size)
{
    LogInfo("Try to invoke method %s", methodName);
    const char *responseMessage = "\"Successfully invoke device method\"";
    int result = 200;

    if (strcmp(methodName, "start") == 0)
    {
        LogInfo("Start sending temperature and humidity data");
        messageSending = true;
    }
    else if (strcmp(methodName, "stop") == 0)
    {
        LogInfo("Stop sending temperature and humidity data");
        messageSending = false;
    }
    else
    {
        LogInfo("No method %s found", methodName);
        responseMessage = "\"No method found\"";
        result = 404;
    }

    *response_size = strlen(responseMessage) + 1;
    *response = (unsigned char *)strdup(responseMessage);

    return result;
}

// 47 is the last blue pixel
void drawEmptyGraph()
{

    // Find where zero is on the graph
    int zeroLine = map(0, TEMP_MIN, TEMP_MAX, 0, HEIGHT_GRAPH);

    // Clear graph area
    display.setColor(BLACK);
    display.fillRect(POS_X_DATA, POS_Y_DATA, 128, 16);
    display.setColor(WHITE);

    display.clear();

    display.drawHorizontalLine(POS_X_GRAPH, HEIGHT_GRAPH, WIDTH_GRAPH);
    display.drawHorizontalLine(POS_X_GRAPH - 2, HEIGHT_GRAPH - zeroLine, 5);
    display.drawVerticalLine(POS_X_GRAPH, POS_Y_GRAPH, HEIGHT_GRAPH);
    display.display();
}

void setup()
{
    Serial.begin(115200);

    // LCD Stuff
    display.init();
    printDisplayMessage(0, "Initializing...");

    // Initialize the WiFi module
    Serial.println(" > WiFi");

    hasWifi = false;
    InitWifi();
    if (!hasWifi)
    {
        return;
    }

    randomSeed(analogRead(0));

    Serial.println(" > IoT Hub");
    printDisplayMessage(0, "Connecting IoT Hub");

    Esp32MQTTClient_SetOption(OPTION_MINI_SOLUTION_NAME, "FridgeFreezerMonitor");
    Esp32MQTTClient_Init((const uint8_t *)connectionString, true);

    Esp32MQTTClient_SetSendConfirmationCallback(SendConfirmationCallback);
    Esp32MQTTClient_SetMessageCallback(MessageCallback);
    Esp32MQTTClient_SetDeviceTwinCallback(DeviceTwinCallback);
    Esp32MQTTClient_SetDeviceMethodCallback(DeviceMethodCallback);

    send_interval_ms = millis();

    // Initialize device.
    fridgedht.begin();
    freezerdht.begin();

    // Set delay between sensor readings based on sensor details.
    delayMS = 2000;

    // LCD Stuff
    display.clear();
    printDisplayMessage(0, "Initialization Complete");

    display.clear();
    drawEmptyGraph();
}

void loop()
{
    // Delay between measurements.
    delay(delayMS);

    sensors_event_t event;

    float fridgetemp;
    float fridgehumidity;
    float freezertemp;
    float freezerhumidity;

    fridgedht.temperature().getEvent(&event);
    if (isnan(event.temperature))
    {
        fridgetemp = 0.0;
    }
    else
    {
        fridgetemp = event.temperature;
    }

    fridgedht.humidity().getEvent(&event);
    if (isnan(event.relative_humidity))
    {
        fridgehumidity = 0.0;
    }
    else
    {
        fridgehumidity = event.relative_humidity;
    }

    freezerdht.temperature().getEvent(&event);
    if (isnan(event.temperature))
    {
        freezertemp = 0.0;
    }
    else
    {
        freezertemp = event.temperature;
    }

    freezerdht.humidity().getEvent(&event);
    if (isnan(event.relative_humidity))
    {
        freezerhumidity = 0.0;
    }
    else
    {
        freezerhumidity = event.relative_humidity;
    }

    display.setColor(BLACK);
    display.fillRect(POS_X_DATA, POS_Y_DATA, 128, 16);
    display.setColor(WHITE);
    String fridgeTempString = "Fr: " + String(fridgetemp) + "°C";
    String freezerTempString = "Fz: " + String(freezertemp) + "°C";
    display.drawString(POS_X_DATA, POS_Y_DATA, fridgeTempString);
    display.drawString(WIDTH_GRAPH / 2, POS_Y_DATA, freezerTempString);

    int fridgeTempMap = map(fridgetemp, TEMP_MIN, TEMP_MAX, 0, HEIGHT_GRAPH);
    int freezerTempMap = map(freezertemp, TEMP_MIN, TEMP_MAX, 0, HEIGHT_GRAPH);

    display.setPixel(POS_X_GRAPH + currentReading, HEIGHT_GRAPH - fridgeTempMap);
    display.setPixel(POS_X_GRAPH + currentReading, HEIGHT_GRAPH - freezerTempMap);

    currentReading += 1;

    if (currentReading == WIDTH_GRAPH)
    {
        drawEmptyGraph();
        currentReading = 0;
    }

    display.display();

    if (hasWifi)
    {
        if (messageSending)
        {
            // Send teperature data
            char messagePayload[MESSAGE_MAX_LEN];
            snprintf(messagePayload, MESSAGE_MAX_LEN, messageData, DEVICE_ID, fridgetemp, fridgehumidity, freezertemp, freezerhumidity);
            Serial.println(messagePayload);
            EVENT_INSTANCE *message = Esp32MQTTClient_Event_Generate(messagePayload, MESSAGE);
            Esp32MQTTClient_Event_AddProp(message, "temperatureAlert", "true");
            Esp32MQTTClient_SendEventInstance(message);

            send_interval_ms = millis();
        }
        else
        {
            Esp32MQTTClient_Check();
        }
    }
}
