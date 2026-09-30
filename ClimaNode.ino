#include <DHT.h>
#include <LiquidCrystal.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <RotaryEncoder.h>

#include "Timer.h"
#include "Secrets.h"
#include "Dashboard.h"
#include "Button.h"

#define LCD_RS            19
#define LCD_EN            23
#define LCD_D4            18
#define LCD_D5            17
#define LCD_D6            16
#define LCD_D7            15
#define DHT_PIN           21
#define DHT_TYPE          DHT22
#define HTTP_PORT         80
#define MDNS_HOSTNAME     "climanode"
#define ROTARY_CLK        4
#define ROTARY_DT         22
#define ROTARY_SW         27

DHT dht(DHT_PIN, DHT_TYPE);
LiquidCrystal lcd(LCD_RS, LCD_EN, LCD_D4, LCD_D5, LCD_D6, LCD_D7);
RotaryEncoder rot(ROTARY_CLK, ROTARY_DT, RotaryEncoder::LatchMode::FOUR3);
Button btn(ROTARY_SW);
WiFiMulti wifiMulti;
WebServer httpServer(HTTP_PORT);
Timer linkTimer;
Timer pollTimer;

enum DisplayScreen {
  SCREEN_CLIMATE,
  SCREEN_NETWORK
};

const uint32_t connectTimeoutMs = 10000;
uint8_t wifiStatus = WL_NO_SHIELD;
bool mdnsStarted = false;
float humidity = 0.0f;
float temperature = 0.0f;
DisplayScreen currentScreen = SCREEN_CLIMATE;

void renderClimateScreen() {
  lcd.setCursor(0, 0);
  lcd.print("Tmp: ");
  lcd.print(temperature, 1);
  lcd.print(" C      ");

  lcd.setCursor(0, 1);
  lcd.print("Hum: ");
  lcd.print(humidity, 1);
  lcd.print(" %      ");
}

void renderNetworkScreen() {
  lcd.setCursor(0, 0);
  if (WiFi.status() == WL_CONNECTED) {
    lcd.print(WiFi.localIP().toString());
    lcd.print("                ");
  } else {
    lcd.print("IP non disp.    ");
  }

  lcd.setCursor(0, 1);
  lcd.print(WiFi.status() == WL_CONNECTED ? "WiFi: Connesso  " : "WiFi: Assente   ");
}

void renderDisplay() {
  switch (currentScreen) {
    case SCREEN_CLIMATE:
      renderClimateScreen();
      break;
    case SCREEN_NETWORK:
      renderNetworkScreen();
      break;
  }
}

void readEnvironment() {
  float newHumidity = dht.readHumidity();
  float newTemperature = dht.readTemperature();

  if (isnan(newHumidity) || isnan(newTemperature)) {
    Serial.println("Errore: lettura sensore non valida");
    if (currentScreen == SCREEN_CLIMATE) {
      lcd.setCursor(0, 0);
      lcd.print("Errore sensore  ");
      lcd.setCursor(0, 1);
      lcd.print("Dati non validi ");
    }
    return;
  }

  humidity = newHumidity;
  temperature = newTemperature;

#ifdef DHT_DEBUG
  Serial.print("Umidità: ");
  Serial.print(humidity, 1);
  Serial.print("%, Temperatura: ");
  Serial.print(temperature, 1);
  Serial.println("° Celsius");
#endif

  if (currentScreen == SCREEN_CLIMATE) {
    renderClimateScreen();
  }
}

void handleClimateRequest() {
  char payload[64];
  snprintf(payload, sizeof(payload),
           "{\"temperature\":%.1f,\"humidity\":%.1f}",
           temperature, humidity);
  httpServer.send(200, "application/json", payload);
}

void handleStatusRequest() {
  char payload[256];

  if (WiFi.status() == WL_CONNECTED) {
    snprintf(payload, sizeof(payload),
             "{\"wifi\":{\"connected\":true,\"ssid\":\"%s\",\"rssi\":%d,\"ip\":\"%s\",\"channel\":%d}}",
             WiFi.SSID().c_str(), WiFi.RSSI(), WiFi.localIP().toString().c_str(), WiFi.channel());
  } else {
    snprintf(payload, sizeof(payload), "{\"wifi\":{\"connected\":false}}");
  }

  httpServer.send(200, "application/json", payload);
}

void handleDashboardRequest() {
  httpServer.send(200, "text/html", DASHBOARD_HTML);
}

void startMdns() {
  if (mdnsStarted) {
    return;
  }

  if (MDNS.begin(MDNS_HOSTNAME)) {
    MDNS.addService("http", "tcp", HTTP_PORT);
    mdnsStarted = true;
    Serial.printf("[MDNS] Responder started: http://%s.local/\n", MDNS_HOSTNAME);
  } else {
    Serial.println("[MDNS] Error starting responder.");
  }
}

void setup() {
  Serial.begin(9600);
  dht.begin();
  btn.begin();
  lcd.begin(16, 2);
  WiFi.mode(WIFI_STA);
  wifiMulti.addAP(WIFI_SSID_1, WIFI_PASS_1);
  wifiMulti.addAP(WIFI_SSID_2, WIFI_PASS_2);
  linkTimer.begin(1000L);
  pollTimer.begin(2000L);

  httpServer.on("/", HTTP_GET, handleDashboardRequest);
  httpServer.on("/api/v1/climate", HTTP_GET, handleClimateRequest);
  httpServer.on("/api/v1/status", HTTP_GET, handleStatusRequest);
  httpServer.begin();
}

void loop() {
  static int pos = 0;

  if (linkTimer.expired()) {
    uint8_t status = wifiMulti.run(connectTimeoutMs);
    if (status != wifiStatus) {
			wifiStatus = status;
			switch (status) {
				case WL_CONNECTED:
					Serial.println("[WIFI] Connecting done.");
					Serial.printf("[WIFI] IP: %s\n", WiFi.localIP().toString().c_str());
					Serial.printf("[WIFI] MAC: %s\n", WiFi.macAddress().c_str());
					Serial.printf("[WIFI] SSID: %s\n", WiFi.SSID().c_str());
					Serial.printf("[WIFI] BSSID: %s\n", WiFi.BSSIDstr().c_str());
					Serial.printf("[WIFI] Channel: %d\n", WiFi.channel());
					startMdns();
					break;
				case WL_NO_SSID_AVAIL:
					Serial.println("[WIFI] Connecting Failed AP not found.");
					break;
				case WL_CONNECT_FAILED:
					Serial.println("[WIFI] Connecting Failed.");
					break;
				case WL_DISCONNECTED:
					Serial.println("[WIFI] Disconnected.");
					break;
				default:
					Serial.printf("[WIFI] Connecting Failed (%d).\n", status);
			}
			if (currentScreen == SCREEN_NETWORK) {
				renderNetworkScreen();
			}
		}
    linkTimer.reset();
  }

  if (pollTimer.expired()) {
    pollTimer.reset();
    readEnvironment();
  }

  rot.tick();
  int newPos = rot.getPosition();
  if (pos != newPos) {
    Serial.print("pos:");
    Serial.print(newPos);
    Serial.print(" dir:");
    Serial.println((int)(rot.getDirection()));
    pos = newPos;

    currentScreen = (currentScreen == SCREEN_CLIMATE) ? SCREEN_NETWORK : SCREEN_CLIMATE;
    lcd.clear();
    renderDisplay();
  }

  if (btn.pressed()) {
    Serial.println("Button PRESSED");
  }

  httpServer.handleClient();
}
