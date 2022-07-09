/*
If VS CODE doesn't compile, check the pre-declaration of methods.

platformio.ini:
lib_deps = 
	ottowinter/ESPAsyncTCP-esphome@^1.2.3
	ottowinter/ESPAsyncWebServer-esphome@^2.1.0
	jwrw/ESP_EEPROM@^2.1.1

ESP_EEPROM.h lib dependency:
  #include <string.h>

Text to speech:
https://github.com/earlephilhower/ESP8266SAM
https://github.com/earlephilhower/ESP8266Audio


Consuming now 7ma on 12v
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

//Text to speech:
#include <ESP8266SAM.h>
//#include <AudioOutputI2S.h>
#include <AudioOutputI2SNoDAC.h>
//AudioOutputI2S *out = NULL;
AudioOutputI2SNoDAC *out = NULL;

const char* AP_SSID = "ESP8266_Wifi";  // AP SSID here
const char* AP_PASS = "123456789";  // AP password here
//const char* PARAM_INPUT_1 = "input1";
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
#define SIM800_TIMEOUT 500  //min time tbwn AT commands: 120ms (for AT command answer timeout)
#define SIM800_TIMEOUT_SMS 10000  //timeout sengins SMS messages
#define SIM800_REPS 3 //number of AT command repetitions when ERROR or timout instead of OK
#define SIM800_MAXCALLMILLIS 300000 //max call duration: 5minutes
#define SIM800baudrate 9600   //too fast generates issues when receibing SMSs, buffer is overloaded and the SMS AT arrives incomplete
#define DEBUGbaudrate 115200
#define SLEEP_TIME_MS 100 // 100 mili seconds of light sleep periods between input readings

#define WIFI_DURATION_MS 60000 // 300000 //Wifi setup duration: 5minutes
#define ESP_ZONES_READ_MS 500    //frequency reading Zones
#define ESP_BLINKINGON_MS 500    //blinking led on time
#define ESP_BLINKINGOFF_MS 1000  //blinking led off time
#define ESP_BLINKINGONFIRED_MS 200    //blinking led on time
#define ESP_BLINKINGOFFFIRED_MS 500  //blinking led off time

#define ESP_VOLTAGE_MIN 11      //minimum voltage baterry to trigger the alarm
#define ESP_VOLTAGE_RESET 12.5  //voltage baterry to reset the battery alarm
#define ESP_VOLTAGE_MS 30000  //frequency to read the battery voltage
unsigned long ESP_VOLTAGE_READMILLIS = 0; //last voltage read millis

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

uint32_t WIFI_TOMEOUT_MILLIS = 0;
//const String PHONE = "+543414681709";
//String smsStatus,senderNumber,receivedDate,msg;
struct SmsMessage {
  String Phone;
  String Message;
};
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
bool ESP_WIFI = true;      //Wifi enabled during start up (120 secs)
bool ESP_DISARMED = false;  //Alarm disarmed
bool ESP_ARMED = false;     //Alarm armed
bool ESP_FIRSTDELAY = false; //Alarm first delay (if zone triggers again, it will be fired)
uint32_t ESP_FIRSTDELAY_MILLIS; //Alarm first delay millis start
bool ESP_FIREDELAY = false; //Alarm fired delay (delayed zone activated)
uint32_t ESP_FIREDELAY_MILLIS; //Alarm fired delay millis start
bool ESP_FIRED = false;     //Alarm fired (siren activated)
uint32_t ESP_FIRED_MILLIS;  //Alarm fired millis
//bool ESP_FIREDTOUT = false; //Alarm fired time out (siren silenced after max siren time)
bool ESP_READYTOSLEEP = false;
bool ESP_LOWBATTERY = false;
//uint8_t ESPStatus = ESP_WIFI;
//bool ReadyToSleep = false;

//SIM800 status:
bool SIM_RINGING = false;       //SIM800 RI pin is active
bool SIM_ONCALL = false;        //SIM800 is on call
bool SIM_SLEEPING = false;      //SIM800 sleeping
uint32_t SIM_ONCALLMILLIS;
bool SIM_WAITINGDTMF_ADA = false;
//bool SIM_INACTIVE = false;       //SIM800 not responding AT commands
//bool SIM_ERROR = false;       //SIM800 not responding AT commands
//bool SIM_CALLING = false;     //SIM800 in a call
//uint8_t SIMStatus = SIM_INACTIVE;

static String Sim800_Buffer_Array[50];
static int Sim800_Buffer_Count = 0;
String DTMFs = "";
//bool waitingCPAS = false;
bool sleepTime = false;
bool ReadyToArm = false;

//ESP8266 NodeMCU Wemos D1 Mini pinout:
// D0/GPIO16 (no interrupts to wake up), D1/GPIO5, D2/GPIO4, D3/GPIO0, D4/GPIO2 (built-in LED), D6/GPIO12, D7/GPIO13

#define DEBUG  // prints WiFi connection info to serial, uncomment if you want WiFi messages
#ifdef DEBUG
  #define DEBUG_PRINTLN(x) Serial.println(x)
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_FLUSH //Serial.flush()
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
  const uint8_t ZONE_PIN[SIZEOF_ZONE] = {D5, D5, D5, D6, D7}; //{D1, D2, D5, D6, D7};
#endif
bool ZONE_DISABLED[SIZEOF_ZONE]; //if the zone has auto disable function enabled, this array will mask them.
uint8_t ZONE_COUNT[SIZEOF_ZONE]; //to count the number of activations since the alarm was last armed.
int ZONE_STATE[SIZEOF_ZONE]; //the state of the zones (las/previous reading).
bool ZONE_TRIGGERED[SIZEOF_ZONE]; //the zone is triggered.

#define SIM800_RING_RESET_PIN D3    //input and output pin, used to reset the sim800
const uint8_t SIREN_PIN[SIZEOF_SIREN] = {D0, D4, D8};
const uint8_t SIREN_DEF[SIZEOF_SIREN] = {HIGH, HIGH, LOW}; //D0 D4 normal HIGH, D8 normal LOW
bool SIREN_FORCED[SIZEOF_SIREN]; //if forced, the output status will be overriden to the oposite of SIREN_DEF despite the alarm status.
bool SIREN_DISABLED[SIZEOF_SIREN];
bool SIREN_TIMEOUT[SIZEOF_SIREN];

//ADC_MODE(ADC_VCC);  //don't connect anything to the analog input pin(s)! allows you to monitor the internal VCC level; it varies with WiFi load
//int ZoneStatus[SIZEOF_ZONE];

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
//String Sim800_Wait_Cmd(uint16_t timeout, String cmd);
//String Sim800_AnswerString(uint16_t timeout);
byte Sim800_checkResponse(unsigned long timeout);
//bool Sim800_setFullMode();
void parseData(String buff);
SmsMessage extractSms(String buff);
void doAction(String msg, String phone);
void DelayYield(unsigned int TiempoMillis);
void Sleep_Forced();
float readVoltage();
//void printMillis();
void WakeUpCallBackFunction();
void ConfigDefault(propAlarm &pa);
void ConfigWifi();
void notFound(AsyncWebServerRequest *request);
String processor(const String& var);
void ConfigStringCopy(propAlarm &pa, String &str, bool toString);
void ConfigToEEPROM(propAlarm &pa);
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
bool SmsReponse(String text, String phone, bool forced);
void CallReponse(String text, String phone, bool forced);
void AlarmFiredSmsAdvise();
void BatteryLowSmsAdvise();
void AlarmFiredCallAdvise();
void AlarmDisarm();
void AlarmFire();
void AlarmArm();
void AlarmReArm();
void Sim800_ManageCommunication();
void Sim800_ManageCommunicationOnCall(unsigned long timeout);
String AlarmStatusText();
void AlarmLoop();
bool Sim800_WriteCommand(String atcmd);
String Sim800_ReadCommand(String atcmd);
void Sim800_HardReset();
void Sim800_RemoveEcho(String &buff);
String Sim800_NextLine(String &buff);
bool Sim800_UnsolicitedResultCode(String line);
void BlinkLED();
void Read_Ring_State();
bool SirenOnPeriod(int i, uint32_t ms);

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
    pinMode(GPIO_ID_PIN(ZONE_PIN[i]), INPUT_PULLUP);  //INPUT_PULLUP *******************
    ZONE_DISABLED[i] = false;
    ZONE_COUNT[i] = 0;
    ZONE_TRIGGERED[i] = false;
  }

  for (int i = 0; i < SIZEOF_SIREN; i++){
    pinMode(GPIO_ID_PIN(SIREN_PIN[i]), OUTPUT);
    digitalWrite(GPIO_ID_PIN(SIREN_PIN[i]), SIREN_DEF[i]);
    SIREN_FORCED[i] = false;
  }
  //GPIO_DIS_OUTPUT(GPIO_ID_PIN(SIM800_RING_RESET_PIN));  because it is input and output
  pinMode(SIM800_RING_RESET_PIN, INPUT_PULLUP); //  INPUT_PULLUP *******************  to read SIM800 RING, later will be set temporarily as output to reset SIM800

  #ifdef DEBUG
    Serial.begin(DEBUGbaudrate);
    //AGREGADO:
    /*while(!Serial)  ***************************************************
    {
      yield();
    }  */
    DEBUG_PRINTLN();
    DEBUG_PRINT(F("\nReset reason = "));
    String resetCause = ESP.getResetReason();
    DEBUG_PRINTLN(resetCause);
  #endif

  //Initialize config if EEPROM is empty
  DEBUG_PRINT(F("\nalarmConfig Size= "));
  DEBUG_PRINTLN(sizeof(alarmConfig));

  /*smsStatus = "";
  senderNumber="";
  receivedDate="";
  msg="";*/
  Sim800_Connect();
  DEBUG_PRINTLN(F("**** Conectado ****"));
  //startT = millis();

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

  ConfigWifi(); //Wifi initializes after EEPROM reading to have loaded the Alarm Config   ****************************************
  DEBUG_PRINTLN(F("Wifi Conectado"));

  //out = new AudioOutputI2S();
  out = new AudioOutputI2SNoDAC();
  out->begin();
  DEBUG_PRINTLN(F("Text to speech iniciado"));

  AlarmDisarm();  //initialize states
}

void loop() {
  static bool espFiredPrev = false;
  //static bool espVoltageOk = true;

  //if (digitalRead(GPIO_ID_PIN(SIM800_RING_RESET_PIN)) == LOW)
  //  DEBUG_PRINTLN(F("RINGGGGGG"));

  AlarmLoop();

  //wake sim800 and read messages
  if (SIM_RINGING && SIM_SLEEPING){
    Sim800_disableSleep();
  }
  Sim800_ManageCommunication();

  //Call and send SMSs if fired or battery low
  if (ESP_FIRED && !espFiredPrev){  //if alarm was just fired
    espFiredPrev = true;
    AlarmFiredSmsAdvise();
    AlarmFiredCallAdvise();
  }
  if (!ESP_FIRED)
    espFiredPrev = false;

  //Check battery voltage
  if (!ESP_LOWBATTERY && readVoltage() < ESP_VOLTAGE_MIN){
    DEBUG_PRINTLN(F("Low battery detected, calling and texting..."));
    ESP_LOWBATTERY = true;
    BatteryLowSmsAdvise();
    AlarmFiredCallAdvise();
  }
  else if (ESP_LOWBATTERY && readVoltage() > ESP_VOLTAGE_RESET){
    ESP_LOWBATTERY = false;
  }

  //BlinkLED();

  //If too much time in a call or ringing, hang up
  if ((SIM_RINGING || SIM_ONCALL) && (RTCmillis() - SIM_ONCALLMILLIS) > SIM800_MAXCALLMILLIS){
    Sim800_WriteCommand(F("ATH"));//hang up
    SIM_ONCALL = false;
    SIM_RINGING = false;
    DTMFs = "";
  }

  //Auto Alarm Arm
  if (ESP_FIRED && (RTCmillis() - ESP_FIRED_MILLIS > alarmConfig.AutoArmDelaySecs)){
    if (ReadyToArm)
      AlarmReArm();
  }

  //Wifi configuration at startup preventing to sleep
  if (ESP_WIFI && ((RTCmillis() - WIFI_TOMEOUT_MILLIS) > (uint32_t)WIFI_DURATION_MS)){
    DEBUG_PRINTLN(F("Wifi Timeout"));
    ESP_WIFI = false;
  }

  //Going to sleep
  if (!ESP_WIFI && !SIM_ONCALL && !SIM_RINGING){ // && !ESP_FIRSTDELAY && !ESP_FIREDELAY && !ESP_FIRED){ //***make it sleep when fired*****************************
    if (!SIM_SLEEPING){
      Sim800_enterSleepMode();
      DelayYield(500);  //500
    }
    if (!ESP_READYTOSLEEP)
      Sleep_Prepare();
    Sleep_Forced();
  }

  /*//Configuring the ESP to be able to LIGHT SLEEP:
  if (!ReadyToSleep && RTCmillis() > 60000)
    Sleep_Prepare();

  //AGREGADO:
  if ((WiFi.status()!= WL_CONNECTED) && (waitingCPAS == false) && (millis() - startT > 120000)){ //If ringing or in call, do not sleep
    waitingCPAS = true;
    DEBUG_PRINTLN(F("Enviando AT+CPAS"));
    sim800.println(F("AT+CPAS")); // activity of phone: 0 Ready, 2 Unknown, 3 Ringing, 4 Call in progress
    sim800.flush();
    //DelayYield(5000);
  }
  if (waitingCPAS && (millis() - startT < 20000))
    waitingCPAS = false;          //reseteo porque ya hubo respuesta
  if (sleepTime) { //if () (millis() - startT > 25000) {
    sleepTime = false;
    server.end(); //Ends the AP Web Server, as configuration is only at startup.
    delay(10);
    DelayYield(500);
    Sim800_enterSleepMode();
    //sim800.end();
    DelayYield(500);
    //disable all timers
    //DelayYield(5000);
    Sleep_Forced();
    Sim800_disableSleep();
    //revisar si estoy en una llamada o si llegó un nuevo mensaje
    startT = millis();
    waitingCPAS = false;
  }*/
}

void AlarmLoop()
{
  static uint32_t lastReadMillis = 0;

  Read_Ring_State();

  if (RTCmillis() - lastReadMillis > (uint32_t)ESP_ZONES_READ_MS){
    lastReadMillis = RTCmillis();

    ReadyToArm = Read_Zones_State();

    SIREN_FORCED[1] = !SIREN_FORCED[1]; //*****************************************************

    //write outputs:
    for (int i=0; i < SIZEOF_SIREN; i++){
      if (SIREN_FORCED[i] || (alarmConfig.Siren[i].Enabled && !SIREN_DISABLED[i] && ESP_FIRED && !SIREN_TIMEOUT[i] && SirenOnPeriod(i,lastReadMillis))){
        digitalWrite(SIREN_PIN[i], SIREN_DEF[i]==HIGH? LOW : HIGH);
        if ((lastReadMillis - ESP_FIRED_MILLIS) > alarmConfig.Siren[i].MaxDurationSecs)
          SIREN_TIMEOUT[i] = true;
      }
      else{
        //pinMode(GPIO_ID_PIN(SIREN_PIN[i]), INPUT);  //************************************************************
        digitalWrite(SIREN_PIN[i], SIREN_DEF[i]);
      }
    }
  }
}

bool SirenOnPeriod(int i, uint32_t ms) //Determines if the siren has to be on or off according to pulse / pause
{
  uint32_t r = (ms - ESP_FIRED_MILLIS) % (alarmConfig.Siren[i].PulseSecs + alarmConfig.Siren[i].PauseSecs);
  if (r <= alarmConfig.Siren[i].PulseSecs){
    return true;
  }
  else{
    return false;
  }
}

void BlinkLED(){
  static uint32_t lastChangeMillis; //last led change millis
  static bool ledState = false;
  uint32_t ms = RTCmillis();

  if (ledState)
  {
    if (ms - lastChangeMillis > (ESP_FIRED? ESP_BLINKINGONFIRED_MS : ESP_BLINKINGON_MS)){
      ledState = false;
      lastChangeMillis = ms;
      DEBUG_PRINTLN(F("LED OFF"));
    }
  }
  else
  {
    if (ms - lastChangeMillis > (ESP_FIRED? ESP_BLINKINGOFFFIRED_MS : ESP_BLINKINGOFF_MS)){
      ledState = true;
      lastChangeMillis = ms;      
      DEBUG_PRINTLN(F("LED ON"));
    }
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

void ConfigWifi(){
  DEBUG_PRINT(F("Setting AP (Access Point)…"));
  //set-up the custom IP address ***** ESTO QUIZAS NO HAGA FALTA
  //WiFi.mode(WIFI_AP_STA);
  //WiFi.softAPConfig(webserver_IP, webserver_IP, IPAddress(255, 255, 255, 0));   // subnet FF FF FF 00  
  WiFi.softAP(AP_SSID, AP_PASS);  //Remove the password parameter, if you want the AP (Access Point) to be open
  DelayYield(100);

  //IPAddress IP = ;
  DEBUG_PRINT(F("Soft AP IP address: "));
  DEBUG_PRINTLN(WiFi.softAPIP());
  
  DEBUG_PRINT(F("Local IP address: "));   // Print ESP8266 Local IP Address
  DEBUG_PRINTLN(WiFi.localIP());

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){  //Route for root / web page
    WIFI_TOMEOUT_MILLIS = RTCmillis();
    request->send_P(200, "text/html", index_html, processor);
  });

  server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request){  //Config page /config
    if (HTMLAdminPass == String(alarmConfig.AdminPass)){
      WIFI_TOMEOUT_MILLIS = RTCmillis();
      request->send_P(200, "text/html", config_html, processor);
    }
  });

  server.on("/pass", HTTP_GET, [](AsyncWebServerRequest *request){ //Send a GET request to <ESP_IP>/pass?HTMLAdminPass=<inputMessage>
    if (request->hasParam("htmladminpass")) {
      WIFI_TOMEOUT_MILLIS = RTCmillis();
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
      WIFI_TOMEOUT_MILLIS = RTCmillis();
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
String processor(const String& var){
  if(var == "htmladminpass"){
    return HTMLAdminPass;
  }
  else if(var == "HTMLConfig"){
    return HTMLConfig;
  }
  return String();
}

void Sim800_ManageCommunicationOnCall(unsigned long timeout){
  unsigned long t = millis();
  //SIM_ONCALL = true;
  while(millis()-t<timeout)
  {
    AlarmLoop();      //Check inputs status from time to time
    //yield();
    Sim800_ManageCommunication();
    if (!SIM_ONCALL){             //call finished
      break;
    }
  }
}
void Sim800_ManageCommunication(){
  String readstr = "";
  if (!SIM_SLEEPING){
    while(sim800.available()){
      parseData(sim800.readString());
    }
    readstr = Sim800_Buffer_Read();
    while(readstr != ""){
      parseData(readstr);
      readstr = Sim800_Buffer_Read();
    }
  }
  #ifdef DEBUG
    while(Serial.available())  {
      readstr = Serial.readString();
      DEBUG_PRINTLN("Enviando: -" + readstr + "-");
      if (readstr == "reset sim800\n"){
        Sim800_HardReset();
      }
      else if (readstr == "battery\n"){
        DEBUG_PRINTLN("Battery voltage: " + String(readVoltage()));
      }
      else {
        sim800.println(readstr);
      }
    }
  #endif
}

void BatteryLowSmsAdvise(){
  if (alarmConfig.Caller.SMSResponse || alarmConfig.Caller.SMSOnAlarm){
    Sim800_disableSleep();
    String msg="BATTERY LOW!";
    msg += AlarmStatusText();
    for (int i=0; i < SIZEOF_SMSPHONE; i++){
      SmsReponse(msg, String(alarmConfig.Caller.SMSPhone[i].Number), true);
    }
  }
}

void AlarmFiredSmsAdvise(){
  Sim800_disableSleep();
  String msg="ALARM FIRED!";
  msg += AlarmStatusText();
  for (int i=0; i < SIZEOF_SMSPHONE; i++){
    SmsReponse(msg, String(alarmConfig.Caller.SMSPhone[i].Number), false);
  }
}

String AlarmStatusText(){
  String msg="";
  msg = "Armed " + String(ESP_ARMED?"1":"0");
  msg += ", Fired " + String(ESP_FIRED?"1":"0");
  //msg += ", Ready " + String(Read_Zones_State()?"1":"0");
  msg += ", Ready " + String(ReadyToArm?"1":"0");
  msg += ", Bat " + String(readVoltage());
  for (int i = 0; i < SIZEOF_ZONE; i++){
    if (ZONE_COUNT[i]>0 || ZONE_TRIGGERED[i] || ZONE_DISABLED[i]){
      msg += ", Zone" + String(i);
      if (ZONE_DISABLED[i])
        msg += " DIS";
      if (ZONE_TRIGGERED[i])
        msg += " TRIG";
      if (ZONE_COUNT[i]>0)
        msg += " count:" + String(ZONE_COUNT[i]);
    }
  }
  for (int i = 0; i < SIZEOF_SIREN; i++){
    if (SIREN_DISABLED[i] || SIREN_FORCED[i]){
      msg += ", Siren" + String(i);
      if (SIREN_DISABLED[i])
        msg += " DIS";
      if (SIREN_FORCED[i])
        msg += " FORCED";
    }
  }
  return msg;
}

void AlarmFiredCallAdvise(){
  Sim800_disableSleep();
  String msg="ALARM FIRED! ALARM FIRED! ALARM FIRED!";
  msg += AlarmStatusText();
  for (int i=0; i < SIZEOF_CALLPHONE; i++){
    CallReponse(msg, String(alarmConfig.Caller.CALLPhone[i].Number), false);
    //Sim800_ManageCommunication();   //Before callen next number, check for new sms
  }
}

bool Sim800_WriteCommand(String atcmd)  // atcmd="AT+<x>=<…>" Sets the user-definable parameter values
{
  unsigned long t;
  //int index;
  String ans, line;
  bool failed;
  for (int i = 0; i < SIM800_REPS; i++){

    DEBUG_PRINTLN(F("Sending AT Write Command: ") + atcmd);
    sim800.println(atcmd);
    sim800.flush();
    
    t = millis();
    failed = false;
    while(millis() - t < SIM800_TIMEOUT && !failed)         // loop through until there is a timeout or a response from the device
    {
      if(sim800.available()) //check if the device is sending a message
      {
        ans = sim800.readString(); // reads the response
        DEBUG_PRINTLN(ans);
        while (ans.length()>0){
          Sim800_RemoveEcho(ans);
          line = Sim800_NextLine(ans);
          if(line == "OK"){           //AT command OK, exit
            if (ans.length()>0)
              Sim800_Buffer_Add(ans); //in case any non requested command is received
            DEBUG_PRINTLN(F("OK DETECTED"));
            return true;
          } else if(line == "ERROR"){ //AT command error, retry
            if (ans.length()>0)
              Sim800_Buffer_Add(ans); //in case any non requested command is received
            failed = true;
            DEBUG_PRINTLN(F("ERROR DETECTED"));
            break;
          } else {
            Sim800_Buffer_Add(line); //in case any non requested command is received
          }
        }
      }
      AlarmLoop();      //Check inputs status from time to time
      //yield();
    }
  }
  return false; //not OK detected in any try
}

String Sim800_ReadCommand(String atcmd) // atcmd="AT+<x>?" or "AT+<x>" or "AT<x>?" or "AT<x><n>" or "AT&<x><n>" and response "+<cmd>: <...>" or "<...>" Returns the currently set value of the parameter or parameters.
{
  String cmd;
  unsigned long t;
  int index, index2;
  String ans, line, rta;
  bool failed;
  for (int i = 0; i < SIM800_REPS; i++){

    DEBUG_PRINTLN(F("Sending AT Read Command: ") + atcmd);
    sim800.println(atcmd);
    sim800.flush();
    
    //Extract command:
    cmd = "";
    if (atcmd.substring(0,3) == "AT+"){
      index = atcmd.indexOf("?");
      index2 = atcmd.indexOf("=");
      if (index > -1){
        if (index2 > -1)
          index = min(index, index2);
      }else if (index2 > -1)
        index = index2;
      if (index > -1)
        cmd = atcmd.substring(2, index);  //including the +
      else
        cmd = atcmd.substring(2);
      DEBUG_PRINTLN(F("AT Read Command Answer Header: ") + cmd);
    }

    t = millis();
    failed = false;
    while(millis() - t < SIM800_TIMEOUT && !failed)         // loop through until there is a timeout or a response from the device
    {
      if(sim800.available()) //check if the device is sending a message
      {
        ans = sim800.readString(); // reads the response
        DEBUG_PRINTLN(F("From Sim800: ") + ans);

        while (ans.length()>0)
        {
          Sim800_RemoveEcho(ans);
          line = Sim800_NextLine(ans);
          while(Sim800_UnsolicitedResultCode(line))
            line = Sim800_NextLine(ans);
          
          if (line.length() > 0)
          {
            rta = line;
            if(cmd.length() > 0 && line.substring(0, cmd.length()) == cmd)
            {
              index = line.indexOf(":");
              if(index > -1)
                rta = line.substring(index+1);
            }
            rta.trim();

            //next lines until an OK is found, are part of the answer:
            failed = true;
            while (ans.length()>0)
            {
              line = Sim800_NextLine(ans);
              if (line == "OK"){
                failed = false;
                break;
              }
              rta += "\n\r" + line;
            }

            if (ans.length() > 0)
              Sim800_Buffer_Add(ans); //in case any non requested command is received

            if (!failed){
              DEBUG_PRINTLN(F("AT Read Command Answer: ") + rta);
              return rta;
            }
          }
        }
      }
      AlarmLoop();      //Check inputs status from time to time
      //yield();
    }
  }
  return "";
}

bool Sim800_UnsolicitedResultCode(String line)  //If there is an Unsolicited Result Code in the line, takes care of it and returns true
{
  if(line == "RING"){
    SIM_RINGING = true;
    SIM_ONCALL = true;
    SIM_ONCALLMILLIS = RTCmillis();
    DTMFs="";
    SIM_WAITINGDTMF_ADA = false;
    DEBUG_PRINTLN("From Sim800: RINGING");
  }
  else if(line == "NO CARRIER"){
    SIM_RINGING = false;
    SIM_ONCALL = false;
    DTMFs="";
    SIM_WAITINGDTMF_ADA = false;
    DEBUG_PRINTLN("From Sim800: CALL ENDED");
  }
  else
  {
    if (line.substring(0,1) = "+")
    {
      int index = line.indexOf(":");
      if (index > -1)
      {
        String cmd = line.substring(0, index);
        String rta = line.substring(index+1);
        rta.trim();

        if(cmd == "+DTMF")
        {
          DEBUG_PRINTLN("From Sim800 DTMF detected: " + rta);
          Sim800_WriteCommand("AT+CLDTMF=10,\"" + rta + "\"");  //DTMF echo due to really bad reception
          if(DTMFs==String(alarmConfig.OpPass)){
            if (!SIM_WAITINGDTMF_ADA){
              SIM_WAITINGDTMF_ADA = true;
              DEBUG_PRINTLN(F("Awaiting Arm / Disarm command from DTMF"));
              ESP8266SAM *sam = new ESP8266SAM;
              String text;
              text = "Alarm is Armed " + String(ESP_ARMED?"1":"0");
              text += ", Fired " + String(ESP_FIRED?"1":"0");
              text += ", Battery " + String(readVoltage());
              sam->Say(out, text.c_str()); //"Alarm is Armed! ALARM IS ARMED! alarm is armed"
              delay(500);
              sam->Say(out, "Type 1 to Arm");
              delay(500);
              sam->Say(out, "Type 2 to DisArm");
              delete sam;
            }
            else{
              if (rta == "1"){  //ARM
                AlarmArm();
                ESP8266SAM *sam = new ESP8266SAM;
                sam->Say(out, "ALARM IS ARMED");
                delay(500);
                sam->Say(out, "ALARM IS ARMED");
                delete sam;
                //sim800.println(F("AT+CLDTMF=10,\"1,5,1\""));
                //sim800.flush();
                //delay(120);
              }
              else if (rta == "2"){             //DISARM
                AlarmDisarm();
                ESP8266SAM *sam = new ESP8266SAM;
                sam->Say(out, "ALARM IS DISARMED");
                delay(500);
                sam->Say(out, "ALARM IS DISARMED");
                delete sam;
              }
            }
          }
          else{
            DTMFs += rta; //accumulate DTMSs for password
            DEBUG_PRINTLN("DTMFs Inserted: " + DTMFs);
          }
        }
        else if(cmd == "+CMTI") //new SMS arrived
        {
          index = rta.indexOf(",");
          String temp = rta.substring(index + 1, rta.length()); //get newly arrived memory location and store it in temp
          temp = "AT+CMGR=" + temp; //+ "\r"; 
          
          DEBUG_PRINTLN("From Sim800: SMS arrived at mem pos " + temp);
          //sim800.println(temp);
          temp = Sim800_ReadCommand(temp); //get the message stored at memory location "temp"
            
          SmsMessage smsmsg = extractSms(temp);  //buff + "\n\r" + buff2);
          doAction(smsmsg.Message, smsmsg.Phone);

          SIM_RINGING = false; //**********************************************************************************
        }
        //else if(line == "OK"){
        //  DEBUG_PRINTLN("OK DETECTADO");
        //}
        else{
          return false;
        }
      }
    }
  }
  return true;
}

void Sim800_RemoveEcho(String &buff)  //Removes received "AT Command ECHO"
{
  int index;
  buff.trim();
  //text.toUpperCase();
  while(buff.substring(0,2) == "AT"){ //por si hay varios comandos encadenados que ese enviaron juntos
    index = buff.indexOf("\r");
    if(index>-1){
      buff.remove(0, index+2);
    } else {
      break;
    }
    buff.trim();
  }
  DEBUG_PRINTLN("From SIM800L wo echo: -" + buff + "-");
}

String Sim800_NextLine(String &buff)  //Extracts the next line form the Sim800 answer
{
  int index;
  String next = "";
  while (next == "" && buff.length() > 0){
    index = buff.indexOf("\r");
    if(index > -1 && index < int(buff.length()-1)){  //there are remaining lines
      next = buff.substring(0, index);
      buff.remove(0, index);
      buff.trim();
    } else {
      next = buff;
      buff = "";
    }
    next.trim();
  }
  DEBUG_PRINTLN("From SIM800L line: -" + next + "-");
  return next;
}

bool Sim800_Connect(){
  DEBUG_PRINTLN(F("Conectando SIM800"));
  sim800.begin(SIM800baudrate);//115200
  delay(120);
  while(Sim800_checkResponse(5000)!=TIMEOUT);   //5 secs without receibing anything, See SIM800 manual, wait for SIM800 startup
  //DEBUG_PRINTLN(F("Enviando AT"));
  Sim800_WriteCommand(F("AT"));
  /*sim800.println("AT");
  sim800.flush();
  Sim800_checkResponse(500);*/
  Sim800_WriteCommand(F("ATE0"));
  /*sim800.println("ATE0");             //No ECHO (AT commands will not be sent back as echo)
  sim800.flush();
  Sim800_checkResponse(500);*/
  //DEBUG_PRINTLN(F("Enviando AT+CFUN=1"));       //Set Full Mode
  Sim800_WriteCommand(F("AT+CFUN=1"));
  /*sim800.println(F("AT+CFUN=1"));
  sim800.flush();
  Sim800_checkResponse(500);*/
  //DEBUG_PRINTLN(F("Enviando AT+CMGF=1"));
  Sim800_WriteCommand(F("AT+CMGF=1"));
  /*sim800.println(F("AT+CMGF=1"));                  //SMS text mode
  sim800.flush();
  Sim800_checkResponse(500);*/
  //DEBUG_PRINTLN(F("Enviando ATS0=2"));
  Sim800_WriteCommand(F("ATS0=2"));
  /*sim800.println(F("ATS0=2"));                  //Atender al segundo Ring
  sim800.flush();
  Sim800_checkResponse(500);*/
  //DEBUG_PRINTLN(F("Enviando AT+DDET=1,0,0,0"));
  Sim800_WriteCommand(F("AT+DDET=1,0,0,0"));
  /*sim800.println(F("AT+DDET=1,0,0,0"));                  //Detección de códigos DTMF
  sim800.flush();
  Sim800_checkResponse(500);*/
  Sim800_WriteCommand(F("AT+CMGDA=\"DEL ALL\""));
  /*sim800.println(F("AT+CMGDA=\"DEL ALL\""));                   //Borrar todos los mensajes
  sim800.flush();
  Sim800_checkResponse(500);*/
  //DEBUG_PRINTLN(F("Enviando AT+IPR?"));
  String ans = Sim800_ReadCommand(F("AT+IPR?"));
  /*sim800.println(F("AT+IPR?"));                   //Auto Baud Rate Serial Port Configuration (0 is auto)
  sim800.flush();
  Sim800_checkResponse(500);*/
  String sim800brstr = String(SIM800baudrate);
  sim800brstr.trim();
  if(ans != sim800brstr){
    DEBUG_PRINTLN("Adjusting default Serial SIM800 speed on " + sim800brstr);
    Sim800_WriteCommand(F("AT+IPR=") + sim800brstr); //115200
    //sim800.println(F("AT+IPR=") + sim800brstr); //115200
    //sim800.flush();
    //delay(120);
    Sim800_WriteCommand(F("AT&W"));
    //sim800.println(F("AT&W"));
    //sim800.flush();
    //delay(120);
  }
  Sim800_WriteCommand(F("AT+CNETLIGHT=0"));   //turn off net light led
  return true;
}

bool Sim800_enterSleepMode(){
  //check if sim800 is not bussy:
  String ans = Sim800_ReadCommand(F("AT+CPAS"));  // activity of phone: 0 Ready, 2 Unknown, 3 Ringing, 4 Call in progress
  //sim800.println(F("AT+CPAS")); // activity of phone: 0 Ready, 2 Unknown, 3 Ringing, 4 Call in progress
  if(ans == "3" || ans == "4"){  //Ringing or in call => wait to sleep
    DEBUG_PRINTLN("SIM800L sleeping FAILED, it is bussy on call or ringing");
    if (ans=="3") SIM_RINGING = true;
    if (ans=="4") SIM_ONCALL = true;
    return false; 
  }
  SIM_RINGING = false;
  SIM_ONCALL = false;
  DTMFs="";
  SIM_WAITINGDTMF_ADA = false;
  //sim800.println(F("AT+CSCLK=2")); // enable automatic sleep
  //if(Sim800_checkResponse(5000) == OK){
  if (Sim800_WriteCommand(F("AT+CSCLK=2"))){
    Sim800_WriteCommand(F("AT+CSCLK=2"));
    DEBUG_PRINTLN(F("SIM800L sleeping OK"));
    SIM_SLEEPING = true;
    return true;
  } else {
    DEBUG_PRINTLN(F("SIM800L sleeping FAILED, hard reseting it"));
    Sim800_HardReset();
    return false;  
  }
}

bool Sim800_disableSleep(){
  if (!SIM_SLEEPING){
    DEBUG_PRINTLN(F("SIM800L already awake"));
    return true;
  }
  sim800.println(F("AT"));    // first we need to send something random for as long as 100ms
  //sim800.flush();
  //DelayYield(120);                // this is between waking charaters and next AT commands  //120
  Sim800_checkResponse(2000); // just incase something pops up, next AT command has to be sent before 5secs after first AT.
  //sim800.println(F("AT+CSCLK=0"));
  //if(Sim800_checkResponse(5000) == OK){
  if (Sim800_WriteCommand(F("AT+CSCLK=0"))){
    DEBUG_PRINTLN(F("SIM800L awake OK"));
    SIM_SLEEPING = false;
    return true;
  } else {
    DEBUG_PRINTLN(F("SIM800L awake FAILED, hard reseting it"));
    Sim800_HardReset();
    return false;  
  }
}

void Sim800_HardReset(){
  pinMode(SIM800_RING_RESET_PIN, OUTPUT);
  digitalWrite(SIM800_RING_RESET_PIN, LOW);
  delay(150);                                     //T reset pull down has to be > 105ms
  pinMode(SIM800_RING_RESET_PIN, INPUT_PULLUP);
  Sim800_Connect();
}

byte Sim800_checkResponse(unsigned long timeout){
  // This function handles the response from the radio and returns a status response
  uint8_t Status = 99; // the defualt stat and it means a timeout
  unsigned long t = millis();
  while(millis()-t<timeout)
  {
    //yield();
    //count++;
    if(sim800.available()) //check if the device is sending a message
    {
      String tempData = sim800.readString(); // reads the response
      DEBUG_PRINTLN(tempData);
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
          //DEBUG_PRINTLN("Status number: " + i);
          return Status;
        }
      }
      Sim800_Buffer_Add(tempData);  //Appeared somthing different than expected (sms or call), save for later
    }
    AlarmLoop();
  }
  return Status;
}

void parseData(String buff)
{
  String line;
  DEBUG_PRINTLN("From SIM800L: -" + buff + "-");
  Sim800_RemoveEcho(buff);
  while (buff.length()>0){
    line = Sim800_NextLine(buff);
    Sim800_UnsolicitedResultCode(line);
  }
}

void AlarmDisarm(){
  ESP_ARMED = false;
  ESP_FIRED = false;
  ESP_FIREDELAY = false;
  for(int i=0; i < SIZEOF_ZONE; i++){
    ZONE_COUNT[i] = 0;
    ZONE_TRIGGERED[i] = false;
  }
  for(int i=0; i < SIZEOF_SIREN; i++){
    SIREN_TIMEOUT[i] = false;
  }
}

void AlarmFire(){
  ESP_FIRED = true;
  ESP_FIRED_MILLIS = RTCmillis();
}

void AlarmArm(){
  ESP_ARMED = true;
}

void AlarmReArm(){
  ESP_ARMED = true;
  ESP_FIRED = false;
  ESP_FIREDELAY = false;
  for(int i=0; i < SIZEOF_SIREN; i++){
    SIREN_TIMEOUT[i] = false;
  }
}

SmsMessage extractSms(String buff)  //+CMGR: <stat>,<fo>,<ct>[,<pid>[,<mn>][,<da>][,<toda>],<length><CR><LF><cdata>]
{
  //AT+CMGR=46
  //+CMGR: "REC UNREAD","+543414681709","","22/06/26,19:36:11-12"
  //mensaje
  //
  //OK

  //unsigned int index;
  uint8_t i;
  SmsMessage smsmsg;
  
  i = buff.indexOf(",");
  String smsStatus = buff.substring(1, i-1); 
  buff.remove(0, i+2);
  
  smsmsg.Phone = buff.substring(0, 13);
  buff.remove(0,19);
  
  String receivedDate = buff.substring(0, 20);
  buff.remove(0,buff.indexOf("\r"));
  buff.trim();
  
  i = buff.indexOf("\n\r");
  if (i>-1)
    buff = buff.substring(0, i);
  else
    buff = buff.substring(0);
  buff.trim();
  buff.toLowerCase();
  smsmsg.Message = buff;

  return smsmsg;
}

void doAction(String msg, String phone){
  DEBUG_PRINTLN("SMS: -" + msg + "-"); //msg is in "lowercase"
  //Recognised sender or password attached on msg?
  bool autorized = false;
  for (int i=0; i < SIZEOF_DISARMPHONE; i++){
    if (phone == String(alarmConfig.Caller.SMSPhone[i].Number)){
      autorized = true;
      break;
    }
  }
  if (String(alarmConfig.OpPass) == msg.substring(0, SIZEOF_PASS)){
    autorized = true;
    msg.remove(0, SIZEOF_PASS);
  }
  if (!autorized) return;

  DEBUG_PRINTLN("Autorized! msg: -" + msg + "-");
  String text;
  uint8_t z;
  if(msg == "s"){
    #ifdef DEBUG
      SmsReponse(AlarmStatusText(), phone, false);
    #else
      SmsReponse(AlarmStatusText(), phone, true);
    #endif
  }
  else if(msg == "a"){
    if (ReadyToArm){
      AlarmArm();
      SmsReponse("Alarm Armed", phone, false);
    }
    else {
      text = "Alarm NOT Armed, Zone Triggered";
      text += AlarmStatusText();
      SmsReponse(text, phone, false);
    }
  }
  else if(msg == "d"){
    AlarmDisarm();
    SmsReponse("Alarm Disarmed", phone, false);
  }
  else if(msg.substring(0,1) == "z"){
    msg.remove(0, 1);
    text = msg.substring(0,1);
    if(text == "e" || text == "d"){
      msg.remove(0, 1);
      z = msg.toInt();
      if (z >= 0 && z < SIZEOF_ZONE){
        ZONE_DISABLED[z] = (text=="e")?false:true;
        SmsReponse("Zone " + String(z) + ((text=="e")?" Enabled":" Disabled"), phone, false);
      }
      else{
        SmsReponse("Invalid Zone " + String(z), phone, false);
      }
    }
  }
  else if(msg.substring(0,1) == "s"){
    msg.remove(0, 1);
    text = msg.substring(0,1);
    if(text == "e" || text == "d"){
      msg.remove(0, 1);
      z = msg.toInt();
      if (z >= 0 && z < SIZEOF_SIREN){
        SIREN_DISABLED[z] = (text=="e")?false:true;
        SmsReponse("Siren " + String(z) + ((text=="e")?" Enabled":" Disabled"), phone, false);
      }
      else{
        SmsReponse("Invalid Siren " + String(z), phone, false);
      }
    }
  }
  else if(msg.substring(0,1) == "o"){
    msg.remove(0, 1);
    text = msg.substring(0,1);
    if(text == "1" || text == "0"){
      msg.remove(0, 1);
      z = msg.toInt();
      if (z >= 0 && z < SIZEOF_SIREN){
        SIREN_FORCED[z] = (text=="0")?false:true;
        SmsReponse("Output " + String(z) + "=" + text, phone, false);
      }
      else{
        SmsReponse("Invalid Output " + z, phone, false);
      }
    }
  }
}

bool SmsReponse(String text, String phone, bool forced){
  unsigned long t;
  String ans, line;
  bool failed;
  
  DEBUG_PRINTLN(text);
  //if forced = true, the sms will be sent even if it is not configured
  if (alarmConfig.Caller.GSMEnabled &&
    (alarmConfig.Caller.SMSResponse || forced || (alarmConfig.Caller.SMSOnAlarm && ESP_FIRED) ) &&
    phone.length()>10)
  {
    DEBUG_PRINTLN("TO SIM800: AT+CMGS=\"" + phone + "\"\r" + text + (char)26);
    //sim800l.print("AT+CMGF=1\r");                   //Set the module to SMS mode
    //delay(100);
    //sim800l.print("AT+CMGS=\"+*********\"\r");
//    sim800l.print("AT+CMGS=\"" + phone + "\"\r");  //Your phone number don't forget to include your country code, example +212123456789"
    //delay(500);
//    sim800l.print(text);       //This is the text to send to the phone number, don't make it too long or you have to modify the SoftwareSerial buffer
    //delay(500);
    //sim800l.print((char)26);// (required according to the datasheet)
    //delay(500);
    //sim800l.println();
//    sim800l.println((char)26);// (required according to the datasheet)
    sim800.println("AT+CMGS=\"" + phone + "\"");
    //sim800.flush();
    delay(200);         //without the delay doesn't work
    sim800.print(text);  // + (char)26 //Your phone number don't forget to include your country code, example +212123456789"
    //sim800.print((char)26);
    sim800.write(26);
    sim800.println();
    sim800.flush();
    //Sim800_checkResponse(5000);
    //DelayYield(5000);    //because if other AT command is sent, will be ignored for a while.

    t = millis();
    failed = false;
    while(millis() - t < SIM800_TIMEOUT_SMS && !failed)         // loop through until there is a timeout or a response from the device
    {
      if(sim800.available()) //check if the device is sending a message
      {
        ans = sim800.readString(); // reads the response
        DEBUG_PRINTLN(ans);
        while (ans.length()>0){
          Sim800_RemoveEcho(ans);
          line = Sim800_NextLine(ans);
          if(line == "OK"){           //AT command OK, exit
            if (ans.length()>0)
              Sim800_Buffer_Add(ans); //in case any non requested command is received
            DEBUG_PRINTLN(F("OK DETECTED"));
            return true;
          } else if(line == "ERROR" || line.substring(0,10) == "+CMS ERROR"){ //AT command error, retry
            if (ans.length()>0)
              Sim800_Buffer_Add(ans); //in case any non requested command is received
            failed = true;
            DEBUG_PRINTLN(F("ERROR DETECTED"));
            break;
          } else {
            Sim800_Buffer_Add(line); //in case any non requested command is received
          }
        }
      }
      AlarmLoop();      //Check inputs status from time to time
      //yield();
    }
    return false;
  }
  return true;
}

void CallReponse(String text, String phone, bool forced){
  DEBUG_PRINTLN(text);
  if (alarmConfig.Caller.GSMEnabled &&
    ((alarmConfig.Caller.CALLOnAlarm && (ESP_FIRED || ESP_LOWBATTERY)) || forced) &&
    phone.length()>10)
  {
    Sim800_WriteCommand("ATD" + phone + ";");//make call  println evita tener q poner \r al final  //Your phone number don't forget to include your country code, example +212123456789"
    //DEBUG_PRINTLN("ATD" + phone + ";");
    //sim800.println("ATD" + phone + ";"); 
    //sim800.flush();
    SIM_ONCALL = true;
    Sim800_ManageCommunicationOnCall(20000); //in case the call is attended and some DTMF sent  //DelayYield(20000);
    Sim800_WriteCommand(F("ATH"));//hang up
    SIM_ONCALL = false;
    //sim800.println(F("ATH")); 
    //sim800.flush();
    //DEBUG_PRINTLN(F("Call ended"));
  }
}

void Sleep_Prepare(){
  server.end();
  WiFi.mode(WIFI_OFF);
  delay(1);                                   //Needs a small delay at the begining!
  //gpio_pin_wakeup_disable();                //If only timed sleep, not pin interrupt
  wifi_station_disconnect();                  //disconnect wifi
  wifi_set_opmode(NULL_MODE);							 	  //set WiFi	mode	to	null	mode.
  wifi_fpm_set_sleep_type(LIGHT_SLEEP_T);		  //This API can only be called before wifi_fpm_open 	light	sleep
  wifi_fpm_open();													  //Enable force sleep function
 /* for (int i = 0; i < SIZEOF_ZONE; i++){       //If TriggerNC = true, zones must be at ground, opening ground loop wakes the ESP and fires the alarm.
    if (alarmConfig.Zone[i].Enabled){
      wifi_enable_gpio_wakeup(GPIO_ID_PIN(ZONE_PIN[i]), alarmConfig.Zone[i].TriggerNC ? GPIO_PIN_INTR_HILEVEL : GPIO_PIN_INTR_LOLEVEL);
    }
  }
  wifi_enable_gpio_wakeup(GPIO_ID_PIN(SIM800_RING_RESET_PIN), GPIO_PIN_INTR_LOLEVEL);*/ //Sending this GPIOs to ground will wake the ESP.
  wifi_fpm_set_wakeup_cb(WakeUpCallBackFunction);	//This API can only be called when force sleep function is enabled, after calling wifi_fpm_open. Will be called after system wakes up only if the force sleep time out (wifi_fpm_do_sleep and the parameter is not 0xFFFFFFF)
  ESP_READYTOSLEEP = true;
}

void Sleep_Forced() {
  //Set the pins with an output status to input status, i.e., MTDO, U0TXD and GPIO0, before enabling Lightsleep to eliminate the leakage current, so that the power consumption becomes even lower.

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
  //sint8 res = wifi_fpm_do_sleep(SLEEP_TIME_MS * 1000);  //microseconds
  wifi_fpm_do_sleep(SLEEP_TIME_MS * 1000);  //microseconds
  delay(SLEEP_TIME_MS + 1);  // it goes to sleep //The system will not enter sleep mode instantly when force-sleep APIs are called, but only after executing an idle task.
  
  //DEBUG_PRINTLN(F("Sleep result (0 is ok): ") + String(res));  // the interrupt callback hits before this is executed
  //0, setting successful;
  //-1, failed to sleep, sleep status error;
  //-2, failed to sleep, force sleep function is not enabled
}

void WakeUpCallBackFunction()
{
  Serial.println();
  Serial.flush();
  //DEBUG_PRINTLN(F("Woke Up - CallBack Function executed at ms: ") + String(RTCmillis()));
  //DEBUG_FLUSH;
  //AlarmLoop();
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
//  static uint32_t lastReadMillis = 0;
//  if (RTCmillis() - lastReadMillis > (uint32_t)ESP_ZONES_READ_MS){
//    lastReadMillis = RTCmillis();
    for (int i = 0; i < SIZEOF_ZONE; i++){
      if (alarmConfig.Zone[i].Enabled && !ZONE_DISABLED[i]){
        s = digitalRead(GPIO_ID_PIN(ZONE_PIN[i]));
        if ((alarmConfig.Zone[i].TriggerNC && s == HIGH) || (!alarmConfig.Zone[i].TriggerNC && s == LOW)){
          zonesOk = false;
          ZONE_TRIGGERED[i] = true;
          if (ESP_ARMED && !ESP_FIRED){
            if (ESP_FIREDELAY){                                           //Was previously triggered a delayed zone
              if ((RTCmillis() - ESP_FIREDELAY_MILLIS)/1000 > alarmConfig.Zone[i].DelayOnSecs){
                AlarmFire();
              }
            } else if (alarmConfig.Zone[i].FirstAdviseDurationSecs > 0){  //Zone with first advise. Needs to be triggering at least FirstAdviseDurationSecs, after that the alarm will be fired
              if (!ESP_FIRSTDELAY){                   //first advise trigger
                ESP_FIRSTDELAY = true;
                ESP_FIRSTDELAY_MILLIS = RTCmillis();
              } else if ((RTCmillis() - ESP_FIRSTDELAY_MILLIS)/1000 > alarmConfig.Zone[i].FirstAdviseResetSecs){
                ESP_FIRSTDELAY = false;               //first advise expired
              } else if ((RTCmillis() - ESP_FIRSTDELAY_MILLIS)/1000 > alarmConfig.Zone[i].FirstAdviseDurationSecs){
                AlarmFire();                     //first advise fires alarm
              }
            } else if (alarmConfig.Zone[i].DelayOnSecs > 0){              //Zone with delay on
              ESP_FIREDELAY = true;
              ESP_FIREDELAY_MILLIS = RTCmillis();
            } else {                                                      //No delay on
              AlarmFire();
            }
          }
          if (ESP_ARMED && ZONE_STATE[i] != s){       //Zone had a new trigger -> register it
            ZONE_COUNT[i]++;
            DEBUG_PRINTLN("Zone" + String(i) + "triggered!");
          }
        }
        else{
          ZONE_TRIGGERED[i] = false;
        }
        ZONE_STATE[i] = s;
      }
    }
//  }
  return zonesOk;
}
void Read_Ring_State(){
  if (alarmConfig.Caller.GSMEnabled){
    int s = digitalRead(GPIO_ID_PIN(SIM800_RING_RESET_PIN));  //SMS RING pulse is only 120ms
    if (!SIM_RINGING && s == LOW){
      SIM_RINGING = true;
      //SIM_ONCALL = true;
      SIM_ONCALLMILLIS = RTCmillis();
      DTMFs="";
      SIM_WAITINGDTMF_ADA = false;
      DEBUG_PRINTLN("From Sim800 RI PIN: RINGING");
      //delay(2000);  //BORRAR ****************************************
    }
    else if (SIM_RINGING && s == HIGH) {
      SIM_RINGING = false;
      //SIM_ONCALL = false;
      //DTMFs="";
      //SIM_WAITINGDTMF_ADA = false;
      //DEBUG_PRINTLN("From Sim800 RI PIN: CALL ENDED");
      //delay(2000);  //BORRAR ****************************************
    }
  } 
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

/*void Sleep_Timed() {
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
}*/

/*String Sim800_Wait_Cmd(uint16_t timeout, String cmd){
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
}*/

/*String Sim800_AnswerString(uint16_t timeout){
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
}*/

float readVoltage() { // read internal VCC
  //DEBUG_PRINTLN("The internal VCC reads " + String(volts / 1000) + " volts");
  //return ESP.getVcc();
  return analogRead(A0) * 14.75 / 1000;
}

/*void printMillis() {
  DEBUG_PRINT(F("millis() = "));  // show that millis() isn't correct across most Sleep modes
  DEBUG_PRINTLN(millis());
  DEBUG_FLUSH;  // needs a Serial.flush() else it may not print the whole message before sleeping
}*/

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

void DelayYield(unsigned int TiempoMillis)
{
  unsigned long startTiempoMillis = millis();
  while (millis() - startTiempoMillis < TiempoMillis) { //tengo q restar en ese orden para que funcione siempre bien
    //espero
    yield();  //esto es para evitar que se RESETEE al quedar atrapado dentro del loop.
  }   
  //&& digitalRead(12)==HIGH Además de esperar el tiempo indicado, esta función monitorea un pin de entrada del Arduino que haya sido cableado para detectar que ha sucedido algún evento externo. Si esa señal va a nivel BAJO (LOW), se interrumpe el retardo.
}

