/*
git clone https://github.com/adafruit/Adafruit_Sensor.git
git clone https://github.com/adafruit/DHT-sensor-library.git
git clone https://github.com/z3t0/Arduino-IRremote.git
git clone https://github.com/adafruit/Adafruit_NeoPixel.git
git clone https://github.com/adafruit/RTClib.git

*/
#include <EEPROM.h>
#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
  #include <avr/power.h>
#endif
#include <IRremote.h>
#include <Wire.h>
#include "RTClib.h"

#include <DHT.h>

#define _DEBUG_

#define LED_PIN1    3
#define LED_PIN2    4
#define DHT22_PIN   5
#define IR_PIN      8
#define PUMP_PIN    9
#define FAN_PIN     10
#define HEATER_PIN  11
//SDA A4
//SDL A5

#define DHTTYPE     DHT22 //DHT 22 (AM2302)
#define SUN_RICE_HOUR       8
#define SUN_SET_HOUR        23
#define MSEC_IN_SEC         1000
#define MSEC_DIFF_ON_CYCLE  60000
#define RAIN_LIGHT_MIN      8
#define FAN_LATENT_MIN      5

#define S_S               9
#define S_MIN             10
#define S_MAX             17
#define S_COL_R           0
#define S_COL_G           1
#define S_COL_B           2
#define S_MOTOR_ON_SEC    3
#define S_MOTOR_OFF_MIN   4
#define S_RAIN_LENGTH_MIN 5
#define S_HUMIDITY_PERC   6

//non-save settings
#define SN_COUNT 1
#define SN_RAIN_HOUR 7

Adafruit_NeoPixel dleds1 = Adafruit_NeoPixel(8, LED_PIN1, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel dleds2 = Adafruit_NeoPixel(8, LED_PIN2, NEO_GRB + NEO_KHZ800);
IRrecv irrecv(IR_PIN);
decode_results irrecvResult;
RTC_DS1307 RTC;
DateTime currentTime;
DHT dht(DHT22_PIN, DHTTYPE);

volatile uint8_t settings[(S_MAX - S_MIN) + SN_COUNT];
volatile uint8_t settingsPos = 0;

volatile uint8_t colors[3];
volatile uint8_t lamps[3] = {0,0,0};
volatile bool isLampMode = false;

volatile uint16_t pumpOnSec;
volatile uint8_t pumpOffMin;
volatile uint8_t fanOnMin = FAN_LATENT_MIN - 1;
volatile uint8_t heatOnMin = 0;
volatile bool isSettingsShowing = false;
volatile uint8_t isNight = 0;
volatile uint8_t isRain = 0;
volatile float humidity = 0;
volatile float temperature = 0;

volatile unsigned long _irValue;
volatile unsigned long _msek = 0;
volatile uint8_t _min = 0;
volatile uint8_t _isDirty = 0;

const uint8_t defSettings[S_MAX - S_MIN] = {208,144,48,10,15,2,80};

void setup() {
  if (EEPROM.read(S_S) != S_MAX) {
    EEPROM.write(S_S, S_MAX);
    for (uint8_t i = S_MIN; i < S_MAX; i++) {
      EEPROM.write(i, defSettings[i - S_MIN]);
    }
  }
  pinMode(PUMP_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  pinMode(HEATER_PIN, OUTPUT);
  for (uint8_t i = S_MIN; i < S_MAX; i++) { 
    settings[i - S_MIN] = EEPROM.read(i);
  }
  setColors();
  pumpOffMin = settings[S_MOTOR_OFF_MIN];
  // start serial port at 9600 bps:
#ifdef _DEBUG_
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }
#endif
  irrecv.enableIRIn(); // Start the receiver
  Wire.begin();
  RTC.begin();
  dht.begin();
  if (!RTC.isrunning()) {
#ifdef _DEBUG_
    Serial.println("RTC is NOT running!");
#endif
    // This will reflect the time that your sketch was compiled
  }
  RTC.adjust(DateTime(__DATE__, __TIME__));

  dleds1.begin();
  dleds2.begin();
  dleds1.show();
  dleds2.show();
  lightOn();
  currentTime = RTC.now();
  settings[SN_RAIN_HOUR] = currentTime.secondstime() % (SUN_SET_HOUR - SUN_RICE_HOUR) + SUN_RICE_HOUR;
  delay(1000);
}

void loop() {
  // put your main code here, to run repeatedly:
  if (irrecv.decode(&irrecvResult)) {
    onIrButtonPressed(irrecvResult.value);
#ifdef _DEBUG_
    Serial.println(irrecvResult.value, HEX);
    const char sNames[] = {'R','G','B','M','m','r','H','h'};
    for (uint8_t i = 0; i < ((S_MAX - S_MIN) + SN_COUNT); i++) { 
      Serial.print(sNames[i]);
      Serial.print(settings[i]);
      Serial.print(" ");
    }
    currentTime = RTC.now();
    Serial.print(currentTime.month(), DEC);
    Serial.print('/');
    Serial.print(currentTime.day(), DEC);
    Serial.print('/');
    Serial.print(currentTime.year(), DEC);
    Serial.print(' ');
    Serial.print(currentTime.hour(), DEC);
    Serial.print(':');
    Serial.print(currentTime.minute(), DEC);
    Serial.print(':');
    Serial.print(currentTime.second(), DEC);
    Serial.print(' ');
    Serial.print(humidity);
    Serial.print("% ");
    Serial.print(temperature);
    Serial.println("*C");
#endif
    irrecv.resume(); // Receive the next value
  }
  //if (Serial.available() > 0) {
    // get incoming byte:
    //uint8_t c = Serial.read();
  //}
  //---------------TIME CYCLE-----------------
  unsigned long _msek_now = millis();
  if (_msek_now < MSEC_DIFF_ON_CYCLE && _msek > MSEC_DIFF_ON_CYCLE) {
     _msek = 0;//start millis cycle!
  }
  if (_msek <= _msek_now) {
    _msek = _msek_now - (_msek_now % MSEC_IN_SEC) + MSEC_IN_SEC;
    currentTime = RTC.now();
#ifdef _DEBUG_
    Serial.print(".");
#endif
    onSecond();
    uint8_t _min_now = currentTime.minute();
    if (_min != _min_now) {
      _min = _min_now;
#ifdef _DEBUG_
      Serial.print("minute:");
      Serial.println(currentTime.minute());
#endif
      onMinute();
      if (_min_now == 0) {
#ifdef _DEBUG_
        Serial.print("hour:");
        Serial.println(currentTime.hour());
#endif
        onHour();
        if (currentTime.hour() == 0) {
          onDay();
        }
      }
    }
  }
}

//-----------------------EVENTS----------------------------
void onIrButtonPressed(unsigned long irValue) {
  switch (irValue) {
    case 0xFF6897://1
      settingsPos = S_COL_R;
      break;
    case 0xFF9867://2
      settingsPos = S_COL_G;
      break;
    case 0xFFB04F://3
      settingsPos = S_COL_B;
      break;
    case 0xFF30CF://4
      settingsPos = S_MOTOR_ON_SEC;
      break;
    case 0xFF18E7://5
      settingsPos = S_MOTOR_OFF_MIN;
      break;
    case 0xFF7A85://6
      settingsPos = S_RAIN_LENGTH_MIN;
      break;
    case 0xFF10EF://7
      settingsPos = S_HUMIDITY_PERC;
      break;
    case 0xFF38C7://8
      settingsPos = SN_RAIN_HOUR;
      break;
    case 0xFF5AA5://9
      break;
    case 0xFF4AB5://0
      isLampMode = !isLampMode;
      _isDirty = 0;
      break;
    case 0xFF629D://up
      if (isLampMode) {
        if (settingsPos < sizeof(colors)) {
          lamps[settingsPos] += settingsStep();
        }
      } else {
        settings[settingsPos] += settingsStep();
        _isDirty = 1;
      }
      break;
    case 0xFFA857://down
      if (isLampMode) {
        if (settingsPos < sizeof(colors)) {
          lamps[settingsPos] -= settingsStep();
        }
      } else {
        settings[settingsPos] -= settingsStep();
        _isDirty = 1;
      }
      break;
    case 0xFF22DD://left
      onNightStart();
      break;
    case 0xFFC23D://right
      onDayStart();
      break;
    case 0xFF02FD://Ok
      isSettingsShowing = !isSettingsShowing;
      saveSettings();
      break;
    case 0xFF52AD://#
      pumpStart();
      break;      
    case 0xFF42BD://*
      if (isRain == 0) {
        rainStart();
      } else {
        rainStop();
      }
      break;
    case 0xFFFFFFFF://repeat
      onIrButtonPressed(_irValue);
      break;
    default:
      break;
  }
  if (irValue != 0xFFFFFFFF) {//repeat
    _irValue = irValue;
  }
  if (_isDirty) {
    setColors();
  }
  lightOn();
}

void onSecond() {
  checkLightDiv();
  if (pumpOnSec > 0) {
    pumpOnSec--;
    if (pumpOnSec == 0) {
      pumpStop();
    }
#ifdef _DEBUG_
    Serial.print("pump:");
    Serial.println(pumpOnSec);
#endif
  }
  if ((currentTime.second() % 2) == 0) {
    humidity = dht.readHumidity();
    temperature = dht.readTemperature();
  }
}

void onMinute() {
  saveSettings();
  pumpOffMin--;
  if (pumpOffMin == 0) {
    pumpStart();
  }
  if (currentTime.hour() == SUN_RICE_HOUR && currentTime.minute() < 15 && (isNight != 0)) {
    onDayStart(); 
  }
  if (currentTime.hour() == SUN_SET_HOUR && currentTime.minute() < 15 && (isNight == 0)) {
    onNightStart();
  }
  if (humidity > settings[S_HUMIDITY_PERC]) {
    if (fanOnMin < FAN_LATENT_MIN) {
      fanOnMin++;
      if (fanOnMin == FAN_LATENT_MIN) {
        powerOn(FAN_PIN);
      }
    }
  } else {
    if (fanOnMin > 0) {
      fanOnMin--;
      if (fanOnMin == 0) {
        powerOff(FAN_PIN);
      }
    }
  }
}

void onHour() {
  if (currentTime.hour() == settings[SN_RAIN_HOUR]) {
    rainStart();
  }
}

void onDay() {
  settings[SN_RAIN_HOUR] = millis() % (SUN_SET_HOUR - SUN_RICE_HOUR) + SUN_RICE_HOUR;
}

void onNightStart() {
  isRain = 0;
  isNight = 1; 
#ifdef _DEBUG_
  Serial.println("NIGHT START:");
#endif
}

void onDayStart() {
  isRain = 0;
  isNight = 252;
#ifdef _DEBUG_
  Serial.println("DAY START:");
#endif
}
//-----------------------FUNCS----------------------------

void saveSettings() {
  if (_isDirty) {
    for (uint8_t i = S_MIN; i < S_MAX; i++) { 
      EEPROM.write(i, settings[i - S_MIN]);
      delay(1);
    }
    _isDirty = 0;
#ifdef _DEBUG_
    Serial.println("SETTINGS SAVED!!!");
#endif
  }
}

void setColors() {
  colors[S_COL_R] = settings[S_COL_R];
  colors[S_COL_G] = settings[S_COL_G];
  colors[S_COL_B] = settings[S_COL_B];
}

void lightOn() {
  if (isSettingsShowing) {
    showLedSettings();
  } else {
    if (isLampMode) {
      setLeds(lamps[S_COL_R], lamps[S_COL_G], lamps[S_COL_B]);
    } else {
      if (isNight == 128) {
        setLeds(0,0,0);
        setLed(colors[S_COL_R], colors[S_COL_G], colors[S_COL_B], 0);
      } else {
        setLeds(colors[S_COL_R], colors[S_COL_G], colors[S_COL_B]);
      }
    }
  }
  showLeds();
#ifdef _DEBUG_
  Serial.print("night:");
  Serial.print(isNight);
  Serial.print(" rain:");
  Serial.print(isRain);
  Serial.print(" RGB:");
  Serial.print(colors[S_COL_R]);
  Serial.print(".");
  Serial.print(colors[S_COL_G]);
  Serial.print(".");
  Serial.println(colors[S_COL_B]);
#endif
}

void setLeds(uint8_t r, uint8_t g, uint8_t b) {
  for (uint8_t pos = 0; pos < 8; pos++) {
    setLed(r, g, b, pos);
  }
}

void setLed(uint8_t r, uint8_t g, uint8_t b, uint8_t pos) {
  dleds1.setPixelColor(pos, dleds1.Color(r, g, b));
  dleds2.setPixelColor(pos, dleds2.Color(r, g, b));
}

void showLeds() {
  dleds1.show();
  dleds2.show();
}

void checkLightDiv() {
  switch(isNight) {
    case 1:
      if (isReducedColor(S_COL_B, nightLedStep(S_COL_B), 0)) {
        isNight = 2;
      }
      lightOn();
      break;
    case 2:
      if (isReducedColor(S_COL_G, nightLedStep(S_COL_G), 0)) {
        isNight = 3;
      }
      lightOn();
      break;
    case 3:
      if (isReducedColor(S_COL_R, nightLedStep(S_COL_R), 1)) {
        isGrowedColor(S_COL_G, 1);
        isGrowedColor(S_COL_B, 1);
        isNight = 128;
      }
      lightOn();
      break;
    case 252:
      isReducedColor(S_COL_G, 1, 0);
      isReducedColor(S_COL_B, 1, 0);
      isNight = 253;
      break;
    case 253:
      if (isGrowedColor(S_COL_R, nightLedStep(S_COL_R))) {
        isNight = 254;
      }
      lightOn();
      break;
    case 254:
      if (isGrowedColor(S_COL_G, nightLedStep(S_COL_G))) {
        isNight = 255;
      }
      lightOn();
      break;
    case 255:
      if (isGrowedColor(S_COL_B, nightLedStep(S_COL_B))) {
        isNight = 0;
      }
      lightOn();
      break;
  }
  if (isRain == 1 && isNight == 0) {
    uint8_t res = isReducedColor(S_COL_R, rainLedStep(S_COL_R), RAIN_LIGHT_MIN);
    res += isReducedColor(S_COL_G, rainLedStep(S_COL_G), RAIN_LIGHT_MIN);
    res += isReducedColor(S_COL_B, rainLedStep(S_COL_B), RAIN_LIGHT_MIN);
    if (res == 3) {
      isRain = 2;
      pumpStart();
    }
    lightOn();
  }
  if (isRain == 255 && isNight == 0) {
    uint8_t res = isGrowedColor(S_COL_R, rainLedStep(S_COL_R));
    res += isGrowedColor(S_COL_G, rainLedStep(S_COL_G));
    res += isGrowedColor(S_COL_B, rainLedStep(S_COL_B));
    if (res == 3) {
      isRain = 0;
    }
    lightOn();
  }
}

uint8_t isReducedColor(uint8_t pos, uint8_t stepLight, uint8_t minLight) {
  if (colors[pos] > minLight) {
    colors[pos] -= stepLight; 
    return 0;
  } else {
    return 1;
  }
}

uint8_t isGrowedColor(uint8_t pos, uint8_t stepLight) {
  if (colors[pos] < settings[pos]) {
    colors[pos] += stepLight;
    if (colors[pos] > settings[pos]) {
       colors[pos] = settings[pos];
    }
    return 0;
  } else {
    return 1;
  }
}

uint8_t nightLedStep(uint8_t colorPos) {
  if (colors[colorPos] >= 128) {
    return 4;
  }
  if (colors[colorPos] >= 16) {
    return 2;
  }
  return 1;
}

uint8_t rainLedStep(uint8_t colorPos) {
  if (colors[colorPos] >= 128) {
    return 16;
  }
  if (colors[colorPos] >= 32) {
    return 8;
  }
  return 4;
}

uint8_t settingsStep() {
  switch (settingsPos) {
    case S_COL_R:
    case S_COL_G:
    case S_COL_B:
      return 16;
      break;
    case S_HUMIDITY_PERC:
      return 5;
      break;
    default:
      return 1;
      break;
  }
}

void showLedSettings() {
  int8_t val = settingsPos + 1;
  for (uint8_t pos = 7; pos >= 5; pos--) {
    dleds2.setPixelColor(pos, dleds2.Color(val >= 1?1:0, val >= 2?1:0, val >= 3?1:0));
    val -= 3;    
  }
  val = (settings[settingsPos] / settingsStep()) % 10;
  for (uint8_t pos = 0; pos < 3; pos++) {
    dleds2.setPixelColor(pos, dleds2.Color(val >= 1?1:0, val >= 2?1:0, val >= 3?1:0));
    val -= 3;    
  }
  val = (settings[settingsPos] / settingsStep()) / 10;
  for (uint8_t pos = 3; pos < 5; pos++) {
    dleds2.setPixelColor(pos, dleds2.Color(val >= 1?1:0, val >= 2?1:0, val >= 3?1:0));
    val -= 3;    
  }
  for (uint8_t pos = 0; pos < 8; pos++) {
    dleds1.setPixelColor(pos, dleds1.Color(0, 0, 0));
  }
}

void rainStart() {
  if (isNight == 0) {
    isRain = 1;
  }
}

void rainStop() {
  isRain = 255;
  if (pumpOnSec > 1) {
    pumpOnSec = 1;
  }
}

void pumpStart() {
  if (isRain == 2) {
    pumpOnSec = settings[S_RAIN_LENGTH_MIN] * 60;
  } else {
    pumpOnSec = settings[S_MOTOR_ON_SEC] + 1;
  }
  pumpOffMin = settings[S_MOTOR_OFF_MIN];
  powerOn(PUMP_PIN);
}

void pumpStop() {
  if (isRain) {
    rainStop();
  }
  powerOff(PUMP_PIN);
  lightOn();
}

//--------------------------------POWER MANAGEMENT---------------------------------
/*#define PUMP_PIN    9
#define FAN_PIN     10
#define HEATER_PIN  11*/
void powerOn(uint8_t unit) {
  switch (unit) {
    case PUMP_PIN:
      if (isNight == 0) {
        digitalWrite(PUMP_PIN, HIGH);
        digitalWrite(FAN_PIN, LOW);
        digitalWrite(HEATER_PIN, LOW);
      }
      break;
    case FAN_PIN:
      if ((isNight == 0) && (pumpOnSec == 0)) {
        digitalWrite(PUMP_PIN, LOW);
        digitalWrite(FAN_PIN, HIGH);
        digitalWrite(HEATER_PIN, LOW);
      }
      break;
    case HEATER_PIN:
      if ((fanOnMin == 0) && (pumpOnSec == 0)) {
        digitalWrite(PUMP_PIN, LOW);
        digitalWrite(FAN_PIN, LOW);
        digitalWrite(HEATER_PIN, HIGH);
      }
      break;
  }
#ifdef _DEBUG_
  Serial.print("power_on:");
  Serial.print(unit);
  Serial.print(" isNight:");
  Serial.print(isNight);
  Serial.print(" pumpOnSec:");
  Serial.print(pumpOnSec);
  Serial.print(" fanOnMin:");
  Serial.print(fanOnMin);
  Serial.print(" heatOnMin:");
  Serial.println(heatOnMin);
#endif
}

void powerOff(uint8_t unit) {
  switch (unit) {
    case PUMP_PIN:
      digitalWrite(PUMP_PIN, LOW);
      if ((isNight == 0) && (fanOnMin > 0)) {
        digitalWrite(FAN_PIN, HIGH);
      } else if (heatOnMin > 0) {
        digitalWrite(HEATER_PIN, HIGH);
      }
      break;
    case FAN_PIN:
      digitalWrite(FAN_PIN, LOW);
      if (heatOnMin > 0) {
        digitalWrite(HEATER_PIN, HIGH);
      }
      break;
    case HEATER_PIN:
      digitalWrite(HEATER_PIN, LOW);
      break;
    default:
      digitalWrite(PUMP_PIN, LOW);
      digitalWrite(FAN_PIN, LOW);
      digitalWrite(HEATER_PIN, LOW);
      break;
  }
#ifdef _DEBUG_
  Serial.print("power_off:");
  Serial.print(unit);
  Serial.print(" isNight:");
  Serial.print(isNight);
  Serial.print(" pumpOnSec:");
  Serial.print(pumpOnSec);
  Serial.print(" fanOnMin:");
  Serial.print(fanOnMin);
  Serial.print(" heatOnMin:");
  Serial.println(heatOnMin);
#endif
}

