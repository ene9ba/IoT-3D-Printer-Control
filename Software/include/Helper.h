#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <WiFiManager.h>

// needed for WiFiManger
#include <DNSServer.h>
#include <ESP8266WebServer.h>


// needed for MQTT
#include <PubSubClient.h>
#include <time.h>



//ntp
#define MY_NTP_SERVER "at.pool.ntp.org"           
#define MY_TZ "CET-1CEST,M3.5.0/02,M10.5.0/03"   


WiFiClient espClient;
time_t now;                         // this are the seconds since Epoch (1970) - UTC
tm my_time;     
char  dt[80];
PubSubClient client(espClient);


void set_time_string() {
  time(&now);                       // read the current time
  localtime_r(&now, &my_time);           // update the structure tm with the current time
  sprintf(dt,"%02d.%02d.%4d %02d:%02d:%02d", my_time.tm_mday, my_time.tm_mon+1, my_time.tm_year + 1900, my_time.tm_hour, my_time.tm_min, my_time.tm_sec);
 
}


/******************************************************************************************************************
* Sendet Degug information auf den MQTT-Kanal
* void mqttDebugInfo(String load ) 
*******************************************************************************************************************/

void mqttDebugInfo(String load ) 
      {

        load.toCharArray(msg1,150);
        Serial.println(load);
        client.publish(mqtt_pub_Debug, msg1);
        
      }




void init_manual_wifi() {

  Serial.print("trying to connect WiFi ");
  WiFi.begin("EQX_1147", "EaT_1-F_5-M_47");
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  Serial.print("Connected, IP address: ");
  Serial.println(WiFi.localIP());
}






void init_WiFiManger()
{
  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  
  //reset saved settings
  //wifiManager.resetSettings();
  WiFiManager wifiManager;    
  //set custom ip for portal
  //wifiManager.setAPStaticIPConfig(IPAddress(10,0,1,1), IPAddress(10,0,1,1), IPAddress(255,255,255,0));

  //fetches ssid and pass from eeprom and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  wifiManager.autoConnect(AUTO_CONNECT_NAME);
  //or use this for auto generated name ESP + ChipID
  //wifiManager.autoConnect();

    
  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");

  //OTA Config
  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(HOSTNAME.c_str());

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");


}


void init_OTA()
{
    // OTA Setup   
    ArduinoOTA.onStart([]()
        {
            Serial.println("Start");
        });
    ArduinoOTA.onEnd([]()
        {
            Serial.println("\nEnd Device =");
            Serial.println(HOSTNAME);
        });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
        {
            Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
        });
    ArduinoOTA.onError([](ota_error_t error)
        {
            Serial.printf("Error[%u]: ", error);
            if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
            else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
            else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
            else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
            else if (error == OTA_END_ERROR) Serial.println("End Failed");
        });
    ArduinoOTA.begin();

    ArduinoOTA.setHostname(HOSTNAME.c_str());
    Serial.println("OTA Ready");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());



}





/******************************************************************************************************************
  *  void callback(char* topic, byte* payload, unsigned int length) void set_WarmColor()
  *  wird über mqtt subscribe getriggert, sobald von OPENHAB rom Event gemeldet wird
  *  
*******************************************************************************************************************/
void callback(char* topic, byte* payload, unsigned int length) 
        {
            // erst mal alles in Strings verwandeln für die Ausgabe und den Vergleich
            String str_topic=String(topic);
            
            char char_payload[20];
            unsigned int i=0;
            for (i = 0; i < length; i++) { char_payload[i]=(char)payload[i];}
            char_payload[i]=0;
            String str_payload=String(char_payload);
                
            String target_rpn=String();
            String str_mqtt_openHab_command=String(mqtt_pub_Val1);

            String out="Message arrived, Topic : [" + str_topic + "] Payload : [" + str_payload + "]";
              
            #ifdef _MQTT_DEBUG_
                
                mqttDebugInfo(out);
                Serial.print(out);
            #else
                Serial.print(out);
           #endif
            

            //
            //---- Kommando abfragen
            //
            
            
            if (str_topic==mqtt_pub_Val1)
                {

                  #ifdef _MQTT_DEBUG_
                     out="Compare : ["+str_topic+"] : ["+ mqtt_pub_Val1+"]";
                     mqttDebugInfo(out);
                     Serial.print(out);
                  #else
                    Serial.print(out);
                  #endif
                                    
                  
                 solldrehzahl = str_payload.toInt(); 

                }
                 
                  
                  
                  
                    
       }






/******************************************************************************************************************
  * void mqtt_subscribe()
  * Trägt sich für Botschaften ein
*******************************************************************************************************************/
void mqtt_subscribe()
      {
              // ... and subscribe
              client.subscribe(mqtt_pub_Val1);
                           
                            
      }


/******************************************************************************************************************
  * void setup_mqtt()
  * mqtt Setup & connect
*******************************************************************************************************************/
void setup_mqtt()
      {

          //generate unique Clientname for mqtt-Server

          IPAddress ip = WiFi.localIP();
          String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
          String MQTT_HostName=String(HOSTNAME)+"_"+ipStr;
          char MQTT_HostNameChar[MQTT_HostName.length()];
          MQTT_HostName.toCharArray(MQTT_HostNameChar,sizeof(MQTT_HostNameChar));

          int loop=0;
          delay(10);
          // We start by connecting to a WiFi network
          Serial.print("This Client = ");
          Serial.println(MQTT_HostName);
          Serial.println("");
          Serial.print(".. MQTT .. ");
          Serial.print("Connecting to ");
          Serial.println(MQTT_SERVER);
        

  
          // Verbinden mit mqtt-Server und einrichten Callback event        
          client.setServer(MQTT_SERVER, 1883);
          client.setCallback(callback);
  
          while (!client.connected()) 
          {
            
            client.connect((char*)MQTT_HostNameChar, MQTT_USER, MQTT_PW);

          
            Serial.print(".");
            loop++;
            if (loop > 2) break;
          }
        if (loop > 2) 
        {

            Serial.println("");
            Serial.println("NQTT-Server not found ...");
            // delay(2000);
            // ESP.restart();
        }
        else  
        {
          Serial.println("");
          Serial.print("MQTT found Adress: ");
          Serial.println(MQTT_SERVER);
          mqtt_subscribe();
        }
    }


/******************************************************************************************************************
  * void mqtt_reconnect()
  * Verbindet sich mit dem MQTT-Server
*******************************************************************************************************************/


void check_mqtt_connect() 
    {   
        // repeat trying connect 4 times, then reset
        int retry = 0;
        #define TRIES 3
                 
        
        if (!client.connected()) 
        {
          Serial.println("connection lost");
          while (retry <= TRIES)
          {
            Serial.println("Attempting MQTT reconnecti...");
            // Attempt to connect
            if (client.connect(HOSTNAME.c_str())) 
            {
              Serial.println("reconnected");
              // Once connected, publish an announcement...
              
              client.publish(HOSTNAME.c_str(), " online");
              client.setCallback(callback);
              mqtt_subscribe();
              // Once connected, publish an announcement...
              set_time_string();
              client.publish(mqtt_pub_lastconnect, dt);

              delay(1500);
              return; 
            }
            retry++;
            Serial.print("Retry nr: "); Serial.println(retry);
          }
          //ESP.restart();     
          


        }
         
    }

/******************************************************************************************************************
  * void mqtt_reconnect()
  * Verbindet sich mit dem MQTT-Server
*******************************************************************************************************************/


void mqtt_reconnect() 
    {   
        // repeat trying connect 4 times, then reset
        int retry = 0;
        #define TRIES 3
                 
        
        if (!client.connected()) 
        {
          while (retry <= TRIES)
          {
            Serial.println("Attempting MQTT reconnecti...");
            // Attempt to connect
            if (client.connect(HOSTNAME.c_str(), MQTT_USER, MQTT_PW));
            {
              Serial.println("connected");
              // Once connected, publish an announcement...
              
              client.publish(HOSTNAME.c_str(), " online");

              delay(1500);
              return; 
            }
            retry++;
          }
          //ESP.restart();     

        }
         
    }