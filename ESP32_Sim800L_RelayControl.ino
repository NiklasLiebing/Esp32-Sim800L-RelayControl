#define SIM800L_PWRKEY 4
#define SIM800L_RST    5
#define SIM800L_POWER  23
#define SIM800L_RX     26
#define SIM800L_TX     27

//defining the relay pins
#define RELAYPIN_1  18
#define RELAYPIN_2  19
#define RELAYPIN_3  25
#define RELAYPIN_4  26

#define uS_TO_S_FACTOR 1000000  //Conversion factor for micro seconds to seconds
#define SLEEP_UNTIL_FAN_START  600        //Time ESP32 will go to sleep (in seconds)
#define FAN_RUN_TIME  1200        //Time ESP32 will go to sleep (in seconds)

// Set serial for debug console (PC)
#define SerialMon Serial
// Set serial for AT commands (to SIM800 module)
#define SerialAT Serial1

//numbers for whitelist and the simcard pin 
const String FIRSTNUMBER = "00XXXXXXXXXXXXX";
const String SECONDNUMBER = "00XXXXXXXXXXXXX";
const String SIMCARDPIN = "1234";

bool heater_state = false;
uint8_t fan_state = 0;

//Interrupt service routine called on interrupt
int print_wakeup_reason() {
  return int(esp_sleep_get_wakeup_cause());
}

void loop()
{
  //disable wakeups
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  //check if call causes wakeup
  if (print_wakeup_reason() == 2)
  {
    if (heater_state==false) 
    {
      if(start_heater(fan_state)==0) heater_state=true;
    }
    else 
    {
      stop_heater();
    }
  }
  else
  {
    start_heater(fan_state);
  }
  updateSerial();
  //enable ext0 interrupt as abort routine
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_33,0);
  //start light sleep mode
  esp_light_sleep_start();
}

int start_heater(uint8_t current_state) 
{
  switch(current_state)
  {
    case 0: // switch on heater, fan off
      //answer call
      if(start_call(true)==0)
      {
        SerialMon.println("Open first relay");
        digitalWrite(RELAYPIN_1, HIGH);
        fan_state++;
        //enable timer interrupt
        esp_sleep_enable_timer_wakeup(SLEEP_UNTIL_FAN_START * uS_TO_S_FACTOR);
      }
      else
      {
        return -1;
      }
      break;
    case 1: //heater on, switch on fan
      SerialMon.println("Open second relay");
      digitalWrite(RELAYPIN_2, HIGH);
      //enable timer interrupt
      esp_sleep_enable_timer_wakeup(FAN_RUN_TIME * uS_TO_S_FACTOR);
      fan_state++;
      break;
    case 2: // heating process done
      heater_state = false;
      fan_state = 0;
      SerialMon.println("Heater finished");
      digitalWrite(RELAYPIN_1, LOW);
      digitalWrite(RELAYPIN_2, LOW);
      break;
    default:
      break;
  }
  return 0;
}

void stop_heater()
{
    heater_state = false;
    fan_state = 0;
    SerialMon.println("Close both relay");
    digitalWrite(RELAYPIN_1, LOW);
    digitalWrite(RELAYPIN_2, LOW);
    start_call(false);
}

int start_call(bool call_switch) {
  SerialMon.println("Answer call");
  // have to wait for the first ring
  delay(500);
  // neccessary to etablish serial connection after SIM modul sleep mode
  SerialAT.println("AT");
  delay(500);
  //answer the call
  SerialAT.println("ATA");
  if(wait_SerialATresponse("OK")<0) return -1;
  // send dtmf tone sequence
  if (call_switch)
  {
    SerialAT.println("AT+CLDTMF=2,\"E,E\"");
  }
  else
  {
    SerialAT.println("AT+CLDTMF=5,\"1\"");
  }
  if(wait_SerialATresponse("OK")<0) return -1;
  // hang up the call
  SerialAT.println("ATH");
  if(wait_SerialATresponse("OK")<0) return -1;
  return 0;
}

void setup()
{
  heater_state = false;
  fan_state = 0;

  //configure Relay Pins
  initRelayPin();
  
  // switch on SIM800L Modul
  initHardwareSIM800();
  delay(1000);
  
  //setup serial connections
  setup_serialConnections();
  delay(5000);

  //send configuration to SIM Modul
  init_GSMConnection();

  //configure wakeup source at pin 33
  pinMode(GPIO_NUM_33, INPUT_PULLUP);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_33,0);
  //attachInterrupt(GPIO_NUM_33, isr, FALLING);

  // start sleep mode
  SerialMon.println("Setup routines finished");
  delay(1000);
  //start light sleep mode
  esp_light_sleep_start();
}

void updateSerial() //this is for debug and finally can be disabled
{
  delay(500);
  while (SerialMon.available()) 
  {
    SerialAT.print(char(SerialMon.read()));//Forward what Serial received to Software Serial Port
  }
  while(SerialAT.available()) 
  {
    SerialMon.print(char(SerialAT.read()));//Forward what Software Serial received to Serial Port
  }
}

void initRelayPin()
{
  pinMode(RELAYPIN_1, OUTPUT);
  pinMode(RELAYPIN_2, OUTPUT);
  pinMode(RELAYPIN_3, OUTPUT);
  pinMode(RELAYPIN_4, OUTPUT);
  pinMode(13, OUTPUT);
  
  digitalWrite(RELAYPIN_1, LOW);
  digitalWrite(RELAYPIN_2, LOW);
  digitalWrite(RELAYPIN_3, LOW);
  digitalWrite(RELAYPIN_4, LOW);
}

void initHardwareSIM800()
{
  pinMode(SIM800L_PWRKEY, OUTPUT);
  pinMode(SIM800L_RST, OUTPUT);
  pinMode(SIM800L_POWER, OUTPUT);
  digitalWrite(SIM800L_PWRKEY, LOW);
  delay(100);
  digitalWrite(SIM800L_PWRKEY, HIGH);
  delay(1000);
  digitalWrite(SIM800L_PWRKEY, LOW);
  digitalWrite(SIM800L_RST, HIGH);
  digitalWrite(SIM800L_POWER, HIGH);
}

void setup_serialConnections()
{
  //Begin serial communication with PC
  SerialMon.begin(115200);
  //Begin serial communication with ESP32 and SIM800L
  SerialAT.begin(115200, SERIAL_8N1, SIM800L_RX, SIM800L_TX);
  delay(3000);
  serialATFlush();
  SerialMon.println("Serial Connections initialized");
}

void init_GSMConnection() 
{
  SerialMon.println("==> AT");
  SerialAT.println("AT"); //Once the handshake test is successful, it will back to OK
  wait_SerialATresponse("OK");

  delay(1000);

  //Get local time stamp
  SerialMon.println("==> Get locale time stamp");
  SerialAT.println("AT+CLTS=1");
  wait_SerialATresponse("OK");
  
  // Enable PIN 
  SerialMon.println("==> Enable PIN");
  SerialAT.println("AT+CPIN=\"" + SIMCARDPIN + "\"");
  wait_SerialATresponse("+CIEV");

  //Check Network Registration Status
  SerialMon.println("==> Check Network Registration");
  SerialAT.println("AT+CREG?");
  wait_SerialATresponse("OK");

  SerialMon.println("==> Set Whitelist");
  SerialAT.println("AT+CWHITELIST=3");
  wait_SerialATresponse("OK");
  SerialAT.println("AT+CWHITELIST=3,1,\"FIRSTNUMBER\",\"SECONDNUMBER\"");
  wait_SerialATresponse("OK");

  SerialMon.println("==> Define DTMF tone output");
  SerialAT.println("AT+DTAM=1");
  wait_SerialATresponse("OK");

  SerialMon.println("==> AT sleep mode");
  SerialAT.println("AT+CSCLK=2");
  wait_SerialATresponse("OK");

  
}

int wait_SerialATresponse(const char *endString)
{
  String response;
  bool ATbusy = true;
  int error_flag = 0;
  while (SerialAT.available() || ATbusy) 
  {
    response = SerialAT.readStringUntil('\n');
    SerialMon.print(response + "\r\n");
    if(response.startsWith(endString)) 
    {
      ATbusy = false;
      error_flag = 0; 
    }
    if(response.startsWith("+CME ERROR"))
    {
      ATbusy = false;
      error_flag = -1;
      break; 
    }
  }
  return error_flag;
}

void serialATFlush(){
  while(SerialAT.available() > 0) {
    SerialAT.read();
  }
}
