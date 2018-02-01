#include <ESP8266WiFi.h>
//#include <ESP8266mDNS.h>
#include <FS.h>
//#include <Hash.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFSEditor.h>
#include <HX711.h>

// SKETCH BEGIN
AsyncWebServer server(80);

/*
 D5 data
 D6 I2C clock SCK
 */
#define DEBUG   //If you comment this line, the DPRINT & DPRINTLN lines are defined as blank.
#ifdef DEBUG    //Macros are usually in all capital letters.
  #define DPRINT(...)    Serial.print(__VA_ARGS__)     //DPRINT is a macro, debug print
  #define DPRINTLN(...)  Serial.println(__VA_ARGS__)   //DPRINTLN is a macro, debug print with new line
#else
  #define DPRINT(...)     //now defines a blank line
  #define DPRINTLN(...)   //now defines a blank line
#endif
#define DT_PIN  D5  // Scale I2C data pin  
#define SCK_PIN  D6 // Scale I2C SCL/clock pin

#define LEDPin D1   // Pin for led (e.g. signalling something), not used currently
#define FIREPin D2  // Pin to fire the motor

// Define Events = binary in one Event variable
#define E_Tare            1
#define E_Calibrate       2
#define E_WriteFile       16
#define E_WriteGraph      32
#define E_TestRead        64
#define E_FireInTheHole   128
//#define REG_SET_BIT(_r, _b)  (*(volatile uint32_t*)(_r) |= (_b))  // just to remember how to toggle the bits
//#define REG_CLR_BIT(_r, _b)  (*(volatile uint32_t*)(_r) &= ~(_b)) // just to remember how to clear the bits

#define FireMillis 500       // millis the FirePin is High == fire period
#define CalibrationRepetition 50 // How many times the measurement for calibration is taken (i.e. average out of 50 measurements)
#define BufferSize 800
unsigned long timeBuffer[BufferSize];
//float unitsBuffer  //scale.get_units(); == (read - tareoffset)/scale ==> calculated only when needed
long rawBuffer[BufferSize]; //scale.read()

const char * hostName = "esp";
const char* http_username = "admin";
const char* http_password = "admin";
const char HTML_Root_Refresh[] = R"=-=-=(<meta http-equiv="refresh" content="0;url=/">)=-=-=";

HX711 scale;        // our scale
double KG=1.0;        // calibration weight
double SCALE=5400.0;  // calibration factor
long OFFSET=0;     // offset factor
unsigned int Events = 0;
int Index = 0;
int ValuesToRead = 0;
unsigned long Time0 = 0;
long currentRaw = 1;

unsigned long FileNum=0;
String FileName;
File file; 

String TemplateProcessor(const String& var)
{
  if(var == "EVENTS")
    return String(String(Events)+" Raw "+String(currentRaw)+" Offs "+String(OFFSET)+" File "+String(FileNum));
  if(var == "MKG")
    return String(KG);
  if(var == "SCALE")
    return String(SCALE);  
  return String();
}

void setup(){
  Serial.begin(115200);
  Serial.setDebugOutput(true);

  pinMode(LEDPin, OUTPUT);    //Set the pins as output and LOW just to be sure
  pinMode(FIREPin, OUTPUT);          
  digitalWrite(LEDPin, LOW);  
  digitalWrite(FIREPin, LOW); 
  
  WiFi.hostname(hostName);
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(hostName);

  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());
  
  SPIFFS.begin();

  server.addHandler(new SPIFFSEditor(http_username,http_password));

  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });

// Handle scale specific events  /////////////////////////////////////////////////////////////////
  server.on("/tare", [](AsyncWebServerRequest *request){
    DPRINTLN("Tare event set");
    Events |= E_Tare; 
    request->redirect("/");
  });
  server.on("/calibrate", [](AsyncWebServerRequest *request){
    DPRINTLN("Calibrate event set");
    
    AsyncWebParameter* p = request->getParam("Mkg", true);
    DPRINTLN(p->value().c_str());
    KG=p->value().toFloat();
    DPRINTLN(KG);
    Events |= E_Calibrate; 
    request->redirect("/");
  });
  server.on("/calibfactor", [](AsyncWebServerRequest *request){
    DPRINTLN("Calibfactor");
    float dd;
    AsyncWebParameter* p = request->getParam("CFa", true);
    dd=p->value().toFloat();
    if(dd!=0){scale.set_scale(dd);SCALE=dd;}
    DPRINT("  Arg(CFa)float=");DPRINTLN(dd);
    request->redirect("/");
  });
  server.on("/testread", [](AsyncWebServerRequest *request){
    DPRINTLN("Testread");
    int dd;
    AsyncWebParameter* p = request->getParam("Num", true);
    dd=p->value().toInt();
    if(dd!=0){ValuesToRead=dd;Index = 0;} else return;
    DPRINT("  Arg(Num)int=");DPRINTLN(dd);
    Events |= E_TestRead;
    request->redirect("/");
    Time0=millis();
  });
  server.on("/fireinthehole", [](AsyncWebServerRequest *request){
    DPRINTLN("FireInTheHole");
    int dd,ans;
    AsyncWebParameter* p = request->getParam("ANS", true);
    ans=p->value().toInt();
    if(ans!=42){request->redirect("/");return;} //check correct security answer
    p = request->getParam("Num", true);
    dd=p->value().toInt();
    if(dd!=0){ValuesToRead=dd;Index = 0;} else return;
    DPRINT("  Arg(Num)int=");DPRINTLN(dd);
    Events |= E_FireInTheHole;
    request->redirect("/");
    Time0=millis();
    digitalWrite(FIREPin, HIGH);
    });
  server.on("/update.txt", [](AsyncWebServerRequest *request){
    DPRINTLN("Update");
    request->send(200, "text/plain", String(currentRaw));
    });

// Rest of the handles etc. //////////////////////////////////////////////////////////////////////
  server.serveStatic("/", SPIFFS, "/")
     .setDefaultFile("index.htm")
     .setTemplateProcessor(TemplateProcessor);

  server.onNotFound([](AsyncWebServerRequest *request){
    Serial.printf("NOT_FOUND: ");
    if(request->method() == HTTP_GET)
      Serial.printf("GET");
    else if(request->method() == HTTP_POST)
      Serial.printf("POST");
    else if(request->method() == HTTP_DELETE)
      Serial.printf("DELETE");
    else if(request->method() == HTTP_PUT)
      Serial.printf("PUT");
    else if(request->method() == HTTP_PATCH)
      Serial.printf("PATCH");
    else if(request->method() == HTTP_HEAD)
      Serial.printf("HEAD");
    else if(request->method() == HTTP_OPTIONS)
      Serial.printf("OPTIONS");
    else
      Serial.printf("UNKNOWN");
    Serial.printf(" http://%s%s\n", request->host().c_str(), request->url().c_str());

    if(request->contentLength()){
      Serial.printf("_CONTENT_TYPE: %s\n", request->contentType().c_str());
      Serial.printf("_CONTENT_LENGTH: %u\n", request->contentLength());
    }

    int headers = request->headers();
    int i;
    for(i=0;i<headers;i++){
      AsyncWebHeader* h = request->getHeader(i);
      Serial.printf("_HEADER[%s]: %s\n", h->name().c_str(), h->value().c_str());
    }

    int params = request->params();
    for(i=0;i<params;i++){
      AsyncWebParameter* p = request->getParam(i);
      if(p->isFile()){
        Serial.printf("_FILE[%s]: %s, size: %u\n", p->name().c_str(), p->value().c_str(), p->size());
      } else if(p->isPost()){
        Serial.printf("_POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
      } else {
        Serial.printf("_GET[%s]: %s\n", p->name().c_str(), p->value().c_str());
      }
    }
    request->send(404);
  });
  server.onFileUpload([](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final){
    if(!index)
      Serial.printf("UploadStart: %s\n", filename.c_str());
    Serial.printf("%s", (const char*)data);
    if(final)
      Serial.printf("UploadEnd: %s (%u)\n", filename.c_str(), index+len);
  });
  server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
    if(!index)
      Serial.printf("BodyStart: %u\n", total);
    Serial.printf("%s", (const char*)data);
    if(index + len == total)
      Serial.printf("BodyEnd: %u\n", total);
  });
  server.begin();
  
  scale.begin(DT_PIN, SCK_PIN);
  scale.set_scale(SCALE);
}

double toNewton(long raw)
  {
    return (double)((double)(raw-OFFSET)/SCALE);
  }

void writeDataFile() // writes data into File file, must be openned etc., i.e. does not check...
  {
      int i;
      file.println("Time ; RawData ; NewtonUnits");
      for(i=0;i<ValuesToRead;i++)
        {
          file.printf("%lu ; %ld ; %s \n",timeBuffer[i], rawBuffer[i], String(toNewton(rawBuffer[i])).c_str());
          //delay(0);
        }
      file.flush();  
      file.close();
  }

void writeGraph()
  {
    double maxi = -100000000.0;
    double mini = +100000000.0;
    double scal=1;
    double temp;
    int i, stepSize = 800/ValuesToRead;
    const char * path = "/graph.svg";

    if(SPIFFS.exists(path)) SPIFFS.remove(path);
    
    file = SPIFFS.open(path, "w");
    if(!file) 
      {
         Serial.println("Graph open failed");
         return;
      }
     for(i=0;i<ValuesToRead;i++)
        {
//#define DEBUGGRAPH   // Random numbers into rawBuffer in testing...
#ifdef DEBUGGRAPH    // Random numbers into rawBuffer in testing...
          rawBuffer[i]+=rand()%4000;
#endif
          temp=toNewton(rawBuffer[i]);
          if(temp>maxi){maxi=temp;}
          if(temp<mini){mini=temp;}
        }
      scal= (maxi-mini)/385.0;
      if(scal==0) scal=1;
       
    file.print("<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" width=\"800\" height=\"401\">\n");
    file.print("<rect width=\"800\" height=\"401\" fill=\"rgb(210,222,255)\" stroke-width=\"1\" stroke=\"rgb(150,0,100)\"/>\n");
    file.printf("<text x=\"0\" y=\"15\" fill=\"red\">Max %s N File %lu</text>\n", String(maxi).c_str(), FileNum);
    file.print("<polyline points=\" 0,400\n");
    for (i = 0; i < ValuesToRead; i++) {
          file.printf("%d,%d\n", i*stepSize, 400 - (int)((toNewton(rawBuffer[i])-mini)/scal)); // Numbers for Y should be between +15 and 400, i.e. 385 points.
        }
    file.print("\"\nstyle=\"fill:none;stroke:black;stroke-width:2\"/></svg>\n");
    
    file.flush();  
    file.close();
  }

void loop(){
  int i;
  if(Events&E_WriteGraph)
    {
      writeGraph();
      Events&= ~(unsigned int)E_WriteGraph;
    }
  if(Events&E_WriteFile)
    {
      DPRINTLN("WriteFile START"); 
      digitalWrite(FIREPin, LOW); // just to be sure FIREPin is low == could happen, if reading is faster than FireMillis
      OFFSET=scale.get_offset();
      FileNum=Time0;
      DPRINTLN(Time0);
      DPRINTLN(FileNum);
      while(SPIFFS.exists("/_"+String(FileNum)+".csv")){FileNum++;}
      FileName= "/_"+String(FileNum)+".csv";
      file = SPIFFS.open(FileName, "w");
      if(!file) {
        Serial.println("File open failed");
        Events&= ~(unsigned int)E_WriteFile;
        return;
        }
      else
        {
          DPRINT("File open: "); DPRINTLN(FileName);
          writeDataFile();
        }
      FileName= "/_"+String(FileNum)+".txt";
      file = SPIFFS.open(FileName, "w");
      if(!file) {
        Serial.println("file open failed");
        Events&= ~(unsigned int)E_WriteFile;
        return;
        }
      else
        {
          DPRINT("File open: "); DPRINTLN(FileName);
          writeDataFile();
        }     
      Events&= ~(unsigned int)E_WriteFile;
      Events|= E_WriteGraph;
      DPRINTLN("WriteFile END");
    }
  if(Events&E_Tare)
    {
      DPRINTLN("Tare START"); 
      scale.tare(CalibrationRepetition);
      OFFSET=scale.get_offset(); 
      Events&= ~(unsigned int)E_Tare;
      DPRINTLN("Tare DONE");
    }
  if(Events&E_Calibrate)
    {
     DPRINTLN("Calib START"); 
     float vv,ss;
     vv=scale.get_value(CalibrationRepetition);
     if(KG!=0) {ss=vv/(KG*9.80665f);} else {ss=1;}
     if(ss!=0) {scale.set_scale(ss); SCALE=ss;}
     DPRINT("Average value=");DPRINT(vv);DPRINT("  Scale=");DPRINT(ss);DPRINT("  KG=");DPRINTLN(KG); 
     Events&= ~(unsigned int)E_Calibrate;
     DPRINTLN("Calib DONE");
    };
  if(Events&E_TestRead)
    {
     if(!Index)DPRINTLN("TestRead START"); 
     if(Index<ValuesToRead)
       {
         if(scale.is_ready())
           {
             timeBuffer[Index] = millis();
             rawBuffer[Index] = scale.read();
             currentRaw = rawBuffer[Index];
             Index++;
           }
       }
     else
       {
         Events&= ~(unsigned int)E_TestRead;
         Events|= E_WriteFile;
       }
    }
  if(Events&E_FireInTheHole)
    {
     if(!Index) DPRINTLN("FireInTheHole START"); 
     if(Index<ValuesToRead)
       {
         if(scale.is_ready())
           {
             timeBuffer[Index]= millis();
             rawBuffer[Index]= scale.read();
             currentRaw = rawBuffer[Index];
             Index++;
           }
       }
     else
       {
         Events&= ~(unsigned int)E_FireInTheHole;
         Events|= E_WriteFile;
       }
     if(millis()-Time0>FireMillis) digitalWrite(FIREPin, LOW);
    }  
  if(!Events)
    {
      if(scale.is_ready())
        {
           currentRaw= scale.read();
        }   
    }
}
