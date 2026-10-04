#include <DHT.h>
#include <LiquidCrystal.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <esp_mac.h>
#include <RotaryEncoder.h>
#include <TinyGPSPlus.h>
#include <sys/time.h>
#include "time.h"
#include "Timer.h"
#include "Secrets.h"
#include "Dashboard.h"
#include "Button.h"
#include "Trend.h"
#include "Glyphs.h"

#define LCD_RS            19
#define LCD_EN            23
#define LCD_D4            18
#define LCD_D5            17
#define LCD_D6            16
#define LCD_D7            15
#define LCD_BACKLIGHT     26
#define DHT_PIN           21
#define DHT_TYPE          DHT22
#define HTTP_PORT         80
#define HOSTNAME_PREFIX   "climanode"
#define FIRMWARE_VERSION  "1.0a"
#define ROTARY_CLK        4
#define ROTARY_DT         22
#define ROTARY_SW         27
#define BACKLIGHT_TIMEOUT 30000L  // ms di inattività prima di spegnere la retroilluminazione
// GPIO34 è solo ingresso e non è un pin di strapping: adatto all'RX dal GPS
#define GPS_RX            34
#define GPS_TX            13
#define GPS_BAUD          9600
#define GPS_MAX_AGE       3000  // ms oltre i quali un dato del GPS è considerato vecchio

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
Timer trendTimer;
Timer backlightTimer;
Timer gpsTimer;
TinyGPSPlus gps;
Trend temperatureTrend(0.3f);  // °C in TREND_WINDOW_MIN minuti
Trend humidityTrend(2.0f);     // % in TREND_WINDOW_MIN minuti
// Copia delle tendenze per gli handler HTTP, aggiornata dal loop() a ogni commit
volatile TrendDirection temperatureDirection = TREND_UNKNOWN;
volatile TrendDirection humidityDirection = TREND_UNKNOWN;

enum DisplayScreen {
  SCREEN_CLIMATE,
  SCREEN_NETWORK,
  SCREEN_SIGNAL,
  SCREEN_TIME,
  SCREEN_GPS,
  SCREEN_NODE,
  SCREEN_FIRMWARE,
  SCREEN_COUNT
};

// Stato del GPS aggiornato dal loop() e letto anche dagli handler HTTP
struct GpsInfo {
  bool fix;
  uint32_t satellites;
  double latitude;
  double longitude;
  double altitude;  // m s.l.m.
  double hdop;
};

GpsInfo gpsInfo = {};
portMUX_TYPE gpsMux = portMUX_INITIALIZER_UNLOCKED;

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
char nodeId[13];    // MAC di fabbrica (eFuse) in esadecimale, es. "a1b2c3d4e5f6"
char hostname[24];  // HOSTNAME_PREFIX + ultimi 3 byte del MAC, es. "climanode-d4e5f6"
float humidity = 0.0f;
float temperature = 0.0f;
DisplayScreen currentScreen = SCREEN_CLIMATE;
bool backlightOn = true;

uint8_t trendSymbol(TrendDirection trend) {
  switch (trend) {
    case TREND_UP:
      return ICON_TREND_UP;
    case TREND_DOWN:
      return ICON_TREND_DOWN;
    case TREND_STEADY:
      return LCD_RIGHT_ARROW;
    default:
      return ' ';
  }
}

// Scrive una riga " <icona>  <valore>    <tendenza> " di 16 caratteri
void printClimateLine(uint8_t row, uint8_t icon, const char *value, TrendDirection trend) {
  lcd.setCursor(0, row);
  lcd.print(' ');
  lcd.write(icon);
  lcd.printf(" %-11s", value);
  lcd.write(trendSymbol(trend));
  lcd.print(' ');
}

void renderClimateScreen() {
  char value[14];

  snprintf(value, sizeof(value), "%5.1f%cC", temperature, LCD_DEGREE);
  printClimateLine(0, ICON_THERMOMETER, value, temperatureTrend.direction());

  snprintf(value, sizeof(value), "%5.1f%%", humidity);
  printClimateLine(1, ICON_DROP, value, humidityTrend.direction());
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

// Soglie RSSI allineate a quelle delle barre della dashboard
const char *signalQuality(int rssi) {
  if (rssi >= -55) return "ottimo";
  if (rssi >= -65) return "buono";
  if (rssi >= -75) return "discreto";
  return "scarso";
}

void renderSignalScreen() {
  char line[17];

  if (WiFi.status() != WL_CONNECTED) {
    lcd.setCursor(0, 0);
    lcd.print("Segnale WiFi    ");
    lcd.setCursor(0, 1);
    lcd.print("Non connesso    ");
    return;
  }

  int rssi = WiFi.RSSI();
  snprintf(line, sizeof(line), "Segnale %-8s", signalQuality(rssi));
  lcd.setCursor(0, 0);
  lcd.print(line);

  // Barra di 9 celle da -90 dBm (vuota) a -30 dBm (piena), poi il valore in dBm
  const int barCells = 9;
  int filled = constrain(map(rssi, -90, -30, 0, barCells), 0, barCells);
  for (int i = 0; i < barCells; i++) {
    line[i] = i < filled ? LCD_FULL_BLOCK : LCD_MIDDLE_DOT;
  }
  snprintf(line + barCells, sizeof(line) - barCells, "%4ddBm", rssi);
  lcd.setCursor(0, 1);
  lcd.print(line);
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

GpsInfo readGpsInfo() {
  taskENTER_CRITICAL(&gpsMux);
  GpsInfo info = gpsInfo;
  taskEXIT_CRITICAL(&gpsMux);
  return info;
}

void renderGpsScreen() {
  GpsInfo info = readGpsInfo();
  char line[17];

  if (!info.fix) {
    lcd.setCursor(0, 0);
    lcd.print("GPS: ricerca... ");
    snprintf(line, sizeof(line), "Satelliti: %-5lu", (unsigned long)info.satellites);
    lcd.setCursor(0, 1);
    lcd.print(line);
    return;
  }

  snprintf(line, sizeof(line), "Lat: %9.5f %c", fabs(info.latitude), info.latitude < 0 ? 'S' : 'N');
  lcd.setCursor(0, 0);
  lcd.print(line);

  snprintf(line, sizeof(line), "Lon: %9.5f %c", fabs(info.longitude), info.longitude < 0 ? 'W' : 'E');
  lcd.setCursor(0, 1);
  lcd.print(line);
}

void renderNodeScreen() {
  char line[17];
  snprintf(line, sizeof(line), "ID: %-12.12s", nodeId);
  lcd.setCursor(0, 0);
  lcd.print(line);

  snprintf(line, sizeof(line), "%-16.16s", hostname);
  lcd.setCursor(0, 1);
  lcd.print(line);
}

void renderFirmwareScreen() {
  char line[17];
  lcd.setCursor(0, 0);
  lcd.print("ClimaNode       ");

  snprintf(line, sizeof(line), "Firmware %-7s", FIRMWARE_VERSION);
  lcd.setCursor(0, 1);
  lcd.print(line);
}

// Riaccende la retroilluminazione e riavvia il timer di spegnimento
void wakeBacklight() {
  backlightTimer.reset();
  if (!backlightOn) {
    backlightOn = true;
    digitalWrite(LCD_BACKLIGHT, HIGH);
  }
}

void renderDisplay() {
  switch (currentScreen) {
    case SCREEN_CLIMATE:
      renderClimateScreen();
      break;
    case SCREEN_NETWORK:
      renderNetworkScreen();
      break;
    case SCREEN_SIGNAL:
      renderSignalScreen();
      break;
    case SCREEN_TIME:
      renderTimeScreen();
      break;
    case SCREEN_GPS:
      renderGpsScreen();
      break;
    case SCREEN_NODE:
      renderNodeScreen();
      break;
    case SCREEN_FIRMWARE:
      renderFirmwareScreen();
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
  humidityTrend.add(humidity);
  temperatureTrend.add(temperature);

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

const char *trendName(TrendDirection trend) {
  switch (trend) {
    case TREND_UP:
      return "up";
    case TREND_DOWN:
      return "down";
    case TREND_STEADY:
      return "steady";
    default:
      return "unknown";
  }
}

void handleClimateRequest(AsyncWebServerRequest *request) {
  char location[96];
  char payload[256];

  // Posizione della misura; null finché il GPS non ha il fix
  GpsInfo info = readGpsInfo();
  if (info.fix) {
    snprintf(location, sizeof(location), "{\"latitude\":%.6f,\"longitude\":%.6f,\"altitude\":%.1f}",
             info.latitude, info.longitude, info.altitude);
  } else {
    snprintf(location, sizeof(location), "null");
  }

  snprintf(payload, sizeof(payload),
           "{\"id\":\"%s\",\"temperature\":%.1f,\"humidity\":%.1f,"
           "\"trend\":{\"temperature\":\"%s\",\"humidity\":\"%s\"},\"location\":%s}",
           nodeId, temperature, humidity,
           trendName(temperatureDirection), trendName(humidityDirection), location);
  request->send(200, "application/json", payload);
}

void handleStatusRequest(AsyncWebServerRequest *request) {
  char wifi[320];
  char clock[64];
  char receiver[64];
  char payload[640];

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

  GpsInfo info = readGpsInfo();
  if (info.fix) {
    snprintf(receiver, sizeof(receiver), "{\"fix\":true,\"satellites\":%lu,\"hdop\":%.1f}",
             (unsigned long)info.satellites, info.hdop);
  } else {
    snprintf(receiver, sizeof(receiver), "{\"fix\":false,\"satellites\":%lu}",
             (unsigned long)info.satellites);
  }

  snprintf(payload, sizeof(payload),
           "{\"id\":\"%s\",\"firmware\":\"%s\",\"wifi\":%s,\"time\":%s,\"gps\":%s}",
           nodeId, FIRMWARE_VERSION, wifi, clock, receiver);
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

  if (MDNS.begin(hostname)) {
    MDNS.addService("http", "tcp", HTTP_PORT);
    MDNS.addServiceTxt("http", "tcp", "id", (const char *)nodeId);
    mdnsStarted = true;
    Serial.printf("[MDNS] Responder started: http://%s.local/\n", hostname);
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

// Secondi dal 1/1/1970 di una data UTC (algoritmo days_from_civil di H. Hinnant)
time_t utcToEpoch(int year, int month, int day, int hour, int minute, int second) {
  year -= month <= 2;
  int era = year / 400;
  int yoe = year - era * 400;
  int doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long days = era * 146097L + doe - 719468;
  return (time_t)days * 86400 + hour * 3600 + minute * 60 + second;
}

// Se l'orologio non è ancora stato impostato (es. NTP non raggiungibile) usa l'ora del GPS
void syncClockFromGps() {
  struct tm timeInfo;
  if (getLocalTime(&timeInfo, 0)) {
    return;
  }
  // Senza fix data e ora possono essere vuote o quelle del RTC interno del modulo
  if (!gps.location.isValid() || gps.time.age() > GPS_MAX_AGE || gps.date.year() < 2024) {
    return;
  }

  struct timeval now = {
    .tv_sec = utcToEpoch(gps.date.year(), gps.date.month(), gps.date.day(),
                         gps.time.hour(), gps.time.minute(), gps.time.second()),
    .tv_usec = 0
  };
  settimeofday(&now, nullptr);
  Serial.println("[GPS] Clock set from GPS.");
}

void updateGpsInfo() {
  GpsInfo info = {};
  info.fix = gps.location.isValid() && gps.location.age() < GPS_MAX_AGE;
  info.satellites = gps.satellites.isValid() ? gps.satellites.value() : 0;
  if (info.fix) {
    info.latitude = gps.location.lat();
    info.longitude = gps.location.lng();
    info.altitude = gps.altitude.meters();
    info.hdop = gps.hdop.hdop();
  }

  taskENTER_CRITICAL(&gpsMux);
  gpsInfo = info;
  taskEXIT_CRITICAL(&gpsMux);

#ifdef GPS_DEBUG
  Serial.printf("[GPS] fix: %d, sat: %lu, lat: %.6f, lon: %.6f, chars: %lu, bad checksum: %lu\n",
                info.fix, (unsigned long)info.satellites, info.latitude, info.longitude,
                (unsigned long)gps.charsProcessed(), (unsigned long)gps.failedChecksum());
#endif
}

// Ricava ID e hostname dal MAC di fabbrica, unico per ogni chip
void initNodeIdentity() {
  uint8_t mac[6];
  esp_efuse_mac_get_default(mac);
  snprintf(nodeId, sizeof(nodeId), "%02x%02x%02x%02x%02x%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  snprintf(hostname, sizeof(hostname), HOSTNAME_PREFIX "-%02x%02x%02x", mac[3], mac[4], mac[5]);
}

void setup() {
  Serial.begin(9600);
  // Margine per le pause del loop(): a 9600 baud il GPS manda circa 1 KB/s
  Serial1.setRxBufferSize(1024);
  Serial1.begin(GPS_BAUD, SERIAL_8N1, GPS_RX, GPS_TX);
  pinMode(LCD_BACKLIGHT, OUTPUT);
  digitalWrite(LCD_BACKLIGHT, HIGH);
  dht.begin();
  btn.begin();
  lcd.begin(16, 2);
  lcd.createChar(ICON_THERMOMETER, thermometerGlyph);
  lcd.createChar(ICON_DROP, dropGlyph);
  lcd.createChar(ICON_TREND_UP, trendUpGlyph);
  lcd.createChar(ICON_TREND_DOWN, trendDownGlyph);
  initNodeIdentity();
  Serial.printf("[NODE] ID: %s, hostname: %s, firmware: %s\n", nodeId, hostname, FIRMWARE_VERSION);
  // Va impostato prima di avviare il WiFi per essere usato anche dal DHCP
  WiFi.setHostname(hostname);
  WiFi.mode(WIFI_STA);
  // Radio sempre attiva: senza modem sleep la latenza scende da ~100 ms a pochi ms
  WiFi.setSleep(false);
  // Con un indirizzo IPv6 link-local l'mDNS risponde anche alle query AAAA,
  // evitando ai client 5 s di attesa nella risoluzione di <hostname>.local
  WiFi.enableIPv6();
  // La riconnessione è gestita da updateWifiLink()
  WiFi.setAutoReconnect(false);
  // Fuso orario impostato subito, così vale anche per l'ora presa dal GPS prima dell'NTP
  setenv("TZ", TIME_ZONE, 1);
  tzset();
  startWifiScan();
  linkTimer.begin(1000L);
  pollTimer.begin(2000L);
  clockTimer.begin(250L);
  trendTimer.begin(60000L);
  backlightTimer.begin(BACKLIGHT_TIMEOUT);
  gpsTimer.begin(1000L);

  httpServer.on("/", HTTP_GET, handleDashboardRequest);
  httpServer.on("/api/v1/climate", HTTP_GET, handleClimateRequest);
  httpServer.on("/api/v1/status", HTTP_GET, handleStatusRequest);
  httpServer.begin();
}

void loop() {
  static int pos = 0;

  if (backlightOn && backlightTimer.expired()) {
    backlightOn = false;
    digitalWrite(LCD_BACKLIGHT, LOW);
  }

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
    if (currentScreen == SCREEN_SIGNAL) {
      renderSignalScreen();
    }
    linkTimer.reset();
  }

  if (pollTimer.expired()) {
    pollTimer.reset();
    readEnvironment();
  }

  if (trendTimer.expired()) {
    trendTimer.reset();
    temperatureTrend.commit();
    humidityTrend.commit();
    temperatureDirection = temperatureTrend.direction();
    humidityDirection = humidityTrend.direction();
    if (currentScreen == SCREEN_CLIMATE) {
      renderClimateScreen();
    }
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
    // Con il display spento la rotazione serve solo a riaccenderlo
    bool wasOn = backlightOn;
    wakeBacklight();
    if (wasOn) {
      int step = (newPos > pos) ? 1 : SCREEN_COUNT - 1;
      currentScreen = (DisplayScreen)((currentScreen + step) % SCREEN_COUNT);
      lcd.clear();
      renderDisplay();
    }
    pos = newPos;

#ifdef ROT_DEBUG
    Serial.print("pos:");
    Serial.print(newPos);
    Serial.print(" dir:");
    Serial.println((int)(rot.getDirection()));
#endif
  }

  if (btn.pressed()) {
    wakeBacklight();
    Serial.println("Button PRESSED");
  }

  // Il parser va alimentato a ogni giro per non perdere caratteri
  while (Serial1.available() > 0) {
    gps.encode(Serial1.read());
  }

  if (gpsTimer.expired()) {
    gpsTimer.reset();
    updateGpsInfo();
    syncClockFromGps();
    if (currentScreen == SCREEN_GPS) {
      renderGpsScreen();
    }
  }
}
