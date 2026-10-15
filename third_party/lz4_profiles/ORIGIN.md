# Getestete LZ4-Profile – eingebettet

Die vier TOML-Profile hier hat **danthespal** geschrieben und an der Konsole getestet; veröffentlicht wurden sie mit
**MkPFS.CSharp** (GPL-3.0, <https://github.com/danthespal/MkPFS.CSharp>). Übernommen sind sie unverändert aus dem Ordner
`profiles/` von **PS5 LZ4 Compression 1.5.0** (theghostonline / Husam Osman, GPL-3.0-or-later,
<https://github.com/theghostonline/PS5-LZ4-Compression>), dort mitgeliefert seit 1.4.0. `index.json` ist die Liste dieses
Projekts: welches Profil zu welchem Spiel passt (Titel-ID, Wörter des Namens, Engine nach dem Dateiaufbau); geändert sind
darin nur zwei Anzeigenamen („Insomniac engine“ → „Insomniac-Engine“, „Unity engine“ → „Unity-Engine“).

Vom Nutzer am 15.10.2026 als Zip bereitgestellt; geprüft: alle vier liest der TOML-Leser der App aus der Datei und aus dem
Speicher gleich (Regeln und Gruppen), keines lädt Listen nach (`include_from` / `exclude_from`).

| Datei | Passt zu | Getestet an | Beitrag | SHA-256 (Anfang) |
|---|---|---|---|---|
| astrobot.toml | ASTRO BOT | ASTRO BOT (PPSA21567, 01.019.000) | [MkPFS.CSharp #5](https://github.com/danthespal/MkPFS.CSharp/issues/5) | 640b0fa3efd75799 |
| ufc6-PPSA23566.toml | EA Sports UFC 6 | EA SPORTS UFC 6 (PPSA23566, 01.004.000) | [MkPFS.CSharp #2](https://github.com/danthespal/MkPFS.CSharp/issues/2) | f0ca9faf75c77123 |
| insomniac.toml | Insomniac-Spiele (`toc`, `dag` und ein Ordner `d/` oben im Spiel) | Marvel's Wolverine (PPSA03671, 01.001.005) | [MkPFS.CSharp #4](https://github.com/danthespal/MkPFS.CSharp/issues/4) | 8c51f840275b6242 |
| unity.toml | Unity-Spiele (`StreamingAssets` neben Unitys `Data`-Dateien) | God of War Sons of Sparta (PPSA28997, 01.008.001) | [MkPFS.CSharp #3](https://github.com/danthespal/MkPFS.CSharp/issues/3) | 3f038f039575b962 |

`tools/gen_lz4_profiles.py` bettet sie als Text ein (`gen/lz4prof_blob.c`, rund 30 KB), `src/imgjob_lz4.inc` bietet sie
im Fenster „LZ4-Asset-Packs“ an. Jedes Profil lässt Dateien lose, die es nicht nennt: Eine andere Fassung des Spiels packt
damit weniger, bleibt aber sicher. Ein Profil, das die Person selbst für das Spiel ablegt (zum Beispiel aus einem
Mitschnitt), geht vor.
