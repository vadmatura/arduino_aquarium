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
#define MSEC_IN_SEC         1000
#define MSEC_DIFF_ON_CYCLE  60000
#define RAIN_LIGHT_MIN      8
#define FAN_LATENT_MIN      5

//============================ Classes start ===================================
//******************************************************************************
class SettingsManager {

	#define S_IS_NEED_RESET   9
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

	#define S_SAVE_COUNT      (S_MAX - S_MIN)
	#define S_COUNT      			(S_SAVE_COUNT + SN_COUNT)

private:
  const uint8_t m_def[S_SAVE_COUNT] = {208,144,48,10,15,2,80};

  volatile uint8_t m_data[S_COUNT];
  volatile uint8_t m_pos = 0;
	volatile bool m_notSaved = false;

public:
  void begin() {
		if (isNeedReset()) {
	    EEPROM.write(S_S, 1);
			delay(1);
	    for (uint8_t i = S_MIN; i < S_MAX; i++) {
	      EEPROM.write(i, defSettings[i - S_MIN]);
				delay(1);
	    }
		}
		read();
	}

	bool isNeedReset() {
		return EEPROM.read(S_IS_NEED_RESET);
	}

	void setNeedReset() {
    EEPROM.write(S_S, 0);
  }

	void read() {
		for (uint8_t i = S_MIN; i < S_MAX; i++) {
	    settings[i - S_MIN] = EEPROM.read(i);
	  }
	}

	void save() {
	  if (m_notSaved) {
	    for (uint8_t i = S_MIN; i < S_MAX; i++) {
	      EEPROM.write(i, settings[i - S_MIN]);
	      delay(1);
	    }
	    m_notSaved = false;
	  }
	}

	void setPos(uint8_t settingsPos) {
		m_pos = settingsPos;
	}

	uint8_t pos() {
		return m_pos;
	}

	uint8_t get() {
		return m_data[m_pos];
	}

	uint8_t get(uint8_t pos) {
		if (S_MIN <= pos && pos <= S_MAX) {
			return m_data[pos];
		} else {
			return 0;
		}
	}

	void set(uint8_t pos, uint8_t val) {
		if (S_MIN <= pos && pos <= S_MAX) {
			m_data[pos] = val;
			m_notSaved = true;
		}
	}

	uint8_t step() {
	  switch (m_pos) {
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

	void reduce() {
		m_data[m_pos] -= step();
		m_notSaved = true;
	}

	void increase() {
		m_data[m_pos] += step();
		m_notSaved = true;
	}

#ifdef _DEBUG_
	void print() {
		const char sNames[] = {'R','G','B','M','m','r','H','h'};
    for (uint8_t i = 0; i < S_COUNT; i++) {
      Serial.print(sNames[i]);
      Serial.print(m_data[i]);
      Serial.print(" ");
    }
	}
#endif
}

//******************************************************************************
class LightManager {

	#define L_LEDS_COUNT      16
	#define L_SUN_RICE_HOUR   8
	#define L_SUN_SET_HOUR    23

	#define L_COL_R           S_COL_R
	#define L_COL_G           S_COL_G
	#define L_COL_B           S_COL_B

	#define L_MODE_DAY        0
	#define L_MODE_REDUCE_B   1
	#define L_MODE_REDUCE_G   2
	#define L_MODE_REDUCE_R   3
	#define L_MODE_EVENING    L_MODE_REDUCE_B
	#define L_MODE_NIGHT      4
	#define L_MODE_MORNING    5
	#define L_MODE_INCREASE_R 6
	#define L_MODE_INCREASE_G 7
	#define L_MODE_INCREASE_B 8

	#define L_RAIN_START      9
	#define L_RAIN_GO         10
	#define L_RAIN_STOP       11

private:

	volatile uint8_t m_colors[3];
	volatile uint8_t m_lamps[3] = {0,0,0};
	volatile uint8_t m_mode = L_MODE_DAY;

	uint8_t reduce(uint8_t pos, uint8_t stepLight, uint8_t minLight) {
	  if (colors[pos] > minLight) {
	    colors[pos] -= stepLight;
	    return 0;
	  } else {
	    return 1;
	  }
	}

	uint8_t increase(uint8_t pos, uint8_t stepLight) {
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

	uint8_t nightStep(uint8_t colorPos) {
	  if (colors[colorPos] >= 128) {
	    return 4;
	  }
	  if (colors[colorPos] >= 16) {
	    return 2;
	  }
	  return 1;
	}

	uint8_t rainStep(uint8_t colorPos) {
	  if (colors[colorPos] >= 128) {
	    return 16;
	  }
	  if (colors[colorPos] >= 32) {
	    return 8;
	  }
	  return 4;
	}

	void setLeds(uint8_t r, uint8_t g, uint8_t b) {
	  for (uint8_t pos = 0; pos < 8; pos++) {
	    setLed(r, g, b, pos);
	  }
	}

	void setLed(uint8_t r, uint8_t g, uint8_t b, uint8_t pos) {
	  dleds.setPixelColor(pos, dleds.Color(r, g, b));
	}

public:
	volatile bool isLampMode = false;

	LightManager() {
		Adafruit_NeoPixel dleds = Adafruit_NeoPixel(LEDS_COUNT, LED_PIN1, NEO_GRB + NEO_KHZ800);
	}

	void begin(uint8_t r, uint8_t g, uint8_t b) {
		dleds.begin();
		lightOn();
	  m_colors[L_COL_R] = r;
	  m_colors[L_COL_G] = g;
	  m_colors[L_COL_B] = b;
	}

	void set() {
    if (isLampMode) {
      setLeds(m_lamps[L_COL_R], m_lamps[L_COL_G], m_lamps[L_COL_B]);
    } else {
      if (m_mode == L_MODE_NIGHT) {
        setLeds(0,0,0);
        setLed(m_colors[L_COL_R], m_colors[L_COL_G], m_colors[L_COL_B], 0);
      } else {
        setLeds(m_colors[L_COL_R], m_colors[L_COL_G], m_colors[L_COL_B]);
      }
    }
	  dleds.show();
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

	void processMode() {
		uint8_t res;
		switch(m_mode) {
	    case L_MODE_REDUCE_B:
	      if (reduce(L_COL_B, nightStep(L_COL_B), 0)) {
	        m_mode = L_MODE_REDUCE_G;
	      }
	      break;
	    case L_MODE_REDUCE_G:
	      if (reduce(L_COL_G, nightStep(L_COL_G), 0)) {
	        m_mode = L_MODE_REDUCE_R;
	      }
	      break;
	    case L_MODE_REDUCE_R:
	      if (reduce(L_COL_R, nightStep(L_COL_R), 1)) {
	        increase(L_COL_G, 1);
	        increase(L_COL_B, 1);
	        m_mode = L_MODE_NIGHT;
	      }
	      break;
	    case L_MODE_MORNING:
	      reduce(L_COL_G, 1, 0);
	      reduce(L_COL_B, 1, 0);
	      m_mode = L_MODE_INCREASE_R;
	      break;
	    case L_MODE_INCREASE_R:
	      if (increase(L_COL_R, nightStep(L_COL_R))) {
	        m_mode = L_MODE_INCREASE_G;
	      }
	      break;
	    case L_MODE_INCREASE_G:
	      if (increase(L_COL_G, nightStep(L_COL_G))) {
	        m_mode = L_MODE_INCREASE_B;
	      }
	      break;
	    case L_MODE_INCREASE_B:
	      if (increase(L_COL_B, nightStep(L_COL_B))) {
	        m_mode = L_MODE_DAY;
	      }
	      break;
			case L_RAIN_START:
				res = reduce(L_COL_R, rainStep(L_COL_R), RAIN_LIGHT_MIN);
				res += reduce(L_COL_G, rainStep(L_COL_G), RAIN_LIGHT_MIN);
				res += reduce(L_COL_B, rainStep(L_COL_B), RAIN_LIGHT_MIN);
				if (res == 3) {
					m_mode = L_RAIN_GO;
					//pumpStart();
				}
				break;
			case L_RAIN_STOP:
				res = increase(L_COL_R, rainStep(L_COL_R));
				res += increase(L_COL_G, rainStep(L_COL_G));
				res += increase(L_COL_B, rainStep(L_COL_B));
				if (res == 3) {
					m_mode = L_MODE_DAY;
				}
				break;
	  }
		set();
	}

	bool isNight() {
		return (L_MODE_REDUCE_B <= m_mode && m_mode <=L_MODE_INCREASE_B);
	}

	void eveningStart() {
	  m_mode = L_MODE_EVENING;
	}

	void morningStart() {
	  m_mode = L_MODE_MORNING;
	}

	bool isRain() {
		return (L_RAIN_START <= m_mode && m_mode <=L_RAIN_STOP);
	}

	void rainStart() {
	  m_mode = L_RAIN_START;
	}

	void rainStop() {
	  m_mode = L_RAIN_STOP;
	}

	void increaseLamp(uint8_t pos, uint8_t step) {
	  if (L_COL_R <= pos && pos <= L_COL_B) {
			m_lamps[pos] += step;
		}
	}

	void showSettings(int8_t pos, int8_t val, int8_t step) {
		uint8_t i;
	  pos++;
	  for (i = 7; i >= 5; i--) {
	    dleds.setPixelColor(i, dleds.Color(pos >= 1?1:0, pos >= 2?1:0, pos >= 3?1:0));
	    pos -= 3;
	  }
	  pos = (val / step()) % 10;
	  for (i = 0; i < 3; i++) {
	    dleds.setPixelColor(i, dleds.Color(pos >= 1?1:0, pos >= 2?1:0, pos >= 3?1:0));
	    pos -= 3;
	  }
	  pos = (val / step()) / 10;
	  for (i = 3; i < 5; i++) {
	    dleds.setPixelColor(i, dleds.Color(pos >= 1?1:0, pos >= 2?1:0, pos >= 3?1:0));
	    pos -= 3;
	  }
	  for (i = 8; i < L_LEDS_COUNT; i++) {
	    dleds.setPixelColor(i, dleds.Color(0, 0, 0));
	  }
	}
}

//******************************************************************************
//******************************************************************************
//******************************************************************************
//******************************************************************************
//============================ Classes end =====================================

IRrecv irrecv(IR_PIN);
decode_results irrecvResult;
RTC_DS1307 RTC;
DateTime currentTime;
DHT dht(DHT22_PIN, DHTTYPE);
SettingsManager sm;
LightManager lm;


volatile uint16_t pumpOnSec;
volatile uint8_t pumpOffMin;
volatile uint8_t fanOnMin = FAN_LATENT_MIN - 1;
volatile uint8_t heatOnMin = 0;
volatile uint8_t isRain = 0;
volatile float humidity = 0;
volatile float temperature = 0;

volatile unsigned long _irValue;
volatile unsigned long _msek = 0;
volatile uint8_t _min = 0;

volatile bool isSettingsShowing = false;

void setup() {
  sm.begin();
  pinMode(PUMP_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  pinMode(HEATER_PIN, OUTPUT);
  lm.begin(sm.get[S_COL_R], sm.get[S_COL_G], sm.get[S_COL_B]);
  pumpOffMin = sm.get[S_MOTOR_OFF_MIN];
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
    RTC.adjust(DateTime(__DATE__, __TIME__));
#ifdef _DEBUG_
    Serial.println("RTC is NOT running!");
#endif
    // This will reflect the time that your sketch was compiled
  }


  currentTime = RTC.now();
  sm.set(SN_RAIN_HOUR, currentTime.secondstime() % (SUN_SET_HOUR - SUN_RICE_HOUR) + SUN_RICE_HOUR);
  delay(1000);
}

void loop() {
  // put your main code here, to run repeatedly:
  if (irrecv.decode(&irrecvResult)) {
    onIrButtonPressed(irrecvResult.value);
#ifdef _DEBUG_
    Serial.println(irrecvResult.value, HEX);
		sm.print();
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
      sm.setPos(S_COL_R);
      break;
    case 0xFF9867://2
      sm.setPos(S_COL_G);
      break;
    case 0xFFB04F://3
      sm.setPos(S_COL_B);
      break;
    case 0xFF30CF://4
      sm.setPos(S_MOTOR_ON_SEC);
      break;
    case 0xFF18E7://5
      sm.setPos(S_MOTOR_OFF_MIN);
      break;
    case 0xFF7A85://6
      sm.setPos(S_RAIN_LENGTH_MIN);
      break;
    case 0xFF10EF://7
      sm.setPos(S_HUMIDITY_PERC);
      break;
    case 0xFF38C7://8
      sm.setPos(SN_RAIN_HOUR);
      break;
    case 0xFF5AA5://9
      break;
    case 0xFF4AB5://0
      lm.isLampMode = !lm.isLampMode;
      _isDirty = 0;
      break;
    case 0xFF629D://up
      if (lm.isLampMode) {
        if (sm.pos() <= S_COL_B) {
					lm.increaseLamp(sm.pos(), sm.step());
        }
      } else {
				sm.increase();
      }
      break;
    case 0xFFA857://down
			if (lm.isLampMode) {
				if (sm.pos() <= S_COL_B) {
					lm.increaseLamp(sm.pos(), -sm.step());
				}
			} else {
				sm.reduce();
			}
			break;
    case 0xFF22DD://left
      lm.eveningStart();
      break;
    case 0xFFC23D://right
      lm.morningStart();
      break;
    case 0xFF02FD://Ok
      isSettingsShowing = !isSettingsShowing;
      sm.save();
      break;
    case 0xFF52AD://#
      pumpStart();
      break;
    case 0xFF42BD://*
      if (lm.isRain()) {
        lm.rainStart();
      } else {
        lm.rainStop();
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
  lm.set();
}

void onSecond() {
  lm.processMode();
  /*if (pumpOnSec > 0) {
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
  }*/
}

void onMinute() {
  saveSettings();
  pumpOffMin--;
  if (pumpOffMin == 0) {
    pumpStart();
  }
  if (currentTime.hour() == SUN_RICE_HOUR && currentTime.minute() < 15 && lm.isNight())) {
    lm.morningStart();
  }
  if (currentTime.hour() == SUN_SET_HOUR && currentTime.minute() < 15 && !lm.isNight())) {
    lm.eveningStart();
  }
  /*if (humidity > settings[S_HUMIDITY_PERC]) {
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
  }*/
}

void onHour() {
  /*if (currentTime.hour() == sm.get(SN_RAIN_HOUR)) {
    rainStart();
  }*/
}

void onDay() {
  sm.set(SN_RAIN_HOUR, millis() % (SUN_SET_HOUR - SUN_RICE_HOUR) + SUN_RICE_HOUR);
}
//-----------------------FUNCS----------------------------

/*void pumpStart() {
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
//#define PUMP_PIN    9
//#define FAN_PIN     10
//#define HEATER_PIN  11
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
}*/
