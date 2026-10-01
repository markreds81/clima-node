#include <DHT.h>
#include <LiquidCrystal.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <RotaryEncoder.h>
#include "time.h"
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

#define NTP_SERVER        "ntp1.inrim.it"
// Europe/Rome: CET (UTC+1), CEST (UTC+2) dall'ultima domenica di marzo all'ultima di ottobre
#define TIME_ZONE         "CET-1CEST,M3.5.0,M10.5.0/3"

DHT dht(DHT_PIN, DHT_TYPE);
LiquidCrystal lcd(LCD_RS, LCD_EN, LCD_D4, LCD_D5, LCD_D6, LCD_D7);
RotaryEncoder rot(ROTARY_CLK, ROTARY_DT, RotaryEncoder::LatchMode::FOUR3);
Button btn(ROTARY_SW);
AsyncWebServer httpServer(HTTP_PORT);
Timer linkTimer;
Timer pollTimer;
Timer clockTimer;

enum DisplayScreen {
  SCREEN_CLIMATE,
  SCREEN_NETWORK,
  SCREEN_TIME,
  SCREEN_COUNT
};

struct KnownNetwork {
  const char *ssid;
  const char *pass;
};

const KnownNetwork knownNetworks[] = {
  { WIFI_SSID_1, WIFI_PASS_1 },
  { WIFI_SSID_2, WIFI_PASS_2 }
};

// Stati della connessione WiFi, gestita senza bloccare il loop()
enum LinkState {
  LINK_IDLE,        // in attesa del prossimo tentativo
  LINK_SCANNING,    // scansione asincrona in corso
  LINK_CONNECTING,  // WiFi.begin() avviato, in attesa dell'esito
  LINK_CONNECTED
};

const uint32_t connectTimeoutMs = 10000;
const uint32_t scanTimeoutMs = 15000;
const uint32_t retryDelayMs = 5000;
LinkState linkState = LINK_IDLE;
uint32_t linkStateSince = 0;
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

void renderTimeScreen() {
  struct tm timeInfo;
  // timeout 0: non bloccare il loop finché l'orario non è sincronizzato
  if (!getLocalTime(&timeInfo, 0)) {
    lcd.setCursor(0, 0);
    lcd.print("Ora non sincr.  ");
    lcd.setCursor(0, 1);
    lcd.print("Attesa NTP...   ");
    return;
  }

  char line[17];
  strftime(line, sizeof(line), "   %d/%m/%Y   ", &timeInfo);
  lcd.setCursor(0, 0);
  lcd.print(line);

  strftime(line, sizeof(line), "    %H:%M:%S    ", &timeInfo);
  lcd.setCursor(0, 1);
  lcd.print(line);
}

void renderDisplay() {
  switch (currentScreen) {
    case SCREEN_CLIMATE:
      renderClimateScreen();
      break;
    case SCREEN_NETWORK:
      renderNetworkScreen();
      break;
    case SCREEN_TIME:
      renderTimeScreen();
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

// Copia src in dst come contenuto di una stringa JSON (senza virgolette esterne)
void jsonEscape(const char *src, char *dst, size_t size) {
  size_t n = 0;
  for (; *src && n + 7 <= size; src++) {
    unsigned char c = *src;
    if (c == '"' || c == '\\') {
      dst[n++] = '\\';
      dst[n++] = c;
    } else if (c < 0x20) {
      n += snprintf(dst + n, size - n, "\\u%04x", c);
    } else {
      dst[n++] = c;
    }
  }
  dst[n] = '\0';
}

void handleClimateRequest(AsyncWebServerRequest *request) {
  char payload[64];
  snprintf(payload, sizeof(payload),
           "{\"temperature\":%.1f,\"humidity\":%.1f}",
           temperature, humidity);
  request->send(200, "application/json", payload);
}

void handleStatusRequest(AsyncWebServerRequest *request) {
  char wifi[320];
  char clock[64];
  char payload[400];

  if (WiFi.status() == WL_CONNECTED) {
    char ssid[32 * 6 + 1];  // SSID max 32 byte, ognuno al più \u00XX
    jsonEscape(WiFi.SSID().c_str(), ssid, sizeof(ssid));
    snprintf(wifi, sizeof(wifi),
             "{\"connected\":true,\"ssid\":\"%s\",\"rssi\":%d,\"ip\":\"%s\",\"channel\":%d}",
             ssid, WiFi.RSSI(), WiFi.localIP().toString().c_str(), WiFi.channel());
  } else {
    snprintf(wifi, sizeof(wifi), "{\"connected\":false}");
  }

  // Ora locale del device (fuso TIME_ZONE) in formato ISO 8601 senza offset
  struct tm timeInfo;
  if (getLocalTime(&timeInfo, 0)) {
    char local[20];
    strftime(local, sizeof(local), "%Y-%m-%dT%H:%M:%S", &timeInfo);
    snprintf(clock, sizeof(clock), "{\"synced\":true,\"local\":\"%s\"}", local);
  } else {
    snprintf(clock, sizeof(clock), "{\"synced\":false}");
  }

  snprintf(payload, sizeof(payload), "{\"wifi\":%s,\"time\":%s}", wifi, clock);
  request->send(200, "application/json", payload);
}

void handleDashboardRequest(AsyncWebServerRequest *request) {
  // Servita direttamente dalla flash, senza copiarla in RAM
  request->send(200, "text/html", (const uint8_t *)DASHBOARD_HTML, sizeof(DASHBOARD_HTML) - 1);
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

void setLinkState(LinkState state) {
  linkState = state;
  linkStateSince = millis();
}

void startWifiScan() {
  WiFi.disconnect();
  WiFi.scanNetworks(true);
  setLinkState(LINK_SCANNING);
}

// Sceglie tra le reti note quella con il segnale migliore e avvia la connessione
void connectBestNetwork(int found) {
  int bestIndex = -1;
  const KnownNetwork *bestNetwork = nullptr;

  for (int i = 0; i < found; i++) {
    for (const KnownNetwork &network : knownNetworks) {
      if (WiFi.SSID(i) == network.ssid && (bestIndex < 0 || WiFi.RSSI(i) > WiFi.RSSI(bestIndex))) {
        bestIndex = i;
        bestNetwork = &network;
      }
    }
  }

  if (bestNetwork == nullptr) {
    Serial.println("[WIFI] Connecting Failed AP not found.");
    setLinkState(LINK_IDLE);
  } else {
    Serial.printf("[WIFI] Connecting to %s (%d dBm)...\n", bestNetwork->ssid, WiFi.RSSI(bestIndex));
    WiFi.begin(bestNetwork->ssid, bestNetwork->pass, WiFi.channel(bestIndex), WiFi.BSSID(bestIndex));
    setLinkState(LINK_CONNECTING);
  }
  WiFi.scanDelete();
}

void updateWifiLink() {
  uint32_t elapsed = millis() - linkStateSince;

  switch (linkState) {
    case LINK_IDLE:
      if (elapsed >= retryDelayMs) {
        startWifiScan();
      }
      break;
    case LINK_SCANNING: {
      int found = WiFi.scanComplete();
      if (found >= 0) {
        connectBestNetwork(found);
      } else if (found == WIFI_SCAN_FAILED || elapsed >= scanTimeoutMs) {
        Serial.println("[WIFI] Scan failed.");
        WiFi.scanDelete();
        setLinkState(LINK_IDLE);
      }
      break;
    }
    case LINK_CONNECTING:
      if (WiFi.status() == WL_CONNECTED) {
        setLinkState(LINK_CONNECTED);
      } else if (elapsed >= connectTimeoutMs) {
        Serial.println("[WIFI] Connecting Failed (timeout).");
        WiFi.disconnect();
        setLinkState(LINK_IDLE);
      }
      break;
    case LINK_CONNECTED:
      if (WiFi.status() != WL_CONNECTED) {
        startWifiScan();
      }
      break;
  }
}

void setup() {
  Serial.begin(9600);
  dht.begin();
  btn.begin();
  lcd.begin(16, 2);
  WiFi.mode(WIFI_STA);
  // Radio sempre attiva: senza modem sleep la latenza scende da ~100 ms a pochi ms
  WiFi.setSleep(false);
  // Con un indirizzo IPv6 link-local l'mDNS risponde anche alle query AAAA,
  // evitando ai client 5 s di attesa nella risoluzione di climanode.local
  WiFi.enableIPv6();
  // La riconnessione è gestita da updateWifiLink()
  WiFi.setAutoReconnect(false);
  startWifiScan();
  linkTimer.begin(1000L);
  pollTimer.begin(2000L);
  clockTimer.begin(250L);

  httpServer.on("/", HTTP_GET, handleDashboardRequest);
  httpServer.on("/api/v1/climate", HTTP_GET, handleClimateRequest);
  httpServer.on("/api/v1/status", HTTP_GET, handleStatusRequest);
  httpServer.begin();
}

void loop() {
  static int pos = 0;

  if (linkTimer.expired()) {
    updateWifiLink();
    uint8_t status = WiFi.status();
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
					configTzTime(TIME_ZONE, NTP_SERVER);
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

  if (clockTimer.expired()) {
    clockTimer.reset();
    if (currentScreen == SCREEN_TIME) {
      renderTimeScreen();
    }
  }

  rot.tick();
  int newPos = rot.getPosition();
  if (pos != newPos) {
    Serial.print("pos:");
    Serial.print(newPos);
    Serial.print(" dir:");
    Serial.println((int)(rot.getDirection()));
    int step = (newPos > pos) ? 1 : SCREEN_COUNT - 1;
    pos = newPos;

    currentScreen = (DisplayScreen)((currentScreen + step) % SCREEN_COUNT);
    lcd.clear();
    renderDisplay();
  }

  if (btn.pressed()) {
    Serial.println("Button PRESSED");
  }
}
