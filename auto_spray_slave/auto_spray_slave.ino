  #include <OneWire.h>
  #include <DallasTemperature.h>
  #include <Wire.h>

  // Declare variables here ---------------------------------------------------

  #define HOST_ADDRESS 0x01

  struct {
    float threshold, celcius;
  } temperature;
  
  struct {
    byte duration, hour, minute;
  } timer;

  struct {
    byte hour, minute;
  } RTC;

  // Global var.
  unsigned long time_now = 0;
  int valve1, valve2, queue, indx = 0;
  char buffer[9];

  // Set relay pin
  const int relay1 = 4;
  const int relay2 = 7;
  const int relay3 = 8;

  // GPIO DS18B20 (Temp sensor)
  // GPIO 13 = Pin D7
  const int oneWireBus = 2;
  OneWire oneWire(oneWireBus);
  DallasTemperature sensors(&oneWire);

  union floatToBytes {
    char text[4];
    float value; 
  } fl2b;

  union recieveData {
    char text[4];
    float value;
  } rec_data;

  // Utility function ---------------------------------------------------------

  unsigned int minuteToMillis(unsigned int minute){
    unsigned int min;
    min = minute*60*1000;
    return min;
  }

  unsigned int hourToMillis(unsigned int hour){
    unsigned int hr;
    hr = hour*60*60*1000;
    return hr;
  }

  // I2C Comms ----------------------------------------------------------------
  void receiveSettings(){// Recieve settings from host per 30 sec and initial setup
    while (Wire.available()){
      buffer[indx] = Wire.read();
      indx++;
    }
    indx = 0;
    for (int i=0; i<4; i++){
      rec_data.text[i] = buffer[i];
    }
    temperature.threshold = rec_data.value;
    RTC.hour = buffer[4];
    RTC.minute = buffer[5];
    timer.duration = buffer[6];
    timer.hour = buffer[7];
    timer.minute = buffer[8];

  }

  void sendStatus(){ // Send temp status, prolly...
    fl2b.value = temperature.celcius;
    Wire.write(fl2b.text, 4);
  }

  // Main function ------------------------------------------------------------

  void checkTemp(){
    sensors.requestTemperatures();
    temperature.celcius = sensors.getTempCByIndex(0);
    if (temperature.celcius >= temperature.threshold && valve1 == 0 && valve2 == 0){
      time_now = millis();
      digitalWrite(relay2, HIGH);
      delay(500);
      digitalWrite(relay1, HIGH);
      valve1 = 1;
    }
    if (valve1 == 1 && temperature.celcius < temperature.threshold && valve2 == 0){
      digitalWrite(relay1, LOW);
      delay(500);
      digitalWrite(relay2, LOW);
      valve1 = 0;
    }
  }

  void checkTime(){
    if (RTC.hour == timer.hour && RTC.minute == timer.minute && queue == 0){
      queue = 1;
    }
    if (queue == 1 && valve1 == 0 && valve2 == 0){
      time_now = millis();
      digitalWrite(relay3, HIGH);
      delay(500);
      digitalWrite(relay1, HIGH);
      valve2 = 1;
    }
    if (valve1 == 0 && valve2 == 1 && millis() >= time_now + minuteToMillis(timer.duration)){
      digitalWrite(relay1, LOW);
      delay(500);
      digitalWrite(relay3, LOW);
      valve2 = 0;
      queue = 0;
    }
  }

  void debugging(){
    Serial.println(sensors.getTempCByIndex(0));
    Serial.print("RTC: ");
    Serial.print(RTC.hour);
    Serial.print(":");
    Serial.println(RTC.minute);
    Serial.print("Timer: ");
    Serial.print(timer.hour);
    Serial.print(":");
    Serial.println(timer.minute);
    Serial.print("Duration: ");
    Serial.println(minuteToMillis(timer.duration));
    Serial.print("Threshold: ");
    Serial.println(temperature.threshold);
    Serial.print("Valve-temp: ");
    Serial.println(valve1);
    Serial.print("Valve-timer: ");
    Serial.println(valve2);
    Serial.print("Q: ");
    Serial.println(queue);
    Serial.println("-----------------------------");
  }

  void setup() {
    Wire.begin(8);
    Wire.onReceive(receiveSettings);
    Wire.onRequest(sendStatus);
    sensors.begin();
    pinMode(relay1, OUTPUT);
    pinMode(relay2, OUTPUT);
    pinMode(relay3, OUTPUT);
    Serial.begin(9600);
    Serial.println("Start ---------");
    RTC.hour, RTC.minute = 0;
    timer.hour, timer.duration = 0;
    timer.minute = 3;
    temperature.threshold = 45.6;
    delay(1000);
  }

  void loop() {
    checkTemp();
    checkTime();
    debugging();
    delay(500);
  }
