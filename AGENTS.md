# AGENTS.md

Note e promemoria per chi lavora sul codice di ClimaNode.

## Server HTTP (ESPAsyncWebServer)

Gli handler HTTP (`handleClimateRequest`, `handleStatusRequest`, `handleDashboardRequest`, ...) vengono eseguiti nel task di AsyncTCP, **in parallelo al `loop()`**, non al suo interno.

- Negli handler limitarsi a leggere lo stato (es. i `float` di temperatura e umidità, lo stato del WiFi) e a rispondere subito.
- Non usare l'LCD, non fare operazioni bloccanti o lunghe (`delay()`, letture del DHT22, chiamate di rete sincrone) e non chiamare `getLocalTime()` con un timeout diverso da 0.
- Se una richiesta deve scatenare un'azione (aggiornare il display, leggere il sensore, cambiare configurazione), impostare un flag (`volatile bool`) o accodare un comando nell'handler e farlo eseguire al `loop()`.
- Se un handler deve leggere dati composti da più campi che il `loop()` aggiorna, proteggerli (es. con un mutex o una sezione critica) per evitare letture incoerenti.
