# ClimaNode

Nodo climatico basato su ESP32-WROOM-32: legge temperatura e umidità da un sensore DHT22, le mostra su un display LCD 16x2 ed espone gli ultimi valori rilevati tramite una semplice API HTTP in JSON e una dashboard web.

## Hardware

- Scheda ESP32-WROOM-32
- Sensore DHT22
- Display LCD 16x2 (interfaccia parallela, driver HD44780 compatibile con `LiquidCrystal`)
- Encoder rotativo con pulsante integrato (es. KY-040)

### Collegamenti

| Componente        | Pin ESP32 |
|-------------------|-----------|
| DHT22 (data)      | GPIO 21   |
| LCD RS            | GPIO 19   |
| LCD E             | GPIO 23   |
| LCD D4            | GPIO 18   |
| LCD D5            | GPIO 17   |
| LCD D6            | GPIO 16   |
| LCD D7            | GPIO 15   |
| Encoder CLK       | GPIO 4    |
| Encoder DT        | GPIO 22   |
| Encoder SW (push) | GPIO 27   |

## Librerie richieste

Da installare tramite Library Manager dell'Arduino IDE:

- **DHT sensor library** (Adafruit) — con la sua dipendenza **Adafruit Unified Sensor**
- **LiquidCrystal** (libreria standard, inclusa con l'Arduino IDE)
- **RotaryEncoder** (Matthias Hertel)
- **ESP Async WebServer** (ESP32Async) — con la sua dipendenza **Async TCP** (ESP32Async)

Incluse direttamente nel progetto (nessuna installazione necessaria):

- `Button` ([mrButton](https://github.com/markreds81/Button) di Mark Reds, vendorizzata come `Button.h`/`Button.cpp`)

Già incluse nel core ESP32 (nessuna installazione necessaria, basta avere il core ESP32 installato tramite Boards Manager):

- `WiFi`
- `ESPmDNS`

## Setup credenziali WiFi

Le credenziali WiFi non sono versionate nel repository. Prima di compilare:

1. Copia il file di esempio:
   ```sh
   cp Secrets.h.example Secrets.h
   ```
2. Modifica `Secrets.h` inserendo SSID e password delle reti WiFi disponibili (sono supportate fino a due reti, il nodo si connette a quella con il segnale migliore e, se la connessione cade, riprova automaticamente senza bloccare display e sensore):
   ```cpp
   #define WIFI_SSID_1  "NomeRete1"
   #define WIFI_PASS_1  "Password1"

   #define WIFI_SSID_2  "NomeRete2"
   #define WIFI_PASS_2  "Password2"
   ```

`Secrets.h` è ignorato da git (vedi `.gitignore`) e non verrà mai inviato al repository remoto.

## Upload

Apri `ClimaNode.ino` con l'Arduino IDE, seleziona la board ESP32-WROOM-32 e la porta seriale corretta, quindi carica lo sketch.

Al boot il device si connette al WiFi (log disponibile su Serial a 9600 baud) e, una volta connesso, avvia anche il responder mDNS, raggiungibile come `climanode-XXXXXX.local` sulla rete locale.

## Identificazione del nodo

Ogni nodo è identificato dal MAC address di fabbrica dell'ESP32 (scritto nell'eFuse, unico per chip e indipendente dal firmware):

- **ID**: il MAC in esadecimale minuscolo, es. `a1b2c3d4e5f6`. È incluso in tutte le risposte dell'API (campo `id`) e nel record TXT `id` del servizio mDNS `_http._tcp`.
- **Hostname**: `climanode-` seguito dagli ultimi 3 byte del MAC, es. `climanode-d4e5f6`. È usato sia per mDNS (`climanode-d4e5f6.local`) sia come nome DHCP, così più nodi possono convivere sulla stessa rete.

ID e hostname sono stampati su Serial all'avvio. Per elencare i nodi presenti sulla rete da macOS:

```sh
dns-sd -B _http._tcp
```

## Display LCD

Il display mostra sei schermate, che si scorrono ruotando l'encoder in entrambe le direzioni:

- **Clima**: temperatura e umidità rilevate dal DHT22, indicate da due icone (termometro e goccia). In fondo a ogni riga una freccia indica la tendenza: ↑ in salita, ↓ in discesa, → stabile, calcolata confrontando la media dell'ultimo minuto con quella di 10 minuti prima (soglie: 0,3 °C e 2% di umidità). Nei primi minuti dopo l'avvio, finché lo storico non è sufficiente, la freccia non viene mostrata.
- **Rete**: indirizzo IP e stato della connessione WiFi.
- **Segnale**: qualità del segnale WiFi (ottimo, buono, discreto, scarso), una barra proporzionale e il valore RSSI in dBm, aggiornati ogni secondo.
- **Data e ora**: data e ora correnti, sincronizzate via NTP (fuso Europe/Rome con ora legale automatica).
- **Nodo**: ID del nodo (MAC di fabbrica) e hostname.
- **Firmware**: versione del firmware installato.

Il pulsante integrato nell'encoder è collegato (GPIO 27) ma al momento non ha alcuna funzione.

## Dashboard web

Una volta connesso alla rete, aprendo `http://climanode-XXXXXX.local/` (o l'IP stampato su Serial) nel browser è disponibile una dashboard single-page che mostra ID del nodo, versione del firmware, data e ora del device, temperatura e umidità con la relativa tendenza, e stato della connessione WiFi (SSID, potenza del segnale, IP, canale), aggiornata automaticamente ogni 3 secondi.

## API

Il device espone anche due endpoint HTTP in JSON, usati dalla dashboard ma interrogabili anche direttamente:

```
GET /api/v1/climate
```

```json
{"id":"a1b2c3d4e5f6","temperature":21.5,"humidity":48.2,"trend":{"temperature":"up","humidity":"steady"}}
```

`trend` indica la tendenza di ciascun valore negli ultimi 10 minuti (come le frecce sul display): `up`, `down`, `steady`, oppure `unknown` nei primi minuti dopo l'avvio.

```
GET /api/v1/status
```

```json
{"id":"a1b2c3d4e5f6","firmware":"1.0a","wifi":{"connected":true,"ssid":"NomeRete","rssi":-58,"ip":"192.168.1.50","channel":6},"time":{"synced":true,"local":"2026-10-01T14:05:32"}}
```

Se il device non è connesso al WiFi, `wifi.connected` risulta `false` e gli altri campi sono assenti. `time.local` è l'ora locale del device in formato ISO 8601 senza offset; finché l'ora non è sincronizzata via NTP `time.synced` risulta `false` e `local` è assente.

Esempio con `curl`, usando l'IP stampato su Serial oppure l'hostname mDNS:

```sh
curl http://climanode-d4e5f6.local/api/v1/climate
curl http://climanode-d4e5f6.local/api/v1/status
```

## Licenza

Distribuito sotto licenza [MIT](LICENSE).
