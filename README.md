# AsyncLoadCellESP8266
Rocket motor test stand with a web interface, using ESP8266, HX711 and Arduino

Main program is AsyncLoadCellESP8266.ino, which uses the ESPAsyncWebServer (hosted by me-no-dev) and modified HX711 library for ESP8266 (scale read timing problem resolved on ESP8266, original library by bogde).

Compile and upload to ESP8266. You have to properly connect the HX711 sensor to the ESP8266 or for testing at least connect the data pin on controller (default D5 on my Nodemcu v3) to ground.

The ESP starts a softAP named "ESP", connect to it and go to the default address with your web browser (shown on serial, normally 192.168.4.1). The web page should appear and you have the options to Tare the scale (should be pressed in real situation, Tare is not done automatically), calibrate the scale with known weight, calibrate with the known scale factor, read specified number of test data and "FireInTheHole" read = fire the motor by putting the specified FirePin HIGH and read specified number of test data (security question/number is required before firing). 

The read data (from TestRead or FireInTheHole read) are stored on ESP8266 in SPIFFS as a .csv file, a graph is generated and shown on the web page. These files can be accessed using the included SPIFFS editor, go to /edit in your browser (e.g. 192.168.4.1/edit) and put the user/password.

Good luck!
