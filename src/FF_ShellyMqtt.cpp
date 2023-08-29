#define VERSION "23.6.9-1"

/*
    FF_ShellyMqtt   Integrates Shelly 1PM as switch in an MQTT environment

    This code manages a button and a relay (inside a Shelly 1PM module but can be anything else)

    Functions are as follow:
      - Manages locally relay giving actions on the button
      - Updates MQTT state topic giving relay state
      - Updates relay state giving MQTT state topic
      - Optionally implements a timer to reset relay
      - When MQTT reconnects, ignore the received message and send back current state (as relay may have been toggled while MQTT offline)

    Lot of things are driven by parameters set in FF_ShellyMqtt.c file. Please have a look to it. Here are the main:
      - Traces are sent to serial (should you decide to test code on a "classical" ESP8266 if you define SERIAL_TRACE,
      - Traces are sent to SYSLOG if you define SYSLOG_HOST (and not SERIAL_TRACE),
      - Else traces are not generated,
      - You may define SHADOW_LED_PIN to visualize internal state on a LED,
      - You should define button level change(s) that will trigger an internal state change (BUTTON_LOW_TO_HIGH or BUTTON_HIGH_TO_LOW
          for a push button, both for a switch)
      - You may write stats to trace defining STATS_INTERVAL,
      - You may send periodically internal temperature to MQTT defining TEMPERATURE_TOPIC


    Written by Flying Domotic (https://github.com/FlyingDomotic/)

    V23.6.9-1  Initial version

*/

//  Settings and parameters
#include "FF_ShellyMqtt.h"

// Wifi Connect event
void onWifiConnect(const WiFiEventStationModeConnected& event) {
  if (lastDisconnect) {                                     // Have we already been disconnected?
    TRACE("Wifi reconnected after %lu ms", millis()-lastDisconnect);
    networkLost++;                                          // Increment network disconnexion count
    mqttCommandFailed = true;                               // To force current MQTT status to be resent when connected
  } else {
    TRACE("Wifi connected at %lu ms", millis());
  }
}

// Wifi Disconnect event
void onWifiDisconnect(const WiFiEventStationModeDisconnected& event) {
  TRACE("Wifi disconnected!");
  // Save last diconnection time
  lastDisconnect = millis();
}

// Wifi got IP event
void onWifiGotIP(const WiFiEventStationModeGotIP& event) {
  TRACE("Wifi got IP %s", WiFi.localIP().toString().c_str());
}

// Send a command to MQTT
void mqttSendCommand(const bool newState) {
  #ifdef MQTT_STATE
    TRACE("Sending %s to %s", newState ? SET_ON : SET_OFF, MQTT_STATE);
    // Send MQTT command (either On or Off)
    mqttClient.publish(MQTT_STATE, newState ? SET_ON : SET_OFF, true);
  #endif
}

// Callback activated when a subscribed event is received
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  char message[lenght + 1];
  // Copy the payload to the message buffer
  memcpy(message, payload, lenght);
  // End string
  message[lenght] = 0;
  TRACE("Got %s on topic %s", message, topic);
  // If last command failed, ignore received message and resend internal relay state
  if (mqttCommandFailed) {
    TRACE("Recovering from failure, sending %s", relayOn ? "ON" : "OFF");
    // Update stats
    syncLost++;
    // Resend relay state
    mqttSendCommand(relayOn);
    // Command is ok (for now)
    mqttCommandFailed = false;
  } else {
    if (strstr(message, STATE_ON)) {
      // We received an ON request
      TRACE("ON requested");
      // Set internal relay state
      setRelayOn(true);
    } else if (strstr(message, STATE_OFF)) {
      // We received an OFF request
      TRACE("OFF requested");
      // Set internal relay state
      setRelayOn(false);
    }
  }
}

// MQTT (re)connection routine
boolean mqttReconnect() {
  char mqttId[50];

  // Define MQTT client ID as program name followed by chip ID on 6 hex chars
  snprintf_P(mqttId, sizeof(mqttId), PSTR("%s_%x"), QUOTE(PROG_NAME), ESP.getChipId());
  // Connect to MQTT with LWT
  if (mqttClient.connect(mqttId, MQTT_USER, MQTT_KEY, MQTT_LWT, 0, true, MQTT_WILL_DOWN_MSG)) {
    TRACE("MQTT connected as %s", mqttId);
    // Tell we're back (LWT up message)
    mqttClient.publish(MQTT_LWT, MQTT_WILL_UP_MSG);
    // Subscribe to state topic
    #ifdef MQTT_SET
      TRACE("Subscribing to %s", MQTT_SET);
      mqttClient.subscribe(MQTT_SET);
    #endif  
  }
  return mqttClient.connected();
}

// MQTT loop
void mqttLoop() {
   // Is MQTT connected?
  if (!mqttClient.connected()) {
    // No, give message and attempt to reconnect every 5 sec
    if (mqttAvailable) {
      TRACE("MQTT disconnected!");
      // Update stats
      mqttLost++;
      // Set not connected
      mqttAvailable = false;
    }
    unsigned long now = millis();
    // Last attempt older than 5 sec ?
    if (now - lastMqttConnectAttempt > 5000) {
      lastMqttConnectAttempt = now;
      // Attempt to reconnect
      if (mqttReconnect()) {
        // Clear last MQTT (re) connect attempt
        lastMqttConnectAttempt = 0;
      // Set MQTT connected flag
      mqttAvailable = true;
      }
    }
  } else {
    // Set MQTT available flag
    mqttAvailable = true;
    // MQTT connected, do loop
    mqttClient.loop();
  }
}

// Set local relay state
bool setRelayOn(const bool newState) {
  // Should we change internal relay state?
  if (relayOn != newState) {
    // Save new state
    relayOn = newState;
    // Set relay on time
    if (relayOn) {
      lastRelayOn = millis();
    }
    // Set new state
    digitalWrite(RELAY_PIN, relayOn ? RELAY_ON : RELAY_OFF);
    // Should we have a shadow LMED, update it
    #ifdef SHADOW_LED_PIN
      digitalWrite(SHADOW_LED_PIN, relayOn ? SHADOW_LED_ON : SHADOW_LED_OFF);
    #endif
    // Send MQTT toggle command
    mqttSendCommand(relayOn);
    TRACE("Setting relay to %s", relayOn ? "ON" : "OFF");
    // Something changed
    return true;
  }
  // No change
  return false;
}

// Loop for button
void buttonLoop() {
  // Does button change?
  if (debouncer.update()) {
    bool toggleSwitch = false;
    #ifdef BUTTON_HIGH_TO_LOW
      toggleSwitch = toggleSwitch || (debouncer.read() == LOW);
   #endif
    #ifdef BUTTON_LOW_TO_HIGH
      toggleSwitch = toggleSwitch || (debouncer.read() == HIGH);
    #endif
    #ifndef BUTTON_HIGH_TO_LOW
      #ifndef BUTTON_LOW_TO_HIGH
        #error "You should define BUTTON_HIGH_TO_LOW and/or BUTTON_LOW_TO_HIGH"
      #endif
    #endif
    if (toggleSwitch) {
      // Update stats
      pushCount++;
      // Toggle relay state
      setRelayOn(!relayOn);
      TRACE("Button pushed, relay state is now %s", relayOn ? "ON" : "OFF");
    }
  }
}

// Manage relay timeout
void manageRelayTimeout() {
  #ifdef RELAY_ON_TIME
    if (lastRelayOn) {
      if ((millis() - lastRelayOn) > RELAY_ON_TIME) {
        TRACE("Timeout after %d seconds", RELAY_ON_TIME / 1000);
        setRelayOn(false);
        lastRelayOn = 0;
      }
    }
  #endif
}

#ifdef STATS_INTERVAL
void statsLoop() {
  unsigned long now = millis();
  
  // Where last stats display older than required?
  if ((now - lastStats) > STATS_INTERVAL) {
    // Save last stats time
    lastStats = now;
    char buffer[100];
    // Build stats
    snprintf_P(buffer, sizeof(buffer), 
      PSTR("Stats: networkLost %d, mqttLost %d, syncLost %d, pushCount %d"), 
        networkLost, mqttLost, syncLost, pushCount);
    TRACE(buffer);
  }
}
#endif

#ifdef TEMPERATURE_TOPIC
  // Shelly specific routines
  #include <float.h>
  double TaylorLog(double x) {
    if (x <= 0.0) { return NAN; }
    if (x == 1.0) { return 0; }
    double z = (x + 1) / (x - 1);                              // We start from power -1, to make sure we get the right power in each iteration;
    double step = ((x - 1) * (x - 1)) / ((x + 1) * (x + 1));   // Store step to not have to calculate it each time
    double totalValue = 0;
    double powe = 1;
    for (uint32_t count = 0; count < 10; count++) {            // Experimental number of 10 iterations
      z *= step;
      double y = (1 / powe) * z;
      totalValue = totalValue + y;
      powe = powe + 2;
    }
    totalValue *= 2;
    return totalValue;
  }
    
  // Read Shelly 1PM temperature (truncates to integer)
  int getTemperature() {
    // Should not use analogread to often otherwise the wifi stops working
    // Range: 387 (cold) to 226 (hot)
    int adc = analogRead(A0);
    
    #define ANALOG_NTC_BRIDGE_RESISTANCE  32000            // NTC Voltage bridge resistor
    #define ANALOG_NTC_RESISTANCE         10000            // NTC Resistance
    #define ANALOG_NTC_B_COEFFICIENT      3350             // NTC Beta Coefficient
    // Parameters for equation
    #define TO_CELSIUS(x) ((x) - 273.15)
    #define TO_KELVIN(x) ((x) + 273.15)
    #define ANALOG_V33                    3.3              // ESP8266 Analog voltage
    #define ANALOG_T0                     TO_KELVIN(25.0)  // 25 degrees Celcius in Kelvin (= 298.15)

    // Steinhart-Hart equation for thermistor as temperature sensor
    double Rt = (adc * ANALOG_NTC_BRIDGE_RESISTANCE) / (1024.0 * ANALOG_V33 - (double)adc);
    double BC = (double)ANALOG_NTC_B_COEFFICIENT * 10000 / 10000;
    double T = BC / (BC / ANALOG_T0 + TaylorLog(Rt / (double)ANALOG_NTC_RESISTANCE));
    double temperature = TO_CELSIUS(T);
    return (int)(temperature < 0 ? temperature - 0.5 : temperature + 0.5);
  }

  // Temperature loop
  void temperatureLoop() {
    unsigned long now = millis();
    // Do the job at regular interval
    if ((now - lastTemperatureMillis) > TEMPERATURE_INTERVAL) {
      // Save last temperature date
      lastTemperatureMillis = now;
      // Read temperature
      int temperature = getTemperature();
      // Have we been initialized?
      if (temperatureValid) {
        // Does the temperature change outside limits?
        if (abs(lastTemperature - temperature) >= TEMPERATURE_DELTA) {
          char buffer[100];
          // Buid MQTT message
          snprintf_P(buffer, sizeof(buffer), PSTR("{\"temperature\":%d,\"delta\":%d}"), temperature, temperature - lastTemperature);
          TRACE("Sending %s to %s", buffer, TEMPERATURE_TOPIC);
          // Publish temperature change
          mqttClient.publish(TEMPERATURE_TOPIC, buffer);
          // Save last temperature value
          lastTemperature = temperature;
        }
      } else {
        // Save last temperature value
        lastTemperature = temperature;
        // Set init flag
        temperatureValid = true;
      }
    }
  }
#endif

void setup() {
  #ifdef SERIAL_TRACE
    // Start serial
    Serial.begin(74880);
  #endif

  // Start Wifi
  WiFi.hostname(QUOTE(PROG_NAME));
  WiFi.mode(WIFI_STA);
  static WiFiEventHandler onConnectedHandler = WiFi.onStationModeConnected(onWifiConnect);
  static WiFiEventHandler onDisonnectedHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);
  static WiFiEventHandler onGotIPHandler = WiFi.onStationModeGotIP(onWifiGotIP);
  WiFi.setAutoReconnect(true);
  WiFi.setAutoConnect(false);
  WiFi.begin(WIFI_SSID, WIFI_KEY);

  // Wait up to 10 seconds for network to start
  while ((WiFi.status() != WL_CONNECTED) && (millis() < 10000)) {
    delay(100);
  }

  struct rst_info *rtc_info = system_get_rst_info();

  //  Connect to syslog
  #ifdef SYSLOG_HOST
    //Initialize syslog server
    syslog.server(SYSLOG_HOST, SYSLOG_PORT);
    syslog.deviceHostname(QUOTE(PROG_NAME));
    syslog.defaultPriority(LOG_USER || LOG_DEBUG);
  #endif

  // Hello message
  TRACE("-----------------------------------");
  TRACE("Server %s V%s started (%d) in %lu ms", QUOTE(PROG_NAME), VERSION, rtc_info->reason, millis());

  // Connect to MQTT
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  // Init debouncer
  debouncer = Bounce();
  debouncer.attach(BUTTON_PIN, BUTTON_MODE);
  debouncer.interval(20);    

  // Init relay
  digitalWrite(RELAY_PIN, RELAY_OFF);
  pinMode(RELAY_PIN, OUTPUT);

  // Shadow LED
  #ifdef SHADOW_LED_PIN
  digitalWrite(SHADOW_LED_PIN, SHADOW_LED_OFF);
  pinMode(SHADOW_LED_PIN, OUTPUT);
  #endif

  // Arduino OTA
  ArduinoOTA.setHostname(QUOTE(PROG_NAME));
  // ArduinoOTA.setPassword("admin");

  ArduinoOTA.onStart([]() {
    TRACE("OTA start updating %s", (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem");
  });
  ArduinoOTA.onEnd([]() {
    TRACE("OTA end");
  });
      
  ArduinoOTA.onError([](ota_error_t error) {
    if (error == OTA_AUTH_ERROR) {
      TRACE("OTA error: Auth Failed!");
    } else if (error == OTA_BEGIN_ERROR) {
      TRACE("OTA error: Begin Failed!");
    } else if (error == OTA_CONNECT_ERROR) {
      TRACE("OTA error: Connect Failed!");
    } else if (error == OTA_RECEIVE_ERROR) {
      TRACE("OTA error: Receive Failed!");
    } else if (error == OTA_END_ERROR) {
      TRACE("OTA error: End Failed!");
    } else {
      TRACE("OTA error: unknown code %d", error);
    }
  });
  
  ArduinoOTA.begin();
}

void loop() {
  // Manage MQTT
  mqttLoop();

  // Manage button changes
  buttonLoop();

  // Manages relay timeout
  manageRelayTimeout();

  #ifdef STATS_INTERVAL
    // Manage stats
    statsLoop();
  #endif

  #ifdef TEMPERATURE_TOPIC
    // Manage temperature
    temperatureLoop();
  #endif

    #ifdef SYSLOG_KEEPALIVE
      #ifndef SERIAL_TRACE
        if (millis() - (syslog.lastSyslogMillis) > SYSLOG_KEEPALIVE) {
          syslog.log("Syslog keep alive message");
        }
      #endif
    #endif

  // Manage Arduino OTA
  ArduinoOTA.handle();
}