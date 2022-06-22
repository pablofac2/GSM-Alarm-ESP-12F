/*
platformio.ini:
lib_deps = 
	ottowinter/ESPAsyncTCP-esphome@^1.2.3
	ottowinter/ESPAsyncWebServer-esphome@^2.1.0
	jwrw/ESP_EEPROM@^2.1.1

ESP_EEPROM.h lib dependency:
  #include <string.h>
*/
#include <ESP_EEPROM.h>     //En esta librería agregué #include string.h
#include <ESP8266WiFi.h>
#include <coredecls.h>         // crc32()
#include <PolledTimeout.h>
#include <include/WiFiState.h> // WiFiState structure details
#include <Arduino.h>
#ifdef ESP32
  #include <WiFi.h>
  #include <AsyncTCP.h>
#else
  #include <ESP8266WiFi.h>
  #include <ESPAsyncTCP.h>
#endif
//#include <ESP8266WebServer.h>
#include <ESPAsyncWebServer.h>
// enter your WiFi configuration below

const char* AP_SSID = "ESP8266_Wifi";  // AP SSID here
const char* AP_PASS = "123456789";  // AP password here
const char* PARAM_INPUT_1 = "input1";
IPAddress webserver_IP(0, 0, 0, 0); // Default IP in AP mode is 192.168.4.1
//ESP8266WebServer server(80);
AsyncWebServer server(80);
//IPAddress gateway(0, 0, 0, 0);
//IPAddress subnet(0, 0, 0, 0);
//IPAddress dns1(0, 0, 0, 0);
//IPAddress dns2(0, 0, 0, 0);
//submitMessage():  when you submit the values, a window opens saying the value was saved, instead of being redirected to another page.
//After that pop up, it reloads the web page so that it displays the current values.
//The processor() is responsible for searching for placeholders in the HTML text and replacing them with actual values saved
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head>
  <title>Alarm Configuration</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  </head><body>
  <form action="/pass" target="hidden-form">
    Type Admin Password: <input type="number" name="htmladminpass">
    <input type="submit" value="Submit">
  </form><br>
</body></html>)rawliteral";
const char config_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head>
  <title>Alarm Config Form</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <script>
    function submitMessage() {
      alert("Configuration Saved!");
      setTimeout(function(){ document.location.reload(false); }, 500);   
    }
  </script>  
  </head><body>
  <form action="/get" target="hidden-form">
    Configuration:<br>
    <textarea name="HTMLConfig" style="height: 800px; width: 300px;">%HTMLConfig%</textarea><br>
    <input type="submit" value="Submit" onclick="submitMessage()">
  </form><br>
  <iframe style="display:none" name="hidden-form"></iframe>
</body></html>)rawliteral";
//In this case, the target attribute and an <iframe> are used so that you remain on the same page after submitting the form.
//The name that shows up for the input field contains a placeholder %inputString% that will then be replaced by the current value of the inputString variable.
//The onclick=”submitMessage()” calls the submitMessage() JavaScript function after clicking the “Submit” button.
// HTML web page to handle 3 input fields (inputString, inputInt, inputFloat)
//The action attribute specifies where to send the data inserted on the form after pressing submit. In this case, it makes an HTTP GET request to /get?input1=value. The value refers to the text you enter in the input field.
String HTMLAdminPass = "";
String HTMLConfig = "";

#define TIMEOUT 99
#define ERROR 0
#define NOT_READY 1
#define READY 2
#define CONNECT_OK 3
#define CONNECT_FAIL 4
#define ALREADY_CONNECT 5
#define SEND_OK 6
#define SEND_FAIL 7
#define DATA_ACCEPT 8
#define CLOSED 9
#define READY_TO_RECEIVE 10 // basically SMSGOOD means >
#define OK 11

#define SIM800baudrate 9600   //too fast generates issues when receibing SMSs, buffer is overloaded and the SMS AT arrives incomplete
#define DEBUGbaudrate 115200
#define SLEEP_TIME_MS 1000 //mili seconds of light sleep periods between input readings

#define SIZEOF_NAME 10    //util characters (witout termination char)
#define SIZEOF_PHONE 15   //util characters (witout termination char)
#define SIZEOF_CALLPHONE 3
#define SIZEOF_SMSPHONE 3
#define SIZEOF_DISARMPHONE 5
#define SIZEOF_ZONE 5
#define SIZEOF_SIREN 3
#define SIZEOF_PASS 4             //util characters (witout termination char)
#define DEFAULT_ADMINPASS "1234"  //must be SIZEOF_PASS
#define DEFAULT_OPPASS "1234"     //must be SIZEOF_PASS
#define MEMCHECK "A9"             //to ckech if the EEPROM has valid data or it is a new chip

/*
First Advise Duration: segundos  (tiempo del pulso se activación de sirenas dentro del Delay On. ej para sensors de presencia, corto aviso inicial antes del disparo luego de cumplirse el Delay On) (si no es cero, el sensor debe volver a activarse luego de Delay On para disparar sirenas)
First Advise Reset: segundos  (tiempo desde la primer activación para olvidar que se activó)
Delay On: segundos (demora desde activación del sensor hasta disparo de las sirenas) (si First Advise Duration no es 0, debe volver a activarse para disparar sirenas)
Delay Off: segundos (demora desde desactivación del sensor hasta apagado de las sirenas)
Min Duration: segundos (mínimo tiempo de activación de sirenas luego del disparo)
Max Duration: segundos (máximo tiempo de activación de sirenas luego del disparo)
Enabled: on, off
Auto Disable: on, off (en caso de activaciones repetidas)
*/
struct propZone { //26      __attribute((__packed__))
  char Name[SIZEOF_NAME+1];  //11
  bool Enabled;   //1
  bool AutoDisable; //1
  bool TriggerNC; //1??
  uint16_t FirstAdviseDurationSecs; //2
  uint16_t FirstAdviseResetSecs;
  uint16_t DelayOnSecs;
  uint16_t DelayOffSecs;
  uint16_t MinDurationSecs;
  uint16_t MaxDurationSecs;
};
struct propSiren {  //19      __attribute((__packed__))
  char Name[SIZEOF_NAME+1];  //11 1 more for the “null-terminated” char
  bool Enabled; //1
  bool Delayed;
  uint16_t PulseSecs; //2
  uint16_t PauseSecs;
  uint16_t MaxDurationSecs;
};
struct phoneNumber {  //14      __attribute((__packed__))
  char Number[SIZEOF_PHONE+1];    //1 more for the “null-terminated” char
};
struct propCaller { //160   __attribute((__packed__))
  bool GSMEnabled;
  bool CALLOnAlarm;
  phoneNumber CALLPhone[SIZEOF_CALLPHONE]; //14*3=42
  bool SMSOnAlarm;
  phoneNumber SMSPhone[SIZEOF_SMSPHONE];  //42
  bool CALLAnswer;
  bool SMSResponse;
  bool CALLArmDisarm;
  phoneNumber CALLArmDisarmPhone[SIZEOF_DISARMPHONE]; //70
};
struct propAlarm {  //361     MEDIDO PACKED:  //  MEDIDO SIN PACKED: 386    __attribute((__packed__))
  char MemCheck[2];
  char AdminPass[SIZEOF_PASS+1];  //5
  char OpPass[SIZEOF_PASS+1]; //5
  uint16_t AutoArmDelaySecs;  //2
  propZone Zone[SIZEOF_ZONE]; //5*26 = 130
  propSiren Siren[SIZEOF_SIREN]; //3*19 = 57
  propCaller Caller;  //160
} alarmConfig;

/*
uint32_t timeout = 30E3;  // 30 second timeout on the WiFi connectio
const uint32_t blinkDelay = 100; // fast blink rate for the LED when waiting for the user
//esp8266::polledTimeout::periodicMs blinkLED(blinkDelay);  // LED blink delay without delay()
esp8266::polledTimeout::oneShotMs altDelay(blinkDelay);  // tight loop to simulate user code
esp8266::polledTimeout::oneShotMs wifiTimeout(timeout);  // 30 second timeout on WiFi connection
// use fully qualified type and avoid importing all ::esp8266 namespace to the global namespace
*/
unsigned long startT;
//const String PHONE = "+543414681709";
String smsStatus,senderNumber,receivedDate,msg;
static const uint8_t _responseInfoSize = 12; 
const String _responseInfo[_responseInfoSize] =
    {"ERROR",
    "NOT READY",
    "READY",
    "CONNECT OK",
    "CONNECT FAIL",
    "ALREADY CONNECT",
    "SEND OK",
    "SEND FAIL",
    "DATA ACCEPT",
    "CLOSED",
    ">",
    "OK"};
byte _checkResponse(uint16_t timeout);

//ESP8266 Status:
bool ESP_WIFI = false;      //Wifi enabled during start up (120 secs)
bool ESP_DISARMED = false;  //Alarm disarmed
bool ESP_ARMED = false;     //Alarm armed
bool ESP_FIREDELAY = false; //Alarm fired delay (delayed zone activated)
uint32_t ESP_FIREDELAY_MILLIS; //Alarm fired delay millis start
bool ESP_FIRED = false;     //Alarm fired (siren activated)
uint32_t ESP_FIRED_MILLIS;  //Alarm fired millis
bool ESP_FIREDTOUT = false; //Alarm fired time out (siren silenced after max siren time)
//uint8_t ESPStatus = ESP_WIFI;
bool ReadyToSleep = false;

//SIM800 status:
bool SIM_INACTIVE = false;       //SIM800 not responding AT commands
bool SIM_ERROR = false;       //SIM800 not responding AT commands
bool SIM_SLEEPING = false;    //SIM800 sleeping
bool SIM_CALLING = false;     //SIM800 in a call
//uint8_t SIMStatus = SIM_INACTIVE;

static String Sim800_Buffer_Array[50];
static int Sim800_Buffer_Count = 0;
String DTMFs = "";
bool waitingCPAS = false;
bool sleepTime = false;

//ESP8266 NodeMCU Wemos D1 Mini pinout:
// D0/GPIO16 (no interrupts to wake up), D1/GPIO5, D2/GPIO4, D3/GPIO0, D4/GPIO2 (built-in LED), D6/GPIO12, D7/GPIO13

#define DEBUG  // prints WiFi connection info to serial, uncomment if you want WiFi messages
#ifdef DEBUG
  #define DEBUG_PRINTLN(x)  Serial.println(x)
  #define DEBUG_PRINT(x)  Serial.print(x)
  #define DEBUG_FLUSH Serial.flush()
  #include <SoftwareSerial.h>
  #define rxPin D1 //D1 = GPIO5  al tx del SIM800   WHEN CHANGING THIS Dx, ALSO CHANGE "PIN_FUNC_SELECT" ON Setup().
  #define txPin D2 //D2 = GPIO4  al rx del SIM800   WHEN CHANGING THIS Dx, ALSO CHANGE "PIN_FUNC_SELECT" ON Setup().
  SoftwareSerial sim800(rxPin,txPin);
  const uint8_t ZONE_PIN[SIZEOF_ZONE] = {D5, D5, D5, D6, D7}; //D1 and D2 needed for Cx with SIM800, so D5 used to replace them
#else
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINT(x)
  #define DEBUG_FLUSH
  HardwareSerial sim800(UART0);
  const uint8_t ZONE_PIN[SIZEOF_ZONE] = {D1, D2, D5, D6, D7};
#endif
bool ZONE_DISABLED[SIZEOF_ZONE]; //if the zone has auto disable function enabled, this array will mask them.
bool ZONE_COUNT[SIZEOF_ZONE]; //to count the number of activations since the alarm was last armed.
uint8_t ZONE_STATE[SIZEOF_ZONE]; //the state of the zones (las/previous reading).

#define SIM800_RING_RESET_PIN D3    //input and output pin, used to reset the sim800
const uint8_t SIREN_PIN[SIZEOF_SIREN] = {D0, D4, D8};
const uint8_t SIREN_DEF[SIZEOF_SIREN] = {HIGH, HIGH, LOW}; //D0 D4 normal HIGH, D8 normal LOW
bool SIREN_FORCED[SIZEOF_SIREN]; //if forced, the output status will be overriden to the oposite of SIREN_DEF despite the alarm status.

//ADC_MODE(ADC_VCC);  //don't connect anything to the analog input pin(s)! allows you to monitor the internal VCC level; it varies with WiFi load
int ZoneStatus[SIZEOF_ZONE];

//#define CAR_ALARM  //it's a Car Alarm, with presence key and blinking led
#ifdef CAR_ALARM
#else
#endif

//Visual Studio Code needs the definition of all functions (but setup and loop):
void Sim800_Buffer_Add(String item);
String Sim800_Buffer_Read();
bool Sim800_Connect();
bool Sim800_enterSleepMode();
bool Sim800_disableSleep();
String Sim800_Wait_Cmd(uint16_t timeout, String cmd);
String Sim800_AnswerString(uint16_t timeout);
byte Sim800_checkResponse(unsigned long timeout);
bool Sim800_setFullMode();
void parseData(String buff);
void extractSms(String buff);
void doAction();
void Espera(unsigned int TiempoMillis);
void Sleep_Forced();
void readVoltage();
void printMillis();
void WakeUpCallBackFunction();
void ConfigDefault(propAlarm &pa);
void ConfigWifi();
void notFound(AsyncWebServerRequest *request);
String processor(const String& var);
void ConfigDefault(propAlarm &pa);
void ConfigToString(propAlarm &pa, String &str);
void StringToConfig(String &str, propAlarm &pa);
void InsertLastLine(String description, String &text, String inserted);
void ExtractFirstLine(String description, String &text, String &extracted);
void InsertExtractLine(String description, String &text, String &ins_ext, bool insert);
void InsertExtractLine(String description, String &text, uint16_t &ins_ext, bool insert);
void InsertExtractLine(String description, String &text, bool &ins_ext, bool insert);
void InsertExtractLine(String description, String &text, char* ins_ext, bool insert);
bool Read_Zones_State();
void Sleep_Prepare();
uint32_t RTCmillis();

void ConfigStringCopy(propAlarm &pa, String &str, bool toString){
  String temp;
  if (toString) str="";
  InsertExtractLine("Auto Arm delay secs", str, pa.AutoArmDelaySecs, toString);
  InsertExtractLine("GSM Enabled", str, pa.Caller.GSMEnabled, toString);
  InsertExtractLine("SMS Response", str, pa.Caller.SMSResponse, toString);
  InsertExtractLine("SMS On Alarm", str, pa.Caller.SMSOnAlarm, toString);
  for(unsigned int i = 0; i < SIZEOF_SMSPHONE; i++)
    InsertExtractLine("SMS Phone " + String(i), str, pa.Caller.SMSPhone[i].Number, toString);
  InsertExtractLine("CALL Answer", str, pa.Caller.CALLAnswer, toString);
  InsertExtractLine("CALL On Alarm", str, pa.Caller.CALLOnAlarm, toString);
  for(unsigned int i = 0; i < SIZEOF_CALLPHONE; i++)
    InsertExtractLine("CALL Phone " + String(i), str, pa.Caller.CALLPhone[i].Number, toString);
  InsertExtractLine("CALL Arm Disarm Enabled", str, pa.Caller.CALLArmDisarm, toString);
  for(unsigned int i = 0; i < SIZEOF_DISARMPHONE; i++)
    InsertExtractLine("CALL Arm Disarm " + String(i), str, pa.Caller.CALLArmDisarmPhone[i].Number, toString);
  for(unsigned int i = 0; i < SIZEOF_ZONE; i++){
    temp = "Zone " + String(i);
    InsertExtractLine(temp + " Name", str, pa.Zone[i].Name, toString);
    InsertExtractLine(temp + " Enabled", str, pa.Zone[i].Enabled, toString);
    InsertExtractLine(temp + " Auto Disable", str, pa.Zone[i].AutoDisable, toString);
    InsertExtractLine(temp + " Trigger Normal Closed", str, pa.Zone[i].TriggerNC, toString);
    InsertExtractLine(temp + " First Advise Duration secs", str, pa.Zone[i].FirstAdviseDurationSecs, toString);
    InsertExtractLine(temp + " First Advise Reset secs", str, pa.Zone[i].FirstAdviseResetSecs, toString);
    InsertExtractLine(temp + " Delay On secs", str, pa.Zone[i].DelayOnSecs, toString);
    InsertExtractLine(temp + " Delay Off secs", str, pa.Zone[i].DelayOffSecs, toString);
    InsertExtractLine(temp + " Min Duration secs", str, pa.Zone[i].MinDurationSecs, toString);
    InsertExtractLine(temp + " Max Duration secs", str, pa.Zone[i].MaxDurationSecs, toString);
  }
  for(unsigned int i = 0; i < SIZEOF_SIREN; i++){
    temp = "Siren " + String(i);
    InsertExtractLine(temp + " Name", str, pa.Siren[i].Name, toString);
    InsertExtractLine(temp + " Enabled", str, pa.Siren[i].Enabled, toString);
    InsertExtractLine(temp + " Delayed", str, pa.Siren[i].Delayed, toString);
    InsertExtractLine(temp + " Pulse secs", str, pa.Siren[i].PulseSecs, toString);
    InsertExtractLine(temp + " Pause secs", str, pa.Siren[i].PauseSecs, toString);
    InsertExtractLine(temp + " Max Duration secs", str, pa.Siren[i].MaxDurationSecs, toString);
  }
}

void InsertExtractLine(String description, String &text, String &ins_ext, bool insert){
  if (insert)
    InsertLastLine(description, text, ins_ext);
  else
    ExtractFirstLine(description, text, ins_ext);
}
void InsertExtractLine(String description, String &text, uint16_t &ins_ext, bool insert){
  if (insert)
    InsertLastLine(description, text, String(ins_ext));
  else{
    String temp;
    ExtractFirstLine(description, text, temp);
    ins_ext = (uint16_t)temp.toInt();
  }
}
void InsertExtractLine(String description, String &text, bool &ins_ext, bool insert){
  if (insert)
    InsertLastLine(description, text, ins_ext? "1" : "0");
  else{
    String temp;
    ExtractFirstLine(description, text, temp);
    ins_ext = temp=="1"? true : false;
  }
}
void InsertExtractLine(String description, String &text, char* ins_ext, bool insert){
  if (insert)
    InsertLastLine(description, text, String(ins_ext));
  else{
    String temp;
    ExtractFirstLine(description, text, temp);
    temp.toCharArray(ins_ext, temp.length()+1);
  }
}

void InsertLastLine(String description, String &text, String inserted){
  text += description + ": " + inserted + "\r";
}

void ExtractFirstLine(String description, String &text, String &extracted){
  int i = text.indexOf(description);
  i = text.indexOf(":", i);
  int j = text.indexOf("\r", i);
  extracted = text.substring(i + 1, j);
  extracted.trim();
}

void ConfigDefault(propAlarm &pa){
  String temp;
  pa.MemCheck[0] = MEMCHECK[0];
  pa.MemCheck[1] = MEMCHECK[1];
  temp = DEFAULT_ADMINPASS;
  temp.toCharArray(pa.AdminPass, temp.length()+1);
  temp = DEFAULT_OPPASS;
  temp.toCharArray(pa.OpPass, temp.length()+1);
  pa.AutoArmDelaySecs = 28800;  //8hs
  for(unsigned int i = 0; i < SIZEOF_ZONE; i++){
    pa.Zone[i].Enabled = true;
    pa.Zone[i].AutoDisable = false;
    pa.Zone[i].DelayOnSecs = 0;
    pa.Zone[i].DelayOffSecs = 0;
    pa.Zone[i].FirstAdviseDurationSecs = 0;
    pa.Zone[i].FirstAdviseResetSecs = 0;
    pa.Zone[i].MinDurationSecs = 0;
    pa.Zone[i].MaxDurationSecs = 0;
    pa.Zone[i].TriggerNC = true;
    temp = "Zona" + i;
    temp.trim();
    temp.toCharArray(pa.Zone[i].Name, temp.length()+1); //1 more for the “null-terminated” char
  }
  for(unsigned int i = 0; i < SIZEOF_SIREN; i++){
    pa.Siren[i].Enabled = true;
    pa.Siren[i].Delayed = true;
    pa.Siren[i].MaxDurationSecs = 600;  //10 minutes
    pa.Siren[i].PulseSecs = 2;
    pa.Siren[i].PauseSecs = 0;
    temp = "Sirena" + i;
    temp.trim();
    temp.toCharArray(pa.Siren[i].Name, temp.length()+1);
  }
  pa.Caller.CALLAnswer = true;
  pa.Caller.CALLArmDisarm = false;
  pa.Caller.CALLOnAlarm = true;
  pa.Caller.GSMEnabled = true;
  pa.Caller.SMSOnAlarm = false;
  pa.Caller.SMSResponse = true;
  temp = "1234";
  for(unsigned int i = 0; i < SIZEOF_DISARMPHONE; i++){
    temp.toCharArray(pa.Caller.CALLArmDisarmPhone[i].Number, temp.length()+1); //1 more for the “null-terminated” char
  }
  for(unsigned int i = 0; i < SIZEOF_CALLPHONE; i++){
    temp.toCharArray(pa.Caller.CALLPhone[i].Number, temp.length()+1);
  }
  for(unsigned int i = 0; i < SIZEOF_SMSPHONE; i++){
    temp.toCharArray(pa.Caller.SMSPhone[i].Number, temp.length()+1);
  }
  DEBUG_PRINT("-");
  for(int i = 0; i<SIZEOF_NAME; i++){
    DEBUG_PRINT(pa.Zone[1].Name[i]);
  }
  DEBUG_PRINTLN("-");
  DEBUG_PRINT("-");
  for(int i = 0; i<SIZEOF_DISARMPHONE; i++){
    DEBUG_PRINT(pa.Caller.CALLArmDisarmPhone[0].Number[i]);
  }
  DEBUG_PRINTLN("-");
  //String variable is char array. You can directly operate on string like a char array:
  //String abc="ABCDEFG";
  //Serial.print(abc[2]); //Prints 'C'
  //char myString [10] = "HELLO";
  //There is no separate “length” field, so many C functions expect the string to be “null-terminated” like this:
  //The overall string size is 10 bytes, however you can really only store 9 bytes because you need to allow for the string terminator (the 0x00 byte). The “active” length can be established by a call to the strlen function. For example:
  //Serial.println ( strlen (myString) );   // prints: 5
  //The total length can be established by using the sizeof operator. For example:
  //Serial.println ( sizeof (myString) );   // prints: 10
  //You can concatenate entire strings by using strcat (string catenate). For example:
  //strcat (myString, "WORLD");
}

void ConfigToEEPROM(propAlarm &pa){
  DEBUG_PRINTLN(F("**** Writing EEPROM ****"));
  EEPROM.put(0, pa);
  if (EEPROM.commit()){
    DEBUG_PRINTLN(F("**** Writing EEPROM OK! ****"));
  }else{
    DEBUG_PRINTLN(F("**** Writing EEPROM FAILED! ****"));
  }
}

void ConfigWifi(){
  DEBUG_PRINT(F("Setting AP (Access Point)…"));
  //set-up the custom IP address ***** ESTO QUIZAS NO HAGA FALTA
  //WiFi.mode(WIFI_AP_STA);
  //WiFi.softAPConfig(webserver_IP, webserver_IP, IPAddress(255, 255, 255, 0));   // subnet FF FF FF 00  
  WiFi.softAP(AP_SSID, AP_PASS);  //Remove the password parameter, if you want the AP (Access Point) to be open
  Espera(100);

  //IPAddress IP = ;
  DEBUG_PRINT(F("Soft AP IP address: "));
  DEBUG_PRINTLN(WiFi.softAPIP());
  
  DEBUG_PRINT(F("Local IP address: "));   // Print ESP8266 Local IP Address
  DEBUG_PRINTLN(WiFi.localIP());

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){  //Route for root / web page
    request->send_P(200, "text/html", index_html, processor);
  });

  server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request){  //Config page /config
    if (HTMLAdminPass == String(alarmConfig.AdminPass))
      request->send_P(200, "text/html", config_html, processor);
  });

  server.on("/pass", HTTP_GET, [](AsyncWebServerRequest *request){ //Send a GET request to <ESP_IP>/pass?HTMLAdminPass=<inputMessage>
    if (request->hasParam("htmladminpass")) {
      HTMLAdminPass = request->getParam("htmladminpass")->value();
      DEBUG_PRINTLN(F("htmladminpass = ") + HTMLAdminPass);
    }
    if (HTMLAdminPass == String(alarmConfig.AdminPass)){
      request->send(200, "text/html", "Admin Password ok.<br><a href=\"/config\">Go to Config Page</a>");
    } else {
      request->send(200, "text/html", "Admin Password NOT OK.<br><a href=\"/\">Return to Home Page</a>");
    }
  });

  server.on("/get", HTTP_GET, [](AsyncWebServerRequest *request){ //Send a GET request to <ESP_IP>/get?input1=<inputMessage>
    if (request->hasParam("HTMLConfig") && HTMLAdminPass == String(alarmConfig.AdminPass)) {
      HTMLConfig = request->getParam("HTMLConfig")->value();
      DEBUG_PRINTLN(F("HTMLConfig = ") + HTMLConfig);
      ConfigStringCopy(alarmConfig, HTMLConfig, false);    //Parse String to alarmConfig
      ConfigToEEPROM(alarmConfig);                      //Write to EEPROM
      ConfigStringCopy(alarmConfig, HTMLConfig, true);    //Parse alarmConfig to String
    }
  });
  server.onNotFound(notFound);
  server.begin(); // Start server
}
void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not found");
}

// Replaces placeholder with stored values
String processor(const String& var){
  if(var == "htmladminpass"){
    return HTMLAdminPass;
  }
  else if(var == "HTMLConfig"){
    return HTMLConfig;
  }
  return String();
}

void setup() {
//usar otra posición de memoria para saber si estaba armada o no la alarma por si se corta la energía

  //PARA ASIGNAR LA FUNCIÓN ADECUADA A CADA PIN (ESTÁN MULTIPLEXADOS, VER EXCEL)
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO0_U, FUNC_GPIO0);
  //PIN_FUNC_SELECT(PERIPHS_IO_MUX_U0TXD_U, FUNC_GPIO1);
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2);
  //PIN_FUNC_SELECT(PERIPHS_IO_MUX_U0RXD_U, FUNC_GPIO3);
  #ifndef DEBUG   //when debuging, D1 and D2 are used to comunicate to SIM800
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO4_U, FUNC_GPIO4);
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO5_U, FUNC_GPIO5);
  #endif
  //PIN_FUNC_SELECT(PERIPHS_IO_MUX_SD_CLK_U, FUNC_GPIO6);
  //PIN_FUNC_SELECT(PERIPHS_IO_MUX_SD_DATA0_U, FUNC_GPIO7);
  //PIN_FUNC_SELECT(PERIPHS_IO_MUX_SD_DATA1_U, FUNC_GPIO8);
  //PIN_FUNC_SELECT(PERIPHS_IO_MUX_SD_DATA2_U, FUNC_GPIO9);
  //PIN_FUNC_SELECT(PERIPHS_IO_MUX_SD_DATA3_U, FUNC_GPIO10);
  //PIN_FUNC_SELECT(PERIPHS_IO_MUX_SD_SD_CMD_U, FUNC_GPIO11);
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDI_U, FUNC_GPIO12); //Use	MTDI	pin	as	GPIO12.
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTCK_U, FUNC_GPIO13); //Use	MTCK	pin	as	GPIO13.
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTMS_U, FUNC_GPIO14); //Use	MTMS	pin	as	GPIO14.
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDO_U, FUNC_GPIO15); //Use	MTDO	pin	as	GPIO15.

  for (int i = 0; i < SIZEOF_ZONE; i++){
    GPIO_DIS_OUTPUT(GPIO_ID_PIN(ZONE_PIN[i]));    //Configura la pata como entrada, traido de Sleep_Forced
    pinMode(GPIO_ID_PIN(ZONE_PIN[i]), INPUT_PULLUP);
    ZONE_DISABLED[i] = false;
    ZONE_COUNT[i] = 0;
  }

  for (int i = 0; i < SIZEOF_SIREN; i++){
    pinMode(GPIO_ID_PIN(SIREN_PIN[i]), OUTPUT);
    digitalWrite(GPIO_ID_PIN(SIREN_PIN[i]), SIREN_DEF[i]);
    SIREN_FORCED[i] = false;
  }
  //GPIO_DIS_OUTPUT(GPIO_ID_PIN(SIM800_RING_RESET_PIN));  because it is input and output
  pinMode(SIM800_RING_RESET_PIN, INPUT_PULLUP); //to read SIM800 RING, later will be set temporarily as output to reset SIM800

  #ifdef DEBUG
    Serial.begin(DEBUGbaudrate);
    //AGREGADO:
    while(!Serial)
    {
      yield();
    }  
    DEBUG_PRINTLN();
    DEBUG_PRINT(F("\nReset reason = "));
    String resetCause = ESP.getResetReason();
    DEBUG_PRINTLN(resetCause);
  #endif

  //Initialize config if EEPROM is empty
  DEBUG_PRINT(F("\nalarmConfig Size= "));
  DEBUG_PRINTLN(sizeof(alarmConfig));

  smsStatus = "";
  senderNumber="";
  receivedDate="";
  msg="";
  Sim800_Connect();
  DEBUG_PRINTLN(F("**** Conectado ****"));
  startT = millis();

  //Initialize EEPROM and read config
  DEBUG_PRINTLN(F("**** Reading EEPROM ****"));
  EEPROM.begin(sizeof(alarmConfig));
  EEPROM.get(0, alarmConfig);
  yield();

  DEBUG_PRINT(MEMCHECK[0]);
  DEBUG_PRINTLN(MEMCHECK[1]);
  DEBUG_PRINT(alarmConfig.MemCheck[0]);
  DEBUG_PRINTLN(alarmConfig.MemCheck[1]);
  if(alarmConfig.MemCheck[0] != MEMCHECK[0] || alarmConfig.MemCheck[1] != MEMCHECK[1]){  //EEPROM empty
    DEBUG_PRINTLN(F("**** EEPROM Empty, loading Default configuration ****"));
    ConfigDefault(alarmConfig);
    ConfigToEEPROM(alarmConfig);
  }
  DEBUG_PRINT(alarmConfig.MemCheck[0]);
  DEBUG_PRINTLN(alarmConfig.MemCheck[1]);

  ConfigStringCopy(alarmConfig, HTMLConfig, true);

  ConfigWifi(); //Wifi initializes after EEPROM reading to have loaded the Alarm Config
  DEBUG_PRINTLN(F("Wifi Conectado"));
}

void loop() {
  String readstr = "";

  //Configuring the ESP to be able to LIGHT SLEEP:
  if (!ReadyToSleep && RTCmillis() > 60000)
    Sleep_Prepare();

  //AGREGADO:
  if ((WiFi.status()!= WL_CONNECTED) && (waitingCPAS == false) && (millis() - startT > 120000)){ //If ringing or in call, do not sleep
    waitingCPAS = true;
    DEBUG_PRINTLN(F("Enviando AT+CPAS"));
    sim800.println(F("AT+CPAS")); // activity of phone: 0 Ready, 2 Unknown, 3 Ringing, 4 Call in progress
    sim800.flush();
    //Espera(5000);
  }
  if (waitingCPAS && (millis() - startT < 20000))
    waitingCPAS = false;          //reseteo porque ya hubo respuesta
  if (sleepTime) { //if () (millis() - startT > 25000) {
    sleepTime = false;

    server.end(); //Ends the AP Web Server, as configuration is only at startup.
    delay(10);
    Espera(500);

//    Sim800_enterSleepMode();      HABILITAT ESTOOO!!!
    
    
    //sim800.end();
    Espera(500);
    
    //disable all timers

  //Set the pins with an output status to input status, i.e., MTDO, U0TXD and GPIO0, before enabling Lightsleep to eliminate the leakage current, so that the power consumption becomes even lower.
    //Sleep_Forced();
    
    //Espera(5000);
    Sleep_Forced();
    //sim800.begin(SIM800baudrate);
    //while(!sim800)
    //{
    //  yield();
    //}      
    //Espera(100);

    Sim800_disableSleep();

    //revisar si estoy en una llamada o si llegó un nuevo mensaje
    startT = millis();
    waitingCPAS = false;
  }
  //if (blinkLED)
  //  digitalWrite(LED, !digitalRead(LED));  // toggle the activity LED
  while(sim800.available()){
    startT = millis();        //reseteo tiempo hasta dormirme
    parseData(sim800.readString());
  }
  #ifdef DEBUG
    while(Serial.available())  {
      readstr = Serial.readString();
      DEBUG_PRINTLN("Enviando: -" + readstr + "-");
      sim800.println(readstr);
      startT = millis();        //reseteo tiempo hasta dormirme
    }
  #endif
  readstr = Sim800_Buffer_Read();
  while(readstr != ""){
    parseData(readstr);
    readstr = Sim800_Buffer_Read();
  }
}

bool Sim800_Connect(){
  //byte result;
  //unsigned long startT;
  DEBUG_PRINTLN(F("Conectando SIM800"));
  sim800.begin(SIM800baudrate);//115200
  delay(120);
  while(Sim800_checkResponse(5000)!=TIMEOUT);   //5 secs without receibing anything, See SIM800 manual, wait for SIM800 startup
  unsigned long t = millis();
  DEBUG_PRINTLN(F("Enviando AT"));
  sim800.println("AT");
  sim800.flush();
  //delay(120);
  while(Sim800_checkResponse(2000)!=OK){
    if (millis() - t > 60000){
      DEBUG_PRINTLN(F("AT sin respuesta"));
      return false;                             //Timeout connecting to SIM800
    }
  }
  DEBUG_PRINTLN(F("Enviando AT+CFUN=1"));       //Set Full Mode
  sim800.println(F("AT+CFUN=1"));
  sim800.flush();
  DEBUG_PRINTLN(F("Enviando AT+CMGF=1"));
  sim800.println(F("AT+CMGF=1"));                  //SMS text mode
  sim800.flush();
  DEBUG_PRINTLN(F("Enviando ATS0=2"));
  sim800.println(F("ATS0=2"));                  //Atender al segundo Ring
  sim800.flush();
  DEBUG_PRINTLN(F("Enviando AT+DDET=1,0,0,0"));
  sim800.println(F("AT+DDET=1,0,0,0"));                  //Detección de códigos DTMF
  sim800.flush();
  DEBUG_PRINTLN(F("Enviando AT+IPR?"));
  sim800.println(F("AT+IPR?"));                   //Auto Baud Rate Serial Port Configuration (0 is auto)
  sim800.flush();
  return true;
}

bool Sim800_enterSleepMode(){
  sim800.println(F("AT+CSCLK=2")); // enable automatic sleep
  if(Sim800_checkResponse(5000) == OK){
    DEBUG_PRINTLN(F("SIM800L sleeping OK"));
    return true;
  } else {
    DEBUG_PRINTLN(F("SIM800L sleeping FAILED"));
    return false;  
  }
}

bool Sim800_disableSleep(){
  sim800.println(F("AT"));    // first we need to send something random for as long as 100ms
  //sim800.flush();
  //Espera(120);                // this is between waking charaters and next AT commands  //120
  Sim800_checkResponse(2000); // just incase something pops up, next AT command has to be sent before 5secs after first AT.
  sim800.println(F("AT+CSCLK=0"));
  if(Sim800_checkResponse(5000) == OK){
    DEBUG_PRINTLN(F("SIM800L awake OK"));
    return true;
  } else {
    DEBUG_PRINTLN(F("SIM800L awake FAILED"));
    return false;  
  }
}

byte Sim800_checkResponse(unsigned long timeout){
  // This function handles the response from the radio and returns a status response
  uint8_t Status = 99; // the defualt stat and it means a timeout
  unsigned long t = millis();
  while(millis()-t<timeout)
  {
    yield();
    //count++;
    if(sim800.available()) //check if the device is sending a message
    {
      String tempData = sim800.readString(); // reads the response
      DEBUG_PRINTLN(tempData);
      //rta=tempData;
      //char *mydataIn = strdup(tempData.c_str()); // convertss to char data from
      /*
      * Checks for the status response
      * Response are - OK, ERROR, READY, >, CONNECT OK
      * SEND OK, DATA ACCEPT, SEND FAIL, CLOSE, CLOSED
      * note_ LOCAL iP COMMANDS HAS NO ENDING RESPONSE 
      * ERROR - 0
      * READY - 1
      * CONNECT OK - 2
      * SEND OK - 3
      * SEND FAIL - 4
      * CLOSED - 5
      * > - 6
      * OK - 7
      */
      for (byte i=0; i<_responseInfoSize; i++)
      {
        //if((strstr(mydataIn, _responseInfo[i])) != NULL)
        if(tempData.indexOf(_responseInfo[i]) > -1)// != NULL)
        {
          Status = i;
          DEBUG_PRINTLN("Status number: " + i);
          return Status;
        }
      }
      Sim800_Buffer_Add(tempData);  //Appeared somthing different than expected (sms or call), save for later
    }
  }
  return Status;
}

//AGREGADO:
void parseData(String buff){
  //La respuesta del SIM800l es "[comando enviado]\r[respuesta]"
  DEBUG_PRINTLN("respuesta completa recibida: -" + buff + "-");

  String buff2="";
  int index;
  //////////////////////////////////////////////////
  //Remove sent "AT Command" from the response string.
  buff.trim();
  buff.toUpperCase();
  while(buff.substring(0,2) == "AT"){ //por si hay varios comandos encadenados que ese enviaron juntos
    index = buff.indexOf("\r");
    if(index>-1){
      buff.remove(0, index+2);
    } else {
      break;
    }
    buff.trim();
    DEBUG_PRINTLN("respuesta sin comando enviado: -" + buff + "-");
  }
  //////////////////////////////////////////////////

  index = buff.indexOf("\r");
  if(index > -1 && index < int(buff.length()-1)){  //hay más de 1 repuesta, la divido para analizar luego el final
    buff2 = buff.substring(index+2);
    buff2.trim();
    DEBUG_PRINTLN("queda para después: -" + buff2 + "-");
    buff.remove(index);
    buff.trim();
  }
  DEBUG_PRINTLN("queda para ahora: -" + buff + "-");
  //////////////////////////////////////////////////
  if(buff == "RING"){
    //resetear dtmf
    DTMFs="";
    DEBUG_PRINTLN("RING DETECTADO");
  }
  else if(buff == "NO CARRIER"){
    DTMFs="";
    //resetesar dtmf
    DEBUG_PRINTLN("FIN LLAMADA DETECTADO");
  }
  else if(buff == "OK"){
    DEBUG_PRINTLN("OK DETECTADO");

  }
  else{
    index = buff.indexOf(":");
    if(index>0){                                  //There's a command response
      String cmd = buff.substring(0, index);
      DEBUG_PRINTLN("Comando: -" + cmd + "-");
      //DEBUG_PRINTLN("Buffer restante: -" + buff + "-");
      cmd.trim();
      buff.remove(0, index+2);
      buff.trim();
      /*
      //DEBUG_PRINTLN("Buffer restante sin comando: -" + buff + "-");
      index = buff.indexOf("\r");
      //DEBUG_PRINTLN("index: -" + String(index) + "-");
      if(index>-1)
        buff.remove(index);
      buff.trim();*/
      DEBUG_PRINTLN("Valor: -" + buff + "-");

      if(cmd == "+IPR"){
        String sim800brstr = String(SIM800baudrate);
        sim800brstr.trim();
        if(buff != sim800brstr){
          DEBUG_PRINTLN("Ajustando velocidad Serial SIM800 por defecto en " + sim800brstr);
          sim800.println(F("AT+IPR=") + sim800brstr); //115200
          sim800.flush();
          delay(120);
          sim800.println(F("AT&W"));
          sim800.flush();
          delay(120);
        }
      }
      else if(cmd == "+CPAS"){
        if(buff != "3" && buff != "4"){  //Ringing or in call => wait to sleep
          sleepTime = true;
          DEBUG_PRINTLN("No estoy Sonando ni en llamada, ir a dormir");
        }
      }
      else if(cmd == "+DTMF"){
        //acumular dtmf
        DTMFs += buff;
        DEBUG_PRINTLN("DTMF DETECTADO: " + buff);
        DEBUG_PRINTLN("DTMFs: " + DTMFs);
        if(DTMFs=="1234"){
          sim800.println(F("AT+CLDTMF=10,\"1,5,1\""));
          sim800.flush();
          delay(120);
          DTMFs="";
        }
      }
      else if(cmd == "+CMTI"){
        //get newly arrived memory location and store it in temp
        index = buff.indexOf(",");
        String temp = buff.substring(index+1, buff.length()); 
        temp = "AT+CMGR=" + temp; //+ "\r"; 
        //get the message stored at memory location "temp"
        DEBUG_PRINTLN("Pidiendo mensaje: " + temp);
        sim800.println(temp);
      }
      else if(cmd == "+CMGR"){
        extractSms(buff + "\n\r" + buff2);  //en buff2 está el mensaje

        index = buff2.indexOf("\r");  //saco el mensaje de buff2 y veo si quedó algo más:
        if(index > -1 && index < int(buff2.length()-2)){  //hay más de 1 repuesta, la divido para analizar luego el final
          buff2.remove(0,index+2);
          DEBUG_PRINTLN("queda para después: -" + buff2 + "-");
        } else {
          buff2 = "";
        }
        //if(senderNumber == PHONE){
          doAction();
        //}
      }
    }
  }
  if(buff2 != "")
    parseData(buff2);
}

void extractSms(String buff){
  unsigned int index;
  
  index = buff.indexOf(",");
  smsStatus = buff.substring(1, index-1); 
  buff.remove(0, index+2);
  
  senderNumber = buff.substring(0, 13);
  buff.remove(0,19);
  
  receivedDate = buff.substring(0, 20);
  buff.remove(0,buff.indexOf("\r"));
  buff.trim();
  
  index =buff.indexOf("\n\r");
  buff = buff.substring(0, index);
  buff.trim();
  msg = buff;
  buff = "";
  msg.toLowerCase();
}

void doAction(){
  DEBUG_PRINT(F("mensaje: "));
  DEBUG_PRINTLN("-" + msg + "-");
  if(msg == "relay1 off"){  
    //DEBUG_PRINTLN(F("relay1 off ENTENDIDO"));
    //digitalWrite(LED, LOW);
    //Reply("Relay 1 has been OFF");
  }
  else if(msg == "llamame"){
    sim800.println(F("ATD+543414681709;")); //make call  println evita tener q poner \r al final
    DEBUG_PRINTLN(F("llamada iniciada"));
    Espera(15000);
    sim800.println(F("ATH")); //hang up
    DEBUG_PRINTLN(F("llamada finalizada"));
  }
  else if(msg == "sleep"){

  }
  smsStatus = "";
  senderNumber="";
  receivedDate="";
  msg="";  
}

void Sleep_Prepare(){
  delay(1);                                   //Needs a small delay at the begining!
  //gpio_pin_wakeup_disable();                //If only timed sleep, not pin interrupt
  wifi_station_disconnect();                  //disconnect wifi
  wifi_set_opmode(NULL_MODE);							 	  //set WiFi	mode	to	null	mode.
  wifi_fpm_set_sleep_type(LIGHT_SLEEP_T);		  //This API can only be called before wifi_fpm_open 	light	sleep
  wifi_fpm_open();													  //Enable force sleep function
  for (int i = 0; i < SIZEOF_ZONE; i++){       //If TriggerNC = true, zones must be at ground, opening ground loop wakes the ESP and fires the alarm.
    if (alarmConfig.Zone[i].Enabled){
      wifi_enable_gpio_wakeup(GPIO_ID_PIN(ZONE_PIN[i]), alarmConfig.Zone[i].TriggerNC ? GPIO_PIN_INTR_HILEVEL : GPIO_PIN_INTR_LOLEVEL);
    }
  }
  wifi_enable_gpio_wakeup(GPIO_ID_PIN(SIM800_RING_RESET_PIN), GPIO_PIN_INTR_LOLEVEL); //Sending this GPIOs to ground will wake the ESP.
  wifi_fpm_set_wakeup_cb(WakeUpCallBackFunction);	//This API can only be called when force sleep function is enabled, after calling wifi_fpm_open. Will be called after system wakes up only if the force sleep time out (wifi_fpm_do_sleep and the parameter is not 0xFFFFFFF)
  ReadyToSleep = true;
}

void Sleep_Forced() {
  DEBUG_PRINTLN(F("Going to Sleep at ms: ") + String(RTCmillis()));
  DEBUG_FLUSH;

  //If there's any timed task pending, go to timed light sleep. Calculate the sleeping period (max ...)
  //DO NOT SLEEP IF:
  //  - in a call
  //  - alarm fired (waiting to sound or sounding)
  //  - wifi enabled at startup period

  extern os_timer_t *timer_list;  //for timer-based light sleep to work, the os timers need to be disconnected
  timer_list = nullptr;

  //If forced light sleep without timed wake up, only external gpio wake:
  //sint8 res = wifi_fpm_do_sleep(0xFFFFFFF);
  //delay(10);

  //If timed light sleep and external gpio wake:
  sint8 res = wifi_fpm_do_sleep(SLEEP_TIME_MS * 1000);  //microseconds
  delay(SLEEP_TIME_MS + 1);  // it goes to sleep //The system will not enter sleep mode instantly when force-sleep APIs are called, but only after executing an idle task.
  
  DEBUG_PRINTLN(F("Sleep result (0 is ok): ") + String(res));  // the interrupt callback hits before this is executed
  //0, setting successful;
  //-1, failed to sleep, sleep status error;
  //-2, failed to sleep, force sleep function is not enabled
}

void WakeUpCallBackFunction()
{
  DEBUG_PRINTLN(F("Woke Up - CallBack Function executed at ms: ") + String(RTCmillis()));
  DEBUG_FLUSH;
  //wifi_fpm_close();					 	        //Only if not sleeping gain, disable force sleep function
  //wifi_set_opmode(STATION_MODE);			//If need to set station mode
  //wifi_station_connect();							//If need to connect to AP
}

uint32_t RTCmillis() {
  return (system_get_rtc_time() * (system_rtc_clock_cali_proc() >> 12)) / 1000;  // system_get_rtc_time() is in us (but very inaccurate anyway)
}

bool Read_Zones_State(){
  int s;
  bool zonesOk = true;  //not any zone triggered
  for (int i = 0; i < SIZEOF_ZONE; i++){
    if (alarmConfig.Zone[i].Enabled && !ZONE_DISABLED[i]){
      s = digitalRead(GPIO_ID_PIN(ZONE_PIN[i]));
      if ((alarmConfig.Zone[i].TriggerNC && s == HIGH) || (!alarmConfig.Zone[i].TriggerNC && s == LOW)){
        zonesOk = true;
        if (ZONE_STATE[i] != s){  //Zone state changed
          if (ESP_ARMED){
            if (!ESP_FIREDELAY){
              ESP_FIREDELAY = true;
              ESP_FIREDELAY_MILLIS = RTCmillis();
            }
          }
        }
      }
      ZONE_STATE[i] = s;
    }
    ZoneStatus[i] = s;
  }
  //Receibing call or sms?
  s = digitalRead(GPIO_ID_PIN(SIM800_RING_RESET_PIN));  //si es SMS dura muy poco
  return !zonesOk;
}

/*void Sleep_Forced_NotWorking() {
  //LEER:
  //Set the pins with an output status to input status, i.e., MTDO, U0TXD and GPIO0, before enabling Lightsleep to eliminate the leakage current, so that the power consumption becomes even lower.
  DEBUG_PRINTLN(F("\nForced Light Sleep, wake with GPIO interrupt"));
  WiFi.mode(WIFI_OFF);  // you must turn the modem off; using disconnect won't work
  readVoltage();  // read internal VCC
  DEBUG_PRINTLN(F("CPU going to sleep, pull WAKE_UP_PIN low to wake it (press the switch)"));
  printMillis();  // show millis() across sleep, including Serial.flush()
  
  //gpio_pin_wakeup_disable();  //Not needed, commented
  wifi_fpm_set_sleep_type(LIGHT_SLEEP_T);   //Other option: //wifi_set_sleep_type();

  //GPIO_DIS_OUTPUT(GPIO_ID_PIN(WAKE_UP_PIN));  //Not needed, commented
  //PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDI_U,	FUNC_GPIO12);     //Not needed, commented
  for (int i = 0; i < SIZEOF_ZONE; i++)
    gpio_pin_wakeup_enable(GPIO_ID_PIN(ZONE_PIN[i]), GPIO_PIN_INTR_LOLEVEL); //HILEVEL    other option: //wifi_enable_gpio_wakeup(GPIO_ID_PIN(WAKE_UP_PIN), GPIO_PIN_INTR_LOLEVEL);
  // only LOLEVEL or HILEVEL interrupts work, no edge, that's an SDK or CPU limitation

  wifi_fpm_open();  //Puse esto arriba de wifi_fpm_set_wakeup_cb... VER MANUAL 3.7.4. wifi_fpm_set_wakeup_cb 
  wifi_fpm_set_wakeup_cb(wakeupCallback); // Set wakeup callback (optional)
  sint8 res = wifi_fpm_do_sleep(0xFFFFFFF);  // only 0xFFFFFFF, any other value and it won't disconnect the RTC timer
  //The system will not enter sleep mode instantly when force-sleep APIs are called, but only after executing an idle task.
  delay(10);  // it goes to sleep during this delay() and waits for an interrupt
  DEBUG_PRINTLN(F("Woke up!"));  // the interrupt callback hits before this is executed
  DEBUG_PRINTLN(F("RESULTADO DEL SLEEP: ") + String(res));
}*/

void Sleep_Timed() {
  DEBUG_PRINTLN(F("\nTimed Light Sleep, wake by time"));

  // for timer-based light sleep to work, the os timers need to be disconnected:
  extern os_timer_t *timer_list;
  timer_list = nullptr;

  gpio_pin_wakeup_disable();  //Only timed sleep, not pin interrupt

  wifi_set_sleep_type(LIGHT_SLEEP_T);

  wifi_station_disconnect();
  wifi_set_opmode(NULL_MODE);							 	 //	set	WiFi	mode	to	null	mode.
  wifi_fpm_set_sleep_type(LIGHT_SLEEP_T);		 //This API can only be called before wifi_fpm_open 	light	sleep 
  wifi_fpm_open();														 //	Enable force sleep function

  wifi_fpm_set_wakeup_cb(WakeUpCallBackFunction);			//This API can only be called when force sleep function is enabled, after calling wifi_fpm_open
  //fpm_wakeup_cb_func1 will be called after system wakes up only if the force sleep time out (wifi_fpm_do_sleep and the parameter is not 0xFFFFFFF)

  sint8 res = wifi_fpm_do_sleep(SLEEP_TIME_MS * 1000);  //microseconds
  delay(SLEEP_TIME_MS + 1);  // it goes to sleep //The system will not enter sleep mode instantly when force-sleep APIs are called, but only after executing an idle task.
  DEBUG_PRINTLN(F("Woke up!"));  // the interrupt callback hits before this is executed
  DEBUG_PRINTLN(F("RESULTADO DEL SLEEP: ") + String(res));
  //0, setting successful;
  //-1, failed to sleep, sleep status error;
  //-2, failed to sleep, force sleep function is not enabled
}

String Sim800_Wait_Cmd(uint16_t timeout, String cmd){
  unsigned long t = millis();
  while(millis()-t<timeout)         // loop through until their is a timeout or a response from the device
  {
    yield();
    if(sim800.available()) //check if the device is sending a message
    {
      String tempData = sim800.readString(); // reads the response
      DEBUG_PRINTLN(tempData);
      tempData.trim();
      if(tempData.indexOf(cmd) == 0){
        return tempData;
      }
      else{
        Sim800_Buffer_Add(tempData);
      }
    }
  }
  return "";
}

String Sim800_AnswerString(uint16_t timeout){
  unsigned long t = millis();
  while(millis()-t<timeout)         // loop through until their is a timeout or a response from the device
  {
    yield();
    if(sim800.available()) //check if the device is sending a message
    {
      sim800.flush();
      String tempData = sim800.readString(); // reads the response
      DEBUG_PRINTLN(tempData);
      return tempData;
      //rta=tempData;
    }
  }
  return "";
}

void readVoltage() { // read internal VCC
  float volts = ESP.getVcc();
  DEBUG_PRINTLN("The internal VCC reads " + String(volts / 1000) + " volts");
}

void printMillis() {
  DEBUG_PRINT(F("millis() = "));  // show that millis() isn't correct across most Sleep modes
  DEBUG_PRINTLN(millis());
  DEBUG_FLUSH;  // needs a Serial.flush() else it may not print the whole message before sleeping
}

void Sim800_Buffer_Add(String item){
  if (Sim800_Buffer_Count >= 49)
    return;
  Sim800_Buffer_Array[Sim800_Buffer_Count] = item;
  ++Sim800_Buffer_Count;
}

String Sim800_Buffer_Read(){
  if (Sim800_Buffer_Count == 0)
    return "";
  String r = Sim800_Buffer_Array[0];
  --Sim800_Buffer_Count;
  for(int i = 0; i < Sim800_Buffer_Count; i++){
    Sim800_Buffer_Array[i] = Sim800_Buffer_Array[i+1];
  }
  return r;
}

/*String Sim800_Find_Cmd(String cmd){
  String found = "";
  cmd.trim();
  for(int i = 0; i < Sim800_Buffer_Count; i++){
    if(Sim800_Buffer_Array[i].indexOf(cmd) >= 0){
      found = Sim800_Buffer_Array[i];
      for(int i = 0; i < Sim800_Buffer_Count; i++){

      }
    }
  unsigned int len, index;
  //////////////////////////////////////////////////
  //Remove sent "AT Command" from the response string.
  //index = buff.indexOf("\r");
  //buff.remove(0, index+2);
  buff.trim();
  //////////////////////////////////////////////////
  
  //////////////////////////////////////////////////
  if(buff != "OK"){
    index = buff.indexOf(":");
    String cmd = buff.substring(0, index);
    cmd.trim();
    
    buff.remove(0, index+2);
    
    if(cmd == "+CMTI"){    
  }
}*/


/*
void updateRTCcrc() {  // updates the reset count CRC
  nv->rtcData.crc32 = crc32((uint8_t*) &nv->rtcData.rstCount, sizeof(nv->rtcData.rstCount));
}

void initWiFi() {
  digitalWrite(LED, LOW);  // give a visual indication that we're alive but busy with WiFi
  uint32_t wifiBegin = millis();  // how long does it take to connect
  if ((crc32((uint8_t*) &nv->rtcData.rstCount + 1, sizeof(nv->wss)) && !WiFi.shutdownValidCRC(nv->wss))) {
    // if good copy of wss, overwrite invalid (primary) copy
    memcpy((uint32_t*) &nv->wss, (uint32_t*) &nv->rtcData.rstCount + 1, sizeof(nv->wss));
  }
  if (WiFi.shutdownValidCRC(nv->wss)) {  // if we have a valid WiFi saved state
    memcpy((uint32_t*) &nv->rtcData.rstCount + 1, (uint32_t*) &nv->wss, sizeof(nv->wss)); // save a copy of it
    Serial.println(F("resuming WiFi"));
  }
  if (!(WiFi.resumeFromShutdown(nv->wss))) {  // couldn't resume, or no valid saved WiFi state yet
    // Explicitly set the ESP8266 as a WiFi-client (STAtion mode), otherwise by default it
    //  would try to act as both a client and an access-point and could cause network issues
    //  with other WiFi devices on your network.
    WiFi.persistent(false);  // don't store the connection each time to save wear on the flash
    WiFi.mode(WIFI_STA);
    WiFi.setOutputPower(10);  // reduce RF output power, increase if it won't connect
    WiFi.config(staticIP, gateway, subnet);  // if using static IP, enter parameters at the top
    WiFi.begin(AP_SSID, AP_PASS);
    Serial.print(F("connecting to WiFi "));
    Serial.println(AP_SSID);
    DEBUG_PRINT(F("my MAC: "));
    DEBUG_PRINTLN(WiFi.macAddress());
  }
  wifiTimeout.reset(timeout);
  while (((!WiFi.localIP()) || (WiFi.status() != WL_CONNECTED)) && (!wifiTimeout)) {
    yield();
  }
  if ((WiFi.status() == WL_CONNECTED) && WiFi.localIP()) {
    DEBUG_PRINTLN(F("WiFi connected"));
    Serial.print(F("WiFi connect time = "));
    float reConn = (millis() - wifiBegin);
    Serial.printf("%1.2f seconds\n", reConn / 1000);
    DEBUG_PRINT(F("WiFi Gateway IP: "));
    DEBUG_PRINTLN(WiFi.gatewayIP());
    DEBUG_PRINT(F("my IP address: "));
    DEBUG_PRINTLN(WiFi.localIP());
  } else {
    Serial.println(F("WiFi timed out and didn't connect"));
  }
  WiFi.setAutoReconnect(true);
}
*/

void Espera(unsigned int TiempoMillis)
{
  unsigned long startTiempoMillis = millis();
  while (millis() - startTiempoMillis < TiempoMillis) { //tengo q restar en ese orden para que funcione siempre bien
    //espero
    yield();  //esto es para evitar que se RESETEE al quedar atrapado dentro del loop.
  }   
  //&& digitalRead(12)==HIGH Además de esperar el tiempo indicado, esta función monitorea un pin de entrada del Arduino que haya sido cableado para detectar que ha sucedido algún evento externo. Si esa señal va a nivel BAJO (LOW), se interrumpe el retardo.
}

