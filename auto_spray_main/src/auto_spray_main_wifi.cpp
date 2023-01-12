#include "Arduino.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>
#include <LittleFS.h>
#include <ESP8266Wifi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>

// Declare variables ---------------------------------------------------

/*
temperature.threshold = float, address at 0-3
deviceSet.backlight = byte, address 4
deviceSet.duration = byte, address at 5
timer1.hour = byte, address at 6
timer1.minute = byte, address at 7
timer1.setting = byte, address at 8
timer2.hour = byte, address at 9
timer2.minute = byte, address at 10
timer2.setting = byte, address at 11
timer3.hour = byte, address at 12
timer3.minute = byte, address at 13
timer3.setting = byte, address at 14
deviceSet.ssid = char array, address at 15 len, address at 16-47 data
deviceSet.pass = char array, address at 48 len, address at 49-112 data

RTC Address 0x68
LCD address 0x27
Arduino address 0x08
*/

#define EEPROM_SIZE 128
#define RTC_ADDRESS 0x68
#define LCD_ADDRESS 0x27
#define ATM_ADDRESS 0x08

IPAddress APIP(192, 168, 1, 1);
IPAddress subnet_mask(255, 255, 255, 0);

const byte WEB_PORT = 80;
const byte DNS_PORT = 53;

DNSServer dnsServer;
AsyncWebServer webServer(WEB_PORT);

class CaptiveRequestHandler : public AsyncWebHandler
{
public:
  CaptiveRequestHandler() {}
  virtual ~CaptiveRequestHandler() {}

  bool canHandle(AsyncWebServerRequest *request)
  {
    return true;
  }

  void handleRequest(AsyncWebServerRequest *request)
  {
    request->redirect("http://192.168.1.1");
  }
};

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
  byte second, minute, hour, dayOfWeek, dayOfMonth, month, year;
} RTC;

struct device_settings
{
  byte backlight; // 0: on, 1: 3 sec, 2: 5 sec, 3: 10 sec, 4: off
  byte duration;  // spray duration
  char ssid[32];
  char pass[63];
} deviceSet;

union floatToBytes
{
  char text[4];
  float value;
} fl2b;

unsigned long counter_send, counter_receive, counter_blink, counter_backlight = 0;
byte state, btn_set, blinker, indx, len = 0;
bool backlight_btn = true;
bool restart = false;

/*
pin GPIO 14 / D5
pin GPIO 12 / D6
pin GPIO 13 / D7
*/

const int buttonUp = 14;
const int buttonDown = 12;
const int buttonSet = 13;

unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 250;

int lcdColumns = 16;
int lcdRows = 2;
LiquidCrystal_I2C lcd(LCD_ADDRESS, lcdColumns, lcdRows);

byte charDegree[8] = {
    0b00010,
    0b00101,
    0b00010,
    0b00000,
    0b00000,
    0b00000,
    0b00000,
    0b00000};

byte charT1[8] = {
    0b11100,
    0b01000,
    0b01000,
    0b00010,
    0b00110,
    0b00010,
    0b00010,
    0b00111};

byte charT2[8] = {
    0b11100,
    0b01000,
    0b01000,
    0b00110,
    0b00001,
    0b00010,
    0b00100,
    0b00111};

byte charT3[8] = {
    0b11100,
    0b01000,
    0b01000,
    0b00110,
    0b00001,
    0b00010,
    0b00001,
    0b00110};

// Declare functions ---------------------------------------------------

void sendSettings();
void receiveStatus();
void factoryReset();
void writeChartoEEPROM();
void fetchEEPROM();
bool buttonRead(int pin);
void backlightMode();
unsigned long minuteToMillis(unsigned long minute);
byte decToBcd(byte val);
byte bcdToDec(byte val);
void setDS3231time(byte second, byte minute, byte hour, byte dayOfWeek, byte dayOfMonth, byte month, byte year);
void readDS3231time(byte *second, byte *minute, byte *hour, byte *dayOfWeek, byte *dayOfMonth, byte *month, byte *year);
void debugging();
void displayMain();
void displayTempSet();
void displayTempSetEdit();
void displayTimeSet();
void displayTimeSetT1Hour();
void displayTimeSetT1Minute();
void displayTimeSetT2Hour();
void displayTimeSetT2Minute();
void displayTimeSetT3Hour();
void displayTimeSetT3Minute();
void displayTimerSelect();
void displayTimerSelectT1Edit();
void displayTimerSelectT2Edit();
void displayTimerSelectT3Edit();
void displayDurationSet();
void displayDurationSetEdit();
void displayBacklightSettings();
void displayBacklightSettingsEdit();
void displayRTCset();
void displayRTCsetHour();
void displayRTCsetMinute();
void displayFactoryReset();
void displayFactoryResetConfirm();
void displayMenu();
void buttonMenu();
String statusTimer(byte status);
String concatTime(byte hour, byte minute);
void setupServer();

// I2C Comms -----------------------------------------------------------

void sendSettings()
{ // send per sec
  if (millis() - counter_send >= 1000)
  {
    Wire.beginTransmission(ATM_ADDRESS);
    fl2b.value = temperature.threshold;
    Wire.write(fl2b.text, 4);
    Wire.write(RTC.hour);
    Wire.write(RTC.minute);
    Wire.write(deviceSet.duration);
    Wire.write(timer1.hour);
    Wire.write(timer1.minute);
    Wire.write(timer1.setting);
    Wire.write(timer2.hour);
    Wire.write(timer2.minute);
    Wire.write(timer2.setting);
    Wire.write(timer3.hour);
    Wire.write(timer3.minute);
    Wire.write(timer3.setting);
    Wire.endTransmission();
    counter_send = millis();
  }
}

void receiveStatus()
{ // receive per sec
  if (millis() - counter_receive >= 1000)
  {
    Wire.requestFrom(ATM_ADDRESS, 4);
    while (Wire.available())
    {
      fl2b.text[indx] = Wire.read();
      indx++;
    }
    indx = 0;
    temperature.celcius = fl2b.value;
    counter_receive = millis();
  }
}

// Utility function -----------------------------------------------------

void factoryReset()
{
  setDS3231time(00, 00, 00, 7, 01, 10, 22);
  temperature.threshold = 30.5;
  EEPROM.put(0, temperature.threshold);
  EEPROM.put(4, 0);
  EEPROM.put(5, 1);
  EEPROM.put(6, 0);
  EEPROM.put(7, 0);
  EEPROM.put(8, 0);
  EEPROM.put(9, 0);
  EEPROM.put(10, 0);
  EEPROM.put(11, 0);
  EEPROM.put(12, 0);
  EEPROM.put(13, 0);
  EEPROM.put(14, 0);
  strcpy(deviceSet.ssid, "ESP Mtech");
  len = strlen(deviceSet.ssid);
  EEPROM.put(15, len);
  for (int i = 0; i < len; i++)
  {
    EEPROM.put(16 + i, deviceSet.ssid[i]);
  }
  strcpy(deviceSet.pass, "1234567890");
  len = strlen(deviceSet.pass);
  EEPROM.put(48, len);
  for (int i = 0; i < len; i++)
  {
    EEPROM.put(49 + i, deviceSet.pass[i]);
  }
  EEPROM.commit();
  restart = true;
}

void fetchEEPROM()
{
  EEPROM.get(0, temperature.threshold);
  EEPROM.get(4, deviceSet.backlight);
  EEPROM.get(5, deviceSet.duration);
  EEPROM.get(6, timer1.hour);
  EEPROM.get(7, timer1.minute);
  EEPROM.get(8, timer1.setting);
  EEPROM.get(9, timer2.hour);
  EEPROM.get(10, timer2.minute);
  EEPROM.get(11, timer2.setting);
  EEPROM.get(12, timer3.hour);
  EEPROM.get(13, timer3.minute);
  EEPROM.get(14, timer3.setting);
  byte len;
  EEPROM.get(15, len);
  for (int i = 0; i < len; i++)
  {
    EEPROM.get(16 + i, deviceSet.ssid[i]);
  }
  EEPROM.get(48, len);
  for (int i = 0; i < len; i++)
  {
    EEPROM.get(49 + i, deviceSet.pass[i]);
  }
}

bool buttonRead(int pin)
{
  if ((millis() - lastDebounceTime) > debounceDelay)
  {
    if (digitalRead(pin) == LOW)
    {
      lastDebounceTime = millis();
      backlight_btn = true;
      return true;
    }
    else
    {
      backlight_btn = false;
      return false;
    }
  }
  else
  {
    return false;
  }
}

void backlightMode()
{
  if (deviceSet.backlight == 0)
  {
    lcd.setBacklight(HIGH);
  }

  if (backlight_btn == true && deviceSet.backlight != 0 && deviceSet.backlight != 4)
  {
    lcd.setBacklight(HIGH);
    counter_backlight = millis();
  }

  if (deviceSet.backlight == 1 && backlight_btn == false && millis() - counter_backlight > 3000)
  {
    lcd.setBacklight(LOW);
  }

  if (deviceSet.backlight == 2 && backlight_btn == false && millis() - counter_backlight > 5000)
  {
    lcd.setBacklight(LOW);
  }

  if (deviceSet.backlight == 3 && backlight_btn == false && millis() - counter_backlight > 10000)
  {
    lcd.setBacklight(LOW);
  }

  if (deviceSet.backlight == 4)
  {
    lcd.setBacklight(LOW);
  }
}

unsigned long minuteToMillis(unsigned long minute)
{
  return minute * 60 * 1000;
}

byte decToBcd(byte val)
{ // Convert normal decimal numbers to binary coded decimal
  return ((val / 10 * 16) + (val % 10));
}

byte bcdToDec(byte val)
{ // Convert binary coded decimal to normal decimal numbers
  return ((val / 16 * 10) + (val % 16));
}

void setDS3231time(byte second, byte minute, byte hour, byte dayOfWeek, byte dayOfMonth, byte month, byte year)
{ // sets time and date data to DS3231
  Wire.beginTransmission(RTC_ADDRESS);
  Wire.write(0);                    // set next input to start at the seconds register
  Wire.write(decToBcd(second));     // set seconds
  Wire.write(decToBcd(minute));     // set minutes
  Wire.write(decToBcd(hour));       // set hours
  Wire.write(decToBcd(dayOfWeek));  // set day of week (1=Sunday, 7=Saturday)
  Wire.write(decToBcd(dayOfMonth)); // set date (1 to 31)
  Wire.write(decToBcd(month));      // set month
  Wire.write(decToBcd(year));       // set year (0 to 99)
  Wire.endTransmission();
}

void readDS3231time(byte *second, // Read from RTC
                    byte *minute,
                    byte *hour,
                    byte *dayOfWeek,
                    byte *dayOfMonth,
                    byte *month,
                    byte *year)
{
  Wire.beginTransmission(RTC_ADDRESS);
  Wire.write(0); // set DS3231 register pointer to 00h
  Wire.endTransmission();
  Wire.requestFrom(RTC_ADDRESS, 7); // request seven bytes of data from DS3231 starting from register 00h
  *second = bcdToDec(Wire.read() & 0x7f);
  *minute = bcdToDec(Wire.read());
  *hour = bcdToDec(Wire.read() & 0x3f);
  *dayOfWeek = bcdToDec(Wire.read());
  *dayOfMonth = bcdToDec(Wire.read());
  *month = bcdToDec(Wire.read());
  *year = bcdToDec(Wire.read());
}

void debugging()
{
  Serial.println(temperature.celcius);
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
  Serial.print("Backlight: ");
  Serial.println(deviceSet.backlight);
  Serial.println("-----------------------------");
  byte error, address;
  int nDevices = 0;
  for (address = 1; address < 127; address++){
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    if (error == 0){
      Serial.print("I2C device found at address 0x");
      if (address < 16)
        Serial.print("0");
      Serial.print(address, HEX);
      Serial.println("  !");
      nDevices++;
    }
    else if (error == 4){
      Serial.print("Unknown error at address 0x");
      if (address < 16)
        Serial.print("0");
      Serial.println(address, HEX);
    }
  }
  if (nDevices == 0)
    Serial.println("No I2C devices found\n");
  else
    Serial.println("done\n");
  delay(5000);
}

// Menu item function ----------------------------------------------------------------

void displayMain()
{
  readDS3231time(&RTC.second, &RTC.minute, &RTC.hour, &RTC.dayOfWeek, &RTC.dayOfMonth, &RTC.month, &RTC.year);
  lcd.setCursor(0, 0);
  lcd.print("Temp:");
  lcd.setCursor(6, 0);
  lcd.print(temperature.celcius);
  lcd.setCursor(11, 0);
  lcd.write((uint8_t)0);
  lcd.setCursor(12, 0);
  lcd.print("C");
  lcd.setCursor(0, 1);
  lcd.print("Time:");
  lcd.setCursor(6, 1);
  if (RTC.hour < 10)
  {
    lcd.print("0");
    lcd.setCursor(7, 1);
    lcd.print(RTC.hour);
  }
  else
  {
    lcd.print(RTC.hour);
  }
  lcd.setCursor(8, 1);
  lcd.print(":");
  lcd.setCursor(9, 1);
  if (RTC.minute < 10)
  {
    lcd.print("0");
    lcd.setCursor(10, 1);
    lcd.print(RTC.minute);
  }
  else
  {
    lcd.print(RTC.minute);
  }
}

void displayTempSet()
{
  lcd.setCursor(0, 0);
  lcd.print("Temp Threshold");
  lcd.setCursor(0, 1);
  lcd.print(temperature.threshold);
  lcd.setCursor(4, 1);
  lcd.write((uint8_t)0);
  lcd.setCursor(5, 1);
  lcd.print("C");
}

void displayTempSetEdit()
{
  lcd.setCursor(0, 0);
  lcd.print("Temp Threshold");
  lcd.setCursor(4, 1);
  lcd.write((uint8_t)0);
  lcd.setCursor(5, 1);
  lcd.print("C");
}

void displayTimeSet()
{
  lcd.setCursor(0, 0);
  lcd.print("Timer");

  lcd.setCursor(8, 0);
  lcd.write((uint8_t)1);
  lcd.setCursor(10, 0);
  if (timer1.hour < 10)
  {
    lcd.print("0");
    lcd.setCursor(11, 0);
    lcd.print(timer1.hour);
  }
  else
  {
    lcd.print(timer1.hour);
  }
  lcd.setCursor(12, 0);
  lcd.print(":");
  lcd.setCursor(13, 0);
  if (timer1.minute < 10)
  {
    lcd.print("0");
    lcd.setCursor(14, 0);
    lcd.print(timer1.minute);
  }
  else
  {
    lcd.print(timer1.minute);
  }

  lcd.setCursor(0, 1);
  lcd.write((uint8_t)2);
  lcd.setCursor(2, 1);
  if (timer2.hour < 10)
  {
    lcd.print("0");
    lcd.setCursor(3, 1);
    lcd.print(timer2.hour);
  }
  else
  {
    lcd.print(timer2.hour);
  }
  lcd.setCursor(4, 1);
  lcd.print(":");
  lcd.setCursor(5, 1);
  if (timer2.minute < 10)
  {
    lcd.print("0");
    lcd.setCursor(6, 1);
    lcd.print(timer2.minute);
  }
  else
  {
    lcd.print(timer2.minute);
  }

  lcd.setCursor(8, 1);
  lcd.write((uint8_t)3);
  lcd.setCursor(10, 1);
  if (timer3.hour < 10)
  {
    lcd.print("0");
    lcd.setCursor(11, 1);
    lcd.print(timer3.hour);
  }
  else
  {
    lcd.print(timer3.hour);
  }
  lcd.setCursor(12, 1);
  lcd.print(":");
  lcd.setCursor(13, 1);
  if (timer3.minute < 10)
  {
    lcd.print("0");
    lcd.setCursor(14, 1);
    lcd.print(timer3.minute);
  }
  else
  {
    lcd.print(timer3.minute);
  }
}

void displayTimeSetT1Hour()
{
  lcd.setCursor(0, 0);
  lcd.print("Timer");

  lcd.setCursor(8, 0);
  lcd.write((uint8_t)1);
  lcd.setCursor(12, 0);
  lcd.print(":");
  lcd.setCursor(13, 0);
  if (timer1.minute < 10)
  {
    lcd.print("0");
    lcd.setCursor(14, 0);
    lcd.print(timer1.minute);
  }
  else
  {
    lcd.print(timer1.minute);
  }

  lcd.setCursor(0, 1);
  lcd.write((uint8_t)2);
  lcd.setCursor(2, 1);
  if (timer2.hour < 10)
  {
    lcd.print("0");
    lcd.setCursor(3, 1);
    lcd.print(timer2.hour);
  }
  else
  {
    lcd.print(timer2.hour);
  }
  lcd.setCursor(4, 1);
  lcd.print(":");
  lcd.setCursor(5, 1);
  if (timer2.minute < 10)
  {
    lcd.print("0");
    lcd.setCursor(6, 1);
    lcd.print(timer2.minute);
  }
  else
  {
    lcd.print(timer2.minute);
  }

  lcd.setCursor(8, 1);
  lcd.write((uint8_t)3);
  lcd.setCursor(10, 1);
  if (timer3.hour < 10)
  {
    lcd.print("0");
    lcd.setCursor(11, 1);
    lcd.print(timer3.hour);
  }
  else
  {
    lcd.print(timer3.hour);
  }
  lcd.setCursor(12, 1);
  lcd.print(":");
  lcd.setCursor(13, 1);
  if (timer3.minute < 10)
  {
    lcd.print("0");
    lcd.setCursor(14, 1);
    lcd.print(timer3.minute);
  }
  else
  {
    lcd.print(timer3.minute);
  }
}

void displayTimeSetT1Minute()
{
  lcd.setCursor(0, 0);
  lcd.print("Timer");

  lcd.setCursor(8, 0);
  lcd.write((uint8_t)1);
  lcd.setCursor(10, 0);
  if (timer1.hour < 10)
  {
    lcd.print("0");
    lcd.setCursor(11, 0);
    lcd.print(timer1.hour);
  }
  else
  {
    lcd.print(timer1.hour);
  }
  lcd.setCursor(12, 0);
  lcd.print(":");

  lcd.setCursor(0, 1);
  lcd.write((uint8_t)2);
  lcd.setCursor(2, 1);
  if (timer2.hour < 10)
  {
    lcd.print("0");
    lcd.setCursor(3, 1);
    lcd.print(timer2.hour);
  }
  else
  {
    lcd.print(timer2.hour);
  }
  lcd.setCursor(4, 1);
  lcd.print(":");
  lcd.setCursor(5, 1);
  if (timer2.minute < 10)
  {
    lcd.print("0");
    lcd.setCursor(6, 1);
    lcd.print(timer2.minute);
  }
  else
  {
    lcd.print(timer2.minute);
  }

  lcd.setCursor(8, 1);
  lcd.write((uint8_t)3);
  lcd.setCursor(10, 1);
  if (timer3.hour < 10)
  {
    lcd.print("0");
    lcd.setCursor(11, 1);
    lcd.print(timer3.hour);
  }
  else
  {
    lcd.print(timer3.hour);
  }
  lcd.setCursor(12, 1);
  lcd.print(":");
  lcd.setCursor(13, 1);
  if (timer3.minute < 10)
  {
    lcd.print("0");
    lcd.setCursor(14, 1);
    lcd.print(timer3.minute);
  }
  else
  {
    lcd.print(timer3.minute);
  }
}

void displayTimeSetT2Hour()
{
  lcd.setCursor(0, 0);
  lcd.print("Timer");

  lcd.setCursor(8, 0);
  lcd.write((uint8_t)1);
  lcd.setCursor(10, 0);
  if (timer1.hour < 10)
  {
    lcd.print("0");
    lcd.setCursor(11, 0);
    lcd.print(timer1.hour);
  }
  else
  {
    lcd.print(timer1.hour);
  }
  lcd.setCursor(12, 0);
  lcd.print(":");
  lcd.setCursor(13, 0);
  if (timer1.minute < 10)
  {
    lcd.print("0");
    lcd.setCursor(14, 0);
    lcd.print(timer1.minute);
  }
  else
  {
    lcd.print(timer1.minute);
  }

  lcd.setCursor(0, 1);
  lcd.write((uint8_t)2);
  lcd.setCursor(4, 1);
  lcd.print(":");
  lcd.setCursor(5, 1);
  if (timer2.minute < 10)
  {
    lcd.print("0");
    lcd.setCursor(6, 1);
    lcd.print(timer2.minute);
  }
  else
  {
    lcd.print(timer2.minute);
  }

  lcd.setCursor(8, 1);
  lcd.write((uint8_t)3);
  lcd.setCursor(10, 1);
  if (timer3.hour < 10)
  {
    lcd.print("0");
    lcd.setCursor(11, 1);
    lcd.print(timer3.hour);
  }
  else
  {
    lcd.print(timer3.hour);
  }
  lcd.setCursor(12, 1);
  lcd.print(":");
  lcd.setCursor(13, 1);
  if (timer3.minute < 10)
  {
    lcd.print("0");
    lcd.setCursor(14, 1);
    lcd.print(timer3.minute);
  }
  else
  {
    lcd.print(timer3.minute);
  }
}

void displayTimeSetT2Minute()
{
  lcd.setCursor(0, 0);
  lcd.print("Timer");

  lcd.setCursor(8, 0);
  lcd.write((uint8_t)1);
  lcd.setCursor(10, 0);
  if (timer1.hour < 10)
  {
    lcd.print("0");
    lcd.setCursor(11, 0);
    lcd.print(timer1.hour);
  }
  else
  {
    lcd.print(timer1.hour);
  }
  lcd.setCursor(12, 0);
  lcd.print(":");
  lcd.setCursor(13, 0);
  if (timer1.minute < 10)
  {
    lcd.print("0");
    lcd.setCursor(14, 0);
    lcd.print(timer1.minute);
  }
  else
  {
    lcd.print(timer1.minute);
  }

  lcd.setCursor(0, 1);
  lcd.write((uint8_t)2);
  lcd.setCursor(2, 1);
  if (timer2.hour < 10)
  {
    lcd.print("0");
    lcd.setCursor(3, 1);
    lcd.print(timer2.hour);
  }
  else
  {
    lcd.print(timer2.hour);
  }
  lcd.setCursor(4, 1);
  lcd.print(":");

  lcd.setCursor(8, 1);
  lcd.write((uint8_t)3);
  lcd.setCursor(10, 1);
  if (timer3.hour < 10)
  {
    lcd.print("0");
    lcd.setCursor(11, 1);
    lcd.print(timer3.hour);
  }
  else
  {
    lcd.print(timer3.hour);
  }
  lcd.setCursor(12, 1);
  lcd.print(":");
  lcd.setCursor(13, 1);
  if (timer3.minute < 10)
  {
    lcd.print("0");
    lcd.setCursor(14, 1);
    lcd.print(timer3.minute);
  }
  else
  {
    lcd.print(timer3.minute);
  }
}

void displayTimeSetT3Hour()
{
  lcd.setCursor(0, 0);
  lcd.print("Timer");

  lcd.setCursor(8, 0);
  lcd.write((uint8_t)1);
  lcd.setCursor(10, 0);
  if (timer1.hour < 10)
  {
    lcd.print("0");
    lcd.setCursor(11, 0);
    lcd.print(timer1.hour);
  }
  else
  {
    lcd.print(timer1.hour);
  }
  lcd.setCursor(12, 0);
  lcd.print(":");
  lcd.setCursor(13, 0);
  if (timer1.minute < 10)
  {
    lcd.print("0");
    lcd.setCursor(14, 0);
    lcd.print(timer1.minute);
  }
  else
  {
    lcd.print(timer1.minute);
  }

  lcd.setCursor(0, 1);
  lcd.write((uint8_t)2);
  lcd.setCursor(2, 1);
  if (timer2.hour < 10)
  {
    lcd.print("0");
    lcd.setCursor(3, 1);
    lcd.print(timer2.hour);
  }
  else
  {
    lcd.print(timer2.hour);
  }
  lcd.setCursor(4, 1);
  lcd.print(":");
  lcd.setCursor(5, 1);
  if (timer2.minute < 10)
  {
    lcd.print("0");
    lcd.setCursor(6, 1);
    lcd.print(timer2.minute);
  }
  else
  {
    lcd.print(timer2.minute);
  }

  lcd.setCursor(8, 1);
  lcd.write((uint8_t)3);
  lcd.setCursor(12, 1);
  lcd.print(":");
  lcd.setCursor(13, 1);
  if (timer3.minute < 10)
  {
    lcd.print("0");
    lcd.setCursor(14, 1);
    lcd.print(timer3.minute);
  }
  else
  {
    lcd.print(timer3.minute);
  }
}

void displayTimeSetT3Minute()
{
  lcd.setCursor(0, 0);
  lcd.print("Timer");

  lcd.setCursor(8, 0);
  lcd.write((uint8_t)1);
  lcd.setCursor(10, 0);
  if (timer1.hour < 10)
  {
    lcd.print("0");
    lcd.setCursor(11, 0);
    lcd.print(timer1.hour);
  }
  else
  {
    lcd.print(timer1.hour);
  }
  lcd.setCursor(12, 0);
  lcd.print(":");
  lcd.setCursor(13, 0);
  if (timer1.minute < 10)
  {
    lcd.print("0");
    lcd.setCursor(14, 0);
    lcd.print(timer1.minute);
  }
  else
  {
    lcd.print(timer1.minute);
  }

  lcd.setCursor(0, 1);
  lcd.write((uint8_t)2);
  lcd.setCursor(2, 1);
  if (timer2.hour < 10)
  {
    lcd.print("0");
    lcd.setCursor(3, 1);
    lcd.print(timer2.hour);
  }
  else
  {
    lcd.print(timer2.hour);
  }
  lcd.setCursor(4, 1);
  lcd.print(":");
  lcd.setCursor(5, 1);
  if (timer2.minute < 10)
  {
    lcd.print("0");
    lcd.setCursor(6, 1);
    lcd.print(timer2.minute);
  }
  else
  {
    lcd.print(timer2.minute);
  }

  lcd.setCursor(8, 1);
  lcd.write((uint8_t)3);
  lcd.setCursor(10, 1);
  if (timer3.hour < 10)
  {
    lcd.print("0");
    lcd.setCursor(11, 1);
    lcd.print(timer3.hour);
  }
  else
  {
    lcd.print(timer3.hour);
  }
  lcd.setCursor(12, 1);
  lcd.print(":");
}

void displayTimerSelect()
{
  lcd.setCursor(0, 0);
  lcd.print("Timer");
  lcd.setCursor(8, 0);
  lcd.write((uint8_t)1);
  lcd.setCursor(10, 0);
  if (timer1.setting == 0)
  {
    lcd.print("Off");
  }
  if (timer1.setting == 1)
  {
    lcd.print("On");
  }

  lcd.setCursor(0, 1);
  lcd.write((uint8_t)2);
  lcd.setCursor(2, 1);
  if (timer2.setting == 0)
  {
    lcd.print("Off");
  }
  if (timer2.setting == 1)
  {
    lcd.print("On");
  }

  lcd.setCursor(8, 1);
  lcd.write((uint8_t)3);
  lcd.setCursor(10, 1);
  if (timer3.setting == 0)
  {
    lcd.print("Off");
  }
  if (timer3.setting == 1)
  {
    lcd.print("On");
  }
}

void displayTimerSelectT1Edit()
{
  lcd.setCursor(0, 0);
  lcd.print("Timer");
  lcd.setCursor(8, 0);
  lcd.write((uint8_t)1);

  lcd.setCursor(0, 1);
  lcd.write((uint8_t)2);
  lcd.setCursor(2, 1);
  if (timer2.setting == 0)
  {
    lcd.print("Off");
  }
  if (timer2.setting == 1)
  {
    lcd.print("On");
  }

  lcd.setCursor(8, 1);
  lcd.write((uint8_t)3);
  lcd.setCursor(10, 1);
  if (timer3.setting == 0)
  {
    lcd.print("Off");
  }
  if (timer3.setting == 1)
  {
    lcd.print("On");
  }
}

void displayTimerSelectT2Edit()
{
  lcd.setCursor(0, 0);
  lcd.print("Timer");
  lcd.setCursor(8, 0);
  lcd.write((uint8_t)1);
  lcd.setCursor(10, 0);
  if (timer1.setting == 0)
  {
    lcd.print("Off");
  }
  if (timer1.setting == 1)
  {
    lcd.print("On");
  }

  lcd.setCursor(0, 1);
  lcd.write((uint8_t)2);

  lcd.setCursor(8, 1);
  lcd.write((uint8_t)3);
  lcd.setCursor(10, 1);
  if (timer3.setting == 0)
  {
    lcd.print("Off");
  }
  if (timer3.setting == 1)
  {
    lcd.print("On");
  }
}

void displayTimerSelectT3Edit()
{
  lcd.setCursor(0, 0);
  lcd.print("Timer");
  lcd.setCursor(8, 0);
  lcd.write((uint8_t)1);
  lcd.setCursor(10, 0);
  if (timer1.setting == 0)
  {
    lcd.print("Off");
  }
  if (timer1.setting == 1)
  {
    lcd.print("On");
  }

  lcd.setCursor(0, 1);
  lcd.write((uint8_t)2);
  lcd.setCursor(2, 1);
  if (timer2.setting == 0)
  {
    lcd.print("Off");
  }
  if (timer2.setting == 1)
  {
    lcd.print("On");
  }

  lcd.setCursor(8, 1);
  lcd.write((uint8_t)3);
}

void displayDurationSet()
{
  lcd.setCursor(0, 0);
  lcd.print("Spray Duration");
  lcd.setCursor(0, 1);
  lcd.print(deviceSet.duration);
  lcd.setCursor(3, 1);
  lcd.print("min");
}

void displayDurationSetEdit()
{
  lcd.setCursor(0, 0);
  lcd.print("Spray Duration");
  lcd.setCursor(3, 1);
  lcd.print("min");
}

void displayBacklightSettings()
{
  lcd.setCursor(0, 0);
  lcd.print("Backlight");
  lcd.setCursor(0, 1);
  switch (deviceSet.backlight)
  {
  case 0:
    lcd.print("Always on");
    break;
  case 1:
    lcd.print("3 sec");
    break;
  case 2:
    lcd.print("5 sec");
    break;
  case 3:
    lcd.print("10 sec");
    break;
  case 4:
    lcd.print("Always off");
    break;
  }
}

void displayBacklightSettingsEdit()
{
  lcd.setCursor(0, 0);
  lcd.print("Backlight");
}

void displayRTCset()
{
  lcd.setCursor(0, 0);
  lcd.print("RTC Set");
  lcd.setCursor(0, 1);
  if (RTC.hour < 10)
  {
    lcd.print("0");
    lcd.setCursor(1, 1);
    lcd.print(RTC.hour);
  }
  else
  {
    lcd.print(RTC.hour);
  }
  lcd.setCursor(2, 1);
  lcd.print(":");
  lcd.setCursor(3, 1);
  if (RTC.minute < 10)
  {
    lcd.print("0");
    lcd.setCursor(4, 1);
    lcd.print(RTC.minute);
  }
  else
  {
    lcd.print(RTC.minute);
  }
}

void displayRTCsetHour()
{
  lcd.setCursor(0, 0);
  lcd.print("RTC Set");
  lcd.setCursor(2, 1);
  lcd.print(":");
  lcd.setCursor(3, 1);
  if (RTC.minute < 10)
  {
    lcd.print("0");
    lcd.setCursor(4, 1);
    lcd.print(RTC.minute);
  }
  else
  {
    lcd.print(RTC.minute);
  }
}

void displayRTCsetMinute()
{
  lcd.setCursor(0, 0);
  lcd.print("RTC Set");
  lcd.setCursor(0, 1);
  if (RTC.hour < 10)
  {
    lcd.print("0");
    lcd.setCursor(1, 1);
    lcd.print(RTC.hour);
  }
  else
  {
    lcd.print(RTC.hour);
  }
  lcd.setCursor(2, 1);
  lcd.print(":");
}

void displayFactoryReset()
{
  lcd.setCursor(0, 0);
  lcd.print("Factory Reset");
}

void displayFactoryResetConfirm()
{
  lcd.setCursor(0, 0);
  lcd.print("Confirm?");
  lcd.setCursor(0, 1);
  lcd.print("Arrow to Cancel");
}

// Menu display function -------------------------------------

void displayMenu()
{
  if (state == 0 && btn_set == 0)
  { // state 0, main menu
    displayMain();
  }

  if (state == 1 && btn_set == 0)
  { // state 1, temp set menu
    displayTempSet();
  }

  if (state == 1 && btn_set == 1)
  {
    if (millis() - counter_blink > 750 && blinker == 0)
    {
      lcd.clear();
      displayTempSetEdit();
      counter_blink = millis();
      blinker = 1;
    }
    if (millis() - counter_blink > 750 && blinker == 1)
    {
      lcd.clear();
      displayTempSet();
      counter_blink = millis();
      blinker = 0;
    }
  }

  if (state == 2 && btn_set == 0)
  { // state 2, timer set menu
    displayTimeSet();
  }

  if (state == 2 && btn_set == 1)
  {
    if (millis() - counter_blink > 750 && blinker == 0)
    {
      lcd.clear();
      displayTimeSetT1Hour();
      counter_blink = millis();
      blinker = 1;
    }
    if (millis() - counter_blink > 750 && blinker == 1)
    {
      lcd.clear();
      displayTimeSet();
      counter_blink = millis();
      blinker = 0;
    }
  }

  if (state == 2 && btn_set == 2)
  {
    if (millis() - counter_blink > 750 && blinker == 0)
    {
      lcd.clear();
      displayTimeSetT1Minute();
      counter_blink = millis();
      blinker = 1;
    }
    if (millis() - counter_blink > 750 && blinker == 1)
    {
      lcd.clear();
      displayTimeSet();
      counter_blink = millis();
      blinker = 0;
    }
  }

  if (state == 2 && btn_set == 3)
  {
    if (millis() - counter_blink > 750 && blinker == 0)
    {
      lcd.clear();
      displayTimeSetT2Hour();
      counter_blink = millis();
      blinker = 1;
    }
    if (millis() - counter_blink > 750 && blinker == 1)
    {
      lcd.clear();
      displayTimeSet();
      counter_blink = millis();
      blinker = 0;
    }
  }

  if (state == 2 && btn_set == 4)
  {
    if (millis() - counter_blink > 750 && blinker == 0)
    {
      lcd.clear();
      displayTimeSetT2Minute();
      counter_blink = millis();
      blinker = 1;
    }
    if (millis() - counter_blink > 750 && blinker == 1)
    {
      lcd.clear();
      displayTimeSet();
      counter_blink = millis();
      blinker = 0;
    }
  }

  if (state == 2 && btn_set == 5)
  {
    if (millis() - counter_blink > 750 && blinker == 0)
    {
      lcd.clear();
      displayTimeSetT3Hour();
      counter_blink = millis();
      blinker = 1;
    }
    if (millis() - counter_blink > 750 && blinker == 1)
    {
      lcd.clear();
      displayTimeSet();
      counter_blink = millis();
      blinker = 0;
    }
  }

  if (state == 2 && btn_set == 6)
  {
    if (millis() - counter_blink > 750 && blinker == 0)
    {
      lcd.clear();
      displayTimeSetT3Minute();
      counter_blink = millis();
      blinker = 1;
    }
    if (millis() - counter_blink > 750 && blinker == 1)
    {
      lcd.clear();
      displayTimeSet();
      counter_blink = millis();
      blinker = 0;
    }
  }

  if (state == 3 && btn_set == 0)
  { // state 3, choose timer on
    displayTimerSelect();
  }

  if (state == 3 && btn_set == 1)
  {
    if (millis() - counter_blink > 750 && blinker == 0)
    {
      lcd.clear();
      displayTimerSelectT1Edit();
      counter_blink = millis();
      blinker = 1;
    }
    if (millis() - counter_blink > 750 && blinker == 1)
    {
      lcd.clear();
      displayTimerSelect();
      counter_blink = millis();
      blinker = 0;
    }
  }

  if (state == 3 && btn_set == 2)
  {
    if (millis() - counter_blink > 750 && blinker == 0)
    {
      lcd.clear();
      displayTimerSelectT2Edit();
      counter_blink = millis();
      blinker = 1;
    }
    if (millis() - counter_blink > 750 && blinker == 1)
    {
      lcd.clear();
      displayTimerSelect();
      counter_blink = millis();
      blinker = 0;
    }
  }

  if (state == 3 && btn_set == 3)
  {
    if (millis() - counter_blink > 750 && blinker == 0)
    {
      lcd.clear();
      displayTimerSelectT3Edit();
      counter_blink = millis();
      blinker = 1;
    }
    if (millis() - counter_blink > 750 && blinker == 1)
    {
      lcd.clear();
      displayTimerSelect();
      counter_blink = millis();
      blinker = 0;
    }
  }

  if (state == 4 && btn_set == 0)
  { // state 4, set spray duration
    displayDurationSet();
  }

  if (state == 4 && btn_set == 1)
  {
    if (millis() - counter_blink > 750 && blinker == 0)
    {
      lcd.clear();
      displayDurationSetEdit();
      counter_blink = millis();
      blinker = 1;
    }
    if (millis() - counter_blink > 750 && blinker == 1)
    {
      lcd.clear();
      displayDurationSet();
      counter_blink = millis();
      blinker = 0;
    }
  }

  if (state == 5 && btn_set == 0)
  { // state 5, set backlight mode
    displayBacklightSettings();
  }

  if (state == 5 && btn_set == 1)
  {
    if (millis() - counter_blink > 750 && blinker == 0)
    {
      lcd.clear();
      displayBacklightSettingsEdit();
      counter_blink = millis();
      blinker = 1;
    }
    if (millis() - counter_blink > 750 && blinker == 1)
    {
      lcd.clear();
      displayBacklightSettings();
      counter_blink = millis();
      blinker = 0;
    }
  }

  if (state == 6 && btn_set == 0)
  { // state 6, set RTC time
    displayRTCset();
  }

  if (state == 6 && btn_set == 1)
  {
    if (millis() - counter_blink > 750 && blinker == 0)
    {
      lcd.clear();
      displayRTCsetHour();
      counter_blink = millis();
      blinker = 1;
    }
    if (millis() - counter_blink > 750 && blinker == 1)
    {
      lcd.clear();
      displayRTCset();
      counter_blink = millis();
      blinker = 0;
    }
  }

  if (state == 6 && btn_set == 2)
  {
    if (millis() - counter_blink > 750 && blinker == 0)
    {
      lcd.clear();
      displayRTCsetMinute();
      counter_blink = millis();
      blinker = 1;
    }
    if (millis() - counter_blink > 750 && blinker == 1)
    {
      lcd.clear();
      displayRTCset();
      counter_blink = millis();
      blinker = 0;
    }
  }

  if (state == 7 && btn_set == 0)
  { // state 7, reset factory setting
    displayFactoryReset();
  }
  if (state == 7 && btn_set == 1)
  {
    displayFactoryResetConfirm();
  }
}

void buttonMenu()
{
  if (btn_set == 0)
  {
    if (buttonRead(buttonUp) == true && state > 0)
    {
      state--;
      lcd.clear();
    }
    if (buttonRead(buttonDown) == true && state < 7)
    {
      state++;
      lcd.clear();
    }
    if (buttonRead(buttonSet) == true && state > 0)
    {
      btn_set = 1;
      lcd.clear();
    }
  }

  if (state == 1 && btn_set == 1)
  {
    if (buttonRead(buttonUp) == true)
    {
      temperature.threshold = temperature.threshold + 0.1;
    }
    if (buttonRead(buttonDown) == true)
    {
      temperature.threshold = temperature.threshold - 0.1;
    }
    if (buttonRead(buttonSet) == true)
    {
      EEPROM.put(0, temperature.threshold);
      EEPROM.commit();
      btn_set = 0;
    }
  }

  if (state == 2 && btn_set == 1)
  {
    if (buttonRead(buttonUp) == true)
    {
      if (timer1.hour < 23)
      {
        timer1.hour++;
      }
      else
      {
        timer1.hour = 0;
      }
    }
    if (buttonRead(buttonDown) == true)
    {
      if (timer1.hour > 0)
      {
        timer1.hour--;
      }
      else
      {
        timer1.hour = 23;
      }
    }
    if (buttonRead(buttonSet) == true)
    {
      btn_set = 2;
    }
  }

  if (state == 2 && btn_set == 2)
  {
    if (buttonRead(buttonUp) == true)
    {
      if (timer1.minute < 59)
      {
        timer1.minute++;
      }
      else if (timer1.hour < 23)
      {
        timer1.minute = 0;
        timer1.hour++;
      }
      else
      {
        timer1.minute = 0;
        timer1.hour = 0;
      }
    }
    if (buttonRead(buttonDown) == true)
    {
      if (timer1.minute > 0)
      {
        timer1.minute--;
      }
      else if (timer1.hour > 0)
      {
        timer1.minute = 59;
        timer1.hour--;
      }
      else
      {
        timer1.minute = 59;
        timer1.hour = 23;
      }
    }
    if (buttonRead(buttonSet) == true)
    {
      btn_set = 3;
    }
  }

  if (state == 2 && btn_set == 3)
  {
    if (buttonRead(buttonUp) == true)
    {
      if (timer2.hour < 23)
      {
        timer2.hour++;
      }
      else
      {
        timer2.hour = 0;
      }
    }
    if (buttonRead(buttonDown) == true)
    {
      if (timer2.hour > 0)
      {
        timer2.hour--;
      }
      else
      {
        timer2.hour = 23;
      }
    }
    if (buttonRead(buttonSet) == true)
    {
      btn_set = 4;
    }
  }

  if (state == 2 && btn_set == 4)
  {
    if (buttonRead(buttonUp) == true)
    {
      if (timer2.minute < 59)
      {
        timer2.minute++;
      }
      else if (timer2.hour < 23)
      {
        timer2.minute = 0;
        timer2.hour++;
      }
      else
      {
        timer2.minute = 0;
        timer2.hour = 0;
      }
    }
    if (buttonRead(buttonDown) == true)
    {
      if (timer2.minute > 0)
      {
        timer2.minute--;
      }
      else if (timer2.hour > 0)
      {
        timer2.minute = 59;
        timer2.hour--;
      }
      else
      {
        timer2.minute = 59;
        timer2.hour = 23;
      }
    }
    if (buttonRead(buttonSet) == true)
    {
      btn_set = 5;
    }
  }

  if (state == 2 && btn_set == 5)
  {
    if (buttonRead(buttonUp) == true)
    {
      if (timer3.hour < 23)
      {
        timer3.hour++;
      }
      else
      {
        timer3.hour = 0;
      }
    }
    if (buttonRead(buttonDown) == true)
    {
      if (timer3.hour > 0)
      {
        timer3.hour--;
      }
      else
      {
        timer3.hour = 23;
      }
    }
    if (buttonRead(buttonSet) == true)
    {
      btn_set = 6;
    }
  }

  if (state == 2 && btn_set == 6)
  {
    if (buttonRead(buttonUp) == true)
    {
      if (timer3.minute < 59)
      {
        timer3.minute++;
      }
      else if (timer3.hour < 23)
      {
        timer3.minute = 0;
        timer3.hour++;
      }
      else
      {
        timer3.minute = 0;
        timer3.hour = 0;
      }
    }
    if (buttonRead(buttonDown) == true)
    {
      if (timer3.minute > 0)
      {
        timer3.minute--;
      }
      else if (timer3.hour > 0)
      {
        timer3.minute = 59;
        timer3.hour--;
      }
      else
      {
        timer3.minute = 59;
        timer3.hour = 23;
      }
    }
    if (buttonRead(buttonSet) == true)
    {
      EEPROM.put(6, timer1.hour);
      EEPROM.put(7, timer1.minute);
      EEPROM.put(9, timer2.hour);
      EEPROM.put(10, timer2.minute);
      EEPROM.put(12, timer3.hour);
      EEPROM.put(13, timer3.minute);
      EEPROM.commit();
      btn_set = 0;
    }
  }

  if (state == 3 && btn_set == 1)
  {
    if (buttonRead(buttonUp) == true)
    {
      timer1.setting = 1;
    }
    if (buttonRead(buttonDown) == true)
    {
      timer1.setting = 0;
    }
    if (buttonRead(buttonSet) == true)
    {
      btn_set = 2;
    }
  }

  if (state == 3 && btn_set == 2)
  {
    if (buttonRead(buttonUp) == true)
    {
      timer2.setting = 1;
    }
    if (buttonRead(buttonDown) == true)
    {
      timer2.setting = 0;
    }
    if (buttonRead(buttonSet) == true)
    {
      btn_set = 3;
    }
  }

  if (state == 3 && btn_set == 3)
  {
    if (buttonRead(buttonUp) == true)
    {
      timer3.setting = 1;
    }
    if (buttonRead(buttonDown) == true)
    {
      timer3.setting = 0;
    }
    if (buttonRead(buttonSet) == true)
    {
      EEPROM.put(8, timer1.setting);
      EEPROM.put(11, timer2.setting);
      EEPROM.put(14, timer3.setting);
      EEPROM.commit();
      btn_set = 0;
    }
  }

  if (state == 4 && btn_set == 1)
  {
    if (buttonRead(buttonUp) == true)
    {
      if (deviceSet.duration < 60)
      {
        deviceSet.duration++;
      }
      else
      {
        deviceSet.duration = 1;
      }
    }
    if (buttonRead(buttonDown) == true)
    {
      if (deviceSet.duration > 1)
      {
        deviceSet.duration--;
      }
      else
      {
        deviceSet.duration = 60;
      }
    }
    if (buttonRead(buttonSet) == true)
    {
      EEPROM.put(5, deviceSet.duration);
      EEPROM.commit();
      btn_set = 0;
    }
  }

  if (state == 5 && btn_set == 1)
  {
    if (buttonRead(buttonUp) == true)
    {
      if (deviceSet.backlight > 0)
      {
        deviceSet.backlight--;
      }
      else
      {
        deviceSet.backlight = 0;
      }
    }
    if (buttonRead(buttonDown) == true)
    {
      if (deviceSet.backlight < 4)
      {
        deviceSet.backlight++;
      }
      else
      {
        deviceSet.backlight = 4;
      }
    }
    if (buttonRead(buttonSet) == true)
    {
      EEPROM.put(4, deviceSet.backlight);
      EEPROM.commit();
      btn_set = 0;
    }
  }

  if (state == 6 && btn_set == 1)
  {
    if (buttonRead(buttonUp) == true)
    {
      if (RTC.hour < 23)
      {
        RTC.hour++;
      }
      else
      {
        RTC.hour = 0;
      }
    }
    if (buttonRead(buttonDown) == true)
    {
      if (RTC.hour > 0)
      {
        RTC.hour--;
      }
      else
      {
        RTC.hour = 23;
      }
    }
    if (buttonRead(buttonSet) == true)
    {
      btn_set = 2;
    }
  }

  if (state == 6 && btn_set == 2)
  {
    if (buttonRead(buttonUp) == true)
    {
      if (RTC.minute < 59)
      {
        RTC.minute++;
      }
      else if (RTC.hour < 23)
      {
        RTC.minute = 0;
        RTC.hour++;
      }
      else
      {
        RTC.minute = 0;
        RTC.hour = 0;
      }
    }
    if (buttonRead(buttonDown) == true)
    {
      if (RTC.minute > 0)
      {
        RTC.minute--;
      }
      else if (RTC.hour > 0)
      {
        RTC.minute = 59;
        RTC.hour--;
      }
      else
      {
        RTC.minute = 59;
        RTC.hour = 23;
      }
    }
    if (buttonRead(buttonSet) == true)
    {
      setDS3231time(00, RTC.minute, RTC.hour, 7, 01, 10, 22);
      btn_set = 0;
    }
  }

  if (state == 7 && btn_set == 1)
  {
    if (buttonRead(buttonUp) == true || buttonRead(buttonDown) == true)
    {
      lcd.clear();
      btn_set = 0;
    }
    if (buttonRead(buttonSet) == true)
    {
      factoryReset();
      lcd.clear();
      lcd.setCursor(4, 0);
      lcd.print("Success!");
      delay(2000);
      lcd.clear();
      btn_set = 0;
      state = 0;
    }
  }
}

// Web function ----------------------------------------------

String statusTimer(byte status)
{
  if (status == 1)
  {
    return "On";
  }
  else
    return "Off";
}

String concatTime(byte hour, byte minute)
{
  String hr, mnt, conc;
  if (hour < 10)
  {
    hr = "0" + String(hour);
  }
  else
  {
    hr = String(hour);
  }
  if (minute < 10)
  {
    mnt = "0" + String(minute);
  }
  else
  {
    mnt = String(minute);
  }
  conc = hr + ":" + mnt;
  return conc;
}

void setupServer()
{
  webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
               { request->send(LittleFS, "/index.html", "text/html", false); });

  webServer.serveStatic("/", LittleFS, "/").setCacheControl("max-age=31536000"); // 365 days

  webServer.on("/temp", HTTP_GET, [](AsyncWebServerRequest *request)
               { request->send_P(200, "text/plain", String(temperature.celcius).c_str()); });

  webServer.on("/thresh", HTTP_GET, [](AsyncWebServerRequest *request)
               { request->send_P(200, "text/plain", String(temperature.threshold).c_str()); });

  webServer.on("/time", HTTP_GET, [](AsyncWebServerRequest *request)
               { request->send_P(200, "text/plain", concatTime(RTC.hour, RTC.minute).c_str()); });

  webServer.on("/timer1", HTTP_GET, [](AsyncWebServerRequest *request)
               { request->send_P(200, "text/plain", concatTime(timer1.hour, timer1.minute).c_str()); });

  webServer.on("/timer1status", HTTP_GET, [](AsyncWebServerRequest *request)
               { request->send_P(200, "text/plain", statusTimer(timer1.setting).c_str()); });

  webServer.on("/timer2", HTTP_GET, [](AsyncWebServerRequest *request)
               { request->send_P(200, "text/plain", concatTime(timer2.hour, timer2.minute).c_str()); });

  webServer.on("/timer2status", HTTP_GET, [](AsyncWebServerRequest *request)
               { request->send_P(200, "text/plain", statusTimer(timer2.setting).c_str()); });

  webServer.on("/timer3", HTTP_GET, [](AsyncWebServerRequest *request)
               { request->send_P(200, "text/plain", concatTime(timer3.hour, timer3.minute).c_str()); });

  webServer.on("/timer3status", HTTP_GET, [](AsyncWebServerRequest *request)
               { request->send_P(200, "text/plain", statusTimer(timer3.setting).c_str()); });

  webServer.on("/duration", HTTP_GET, [](AsyncWebServerRequest *request)
               { request->send_P(200, "text/plain", String(deviceSet.duration).c_str()); });

  webServer.on("/wifi", HTTP_POST, [](AsyncWebServerRequest *request)
               {
    int params = request->params();
    for (int i=0; i<params; i++){
      AsyncWebParameter* p = request->getParam(i);
      if(p->isPost()){
        if (p->name() == "ssid") {
          strcpy(deviceSet.ssid, p->value().c_str());
          len = strlen(deviceSet.ssid);
          EEPROM.put(15, len);
          for(int i=0; i<len; i++){
            EEPROM.put(16 + i, deviceSet.ssid[i]);
          }
          }
        if (p->name() == "pass") {
          strcpy(deviceSet.pass, p->value().c_str());
          len = strlen(deviceSet.pass);
          EEPROM.put(48, len);
          for(int i=0; i<len; i++){
            EEPROM.put(49 + i, deviceSet.pass[i]);
          }
          }
        EEPROM.commit();
        request->send(200, "text/html", "<p>Data diterima, alat akan restart.</p><p>Mohon tunggu beberapa saat, kemudian hubungkan kembali ke alat.</p>");
        restart = true;
      }
    } });

  webServer.on("/settings", HTTP_POST, [](AsyncWebServerRequest *request)
               {
    int params = request->params();
    for (int i=0; i<params; i++){
      AsyncWebParameter* p = request->getParam(i);
      if(p->isPost()){
        if (p->name() == "TempThresh") {
          String temp = p->value();
          temp.replace(",", ".");
          temperature.threshold = temp.toFloat();
          EEPROM.put(0, temperature.threshold);
          }
        if (p->name() == "timeT1") {
          String temp1 = p->value();
          String temp2 = temp1;
          temp1 = temp1.substring(0,2);
          temp2 = temp2.substring(3,5);
          timer1.hour = byte(temp1.toInt());
          timer1.minute = byte(temp2.toInt());
          EEPROM.put(6, timer1.hour);
          EEPROM.put(7, timer1.minute);
          }
        if (p->name() == "statusT1") {
          if (p->value() == "on"){
            timer1.setting = 1;
          }
          else if (p->value() == "off"){
            timer1.setting = 0;
          }
          EEPROM.put(8, timer1.setting);
          }
        if (p->name() == "timeT2") {
          String temp1 = p->value();
          String temp2 = temp1;
          temp1 = temp1.substring(0,2);
          temp2 = temp2.substring(3,5);
          timer2.hour = byte(temp1.toInt());
          timer2.minute = byte(temp2.toInt());
          EEPROM.put(9, timer2.hour);
          EEPROM.put(10, timer2.minute);
          }
          if (p->name() == "statusT2") {
          if (p->value() == "on"){
            timer2.setting = 1;
          }
          else if (p->value() == "off"){
            timer2.setting = 0;
          }
          EEPROM.put(11, timer2.setting);
          }
        if (p->name() == "timeT3") {
          String temp1 = p->value();
          String temp2 = temp1;
          temp1 = temp1.substring(0,2);
          temp2 = temp2.substring(3,5);
          timer3.hour = byte(temp1.toInt());
          timer3.minute = byte(temp2.toInt());
          EEPROM.put(12, timer3.hour);
          EEPROM.put(13, timer3.minute);
          }
          if (p->name() == "statusT3") {
          if (p->value() == "on"){
            timer3.setting = 1;
          }
          else if (p->value() == "off"){
            timer3.setting = 0;
          }
          EEPROM.put(14, timer3.setting);
          }
        if (p->name() == "duration") {
          String temp = p->value();
          deviceSet.duration = byte(temp.toInt());
          EEPROM.put(5, deviceSet.duration);
          }
        EEPROM.commit();
        request->send(200, "text/html", "<p>Data telah diterima dan disimpan, untuk kembali ke laman utama klik <a href=\"http://192.168.1.1\">disini</a>.</p>");
      }
    } });

  webServer.on("/RTC", HTTP_POST, [](AsyncWebServerRequest *request)
               {
    int params = request->params();
    for (int i=0; i<params; i++){
      AsyncWebParameter* p = request->getParam(i);
      if(p->isPost()){
        if (p->name() == "RTC") {
          String temp1 = p->value();
          String temp2 = temp1;
          temp1 = temp1.substring(0,2);
          temp2 = temp2.substring(3,5);
          RTC.hour = byte(temp1.toInt());
          RTC.minute = byte(temp2.toInt());
          setDS3231time(00, RTC.minute, RTC.hour, 7, 01, 10, 22);
          }
        request->send(200, "text/html", "<p>Data telah diterima dan disimpan, untuk kembali ke laman utama klik <a href=\"http://192.168.1.1\">disini</a>.</p>");
      }
    } });
}

// Main function ---------------------------------------------

void setup()
{
  LittleFS.begin();
  Serial.begin(9600);
  EEPROM.begin(EEPROM_SIZE);
  fetchEEPROM();
  Wire.begin(1);
  pinMode(buttonUp, INPUT_PULLUP);
  pinMode(buttonDown, INPUT_PULLUP);
  pinMode(buttonSet, INPUT_PULLUP);
  lcd.init();
  lcd.backlight();
  lcd.createChar(0, charDegree);
  lcd.createChar(1, charT1);
  lcd.createChar(2, charT2);
  lcd.createChar(3, charT3);
  lcd.setCursor(0, 0);
  lcd.print("Multitechnologi");
  delay(3000);
  lcd.clear();
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(APIP, APIP, subnet_mask);
  WiFi.softAP(deviceSet.ssid, deviceSet.pass);
  setupServer();
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  webServer.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER);
  webServer.begin();
}

void loop()
{
  receiveStatus();
  buttonMenu();
  displayMenu();
  backlightMode();
  sendSettings();
  dnsServer.processNextRequest();
  if (restart)
  {
    delay(5000);
    ESP.restart();
  }
  debugging();
}
