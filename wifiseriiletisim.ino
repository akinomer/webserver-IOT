#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include "SPIFFS.h"
#include <EEPROM.h>
#include <ESPmDNS.h>
#include <esp_task_wdt.h>

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

AsyncWebSocketClient * globalClient = NULL;

const char* input_parameter1 = "ssid";
const char* input_parameter2 = "pass";
const char* input_parameter3 = "devicename";

//Variables to save values from HTML form
String ssid;
String pass;
String device_name;

// File paths to save input values permanently
const char* SSID_path = "/ssid.txt";
const char* Password_path = "/pass.txt";
const char* devicename_path = "/devicename.txt";

int flag = 0;
int n = 0;
String returnvalue = "";

const char* host = "esp32sd";

IPAddress localIP;
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 0, 0);

unsigned long previous_time = 0;
const long Delay = 10000;

unsigned long previousMillis = 0;
unsigned long interval = 30000;

void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len){

  if(type == WS_EVT_CONNECT){

    Serial.println("Websocket client connection received");
    globalClient = client;

  } else if(type == WS_EVT_DISCONNECT){

    Serial.println("Websocket client connection finished");
    globalClient = NULL;

  }
}

// Read File from SPIFFS
String readFile(fs::FS &fs, const char * path){
  Serial.printf("Reading file: %s\r\n", path);

  File file = fs.open(path);
  if(!file || file.isDirectory()){
    Serial.println("- failed to open file for reading");
    return String();
  }
  
  String fileContent;
  while(file.available()){
    fileContent = file.readStringUntil('\n');
    break;     
  }
  return fileContent;
}

// Write file to SPIFFS
void writeFile(fs::FS &fs, const char * path, const char * message){
  Serial.printf("Writing file: %s\r\n", path);

  File file = fs.open(path, FILE_WRITE);
  if(!file){
    Serial.println("- failed to open file for writing");
    return;
  }
  if(file.print(message)){
    Serial.println("- file written");
  } else {
    Serial.println("- frite failed");
  }
}

// Initialize WiFi
bool initialize_Wifi() {
  if(ssid==""){
    Serial.println("Undefined SSID");
    return false;
  }

  WiFi.mode(WIFI_STA);
  //localIP.fromString(ip.c_str());

  if (!WiFi.config(localIP, gateway, subnet)){
    Serial.println("STA Failed to configure");
    return false;
  }
  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.println("Connecting to WiFi...");

  unsigned long current_time = millis();
  previous_time = current_time;

  while(WiFi.status() != WL_CONNECTED) {
    current_time = millis();
    if (current_time - previous_time >= Delay) {
      Serial.println("Failed to connect.");
      return false;
    }
  }

  Serial.println(WiFi.localIP());
  return true;
}

void clearEEPROM(){
    for (int i = 0; i < 512; i++) {
     EEPROM.write(i, 0);
     }
    EEPROM.commit();
    delay(10000);
    ESP.restart(); 
}

void setup() {
  Serial.begin(115200);
  //EEPROM.begin(200);
  //clearEEPROM();

  if (!SPIFFS.begin(true)) {
    Serial.println("An error has occurred while mounting SPIFFS");
  }
  Serial.println("SPIFFS mounted successfully");
  
  ssid = readFile(SPIFFS, SSID_path);
  pass = readFile(SPIFFS, Password_path);
  device_name = readFile(SPIFFS, devicename_path);
  Serial.println(ssid);
  Serial.println(pass);
  Serial.println(device_name);

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);


  server.on("/wifistatus", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(200, "text/plain", String(initialize_Wifi()));
  });

  

  if(initialize_Wifi()) {

    File dn = SPIFFS.open("/devicename.txt");
    String dncontent = "";
    while (dn.available()){
      dncontent += (char)dn.read();
    }
    Serial.print(dncontent);
    dn.close();

    if(dncontent!=""){
       host = dncontent.c_str();
    }

    server.on("/sendSerial", HTTP_POST, [](AsyncWebServerRequest *request) {
        int params = request->params();
        for(int i=0;i<params;i++){
          AsyncWebParameter* p = request->getParam(i);
          if(p->isPost()){
              if (p->name() == "data") {
                Serial.write(p->value().c_str());
                request->send(200, "text/plain", "1");
              }
          }
        }
        
    });


    if (MDNS.begin(host)) {
      MDNS.addService("http", "tcp", 80);
      Serial.println("MDNS responder started");
      Serial.print("You can now connect to http://");
      Serial.print(host);
      Serial.println(".local");
    }
    
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(SPIFFS, "/index.html", "text/html");
    });
    server.serveStatic("/", SPIFFS, "/");

    server.on("/wifiinfo", HTTP_GET, [](AsyncWebServerRequest *request) {
        //String wifiinfo = "{\"ssid\":\""+WiFi.SSID()+"\", \"rssi\":"+String(WiFi.RSSI())+", ""ipaddr':'"+ip2Str(WiFi.localIP())+"'}";
        String wifiinfo = "{";
        wifiinfo += "\"ssid\":\""+String(WiFi.SSID())+"\"";
        wifiinfo += ",\"rssi\":\""+String(WiFi.RSSI())+"\"";
        wifiinfo += ",\"ipaddr\":\""+ip2Str(WiFi.localIP())+"\"";
        wifiinfo += "}";
        request->send(200, "text/json", wifiinfo);
        wifiinfo = String();
    });

    server.on("/forgetNetwork", HTTP_GET, [](AsyncWebServerRequest *request) {
        File devicenamefile = SPIFFS.open("/devicename.txt", FILE_WRITE);
        if(devicenamefile.print("")){ Serial.println("File was written"); }
        else { Serial.println("File write failed"); }
        devicenamefile.close();

        File ssidfile = SPIFFS.open("/ssid.txt", FILE_WRITE);
        if(ssidfile.print("")){ Serial.println("File was written"); }
        else { Serial.println("File write failed"); }
        ssidfile.close();

        File passfile = SPIFFS.open("/pass.txt", FILE_WRITE);
        if(passfile.print("")){ Serial.println("File was written"); }
        else { Serial.println("File write failed"); }
        passfile.close();
        
        request->send(200, "text/plain", "1");
        delay(5000);
        ESP.restart(); 
    });
    
    server.begin();
  }
  else {
    Serial.println("Setting Access Point");
    //WiFi.softAP("ESP32-WIFI-MANAGER", NULL);
    WiFi.softAP("ARGESAN-WiFi", NULL);

    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP); 

    // Web Server Root URL
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(SPIFFS, "/index.html", "text/html");
    });
    
    server.serveStatic("/", SPIFFS, "/");

    server.on("/wifiscan", HTTP_GET, [](AsyncWebServerRequest *request) {
        String json = "[";
        int n = WiFi.scanComplete();
        if(n == -2){
          WiFi.scanNetworks(true);
        } else if(n){
          for (int i = 0; i < n; ++i){
            if(i) json += ",";
            json += "{";
            json += "\"quality\":"+String(WiFi.RSSI(i));
            json += ",\"ssid\":\""+WiFi.SSID(i)+"\"";
            json += ",\"bssid\":\""+WiFi.BSSIDstr(i)+"\"";
            json += ",\"channel\":"+String(WiFi.channel(i));
            json += ",\"secure\":"+String(WiFi.encryptionType(i));
            json += "}";
          }
          WiFi.scanDelete();
          if(WiFi.scanComplete() == -2){
            WiFi.scanNetworks(true);
          }
        }
        json += "]";
        request->send(200, "text/json", json);
        json = String();
        
    });
    
    server.on("/connectTo", HTTP_POST, [](AsyncWebServerRequest *request) {
      int params = request->params();
      
      for(int i=0;i<params;i++){
        AsyncWebParameter* p = request->getParam(i);
        if(p->isPost()){
          
          // HTTP POST ssid value
          if (p->name() == input_parameter1) {
            ssid = p->value().c_str();
            Serial.print("SSID set to: ");
            Serial.println(ssid);
            // Write file to save value
            //writeFile(SPIFFS, SSID_path, ssid.c_str());
          }
          // HTTP POST pass value
          if (p->name() == input_parameter2) {
            pass = p->value().c_str();
            Serial.print("Password set to: ");
            Serial.println(pass);
            // Write file to save value
            //writeFile(SPIFFS, Password_path, pass.c_str());
          }
          // HTTP POST ip value
          if (p->name() == input_parameter3) {
            device_name = p->value().c_str();
            Serial.print("Device Name set to: ");
            Serial.println(device_name);
            // Write file to save value
            //writeFile(SPIFFS, devicename_path, device_name.c_str());
          }
        }
      }
      int saydir = 0;
      
      bool conn = wifiConnect((char *) ssid.c_str(), (char *) pass.c_str());
      //burasi
      if(conn==false){
        request->send(200, "text/plain", "Wrong Password");
      }else{
        writeFile(SPIFFS, SSID_path, ssid.c_str());
        delay(50);
        writeFile(SPIFFS, Password_path, pass.c_str());
        delay(50);
        writeFile(SPIFFS, devicename_path, device_name.c_str());
        delay(50);
        request->send(200, "text/plain", "Device will now restart. You can connect to <b>http://"+String(device_name)+".local<b>");
        delay(3000);
        ESP.restart();
      }
      
      
    });
    server.begin();
  }
}

void loop() {
  if(globalClient != NULL && globalClient->status() == WS_CONNECTED){
      String randomNumber = String(random(0,10000));
      globalClient->text(randomNumber);
   }
   delay(1000);
}

bool wifiConnect(char* ssid, char* password){
    esp_task_wdt_init(15, false);
    bool conn = false;
    int trial = 0;
    WiFi.begin(ssid, password);


    while (WiFi.status() != WL_CONNECTED) {
      delay(1000);
      Serial.println("Connecting to WiFi..");
      
      if(trial==10){
        break;
      }else{
        trial = trial + 1;
      }
      
    }

    if(WiFi.status() == WL_CONNECTED){
      conn = true; 
    }else{
      conn = false;
    }
    
    return conn;
}

String getIp()
{
  esp_task_wdt_init(25, false);
  WiFiClient client;
  if (client.connect("api.ipify.org", 80)) 
  {
      Serial.println("connected");
      client.println("GET / HTTP/1.0");
      client.println("Host: api.ipify.org");
      client.println();
  } else {
      Serial.println("Connection to ipify.org failed");
      return String();
  }
  delay(5000);
  String line;
  while(client.available())
  {
    line = client.readStringUntil('\n');
    Serial.println(line);
  }
  return line;
}


String ip2Str(IPAddress ip){
  String s="";
  for (int i=0; i<4; i++) {
    s += i  ? "." + String(ip[i]) : String(ip[i]);
  }
  return s;
}
