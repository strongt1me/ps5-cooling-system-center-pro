# PatchDL (Knutwurst)

Quelle: <https://github.com/knutwurst/ps5-patchdl>, Lizenz GPL-3.0; das Projekt selbst steht unter GPL-3.0-or-later (`LICENSE`
im Hauptordner).

Die Funktion „Spiel-Updates“, für die PatchDL ab 0.3.0 portiert worden war, ist am 29.10.2026 wieder aus der App entfernt
worden (Wunsch des Users). Geblieben ist der Netzwerkteil, den der Payload-Store (`src/paystore.c`) benutzt:

| Original (0.3.0) | Hier | Änderung |
|---|---|---|
| `src/patchdl_dns.c` | `src/upd_dns.c` | eigene Namensauflösung (Cloudflare, Google, Quad9, notfalls über HTTPS) mit unveränderter Logik; die DNS-Einstellungen der Konsole bleiben unberührt |
| der libcurl-Abruf in `patchdl_net.c` | `src/upd_http.c` | dasselbe von Hand über BearSSL (TLS 1.2): Adressen aus `upd_dns.c`, Wiederholung, große Empfangsfenster |
| `src/patchdl_ca.h`, `src/patchdl_doh_ca.h` | `src/upd_ca.h` (über `tools/mk_upd_ta.py`) | dieselben öffentlichen Stammzertifikate als BearSSL-Vertrauensanker |
