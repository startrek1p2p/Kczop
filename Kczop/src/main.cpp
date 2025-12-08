#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "DHT.h"

// --- Konfiguracja DHT22 ---
#define DHTPIN 15      // pin danych czujnika DHT22
#define DHTTYPE DHT22  // typ czujnika

DHT dht(DHTPIN, DHTTYPE);

// --- Konfiguracja LCD I2C ---
#define LCD_ADDR 0x27  // jeśli nie działa, spróbuj 0x3F
#define LCD_COLS 16
#define LCD_ROWS 2

LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);

// --- Opcjonalnie: piny I2C dla ESP32 (domyślnie SDA=21, SCL=22) ---
// Jeśli masz podłączone SDA/SCL inaczej, zmień tutaj i odkomentuj Wire.begin():
// #define I2C_SDA 21
// #define I2C_SCL 22

void setup() {
  // Port szeregowy
  Serial.begin(115200);
  delay(500);

  // Inicjalizacja I2C
  // Wire.begin(I2C_SDA, I2C_SCL);  // jeśli chcesz jawnie ustawić piny
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
}

void loop() {
  // Odczyt z czujnika
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature(); // domyślnie *C

  // Sprawdzenie czy odczyt sie udal
  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("Blad odczytu z DHT22!");

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Blad odczytu");
    lcd.setCursor(0, 1);
    lcd.print("z DHT22...");

  } else {
    // Wyswietlanie na porcie szeregowym
    Serial.print("Temperatura: ");
    Serial.print(temperature, 1);
    Serial.print(" *C  |  Wilgotnosc: ");
    Serial.print(humidity, 1);
    Serial.println(" %");

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
  }

  // Odczekaj 2 sekundy do kolejnego pomiaru
  delay(2000);
}
