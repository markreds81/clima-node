# AGENTS.md

Note e promemoria per chi lavora sul codice di ClimaNode.

## Compilazione da shell

Su macOS `arduino-cli` non è nel PATH: si usa quello incluso nell'Arduino IDE. Va indicata la cartella delle librerie dello sketchbook (`~/Developer/Arduino/libraries`), altrimenti non trova `DHT.h` e le altre librerie installate.

```sh
CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
"$CLI" compile --fqbn esp32:esp32:esp32 --libraries ~/Developer/Arduino/libraries .
```

- Core: `esp32:esp32` 3.3.x, installato dal Boards Manager dell'IDE.
- Per installare una libreria dove la vede anche l'IDE, usare la configurazione dell'IDE:
  `"$CLI" --config-file ~/.arduinoIDE/arduino-cli.yaml lib install "<nome libreria>"`
- Serve un `Secrets.h` (copia di `Secrets.h.example`), altrimenti la compilazione fallisce.

## Server HTTP (ESPAsyncWebServer)

Gli handler HTTP (`handleClimateRequest`, `handleStatusRequest`, `handleDashboardRequest`, ...) vengono eseguiti nel task di AsyncTCP, **in parallelo al `loop()`**, non al suo interno.

- Negli handler limitarsi a leggere lo stato (es. i `float` di temperatura e umidità, lo stato del WiFi) e a rispondere subito.
- Non usare l'LCD, non fare operazioni bloccanti o lunghe (`delay()`, letture del DHT22, chiamate di rete sincrone) e non chiamare `getLocalTime()` con un timeout diverso da 0.
- Se una richiesta deve scatenare un'azione (aggiornare il display, leggere il sensore, cambiare configurazione), impostare un flag (`volatile bool`) o accodare un comando nell'handler e farlo eseguire al `loop()`.
- Se un handler deve leggere dati composti da più campi che il `loop()` aggiorna, proteggerli (es. con un mutex o una sezione critica) per evitare letture incoerenti.
