/*
Version 1.0 v. 24.11.2023

Dieses Programm steuert die 3D Druckerbox, überwacht und steuert die
Lüfterdrehzahl, überwacht mit Feuersensor und Temperatur und Luftfeuchte,
zeigt die wesentlichen Werte in eine TFT-Display an.

Version 1.10 v. 16.03.2024 ntp hinzugefügt las message Meldung geht an mqtt
Version 1.20 v. 24.09.2024 mqtt mit user und passort, reconnect getestet

*/

//#define __DEBUG
#define _MQTT_DEBUG_

#include <Arduino.h>
#include <DHT.h>

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>



String        HOSTNAME           = "3DCabinetControl";
const char*   AUTO_CONNECT_NAME  = "IoT_CabinetControl";
//MQTT-Server Adresse
const char*   MQTT_SERVER        = "docker-home";
#define MQTT_USER "mymqtt"
#define MQTT_PW   "yfAlORp1C3k70fy6XSkY"

//Portzuordnung
#define PWM                 0     // gpio 0 oder D3
#define FREQ               12     // Drehzahl Lüfter
#define DHT22_PIN          10     // DHT 22  (AM2302) - what pin we're connected to
#define RELAIS_LED         16     // Output Switch LED Flasher on
#define RELAIS             15     // Output Relais
#define ANALOG             A0     // Flame sensor
#define SMOKE               2     // Smoke Sensor

#define TFT_CS             15     // define CS
#define TFT_DC              5     // define data/command pin
#define TFT_RST             4     // define reset pin

#define DHTTYPE            DHT22




// mqtt publish values

const char* mqtt_pub_Val2     ="/openHAB/3dCabinet/Fan_rpm_actual";
const char* mqtt_pub_Val3     ="/openHAB/3dCabinet/Temperature";
const char* mqtt_pub_Val4     ="/openHAB/3dCabinet/Humidity";
const char* mqtt_pub_Val5     ="/openHAB/3dCabinet/Fire";
const char* mqtt_pub_Val6     ="/openHAB/3dCabinet/Smoke";
const char* mqtt_pub_Val7     ="/openHAB/3dCabinet/FireAnalog";
const char* mqtt_pub_Version  ="/openHAB/3dCabinet/Version";
const char* mqtt_pub_RSSI     ="/openHAB/3dCabinet/RSSI";
const char* mqtt_pub_Debug    ="/openHAB/3dCabinet/Debug";

const char* mqtt_pub_lastconnect    =  "/openHAB/3dCabinet/lastReconnect";
const char* mqtt_pub_lastmsg        =  "/openHAB/3dCabinet/lastMessage";


// mqtt subscribe values
const char* mqtt_pub_Val1     ="/openHAB/3dCabinet/Fan_rpm_target";
char    msg1[250]; 


// Global Variables
float             rpm               = 0.0;            // Lüfterdrehzahl
word              solldrehzahl      = 0;              // Solldrehzahl PWM-Wert in %
float             humidity          = 0.0;            //Stores humidity value
float             temperatur        = 0.0;            //Stores temperature value
String            smoke_detected    = "no init";      // Rauchsensor
String            fire_detected     = "no init";      // Feuersensor
word              fire_analog       = 0;
bool              alarm             = false;
bool              firealarm_trigg   = false;
word              roundrobin        = 0;

#include "Helper.h"



String Version    = "V1.10 :: ";
String AppName    = "Smart-Druckergehäuse";


//Globale Variablen 

String ipStr;
String MQTT_HostName;

// Zeitspeicher für Pseudothreads im Loop
long        thread_last_setrpm       = 0;
long        thread_last_getrpm       = 0;
long        thread_last_getTemHum    = 0;
long        thread_last_getFire      = 0;
long        thread_last_getSmoke     = 0;
long        thread_lastMsg_RSSI      = 0;
long        thread_check_system      = 0;
long        thread_show_info_display = 0;



// Schwellwert Analog Feuer erkannt
#define     FIRETRIGGER       360     // Schwellwert Feuertrigger Alarm, wenn dieser Wert unterschritten
#define     MIN_ERR_RPM_100  2000     // Minimal erwartete Drehzahl Lüfter bei Sollwert 100 % 
#define     MIN_WARN_RPM_100 1000     // Minimal erwartete Drehzahl Lüfter bei Sollwert 100 % 
#define     MAX_TEMP          45      // Maximale Temperatur im Gehäuse
#define     MIN_RPM         1500      // Minimale Drehzahl Lüfter




DHT dht(DHT22_PIN,DHTTYPE);
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);



 


void publish_MQTT_Values()

{
    //Zeit seit letztem Durchlauf berechen und die Sekunden in value hochz?hlen  
    long now = millis();

    if (now - thread_lastMsg_RSSI < 3000) return;

    int rssi = WiFi.RSSI();
    Serial.print("RSSI: ");
    Serial.print(rssi);
    Serial.println(" db");

    // publish rssi
    itoa(rssi,msg1,10);
    client.publish(mqtt_pub_RSSI,msg1);

    // publish rmp acutal
    sprintf(msg1,"%.1f",rpm);
    client.publish(mqtt_pub_Val2,msg1);

    // publish temperature & humitidy
    sprintf(msg1,"%.1f",temperatur);
    client.publish(mqtt_pub_Val3,msg1);
    sprintf(msg1,"%.1f",humidity);
    client.publish(mqtt_pub_Val4,msg1);

    // Rauch und Feuer
    client.publish(mqtt_pub_Val6,smoke_detected.c_str());
    client.publish(mqtt_pub_Val5,fire_detected.c_str());
    sprintf(msg1,"%d",fire_analog);
    client.publish(mqtt_pub_Val7,msg1);

    set_time_string();  
    client.publish(mqtt_pub_lastmsg,dt);

    thread_lastMsg_RSSI = millis();

}












 void get_rpm() {
      unsigned long     highpulse = 0;
      unsigned long     lowpulse = 0;
      double            frequency = 0.0;



      //Zeit seit letztem Durchlauf berechen und die Sekunden in value hochz?hlen  
      long now = millis();
      if (now - thread_last_getrpm < 1000) return;

      highpulse = pulseIn(FREQ, HIGH);
      lowpulse = pulseIn(FREQ, LOW);

      // wenn timeout wird 0 zurück geliefert, dann steht der Lüfter
      if(highpulse !=0 && lowpulse !=0) {
            // berechne drehzahl
            frequency = 1.0 / ((highpulse + lowpulse) / 1000000.0);
            rpm = frequency * 60;

      }
      else rpm = 0;

     
      #ifdef _MQTT_DEBUG_
            
      
            sprintf(msg1,"highpulse: %lu  us, Lowpulse: %lu  us Frequenz: %.0f  Hz Drehzal: %.0f",highpulse, lowpulse, frequency, rpm);
            mqttDebugInfo(msg1);
            Serial.println(msg1);
      #else
            Serial.print(msg1);
      #endif
            

      thread_last_getrpm = now;
      return;
}

void set_rpm(int sollwert) {
      //Zeit seit letztem Durchlauf berechen und die Sekunden in value hochz?hlen  
      long now = millis();
      if (now - thread_last_setrpm < 1000) return;

      float soll = (100 - sollwert) * 2.55;
      analogWrite(PWM,(int)soll);
      #ifdef __DEBUG
      Serial.print("Sollwert: "); Serial.print(sollwert); Serial.print("PWM: "); Serial.println((int(soll))); 
      #endif  

      thread_last_setrpm = now;
      return;
}


void get_Temp_Hum() {
      //Zeit seit letztem Durchlauf berechen und die Sekunden in value hochz?hlen  
      long now = millis();
      if (now - thread_last_getTemHum < 2000) return;

      humidity = dht.readHumidity();
      temperatur = dht.readTemperature();
      

      #ifdef __DEBUG
      Serial.print("Luftfeuchte: "); Serial.print(humidity); Serial.print(" %, Temperatur: "); Serial.print(temperatur); Serial.println(" Grad Celsius"); 
      #endif  
    

      thread_last_getTemHum = now;
      return;
}


void get_FireSense() {
      //Zeit seit letztem Durchlauf berechen und die Sekunden in value hochz?hlen  
      long now = millis();
      if (now - thread_last_getFire < 500) return;

      fire_analog = analogRead(ANALOG);
      
      

      #ifdef __DEBUG
      Serial.print("Feuersensor: "); Serial.println(fire_analog); //Serial.print(" %, Temperatur: "); Serial.print(temperatur); Serial.println(" Grad Celsius"); 
      #endif  

      
      if (fire_analog > FIRETRIGGER) fire_detected = "OK"; else fire_detected = "FIRE";


      thread_last_getFire = now;
      return;


}

void get_SmokeSense() {
      //Zeit seit letztem Durchlauf berechen und die Sekunden in value hochz?hlen  
      long now = millis();
      if (now - thread_last_getSmoke < 500) return;

      int val = digitalRead(SMOKE);
      
      

      #ifdef __DEBUG
      Serial.print("Rauch: "); Serial.println(val); //Serial.print(" %, Temperatur: "); Serial.print(temperatur); Serial.println(" Grad Celsius"); 
      #endif  
      if (val != 0) smoke_detected = "OK"; else smoke_detected = "SMOKE";

      thread_last_getSmoke = now;
      return;


}


void Init_System() {

      bool check = true;
      alarm = false;
      
      tft.fillScreen(ST77XX_BLACK);
      tft.setCursor(0, 0);
      tft.setTextColor(ST77XX_ORANGE);
      tft.setTextSize(4);
      tft.println(" System");
      tft.println(" Check");
      delay(1000);

      
      tft.fillScreen(ST77XX_BLACK);
      tft.setCursor(0, 0);

      // stetze MAX Drehzahl und messe Umdrehungen
      tft.setTextColor(ST77XX_WHITE);
      tft.print(" FAN:  ");

      set_rpm(100);
      int pos = 120;
      for (int i=0; i<10; i++) {
            tft.setCursor(pos, 0);
            tft.setTextColor(ST77XX_BLACK);
            sprintf(msg1," %1.f U", rpm);
            tft.print(msg1);
            get_rpm();
            tft.setTextColor(ST77XX_WHITE);
            tft.setCursor(pos, 0);
            sprintf(msg1," %1.f U", rpm);
            tft.print(msg1);
            delay(500);
                  

      }
      tft.setCursor(pos, 0);
      tft.setTextColor(ST77XX_BLACK);
      tft.print(msg1);
            
      tft.setCursor(pos, 0);
      if (rpm < MIN_ERR_RPM_100) {
            tft.setTextColor(ST77XX_RED);
            tft.println("ERR");
            check = false;
      }
      else {
            tft.setTextColor(ST77XX_GREEN);
            tft.println("O.K.");
      }
      delay(100);
      
      // Prüfe Temperatur auf Plausibilität      
      tft.setTextColor(ST77XX_WHITE);
      tft.print(" TEMP: ");
      get_Temp_Hum();
      if (temperatur < 10 || temperatur > 40 ) {
            tft.setTextColor(ST77XX_RED);
            tft.println("ERR");
            check = false;
      }
      else {
            tft.setTextColor(ST77XX_GREEN);
            tft.println("O.K.");
      }
      
      
      // Prüfe Analogstpannung Feuersensor
      tft.setTextColor(ST77XX_WHITE);
      tft.print(" FLAME:");
      get_FireSense();
      if (fire_detected != "OK") {
            tft.setTextColor(ST77XX_RED);
            tft.println("ERR");
            check = false;
      }
      else {
            tft.setTextColor(ST77XX_GREEN);
            tft.println("O.K.");
      }
      
      // Prüfe Eingang Rauchmelder
      tft.setTextColor(ST77XX_WHITE);
      tft.print(" Smoke:");
      get_SmokeSense();
      if (smoke_detected != "OK") {
            tft.setTextColor(ST77XX_RED);
            tft.println("ERR");
            check = false;
      }
      else {
            tft.setTextColor(ST77XX_GREEN);
            tft.println("O.K.");
      }
 
      if (!check) {
            tft.setTextColor(ST77XX_RED);
            tft.println(""); tft.print("  ERROR");
            delay(10000);

      }
      // Every Thing is o.k. Relay on, LED on Fan on
      else {
            delay(1000);
            tft.setTextColor(ST77XX_GREEN);
            tft.println(""); tft.print("  O.K.");
            
            solldrehzahl = 50;
            tft.fillScreen(ST77XX_BLACK);
            tft.setCursor(0, 0);
            delay(500);
            digitalWrite(RELAIS_LED,LOW);    

            delay(500);
            tft.init(240, 240, SPI_MODE2);
            tft.setRotation(1); 
            delay(500);
            pinMode(FREQ,INPUT);     // Fan pwm control
            


      }
      

}


// prüft auf unregelmäßigkeiten uns schaltet bei Gefahr aus
void check_system() {

        
      //Zeit seit letztem Durchlauf berechen und die Sekunden in value hochzählen  
      long now = millis();
      if (now - thread_check_system < 500) return;

      String errtext="none";
      
      // Überwache Drehzahl
      if (rpm < MIN_RPM) {
            errtext = "\n ERR Fan";
            alarm = true;
      }
      
      
      // Überwache Temperatur      
      if (temperatur > MAX_TEMP) {
            errtext = "\n ERR Temp";
            alarm = true;
      }
      
      // Überwache Analogstpannung Feuersensor
      if (fire_detected != "OK") {
            errtext = "\n ERR Fire";
              firealarm_trigg = true;
  //          alarm = true;
      }
      
      // Überwache Eingang Rauchmelder
      if (smoke_detected != "OK") {
            errtext = "\n ERR Smoke";
            alarm = true;
      }
      
     // System, stoppen bei Fehler
     if (alarm) {

      digitalWrite(RELAIS_LED,HIGH);
      delay(1000);
      set_rpm(0);
      delay(200);
      tft.fillScreen(ST77XX_RED);
      bool alternate = false;
      while(true) {

          if (alternate) {
            tft.setTextColor(ST77XX_BLUE);
            alternate = false;
          }
          else { 
            tft.setTextColor(ST77XX_WHITE);
            alternate = true;
          }
          tft.setCursor(0, 0);
          tft.println(errtext);  
          get_rpm();
          get_FireSense();
          publish_MQTT_Values();
          ArduinoOTA.handle();
          //check_mqtt_connect();
          client.loop();
          delay(500);  
      }

     } 
     
      thread_check_system = now;


}


void show_info_display() {

       //Zeit seit letztem Durchlauf berechen und die Sekunden in value hochz?hlen  
      long now = millis();
      if (now - thread_show_info_display < 5000) return;

      

      tft.setCursor(0, 0);
      
      switch (roundrobin) {

          case 0: // Drehzahl anzeigen
                  tft.setCursor(0, 0);
                  tft.fillScreen(ST77XX_BLACK);
                  tft.setTextColor(ST77XX_WHITE);
                  tft.println(" Fan:");
                  sprintf(msg1," %.f U", rpm);
                  if (rpm < MIN_ERR_RPM_100) tft.setTextColor(ST77XX_RED);
                  else 
                    if (rpm < MIN_WARN_RPM_100) tft.setTextColor(ST77XX_YELLOW);
                    else tft.setTextColor(ST77XX_GREEN);

                  tft.println(msg1);
                   
                  // Temperatur und Feuchte anzeigen
                  tft.setTextColor(ST77XX_WHITE);
                  tft.println(" TEMP:");
                  tft.setTextColor(ST77XX_GREEN);
                  sprintf(msg1," %.1f C", temperatur);
                  tft.println(msg1);

                  tft.setTextColor(ST77XX_WHITE);
                  tft.println(" Hum:");
                  tft.setTextColor(ST77XX_GREEN);
                  sprintf(msg1," %.1f %%", humidity);
                  tft.println(msg1);
                  break;
          case 1: // Status Rauch und Feuer
                  tft.setCursor(0, 0);
                  tft.fillScreen(ST77XX_BLACK);
                  tft.setCursor(0, 0);
      
                  // Prüfe Analogstpannung Feuersensor
                  tft.setTextColor(ST77XX_WHITE);
                  tft.print(" FLAME:");
                  if (firealarm_trigg) {
                        tft.setTextColor(ST77XX_RED);
                        tft.println("ERR");
                  }
                  else {
                        tft.setTextColor(ST77XX_GREEN);
                        tft.println("O.K.");
                  }
      
                  // Prüfe Eingang Rauchmelder
                  tft.setTextColor(ST77XX_WHITE);
                  tft.print(" Smoke:");
                  if (smoke_detected != "OK") {
                  tft.setTextColor(ST77XX_RED);
                  tft.println("ERR");
                  }
                  else {
                              tft.setTextColor(ST77XX_GREEN);
                              tft.println("O.K.");
                  }
                  break;
       
                  


      }
      
      roundrobin++;
      if (roundrobin == 2) roundrobin = 0;
      thread_show_info_display = now;
      

}



void setup() {
      
      pinMode(PWM,                OUTPUT);     // Fan pwm control
      pinMode(RELAIS_LED,         OUTPUT);     // Fan pwm control
      pinMode(RELAIS,             OUTPUT);     // Relais power control      
      pinMode(SMOKE,               INPUT);     // Smoke sensor

      configTime(MY_TZ, MY_NTP_SERVER);
     
      digitalWrite(RELAIS_LED,HIGH);  
      // pwm frequency
      analogWriteFreq(25000); 
      set_rpm(0);  
      
      // Serielle Schnittstelle init      
      Serial.begin(19200);
      
      Serial.println(".. starting up");
      
       

      // Temperature & Humidity init
      dht.begin();
      // init display
      tft.init(240, 240, SPI_MODE2); 
      tft.setSPISpeed(8500000);
      pinMode(FREQ,                INPUT);     // Fan pwm control
      // achtung wichtig, tft class initialisiert Mosi um, deshalb danach Init

      tft.setTextSize(4);
      tft.setTextColor(ST77XX_ORANGE);
      tft.setRotation(1);               // turn disply 90 degree left and blank screen
      tft.setTextWrap(false);
      tft.fillScreen(ST77XX_BLACK);

      tft.setCursor(0, 0);
      tft.println(Version);
      tft.println("Startup");
      delay(1000);

      // Fan off
      analogWrite(PWM , 255);
      
      Serial.println("Init WiFimanager");
      tft.println("WiFi");
      init_WiFiManger();

      init_OTA(); 
      Serial.println("Init OTA");
      tft.println("OTA");

      // Verbindung mit mqtt-Server aufbauen
      setup_mqtt();
      Serial.println("Init MQTT");
      tft.println("MQTT");
      
      // publish version
      set_time_string();
      String out=Version+AppName+" startup at "+dt;
      Serial.println(out);
      out.toCharArray(msg1,75);
      client.publish(mqtt_pub_Version, msg1);
      delay(1000);

      Init_System();
  
  
      
}








/******************************************************************************************************************
  * void loop() 
  * Program Start init
*******************************************************************************************************************/

void loop() 
{
          
          // Stautus lesen und in globale Variable schreiben          
          get_rpm();
         
          set_rpm(solldrehzahl);

          get_Temp_Hum();

          get_FireSense();

          get_SmokeSense();

          check_system();

          show_info_display();

          publish_MQTT_Values();
          
          // Regelmäßig abfragen, ob ein OTA Update ansteht
          ArduinoOTA.handle();
              
          
          //WiFi Verbindung prüfen
          if (!client.connected()) 
          {
            mqtt_reconnect();
          }
          
          
          client.loop();

}






