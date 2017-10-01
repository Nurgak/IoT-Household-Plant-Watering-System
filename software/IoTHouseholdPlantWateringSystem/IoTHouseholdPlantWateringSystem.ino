/*
  Software for the IoTHouseholdPlantWateringSystem based on the ESP8266
  Copyright (c) 2017 Karl Kangur. All rights reserved.
  This file is part of IoTHouseholdPlantWateringSystem.
  IoTHouseholdPlantWateringSystem is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  IoTHouseholdPlantWateringSystem is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  You should have received a copy of the GNU General Public License
  along with IoTHouseholdPlantWateringSystem.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
 * File:    IoTHouseholdPlantWateringSystem.ino
 * Author:  Karl Kangur <karl.kangur@gmail.com>
 * Licnece: GPL
 * URL:     https://github.com/Nurgak/IoT-Household-Plant-Watering-System
 */

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>

#define SWITCH_PIN 3
#define WIFI_RECONNECT_DELAY_MS 500
#define DUTYCYCLE_MIN 0
#define DUTYCYCLE_MAX 100
// Used EEPROM bytes must be a multiple of 4
#define EEPROM_USED_MEMORY_BYTES 4
#define EEPROM_ADDR_DUTYCYCLE 0
#define EEPROM_ADDR_DURATION 1

const char * ssid = "...";
const char * password = "...";
const char * host_name = "IoTHouseholdPlantWateringSystem";
const char * mqtt_server = "192.168.0.1";
const unsigned int mqtt_port = 1883;

// Define the names for the topics
const char * topic_water = "water";
const char * topic_dutycycle = "dutycycle";
const char * topic_timeout = "timeout";

WiFiClient espClient;
PubSubClient client(espClient);

byte dutyCycle;
unsigned int duration;
long timeout;

void setup()
{
  // Set LED and switch pins to outputs
  pinMode(BUILTIN_LED, OUTPUT);
  pinMode(SWITCH_PIN, OUTPUT);

  // Connect to access point and MQTT broker
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  // Stop the motor when updating
  ArduinoOTA.onStart([]()
  {
    stop();
  });

  // Show update progress on the built-in LED by toggling it
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
  {
    digitalWrite(BUILTIN_LED, !digitalRead(BUILTIN_LED));
  });

  // Turn LED off (HIGH state) when completed
  ArduinoOTA.onEnd([]()
  {
    digitalWrite(BUILTIN_LED, HIGH);
  });

  // Configure over-the-air update
  ArduinoOTA.setHostname(host_name);
  ArduinoOTA.begin();

  // Indicate the number of bytes being used in the EEPROM
  EEPROM.begin(EEPROM_USED_MEMORY_BYTES);
  // Load the configuration from EEPROM
  dutyCycle = loadIntEEPROM<byte>(EEPROM_ADDR_DUTYCYCLE);
  duration = loadIntEEPROM<unsigned int>(EEPROM_ADDR_DURATION);

  // Set duty cycle from 0 to 100 instead of the default 0-1023
  analogWriteRange(DUTYCYCLE_MAX);
}

void setup_wifi()
{
  WiFi.begin(ssid, password);
  while(WiFi.status() != WL_CONNECTED)
  {
    // Blink LED fast while connecting
    digitalWrite(BUILTIN_LED, !digitalRead(BUILTIN_LED));
    delay(250);
  }

  // LED will stay lit after connection
  digitalWrite(BUILTIN_LED, LOW);
}

template<typename T> void saveIntEEPROM(unsigned int address, T value)
{
  for(size_t i = 0; i < sizeof(value); ++i)
  {
    // Write the ith byte to EEPROM
    EEPROM.write(address + i, (value >> (8 * i)) & 0xff);
  }
  EEPROM.commit();
}

template<typename T> T loadIntEEPROM(unsigned int address)
{
  // Initialise return value
  T value = 0;
  for(size_t i = 0; i < sizeof(value); ++i)
  {
    // Read the ith byte from EEPROM to value
    value |= EEPROM.read(address + i) << (8 * i);
  }
  return value;
}

// MQTT callback for all topics
void callback(char* topic, byte* payload, unsigned int length)
{
  // Make sure there is space in the buffer for a terminator character
  if(length >= MQTT_MAX_PACKET_SIZE)
  {
    return;
  }
  // Add a termintor so String() would work properly
  payload[length] = '\0';
  String data = String((char*)payload);
  // Transform payload to an integer
  int temp = data.toInt();
  
  if(strcmp(topic, topic_water) == 0)
  {
    if(temp == 0)
    {
      stop();
    }
    else if(temp == 1 && duration > 0)
    {
      // Turn on the switch
      timeout = millis() + duration;
      analogWrite(SWITCH_PIN, dutyCycle);
    }
  }
  else if(strcmp(topic, topic_dutycycle) == 0)
  {
    // Sanitize data
    if(temp < DUTYCYCLE_MIN)
    {
      temp = DUTYCYCLE_MIN;
    }
    else if(temp > DUTYCYCLE_MAX)
    {
      temp = DUTYCYCLE_MAX;
    }
    dutyCycle = temp;
    // Save configuration to EEPROM
    saveIntEEPROM(EEPROM_ADDR_DUTYCYCLE, dutyCycle);
  }
  else if(strcmp(topic, topic_timeout) == 0)
  {
    // Sanitize data
    duration = temp < 0 ? 0 : temp;
    // Save configuration to EEPROM
    saveIntEEPROM(EEPROM_ADDR_DURATION, duration);
  }

  // Acknowledge message by blinking the built-in LED
  digitalWrite(BUILTIN_LED, !digitalRead(BUILTIN_LED));
}

void stop()
{
  timeout = 0;
  // Must write 0 to cancel the PWM
  analogWrite(SWITCH_PIN, 0);
  // Make sure the switch is off
  digitalWrite(SWITCH_PIN, LOW);
}

void reconnect()
{
  while(!client.connected())
  {
    if(client.connect(host_name))
    {
      // Subscribe to 
      client.subscribe(topic_water);
      client.subscribe(topic_dutycycle);
      client.subscribe(topic_timeout);
    }
    else
    {
      delay(WIFI_RECONNECT_DELAY_MS);
    }
  }
}

void loop()
{
  if(!client.connected())
  {
    reconnect();
  }
  client.loop();

  // Turn off the switch when the timeout is reached, rollover-safe
  if(timeout && duration && (long)(millis() - timeout) >= 0)
  {
    stop();
  }

  // Check if a program update is coming
  ArduinoOTA.handle();
}
