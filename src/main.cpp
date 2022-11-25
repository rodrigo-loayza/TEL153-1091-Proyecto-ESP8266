#include <cstdlib>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <base64.h>
#include <bearssl/bearssl.h>
#include <bearssl/bearssl_hmac.h>
#include <libb64/cdecode.h>

#include <az_core.h>
#include <az_iot.h>
#include <azure_ca.h>

#include "iot_configs.h"
#include "DHT.h"

#define MESSAGE_MAX_LEN 256
#define ESP32_LED 2
#define DHT_PIN 4
#define DHT_TYPE DHT11
#define AM312_PIN 5

#define AZURE_SDK_CLIENT_USER_AGENT "c%2F" AZ_SDK_VERSION_STRING "(ard;esp8266)"
#define NTP_SERVERS "pool.ntp.org", "time.nist.gov"
#define MQTT_PACKET_SIZE 1024

int motionVal = 0;
int motionCount = 0;
int motionDetected = 0;
int noMotionDetected = 0;
DHT dht(DHT_PIN, DHT_TYPE);
float temperature;
float lastTempMes = 0;
char timestamp[80];
const char *messageData = "{\"timestamp\": \"%s\", \"temperature\": %f, \"presence\": %s}";

static const char *ssid = IOT_CONFIG_WIFI_SSID;
static const char *password = IOT_CONFIG_WIFI_PASSWORD;
static const char *host = IOT_CONFIG_IOTHUB_FQDN;
static const char *device_id = IOT_CONFIG_DEVICE_ID;
static const int port = 8883;

static WiFiClientSecure wifi_client;
static X509List cert((const char *)ca_pem);
static PubSubClient mqtt_client(wifi_client);
static az_iot_hub_client client;

static void connectToWiFi()
{
  Serial.println();
  Serial.print("Conectando a WIFI: ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.print("\nWiFi conectado, IP: ");
  Serial.println(WiFi.localIP());
}

static void initializeTime()
{
  Serial.print("Configurando el tiempo con SNTP");
  configTime(-5 * 3600, 0, NTP_SERVERS);
  time_t now = time(NULL);
  while (now < 1510592825)
  {
    delay(500);
    Serial.print(".");
    now = time(NULL);
  }
  Serial.println(" Terminado!");
}

static void setCurrentLocalTimeString()
{
  time_t rawNow;
  struct tm *now;
  time(&rawNow);
  now = localtime(&rawNow);
  strftime(timestamp, 80, "%Y-%m-%d %H:%M:%S", now);
}

static void printCurrentTime()
{
  Serial.print("Current time: ");
  setCurrentLocalTimeString();
  Serial.print(timestamp);
}

static void initializeClients()
{
  az_iot_hub_client_options options = az_iot_hub_client_options_default();
  options.user_agent = AZ_SPAN_FROM_STR(AZURE_SDK_CLIENT_USER_AGENT);
  wifi_client.setTrustAnchors(&cert);
  if (az_result_failed(az_iot_hub_client_init(
          &client,
          az_span_create((uint8_t *)host, strlen(host)),
          az_span_create((uint8_t *)device_id, strlen(device_id)),
          &options)))
  {
    Serial.println("Failed initializing Azure IoT Hub client");
    return;
  }
  mqtt_client.setServer(host, port);
}

static int connectToAzureIoTHub()
{
  mqtt_client.setBufferSize(MQTT_PACKET_SIZE);

  while (!mqtt_client.connected())
  {
    Serial.print("Realizando conexión MQTT ... ");
    if (mqtt_client.connect("NodeMCUv3_ESP8266", "iot-hub-proyecto.azure-devices.net/NodeMCUv3_ESP8266/?api-version=2021-04-12", "SharedAccessSignature sr=iot-hub-proyecto.azure-devices.net%2Fdevices%2FNodeMCUv3_ESP8266&sig=S%2BgtdjUrSD%2B2FaPIRmZ6edUD2UAqY0AU3KlMcGsjn34%3D&se=1669797755"))
    {
      Serial.println("conectado.");
    }
    else
    {
      Serial.print("failed, status code =");
      Serial.print(mqtt_client.state());
      Serial.println(". Intentando de nuevo en 5 segundos.");
      delay(5000);
    }
  }
  return 0;
}

static void establishConnection()
{
  connectToWiFi();
  initializeTime();
  printCurrentTime();
  initializeClients();
  connectToAzureIoTHub();
}

void setup()
{
  pinMode(ESP32_LED, OUTPUT);
  pinMode(AM312_PIN, INPUT);
  Serial.begin(115200);
  dht.begin();
  lastTempMes = millis();
  establishConnection();
  delay(2000);
}

void loop()
{
  /* Conexión al IoT Hub usando MQTT */
  if (!mqtt_client.connected())
    establishConnection();

  /* DHT11 sensor reading and obtaining the temperature value */
  if (millis() - lastTempMes >= 2000)
  {
    temperature = dht.readTemperature();
    lastTempMes = millis();
  }

  /* Sensor AM312 para detección de presencia si hay movimiento */
  motionVal = digitalRead(AM312_PIN);
  if (motionVal == HIGH)
    motionDetected++;
  else
    noMotionDetected++;
  motionCount++;

  if (motionCount == 3)
  {
    if (motionDetected > noMotionDetected)
      digitalWrite(ESP32_LED, HIGH); // led se prende
    else
      digitalWrite(ESP32_LED, LOW); // led se apaga

    /* Empaquetamiendo de data y envio*/
    char messagePayload[MESSAGE_MAX_LEN];
    setCurrentLocalTimeString();
    snprintf(messagePayload, MESSAGE_MAX_LEN, messageData, timestamp, temperature, motionDetected > noMotionDetected ? "true" : "false");
    Serial.println(messagePayload);
    mqtt_client.publish("devices/NodeMCUv3_ESP8266/messages/events/", messagePayload, false);

    motionDetected = 0;
    noMotionDetected = 0;
    motionCount = 0;
  }

  mqtt_client.loop();
  delay(250);
}