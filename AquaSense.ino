#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>
#include <IRremote.h>
#include <RTClib.h>

// ── Pin Definitions ──
#define TRIG_PIN 9
#define ECHO_PIN 10
#define TEMP_PIN 7
#define DHT_PIN 6
#define DHT_TYPE DHT22
#define RELAY_PIN 5
#define BUZZER_PIN 3
#define RGB_RED_PIN 11
#define RGB_GREEN_PIN 12
#define RGB_BLUE_PIN 13
#define SMALL_RED_PIN A3
#define JOYSTICK_X A6
#define BTN_OK 8
#define BTN_BACK 7
#define IR_PIN 4
#define WATER_SENSOR_PIN A0
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define TANK_HEIGHT 100.0

// ── IR Remote Button Codes ──
#define IR_SCREEN_ON 0xFF6897
#define IR_SCREEN_OFF 0xFF30CF

// ── Objects ──
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
DHT dht(DHT_PIN, DHT_TYPE);
OneWire oneWire(TEMP_PIN);
DallasTemperature tempSensor(&oneWire);
IRrecv irReceiver(IR_PIN);
decode_results irResults;
RTC_DS1307 rtc;

// ── Variables ──
float waterLevel = 0;
float waterSensorValue = 0;
float temperature = 0;
float humidity = 0;
float prevWaterLevel = 0;
float avgConsumption = 0;
float leakDropAmount = 0;
int leakMinutes = 0;
int currentMenu = 0;
int totalMenus = 5;
bool screenOn = true;
bool possibleLeak = false;
bool systemStarted = false;
unsigned long lastReadTime = 0;
unsigned long lastMenuMove = 0;
unsigned long leakStartTime = 0;
DateTime leakStartDateTime;
bool leakTimerStarted = false;

// ── Menu Selection ──
int selectedMenu = 0;
bool inSubMenu = false;

// ── EEPROM Addresses ──
#define ADDR_AVG_CONSUMPTION 0
#define ADDR_LAST_LEVEL 4

// ──────────────────────────────
void setRGB(bool red, bool green, bool blue) {
  digitalWrite(RGB_RED_PIN, red);
  digitalWrite(RGB_GREEN_PIN, green);
  digitalWrite(RGB_BLUE_PIN, blue);
}

// ──────────────────────────────
void drawCentered(String text, int y, int textSize) {
  display.setTextSize(textSize);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, y);
  display.println(text);
}

// ──────────────────────────────
void showWelcomeScreen() {
  display.clearDisplay();
  display.setTextColor(WHITE);
  drawCentered("AquaSense", 5, 2);
  drawCentered("Nizam Muraqabat", 28, 1);
  drawCentered("AlKhazzan AlZaki", 38, 1);
  drawCentered("Press OK to Start", 52, 1);
  display.display();
}

// ──────────────────────────────
void showLoadingScreen() {
  display.clearDisplay();
  display.setTextColor(WHITE);
  drawCentered("AquaSense", 5, 2);
  drawCentered("Nizam Muraqabat", 28, 1);
  drawCentered("AlKhazzan AlZaki", 38, 1);
  drawCentered("Starting...", 50, 1);
  display.display();
  delay(500);

  // Loading bar
  for (int i = 0; i <= 10; i++) {
    display.clearDisplay();
    display.setTextColor(WHITE);
    drawCentered("AquaSense", 5, 2);
    drawCentered("Nizam Muraqabat", 28, 1);
    drawCentered("AlKhazzan AlZaki", 38, 1);
    drawCentered("Starting...", 50, 1);

    // Draw loading bar
    int barX = 14;
    int barY = 56;
    int barWidth = 100;
    int barHeight = 6;
    display.drawRect(barX, barY, barWidth, barHeight, WHITE);
    display.fillRect(barX, barY, i * 10, barHeight, WHITE);
    display.display();
    delay(200);
  }
  delay(500);
}

// ──────────────────────────────
void showMainMenuList() {
  display.clearDisplay();
  display.setTextColor(WHITE);
  drawCentered("-- Main Menu --", 0, 1);

  String menuItems[] = {
    "Water Level",
    "Temperature",
    "Humidity",
    "Prediction",
    "Alerts"
  };

  for (int i = 0; i < totalMenus; i++) {
    display.setCursor(10, 14 + (i * 10));
    if (i == selectedMenu) {
      display.print("> ");
    } else {
      display.print("  ");
    }
    display.println(menuItems[i]);
  }

  display.setCursor(0, 56);
  display.print("OK:Select  Back:Home");
  display.display();
}

// ──────────────────────────────
void setup() {
  Serial.begin(9600);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RGB_RED_PIN, OUTPUT);
  pinMode(RGB_GREEN_PIN, OUTPUT);
  pinMode(RGB_BLUE_PIN, OUTPUT);
  pinMode(SMALL_RED_PIN, OUTPUT);
  pinMode(BTN_OK, INPUT_PULLUP);
  pinMode(BTN_BACK, INPUT_PULLUP);

  dht.begin();
  tempSensor.begin();
  irReceiver.enableIRIn();

  if (!rtc.begin()) {
    Serial.println("RTC not found!");
  }
  if (!rtc.isrunning()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextColor(WHITE);

  EEPROM.get(ADDR_AVG_CONSUMPTION, avgConsumption);
  EEPROM.get(ADDR_LAST_LEVEL, prevWaterLevel);
  if (isnan(avgConsumption) || avgConsumption < 0) avgConsumption = 0;
  if (isnan(prevWaterLevel) || prevWaterLevel < 0) prevWaterLevel = 0;

  // Show welcome screen and wait for OK
  showWelcomeScreen();
  while (digitalRead(BTN_OK)) {
    delay(50);
  }
  delay(200);

  // Show loading screen
  showLoadingScreen();
  systemStarted = true;

  // Show main menu list
  inSubMenu = false;
  showMainMenuList();
}

// ──────────────────────────────
float readWaterLevel() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH);
  float distance = (duration * 0.0343) / 2;
  float level = TANK_HEIGHT - distance;
  if (level < 0) level = 0;
  if (level > TANK_HEIGHT) level = TANK_HEIGHT;
  return (level / TANK_HEIGHT) * 100.0;
}

// ──────────────────────────────
float readWaterSensor() {
  int raw = analogRead(WATER_SENSOR_PIN);
  return map(raw, 0, 1023, 0, 100);
}

// ──────────────────────────────
void checkLeak(float current, float previous) {
  float drop = previous - current;
  DateTime now = rtc.now();

  if (drop > 10.0) {
    if (!leakTimerStarted) {
      leakStartDateTime = now;
      leakTimerStarted = true;
      leakDropAmount = drop;
    } else {
      leakDropAmount += drop;
      TimeSpan elapsed = now - leakStartDateTime;
      leakMinutes = elapsed.totalseconds() / 60;
    }
    possibleLeak = true;
    digitalWrite(RELAY_PIN, HIGH);
  } else {
    possibleLeak = false;
    leakTimerStarted = false;
    leakDropAmount = 0;
    leakMinutes = 0;
    digitalWrite(RELAY_PIN, LOW);
  }
}

// ──────────────────────────────
float daysRemaining(float level, float consumption) {
  if (consumption <= 0) return 99;
  return level / consumption;
}

// ──────────────────────────────
void handleAlerts(float level, float temp,
                  float hum, bool leak) {
  static unsigned long lastFlash = 0;
  static bool flashState = false;
  unsigned long now = millis();

  setRGB(false, false, false);
  digitalWrite(SMALL_RED_PIN, LOW);
  noTone(BUZZER_PIN);

  if (now - lastFlash > 200) {
    lastFlash = now;
    flashState = !flashState;
  }

  if (leak) {
    if (flashState) setRGB(true, false, false);
    else setRGB(false, false, false);
    digitalWrite(SMALL_RED_PIN, flashState);
    tone(BUZZER_PIN, 1200);
    return;
  }

  if (level < 10) {
    if (flashState) setRGB(true, false, false);
    else setRGB(false, false, false);
    tone(BUZZER_PIN, 1000);
    return;
  }

  if (temp > 35) {
    setRGB(true, false, false);
    digitalWrite(SMALL_RED_PIN, flashState);
    tone(BUZZER_PIN, 900);
    return;
  }

  if (temp > 30 && hum > 70) {
    setRGB(true, true, false);
    digitalWrite(SMALL_RED_PIN, flashState);
    tone(BUZZER_PIN, 600);
    return;
  }

  if (level <= 25) {
    setRGB(true, false, false);
    tone(BUZZER_PIN, 800);
    return;
  }

  if (level <= 50) {
    setRGB(true, true, false);
    tone(BUZZER_PIN, 500);
    return;
  }

  setRGB(false, true, false);
}

// ──────────────────────────────
void showSubMenu(int menu, float level, float temp,
                 float hum, bool leak, float days,
                 float waterSensor) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  DateTime now = rtc.now();

  switch (menu) {
    case 0:
      drawCentered("-- Water Level --", 0, 1);
      display.setCursor(0, 14);
      display.print("Ultrasonic: ");
      display.print(level, 1);
      display.println("%");
      display.print("Sensor:     ");
      display.print(waterSensor, 1);
      display.println("%");
      display.print("Time: ");
      display.print(now.hour());
      display.print(":");
      if (now.minute() < 10) display.print("0");
      display.println(now.minute());
      break;

    case 1:
      drawCentered("-- Temperature --", 0, 1);
      display.setCursor(0, 14);
      display.print("Temp: ");
      display.print(temp, 1);
      display.println(" C");
      if (temp > 35) {
        display.println("! HIGH TEMP !");
        display.println("Add ventilation");
        display.println("fans to tank");
      } else {
        display.println("Status: Normal");
      }
      break;

    case 2:
      drawCentered("-- Humidity --", 0, 1);
      display.setCursor(0, 14);
      display.print("Humidity: ");
      display.print(hum, 1);
      display.println("%");
      if (temp > 30 && hum > 70) {
        display.println("! Poor ventilation");
        display.println("Risk of mold!");
      } else {
        display.println("Ventilation: OK");
      }
      break;

    case 3:
      drawCentered("-- Prediction --", 0, 1);
      display.setCursor(0, 14);
      display.print("Days left: ");
      display.println(days, 1);
      display.print("Avg use: ");
      display.print(avgConsumption, 2);
      display.println("%/cycle");
      display.print("Date: ");
      display.print(now.day());
      display.print("/");
      display.println(now.month());
      break;

    case 4:
      drawCentered("-- Alerts --", 0, 1);
      display.setCursor(0, 14);
      if (leak) {
        display.println("!! POSSIBLE LEAK !!");
        display.print("Lost: ");
        display.print(leakDropAmount, 1);
        display.println("%");
        display.print("In: ");
        display.print(leakMinutes);
        display.println(" min");
      } else if (level < 10) {
        display.println("!! CRITICAL LOW !!");
      } else if (level < 25) {
        display.println("! Low Water");
      } else if (temp > 35) {
        display.println("! High Temp");
        display.println("Add ventilation!");
      } else if (temp > 30 && hum > 70) {
        display.println("! Poor Ventilation");
      } else {
        display.println("All Systems OK");
      }
      break;
  }

  display.setCursor(0, 56);
  display.print("Back:Menu  ");
  display.print(menu + 1);
  display.print("/");
  display.println(totalMenus);
  display.display();
}

// ──────────────────────────────
void loop() {
  unsigned long now = millis();

  // ── IR Remote ──
  if (irReceiver.decode(&irResults)) {
    if (irResults.value == IR_SCREEN_ON) {
      screenOn = true;
    } else if (irResults.value == IR_SCREEN_OFF) {
      screenOn = false;
      display.clearDisplay();
      display.display();
    }
    irReceiver.resume();
  }

  // ── Read Sensors Every 2 Seconds ──
  if (now - lastReadTime > 2000) {
    lastReadTime = now;
    waterLevel = readWaterLevel();
    waterSensorValue = readWaterSensor();
    tempSensor.requestTemperatures();
    temperature = tempSensor.getTempCByIndex(0);
    humidity = dht.readHumidity();
    if (isnan(humidity)) humidity = 0;
    if (isnan(temperature)) temperature = 0;

    float consumed = prevWaterLevel - waterLevel;
    if (consumed > 0 && consumed < 10) {
      avgConsumption = (avgConsumption + consumed) / 2;
      EEPROM.put(ADDR_AVG_CONSUMPTION, avgConsumption);
      EEPROM.put(ADDR_LAST_LEVEL, waterLevel);
    }
    checkLeak(waterLevel, prevWaterLevel);
    prevWaterLevel = waterLevel;
  }

  float days = daysRemaining(waterLevel, avgConsumption);
  handleAlerts(waterLevel, temperature, humidity, possibleLeak);

  // ── Navigation ──
  int joyX = analogRead(JOYSTICK_X);
  bool okPressed = !digitalRead(BTN_OK);
  bool backPressed = !digitalRead(BTN_BACK);

  if (!inSubMenu) {
    // ── Main Menu List Navigation ──
    if (now - lastMenuMove > 300) {
      if (joyX < 400) {
        selectedMenu--;
        if (selectedMenu < 0) selectedMenu = totalMenus - 1;
        lastMenuMove = now;
      } else if (joyX > 600) {
        selectedMenu++;
        if (selectedMenu >= totalMenus) selectedMenu = 0;
        lastMenuMove = now;
      }
    }

    if (okPressed) {
      inSubMenu = true;
      currentMenu = selectedMenu;
      delay(200);
    }

    if (screenOn) showMainMenuList();

  } else {
    // ── Sub Menu Navigation ──
    if (now - lastMenuMove > 300) {
      if (joyX < 400) {
        currentMenu--;
        if (currentMenu < 0) currentMenu = totalMenus - 1;
        lastMenuMove = now;
      } else if (joyX > 600) {
        currentMenu++;
        if (currentMenu >= totalMenus) currentMenu = 0;
        lastMenuMove = now;
      }
    }

    if (backPressed) {
      inSubMenu = false;
      selectedMenu = currentMenu;
      delay(200);
    }

    if (screenOn) {
      showSubMenu(currentMenu, waterLevel, temperature,
                  humidity, possibleLeak, days,
                  waterSensorValue);
    }
  }
}
