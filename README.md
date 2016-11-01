# esp8266-motion-sensor
This is an abandoned attempt to create a motion sensor using an esp8266 esp-01 module.  The code itself works, and detects motion when GPIO0 is high, and reports it to the MQTT broker, but the set up time from sleep is about 4 seconds, which is too long for a useful motion sensor.

The code is here as an example of how wake the esp8266 from sleep using a GPIO pin, which took a while for me too figure out from the manuals, some may prove useful to someone.  The trick was that it doesn't seem to work on edge transitions.

The code is based on a copy of esp_mqtt_proj from the ESP8266_NONOS_SDK, downloaded from espressif.  This is in turn copied from tuampmt's  [esp_mqtt] (https://github.com/tuanpmt/esp_mqtt ) project.
