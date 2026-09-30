# ClimaNode

Nodo climatico basato su ESP32-WROOM-32: legge temperatura e umidità da un sensore DHT22, le mostra su un display LCD 16x2 ed espone gli ultimi valori rilevati tramite una semplice API HTTP in JSON.

## Hardware

- Scheda ESP32-WROOM-32
- Sensore DHT22
- Display LCD 16x2 (interfaccia parallela, driver HD44780 compatibile con `LiquidCrystal`)

### Collegamenti

| Componente      | Pin ESP32 |
|-----------------|-----------|
| DHT22 (data)    | GPIO 21   |
| LCD RS          | GPIO 19   |
| LCD E           | GPIO 23   |
| LCD D4          | GPIO 18   |
| LCD D5          | GPIO 17   |
| LCD D6          | GPIO 16   |
| LCD D7          | GPIO 15   |

## Librerie richieste

Da installare tramite Library Manager dell'Arduino IDE:

- **DHT sensor library** (Adafruit) — con la sua dipendenza **Adafruit Unified Sensor**
- **LiquidCrystal** (libreria standard, inclusa con l'Arduino IDE)

Già incluse nel core ESP32 (nessuna installazione necessaria, basta avere il core ESP32 installato tramite Boards Manager):

- `WiFi`
- `WiFiMulti`
- `WebServer`
- `ESPmDNS`

## Setup credenziali WiFi

Le credenziali WiFi non sono versionate nel repository. Prima di compilare:

1. Copia il file di esempio:
   ```sh
   cp Secrets.h.example Secrets.h
   ```
2. Modifica `Secrets.h` inserendo SSID e password delle reti WiFi disponibili (sono supportate fino a due reti, gestite con fallback automatico da `WiFiMulti`):
   ```cpp
   #define WIFI_SSID_1  "NomeRete1"
   #define WIFI_PASS_1  "Password1"

   #define WIFI_SSID_2  "NomeRete2"
   #define WIFI_PASS_2  "Password2"
   ```

`Secrets.h` è ignorato da git (vedi `.gitignore`) e non verrà mai inviato al repository remoto.

## Upload

Apri `ClimaNode.ino` con l'Arduino IDE, seleziona la board ESP32-WROOM-32 e la porta seriale corretta, quindi carica lo sketch.

Al boot il device si connette al WiFi (log disponibile su Serial a 9600 baud) e, una volta connesso, avvia anche il responder mDNS, raggiungibile come `climanode.local` sulla rete locale.

## API

Una volta connesso alla rete, il device espone un endpoint HTTP con gli ultimi valori rilevati:

```
GET /api/v1/climate
```

Risposta:

```json
{"temperature":21.5,"humidity":48.2}
```

Esempio con `curl`, usando l'IP stampato su Serial oppure l'hostname mDNS:

```sh
curl http://climanode.local/api/v1/climate
```
