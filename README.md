## ESPAsyncSDWebServer

This is an adaptation of ESP8266SDWebServer using ESPAsyncWebServer.

I haven't stressed it very much or benchmarked the speed difference,
but it seems to be really fast.  It will work right out of the box
with an Adafruit feather ESP8266 and their Adalogger SDcard.

Requires the latest version of ESPAsyncWebServer which resolves
conflicts between the native support for the ESP SPIFFS file system
and SD.h.

Although everything runs completely asynchronously, I had to serialize
the download requests in a FiFo because I couldn't figure out a 
simple way to maintain a context in the callback.  Maybe someone 
can point out what I am missing there.

Enjoy.
