#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>

// Declare variables ---------------------------------------------------

#define HOST_ADDRESS 0x01

struct temperature_set
{
  float threshold, celcius;
} temperature;

struct timer_set
{
  byte hour, minute, setting;
} timer1, timer2, timer3;

struct RTC_now
{
  byte minute, hour;
} RTC;

struct device_settings
{
  byte duration;  // spray duration
} deviceSet;

unsigned long time_now = 0;
int valve1, valve2, queue, indx = 0;
char buffer[16];

const int relay1 = 4;
const int relay2 = 7;
const int relay3 = 8;

const int oneWireBus = 2; // GPIO DS18B20 (Temp sensor)
OneWire oneWire(oneWireBus);
DallasTemperature sensors(&oneWire);

union floatToBytes
{
  char text[4];
  float value;
} fl2b;

// Declare functions ---------------------------------------------------

unsigned long minuteToMillis(unsigned long minute);
unsigned long hourToMillis(unsigned long hour);
void receiveSettings();
void sendStatus();
void checkTemp();
void checkTime();
void debugging();

// Utility function ---------------------------------------------------------

unsigned long minuteToMillis(unsigned long minute)
{
  return minute * 60 * 1000;
}

unsigned long hourToMillis(unsigned long hour)
{
  return hour * 60 * 60 * 1000;
}

// I2C Comms ----------------------------------------------------------------

void receiveSettings(int n)
{ // Recieve settings from host per 30 sec and initial setup
  while (Wire.available())
  {
    buffer[indx] = Wire.read();
    indx++;
  }
  indx = 0;
  for (int i = 0; i < 4; i++)
  {
    fl2b.text[i] = buffer[i];
  }
  temperature.threshold = fl2b.value;
  RTC.hour = buffer[4];
  RTC.minute = buffer[5];
  deviceSet.duration = buffer[6];
  timer1.hour = buffer[7];
  timer1.minute = buffer[8];
  timer1.setting = buffer[9];
  timer2.hour = buffer[10];
  timer2.minute = buffer[11];
  timer2.setting = buffer[12];
  timer3.hour = buffer[13];
  timer3.minute = buffer[14];
  timer3.setting = buffer[15];
}

void sendStatus()
{ // Send temp status, prolly...
  fl2b.value = temperature.celcius;
  Wire.write(fl2b.text, 4);
}

// Main function ------------------------------------------------------------

void checkTemp()
{
  sensors.requestTemperatures();
  temperature.celcius = sensors.getTempCByIndex(0);
  if (temperature.celcius >= temperature.threshold && valve1 == 0 && valve2 == 0)
  {
    digitalWrite(relay2, HIGH);
    delay(500);
    digitalWrite(relay1, HIGH);
    valve1 = 1;
  }
  if (valve1 == 1 && temperature.celcius < temperature.threshold && valve2 == 0)
  {
    digitalWrite(relay1, LOW);
    delay(500);
    digitalWrite(relay2, LOW);
    valve1 = 0;
  }
}

void checkTime()
{
  if (RTC.hour == timer1.hour && RTC.minute == timer1.minute && timer1.setting == 1 && queue == 0)
  {
    queue = 1;
  }
  if (RTC.hour == timer2.hour && RTC.minute == timer2.minute && timer2.setting == 1 && queue == 0)
  {
    queue = 1;
  }
  if (RTC.hour == timer3.hour && RTC.minute == timer3.minute && timer3.setting == 1 && queue == 0)
  {
    queue = 1;
  }
  if (queue == 1 && valve1 == 0 && valve2 == 0)
  {
    time_now = millis();
    digitalWrite(relay3, HIGH);
    delay(500);
    digitalWrite(relay1, HIGH);
    valve2 = 1;
  }
  if (valve1 == 0 && valve2 == 1 && millis() - time_now >= minuteToMillis(deviceSet.duration))
  {
    digitalWrite(relay1, LOW);
    delay(500);
    digitalWrite(relay3, LOW);
    valve2 = 0;
    queue = 0;
  }
}

void debugging()
{
  Serial.println(sensors.getTempCByIndex(0));
  Serial.print("RTC: ");
  Serial.print(RTC.hour);
  Serial.print(":");
  Serial.println(RTC.minute);
  Serial.print("Timer1: ");
  Serial.print(timer1.hour);
  Serial.print(":");
  Serial.println(timer1.minute);
  Serial.println(timer1.setting);
  Serial.print("Timer2: ");
  Serial.print(timer2.hour);
  Serial.print(":");
  Serial.println(timer2.minute);
  Serial.println(timer2.setting);
  Serial.print("Timer3: ");
  Serial.print(timer3.hour);
  Serial.print(":");
  Serial.println(timer3.minute);
  Serial.println(timer3.setting);
  Serial.print("Duration: ");
  Serial.println(minuteToMillis(deviceSet.duration));
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

void setup()
{
  Wire.begin(8);
  Wire.onReceive(receiveSettings);
  Wire.onRequest(sendStatus);
  sensors.begin();
  pinMode(relay1, OUTPUT);
  pinMode(relay2, OUTPUT);
  pinMode(relay3, OUTPUT);
  Serial.begin(9600);
  Serial.println("Start ---------");
  RTC.hour = RTC.minute = timer1.hour = timer1.minute = timer1.setting = timer2.hour = timer2.minute = timer2.setting = timer3.hour = timer3.minute = timer3.setting = 0;
  deviceSet.duration = 1;
  temperature.threshold = 45.6;
  delay(1000);
}

void loop()
{
  checkTemp();
  checkTime();
  debugging();
  delay(500);
}
