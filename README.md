# FF_ShellyMqtt
 Integrates Shelly as switch in an MQTT environment

## What's for?

This code manages a button and a relay (inside a Shelly 1PM module but can be anything else) to light a Milight bulb (but can be anything else, including ZigBee lights), with a local bypass (per WAF requirement).

Functions are as follow:
  - Manages locally relay giving actions on the button
  - Updates MQTT state topic giving relay state
  - Updates relay state giving MQTT state topic
  - Optionally implements a timer to reset relay
  - When MQTT reconnects, ignore the received message and send back current state (as relay may have been toggled while MQTT offline)

## What can be setup?

Lot of things are driven by parameters set in FF_ShellyMqtt.c file. Please have a look to it. Here are the main:
  - Traces are sent to serial (should you decide to test code on a "classical" ESP8266) if you define SERIAL_TRACE,
  - Traces are sent to SYSLOG if you define SYSLOG_HOST (and not SERIAL_TRACE),
  - Else traces are not generated,
  - You may define SHADOW_LED_PIN to visualize internal state on a LED,
  - You should define button level change(s) that will trigger an internal state change (BUTTON_LOW_TO_HIGH or BUTTON_HIGH_TO_LOW for a push button, both for a switch),
  - You may write stats to trace defining STATS_INTERVAL,
  - You may send periodically internal temperature to MQTT defining TEMPERATURE_TOPIC.

## Prerequisites

This codes comes with a PlatformIo configuration file, allowing to setup multiple environments. If you don't want to use PlatformIo, you may use Arduino IDE, but should move lib and src folder to root folder, and add the few defines from platformio.ini to FF_ShellyMqtt.h.

You may also use VSCodium (or VsCode if you're an MS fan) with platformio extension.

## Installation

Follow these steps:
1. Clone repository somewhere on your disk.
```
cd <where_you_want_to_install_it>
git clone https://github.com/FlyingDomotic/FF_ShellyMqtt.git FF_ShellyMqtt
```

2. Copy platformio.ini file from examples to project root folder
```
cd <where_you_installed_FF_ShellyMqtt>
cp ./example/platformio.example ./platformio.ini
```

3. Copy FF_ShellyMqtt.h from examples to /src folder
```
cd <where_you_installed_FF_ShellyMqtt>
cp ./example/FF_ShellyMqtt.example ./src/FF_ShellyMqtt.h
```

4. Adapt these two files to your needs

## Update

1. Go to code folder and pull new version:
```
cd <where_you_installed_FF_ShellyMqtt>
git pull
```

Note: if you did any changes to plugin files and `git pull` command doesn't work for you anymore, you could stash all local changes using
```
git stash
```
or
```
git checkout <modified file>
```

## ** WARNING - DANGER OF DEATH **

** MODULES SHOULD BE PHYSICALLY DISCONNECTED (WIRES REMOVED) FROM POWER BEFORE DOING ANYTHING **.

As told everywhere, Shelly modules are connected to 120V/240V power. This can kill you should you touch anything when module is connected to power. In addition, connecting both power on module and serial on a PC will fry the latest.

## Code download

This code includes Arduino OTA routines. This allows (remote) code update through WiFi connection. As usual, first version would probably need a serial connection to load it, unless module already has OTA code already loaded. In any case, follow mandatory rule of disconnecting wires before using serial connection.
