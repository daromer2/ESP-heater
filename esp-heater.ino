#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "SSD1306Wire.h"
#include <Nextion.h>
#include <NextionPage.h>
#include <NextionButton.h>
#include <SoftwareSerial.h>
#include <NextionText.h>
#include <NextionNumber.h>
#include <NextionSlider.h>
#include <EEPROM.h>
#include <NextionPicture.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"


//SSD1306Wire  display(0x3c, D1, D5);

SoftwareSerial nextionSerial(13, 12); // RX, TX

Nextion nex(nextionSerial);
//NextionPage pgButton(nex, 0, 0, "pgButton");
NextionButton button1(nex, 0, 2, "b0");
NextionButton button2(nex, 0, 3, "b1");
NextionText temp1(nex, 0, 4, "t1"); // Husets temperatur = givareHus
NextionText temp2(nex, 0, 6, "t3");  // Bör värde = baseTempHouse
NextionText nTempUte(nex, 0, 9, "t5");  // Bör värde = baseTempHouse
NextionText nTempLedning(nex, 0, 10, "t6");  // Bör värde = baseTempHouse

NextionText nWifi(nex,0,16, "t12"); // WIFI status symbol
NextionText nMqtt(nex,0,15, "t11"); // MQTT status symbol
NextionText nPower(nex,0,17, "t13"); // MQTT status symbol


NextionText nShuntPos(nex,0,12, "t8"); // % for the shunt position.

NextionText nTank(nex,0,18, "t14"); // % for the shunt position.
NextionText nT1(nex,0,22, "t17"); // % for the shunt position.
NextionText nT2(nex,0,23, "t18"); // % for the shunt position.
NextionText nT3(nex,0,24, "t19"); // % for the shunt position.
NextionText nT4(nex,0,25, "t20"); // % for the shunt position.

NextionPicture nShuntBild(nex, 0, 21, "p0");


NextionSlider slider(nex, 0, 7, "h0");

#define shuntOka D5
#define shuntMinska D1

//------------------------------------------
//DS18B20
#define ONE_WIRE_BUS D2 //Pin to which is attached a temperature sensor
#define ONE_WIRE_MAX_DEV 15 //The maximum number of devices

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature DS18B20(&oneWire);
int numberOfDevices; //Number of temperature devices found
DeviceAddress devAddr[ONE_WIRE_MAX_DEV];  //An array device temperature sensors
float tempDev[ONE_WIRE_MAX_DEV]; //Saving the last measurement of temperature
float tempDevLast[ONE_WIRE_MAX_DEV]; //Previous temperature measurement
long lastTemp; //The last measurement
long lastSearch;
long shuntTime = 0;
int statusShuntBild = 0;

long lastCheckHouse; 
long durationCheckHouse = 30 * 1000; //Frequenxy of checking if we should change shunt for heating the house -- Also stored in data structure

const int durationTemp = 10 * 1000; //The frequency of temperature measurement
const int durationSearch = 15 * 1000; //The frequency of search measurement
const int durationDisplay = 1000;  
const int shuntRunning = 1400; // Frequency on how often we should should crosschekc if shunt is running

int previousMillisDisplay = 0; 

float stigare;
float tempInne = 28.0;
float tempLedning = 0.0;
float tempUte = 0.0;
float maxLedning = 60.0;

float t1, t2, t3, t4 = 0.0;
int laddgrad =0;

//Define the different onewire sensors below
String givareHus = "28bc849703000033";
String givareUte = "28af699703000007";
String givareLedning = "28a812cb0100001a";  // temperatur ledning till huset
String givareLedningRetur ="28523ecb0100007e";  // Retur från huset
// 2894409703000019 - Pool retur
// 284c7097030000f8  = pool from?
// 284f6897030000e3 = pool to efter Heatr exchanger
// 28eb579703000088 = Pool sensor to heat exchanger

String ot1 ="282a6f970300009a";
String ot2 ="283209ec0100009e";
String ot3 ="28cb33cb010000b5";
String ot4 ="28f302ec01000046";

int shuntPos = 0;


bool wifi = false;
bool mqttStatus = false;

bool nextionStatus = false;
bool screenchanged = true;
bool eepromStatus = false;

  struct {
    float baseTempHouse = 20;   // Default temperature in house
    bool heatingOn = false;
    int durationCheckHouse = durationCheckHouse;
    int min = 35;
    int max = 94;
  } data, data2;


/************************* MQTT Settings *********************************/

#define AIO_SERVER      "192.168.10.100"
#define AIO_SERVERPORT  1883                   // use 8883 for SSL
#define AIO_USERNAME    "mqtt"
#define AIO_KEY         "mqtt"
WiFiClient client;
Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);

//Adafruit_MQTT_Publish mqttTempHus = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/temps/hus");

// Setup a feed called 'onoff' for subscribing to changes.
Adafruit_MQTT_Subscribe mqttHeatingOnOff = Adafruit_MQTT_Subscribe(&mqtt, "config/heating");
Adafruit_MQTT_Subscribe mqttTempInne = Adafruit_MQTT_Subscribe(&mqtt, "sensor/inne/temperature");

//------------------------------------------
//WIFI
const char* ssid = "Esperyd";
const char* password = "Esperyd4";

//------------------------------d------------
//HTTP
ESP8266WebServer server(80);

//------------------------------------------
//Convert device id to String
String GetAddressToString(DeviceAddress deviceAddress){
  String str = "";
  for (uint8_t i = 0; i < 8; i++){
    if( deviceAddress[i] < 16 ) str += String(0, HEX);
    str += String(deviceAddress[i], HEX);
  }
  return str;
}

//Setting the temperature sensor
void SetupDS18B20(){
  DS18B20.begin();

  Serial.print("Parasite power is: "); 
  if( DS18B20.isParasitePowerMode() ){ 
    Serial.println("ON");
  }else{
    Serial.println("OFF");
  }
   initDS18B20(0);
}

int calcPanna() {
  float value;
  value = (((t1-data.min)/(data.max-data.min)+(t2-data.min)/(data.max-data.min)+(t3-data.min)/(data.max-data.min)+(t4-data.min)/(data.max-data.min))/4);
  return (int)(value * 100);
}

void MQTT_connect() {
  int8_t ret;

  // Stop if already connected.
  if (mqtt.connected()) {
    mqttStatus = true;
    return;
  } 

  Serial.println("Connecting to MQTT... ");
  if ((ret = mqtt.connect()) != 0) {
    mqttStatus = false;
    Serial.print(mqtt.connectErrorString(ret));
    Serial.println("MQTT failed");
    screenchanged = true;
  } else {
    mqttStatus = true;
    Serial.println("MQTT Connected!");
    screenchanged = true;
  }
}


void initDS18B20(long now) {
if( now - lastSearch > durationSearch ){ //Take a measurement at a fixed time (durationTemp = 5000ms, 5s)
  Serial.println("*** Starting search for sensors ***");
  DS18B20.begin();
  numberOfDevices = DS18B20.getDeviceCount();
  //display.clear();
  lastTemp = millis();
  DS18B20.requestTemperatures();
  float tempC;

  // Loop through each device, print out address
  for(int i=0;i<numberOfDevices; i++){
    // Search the wire for address
    if( DS18B20.getAddress(devAddr[i], i) ){
      //devAddr[i] = tempDeviceAddress;
      Serial.print("Found device ");
      Serial.print(i+1);
      Serial.print("/");
      Serial.print(numberOfDevices);
      Serial.print(" with address: " + GetAddressToString(devAddr[i]));
    }else{
      Serial.print("Found ghost device at ");
      Serial.print(i, DEC);
      Serial.print(" but could not detect address. Check power and cabling");
    }

    //Get resolution of DS18b20
    Serial.print(" Res: ");
    Serial.print(DS18B20.getResolution( devAddr[i] ));

    //Read temperature from DS18b20
    tempC = DS18B20.getTempC( devAddr[i] );
    Serial.print("Temp: ");
    Serial.println(tempC);

  }

   /* display.drawString(0, 0, String(tempC));
    display.drawString(0, 13, String(numberOfDevices));
    display.drawString(0, 26, String(WiFi.localIP()));
    display.display();*/
    Serial.print("Search is over in: ");
    Serial.println(millis()-lastTemp);
      
    checkEeprom();
 
     // Finally check if mqtt is down
    MQTT_connect();
    lastSearch = millis();  //Remember the last time measurement
}

}

bool checkEeprom() {
  EEPROM.get(0, data);
    if (eepromStatus) {
      eepromStatus = false;
      EEPROM.put(0, data);
      EEPROM.commit();
      Serial.println("Eeprom updated");
    } 
}


void mqttSend(String serial, float value) {
if (mqttStatus == false) {
  return;
}
 String topic = serial;
 Serial.print("Sending to MQTT: " + topic + " Value: ");
 Serial.println(value);
// Length (with one extra character for the null terminator)
  int str_len = topic.length() + 1; 
 
  char char_array[str_len];
  topic.toCharArray(char_array, str_len);
  Adafruit_MQTT_Publish mqttTempHus = Adafruit_MQTT_Publish(&mqtt, char_array);

  if (! mqttTempHus.publish(value)) {
    Serial.println("Failed sending to mqtt");
  }

}



void checkHouseTemp() {
static char buffer[6];
  if( millis() - lastCheckHouse > durationCheckHouse ){
        if (tempLedning > maxLedning ) {
          Serial.println("Minska stigare eftersom temp in är för hög");
           shuntTime = millis()+ shuntRunning*3;
           digitalWrite(shuntMinska, HIGH);
        }
        int faktor = int(data.baseTempHouse - tempInne);
        if ( faktor == 0 ) { faktor = 1; }
        else if ( faktor < 0 ) { faktor = faktor * -1; }
        Serial.print("Faktor att öka med :");
        Serial.println(faktor);
        dtostrf(tempInne, 6, 2, buffer);
        temp1.setText(buffer);  //Update Nextion display


        if ( tempInne < data.baseTempHouse - 0.3 && data.heatingOn) {
          Serial.println("För lite - Öka shunt"); 
          if (statusShuntBild != 2 ) {
            nShuntBild.setPictureID(2);
            statusShuntBild = 2;
          } 
           
          if (shuntTime == 0 && (int)shuntPos < 95) { 
            digitalWrite(shuntOka, HIGH);
            delay(shuntRunning);
            digitalWrite(shuntOka, LOW);
            
            } else {
              Serial.println("No use to raise more since its already to high");
            }
          
        } else if (tempInne > data.baseTempHouse + 0.3 && data.heatingOn ) {
          Serial.println("För mycket - Minska shunt");
          if (statusShuntBild != 1 ) {
            nShuntBild.setPictureID(1);
          }
          if (shuntTime == 0 && (int)shuntPos > 8) { 
            digitalWrite(shuntMinska, HIGH);
            delay(shuntRunning);
            digitalWrite(shuntMinska, LOW);
            } else {
              Serial.println("Shunt is way to low so cant lower it");
            }
        } else if (data.heatingOn) {
          Serial.println("Lika - Ändra inget");
          if (statusShuntBild != 3 ) {
            nShuntBild.setPictureID(3);
          }
        }
   lastCheckHouse = millis();     
  }
    
}

//Loop measuring the temperature
void TempLoop(long now){

  if( now - lastTemp > durationTemp ){ //Take a measurement at a fixed time (durationTemp = 5000ms, 5s)
    Serial.print("Reading analog input ");
    String StrshuntPos = (String)(int)(((analogRead(A0)-155.0)/874.0)*100) + "%";
    shuntPos = (int)(((analogRead(A0)-155.0)/874.0)*100);
    Serial.println (StrshuntPos);
    mqttSend("shunt/pos",StrshuntPos.toFloat());
    char buffer[4];
    StrshuntPos.toCharArray(buffer, 4);
    nShuntPos.setText(buffer);  //Update Nextion display
    
    Serial.println("Updating temp data");
      for(int i=0; i<numberOfDevices; i++){
        float tempC = DS18B20.getTempC( devAddr[i] ); //Measuring temperature in Celsius
        if (tempC != 85 && tempC > -50 ) {
          tempDev[i] = tempC; //Save the measured value to the array
          mqttSend("temps/" + GetAddressToString(devAddr[i]),tempDev[i]);
        } 
      }
      DS18B20.setWaitForConversion(false); //No waiting for measurement
      DS18B20.requestTemperatures(); //Initiate the temperature measurement
      
  
    for(int i=0; i<numberOfDevices; i++){  // Go through sensors again and set them to corect variables if needed
      if ( givareHus == GetAddressToString(devAddr[i]) ) { //Leta reda på positionen för Husets temperatur
          //Serial.println("Dont do anything with this for now");    
        } else if ( givareUte == GetAddressToString(devAddr[i]) ) {
          if (tempUte != tempDev[i]) {
            tempUte = tempDev[i];
            static char buffer[6];
            dtostrf(tempUte, 6, 2, buffer);
            nTempUte.setText(buffer);  //Update Nextion display
          } 
        } else if ( givareLedning == GetAddressToString(devAddr[i]) ) {
          if (tempLedning != tempDev[i]) {
            tempLedning = tempDev[i];
            static char buffer[6];
            dtostrf(tempLedning, 6, 2, buffer);
            nTempLedning.setText(buffer);  //Update Nextion display
          }
        } else if ( ot1 == GetAddressToString(devAddr[i]) ) {
          t1 = tempDev[i];
        } else if ( ot2 == GetAddressToString(devAddr[i]) ) {
          t2 = tempDev[i];
        } else if ( ot3 == GetAddressToString(devAddr[i]) ) {
          t3 = tempDev[i];
        } else if ( ot4 == GetAddressToString(devAddr[i]) ) {
          t4 = tempDev[i];
        }
          
      }
              dtostrf(calcPanna(), 6, 2, buffer);
        nTank.setText(buffer);  //Update Nextion display
                dtostrf(t1, 6, 2, buffer);
        nT1.setText(buffer);  //Update Nextion display
                dtostrf(t2, 6, 2, buffer);
        nT2.setText(buffer);  //Update Nextion display
                dtostrf(t3, 6, 2, buffer);
        nT3.setText(buffer);  //Update Nextion display
                dtostrf(t4, 6, 2, buffer);
        nT4.setText(buffer);  //Update Nextion display
    lastTemp = millis();  //Remember the last time measurement
    }
}

//------------------------------------------
void HandleRoot(){
  String message = "Number of devices: ";
  message += numberOfDevices;
  message += "\r\n<br>";
  char temperatureString[6];

  message += "<table border='1'>\r\n";
  message += "<tr><td>Device id</td><td>Temperature</td></tr>\r\n";
  for(int i=0;i<numberOfDevices;i++){
    dtostrf(tempDev[i], 2, 2, temperatureString);
    Serial.print( "Sending temperature: " );
    Serial.println( temperatureString );

    message += "<tr><td>";
    message += GetAddressToString( devAddr[i] );
    message += "</td>\r\n";
    message += "<td>";
    message += temperatureString;
    message += "</td></tr>\r\n";
    message += "\r\n";
  }
  message += "</table>\r\n";
  
  server.send(200, "text/html", message );
}

void HandleNotFound(){
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/html", message);
}


void callback(NextionEventType type, INextionTouchable *widget)
{
  if (type == NEX_EVENT_PUSH)
  {
    button1.setText("Hyah!");
  }
  else if (type == NEX_EVENT_POP)
  {
    //button1.setText("You pressed me");
    data.baseTempHouse = data.baseTempHouse +0.5;
    eepromStatus = true;
    static char buffer[6];
    dtostrf(data.baseTempHouse, 6, 2, buffer);
    temp2.setText(buffer);
    dtostrf(tempInne, 6, 2, buffer);
    slider.setValue(data.baseTempHouse);
  }
}

void callbackPower(NextionEventType type, INextionTouchable *widget)
{
 if (type == NEX_EVENT_POP)
  {
    if (data.heatingOn) {
      nPower.setBackgroundColour(NEX_COL_RED);
      data.heatingOn = false;
      eepromStatus = true; //Make sure we write down the eeprom on next check 
      mqttSend("config/heaterStatus",0);
      shuntTime = millis() + 100000;
      digitalWrite(shuntMinska, HIGH);
    } else {
      nPower.setBackgroundColour(NEX_COL_GREEN);
      data.heatingOn = true;
      eepromStatus = true; //Make sure we write down the eeprom on next check 
      mqttSend("config/heaterStatus",1);
    }    
  }
}



void callback3(NextionEventType type, INextionTouchable *widget)
{
  if (type == NEX_EVENT_PUSH)
  {
    //digitalWrite(13, HIGH);
    button1.setText("Hyah!");
  }
  else if (type == NEX_EVENT_POP)
  {
//   button2.setText("Öka");
    data.baseTempHouse = data.baseTempHouse -0.5;
    eepromStatus=true;
    static char buffer[6];
    dtostrf(data.baseTempHouse, 6, 2, buffer);
    temp2.setText(buffer);
    dtostrf(tempInne, 6, 2, buffer);
    slider.setValue(data.baseTempHouse);

    
  }
}


void callback2(NextionEventType type, INextionTouchable *widget)
{
  if (type == NEX_EVENT_PUSH)
  {

    //button1.setText(String(slider.getValue()));
    Serial.print("Nummret : ");
    Serial.println(slider.getValue());
  }
  else if (type == NEX_EVENT_POP)
  {
    Serial.println(slider.getValue());
    data.baseTempHouse = slider.getValue();
    eepromStatus = true;
    static char buffer[6];
    dtostrf(data.baseTempHouse, 6, 2, buffer);
    temp2.setText(buffer);
    dtostrf(tempInne, 6, 2, buffer);
    
  }
}

void updateScreen() {
    if (!nextionStatus) {
      Serial.println("Reinit nextion display");
      if (nex.init()) {
        nextionStatus=true;
        
      }
    } else {

    static char buffer[6];
    //dtostrf(slider.getValue(), 6, 2, buffer);
    dtostrf(data.baseTempHouse, 6, 2, buffer);
    temp2.setText(buffer);
    dtostrf(tempInne, 6, 2, buffer);
    temp1.setText(buffer);  //Update Nextion display
    dtostrf(tempUte, 6, 2, buffer);
    nTempUte.setText(buffer);  //Update Nextion display
    dtostrf(tempLedning, 6, 2, buffer);
    nTempLedning.setText(buffer);  //Update Nextion display
    slider.setValue(data.baseTempHouse);

    if (data.heatingOn) {
      nPower.setBackgroundColour(NEX_COL_GREEN);
    } else {
      nPower.setBackgroundColour(NEX_COL_RED);
    }
    if (mqttStatus) {
      nMqtt.setBackgroundColour(NEX_COL_GREEN);
    } else {
      nMqtt.setBackgroundColour(NEX_COL_RED);
    }

Serial.println("Checking wifi");
    if (WiFi.status() == WL_CONNECTED) {
      //nWifi.bco = 7874; // green
      nWifi.setBackgroundColour(NEX_COL_GREEN);
    } else {
      nWifi.setBackgroundColour(NEX_COL_RED);
      //nWifi.bco = 63553; // yellow
    }
  
  screenchanged = false;
    }
}

void checkMQTT() {
 Adafruit_MQTT_Subscribe *subscription;
  while ((subscription = mqtt.readSubscription(10))) {
    if (subscription == &mqttHeatingOnOff) {
      Serial.print(" MQTT read Heating: ");
      Serial.println((char *)mqttHeatingOnOff.lastread);

      if (strcmp((char *)mqttHeatingOnOff.lastread, "2") == 0) {
      if (data.heatingOn) {
        mqttSend("config/heaterStatus",1);
      } else {
        mqttSend("config/heaterStatus",0);
      } 
     }else if (strcmp((char *)mqttHeatingOnOff.lastread, "1") == 0) {
         nPower.setBackgroundColour(NEX_COL_GREEN);
        data.heatingOn = true;//Make sure we write down the eeprom on next check 
        eepromStatus = true; 
      }
      else if (strcmp((char *)mqttHeatingOnOff.lastread, "0") == 0) {
        nPower.setBackgroundColour(NEX_COL_RED);
        data.heatingOn = false;
        eepromStatus = true;  //Make sure we write down the eeprom on next check 
        shuntTime = millis() + 30000;
        digitalWrite(shuntMinska, HIGH);
      }
    } else if (subscription == &mqttTempInne){
      tempInne = atof((char *)mqttTempInne.lastread);
      Serial.print(" MQTT read temperature: ");
      Serial.println((char *)mqttTempInne.lastread);
    }
    
  
}
}
//------------------------------------------
//***********************************************************************************
//***********************************************************************************
//***********************************************************************************


void setup() {
  //Setup Serial port speed
  Serial.begin(115200);

  //Setup WIFI
  Serial.println("Setting up wifi");
  WiFi.begin(ssid, password);
delay(5500);
  //Wait for WIFI connection
  /*while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }*/

  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  server.on("/", HandleRoot);
  server.onNotFound( HandleNotFound );
  server.begin();
  Serial.println("HTTP server started at ip " + WiFi.localIP().toString() );

// Below should be changed to read a json with config data instead and parse new settings?!
  mqtt.subscribe(&mqttHeatingOnOff);
  mqtt.subscribe(&mqttTempInne);
  MQTT_connect();
  
  //Setup DS18b20 temperature sensor
  SetupDS18B20();

  nextionSerial.begin(115200);
  if (nex.init()) {
    nextionStatus = true;
  }

  // Nextion callbacks - Need to clean this code up
  button1.attachCallback(&callback3);
  button2.attachCallback(&callback);
  slider.attachCallback(&callback2);
  nPower.attachCallback(&callbackPower);


  // Init Eprom and check if we got temperature stored. if not set then set it. 
  
  EEPROM.begin(512);
  EEPROM.get(0, data);
  if (data.baseTempHouse >40 || data.baseTempHouse < 10 ) {  // If the eeprom doesnt have proper data in temp then reset them all!
    data = data2;
    EEPROM.put(0, data);
    EEPROM.commit();
  }

  // Set output pins for controlling the shunt
  pinMode(shuntOka, OUTPUT);
  pinMode(shuntMinska, OUTPUT);  
  
  Serial.println("Setup done");
}
//***********************************************************************************
//***********************************************************************************
//***********************************************************************************

void loop() {
  yield();
  long t = millis();
   
  //server.handleClient();
  
  TempLoop( t );  // Update temp values
  checkHouseTemp(); //Kolla om vi behöver justera värmen till huset 
  nex.poll();

   if (shuntTime >0 && shuntTime < t) {
    Serial.println("Need to stop shunt movement");
    digitalWrite(shuntOka, LOW); 
    digitalWrite(shuntMinska, LOW); 
    shuntTime = 0;
   }

   // Check if we have calls waiting to be fetched via MQTT
   if (mqttStatus) {
      checkMQTT();
   }
   initDS18B20(t); // Check for new sensors


 if (screenchanged) {
  previousMillisDisplay = millis(); 
  updateScreen();
  Serial.print("*** Update screen took: "); 
  Serial.println( millis() - previousMillisDisplay);
 }
}
