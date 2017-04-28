## ESPAsyncSDWebServer

This is an adaptation of ESP8266SDWebServer using ESPAsyncWebServer.

It will work right out of the box with an Adafruit feather ESP8266 
and their Adalogger SDcard, or any ESP8266 with an SD card connected to the SPI.
Uses pin 15 for CS, but easily changed in the code.

Requires the latest version of ESPAsyncWebServer which resolves
conflicts between the native support for the ESP SPIFFS file system
and SD.h.

Requires ArduinoJson and ESPAsyncWiFiManager (both available on GitHub).

Includes an editor application that uses the ACE editor and has a file manager
that can be used to upload and download filed to/from the SD as well as delete.

A nice platform to develop JS/Ajax applications or to develop a custom web enabled
ESP program by adding handlers for new request types.

Enjoy.
