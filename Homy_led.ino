#define DEBUG

#include "Homy_led.h"

//int timer = 0;
//int color = 0xFFFFFF;

void setup(void)
{
#ifdef DEBUG
  Serial.begin(115200);
#endif
  DEBUGGING_L("");
  DEBUGGING("");
  DEBUGGING("");

  //InitSHT31();
  WiFiConnect();
  MDNSSetup();
  MqttSetup();
  WebSocketSetup();
  InitHandleHTTP();
  HTTPUpdateConnect();
  InitWS2812();
}

void loop(void)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    WiFiConnect();
    MDNSSetup();
    MqttSetup();
    WebSocketSetup();
  }
  else
  {
    if (!client.connected())
    {
      MqttConnect();
    }
    client.loop();
    webSocket.loop();
    httpServer.handleClient();
    MDNS.update();
  }
  ws2812fx.service();
  /*if (timer >= 500)
  {
    //GetDataSHT31(&mdl1);
    ws2812fx.setColor(color);
    color--;
    timer = 0;
  }

  timer++;*/
}
