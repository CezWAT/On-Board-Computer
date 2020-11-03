/*  Simple on-board computer for simple old car
 *  v. 0.5.2
 *  Measures fuel economy based on two flow meters and hall sensor, shows it as bar and percentage (voltage divider needed)
 *  Shows voltage of the battery (voltage divider needed)
 *  Shows engine/oil temperature
 *  Shows current time based on DS3231
 *  Might show speed in future
 *  Might use buttons to control ODO meter, backlight, reset
 *  
 *  -- HARDWARE USED --
 *  board: Pro Micro by Sparkfun
 *  flow meters: 2x PC006377
 *  RTC: DS3231
 *  LCD: 2004A with additional I2C module
 *  buzzer: cheap NoName module with BJT
 *  
 *  -- PINS used --
 *  
 *  RAW - VCC
 *  TXI - hall sensor (distance meter, interrupt 3)
 *  RXI - flow meter input (interrupt 2)
 *  2 - I2C SCL (RTC, LCD)
 *  3 - I2C SDA (RTC, LCD)
 *  5 - buzzer output
 *  7 - flow meter input (interrupt 4)
 *  10 - OneWire bus for DS18B20
 *  A1 - fuel meter input
 *  A2 - voltage meter
 *  
 *  TODO
 *  0. Check wheel circumference
 *  1. Calibrate data from voltage dividers
 *  2. Calibrate data from fuel meters, distance meter and make fuel consumption show more real values
 */

#include <Wire.h>
#include <OneWire.h>
#include <LiquidCrystal_I2C.h> // manually installed from https://github.com/pkourany/LiquidCrystal_V1.2.1; changed ../Wire/Wire.h in I2CIO.cpp

LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE); //0xADDR
OneWire  ds(10); // on pin 10 (a 4.7K resistor to VCC is necessary)

char buzzDIS = 0; //is buzzer disabled
byte half[] = {0x1C, 0x18, 0x1C, 0x18, 0x1C, 0x18, 0x1C, 0x18}; //half rectangle on LCD
byte full[] = { 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F}; //full rectangle on LCD
byte endbar[] = { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}; //thin bar ending fuel meter

// --- variables to be defined by user ---
char fuelMax = 21; // in litres
char fuelAlarm = 4; // in litres
char buzzer = 5; //buzzer pin
unsigned short int distance = 500; // distance for fuel measurement in meters
float wheelCircum = 1.915; //wheel circumference in meters

//variables for time measurement
unsigned long time_now = 0;
unsigned long buzzer_time = 0;
unsigned long rtc_time = 0;
unsigned long fuel_time = 0;
unsigned long temper_time = 0;

//variables for fuel economy measurement
volatile unsigned short int countFlow1 = 0; //counter for flow interrupt
volatile unsigned short int countFlow2 = 0; //counter for flow interrupt
volatile unsigned short int countWheel = 0; //counter for distance interrupt
float copyFlow1 = 0; //backup for flow calculations
float copyFlow2 = 0; //backup for flow calculations
float copyWheel = 0; //backup for distance calculations
unsigned long lastRead = 0; //storage for last read
unsigned long interval = 5000; //interval for flow interrupts calculations

//function declaration
void rtc();
void fuel();
void temp();
void voltage();
void rpm();
void isrFlow1();
void isrFlow2();
void isrWheel();

void setup()
{
  // LCD
  lcd.begin(20,4); // initialize the lcd 
  lcd.backlight(); // turn backlight on
  //lcd.cursor(); // set cursor to be visible
  lcd.createChar(0, half);
  lcd.createChar(1, full);
  lcd.createChar(2, endbar);
  // BUZZER
  pinMode(buzzer, OUTPUT); //init
  digitalWrite(buzzer, HIGH);  

  //run the first time to fill LCD
  rtc();
  temp();
  voltage();
  fuel();
  rpm();
  
  attachInterrupt(4, isrFlow1, RISING); //interrupt signal to pin 7
  attachInterrupt(2, isrFlow2, RISING); //interrupt signal to pin 11 - RXI
  attachInterrupt(3, isrWheel, RISING); //interrupt signal to pin 12 - TXI
}

// ------------------------------- temperature read and print

void temp(){
  //char present = 0;
  byte addr[8];
  short int celsius;
  short int data[12];

  if (!ds.search(addr)) {
    ds.reset_search();
    delay(250);
  }
  
  if (OneWire::crc8(addr, 7) != addr[7]){
    return;
  }

  ds.reset();
  ds.select(addr);
  ds.write(0x44, 1);  // start conversion, with parasite power on at the end
  delay(1000);
  ds.reset();
  ds.select(addr);
  ds.write(0xBE);         // Read Scratchpad
  
  for (char i = 0; i < 9; i++) {           // we need 9 bytes
    data[i] = ds.read();
  }
  // Convert the data to actual temperature
  // because the result is a 16 bit signed integer, it should
  // be stored to an "int16_t" type, which is always 16 bits
  // even when compiled on a 32 bit processor.
  int16_t raw = (data[1] << 8) | data[0];
  raw = raw & ~1; // 11 bit res, 375 ms
  celsius = raw / 16;

  lcd.setCursor(0,0);
  lcd.print("      ");
  lcd.setCursor(0,0);
  lcd.print(celsius);
  lcd.print((char)223); //degree sign
  lcd.print("C");
  if (celsius < 100){
    lcd.print(" "); //prevent leaving "C" sign when temperature falls
  }
}

// ------------------------------- clock read and print

void rtc(){
  // send request to receive data starting at register 0
  Wire.beginTransmission(0x68); // 0x68 is DS3231 device address
  Wire.write((byte)0); // start at register 0
  Wire.endTransmission();
  Wire.requestFrom(0x68, 3); // request three bytes (seconds, minutes, hours)
  byte hours, minutes, seconds;
  while(Wire.available())
  { 
    seconds = Wire.read(); // get seconds; without this clock on LCD goes wild
    minutes = Wire.read(); // get minutes
    hours = Wire.read();   // get hours

    minutes = (((minutes & 0b11110000)>>4)*10 + (minutes & 0b00001111)); // convert BCD to decimal
    hours = (((hours & 0b00100000)>>5)*20 + ((hours & 0b00010000)>>4)*10 + (hours & 0b00001111)) + 1; // convert BCD to decimal (assume 24 hour mode); +1 to make it work in the summer
    lcd.setCursor(15,0);
    if (hours >= 10){
      lcd.print(hours);
    }
    else {
      lcd.print("0" + String(hours));
    }
    lcd.print(":");
    if (minutes < 10){
      lcd.print("0" + String(minutes));
    }
    else {
      lcd.print(minutes);
    }
  }
}

// ------------------------------- just make some noise using buzzer

void buzz(char &buzzDIS){ //strangely for loop stopped working so duplicated digitalwrite will have to do its work
    if (!buzzDIS){
      digitalWrite(buzzer, LOW);
      delay(500);
      digitalWrite(buzzer, HIGH);
      delay(250);
      digitalWrite(buzzer, LOW);
      delay(500);
      digitalWrite(buzzer, HIGH);
    }
    buzzDIS = 1;
}

// ------------------------------- fuel read, calc and print

void fuel(){
  short int analog = analogRead(A1); //0-1023
  int fuelPerc = (analog * (5.0 / 1023.0) * 300) / 5.0; //* 3 because of voltage divider
  char cleanup;

  if (fuelPerc >= 80){ // disable alarm if fuel % goes above 80% (from less)
    buzzDIS = 0;
  }
  else if (fuelPerc <= (100 * fuelAlarm/fuelMax)){ // alarm if not much fuel left
    buzz(buzzDIS); //buzzer enabled
  }
  
  // print progress bar
  lcd.setCursor(0,1);
  for (char i = 0; i <= 10; i++){ // loop for full bars
    if (i < fuelPerc/10 && fuelPerc > 5){
      lcd.write(byte(1));
    }
    else {
      break;
    }
  }

  if (fuelPerc % 10 >= 5){ // print half bar
      lcd.write(byte(0));
      cleanup = 1; // if half sign is printed, add 1 to starting point of cleanup
  }
  else {
    cleanup = 0;
  }
  
  for (byte i = (fuelPerc/10) + cleanup; i <= 10; i++) { // print clean bars starting from 
      lcd.setCursor(i, 1);
      lcd.print(" ");
    }
  
  lcd.setCursor(10,1);
  lcd.write(byte(2)); // print endbar
  
  // print percentage
  if (fuelPerc == 100){ // only equal 100%
    lcd.setCursor(16,1);
  }
  else if (fuelPerc <= 9){ // less than 9 - clear digits before at the lcd
    lcd.setCursor(16,1);
    lcd.print("   ");
    lcd.setCursor(18,1);
  }
  else { // between 9-100, clear first digit from 100%
    lcd.setCursor(16,1);
    lcd.print("  ");
    lcd.setCursor(17,1);
  }
  lcd.print(fuelPerc);
  lcd.print("%");
}

// ------------------------------- Voltage measurement

void voltage(){
  int analog = analogRead(A2); //0-1023
  float voltage = analog * (5.0 / 1023.0) * 30; // *10 to have 1 decimal place; *3 because of voltage divider
  lcd.setCursor(7,0);
  lcd.print("     ");
  lcd.setCursor(7,0);
  lcd.print(abs((float)voltage / 10));
  lcd.print(" V");
}

// ------------------------------- Engine RPM measurement

void rpm(){
  int rpmPin = 6;
  int duration = pulseIn(rpmPin, HIGH);
  int RPM = 60000 / duration; //60 sec in minute * 1000 ms / duration; (1/T)/duration
  lcd.setCursor(15,2);
  lcd.print("     ");
  lcd.setCursor(15,2);
  lcd.print(RPM);
}

// ------------------------------- Fuel consumption measurement

void consumption(float copyFlow1, float copyFlow2, float copyWheel){
  float diff, coeff = 0; //difference between fuel in and out, coefficient for distance measurement
  copyFlow1 /= 450; //450 imp @ 1 L
  copyFlow2 /= 450; //450 imp @ 1 L
  diff = 100 * abs(copyFlow1 - copyFlow2); // -> = L
  copyWheel *= wheelCircum; // imp * wheel circumference in [m]          CHECK!
  coeff = 100000 / copyWheel; // coefficient for calculating part of 100 km driven
  float consumption = (diff / copyWheel) * coeff; // fuel consumption for 100 km in L/100 km
  if (consumption > 0){ // prevent printing negative values
    lcd.setCursor(0,2);
    lcd.print("                    "); // clean the line
    lcd.setCursor(0,2);
    lcd.print(consumption);
    lcd.print(" L/100 km");    
  }
  else {
    lcd.setCursor(0,2);
    lcd.print("                    "); // clean the line
    lcd.setCursor(0,2);
    lcd.print("0.00 L/100 km");   
  }
  if (copyWheel <= 0){ //if car is not moving
        lcd.setCursor(0,2);
        lcd.print("               ");
        consumption = (60 / interval) * diff;
        lcd.setCursor(0,2);
        lcd.print(consumption);
        lcd.print(" L/min");
    }
  lcd.setCursor(0,3); // ONLY FOR TESTING WITHOUT HALL SENSOR
  lcd.print("   ");
  lcd.setCursor(0,3);
  lcd.print(copyFlow1);
  lcd.setCursor(5,3);
  lcd.print("   ");
  lcd.setCursor(5,3);
  lcd.print(copyFlow2);
  lcd.setCursor(10,3);
  lcd.print("   ");
  lcd.setCursor(10,3);
  lcd.print(copyWheel);
  //lcd.print("  ");
}

// ------------------------------- main loop

void loop()
{
  time_now = millis();
  if (time_now - temper_time >= 1000){ // update temperature and voltage every 1 sec
    temper_time = time_now;
    temp();
    voltage();
    rpm();
  }
  if (time_now - rtc_time >= 15000){ // update clock and fuel level every 15 sec
    rtc_time = time_now;
    rtc();
    fuel();
  }
  //if (millis() - lastRead >= interval) // read interrupts and calculate fuel consumption
  if (countWheel * wheelCircum >= distance)
  {
    lastRead  += interval;
    noInterrupts(); // cli(); disable interrupts
    copyFlow1 = countFlow1;
    copyFlow2 = countFlow2;
    copyWheel = countWheel;
    countFlow1 = 0;
    countFlow2 = 0;
    countWheel = 0;
    interrupts(); // sei(); enable interrupts
    consumption(copyFlow1, copyFlow2, copyWheel); // run function calculating it all
  }
}

// ------------------------------- INTERRUPTS

void isrFlow1() // Flow sensor 1
{
  countFlow1++;
}

void isrFlow2() // Flow sensor 2
{
  countFlow2++;
}

void isrWheel() // Wheel sensor (distance sensor)
{
  countWheel++;
}
