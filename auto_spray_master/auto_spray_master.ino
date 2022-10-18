  #include <Wire.h>
  #include <LiquidCrystal_I2C.h>
  #include <EEPROM.h>

  // Declare variables here ---------------------------------------------------
  
  #define EEPROM_SIZE 32
  // temperature.threshold = float, address at 0-3
  // timer.duration = byte, address at 4
  // timer.hour = byte, address at 5
  // timer.minute = byte, address at 6

  // RTC Address 0x68
  #define RTC_ADDRESS 0x68

  // LCD address 0x27
  #define LCD_ADDRESS 0x27

  // ATMega address
  #define ATM_ADDRESS 0x08
  
  // EEPROM temp variable
  struct {
    float threshold, celcius;
  } temperature;
  
  struct {
    byte duration, hour, minute;
  } timer;

  struct {
    byte second, minute, hour, dayOfWeek, dayOfMonth, month, year;
  } RTC;

  union floatToBytes {
    char text[4];
    float value;
  } fl2b;

  // Global var.
  unsigned long counter1, counter2 = 0;

  int state, btn_set = 0;

  // Set button pin
  const int buttonUp = 14; // pin GPIO0 / D3
  const int buttonDown = 12; // pin GPIO3 / Rx (turn off Serial)
  const int buttonSet = 13; // pin GPIO2 / D4

  long lastDebounceTime = 0;
  long debounceDelay = 150;

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
	0b00000
  };

  int indx = 0;

  // I2C Comms -----------------------------------------------------------
  void sendSettings(){ // send per 5 sec
    if (millis() >= (counter1 + 5000)){
      Wire.beginTransmission(ATM_ADDRESS);
      fl2b.value = temperature.threshold;
      Wire.write(fl2b.text, 4);
      Wire.write(RTC.hour);
      Wire.write(RTC.minute);
      Wire.write(timer.duration);
      Wire.write(timer.hour);
      Wire.write(timer.minute);
      Wire.endTransmission();
      counter1 = millis();
    }
  }

  void receiveStatus(){ // receive per sec
    if (millis() >= (counter2 + 1000)){
      Wire.requestFrom(ATM_ADDRESS, 4);
      while (Wire.available()){
        fl2b.text[indx] = Wire.read();
        indx++;
      }
      indx = 0;
      temperature.celcius = fl2b.value;
      counter2 = millis();
    } 
  }

  // Utility function -----------------------------------------------------

  void clear_memory(){
    for (int i = 0; i <32; i++){
      EEPROM.put(i, 0);
    }
    EEPROM.commit();
    delay (500);
  }

  bool buttonRead(int pin){
    if ((millis() - lastDebounceTime) > debounceDelay){
      if (digitalRead(pin) == LOW){
        lastDebounceTime = millis();
        return true;
      }
      else return false;
    }
    else return false;
  }

  // Convert normal decimal numbers to binary coded decimal
  byte decToBcd(byte val){
  return( (val/10*16) + (val%10) );
  }
  // Convert binary coded decimal to normal decimal numbers
  byte bcdToDec(byte val){
  return( (val/16*10) + (val%16) );
  }
  // Set RTC function
  void setDS3231time(byte second, byte minute, byte hour, byte dayOfWeek, byte dayOfMonth, byte month, byte year){
  // sets time and date data to DS3231
  Wire.beginTransmission(RTC_ADDRESS);
  Wire.write(0); // set next input to start at the seconds register
  Wire.write(decToBcd(second)); // set seconds
  Wire.write(decToBcd(minute)); // set minutes
  Wire.write(decToBcd(hour)); // set hours
  Wire.write(decToBcd(dayOfWeek)); // set day of week (1=Sunday, 7=Saturday)
  Wire.write(decToBcd(dayOfMonth)); // set date (1 to 31)
  Wire.write(decToBcd(month)); // set month
  Wire.write(decToBcd(year)); // set year (0 to 99)
  Wire.endTransmission();
  }
  // Read from RTC function
  void readDS3231time(byte *second,
    byte *minute,
    byte *hour,
    byte *dayOfWeek,
    byte *dayOfMonth,
    byte *month,
    byte *year){
      Wire.beginTransmission(RTC_ADDRESS);
      Wire.write(0); // set DS3231 register pointer to 00h
      Wire.endTransmission();
      Wire.requestFrom(RTC_ADDRESS, 7);
      // request seven bytes of data from DS3231 starting from register 00h
      *second = bcdToDec(Wire.read() & 0x7f);
      *minute = bcdToDec(Wire.read());
      *hour = bcdToDec(Wire.read() & 0x3f);
      *dayOfWeek = bcdToDec(Wire.read());
      *dayOfMonth = bcdToDec(Wire.read());
      *month = bcdToDec(Wire.read());
      *year = bcdToDec(Wire.read());
    }

  // Menu function ----------------------------------------------------------------

  void displayMain(){
    readDS3231time(&RTC.second, &RTC.minute, &RTC.hour, &RTC.dayOfWeek, &RTC.dayOfMonth, &RTC.month, &RTC.year);
    lcd.setCursor(0, 0);
    lcd.print("Temp:");
    lcd.setCursor(6, 0);
    lcd.print(temperature.celcius);
    lcd.setCursor(11, 0);
    lcd.write(0);
    lcd.setCursor(12, 0);
    lcd.print("C");
    lcd.setCursor(0, 1);
    lcd.print("Time:");
    lcd.setCursor(6, 1);
    if (RTC.hour < 10){
      lcd.print("0");
      lcd.setCursor(7, 1);
      lcd.print(RTC.hour);
    }
    else {
      lcd.print(RTC.hour);
    }
    lcd.setCursor(8, 1);
    lcd.print(":");
    lcd.setCursor(9, 1);
    if (RTC.minute < 10){
      lcd.print("0");
      lcd.setCursor(10, 1);
      lcd.print(RTC.minute);
      }
    else {
      lcd.print(RTC.minute);
    }
  }

  void displayTempSet(){
    lcd.setCursor(0, 0);
    lcd.print("Temp Set");
    lcd.setCursor(0, 1);
    lcd.print(temperature.threshold);
    lcd.setCursor(4, 1);
    lcd.write(0);
    lcd.setCursor(5, 1);
    lcd.print("C");
  }

  void displayTimeSet(){
    lcd.setCursor(0,0);
    lcd.print("Timer Set");
    lcd.setCursor(0,1);
    if (timer.hour < 10){
      lcd.print("0");
      lcd.setCursor(1,1);
      lcd.print(timer.hour);
    }
    else{
      lcd.print(timer.hour); 
    } 
    lcd.setCursor(2, 1);
    lcd.print(":");
    lcd.setCursor(3, 1);
    if (timer.minute < 10){
      lcd.print("0");
      lcd.setCursor(4, 1);
      lcd.print(timer.minute);
      }
    else {
      lcd.print(timer.minute);
    }   
  }

  void displayRTCset(){
    readDS3231time(&RTC.second, &RTC.minute, &RTC.hour, &RTC.dayOfWeek, &RTC.dayOfMonth, &RTC.month, &RTC.year);
    lcd.setCursor(0, 0);
    lcd.print("RTC Set");
    lcd.setCursor(0, 1);
    if (RTC.hour < 10){
      lcd.print("0");
      lcd.setCursor(1, 1);
      lcd.print(RTC.hour);
    }
    else {
      lcd.print(RTC.hour);
    }
    lcd.setCursor(2, 1);
    lcd.print(":");
    lcd.setCursor(3, 1);
    if (RTC.minute < 10){
      lcd.print("0");
      lcd.setCursor(4, 1);
      lcd.print(RTC.minute);
      }
    else {
      lcd.print(RTC.minute);
    }
  }

  // Main function ---------------------------------------------

  void displayMenu(){
    if (state == 0 && btn_set == 0){
      displayMain();
    }
    if (state == 1 && btn_set == 0){
      displayTempSet();
      lcd.setCursor(15, 1);
      lcd.print(" ");
    }
    if (state == 1 && btn_set == 1){
      displayTempSet();
      lcd.setCursor(15, 1);
      lcd.print("<");
    }
    if (state == 1 && btn_set == 2){
      displayTempSet();
      lcd.setCursor(15, 1);
      lcd.print(">");
    }
    if (state == 2 && btn_set == 0){
      displayTimeSet();
      lcd.setCursor(15, 1);
      lcd.print(" ");
    }
    if (state == 2 && btn_set == 1){
      displayTimeSet();
      lcd.setCursor(15, 1);
      lcd.print("<");
    }

    if (state == 3 && btn_set == 0){
      displayRTCset();
      lcd.setCursor(15, 1);
      lcd.print(" ");
    }

    if (state == 3 && btn_set == 1){
      displayRTCset();
      lcd.setCursor(15, 1);
      lcd.print("<");
    }  
  }

  void buttonMenu(){
    if (buttonRead(buttonUp) == true && btn_set == 0 && state > 0){
      state--;
      lcd.clear();
    }
    if (buttonRead(buttonDown) == true && btn_set == 0 && state < 3){
      state++;
      lcd.clear();
    }
    if (buttonRead(buttonSet) == true && btn_set == 0 && state > 0){
      btn_set = 1;
      lcd.clear();
    }
  }

  void buttonMenuSet(){
    if (state == 1 && btn_set == 1){
      if (buttonRead(buttonUp) == true){
        temperature.threshold = temperature.threshold + 0.05;
      }
      if (buttonRead(buttonDown) == true){
        temperature.threshold = temperature.threshold - 0.05;
      }
      if (buttonRead(buttonSet) == true){
        // EEPROM.put(0, temperature.threshold);
        // EEPROM.commit();
        btn_set = 0;
      }
    }
    if (state == 2 && btn_set == 1){
      if (buttonRead(buttonUp) == true){
        if (timer.hour <23){
          timer.hour++;
        }
        else {
          timer.hour = 0;
        }
      }
      if (buttonRead(buttonDown) == true){
        if (timer.hour > 0){
          timer.hour--;
        }
        else {
          timer.hour = 23;
        }
      }
      if (buttonRead(buttonSet) == true){
        btn_set = 2;
      }
    }
    if (state == 2 && btn_set == 2){
      if (buttonRead(buttonUp) == true){
        if (timer.minute < 59){
          timer.minute++;
        } 
        else if (timer.hour < 23){
          timer.minute = 0;
          timer.hour++;
        }
        else{
          timer.minute = 0;
          timer.hour = 0;
        }
      }
      if (buttonRead(buttonDown) == true){
        if (timer.minute > 0){
          timer.minute--;
        } 
        else if (timer.hour > 0){
          timer.minute = 59;
          timer.hour--;
        }
        else{
          timer.minute = 59;
          timer.hour = 23;
        }
      }
      if (buttonRead(buttonSet) == true){
        // EEPROM.put(5, Jam);
        // EEPROM.put(6, Menit);
        // EEPROM.commit();
        btn_set = 0;
      }
    }

    if (state == 3 && btn_set == 1){
      if (buttonRead(buttonUp) == true){
        temperature.threshold = temperature.threshold + 0.1;
      }
      if (buttonRead(buttonDown) == true){
        temperature.threshold = temperature.threshold - 0.1;
      }
      if (buttonRead(buttonSet) == true){
        // EEPROM.put(5, Jam);
        // EEPROM.commit();
        btn_set = 0;
      }
    }
  }

  void setup() {
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.get(0, temperature.threshold);
    EEPROM.get(4, timer.duration);
    EEPROM.get(5, timer.hour);
    EEPROM.get(6, timer.minute);
    
    Wire.begin(1);
    pinMode(buttonUp, INPUT_PULLUP);
    pinMode(buttonDown, INPUT_PULLUP);
    pinMode(buttonSet, INPUT_PULLUP);
    
    lcd.init();
    lcd.backlight();
    lcd.createChar(0, charDegree);
    lcd.setCursor(5, 0);
    lcd.print("MTech");
    delay(3000);
    lcd.clear();
    
    //Serial.begin(115200);
    //Setup one time only and debugging below
    //setDS3231time(00,30,15,2,17,10,22);
    readDS3231time(&RTC.second, &RTC.minute, &RTC.hour, &RTC.dayOfWeek, &RTC.dayOfMonth, &RTC.month, &RTC.year);
    temperature.threshold = 32.5;
    timer.duration = 1;
    timer.hour = RTC.hour;
    timer.minute = RTC.minute + 1 ;
    
  }

  void loop() {
    receiveStatus();
    if (btn_set == 0){
      buttonMenu();
    }
    else if (btn_set == 1){
      buttonMenuSet();
    }
    displayMenu();
    sendSettings();

  }
