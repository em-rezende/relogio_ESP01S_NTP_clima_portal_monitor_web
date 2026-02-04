/*
 * RELÓGIO ESP8266 com MATRIZ LED 8x32 NTP e CLIMA + SERVIDOR WEB
 * Versão com relógio com números pequenos: HH:MM:ss
 * Autor: Ezequiel M Rezende / Deepseek
 * 2026/01/02 - Início
 * 2026/01/28 - Modificado: Servidor Web com display virtual 8x32
 * 2026/01/29 - Modificado: Dados climáticos detalhados na web - CORRIGIDO
 * 2026/01/29 - Versão CORRIGIDA: Display virtual funcionando
 */

#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <EEPROM.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <time.h>
#include <ArduinoJson.h>
#include <ESP8266WebServer.h>

/* ================== PINOS ================== */
#define NUM_MAX 4

// PINOS PARA ESP-01S
#define DIN_PIN 2            // GPIO2
#define CS_PIN 3             // GPIO3/RX
#define CLK_PIN 0            // GPIO0
#define CONFIG_BUTTON_PIN 1  // GPIO1/TX

// PINOS PARA ESP8266 NodeMcu ESP-12E
// #define CLK_PIN 12  // D6
// #define CS_PIN  13  // D7
// #define DIN_PIN 15  // D8
// #define CONFIG_BUTTON_PIN 2  // GPIO2/D4

/* ================== INCLUDES ================== */
#include "max7219_final.h"
#include "new_fonts.h"

/* ================== DEFINIÇÕES ================== */
static const uint8_t CLOCK_DSTMODE_NONE = 0;
static const uint8_t CLOCK_DSTMODE_EU = 1;
static const uint8_t CLOCK_DSTMODE_US = 2;
static const uint32_t SETTINGS_MAGIC = 0x4B315749;
static const int EEPROM_ADDR = 0;

// Configurações padrão
#define DEFAULT_CLOCK_DURATION 30     // 30 segundos de relógio
#define SCROLL_SPEED 80               // Velocidade do scroll
#define SCROLL_STEP 2                 // Passo do scroll

// Tempo para contador regressivo (em segundos)
#define COUNTDOWN_DURATION 10

// Períodos do dia para controle de brilho
enum BrightnessPeriod {
  PERIOD_NIGHT,      // 22:00 - 05:59
  PERIOD_DAWN,       // 06:00 - 06:59
  PERIOD_MORNING,    // 07:00 - 10:59
  PERIOD_DAY,        // 11:00 - 15:59
  PERIOD_AFTERNOON,  // 16:00 - 17:59
  PERIOD_EVENING,    // 18:00 - 21:59
  PERIOD_ALWAYS_ON   // Sempre ligado (para testes)
};

// Estrutura para configuração de brilho
struct BrightnessConfig {
  uint8_t night;      // 22:00 - 05:59
  uint8_t dawn;       // 06:00 - 06:59
  uint8_t morning;    // 07:00 - 10:59
  uint8_t day;        // 11:00 - 15:59
  uint8_t afternoon;  // 16:00 - 17:59
  uint8_t evening;    // 18:00 - 21:59
};

// Configurações de brilho padrão (0-15)
const BrightnessConfig DEFAULT_BRIGHTNESS = {
  1,   // Noite: muito baixo
  3,   // Madrugada: baixo
  5,   // Manhã: médio-baixo
  8,   // Dia: médio (padrão)
  6,   // Tarde: médio
  4    // Noite inicial: médio-baixo
};

// Variável global com configuração de brilho
BrightnessConfig brightnessConfig = DEFAULT_BRIGHTNESS;

/* ================== ESTRUTURA DE CONFIGURAÇÕES ================== */
struct Settings {
  float utcOffset;
  bool is12H;
  bool observeDST;
  uint8_t dstMode;
  float dstExtraHours;

  // Configurações de clima
  bool weatherEnabled;
  char weatherAPIKey[33];
  char weatherLat[12];
  char weatherLon[13];
  char weatherLang[4];
  int weatherUpdateInterval;
  
  // Configurações de exibição
  int clockDisplayDuration;  // Segundos que o relógio é mostrado

  uint32_t magic;
};

// Array com nomes dos dias abreviados
const char* dayNames[] = {
  "Dom", "Seg", "Ter", "Qua", "Qui", "Sex", "Sab"
};

// Array com nomes dos meses abreviados
const char* monthNames[] = {
  "jan", "fev", "mar", "abr", "mai", "jun",
  "jul", "ago", "set", "out", "nov", "dez"
};

/* ================== VARIÁVEIS GLOBAIS ================== */
Settings settings;

float utcOffset = -3.0;
bool is12HFormat = true;
bool isPM = false;
bool dots = false;
bool observeDST = false;
uint8_t dstMode = CLOCK_DSTMODE_US;
float dstExtraHours = 1.0;

// Hora atual
int h = 12, m = 0, s = 0;

// Data atual
int d = 31;     // Dia do mês
int mo = 12;    // Mês (1-12)
int ye = 2025;  // Ano
int w = 3;      // Dia da semana (1=Dom, 2=Seg, 3=Ter, 4=Qua, 5=Qui, 6=Sex, 7=Sab)

// Variáveis para animação do relógio
int dig[6] = { 0, 0, 0, 0, 0, 0 };
int digold[6] = { 10, 10, 10, 10, 10, 10 };
int digtrans[6] = { 0, 0, 0, 0, 0, 0 };
int del = 0;
int dy = 0;

// Configurações de clima
bool weatherEnabled = false;
String weatherAPIKey = "";
String weatherLat = "";
String weatherLon = "";
String weatherLang = "&lang=pt";
int weatherUpdateInterval = 30;

// Configurações de exibição
int clockDisplayDuration = DEFAULT_CLOCK_DURATION;

// Dados do clima (ATUALIZADO com novas variáveis)
float currentTemp = 0;
float feelsLike = 0;        // Nova: sensação térmica
float tempMin = 0;          // Nova: temperatura mínima
float tempMax = 0;          // Nova: temperatura máxima
float humidity = 0;
float pressure = 0;
float windSpeed = 0;
int windDeg = 0;
String weatherDescription = "";
int clouds = 0;             // Nova: porcentagem de nuvens
int visibility = 0;         // Nova: visibilidade em metros
String cityName = "";       // Nova: nome da cidade
unsigned long sunriseTime = 0;      // Nova: nascer do sol (timestamp)
unsigned long sunsetTime = 0;       // Nova: pôr do sol (timestamp)

// String unificada data + clima
String infoString = "";

// Controle
unsigned long lastWeatherUpdate = 0;
WiFiClient weatherClient;

// Controle de modo
enum DisplayMode {
  MODE_CLOCK,
  MODE_INFO_SCROLL
};

DisplayMode currentMode = MODE_CLOCK;
bool scrollCompleted = false;
int scrollPos = 0;
unsigned long lastScrollTime = 0;
unsigned long clockStartTime = 0;

// Controle de brilho
uint8_t currentBrightness = 8;  // Brilho atual (0-15)
unsigned long lastBrightnessCheck = 0;
const unsigned long BRIGHTNESS_CHECK_INTERVAL = 60000;  // Verificar a cada minuto

// Definir qual fonte usar para texto geral (não-relógio)
const uint8_t* textFont = font_emr;

/* ================== WIFI E NTP ================== */
WiFiManager wifiManager;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0);

/* ================== SERVIDOR WEB ================== */
ESP8266WebServer server(80);  // Servidor web na porta 80

/* ================== BUFFERS PARA WIFIMANAGER ================== */
char bufUtc[10] = "-3.0";
char buf12h[6] = "false";
char bufDst[6] = "false";
char bufDstMode[10] = "NONE";
char bufDstExtra[10] = "1.0";
char bufWeatherEnabled[6] = "true";
char bufWeatherAPIKey[33] = "";
char bufWeatherLat[12] = "";
char bufWeatherLon[13] = "";
char bufWeatherLang[4] = "pt";
char bufWeatherUpdateInterval[4] = "30";
char bufClockDisplayDuration[4] = "30";

/* ================== PARÂMETROS WIFIMANAGER ================== */
WiFiManagerParameter pUtc("utc", "UTC Offset (ex: -3.0)", bufUtc, sizeof(bufUtc));
WiFiManagerParameter p12h("12h", "Formato 12h (true/false)", buf12h, sizeof(buf12h));
WiFiManagerParameter pDst("dst", "Horário de verão (true/false)", bufDst, sizeof(bufDst));
WiFiManagerParameter pDstMode("dstmode", "Modo DST: AUTO_US/AUTO_EU/NONE", bufDstMode, sizeof(bufDstMode));
WiFiManagerParameter pDstExtra("dstextra", "Horas extra DST (1.0)", bufDstExtra, sizeof(bufDstExtra));
WiFiManagerParameter pWeatherEnabled("weatherenabled", "Clima ativo (true/false)", bufWeatherEnabled, sizeof(bufWeatherEnabled));
WiFiManagerParameter pWeatherAPIKey("weatherapikey", "API Key OpenWeatherMap", bufWeatherAPIKey, sizeof(bufWeatherAPIKey));
WiFiManagerParameter pWeatherLat("weatherlat", "Latitude (ex: -19.9167)", bufWeatherLat, sizeof(bufWeatherLat));
WiFiManagerParameter pWeatherLon("weatherlon", "Longitude (ex: -43.9345)", bufWeatherLon, sizeof(bufWeatherLon));
WiFiManagerParameter pWeatherLang("weatherlang", "Idioma: pt, en, es, de, fr, it", bufWeatherLang, sizeof(bufWeatherLang));
WiFiManagerParameter pWeatherUpdateInterval("weatherupdate", "Atualizar clima a cada (minutos)", bufWeatherUpdateInterval, sizeof(bufWeatherUpdateInterval));
WiFiManagerParameter pClockDisplayDuration("clockduration", "Mostrar relógio por (segundos)", bufClockDisplayDuration, sizeof(bufClockDisplayDuration));

/* ================== FUNÇÕES EEPROM ================== */
void loadDefaultSettings() {
  settings.utcOffset = -3.0;
  settings.is12H = true;
  settings.observeDST = false;
  settings.dstMode = CLOCK_DSTMODE_US;
  settings.dstExtraHours = 1.0;

  settings.weatherEnabled = false;
  strcpy(settings.weatherAPIKey, "");
  strcpy(settings.weatherLat, "");
  strcpy(settings.weatherLon, "");
  strcpy(settings.weatherLang, "pt");
  settings.weatherUpdateInterval = 30;

  settings.clockDisplayDuration = DEFAULT_CLOCK_DURATION;

  settings.magic = SETTINGS_MAGIC;
}

void loadSettings() {
  EEPROM.begin(sizeof(Settings));
  EEPROM.get(EEPROM_ADDR, settings);

  if (settings.magic != SETTINGS_MAGIC) {
    loadDefaultSettings();
    EEPROM.put(EEPROM_ADDR, settings);
    EEPROM.commit();
  }
}

void saveSettings() {
  settings.magic = SETTINGS_MAGIC;
  EEPROM.put(EEPROM_ADDR, settings);
  EEPROM.commit();
}

/* ================== FUNÇÕES AUXILIARES ================== */
void applySettingsFromEEPROM() {
  utcOffset = settings.utcOffset;
  is12HFormat = settings.is12H;
  observeDST = settings.observeDST;
  dstMode = settings.dstMode;
  dstExtraHours = settings.dstExtraHours;

  weatherEnabled = settings.weatherEnabled;
  weatherAPIKey = String(settings.weatherAPIKey);
  weatherLat = String(settings.weatherLat);
  weatherLon = String(settings.weatherLon);
  weatherLang = "&lang=" + String(settings.weatherLang);
  weatherUpdateInterval = settings.weatherUpdateInterval;

  clockDisplayDuration = settings.clockDisplayDuration;

  if (clockDisplayDuration < 10) {
    clockDisplayDuration = 10;
  }
}

void updateWiFiManagerBuffers() {
  dtostrf(utcOffset, 1, 1, bufUtc);
  strcpy(buf12h, is12HFormat ? "true" : "false");
  strcpy(bufDst, observeDST ? "true" : "false");

  if (dstMode == CLOCK_DSTMODE_US) strcpy(bufDstMode, "AUTO_US");
  else if (dstMode == CLOCK_DSTMODE_EU) strcpy(bufDstMode, "AUTO_EU");
  else strcpy(bufDstMode, "NONE");

  dtostrf(dstExtraHours, 1, 1, bufDstExtra);
  strcpy(bufWeatherEnabled, weatherEnabled ? "true" : "false");
  strncpy(bufWeatherAPIKey, weatherAPIKey.c_str(), sizeof(bufWeatherAPIKey) - 1);
  strncpy(bufWeatherLat, weatherLat.c_str(), sizeof(bufWeatherLat) - 1);
  strncpy(bufWeatherLon, weatherLon.c_str(), sizeof(bufWeatherLon) - 1);
  strncpy(bufWeatherLang, settings.weatherLang, sizeof(bufWeatherLang) - 1);
  itoa(weatherUpdateInterval, bufWeatherUpdateInterval, 10);
  itoa(clockDisplayDuration, bufClockDisplayDuration, 10);
}

void updateEEPROMStructure() {
  settings.utcOffset = utcOffset;
  settings.is12H = is12HFormat;
  settings.observeDST = observeDST;
  settings.dstMode = dstMode;
  settings.dstExtraHours = dstExtraHours;

  settings.weatherEnabled = weatherEnabled;
  strncpy(settings.weatherAPIKey, weatherAPIKey.c_str(), sizeof(settings.weatherAPIKey) - 1);
  strncpy(settings.weatherLat, weatherLat.c_str(), sizeof(settings.weatherLat) - 1);
  strncpy(settings.weatherLon, weatherLon.c_str(), sizeof(settings.weatherLon) - 1);
  strncpy(settings.weatherLang, weatherLang.substring(6).c_str(), sizeof(settings.weatherLang) - 1);
  settings.weatherUpdateInterval = weatherUpdateInterval;

  settings.clockDisplayDuration = clockDisplayDuration;
}

/* ================== CALLBACK WIFIMANAGER ================== */
void saveConfigCallback() {
  utcOffset = atof(pUtc.getValue());
  is12HFormat = String(p12h.getValue()) == "true";
  observeDST = String(pDst.getValue()) == "true";
  dstExtraHours = atof(pDstExtra.getValue());

  String mode = String(pDstMode.getValue());
  mode.toUpperCase();
  if (mode == "AUTO_US") dstMode = CLOCK_DSTMODE_US;
  else if (mode == "AUTO_EU") dstMode = CLOCK_DSTMODE_EU;
  else dstMode = CLOCK_DSTMODE_NONE;

  weatherEnabled = String(pWeatherEnabled.getValue()) == "true";
  weatherAPIKey = String(pWeatherAPIKey.getValue());
  weatherLat = String(pWeatherLat.getValue());
  weatherLon = String(pWeatherLon.getValue());
  weatherLang = "&lang=" + String(pWeatherLang.getValue());
  weatherUpdateInterval = atoi(pWeatherUpdateInterval.getValue());
  
  clockDisplayDuration = atoi(pClockDisplayDuration.getValue());

  if (clockDisplayDuration < 10) {
    clockDisplayDuration = 10;
  }

  updateEEPROMStructure();
  saveSettings();

  clr();
  refreshAll();
  showChar('O', 6);
  showChar('K', 12);
  refreshAll();
  delay(2000);
}

/* ================== FUNÇÕES DE DISPLAY ================== */
void showDigit(int digit, int col, const uint8_t* font) {
  if (digit < 0 || digit > 9) return;

  int bytesPerChar = pgm_read_byte(font);
  int startPos = 1 + (digit * bytesPerChar);
  int width = pgm_read_byte(font + startPos);

  for (int i = 0; i < width; i++) {
    if (col + i < NUM_MAX * 8) {
      scr[col + i] = pgm_read_byte(font + startPos + 1 + i);
    }
  }
}

void showChar(char c, int col) {
  if (c < 32 || c > 255) return;

  const uint8_t* fontData = textFont;
  int bytesPerChar = pgm_read_byte(fontData);
  int fontIndex = c - 32;
  
  if (fontIndex < 0) return;
  
  int startPos = 1 + (fontIndex * bytesPerChar);
  int width = pgm_read_byte(fontData + startPos);

  for (int i = 0; i < 6; i++) {
    if (col + i < NUM_MAX * 8) {
      scr[col + i] = 0;
    }
  }

  for (int i = 0; i < width; i++) {
    if (col + i < NUM_MAX * 8) {
      scr[col + i] = pgm_read_byte(fontData + startPos + 1 + i);
    }
  }
}

void setCol(int col, byte v) {
  if (col >= 0 && col < NUM_MAX * 8) {
    scr[col] = v;
  }
}

/* ================== CONTROLE DE BRILHO ================== */
BrightnessPeriod getCurrentPeriod() {
  int currentHour = h;
  
  if (currentHour >= 22 || currentHour < 6) {
    return PERIOD_NIGHT;      // 22:00 - 05:59
  } else if (currentHour >= 6 && currentHour < 7) {
    return PERIOD_DAWN;       // 06:00 - 06:59
  } else if (currentHour >= 7 && currentHour < 11) {
    return PERIOD_MORNING;    // 07:00 - 10:59
  } else if (currentHour >= 11 && currentHour < 16) {
    return PERIOD_DAY;        // 11:00 - 15:59
  } else if (currentHour >= 16 && currentHour < 18) {
    return PERIOD_AFTERNOON;  // 16:00 - 17:59
  } else if (currentHour >= 18 && currentHour < 22) {
    return PERIOD_EVENING;    // 18:00 - 21:59
  }
  
  return PERIOD_DAY;  // Default
}

uint8_t getBrightnessForPeriod(BrightnessPeriod period) {
  switch (period) {
    case PERIOD_NIGHT:
      return brightnessConfig.night;
    case PERIOD_DAWN:
      return brightnessConfig.dawn;
    case PERIOD_MORNING:
      return brightnessConfig.morning;
    case PERIOD_DAY:
      return brightnessConfig.day;
    case PERIOD_AFTERNOON:
      return brightnessConfig.afternoon;
    case PERIOD_EVENING:
      return brightnessConfig.evening;
    default:
      return brightnessConfig.day;
  }
}

void updateBrightness() {
  static BrightnessPeriod lastPeriod = PERIOD_ALWAYS_ON;
  static unsigned long lastUpdate = 0;
  
  unsigned long currentTime = millis();
  
  if (currentTime - lastUpdate >= BRIGHTNESS_CHECK_INTERVAL || 
      currentTime - lastBrightnessCheck >= 1000) {
    
    lastBrightnessCheck = currentTime;
    BrightnessPeriod currentPeriod = getCurrentPeriod();
    
    if (currentPeriod != lastPeriod) {
      uint8_t newBrightness = getBrightnessForPeriod(currentPeriod);
      
      if (newBrightness != currentBrightness) {
        currentBrightness = newBrightness;
        sendCmdAll(CMD_INTENSITY, currentBrightness);
      }
      
      lastPeriod = currentPeriod;
      lastUpdate = currentTime;
    }
  }
}

/* ================== CONTADOR REGRESSIVO ================== */
bool showCountdown() {
  sendCmdAll(CMD_INTENSITY, 15);
  
  unsigned long countdownStart = millis();
  bool buttonPressed = false;
  int lastSecondShown = COUNTDOWN_DURATION;
  
  const uint8_t* countdownFont = dig4x8;
  
  clr();
  showDigit(1, 10, countdownFont);
  showDigit(0, 16, countdownFont);
  refreshAll();
  delay(100);
  
  while (millis() - countdownStart < (COUNTDOWN_DURATION * 1000UL)) {
    unsigned long elapsed = millis() - countdownStart;
    int secondsRemaining = COUNTDOWN_DURATION - (elapsed / 1000);
    
    if (secondsRemaining != lastSecondShown && secondsRemaining >= 0) {
      lastSecondShown = secondsRemaining;
      
      clr();
      
      if (secondsRemaining >= 10) {
        showDigit(secondsRemaining / 10, 10, countdownFont);
        showDigit(secondsRemaining % 10, 16, countdownFont);
      } else if (secondsRemaining >= 0) {
        showDigit(secondsRemaining, 13, countdownFont);
      }
      
      refreshAll();
    }
    
    if (digitalRead(CONFIG_BUTTON_PIN) == LOW) {
      delay(50);
      if (digitalRead(CONFIG_BUTTON_PIN) == LOW) {
        buttonPressed = true;
        
        clr();
        showChar('C', 5);
        showChar('F', 11);
        showChar('G', 17);
        refreshAll();
        delay(500);
        
        break;
      }
    }
    
    delay(10);
  }
  
  clr();
  refreshAll();
  
  updateBrightness();
  
  return buttonPressed;
}

/* ================== ANIMAÇÃO CONTÍNUA SEM PAUSA ================== */
void showAnimClock() {
  byte digPos[6] = { 1, 6, 13, 18, 25, 29 };
  int animFrames = 8;
  int num = 6;

  static unsigned long lastUpdate = 0;
  static int animCounter = 0;

  unsigned long now = millis();

  if (now - lastUpdate >= 40) {
    lastUpdate = now;

    animCounter++;
    if (animCounter >= animFrames * 10) animCounter = 0;

    int displayHour = h;
    if (is12HFormat) {
      displayHour %= 12;
      if (displayHour == 0) displayHour = 12;
    }

    int currentDigits[6] = {
      displayHour / 10 ? displayHour / 10 : 10,
      displayHour % 10,
      m / 10,
      m % 10,
      s / 10,
      s % 10
    };

    for (int i = 0; i < num; i++) {
      if (currentDigits[i] != dig[i]) {
        digold[i] = dig[i];
        dig[i] = currentDigits[i];
        digtrans[i] = animFrames;
      }
    }

    clr();

    for (int i = 0; i < num; i++) {
      const uint8_t* font = (i >= 4) ? dig3x5 : dig4x8;

      if (digtrans[i] == 0) {
        showDigit(dig[i], digPos[i], font);
      } else {
        int progress = animFrames - digtrans[i];

        if (digold[i] >= 0 && digold[i] <= 9) {
          int startPosOld = 1 + (digold[i] * pgm_read_byte(font));
          int widthOld = pgm_read_byte(font + startPosOld);

          for (int col = 0; col < widthOld; col++) {
            int displayCol = digPos[i] + col;
            if (displayCol < 32) {
              byte data = pgm_read_byte(font + startPosOld + 1 + col);
              data >>= progress;
              scr[displayCol] |= data;
            }
          }
        }

        int startPosNew = 1 + (dig[i] * pgm_read_byte(font));
        int widthNew = pgm_read_byte(font + startPosNew);

        for (int col = 0; col < widthNew; col++) {
          int displayCol = digPos[i] + col;
          if (displayCol < 32) {
            byte data = pgm_read_byte(font + startPosNew + 1 + col);
            data <<= (animFrames - 1 - progress);
            scr[displayCol] |= data;
          }
        }

        digtrans[i]--;
      }
    }

    setCol(11, dots ? 0b00100100 : 0);
    setCol(23, dots ? 0b00100100 : 0);

    refreshAll();
  }
}

/* ================== FUNÇÕES DE TEMPO ================== */
void updateTime() {
  if (WiFi.status() == WL_CONNECTED) {
    timeClient.update();
    unsigned long epoch = timeClient.getEpochTime();

    epoch += (long)(utcOffset * 3600);

    time_t rawTime = (time_t)epoch;
    struct tm* timeinfo = gmtime(&rawTime);

    h = timeinfo->tm_hour;
    m = timeinfo->tm_min;
    s = timeinfo->tm_sec;

    d = timeinfo->tm_mday;
    mo = timeinfo->tm_mon + 1;
    ye = timeinfo->tm_year + 1900;

    w = timeinfo->tm_wday; // 0=Domingo, 1=Segunda, etc.
   
    updateInfoString();
  }
}

/* ================== FUNÇÕES DE CLIMA ================== */
void updateWeatherData() {
  if (!weatherEnabled || weatherAPIKey.length() == 0) {
    return;
  }

  if (weatherClient.connect("api.openweathermap.org", 80)) {
    String url = "/data/2.5/weather?";
    url += "lat=" + weatherLat;
    url += "&lon=" + weatherLon;
    url += "&units=metric";
    url += "&appid=" + weatherAPIKey;
    url += weatherLang;

    weatherClient.println("GET " + url + " HTTP/1.1");
    weatherClient.println("Host: api.openweathermap.org");
    weatherClient.println("Connection: close");
    weatherClient.println();

    delay(1000);

    if (weatherClient.available()) {
      String response = weatherClient.readString();

      int jsonStart = response.indexOf('{');
      if (jsonStart != -1) {
        String jsonData = response.substring(jsonStart);

        DynamicJsonBuffer jsonBuffer;
        JsonObject& root = jsonBuffer.parseObject(jsonData);

        if (root.success()) {
          // Dados principais
          currentTemp = root["main"]["temp"];
          feelsLike = root["main"]["feels_like"];
          tempMin = root["main"]["temp_min"];
          tempMax = root["main"]["temp_max"];
          humidity = root["main"]["humidity"];
          pressure = root["main"]["pressure"];
          
          // Nome da cidade
          cityName = root["name"].as<String>();
          
          // Visibilidade
          visibility = root["visibility"];
          
          // Nuvens
          clouds = root["clouds"]["all"];
          
          // Vento
          if (root["wind"].success()) {
            windSpeed = root["wind"]["speed"];
            windDeg = root["wind"]["deg"];
          }
          
          // Horários do sol
          sunriseTime = root["sys"]["sunrise"];
          sunsetTime = root["sys"]["sunset"];
          
          // Descrição do tempo
          JsonArray& weatherArray = root["weather"];
          if (weatherArray.size() > 0) {
            weatherDescription = weatherArray[0]["description"].as<String>();
            weatherDescription.toLowerCase();
            if (weatherDescription.length() > 0) {
              weatherDescription.setCharAt(0, toupper(weatherDescription.charAt(0)));
            }
          }

          lastWeatherUpdate = millis();
          
          Serial.println("✅ Dados climáticos atualizados:");
          Serial.print("   Cidade: "); Serial.println(cityName);
          Serial.print("   Temp: "); Serial.print(currentTemp); Serial.println("°C");
          Serial.print("   Sensação: "); Serial.print(feelsLike); Serial.println("°C");
          Serial.print("   Mín/Máx: "); Serial.print(tempMin); Serial.print("/"); Serial.println(tempMax);
        }
      }
    }

    weatherClient.stop();
  }
}

/* ================== FUNÇÃO PARA ATUALIZAR STRING DE INFO ================== */
void updateInfoString() {
  infoString = String(dayNames[w]) + ", " + String(d) + " " + 
               monthNames[mo - 1] + " " + String(ye);
  
  if (weatherEnabled && weatherAPIKey.length() > 0) {
    infoString += " - Temp: " + String(currentTemp, 1) + "C ";
    infoString += "Umid: " + String(humidity, 0) + "% ";
    infoString += "Press: " + String(pressure, 0) + "hPa ";
    infoString += "Vento: " + String(windSpeed * 3.6, 1) + "km/h ";
    
    if (windSpeed > 0.1) {
      String windDir = getWindDir(windDeg);
      if (windDir.length() > 0) {
        infoString += windDir + " ";
      }
    }
    
    if (weatherDescription.length() > 0) {
      infoString += weatherDescription;
    }
  }
}

String getWindDir(int degrees) {
  if (windSpeed <= 0.1) return "calmo";

  degrees = degrees % 360;

  if (degrees >= 337.5 || degrees < 22.5) return "N";
  if (degrees >= 22.5 && degrees < 67.5) return "NE";
  if (degrees >= 67.5 && degrees < 112.5) return "L";
  if (degrees >= 112.5 && degrees < 157.5) return "SE";
  if (degrees >= 157.5 && degrees < 202.5) return "S";
  if (degrees >= 202.5 && degrees < 247.5) return "SO";
  if (degrees >= 247.5 && degrees < 292.5) return "O";
  if (degrees >= 292.5 && degrees < 337.5) return "NO";

  return "";
}

/* ================== FUNÇÃO PARA CONVERTER TIMESTAMP UNIX ================== */
String timestampToLocalTime(unsigned long timestamp) {
  if (timestamp == 0) return "--:--";
  
  // Ajustar para fuso horário local
  timestamp += (long)(utcOffset * 3600);
  
  // Converter para horas e minutos
  time_t timeValue = timestamp;
  struct tm* timeinfo = gmtime(&timeValue);
  
  char buffer[6];
  sprintf(buffer, "%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min);
  return String(buffer);
}

/* ================== FUNÇÕES DE SCROLL ================== */
void displayInfoScroll() {
  String displayText = "   " + infoString + "   ";
  
  if (millis() - lastScrollTime > SCROLL_SPEED) {
    lastScrollTime = millis();

    clr();

    int totalWidth = 0;
    for (int i = 0; i < displayText.length(); i++) {
      char c = displayText[i];
      if (c >= 32 && c <= 255) {
        int fontIndex = c - 32;
        int startPos = 1 + (fontIndex * pgm_read_byte(textFont));
        totalWidth += pgm_read_byte(textFont + startPos) + 1;
      }
    }
    
    int maxScroll = totalWidth + 32;
    int startX = -scrollPos;

    int currentX = startX;
    for (int i = 0; i < displayText.length(); i++) {
      char c = displayText[i];
      if (c >= 32 && c <= 255) {
        int fontIndex = c - 32;
        int startPos = 1 + (fontIndex * pgm_read_byte(textFont));
        int width = pgm_read_byte(textFont + startPos);
        
        for (int col = 0; col < width; col++) {
          int screenCol = currentX + col;
          if (screenCol >= 0 && screenCol < 32) {
            scr[screenCol] = pgm_read_byte(textFont + startPos + 1 + col);
          }
        }
        currentX += width + 1;
      }
    }

    refreshAll();
    scrollPos += SCROLL_STEP;

    if (scrollPos >= maxScroll) {
      scrollCompleted = true;
    }
  }
}

void displayIPScroll(int scrollSpeed = 80, int scrollStep = 1) {
  sendCmdAll(CMD_INTENSITY, 8);

  String ipText = "       Conectado IP: " + WiFi.localIP().toString() + "   ";
  int ipScrollPos = 0;
  bool completed = false;
  unsigned long lastIPScrollTime = 0;

  while (!completed) {
    unsigned long currentTime = millis();

    if (currentTime - lastIPScrollTime > scrollSpeed) {
      lastIPScrollTime = currentTime;

      clr();

      int textWidth = 0;
      for (int i = 0; i < ipText.length(); i++) {
        char c = ipText[i];
        if (c >= 32 && c <= 255) {
          int fontIndex = c - 32;
          int startPos = 1 + (fontIndex * pgm_read_byte(textFont));
          textWidth += pgm_read_byte(textFont + startPos) + 1;
        }
      }
      
      int maxScroll = textWidth + 32;
      int startX = -ipScrollPos;

      int currentX = startX;
      for (int i = 0; i < ipText.length(); i++) {
        char c = ipText[i];
        if (c >= 32 && c <= 255) {
          int fontIndex = c - 32;
          int startPos = 1 + (fontIndex * pgm_read_byte(textFont));
          int width = pgm_read_byte(textFont + startPos);
          
          for (int col = 0; col < width; col++) {
            int screenCol = currentX + col;
            if (screenCol >= 0 && screenCol < 32) {
              scr[screenCol] = pgm_read_byte(textFont + startPos + 1 + col);
            }
          }
          currentX += width + 1;
        }
      }

      refreshAll();
      ipScrollPos += scrollStep;

      if (ipScrollPos >= maxScroll) {
        completed = true;
      }
    }
    delay(10);
  }

  delay(500);
  clr();
  refreshAll();
  updateBrightness();
}

/* ================== CONTROLE DE MODO ================== */
void checkModeTransition() {
  unsigned long currentTime = millis();
  
  if (currentMode == MODE_CLOCK) {
    if (currentTime - clockStartTime >= (clockDisplayDuration * 1000UL)) {
      if (WiFi.status() == WL_CONNECTED) {
        updateTime();
        
        if (weatherEnabled && weatherAPIKey.length() > 0) {
          if (currentTime - lastWeatherUpdate >= (weatherUpdateInterval * 60000UL)) {
            updateWeatherData();
          }
        }
      }
      
      updateInfoString();
      
      currentMode = MODE_INFO_SCROLL;
      scrollCompleted = false;
      scrollPos = 0;
      lastScrollTime = 0;
    }
  }
  
  else if (currentMode == MODE_INFO_SCROLL) {
    if (scrollCompleted) {
      currentMode = MODE_CLOCK;
      clockStartTime = currentTime;
    }
  }
}

/* ================== SERVIDOR WEB ================== */
String formatTime() {
  String timeStr = "";
  if (h < 10) timeStr += "0";
  timeStr += String(h);
  timeStr += ":";
  if (m < 10) timeStr += "0";
  timeStr += String(m);
  timeStr += ":";
  if (s < 10) timeStr += "0";
  timeStr += String(s);
  return timeStr;
}


String getHTML() {
  String html = "<!DOCTYPE html>";
  html += "<html lang='pt-BR'>";
  html += "<head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Relógio LED Matrix ESP8266 - Monitor</title>";

  html += "<style>";
  html += "@import url('https://fonts.googleapis.com/css2?family=Inter:wght@300;400;600;700&display=swap');";
  html += "* { box-sizing: border-box; margin: 0; padding: 0; }";
  html += "body { font-family: 'Inter', 'Segoe UI', system-ui, sans-serif; margin: 20px; background: #f0f0f0; }";
  html += ".container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 0 10px rgba(0,0,0,0.1); }";
  html += "h1, h2, h3 { color: #2c3e50; }";
  html += ".section { margin-bottom: 30px; padding: 15px; background: #ecf0f1; border-radius: 5px; }";
  html += ".status { padding: 10px; border-radius: 5px; margin: 5px 0; }";
  html += ".connected { background: #d4edda; border: 1px solid #c3e6cb; }";
  html += ".disconnected { background: #f8d7da; border: 1px solid #f5c6cb; }";
  html += ".grid { display: grid; grid-template-columns: repeat(32, 12px); grid-template-rows: repeat(8, 12px); gap: 1px; margin: 10px 0; }";
  html += ".pixel { width: 12px; height: 12px; border-radius: 1px; }";
  html += ".pixel-on { background: #ff4444; box-shadow: 0 0 3px #ff4444; }";
  html += ".pixel-off { background: #222; border: 1px solid #444; }";
  html += ".clock { font-size: 2em; font-family: monospace; margin: 10px 0; }";
  html += ".info { background: #fff3cd; padding: 10px; border-radius: 5px; }";
  html += "table { width: 100%; border-collapse: collapse; }";
  html += "td, th { padding: 8px; text-align: left; border-bottom: 1px solid #ddd; }";
  html += "th { background: #3498db; color: white; }";
  html += "tr:nth-child(even) { background: #f9f9f9; }";
  html += ".refresh { background: #3498db; color: white; border: none; padding: 8px 15px; border-radius: 5px; cursor: pointer; }";
  html += ".refresh:hover { background: #2980b9; }";
  html += ".auto-refresh { display: inline-block; margin-left: 10px; }";
  html += "</style>";

  html += "<script>";
  html += "let autoRefresh = true;";
  html += "function updateTime() {";
  html += "  fetch('/data')";
  html += "    .then(response => response.json())";
  html += "    .then(data => {";
  html += "      document.getElementById('currentTime').textContent = data.time;";
  html += "      document.getElementById('infoString').textContent = data.infoString;";
  html += "      document.getElementById('displayMode').textContent = data.displayMode;";
  html += "      document.getElementById('brightness').textContent = data.brightness;";
  html += "      document.getElementById('ipAddress').textContent = data.ip;";
  html += "      document.getElementById('wifiStatus').textContent = data.wifiStatus;";
  html += "      document.getElementById('wifiStatus').className = data.wifiStatus === 'Conectado' ? 'status connected' : 'status disconnected';";
  html += "      updateDisplay(data.displayData);";
  html += "    });";
  html += "}";
  html += "function updateDisplay(displayData) {";
  html += "  const grid = document.getElementById('displayGrid');";
  html += "  grid.innerHTML = '';";
  html += "  for(let row = 0; row < 8; row++) {";
  html += "    for(let col = 0; col < 32; col++) {";
  html += "      const pixel = document.createElement('div');";
  html += "      const index = col;";
  html += "      const byteValue = displayData[index];";
  html += "      const isOn = byteValue & (1 << row);";
  html += "      pixel.className = 'pixel ' + (isOn ? 'pixel-on' : 'pixel-off');";
  html += "      pixel.title = 'Col: ' + col + ', Row: ' + row + ', Byte: 0x' + byteValue.toString(16);";
  html += "      grid.appendChild(pixel);";
  html += "    }";
  html += "  }";
  html += "}";
  html += "function toggleAutoRefresh() {";
  html += "  autoRefresh = !autoRefresh;";
  html += "  document.getElementById('autoRefreshBtn').textContent = autoRefresh ? 'Desativar Auto-Refresh' : 'Ativar Auto-Refresh';";
  html += "  if(autoRefresh) {";
  html += "    startAutoRefresh();";
  html += "  }";
  html += "}";
  html += "function startAutoRefresh() {";
  html += "  if(autoRefresh) {";
  html += "    updateTime();";
  html += "    setTimeout(startAutoRefresh, 1000);";
  html += "  }";
  html += "}";
  html += "document.addEventListener('DOMContentLoaded', function() {";
  html += "  startAutoRefresh();";
  html += "});";
  html += "</script>";
  html += "</head>";
  html += "<body>";
  html += "<div class='container'>";
  html += "<h1>📱 Relógio LED Matrix ESP8266 - Monitor Web</h1>";
  
  html += "<div class='section'>";
  html += "<h2>🖥️ Display Virtual 8x32</h2>";
  html += "<div id='displayGrid' class='grid'></div>";
  html += "<div class='clock' id='currentTime'>";
  html += formatTime();
  html += "</div>";
  html += "<div class='info' id='infoString'>";
  html += infoString;
  html += "</div>";
  html += "</div>";
  
  html += "<div class='section'>";
  html += "<h2>📊 Status do Sistema</h2>";
  html += "<table>";
  html += "<tr><th>Parâmetro</th><th>Valor</th></tr>";
  html += "<tr><td>Modo de Exibição</td><td id='displayMode'>";
  html += (currentMode == MODE_CLOCK ? "Relógio" : "Scroll Info");
  html += "</td></tr>";
  html += "<tr><td>Nível de Brilho</td><td id='brightness'>";
  html += String(currentBrightness) + "/15";
  html += "</td></tr>";
  html += "<tr><td>IP Address</td><td id='ipAddress'>";
  html += WiFi.localIP().toString();
  html += "</td></tr>";
  html += "<tr><td>Status WiFi</td><td id='wifiStatus' class='";
  html += (WiFi.status() == WL_CONNECTED ? "status connected" : "status disconnected");
  html += "'>";
  html += (WiFi.status() == WL_CONNECTED ? "Conectado" : "Desconectado");
  html += "</td></tr>";
  html += "</table>";
  html += "</div>";
  
  html += "<div class='section'>";
  html += "<h2>⚙️ Configurações Atuais</h2>";
  html += "<table>";
  html += "<tr><th>Configuração</th><th>Valor</th></tr>";
  html += "<tr><td>UTC Offset</td><td>";
  html += String(utcOffset, 1);
  html += "</td></tr>";
  html += "<tr><td>Formato 12h</td><td>";
  html += (is12HFormat ? "Sim" : "Não");
  html += "</td></tr>";
  html += "<tr><td>Duração do Relógio</td><td>";
  html += String(clockDisplayDuration);
  html += " segundos</td></tr>";
  html += "<tr><td>Clima Ativado</td><td>";
  html += (weatherEnabled ? "Sim" : "Não");
  html += "</td></tr>";
  html += "<tr><td>Intervalo Atualização Clima</td><td>";
  html += String(weatherUpdateInterval);
  html += " minutos</td></tr>";
  html += "<tr><td>API Key</td><td>";
  html += (weatherAPIKey.length() > 0 ? "Configurada" : "Não configurada");
  html += "</td></tr>";
  html += "<tr><td>Coordenadas</td><td>Lat: ";
  html += weatherLat;
  html += " Lon: ";
  html += weatherLon;
  html += "</td></tr>";
  html += "</table>";
  html += "</div>";
  
  // SEÇÃO DE DADOS METEOROLÓGICOS DETALHADOS (mantido da v06)
  if (weatherEnabled && weatherAPIKey.length() > 0) {
    html += "<div class='section'>";
    html += "<h2>🌤️ Dados Meteorológicos Detalhados</h2>";
    
    // Informações Básicas
    html += "<div style='margin-bottom: 15px;'>";
    html += "<h3 style='background: linear-gradient(to right, #3498db, #2ecc71); color: white; padding: 8px 12px; border-radius: 5px;'>📍 Informações Básicas</h3>";
    html += "<table style='width: 100%; margin-bottom: 15px;'>";
    html += "<tr><td style='width: 40%; padding: 8px;'><strong>Cidade:</strong></td><td style='padding: 8px;'>";
    html += cityName.length() > 0 ? cityName : "Não disponível";
    html += "</td></tr>";
    html += "<tr><td style='padding: 8px;'><strong>Coordenadas:</strong></td><td style='padding: 8px;'>";
    html += "Lat: " + weatherLat + " Lon: " + weatherLon;
    html += "</td></tr>";
    html += "<tr><td style='padding: 8px;'><strong>Hora Local:</strong></td><td style='padding: 8px;'>";
    html += formatTime();
    html += "</td></tr>";
    html += "</table>";
    html += "</div>";
    
    // Temperatura
    html += "<div style='margin-bottom: 15px;'>";
    html += "<h3 style='background: linear-gradient(to right, #3498db, #2ecc71); color: white; padding: 8px 12px; border-radius: 5px;'>🌡️ Temperatura</h3>";
    html += "<table style='width: 100%; margin-bottom: 15px;'>";
    html += "<tr><td style='width: 40%; padding: 8px;'><strong>Atual:</strong></td><td style='padding: 8px;'>";
    html += String(currentTemp, 1) + "°C</td></tr>";
    html += "<tr><td style='padding: 8px;'><strong>Sensação Térmica:</strong></td><td style='padding: 8px;'>";
    html += String(feelsLike, 1) + "°C</td></tr>";
    html += "<tr><td style='padding: 8px;'><strong>Mínima:</strong></td><td style='padding: 8px;'>";
    html += String(tempMin, 1) + "°C</td></tr>";
    html += "<tr><td style='padding: 8px;'><strong>Máxima:</strong></td><td style='padding: 8px;'>";
    html += String(tempMax, 1) + "°C</td></tr>";
    html += "</table>";
    html += "</div>";
    
    // Outros Parâmetros
    html += "<div style='margin-bottom: 15px;'>";
    html += "<h3 style='background: linear-gradient(to right, #3498db, #2ecc71); color: white; padding: 8px 12px; border-radius: 5px;'>📊 Outros Parâmetros</h3>";
    html += "<table style='width: 100%; margin-bottom: 15px;'>";
    html += "<tr><td style='width: 40%; padding: 8px;'><strong>Pressão Atmosférica:</strong></td><td style='padding: 8px;'>";
    html += String(pressure, 0) + " hPa</td></tr>";
    html += "<tr><td style='padding: 8px;'><strong>Umidade:</strong></td><td style='padding: 8px;'>";
    html += String(humidity, 0) + "%</td></tr>";
    html += "<tr><td style='padding: 8px;'><strong>Visibilidade:</strong></td><td style='padding: 8px;'>";
    if (visibility >= 10000) {
      html += "10+ km";
    } else if (visibility > 0) {
      html += String(visibility / 1000.0, 1) + " km";
    } else {
      html += "Não disponível";
    }
    html += "</td></tr>";
    html += "<tr><td style='padding: 8px;'><strong>Nuvens:</strong></td><td style='padding: 8px;'>";
    html += String(clouds) + "%</td></tr>";
    html += "</table>";
    html += "</div>";
    
    // Vento
    html += "<div style='margin-bottom: 15px;'>";
    html += "<h3 style='background: linear-gradient(to right, #3498db, #2ecc71); color: white; padding: 8px 12px; border-radius: 5px;'>💨 Vento</h3>";
    html += "<table style='width: 100%; margin-bottom: 15px;'>";
    html += "<tr><td style='width: 40%; padding: 8px;'><strong>Velocidade:</strong></td><td style='padding: 8px;'>";
    html += String(windSpeed * 3.6, 1) + " km/h</td></tr>";
    html += "<tr><td style='padding: 8px;'><strong>Direção:</strong></td><td style='padding: 8px;'>";
    String windDirection = getWindDir(windDeg);
    if (windSpeed > 0.1) {
      html += windDirection + " (" + String(windDeg) + "°)";
    } else {
      html += "Calmo";
    }
    html += "</td></tr>";
    html += "</table>";
    html += "</div>";
    
    // Informações Astronômicas
    html += "<div style='margin-bottom: 15px;'>";
    html += "<h3 style='background: linear-gradient(to right, #3498db, #2ecc71); color: white; padding: 8px 12px; border-radius: 5px;'>🌅 Informações Astronômicas</h3>";
    html += "<table style='width: 100%; margin-bottom: 15px;'>";
    html += "<tr><td style='width: 40%; padding: 8px;'><strong>Nascer do Sol:</strong></td><td style='padding: 8px;'>";
    html += timestampToLocalTime(sunriseTime);
    html += "</td></tr>";
    html += "<tr><td style='padding: 8px;'><strong>Pôr do Sol:</strong></td><td style='padding: 8px;'>";
    html += timestampToLocalTime(sunsetTime);
    html += "</td></tr>";
    html += "<tr><td style='padding: 8px;'><strong>Condições:</strong></td><td style='padding: 8px;'>";
    html += weatherDescription;
    html += "</td></tr>";
    html += "</table>";
    html += "</div>";
    
    // Última atualização
    html += "<div class='info' style='margin-top: 15px;'>";
    html += "<p><strong>⏱️ Última Atualização:</strong> ";
    html += String((millis() - lastWeatherUpdate) / 60000, 0);
    html += " minutos atrás</p>";
    html += "</div>";
    
    html += "</div>";
  }
  
  // Links da API OpenWeatherMap
  if (weatherEnabled && weatherAPIKey.length() > 0 && weatherLat.length() > 0 && weatherLon.length() > 0) {
    html += "<div class='section'>";
    html += "<h2>🌐 Links da API OpenWeatherMap</h2>";
    
    // Link 1: API completa
    html += "<p><strong>API Completa:</strong> ";
    String apiUrl = "https://api.openweathermap.org/data/2.5/weather?";
    apiUrl += "lat=" + weatherLat + "&";
    apiUrl += "lon=" + weatherLon + "&";
    apiUrl += "units=metric&";
    apiUrl += "appid=" + weatherAPIKey;
    if (weatherLang.length() > 6) {
      apiUrl += weatherLang;
    }
    html += "<a href='" + apiUrl + "' target='_blank' rel='noopener'>Abrir API</a></p>";
    
    // Link 2: JSON formatado (jsonviewer)
    html += "<p><strong>Visualizador JSON:</strong> ";
    html += "<a href='https://jsonviewer.stack.hu/' target='_blank' rel='noopener'>";
    html += "JSON Viewer Online</a> (cole a URL abaixo)</p>";
    
    // Mostrar URL para copiar
    html += "<div class='info' style='margin-top:10px;'>";
    html += "<strong>URL para teste:</strong><br>";
    html += "<input type='text' value='" + apiUrl + "' ";
    html += "style='width:100%;padding:5px;margin:5px 0;' ";
    html += "onclick='this.select()' readonly>";
    html += "<small>Clique para selecionar e copiar (Ctrl+C)</small>";
    html += "</div>";
    html += "</div>";
  }
  
  html += "<div class='section'>";
  html += "<button class='refresh' onclick='updateTime()'>🔄 Atualizar Agora</button>";
  html += "<button id='autoRefreshBtn' class='refresh auto-refresh' onclick='toggleAutoRefresh()'>Desativar Auto-Refresh</button>";
  html += "</div>";
  
  html += "<div class='section'>";
  html += "<h3>🌐 Acesso Rápido</h3>";
  html += "<p><strong>📱 Portal de Configuração:</strong> <a href='/config'>/config</a></p>";
  html += "<p><strong>📊 Monitor (esta página):</strong> <a href='/'>/</a></p>";
  html += "<p><strong>📡 Dados JSON:</strong> <a href='/data'>/data</a></p>";
  
  html += "</div>";
  
  html += "<div class='section'>";
  html += "<p><small>ESP8266 LED Matrix Clock v2.0 • ";
  html += String(dayNames[w-1]) + ", " + String(d) + " " + monthNames[mo-1] + " " + String(ye);
  html += String(dayNames[w]) + ", " + String(d) + " " + monthNames[mo-1] + " " + String(ye);
  html += "</small></p>";
  html += "</div>";
  
  html += "</div>";
  html += "</body>";
  html += "</html>";
  
  return html;
}


void handleRoot() {
  Serial.println("📡 Requisição recebida: /");
  server.send(200, "text/html", getHTML());
}

void handleData() {
  Serial.println("📡 Requisição recebida: /data");

  // DEBUG detalhado
  Serial.println("📊 Dados do display [0-31]: ");
  for (int i = 0; i < 32; i++) {
    Serial.print("scr[");
    Serial.print(i);
    Serial.print("] = ");
    Serial.print(scr[i], DEC);
    Serial.print(" (0x");
    Serial.print(scr[i], HEX);
    Serial.print(") = B");
    for (int bit = 7; bit >= 0; bit--) {
      Serial.print((scr[i] >> bit) & 1);
    }
    Serial.println();
  }

  // Criar JSON com dados atuais - FORMATAÇÃO SIMPLIFICADA
  String json = "{";
  
  // Dados básicos
  json += "\"time\":\"" + formatTime() + "\",";
  json += "\"infoString\":\"" + infoString + "\",";
  json += "\"displayMode\":\"" + String(currentMode == MODE_CLOCK ? "Relógio" : "Scroll Info") + "\",";
  json += "\"brightness\":\"" + String(currentBrightness) + "/15\",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"wifiStatus\":\"" + String(WiFi.status() == WL_CONNECTED ? "Conectado" : "Desconectado") + "\",";
  
  // Dados do display - FORMATO SIMPLES
  json += "\"displayData\":[";
  for (int i = 0; i < 32; i++) {
    json += String(scr[i]);
    if (i < 31) json += ",";
  }
  json += "]";
  
  // Dados meteorológicos (se disponíveis) - SEM vírgula extra
  if (weatherEnabled && weatherAPIKey.length() > 0) {
    json += ",\"weather\":{";
    json += "\"city\":\"" + cityName + "\",";
    json += "\"temp\":\"" + String(currentTemp, 1) + "\",";
    json += "\"feelsLike\":\"" + String(feelsLike, 1) + "\",";
    json += "\"tempMin\":\"" + String(tempMin, 1) + "\",";
    json += "\"tempMax\":\"" + String(tempMax, 1) + "\",";
    json += "\"humidity\":\"" + String(humidity, 0) + "\",";
    json += "\"pressure\":\"" + String(pressure, 0) + "\",";
    json += "\"windSpeed\":\"" + String(windSpeed * 3.6, 1) + "\",";
    json += "\"windDir\":\"" + getWindDir(windDeg) + "\",";
    json += "\"clouds\":\"" + String(clouds) + "\",";
    json += "\"sunrise\":\"" + timestampToLocalTime(sunriseTime) + "\",";
    json += "\"sunset\":\"" + timestampToLocalTime(sunsetTime) + "\",";
    json += "\"description\":\"" + weatherDescription + "\"";
    json += "}";
  }
  
  json += "}";
  
  server.send(200, "application/json", json);
  
  Serial.print("📦 JSON enviado (tamanho: ");
  Serial.print(json.length());
  Serial.println(" bytes)");
  
  // DEBUG: Mostrar parte do JSON
  if (json.length() > 100) {
    Serial.print("📋 JSON (primeiros 100 chars): ");
    Serial.println(json.substring(0, 100));
  }
}

void handleConfig() {
  Serial.println("📡 Requisição recebida: /config");
  
  // Página de configuração COM UTF-8
  String html = "<!DOCTYPE html>";
  html += "<html lang='pt-BR'>";
  html += "<head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Configuração do Relógio</title>";
  html += "<style>";
  html += "* { box-sizing: border-box; }";
  html += "body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; margin: 0; padding: 20px; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); min-height: 100vh; }";
  html += ".container { max-width: 800px; margin: 0 auto; background: white; padding: 30px; border-radius: 15px; box-shadow: 0 10px 30px rgba(0,0,0,0.2); }";
  html += "h1 { color: #2c3e50; margin-top: 0; border-bottom: 2px solid #3498db; padding-bottom: 10px; }";
  html += ".back-link { display: inline-flex; align-items: center; color: #3498db; text-decoration: none; font-weight: bold; margin-bottom: 20px; }";
  html += ".back-link:hover { color: #2980b9; text-decoration: underline; }";
  html += ".config-section { background: #f8f9fa; padding: 20px; border-radius: 10px; margin: 20px 0; border-left: 4px solid #3498db; }";
  html += "label { display: block; margin: 15px 0 5px; color: #2c3e50; font-weight: 600; }";
  html += "input, select { width: 100%; padding: 12px; border: 2px solid #ddd; border-radius: 6px; font-size: 16px; transition: border 0.3s; }";
  html += "input:focus, select:focus { border-color: #3498db; outline: none; box-shadow: 0 0 0 3px rgba(52, 152, 219, 0.2); }";
  html += ".form-group { margin-bottom: 20px; }";
  html += ".form-note { font-size: 14px; color: #7f8c8d; margin-top: 5px; }";
  html += ".btn { background: linear-gradient(to right, #3498db, #2ecc71); color: white; border: none; padding: 14px 28px; border-radius: 6px; font-size: 16px; font-weight: bold; cursor: pointer; transition: transform 0.2s, box-shadow 0.2s; }";
  html += ".btn:hover { transform: translateY(-2px); box-shadow: 0 5px 15px rgba(0,0,0,0.1); }";
  html += ".btn:active { transform: translateY(0); }";
  html += ".btn-save { background: linear-gradient(to right, #2ecc71, #27ae60); }";
  html += ".current-value { background: #e8f4fc; padding: 10px; border-radius: 5px; margin: 5px 0; border-left: 3px solid #3498db; }";
  html += "</style>";
  html += "</head>";
  html += "<body>";
  html += "<div class='container'>";
  html += "<a href='/' class='back-link'>← Voltar ao Monitor Principal</a>";
  html += "<h1>⚙️ Configurações do Relógio</h1>";
  
  // Seção de configurações do relógio
  html += "<div class='config-section'>";
  html += "<h2>🕒 Configurações de Horário</h2>";
  
  html += "<div class='current-value'>";
  html += "<strong>Hora Atual:</strong> " + formatTime();
  html += "</div>";
  
  html += "<form action='/update' method='POST'>";
  
  html += "<div class='form-group'>";
  html += "<label for='utc'>Fuso Horário (UTC Offset):</label>";
  html += "<input type='text' id='utc' name='utc' value='" + String(utcOffset, 1) + "' placeholder='ex: -3.0 para Brasília'>";
  html += "<div class='form-note'>Exemplos: -3.0 (Brasília), -4.0 (Manaus), -2.0 (Fernando de Noronha)</div>";
  html += "</div>";
  
  html += "<div class='form-group'>";
  html += "<label for='format12h'>Formato de Hora:</label>";
  html += "<select id='format12h' name='format12h'>";
  html += "<option value='true'" + String(is12HFormat ? " selected" : "") + ">12 horas (AM/PM)</option>";
  html += "<option value='false'" + String(!is12HFormat ? " selected" : "") + ">24 horas</option>";
  html += "</select>";
  html += "</div>";
  
  html += "<div class='form-group'>";
  html += "<label for='duration'>Duração do Relógio (segundos):</label>";
  html += "<input type='number' id='duration' name='duration' value='" + String(clockDisplayDuration) + "' min='10' max='300' step='5'>";
  html += "<div class='form-note'>Tempo que o relógio fica visível antes do scroll (10-300 segundos)</div>";
  html += "</div>";
  
  html += "<button type='submit' class='btn btn-save'>💾 Salvar Configurações</button>";
  html += "</form>";
  html += "</div>";
  
  // Seção de informações
  html += "<div class='config-section'>";
  html += "<h2>📋 Informações do Sistema</h2>";
  html += "<div class='current-value'>";
  html += "<strong>IP do Relógio:</strong> " + WiFi.localIP().toString();
  html += "</div>";
  html += "<div class='current-value'>";
  html += "<strong>SSID WiFi:</strong> " + WiFi.SSID();
  html += "</div>";
  html += "<div class='current-value'>";
  html += "<strong>Sinal WiFi:</strong> " + String(WiFi.RSSI()) + " dBm";
  html += "</div>";
  html += "<div class='current-value'>";
  html += "<strong>Clima Ativo:</strong> " + String(weatherEnabled ? "Sim" : "Não");
  html += "</div>";
  html += "</div>";
  
  // Link para portal completo do WiFiManager se necessário
  html += "<div style='text-align:center; margin-top:30px;'>";
  html += "<p><small>Para configurações avançadas (WiFi, Clima, DST), ";
  html += "reinicie no modo configuração pressionando o botão durante a inicialização.</small></p>";
  html += "</div>";
  
  html += "</div>";
  html += "</body>";
  html += "</html>";
  
  // ENVIAR COM CHARSET UTF-8
  server.sendHeader("Content-Type", "text/html; charset=utf-8");
  server.send(200, "text/html", html);
}

void handleUpdate() {
  Serial.println("📡 Requisição recebida: /update (POST)");
  
  String message = "<!DOCTYPE html><html lang='pt-BR'><head><meta charset='UTF-8'>";
  message += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  message += "<title>Configuração Salva</title>";
  message += "<style>";
  message += "body { font-family: Arial, sans-serif; margin: 40px; background: #f0f0f0; }";
  message += ".container { max-width: 500px; margin: 0 auto; background: white; padding: 30px; border-radius: 10px; text-align: center; }";
  message += ".success { color: #27ae60; font-size: 48px; }";
  message += "a { display: inline-block; margin-top: 20px; padding: 10px 20px; background: #3498db; color: white; text-decoration: none; border-radius: 5px; }";
  message += "</style>";
  message += "</head><body>";
  message += "<div class='container'>";
  message += "<div class='success'>✅</div>";
  message += "<h2>Configurações Salvas com Sucesso!</h2>";
  
  // Processar dados do formulário
  if (server.hasArg("utc")) {
    utcOffset = server.arg("utc").toFloat();
    message += "<p><strong>UTC Offset:</strong> " + String(utcOffset, 1) + "</p>";
  }
  if (server.hasArg("format12h")) {
    is12HFormat = server.arg("format12h") == "true";
    message += "<p><strong>Formato:</strong> " + String(is12HFormat ? "12 horas" : "24 horas") + "</p>";
  }
  if (server.hasArg("duration")) {
    clockDisplayDuration = server.arg("duration").toInt();
    if (clockDisplayDuration < 10) clockDisplayDuration = 10;
    message += "<p><strong>Duração do Relógio:</strong> " + String(clockDisplayDuration) + " segundos</p>";
  }
  
  // Atualizar estrutura EEPROM
  updateEEPROMStructure();
  saveSettings();
  
  message += "<p>As alterações foram salvas na memória permanente.</p>";
  message += "<a href='/'>Voltar ao Monitor</a>";
  message += "</div>";
  message += "</body></html>";
  
  server.sendHeader("Content-Type", "text/html; charset=utf-8");
  server.send(200, "text/html", message);
  
  // Pequeno delay para garantir salvamento
  delay(100);
}

void handleNotFound() {
  Serial.print("❌ Arquivo não encontrado: ");
  Serial.println(server.uri());
  
  String message = "Arquivo não encontrado\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void setupServer() {
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/config", handleConfig);
  server.on("/update", HTTP_POST, handleUpdate);
  server.onNotFound(handleNotFound);
  
  server.begin();
  Serial.println("✅ Servidor HTTP iniciado na porta 80");
}

/* ================== SETUP ================== */
void setup() {
  Serial.begin(74880);
  delay(4000);
  
  Serial.println("\n\n==========================================");
  Serial.println("🚀 Relógio LED Matrix ESP8266 - Iniciando");
  Serial.println("==========================================");

  loadSettings();
  applySettingsFromEEPROM();
  updateWiFiManagerBuffers();

  initMAX7219();
  clr();
  refreshAll();

  showChar('B', 5);
  showChar('O', 11);
  showChar('O', 17);
  showChar('T', 23);
  refreshAll();
  delay(1000);
  clr();
  refreshAll();

  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.setConfigPortalTimeout(180);

  wifiManager.addParameter(&pUtc);
  wifiManager.addParameter(&p12h);
  wifiManager.addParameter(&pDst);
  wifiManager.addParameter(&pDstMode);
  wifiManager.addParameter(&pDstExtra);
  wifiManager.addParameter(&pWeatherEnabled);
  wifiManager.addParameter(&pWeatherAPIKey);
  wifiManager.addParameter(&pWeatherLat);
  wifiManager.addParameter(&pWeatherLon);
  wifiManager.addParameter(&pWeatherLang);
  wifiManager.addParameter(&pWeatherUpdateInterval);
  wifiManager.addParameter(&pClockDisplayDuration);

  pinMode(CONFIG_BUTTON_PIN, INPUT_PULLUP);
  delay(100);

  bool configMode = showCountdown();
  
  if (configMode) {
    Serial.println("⚙️ Modo Configuração ativado via botão");
    
    showChar('C', 2);
    showChar('O', 7);
    showChar('N', 12);
    showChar('F', 18);
    showChar('I', 23);
    showChar('G', 27);
    refreshAll();
    delay(1000);

    WiFi.disconnect(true);
    delay(1000);

    Serial.println("📡 Iniciando Portal de Configuração WiFi...");
    Serial.println("🌐 Conecte-se à rede: 'Relogio NTP+Clima'");
    Serial.println("🌐 Acesse: 192.168.4.1 no navegador");

    wifiManager.startConfigPortal("Relogio NTP+Clima");

    clr();
    refreshAll();
    displayIPScroll();
  } else {
    Serial.println("🤖 Modo Automático ativado");
    
    showChar('A', 6);
    showChar('U', 11);
    showChar('T', 16);
    showChar('O', 22);
    refreshAll();
    delay(1000);
    clr();

    if (!wifiManager.autoConnect("Relogio NTP+Clima")) {
      Serial.println("❌ Falha na conexão WiFi. Iniciando modo configuração...");
      wifiManager.startConfigPortal("Relogio NTP+Clima");
      clr();
      refreshAll();
      displayIPScroll();
    } else {
      Serial.println("✅ Conectado ao WiFi com sucesso!");
      clr();
      refreshAll();
      displayIPScroll();
    }
  }

  timeClient.begin();
  delay(2000);

  if (timeClient.update()) {
    updateTime();
    Serial.println("✅ Sincronização NTP realizada com sucesso");
  } else {
    h = 0;
    m = 23;
    s = 0;
    Serial.println("⚠️ Falha na sincronização NTP. Usando hora interna");
  }

  if (weatherEnabled && weatherAPIKey.length() > 0) {
    updateWeatherData();
    Serial.println("🌤️ Dados meteorológicos atualizados");
  }

  updateInfoString();

  updateBrightness();

  clockStartTime = millis();
  showAnimClock();

  // Iniciar servidor web
  setupServer();
  
  Serial.println("\n==========================================");
  Serial.println("✅ SISTEMA INICIALIZADO COM SUCESSO!");
  Serial.println("==========================================");
  Serial.print("🌐 IP Conectado: ");
  Serial.println(WiFi.localIP());
  Serial.print("📶 SSID WiFi: ");
  Serial.println(WiFi.SSID());
  Serial.print("📡 Força do sinal: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
  Serial.println("==========================================");
  Serial.println("Acesse o monitor web:");
  Serial.print("👉 http://");
  Serial.println(WiFi.localIP());
  Serial.println("==========================================");
}

/* ================== LOOP ================== */
void loop() {
  static unsigned long lastClockUpdate = 0;
  static unsigned long lastNTPUpdate = millis();
  static unsigned long lastSerialPrint = 0;

  if (millis() - lastClockUpdate >= 1000) {
    lastClockUpdate = millis();

    s++;
    if (s >= 60) {
      s = 0;
      m++;
      if (m >= 60) {
        m = 0;
        h = (h + 1) % 24;
      }
    }

    dots = !dots;
    updateBrightness();
  }

  if (millis() - lastNTPUpdate >= 3600000) {
    lastNTPUpdate = millis();
    if (WiFi.status() == WL_CONNECTED) {
      updateTime();
      Serial.print("🔄 Sincronização NTP realizada: ");
      Serial.println(formatTime());
    }
  }

  // Mostrar status periódico no Serial Monitor
  if (millis() - lastSerialPrint >= 30000) { // A cada 30 segundos
    lastSerialPrint = millis();
    
    Serial.println("\n📊 STATUS DO SISTEMA:");
    Serial.print("   Hora: ");
    Serial.println(formatTime());
    Serial.print("   Modo: ");
    Serial.println(currentMode == MODE_CLOCK ? "Relógio" : "Scroll Info");
    Serial.print("   Brilho: ");
    Serial.print(currentBrightness);
    Serial.println("/15");
    Serial.print("   WiFi: ");
    Serial.println(WiFi.status() == WL_CONNECTED ? "Conectado" : "Desconectado");
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("   IP: ");
      Serial.println(WiFi.localIP());
    }
    Serial.println("==========================================");
  }

  checkModeTransition();

  if (currentMode == MODE_CLOCK) {
    showAnimClock();
  } else if (currentMode == MODE_INFO_SCROLL) {
    displayInfoScroll();
  }

  // Lidar com requisições do servidor web
  server.handleClient();

  delay(50);
}
