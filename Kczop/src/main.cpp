#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <LiquidCrystal_I2C.h>
#include "DHT.h"
#include <WiFi.h>
#include <time.h> // *** NTP ***
#include <WiFiClientSecure.h>
// #include <FirebaseClient.h>
#include <FirebaseESP32.h>

// Provide the token generation process info.
#include "addons/TokenHelper.h"
// Provide the RTDB payload printing info and other helper functions.
#include "addons/RTDBHelper.h"

#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

// Insert your network credentials
#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASSWORD ""

#define Web_API_KEY "AIzaSyC454ZiHeXjjwMkIYqdZrABAMnzZ30-rmQ"
#define DATABASE_URL "https://kczop-551b1-default-rtdb.europe-west1.firebasedatabase.app/"

// Dane użytkownika do logowania/signup – bez spacji
const char *USER_EMAIL = "dajcz.przemek@gmail.com";
const char *USER_PASS = "!Qasde32w";

// // User function
// void processData(AsyncResult &aResult);

// // Authentication
// UserAuth user_auth(Web_API_KEY, USER_EMAIL);

// // Firebase components
// FirebaseApp app;
// WiFiClientSecure ssl_client;
// using AsyncClient = AsyncClientClass;
// AsyncClient aClient(ssl_client);
// RealtimeDatabase Database;

// Timer variables for sending data every 10 seconds
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 10000; // 10 seconds in milliseconds

FirebaseData firebaseData;

FirebaseConfig firebaseConfig;
FirebaseAuth firebaseAuth;

unsigned long sendDataPrevMillis = 0;
int count = 0;
bool signupOK = false;
bool authReady = false;

String uid;

// Database main path (to be updated in setup with the user UID)
String databasePath;
// Database child nodes
String tempPath = "/temperature";
String humPath = "/humidity";
String timePath = "/timestamp";

// Parent Node (to be updated in every loop)
String parentPath;

int timestamp;

float temperature;
float humidity;

// --- Konfiguracja DHT22 ---
#define DHTPIN 15     // pin danych czujnika DHT22
#define DHTTYPE DHT22 // typ czujnika

DHT dht(DHTPIN, DHTTYPE);

// --- Konfiguracja LCD I2C ---
#define LCD_ADDR 0x27 // jeśli nie działa, spróbuj 0x3F
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

// --- Konfiguracja NTP / czasu lokalnego (Polska) ---
// *** NTP ***
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;  // UTC+1
const int daylightOffset_sec = 0; // możesz ustawić 3600 jeśli chcesz na stałe +2h

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
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Blad karty SD");
    delay(2000);
  }
  else
  {
    Serial.println("Karta SD OK.");

    // Jeśli plik nie istnieje – utwórz go i dodaj nagłówek
    if (!SD.exists(SD_FILE_NAME))
    {
      File file = SD.open(SD_FILE_NAME, FILE_WRITE);
      if (file)
      {
        // zaktualizowany nagłówek z datą
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

void logToSD(float temperature, float humidity)
{
  String line;

  // *** NTP: proba pobrania aktualnego czasu ***
  struct tm timeinfo;
  if (getLocalTime(&timeinfo))
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
    // fallback jeśli NTP nie zadziałało – czas od startu
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
    // Jesli FILE_APPEND zawiedzie (np. plik nie istnieje) – sprobuj FILE_WRITE
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

  // Wi-Fi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();

  // *** NTP: inicjalizacja czasu po polaczeniu Wi-Fi ***
  initTime();

  // // Configure SSL client
  // ssl_client.setInsecure();
  // ssl_client.setConnectionTimeout(1000);
  // ssl_client.setHandshakeTimeout(5);

  // // Initialize Firebase
  // initializeApp(aClient, app, getAuth(user_auth), processData, "🔐 authTask");
  // app.getApp<RealtimeDatabase>(Database);
  // Database.url(DATABASE_URL);

  //////////////

  // Set Firebase configuration and authentication details
  firebaseConfig.database_url = DATABASE_URL;
  firebaseConfig.api_key = Web_API_KEY;
  firebaseAuth.user.email = USER_EMAIL;
  firebaseAuth.user.password = USER_PASS;

  /* Sign up (lub zaloguj istniejącego) */
  if (Firebase.signUp(&firebaseConfig, &firebaseAuth, USER_EMAIL, USER_PASS))
  {
    Serial.println("Signup ok");
    signupOK = true;
    authReady = true;
  }
  else
  {
    Serial.printf("Signup error: %s\n", firebaseConfig.signer.signupError.message.c_str());

    // Jeśli konto już istnieje, spróbujemy zalogować się tymi danymi
    if (String(firebaseConfig.signer.signupError.message.c_str()) == "EMAIL_EXISTS")
    {
      Serial.println("Konto istnieje – używam logowania na istniejące dane.");
      authReady = true;
    }
  }

  /* Assign the callback function for the long running token generation task */
  firebaseConfig.token_status_callback = tokenStatusCallback; // see addons/TokenHelper.h

  // Connect to Firebase tylko gdy mamy dane autoryzacyjne
  if (authReady)
  {
    Firebase.begin(&firebaseConfig, &firebaseAuth);
    Firebase.reconnectWiFi(true);
  }
  else
  {
    Serial.println("Brak autoryzacji - pomijam Firebase.begin()");
  }

  // Inicjalizacja I2C
  Wire.begin(); // domyślne piny ESP32: SDA=21, SCL=22

  // Inicjalizacja LCD
  lcd.init();
  lcd.backlight();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("ESP32 + DHT22");
  lcd.setCursor(0, 1);
  lcd.print("Start...");
  delay(2000);

  // Inicjalizacja DHT
  dht.begin();
  Serial.println("Start pomiaru z DHT22.");

  // Inicjalizacja karty SD
  initSD();
}
// Function that gets current epoch time
unsigned long getTime()
{
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    // Serial.println("Failed to obtain time");
    return (0);
  }
  time(&now);
  return now;
}

void loop()
{
  // Odczyt z czujnika
  humidity = dht.readHumidity();
  temperature = dht.readTemperature(); // domyślnie *C
  // timestamp = getTime();
  // Serial.print("time: ");
  // Serial.println(timestamp);

  // Sprawdzenie czy odczyt sie udal
  if (isnan(humidity) || isnan(temperature))
  {
    Serial.println("Blad odczytu z DHT22!");

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Blad odczytu");
    lcd.setCursor(0, 1);
    lcd.print("z DHT22...");
  }
  else
  {
    // Wyswietlanie na porcie szeregowym
    Serial.print("Temperatura: ");
    Serial.print(temperature, 1);
    Serial.print(" *C  |  Wilgotnosc: ");
    Serial.print(humidity, 1);
    Serial.println(" %");

    // zapisz dany uzytkownika po jego UID, zgodnie z regułami RTDB
    if (authReady && Firebase.ready())
    {
      FirebaseJson json;
      json.set("temperature", temperature);
      json.set("humidity", humidity);

      time_t now = time(nullptr);
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
    // Wyswietlanie na LCD
    lcd.clear();

    // Linia 1: temperatura
    lcd.setCursor(0, 0);
    lcd.print("T:");
    lcd.print(temperature, 1);
    lcd.print((char)223); // znak stopnia
    lcd.print("C");

    // Linia 2: wilgotnosc
    lcd.setCursor(0, 1);
    lcd.print("H:");
    lcd.print(humidity, 1);
    lcd.print("%");

    // --- Zapis na karte SD ---
    logToSD(temperature, humidity);
  }

  // Odczekaj 2 sekundy do kolejnego pomiaru
  delay(2000);
}

// void processData(AsyncResult &aResult)
// {
//   if (!aResult.isResult())
//     return;

//   if (aResult.isEvent())
//     Firebase.printf("Event task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.eventLog().message().c_str(), aResult.eventLog().code());

//   if (aResult.isDebug())
//     Firebase.printf("Debug task: %s, msg: %s\n", aResult.uid().c_str(), aResult.debug().c_str());

//   if (aResult.isError())
//     Firebase.printf("Error task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.error().message().c_str(), aResult.error().code());

//   if (aResult.available())
//     Firebase.printf("task: %s, payload: %s\n", aResult.uid().c_str(), aResult.c_str());
// }
