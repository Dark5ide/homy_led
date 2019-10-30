#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <WebSocketsServer.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
//#include <Wire.h>
//#include "Adafruit_SHT31.h"
#include <WS2812FX.h>

#ifdef DEBUG
#define DEBUGGING(...) Serial.println( __VA_ARGS__ )
#define DEBUGGING_L(...) Serial.print( __VA_ARGS__ )
#define DEBUG_INIT_LED(...) pinMode( __VA_ARGS__ )
#define DEBUG_LED(...) digitalWrite( __VA_ARGS__ )
#endif

#define LED_NB              15
#define DEFAULT_COLOR       0xFF5900
#define DEFAULT_BRIGHTNESS  255
#define DEFAULT_SPEED       1000
#define DEFAULT_MODE        FX_MODE_STATIC

//////////////////////////////////////////////////////////
//                   Globals Variables                  //
//////////////////////////////////////////////////////////


/************ WIFI and MQTT Information (CHANGE THESE FOR YOUR SETUP) ******************/
const char *ssid = "yourSSID";
const char *password = "yourPassword";
const char *mqtt_server = "yourServer";
const char *mqtt_backup_server = "yourBackupServer";
//const char* mqtt_username = "yourMQTTusername";
//const char* mqtt_password = "yourMQTTpassword";
const int mqtt_port = 1883;
int mqtt_conn_try = 0;

/************ Pin Number assignation  **************************************************/
typedef struct {
  int pinNb;
  String strType;
  String strName;
  uint32_t  state;
  String stateSensor;
} Module;

Module mdl0{2, "led", "all", 0xFFFFFF, ""};
Module mdl1{2, "led", "led1", 0xFF5900, ""};
//Module mdl2{12, "lamp", "relay1", LOW, ""}; // Pin Number and state initializtion for the mood lamp

#ifdef DEBUG
//const int led = 13; // Led that indicates the server request
#endif


/************ MQTT TOPICS (change these topics as you wish) ****************************/
const char *state_topic = "mycroft/homy/state";
const char *cmd_topic = "mycroft/homy/cmd";

#define NB_MDL 2
const int self_id = 4;
const char *self_name = "homy_led";
Module *self_module[NB_MDL] = {&mdl0, &mdl1};
String on_cmd[] = {"turn_on", "switch_on", "power_on"};
String off_cmd[] = {"turn_off", "switch_off", "power_off"};
String toggle_cmd[] = {"toggle", "switch"};
String set_cmd[] = {"set"};


/************ FOR JSON *****************************************************************/
const size_t bufferSize = JSON_ARRAY_SIZE(NB_MDL) + NB_MDL*JSON_OBJECT_SIZE(3) + 180;


/************ WEB PAGE *****************************************************************/
String html = 
"<html>\
  <head>\
    <title>ESP8266 Temperature & Humidity</title>\
    <style>\
      body { background-color: #993333; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
    </style>\
  </head>\
  <body>\
    <h1>Hello from ESP8266!</h1>\
    <p>Uptime: %%UPTIME%%</p>\
    <p>Color: %%COLOR%%</p>\
    <br />\
    <p><a href='update'><button name='update'>UPDATE</button></a></p>\
  </body>\
</html>";


WebSocketsServer webSocket = WebSocketsServer(81);
ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;
WiFiClient espClient;
PubSubClient client(espClient);
//Adafruit_SHT31 sht31 = Adafruit_SHT31();
WS2812FX ws2812fx = WS2812FX(LED_NB, mdl1.pinNb, NEO_GRBW + NEO_KHZ800);
//////////////////////////////////////////////////////////
//              Function Headers                        //
//////////////////////////////////////////////////////////
String StateToJson(void);

//////////////////////////////////////////////////////////
//                    Functions                         //
//////////////////////////////////////////////////////////
// LED WS2812 initialization
void InitWS2812(void)
{
  ws2812fx.init();
  ws2812fx.setMode(DEFAULT_MODE);
  ws2812fx.setColor(DEFAULT_COLOR);
  ws2812fx.setSpeed(DEFAULT_SPEED);
  ws2812fx.setBrightness(DEFAULT_BRIGHTNESS);
  ws2812fx.start();
}
// SHT31 initialization
/*void InitSHT31(void)
{
  if (!sht31.begin(0x44)) 
  {
    DEBUGGING("Couldn't find SHT31");
    while(1) delay(1);
  }
  else
  {
    DEBUGGING("SHT31 sensor ready.");
  }
}*/
// IR initialization
/*void InitIR(void)
{
  irsend.begin();
}*/

// WiFi Connection
void WiFiConnect(void)
{
  WiFi.begin(ssid, password);

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(100);
    DEBUGGING_L(".");
  }

  DEBUGGING("");
  DEBUGGING_L("Connected to ");
  DEBUGGING(ssid);
  DEBUGGING_L("IP address: ");
  DEBUGGING(WiFi.localIP());
}

// MDNS Initialization
void MDNSSetup()
{
  if (MDNS.begin(self_name, WiFi.localIP())) // ex: http://homy_dev.local/ in stead of IP adresse
  {
    DEBUGGING("MDNS responder started : http://" + String(self_name) + ".local");
  }
  else
  {
    DEBUGGING("Error setting up MDNS responder!");
  }
  
  // Add service to MDNS-SD
  MDNS.addService("ws", "tcp", 81);
  MDNS.addService("http", "tcp", 80);
}
//////////////////////////////////////////////////////////
//                    Sensor functions                  //
//////////////////////////////////////////////////////////
/*void GetDataSHT31(Module *sensor)
{
  String json_l;
  float t = sht31.readTemperature();
  float h = sht31.readHumidity();

  if ((! isnan(t)) && (! isnan(h)))
  {
    DEBUGGING_L("Temp. = "); DEBUGGING_L(t); DEBUGGING("[*C]");
    DEBUGGING_L("Hum.  = "); DEBUGGING_L(h); DEBUGGING("[%]");
    sensor->stateSensor = String("Temp. = " + String(t) + "[*C];" + "Hum. = " + String(h) + "[%]");
    json_l = StateToJson();

    if(!client.publish(state_topic, json_l.c_str(), true))
    {
      DEBUGGING("Failed to publish!");
    }
  } 
  else 
  { 
    DEBUGGING("Failed to read SHT31.");
  }
}*/

//////////////////////////////////////////////////////////
//                    Commands functions                //
//////////////////////////////////////////////////////////
// Functions that execute the command received

bool SearchStr(String str_p[], int size_p, String item_p)
{
  int i = 0;
  for (i = 0; i < size_p; i++)
  {
    if (item_p == str_p[i])
    {
      return true;
    }
  }
  return false;
}

// Manage generic Commands
bool Command(String cmd_p, Module *mdl_p, uint32_t state_p)
{
  bool cmd_executed = false;
  int i = 1;
  
  if (SearchStr(on_cmd, 3, cmd_p))
  {
    if (mdl_p->strType == "led")
    {
      if (mdl_p->strName == "all")
      {
        for (i = 1; i < NB_MDL; i++)
        {
          if (self_module[i]->strType == "led")
          {
            DEBUGGING(self_module[i]->strName + " - On");
            ws2812fx.setColor(self_module[0]->state);
            ws2812fx.start();
            self_module[i]->state = self_module[0]->state;
          }
        }
      }
      else
      {
        DEBUGGING(mdl_p->strName + " - On");
        ws2812fx.setColor(mdl_p->state);
        ws2812fx.start();
      }
    }
    else if (mdl_p->strType == "lamp")
    {
      if (mdl_p->strName == "all")
      {
        for (i = 1; i < NB_MDL; i++)
        {
          if (self_module[i]->strType == "lamp")
          {
            DEBUGGING(self_module[i]->strName + " - On");
            digitalWrite(self_module[i]->pinNb, HIGH);
            self_module[i]->state = HIGH;
          }
        }
      }
      else
      {
        DEBUGGING(mdl_p->strName + " - On");
        digitalWrite(mdl_p->pinNb, HIGH);
        mdl_p->state = HIGH;
      }
    }
    else if (mdl_p->strType == "tv")
    {
      DEBUGGING("LG TV - On/Off");
      //irsend.sendNEC(0x20DF10EF, 32); // Send ON/OFF TV
      delay(100);
    }

    cmd_executed = true;
  }
  else if (SearchStr(off_cmd, 3, cmd_p))
  {
    if (mdl_p->strType == "led")
    {
      if (mdl_p->strName == "all")
      {
        for (i = 1; i < NB_MDL; i++)
        {
          if (self_module[i]->strType == "led")
          {
            DEBUGGING(self_module[i]->strName + " - On");
            ws2812fx.stop();
          }
        }
      }
      else
      {
        DEBUGGING(mdl_p->strName + " - On");
        ws2812fx.stop();
      }
    }
    else if (mdl_p->strType == "lamp")
    {
      if (mdl_p->strName == "all")
      {
        for (i = 1; i < NB_MDL; i++)
        {
          if (self_module[i]->strType == "lamp")
          {
            DEBUGGING(self_module[i]->strName + " - Off");
            digitalWrite(self_module[i]->pinNb, LOW);
            self_module[i]->state = LOW;
          }
        }
      }
      else
      {
        DEBUGGING(mdl_p->strName + " - Off");
        digitalWrite(mdl_p->pinNb, LOW);
        mdl_p->state = LOW;
      }
    }
    else if (mdl_p->strType == "tv")
    {
      DEBUGGING("LG TV - On/Off");
      //irsend.sendNEC(0x20DF10EF, 32); // Send ON/OFF TV
      delay(100);
    }

    cmd_executed = true;
  }
  else if (SearchStr(toggle_cmd, 2, cmd_p))
  {
    if (mdl_p->strType == "lamp")
    {
      mdl_p->state = !mdl_p->state;
      digitalWrite(mdl_p->pinNb, mdl_p->state);
      DEBUGGING((mdl_p->state == LOW) ? mdl_p->strName + " - Off" : mdl_p->strName + " - On");
    }

    cmd_executed = true;
  }
  else if (SearchStr(set_cmd, 1, cmd_p))
  {
    if (mdl_p->strType == "led")
    {
      ws2812fx.setColor(state_p);
      mdl_p->state = state_p;
      DEBUGGING_L(mdl_p->strName + " - 0x");
      DEBUGGING(mdl_p->state, HEX);
    }
    cmd_executed = true;
  }
  else
  {
    cmd_executed = false;
  }

  return cmd_executed;
}


//////////////////////////////////////////////////////////
//                    JSON functions                    //
//////////////////////////////////////////////////////////
int SearchModule(Module *mdls_p[], int mdl_nb_p, String mdl_p)
{
  int i = 0;
  for (i = 0; i < mdl_nb_p; i++)
  {
    if (mdl_p == mdls_p[i]->strName)//if (mdl_p.compareTo(mdls_p[i]->strName) == 0)
    {
      return i;
    }
  }

  return -1;
}

String StateToJson(void)
{
  int i = 1;
  DynamicJsonDocument doc(bufferSize);

  doc["id"] = self_id;
  doc["name"] = self_name;

  JsonArray devices = doc.createNestedArray("devices");
  devices.add(NB_MDL - 1);

  for (i = 1; i < NB_MDL; i++)
  {
    JsonObject devices_n = devices.createNestedObject();
    devices_n["type"] = self_module[i]->strType;
    devices_n["module"] = self_module[i]->strName;
    if (strcmp(self_module[i]->strType.c_str(), "sensor") == 0)
    {
      devices_n["state"] = self_module[i]->stateSensor;
    }
    else
    {
      devices_n["state"] = self_module[i]->state;
    }
  }

  String message_l;
  serializeJson(doc, message_l);
  
  return message_l;
}

bool DecodeJson(const char *msgJson)
{
  int devices_nb = 0;
  int i = 0;
  int index_mdl = 0;
  String str_mdl;
  String str_cmd;
  uint32_t  int_state = 0;
  DynamicJsonDocument doc(bufferSize);
  DeserializationError error = deserializeJson(doc, msgJson);

  if (error)
  {
    DEBUGGING("deserializeJson() failed");
    return false;
  }

  if ((!doc.containsKey("name")) || (doc.containsKey("name") && (strcmp(doc["name"], self_name) != 0)))
  {
    DEBUGGING("The message is not intended for this device.");
    return false;
  }

  if (doc.containsKey("devices"))
  {
    JsonArray devices = doc["devices"];
    
    devices_nb = devices[0];
    for (i = 0; i < devices_nb; i++)
    {
      str_mdl = String((const char *) devices[i+1]["module"]);
      str_cmd = String((const char *) devices[i+1]["cmd"]);
      if (str_cmd == "set")
      {
        int_state = devices[i+1]["state"];
      }
      index_mdl = SearchModule(self_module, NB_MDL, str_mdl);
      Command(str_cmd, self_module[index_mdl], int_state);
    }
  }

  return true;
}

//////////////////////////////////////////////////////////
//                  MQTT functions                      //
//////////////////////////////////////////////////////////

// MQTT Connect
void MqttConnect(void)
{
  static char *broker = (char *) mqtt_server;
  DEBUGGING("Attempting MQTT connection...");
  // Attempt to connect
  if (client.connect(self_name))
  {
    DEBUGGING_L("connected to broker : ");
    DEBUGGING(broker);
    // Publish he is alive
    if (!client.publish(state_topic, "{\"MQTT\": \"OK\"}"))
    {
      DEBUGGING("Failed to publish at reconnection");
    }
    // ... and subscribe to topic
    client.subscribe(cmd_topic);
  }
  else
  {
    // Try to connect to the server 3 times
    // before switching to the backup server
    if (mqtt_conn_try < 3)
    {
      mqtt_conn_try++;
      DEBUGGING_L("failed, rc=");
      DEBUGGING_L(client.state());
      DEBUGGING(" try again in 3 seconds");
      // Wait 3 seconds before retrying
      delay(3000);
    }
    else
    {
      mqtt_conn_try = 0;
      client.setServer(mqtt_backup_server, mqtt_port);
      broker = (char *) mqtt_backup_server;
    }
  }
}

// mqtt callback
void MqttCallback(char* topic_p, byte* payload_p, unsigned int length_p)
{
  String top_l = String((char *) &topic_p[0]);
  String pld_l = String((char *) &payload_p[0]);
  String json_l;

  pld_l.remove(length_p);
  DEBUGGING(top_l);
  DEBUGGING(pld_l);

  if (top_l.startsWith(cmd_topic))
  {
    DecodeJson(pld_l.c_str());
    json_l = StateToJson();

    if(!client.publish(state_topic, json_l.c_str(), true))
    {
      DEBUGGING("Failed to publish!");
    }
  }
}

// mqtt connection
void MqttSetup(void)
{
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(MqttCallback);
}

//////////////////////////////////////////////////////////
//                  Websocket functions                 //
//////////////////////////////////////////////////////////

// WebSOcket Events
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length)
{
  String json_l;
  
  switch (type)
  {
    case WStype_DISCONNECTED:
      {
        DEBUGGING("WebSocket Disconnected!");
      }
      break;
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
      }
      break;
    case WStype_TEXT:
      {
        String txt = String((char *) &payload[0]);
        DecodeJson(txt.c_str());
        json_l = StateToJson();
        if (!webSocket.sendTXT(num, json_l))
        {
          DEBUGGING("Failed to send message through WS!");
        }
      }
      break;
    case WStype_BIN:
      {
        hexdump(payload, length);
        // echo data back to browser
        webSocket.sendBIN(num, payload, length);
      }
      break;
  }
}

// WebSocket Setup
void WebSocketSetup(void)
{
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}

//////////////////////////////////////////////////////////
//                     HTTP functions                   //
//////////////////////////////////////////////////////////

// HTTP request not found
void HandleNotFound(void)
{
  //DEBUG_LED(led, HIGH);
  
  String message = "Not Found\n\n";
  message += "URI: ";
  message += httpServer.uri();
  message += "\nMethod: ";
  message += ( httpServer.method() == HTTP_GET ) ? "GET" : "POST";
  message += "\nArguments: ";
  message += httpServer.args();
  message += "\n";

  for ( uint8_t i = 0; i < httpServer.args(); i++ )
  {
    message += " " + httpServer.argName(i) + ": " + httpServer.arg(i) + "\n";
  }

  httpServer.send(404, "text/plain", message);

  //DEBUG_LED(led, LOW);
}

// Function that handles the HTTP root
void HandleRoot(void)
{
  //DEBUG_LED(led, HIGH);
 
  char temp[9];
  int sec = millis() / 1000;
  int min = sec / 60;
  int hr = min / 60;
  String to_send = html;

  snprintf(temp, sizeof(temp), "%02d:%02d:%02d", hr, min % 60, sec % 60);

  to_send.replace("%%UPTIME%%", temp);
  to_send.replace("%%COLOR%%", String(mdl1.state));
  
  httpServer.send(200, "text/html", to_send);
  
  //DEBUG_LED(led, LOW);
}

// Fonction that handles the GPIO
void HandleGPIO(Module *Lampe)
{
  String json_l;
  String cmd = httpServer.arg("cmd");
  
  Command(cmd, Lampe, -1);
  json_l = StateToJson();

  if(!client.publish(state_topic, json_l.c_str(), true))
  {
    DEBUGGING("Failed to publish!");
  }

  HandleRoot();

  //httpServer.send(200, "text/html", html);
}


// Function that handles the LG TV commands
/*void HandleTV(void)
{
  String json_l;
  String cmd = httpServer.arg("cmd");
  static String prev_cmd = "switch_off";

  if (cmd != prev_cmd)
  {
    if (Command(cmd, &mdl3) , -1)
    {
      html.replace(cmd, prev_cmd);
      prev_cmd = cmd;

      json_l = StateToJson();

      if(!client.publish(state_topic, json_l.c_str(), true))
      {
        DEBUGGING("Failed to publish!");
      }
      
      HandleRoot();
    }
    else
    {
      HandleNotFound();
    }
  }
  else
  {
    HandleRoot();
  }
}*/

// Initialize the handler function (callback) for http requests
void InitHandleHTTP(void)
{
  httpServer.on("/", HandleRoot);
  //httpServer.on("/led0", []() {HandleGPIO(&mdl1);});
  //httpServer.on("/tv", HandleTV);
  //httpServer.on( "/inline", []() {httpServer.send ( 200, "text/plain", "this works as well" );} );
  httpServer.onNotFound(HandleNotFound);
  httpServer.begin();
  DEBUGGING("HTTP server started.");
}

// HTTP updater connection
void HTTPUpdateConnect()
{
  httpUpdater.setup(&httpServer);
  httpServer.begin();
  DEBUGGING_L("HTTPUpdateServer ready! Open http://");
  DEBUGGING_L(self_name);
  DEBUGGING(".local/update in your browser\n");
}
