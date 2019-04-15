#include <SPI.h>
#include <SD.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#define NTP_OFFSET  -21600 // In seconds 
#define NTP_INTERVAL 60 * 1000    // In miliseconds
#define NTP_ADDRESS  "time.apple.com"
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, NTP_ADDRESS, NTP_OFFSET, NTP_INTERVAL);

#include <OneWire.h>
#include <DallasTemperature.h>
#define ONE_WIRE_BUS D1
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);




char *ssid = "your network";
const char *password = "your password";



int sampleFreq = 1000; //in microseconds
bool sampleTemp = true;
bool sampleVolt = false;





ESP8266WebServer server(80);
const int chipSelect = D0;
const int ledPin = D4; //pull low for light
const int recordPin = D3; //pull low with switch
bool recordNow = false;
float voltage;
float seconds;
String dataString;
String timestamp;
String filename;
File uploadFile;
int fileNum = 1;
bool hasSD = false;
bool newfile = false;
long lastTime = 0;
int previous = HIGH; 
float tempC;
long previousMillis = 0;

bool loadFromSdCard(String path) { //send SD data to client
  Serial.println("loadfromSdCard");
  String dataType = "text/plain";
  if (path.endsWith("/")) {
    path += "index.htm";
  }

  if (path.endsWith(".src")) {
    path = path.substring(0, path.lastIndexOf("."));
  } else if (path.endsWith(".htm")) {
    dataType = "text/html";
  } else if (path.endsWith(".css")) {
    dataType = "text/css";
  } else if (path.endsWith(".csv")) {
    dataType = "text/csv";
  } else if (path.endsWith(".js")) {
    dataType = "application/javascript";
  } else if (path.endsWith(".png")) {
    dataType = "image/png";
  } else if (path.endsWith(".gif")) {
    dataType = "image/gif";
  } else if (path.endsWith(".jpg")) {
    dataType = "image/jpeg";
  } else if (path.endsWith(".ico")) {
    dataType = "image/x-icon";
  } else if (path.endsWith(".xml")) {
    dataType = "text/xml";
  } else if (path.endsWith(".pdf")) {
    dataType = "application/pdf";
  } else if (path.endsWith(".zip")) {
    dataType = "application/zip";
  }

  File dataFile = SD.open(path.c_str());
  if (dataFile.isDirectory()) {
    path += "/index.htm";
    dataType = "text/html";
    dataFile = SD.open(path.c_str());
  }

  if (!dataFile) {
    return false;
  }

  if (server.hasArg("download")) {
    dataType = "application/octet-stream";
  }

  if (server.streamFile(dataFile, dataType) != dataFile.size()) {
  //  DBG_OUTPUT_PORT.println("Sent less data than expected!");
  }

  dataFile.close();
  return true;
  }

void newDatalog() { // new session and file?
  bool slot = false;
    for(int x = 1; slot==false; x=x+1) {
      filename = String(fileNum) + String(".csv");
      File dataFile = SD.open(filename);
      if (dataFile.available()) {
        dataFile.close();
        slot = false;
        fileNum = fileNum + 1;
        }
      else{
        slot = true;
        }
      }
  Serial.print("New datalog filename: ");
  Serial.println(filename);

  File cleanFile = SD.open(filename, FILE_WRITE);
      cleanFile.println("temp,uptime");
      Serial.print("wrote new file: ");
      Serial.println(filename);
      cleanFile.close();
      newfile = true;
  }

void handleRoot() { //webpage
  //digitalWrite(led, 1);
  char temp[1200];
  int sec = millis() / 1000;
  int min = sec / 60;
  int hr = min / 60;

  snprintf(temp, 1200,
"<html>\
<head>\
<script src=\"https://cdn.plot.ly/plotly-latest.min.js\"></script>\
<title>Datalogger</title>\
<style>\
body { background-color: white; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
</style>\
</head>\
<body>\
<div id=\"graph\"></div>\
\
<script>\
Plotly.d3.csv(\
\"%d.csv\",\
(err, rows) => {\
function unpack(rows, key) {\
return rows.map(function(row) { return row[key]; });\
}\
var trace1 = {\
type: \"scatter\",\
mode: \"lines\",\
name: \"name\",\
x: unpack(rows, 'uptime'),\
y: unpack(rows, 'temp'),\
line: {shape: 'spline' ,color: 'red', width: 3}\
};\
var data = [trace1];\
var layout = {\
title: 'Datalogger v1',\
xaxis: {\
title: 'timestamp',\
},\
yaxis: {\
title: 'temp',\
rangemode: \"normal\" \
}\
};\
var config = {\
  toImageButtonOptions: {\
    format: 'svg',\
    filename: 'custom_image',\
    height: 1080,\
    width: 1920,\
    scale: 1\
}\
};\
Plotly.newPlot('graph', data, layout, config, {responsive: true, displayModeBar: true, displaylogo: false});\
});\
</script>\
<p>Uptime: %02d:%02d:%02d</p>\
<p>File: %d.csv</p>\
</body>\
</html>",
fileNum, hr, min % 60, sec % 60,fileNum
);
          
  server.send(200, "text/html", temp);
  //  digitalWrite(led, 0);
  }

void handleNotFound() { // file search algorithm
  // digitalWrite(led, 1);
   if (hasSD && loadFromSdCard(server.uri())) {
    return;
  }
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }

  server.send(404, "text/plain", message);
  //  digitalWrite(led, 0);
  }

void setup() {
  // Open serial communications and wait for port to open:
  pinMode(ledPin, OUTPUT); 
  pinMode(recordPin, INPUT); 
  
  Serial.begin(115200);
  sensors.begin();
  Serial.print("Initializing SD card...");

  // see if the card is present and can be initialized:
  if (!SD.begin(chipSelect)) {
    Serial.println("Card failed, or not present");
    hasSD = false;
    //wait
    while (1);
  }
  hasSD = true;
  Serial.println("card initialized.");

  newDatalog();

  // pinMode(led, OUTPUT);
  //  digitalWrite(led, 0);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("");

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  if (MDNS.begin("esp8266")) {
    Serial.println("MDNS responder started");
  }

  server.on("/", handleRoot);
  server.on("/inline", []() {
  server.send(200, "text/plain", "this works as well");
  });
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started");
  }

void loop() {

  status();
  
  if(recordNow == true) {
    record();  
    }

  server.handleClient();

}

void status() { //toggle recording status w/ led

  int debounce = 200;
  int reading = digitalRead(recordPin);  // read the input pin

  if (reading == LOW && previous == HIGH && millis() - lastTime > debounce) {
    if (recordNow == HIGH) {
      recordNow = LOW;
    }
    else {
      recordNow = HIGH;
    }
    lastTime = millis();    
  }

  previous = reading;
  digitalWrite(ledPin, !recordNow);  
}

void sample() { //analog reading
  int probe = analogRead(A0);
  voltage = abs(map(probe, 8, 1023, 0, 22300)); //100k & 220k voltage divider stock +2Mohm to 220k (23.2v MAX-23200)
  voltage = voltage/1000.0;
    
  // float sec = (millis() / 1000.0);
  //int min = (sec / 60);
  //int hr = (min / 60);
  
  timeClient.update();
  timestamp = timeClient.getFormattedDate();

  dataString = "";
 //     dataString += String(voltage,1) + ",";
      dataString += String(tempC,1) + ",";
    dataString += String(timestamp);
    //dataString += String(seconds,3) + "s,";
    //dataString += String(temp,1) + "c,";
    Serial.println(dataString);
  }

void getTemp() {
    // call sensors.requestTemperatures() to issue a global temperature 
  // request to all devices on the bus
  //Serial.print("Requesting temperatures...");
  sensors.requestTemperatures(); // Send the command to get temperatures
  //Serial.println("DONE");
  // After we got the temperatures, we can print them here.
  // We use the function ByIndex, and as an example get the temperature from the first sensor only.
  Serial.print("Temperature is: ");
  tempC = sensors.getTempCByIndex(0);
  Serial.println(tempC);  
}

void record() {

  unsigned long currentMillis = millis();
 
  if(currentMillis - previousMillis > sampleFreq) {
    // save the last time you blinked the LED 
    previousMillis = currentMillis; 

    getTemp(); //ds18b20
    sample(); //voltage only


 
  File dataFile = SD.open(filename);
  
  if (dataFile.available()) {
         // Serial.println("found filename");
          dataFile.close();
          File dataFile = SD.open(filename, FILE_WRITE);
          dataFile.println(dataString);
          dataFile.close();
      }
      else{
          Serial.println("no filename");
          dataFile.close();
          File cleanFile = SD.open(filename, FILE_WRITE);
            if (cleanFile && (newfile == false)) {//make a new file
              cleanFile.println("temp,uptime");
              Serial.print("wrote new file: ");
              Serial.println(filename);
              cleanFile.close();
              newfile = true;
            }
            else  //sd missing, try to load something?
            {
              cleanFile.close();
                if (!SD.begin(chipSelect)) {
                  Serial.println("Card failed, or not present");
                  hasSD = false;
                  }
                  else{
                    Serial.println("SD ok,...not sure why file is missing...");
                  }
                  
            }
      
       }
    }
  }








