/*

Here be thee configuration laddie!

*/
#define TEMP_IN_PIN 5 // Pin D1
#define IR_SEND_PIN 14 // Pin D5

#define WIFI_SSID "WIFI-SSID"
#define WIFI_PASS "YOURPASSWORD"
#define MQTT_HOST "YOURMQTTSERVER"
#define MQTT_PORT 1883
#define MQTT_USER "YOURMQTTUSER"
#define MQTT_PASS "YOURMQTTPASS"

#define OTA_PASSWORD "YOUROTAPASSWORD"

#define MQTT_CLIENT_ID "mitsubishiremote"

#define MQTT_AVAILABILITY_TOPIC "heatpump/node/state"

// Status Topics - See climate.yaml for an example of how I have this set up
#define MQTT_CURRENT_TEMP_TOPIC "heatpump/stat/current_temp"
#define MQTT_CURRENT_HUMIDITY_TOPIC "heatpump/stat/current_humidity"
#define MQTT_POWER_TOPIC "heatpump/stat/power"
#define MQTT_MODE_TOPIC "heatpump/stat/mode"
#define MQTT_TEMP_TOPIC "heatpump/stat/temp"
#define MQTT_FAN_TOPIC "heatpump/stat/fan"
#define MQTT_SWING_TOPIC "heatpump/stat/swing"

// Control Topics
#define MQTT_POWER_SET_TOPIC "heatpump/cmnd/power"
#define MQTT_MODE_SET_TOPIC "heatpump/cmnd/mode"
#define MQTT_TEMP_SET_TOPIC "heatpump/cmnd/temp"
#define MQTT_FAN_SET_TOPIC "heatpump/cmnd/fan"
#define MQTT_SWING_SET_TOPIC "heatpump/cmnd/swing"

// HeatPump Code for your Heatpump
#include <MitsubishiHeatpumpIR.h> // CHange this for your particular heatpump
MitsubishiFDHeatpumpIR *heatpump;

/*

And here be where thee configuration ends - Yarr!

*/
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>                   // MQTT
#include "Timer.h"
#include <SimpleDHT.h>
#include <EEPROM.h>

#define TOPSZ                  60           // Max number of characters in topic string
#define MESSZ                  240          // Max number of characters in JSON message string

// Temp Sensor
int pinDHT22 = TEMP_IN_PIN;
SimpleDHT22 dht22;

WiFiClient espClient;               // Wifi Client
PubSubClient mqttClient(espClient);   // MQTT Client

IRSenderBitBang irSender(IR_SEND_PIN);

Timer t;

// Set defaults
uint8_t AC_POWER = POWER_OFF,
        AC_MODE = MODE_AUTO,
        AC_FAN = FAN_AUTO,
        AC_TEMP = 24,
        AC_VSWING = VDIR_AUTO,
        AC_HSWING = HDIR_AUTO,
        AC_SWING = 0;


float AMBIENT_TEMP;
float AMBIENT_HUMIDITY;

int mqttConnectionFailedCount = 0;
bool is_starting=true;

void setup() {
  Serial.begin(115200);
  delay(10);
  Serial.println("Booting");

  heatpump = new MitsubishiFDHeatpumpIR();

  EEPROM.begin(512);
  // Read the EEPROM and restore variables from their previous configuration
  EEPROM.get(1, AC_POWER);
  EEPROM.get(2, AC_MODE);
  EEPROM.get(3, AC_FAN);
  EEPROM.get(4, AC_TEMP);
  EEPROM.get(5, AC_SWING);
  EEPROM.get(6, AMBIENT_TEMP);
  EEPROM.get(7, AMBIENT_HUMIDITY);
  setSwing();

  initWIFI();
  initOTA();

  // A little bit of debug
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Init loop timers
  t.every(5000, fiveLoop);
  t.every(10000, tenLoop);

  initMQTT();
}

void loop() {
  ArduinoOTA.handle();
  mqttClient.loop();
  t.update();
}


void tenLoop() {
  Serial.println("Ping...");
  publishState();
}

// Reconnect to mqtt every 5 seconds if connection is lost
void fiveLoop() {
  // check WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    initWIFI();
  }
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
}

void initMQTT() {
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  publishState();
}

void mqttDataCb(char* topic, byte* data, unsigned int data_len) {

  char svalue[MESSZ];
  char topicBuf[TOPSZ];
  char dataBuf[data_len+1];

  strncpy(topicBuf, topic, sizeof(topicBuf));
  memcpy(dataBuf, data, sizeof(dataBuf));
  dataBuf[sizeof(dataBuf)-1] = 0;

  snprintf_P(svalue, sizeof(svalue), PSTR("RSLT: Receive topic %s, data size %d, data %s"), topicBuf, data_len, dataBuf);
  Serial.println(svalue);

  // Extract command
  //memmove(topicBuf, topicBuf+sizeof(MQTT_POWET_SET_TOPIC)-2, sizeof(topicBuf)-sizeof(MQTT_COMMAND_CHANNEL));

  String payload(dataBuf);

  if (!strcmp(topicBuf, MQTT_POWER_SET_TOPIC)) {
    Serial.print("power ");
    Serial.println(payload);
    if (payload.equalsIgnoreCase("on")) {
        AC_POWER = 1;
    }
    else if (payload.equalsIgnoreCase("off")) {
        AC_POWER = 0;
    }
    EEPROM.put(1, AC_POWER);
  } else if (!strcmp(topicBuf, MQTT_MODE_SET_TOPIC)) {
    Serial.print("mode ");
    Serial.println(payload);
    if (payload.equalsIgnoreCase("cool")) {
        AC_POWER = 1;
        AC_MODE = MODE_COOL;
    }
    else if (payload.equalsIgnoreCase("heat")) {
        AC_POWER = 1;
        AC_MODE = MODE_HEAT;
    }
    else if (payload.equalsIgnoreCase("auto")) {
        AC_POWER = 1;
        AC_MODE = MODE_AUTO;
    }
    else if (payload.equalsIgnoreCase("dry")) {
        AC_POWER = 1;
        AC_MODE = MODE_DRY;
    }
    else if (payload.equalsIgnoreCase("fan_only")) {
        AC_POWER = 1;
        AC_MODE = MODE_FAN;
    }
    else if (payload.equalsIgnoreCase("off")) {
        AC_POWER = 0;
    }
    EEPROM.put(1, AC_POWER);
    EEPROM.put(2, AC_MODE);
  } else if (!strcmp(topicBuf, MQTT_FAN_SET_TOPIC)) {
    Serial.print("fan ");
    Serial.println(payload);
    if(payload.equalsIgnoreCase("auto")) AC_FAN = 0;
    else if(payload.equalsIgnoreCase("min")) AC_FAN = 1;
    else if(payload.equalsIgnoreCase("normal")) AC_FAN = 2;
    else if(payload.equalsIgnoreCase("max")) AC_FAN = 3;
    EEPROM.put(3, AC_FAN);
  } else if (!strcmp(topicBuf, MQTT_TEMP_SET_TOPIC)) {
    Serial.print("temp ");
    Serial.println(payload);
    AC_TEMP = atof(dataBuf);
    EEPROM.put(4, AC_TEMP);
  } else if (!strcmp(topicBuf, MQTT_SWING_SET_TOPIC)) {
    Serial.print("swing ");
    Serial.println(payload);
    if(payload.equalsIgnoreCase("off")) AC_SWING = 0;
    else if(payload.equalsIgnoreCase("on")) AC_SWING = 1;
    setSwing();
    EEPROM.put(5, AC_SWING);
  }
  EEPROM.commit();

  heatpump->send(irSender, AC_POWER, AC_MODE, AC_FAN, AC_TEMP, AC_VSWING, AC_HSWING);
  publishState();
}

void publishState() {
    char message[MESSZ];
    sprintf(
      message,
      "%d",
      AC_TEMP
    );
    mqttClient.publish(MQTT_TEMP_TOPIC, message, true);

    if (AC_FAN == 0) mqttClient.publish(MQTT_FAN_TOPIC, "Auto", true);
    else if(AC_FAN == 1) mqttClient.publish(MQTT_FAN_TOPIC, "Min", true);
    else if(AC_FAN == 2) mqttClient.publish(MQTT_FAN_TOPIC, "Normal", true);
    else if(AC_FAN == 3) mqttClient.publish(MQTT_FAN_TOPIC, "Max", true);

    if (AC_POWER == 0)
    {
        mqttClient.publish(MQTT_POWER_TOPIC, "Off", true);
        mqttClient.publish(MQTT_MODE_TOPIC, "off", true);
    }
    else
    {
        mqttClient.publish(MQTT_POWER_TOPIC, "On", true);

        if (AC_MODE == MODE_DRY) mqttClient.publish(MQTT_MODE_TOPIC, "dry", true);
        else if (AC_MODE == MODE_COOL) mqttClient.publish(MQTT_MODE_TOPIC, "cool", true);
        else if (AC_MODE == MODE_HEAT) mqttClient.publish(MQTT_MODE_TOPIC, "heat", true);
        else if (AC_MODE == MODE_AUTO) mqttClient.publish(MQTT_MODE_TOPIC, "auto", true);
        else if (AC_MODE == MODE_FAN) mqttClient.publish(MQTT_MODE_TOPIC, "fan_only", true);
    }

    if (AC_SWING == 0) mqttClient.publish(MQTT_SWING_TOPIC, "Off", true);
    else if (AC_SWING == 1) mqttClient.publish(MQTT_SWING_TOPIC, "On", true);

    int err = SimpleDHTErrSuccess;
    if ((err = dht22.read2(pinDHT22, &AMBIENT_TEMP, &AMBIENT_HUMIDITY, NULL)) != SimpleDHTErrSuccess) {
        Serial.print("Read DHT22 failed, err=");
        Serial.println(err);
    } else {
        AMBIENT_TEMP = round(AMBIENT_TEMP*10)/10.0;
        AMBIENT_HUMIDITY = round(AMBIENT_HUMIDITY);

        sprintf(
            message,
            "%.1f",
            AMBIENT_TEMP
        );
        mqttClient.publish(MQTT_CURRENT_TEMP_TOPIC, message, true);
        sprintf(
            message,
            "%.0f",
            AMBIENT_HUMIDITY
        );
        mqttClient.publish(MQTT_CURRENT_HUMIDITY_TOPIC, message, true);
    }
    Serial.println("Power = " + (String)AC_POWER);
    Serial.println("Mode = " + (String)AC_MODE);
    Serial.println("Fan = " + (String)AC_FAN);
    Serial.println("Temp = " + (String)AC_TEMP);

    Serial.println("VDir = " + (String)AC_VSWING);
    Serial.println("HDir = " + (String)AC_HSWING);

    Serial.println("Ambient Temp = " + (String)AMBIENT_TEMP);
    Serial.println("Ambient Hum = " + (String)AMBIENT_HUMIDITY);
}

void reconnectMQTT() {
  // Loop until we're reconnected
  Serial.println("Attempting MQTT connection...");
  // Attempt to connect
  //if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS, mqttDiscoBinaryStateTopic.c_str(), 1, 1, "OFF")
  if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS, MQTT_AVAILABILITY_TOPIC, 1, 1, "Offline")) {
    Serial.println("connected");
    mqttClient.setCallback(mqttDataCb);
    mqttClient.subscribe(MQTT_POWER_SET_TOPIC);
    mqttClient.subscribe(MQTT_MODE_SET_TOPIC);
    mqttClient.subscribe(MQTT_TEMP_SET_TOPIC);
    mqttClient.subscribe(MQTT_FAN_SET_TOPIC);
    mqttClient.subscribe(MQTT_SWING_SET_TOPIC);
    mqttClient.publish(MQTT_AVAILABILITY_TOPIC, "Online", true);
    mqttConnectionFailedCount = 0;

    if (is_starting) {
        heatpump->send(irSender, AC_POWER, AC_MODE, AC_FAN, AC_TEMP, AC_VSWING, AC_HSWING);
        is_starting=false;
    }
  } else {
    Serial.print("failed, rc=");
    Serial.println(mqttClient.state());
    mqttConnectionFailedCount++;

    // Unable to connect to MQTT after 10 attempts - Reboot the ESP
    if (mqttConnectionFailedCount >= 10) {
        delay(5000);
        ESP.restart();
    }
  }
}



void initOTA() {
  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname("HeatpumpController");

  // No authentication by default
  ArduinoOTA.setPassword(OTA_PASSWORD);

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
}


void initWIFI() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }
}

void setSwing() {
    if (AC_SWING == 1) {
        AC_VSWING = VDIR_AUTO;
        AC_HSWING = HDIR_AUTO;
    } else if (AC_SWING == 0) {
        AC_VSWING = VDIR_MUP;
        AC_HSWING = HDIR_RIGHT;
    }
}