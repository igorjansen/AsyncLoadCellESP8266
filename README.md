# AsyncLoadCellESP8266
Rocket motor test stand with a web interface, using ESP8266, HX711 and Arduino

Main program is AsyncLoadCellESP8266.ino, which uses the ESPAsyncWebServer (hosted by me-no-dev) and modified HX711 library for ESP8266 (read timing problem resolved on ESP8266, original library by bogde).

Compile and upload to ESP8266. You have to properly connect the HX711 sensor to the ESP8266 or for testing at least XXXXXXXXXXXXXXXXX
The data are stored on ESP8266 in SPIFFS as a .cvs file 
