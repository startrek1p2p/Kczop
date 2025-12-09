// https://kczop-551b1.web.app //
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <LiquidCrystal_I2C.h>
#include "DHT.h"
#include <WiFi.h>
#include <time.h> // *** NTP ***
#include <WiFiClientSecure.h>
#include <FirebaseESP32.h>

// Informacje o generowaniu tokenu.
#include "addons/TokenHelper.h"
// Pomocnicze funkcje do RTDB i wypisywania danych.
#include "addons/RTDBHelper.h"

// Dane sieci Wi-Fi
#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASSWORD ""

#define Web_API_KEY "AIzaSyC454ZiHeXjjwMkIYqdZrABAMnzZ30-rmQ"
#define DATABASE_URL "https://kczop-551b1-default-rtdb.europe-west1.firebasedatabase.app/"

// Dane uzytkownika do logowania/signup - bez spacji
const char *USER_EMAIL = "krz.czop@gmail.com";
const char *USER_PASS = "12345678a";

FirebaseData firebaseData;

FirebaseConfig firebaseConfig;
FirebaseAuth firebaseAuth;

bool signupOK = false;
bool authReady = false;

String uid;

float temperature;
float humidity;
bool hasReading = false;

const unsigned long measureIntervalMs = 5000; // czestotliwosc pomiaru DHT22
const unsigned long sendIntervalMs = 30000;   // czestotliwosc wysylki do Firebase i zapisu na SD
unsigned long lastMeasureMs = 0;
unsigned long lastSendMs = 0;
// --- Buzzer ---
#define BUZZER_PIN 14
const unsigned long buzzerIntervalMs = 500; // 500 ms dzwiek / 500 ms ciszy
unsigned long lastBuzzerToggleMs = 0;
bool buzzerState = false;

// --- Konfiguracja DHT22 ---
#define DHTPIN 15     // pin danych czujnika DHT22
#define DHTTYPE DHT22 // typ czujnika

DHT dht(DHTPIN, DHTTYPE);

// --- Konfiguracja LCD I2C ---
#define LCD_ADDR 0x27 // jesli nie dziala, sprobuj 0x3F
#define LCD_COLS 16
#define LCD_ROWS 2

LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);

// --- Konfiguracja SD (SPI) ---
// CS  -> GPIO 5
// DI  -> GPIO 23 (MOSI)
// SCK -> GPIO 18
// DO  -> GPIO 19 (MISO)
#define APP_SD_CS_PIN 5
#define APP_SD_SCK_PIN 18
#define APP_SD_MISO_PIN 19
#define APP_SD_MOSI_PIN 23

const char *SD_FILE_NAME = "/pomiary.txt";
bool sdOk = false;

// --- Konfiguracja NTP / czasu lokalnego (Polska) ---
// *** NTP ***
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;  // UTC+1
const int daylightOffset_sec = 0; // mozesz ustawic 3600 jesli chcesz na stale +2h

void initTime() // *** NTP ***
{
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  Serial.print("Synchronizacja czasu z NTP");
  struct tm timeinfo;
  int retry = 0;
  const int maxRetry = 10;

  while (!getLocalTime(&timeinfo) && retry < maxRetry)
  {
    Serial.print(".");
    retry++;
    delay(500);
  }
  Serial.println();

  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Blad pobrania czasu z NTP!");
  }
  else
  {
    Serial.print("Czas zsynchronizowany: ");
    Serial.printf("%04d-%02d-%02d %02d:%02d:%02d\n",
                  timeinfo.tm_year + 1900,
                  timeinfo.tm_mon + 1,
                  timeinfo.tm_mday,
                  timeinfo.tm_hour,
                  timeinfo.tm_min,
                  timeinfo.tm_sec);
  }
}

void initSD()
{
  // Inicjalizacja SPI z konkretnymi pinami
  SPI.begin(APP_SD_SCK_PIN, APP_SD_MISO_PIN, APP_SD_MOSI_PIN, APP_SD_CS_PIN);

  if (!SD.begin(APP_SD_CS_PIN))
  {
    Serial.println("Blad inicjalizacji karty SD!");
    sdOk = false;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Blad karty SD");
    delay(2000);
  }
  else
  {
    sdOk = true;
    Serial.println("Karta SD OK.");

    // Jesli plik nie istnieje - utworz go i dodaj naglowek
    if (!SD.exists(SD_FILE_NAME))
    {
      File file = SD.open(SD_FILE_NAME, FILE_WRITE);
      if (file)
      {
        // Zaktualizowany naglowek z data
        file.println("czas- data, godzina, minuta, sekunda, Temperatura[C], Wilgotnosc[%]");
        file.close();
        Serial.println("Utworzono plik pomiary.txt z naglowkiem.");
      }
      else
      {
        Serial.println("Nie udalo sie utworzyc pliku pomiary.txt");
      }
    }
    else
    {
      Serial.println("Plik pomiary.txt juz istnieje.");
    }
  }
}

void logToSD(float temperature, float humidity, time_t epoch)
{
  if (!sdOk)
    return;

  String line;

  // Ujednolicony czas: ten sam epoch jak dla Firebase/web
  struct tm timeinfo;
  if (epoch > 0 && localtime_r(&epoch, &timeinfo))
  {
    char buf[80];
    // format: czas- data,godzina,minuta,sekunda
    snprintf(buf, sizeof(buf),
             "czas- data:%04d-%02d-%02d,godzina:%02d,minuta:%02d,sekunda:%02d",
             timeinfo.tm_year + 1900,
             timeinfo.tm_mon + 1,
             timeinfo.tm_mday,
             timeinfo.tm_hour,
             timeinfo.tm_min,
             timeinfo.tm_sec);
    line = String(buf);
  }
  else
  {
    // Fallback: gdy NTP nie dziala, uzyj czasu od startu
    unsigned long totalSeconds = millis() / 1000;
    unsigned long seconds = totalSeconds % 60;
    unsigned long minutes = (totalSeconds / 60) % 60;
    unsigned long hours = (totalSeconds / 3600) % 24;
    unsigned long days = totalSeconds / 86400;

    line = "czas- dzien:";
    line += days;
    line += ",godzina:";
    if (hours < 10)
      line += "0";
    line += hours;
    line += ",minuta:";
    if (minutes < 10)
      line += "0";
    line += minutes;
    line += ",sekunda:";
    if (seconds < 10)
      line += "0";
    line += seconds;
  }

  line += ", Temperatura:";
  line += String(temperature, 1);
  line += ", Wilgotnosc:";
  line += String(humidity, 1);

  File file = SD.open(SD_FILE_NAME, FILE_APPEND);
  if (!file)
  {
    // Jesli FILE_APPEND zawiedzie (np. plik nie istnieje) - sprobuj FILE_WRITE
    file = SD.open(SD_FILE_NAME, FILE_WRITE);
  }

  if (file)
  {
    file.println(line);
    file.close();
    Serial.println("Zapisano na SD: " + line);
  }
  else
  {
    Serial.println("Blad otwarcia pliku do zapisu na SD!");
  }
}

void setup()
{
  // Port szeregowy
  Serial.begin(115200);
  delay(500);

  // Inicjalizacja I2C i LCD przed pierwszym uzyciem
  Wire.begin(); // domyslne piny ESP32: SDA=21, SCL=22
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Start...");
  // Buzzer
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // Wi-Fi
  lcd.setCursor(0, 0);
  lcd.print("Laczenie z WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    lcd.setCursor(0, 1);
    lcd.print(".");
    delay(300);
  }
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi OK:");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP());
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();

  // *** NTP: inicjalizacja czasu po polaczeniu Wi-Fi ***
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Synchronizacja");
  lcd.setCursor(0, 1);
  lcd.print("czasu (NTP)...");
  initTime();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Czas OK");

  // Konfiguracja polaczenia z Firebase i danych uwierzytelniajacych
  firebaseConfig.database_url = DATABASE_URL;
  firebaseConfig.api_key = Web_API_KEY;
  firebaseAuth.user.email = USER_EMAIL;
  firebaseAuth.user.password = USER_PASS;

  /* Rejestracja lub logowanie istniejacego uzytkownika */
  if (Firebase.signUp(&firebaseConfig, &firebaseAuth, USER_EMAIL, USER_PASS))
  {
    Serial.println("Signup ok");
    signupOK = true;
    authReady = true;
  }
  else
  {
    Serial.printf("Signup error: %s\n", firebaseConfig.signer.signupError.message.c_str());

    // Jesli konto juz istnieje, sprobuje zalogowac sie tymi danymi
    if (String(firebaseConfig.signer.signupError.message.c_str()) == "EMAIL_EXISTS")
    {
      Serial.println("Konto istnieje – używam logowania na istniejące dane.");
      authReady = true;
    }
  }

  /* Callback statusu tokenu (generowanie dlugotrwale) */
  firebaseConfig.token_status_callback = tokenStatusCallback; // patrz addons/TokenHelper.h

  // Polacz z Firebase tylko gdy mamy dane autoryzacyjne
  if (authReady)
  {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Logowanie");
    lcd.setCursor(0, 1);
    lcd.print("do Firebase...");
    Firebase.begin(&firebaseConfig, &firebaseAuth);
    Firebase.reconnectWiFi(true);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Firebase OK");
  }
  else
  {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Brak autoryz.");
    lcd.setCursor(0, 1);
    lcd.print("Firebase pomin");
    Serial.println("Brak autoryzacji - pomijam Firebase.begin()");
  }

  // Inicjalizacja DHT
  dht.begin();
  Serial.println("Start pomiaru z DHT22.");

  // Inicjalizacja karty SD
  initSD();
}
void loop()
{
  unsigned long nowMs = millis();
  time_t now = time(nullptr);

  // Odczyt z czujnika co 5 s
  if (nowMs - lastMeasureMs >= measureIntervalMs)
  {
    lastMeasureMs = nowMs;
    float h = dht.readHumidity();
    float t = dht.readTemperature();

    if (isnan(h) || isnan(t))
    {
      hasReading = false;
      Serial.println("Blad odczytu z DHT22!");

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Blad odczytu");
      lcd.setCursor(0, 1);
      lcd.print("z DHT22...");
    }
    else
    {
      humidity = h;
      temperature = t;
      hasReading = true;

      Serial.print("Temperatura: ");
      Serial.print(temperature, 1);
      Serial.print(" *C  |  Wilgotnosc: ");
      Serial.print(humidity, 1);
      Serial.println(" %");

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("T:");
      lcd.print(temperature, 1);
      lcd.print((char)223); // znak stopnia
      lcd.print("C");

      lcd.setCursor(0, 1);
      lcd.print("H:");
      lcd.print(humidity, 1);
      lcd.print("%");
    }
  }

  // Alarm buzzer: ponizej 5°C lub powyzej 45°C, puls 500 ms
  bool alarmActive = hasReading && (temperature < 5.0 || temperature > 45.0);
  if (alarmActive)
  {
    if (nowMs - lastBuzzerToggleMs >= buzzerIntervalMs)
    {
      lastBuzzerToggleMs = nowMs;
      buzzerState = !buzzerState;
      digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW);
    }
  }
  else
  {
    buzzerState = false;
    digitalWrite(BUZZER_PIN, LOW);
  }

  // Wysylka i zapis co 30 s
  if (hasReading && (nowMs - lastSendMs >= sendIntervalMs))
  {
    lastSendMs = nowMs;

    if (authReady && Firebase.ready())
    {
      FirebaseJson json;
      json.set("temperature", temperature);
      json.set("humidity", humidity);

      if (now > 0)
        json.set("timestamp", (int)now);

      String path = "/UsersData/public/readings"; // zgodnie z frontendem
      if (Firebase.pushJSON(firebaseData, path, json))
        Serial.println("Reading sent");
      else
      {
        Serial.println("Failed to send reading");
        Serial.println(firebaseData.errorReason());
      }
    }
    else
    {
      Serial.println("Firebase not ready yet - skipping send");
    }

    logToSD(temperature, humidity, now);
  }

  delay(200);
}
