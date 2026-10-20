<div align="center">

<img src="web/img/app-mark.png" alt="" width="96" height="96">

# PS5 Cooling & System Center - Pro

**Controllo della ventola, monitoraggio della temperatura e centro di sistema per la PlayStation 5 con jailbreak,<br>usabile nel browser della console o da qualsiasi dispositivo della rete domestica, in sei lingue.**

![Versione](https://img.shields.io/badge/Versione-1.71.2-1f6feb)
![Licenza](https://img.shields.io/badge/Licenza-GPL--3.0--or--later-blue)
![Piattaforma](https://img.shields.io/badge/Piattaforma-PS5%20Payload-003791)
![Lingue](https://img.shields.io/badge/Lingue-DE%20%C2%B7%20EN%20%C2%B7%20IT%20%C2%B7%20ES%20%C2%B7%20FR%20%C2%B7%20RU-lightgrey)

[Funzioni](#funzioni) · [Avvio rapido](#avvio-rapido) · [Requisiti](#requisiti) · [Utilizzo](#utilizzo) · [Documentazione](#documentazione) · [Note legali](#note-legali-ed-esclusione-di-responsabilità) · [Riconoscimenti](#riconoscimenti-e-ringraziamenti)

[Deutsch](README.md) · [English](README.en.md) · **Italiano** · [Español](README.es.md) · [Français](README.fr.md) · [Русский](README.ru.md)

<img src="docs/images/kuehlung.jpg" alt="Raffreddamento: temperatura, ventola, gioco in corso, temperatura obiettivo e modalità" width="860">

</div>

Un payload homebrew (ELF) che legge i sensori di temperatura della PS5, regola la ventola secondo una curva di comfort silenziosa e regolabile e include un'interfaccia web. In più: gestione di giochi, file, payload e profilo. Tutto gira sulla console stessa, senza PC e senza Internet.

## Funzioni

<table>
  <tr>
    <td width="33%" valign="top">
      <h3>🌡️ Raffreddamento</h3>
      Regolazione silenziosa della ventola su una temperatura obiettivo (60–91 °C), modalità, profili per gioco, modalità notte con obiettivo più silenzioso, autotest della ventola, limite di sicurezza all'hardware.
    </td>
    <td width="33%" valign="top">
      <h3>📊 Valori</h3>
      CPU, chip principale, GPU (Pro), ventola, carico dei core, frequenze, linee di alimentazione, frame rate e batteria del controller, con storico.
    </td>
    <td width="33%" valign="top">
      <h3>🎮 Giochi</h3>
      Avviare, chiudere, copiare, spostare, convertire in exFAT/ffpkg/ffpfsc, asset pack LZ4, APR-EMU, PlayGo.
    </td>
  </tr>
  <tr>
    <td valign="top">
      <h3>💾 PS4 da USB</h3>
      Giocare ai PKG falsi PS4 direttamente dall'unità USB senza installarli (PS4Mount integrato), con aggiornamenti e DLC.
    </td>
    <td valign="top">
      <h3>📦 Pacchetti e salvataggi</h3>
      Trovare, dividere e installare PKG; salvare e ripristinare i salvataggi; tempo di gioco con temperature.
    </td>
    <td valign="top">
      <h3>📁 File e payload</h3>
      File manager per console e unità; scaricare payload (l'elenco del Payload Manager), avviarli e fermarli, caricarli in sequenza come profili.
    </td>
  </tr>
  <tr>
    <td valign="top">
      <h3>🖥️ Sistema e registro</h3>
      Modello, firmware, rete, schermo; registro eventi e log del kernel in diretta con registrazione.
    </td>
    <td valign="top">
      <h3>🕹️ Comandi dal controller</h3>
      L1/R1 cambiano pagina, L2/R2 scorrono; premere due volte Mute mostra temperatura e ventola sulla TV.
    </td>
    <td valign="top">
      <h3>🚀 Riquadro di avvio</h3>
      Una cartella per <code>/data/homebrew</code>: un tocco sul riquadro avvia l'app e apre l'interfaccia.
    </td>
  </tr>
</table>

### Schermate

<table>
  <tr>
    <td width="50%"><img src="docs/images/spiele.jpg" alt="Libreria dei giochi"></td>
    <td width="50%"><img src="docs/images/ps4-von-usb.jpg" alt="Giochi PS4 da USB"></td>
  </tr>
  <tr>
    <td align="center"><sub>Giochi: libreria con avvio, copia, conversione</sub></td>
    <td align="center"><sub>PS4 da USB: PKG falsi senza installazione</sub></td>
  </tr>
  <tr>
    <td><img src="docs/images/spielzeit.jpg" alt="Tempo di gioco"></td>
    <td><img src="docs/images/dateimanager.jpg" alt="File manager"></td>
  </tr>
  <tr>
    <td align="center"><sub>Tempo di gioco con grafico giornaliero e temperature</sub></td>
    <td align="center"><sub>File manager per console e unità USB</sub></td>
  </tr>
  <tr>
    <td><img src="docs/images/payloads.jpg" alt="Payload"></td>
    <td><img src="docs/images/system.jpg" alt="Sistema"></td>
  </tr>
  <tr>
    <td align="center"><sub>Payload: profili, in esecuzione e salvati</sub></td>
    <td align="center"><sub>Sistema: console, rete, schermo</sub></td>
  </tr>
</table>

<details>
<summary><b>Altre schermate</b> (raffreddamento in dettaglio, salvataggi, pacchetti, profilo, registro, smartphone)</summary>
<br>
<table>
  <tr>
    <td width="50%"><img src="docs/images/kuehlung-diagramme.jpg" alt="Andamento della temperatura, sensori e profili per gioco"></td>
    <td width="50%"><img src="docs/images/spielstaende.jpg" alt="Backup dei salvataggi"></td>
  </tr>
  <tr>
    <td><img src="docs/images/pakete.jpg" alt="Installare pacchetti"></td>
    <td><img src="docs/images/profil.jpg" alt="Profilo con 30 immagini integrate"></td>
  </tr>
  <tr>
    <td><img src="docs/images/protokoll.jpg" alt="Registro"></td>
    <td align="center"><img src="docs/images/kuehlung-mobil.jpg" alt="Raffreddamento sullo smartphone" width="240"></td>
  </tr>
</table>
<sub>Tutte le immagini con dati di esempio e giochi inventati; l'interfaccia è mostrata in tedesco.</sub>
</details>

<details>
<summary><b>Tutte le funzioni in dettaglio</b></summary>

**Raffreddamento**
- Mantiene una temperatura obiettivo (predefinita 66 °C, regolabile 60–91 °C) con media mobile, zona morta e tendenza, e cambia la velocità solo a piccoli passi.
- Modalità silenziosa, bilanciata e fresca, scelte rapide e regole per gioco.
- Oltre la temperatura di sicurezza (predefinita 78 °C) decide solo l'hardware; se l'app non è in esecuzione, la console regola da sola.
- Modalità notte: tra due orari vale un obiettivo più silenzioso; l'orario è quello del dispositivo su cui è aperta la pagina.
- Autotest della ventola con un pulsante (la ventola risponde alla soglia?) e velocità minima della console appresa (20 % a riposo, 34 % in gioco), mostrata nella diagnostica.

**Giochi**
- Avviare (dal browser della console la pagina si chiude subito), chiudere, copiare su altre unità, spostare o estrarre con ShadowMountPlus.
- Convertire in immagini exFAT, ffpkg e ffpfsc senza PC; tutto viene riletto e verificato dopo la scrittura, con un file `.sha256`.
- Asset pack LZ4, indice AMPR, APR-EMU, stub PlayGo e moduli DLC per gioco; verificare, estrarre e riparare immagini.
- PS4 da USB: PKG falsi PS4 da un'unità USB nella libreria senza installarli (PS4Mount di gasb07).
- Tempo di gioco con temperature, backup e ripristino dei salvataggi, trovare, dividere e installare pacchetti, cronologia di tutte le operazioni.

**Gestione**
- File manager: visualizzare, scaricare e caricare, creare cartelle, rinominare, copiare, spostare, eliminare (modifiche solo sulle unità e in `/data`).
- Payload: vedere e fermare quelli in esecuzione, avviare i propri `.elf` da una cartella o da una chiavetta USB, profili come sequenza di avvio; scaricare e aggiornare i payload dall'elenco del Payload Manager di itsPLK (con controllo SHA-256).
- Profilo: nome visualizzato e immagine del profilo (30 immagini integrate o un file proprio).
- Tema: standard, chiaro, scuro e ottimale (il più veloce da disegnare, pensato per il televisore); ogni dispositivo ricorda la sua scelta.
- Sistema: modello, firmware, tempo di attività, memoria, rete; sensori grezzi e diagnostica in modalità esperto.
- Intestazione su ogni pagina: modalità riposo, riavvio, spegnimento, modalità provvisoria (due clic ciascuno).

**Tecnica**
- Server HTTP proprio senza librerie esterne, [interfaccia JSON](docs/API.md) documentata, impostazioni in `/data/PS5-Cooling-Center/config.json`.
- Nessun accesso a Internet, a meno che non attivi gli **aggiornamenti dei giochi** (disattivati di serie): allora l'app interroga i server di download di Sony e, per le patch precedenti, prosperopatches.com o orbispatches.com sugli aggiornamenti dei tuoi giochi. Allo stesso modo «Scarica payload» prende l'elenco e i file da GitHub solo premendo il pulsante. Altrimenti parla solo con programmi sulla console e con i browser della rete domestica.

</details>

## Avvio rapido

1. Scaricare `PS5_Cooling_System_Center_v<versione>.elf` dalle [Release](../../releases).
2. Inviarlo alla console (loader ELF, porta **9021**), per esempio:
   ```bash
   nc -q0 <IP-PS5> 9021 < PS5_Cooling_System_Center_v1.71.2.elf
   ```
3. Aprire **`http://<IP-PS5>:8086`** nel browser (l'indirizzo compare anche nella notifica in alto a sinistra sulla TV).

> [!TIP]
> **Avvio senza invio:** estrarre `PPSA86921.zip` dalla release e copiare la cartella in `/data/homebrew/`. ShadowMountPlus crea un riquadro; un tocco avvia l'app e apre l'interfaccia.

> [!IMPORTANT]
> Può essere in esecuzione una sola **istanza** alla volta, altrimenti due programmi regolano la ventola uno contro l'altro. Una vecchia istanza si ferma nella pagina "Payload". Altro nel [LIESMICH](docs/LIESMICH.txt) (in tedesco).

## Requisiti

| | |
| --- | --- |
| **Console** | PS5 con jailbreak e loader ELF sulla porta 9021 ([elfldr](https://github.com/ps5-payload-dev/elfldr) o simile) |
| **Testato** | PS5 Pro (CFI-7021), firmware 12.00; diversi utenti segnalano che funziona anche su 13.60 |
| **Ventola** | [kstuff](https://github.com/EchoStretch/kstuff-lite) (senza, sensori e interfaccia funzionano, la regolazione no) |
| **Rete** | Un browser nella stessa rete, oppure il browser della console |
| **Opzionale** | [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus) per spostare, estrarre e per il riquadro di avvio |

<details>
<summary>Firmware e SDK in dettaglio</summary>

L'ELF è compilato con il PS5 payload SDK **v0.44**, il cui codice di avvio conosce il firmware **fino a 13.60**. Un firmware che il codice di avvio non conosce non arriva a `main()`: il programma non si avvia affatto e non scrive nulla nel registro. Per 13.00–13.40 non ci sono ancora segnalazioni (il codice di avvio li conosce). La temperatura della GPU esiste solo sulla Pro.
</details>

## Utilizzo

| Pagina | Contenuto |
| --- | --- |
| **Raffreddamento** | Stato, andamento, sensori, temperatura obiettivo, modalità, profili per gioco |
| **Giochi** | Libreria, tempo di gioco, salvataggi, pacchetti, PS4 da USB, backup, cronologia |
| **File manager** | Cartelle della console e delle unità |
| **Payload** | Payload in esecuzione, salvati e su USB, profili, scarica payload |
| **Sistema** | Console, memoria, rete, diagnostica |
| **Profilo** | Nome visualizzato e immagine del profilo |
| **Registro** | Eventi dell'app e log del kernel in diretta |
| **Riconoscimenti** | Grazie agli sviluppatori; più **manuale** e **FAQ** in sei lingue |

**Dal controller (browser della console):** L1 / R1 pagina precedente / successiva · L2 / R2 scorrere su / giù · Mute due volte: temperatura e ventola sulla TV.

> [!WARNING]
> L'interfaccia **non ha login**: ogni dispositivo della rete domestica può leggere e modificare. Non impostare l'inoltro della porta 8086. Per consentire solo il browser della console, impostare `bind_address` su `127.0.0.1`. Dettagli nel [manuale](docs/HANDBUCH.md#zugriff-und-sicherheit-im-heimnetz) e nella [politica di sicurezza](SECURITY.md).

## Documentazione

| Documento | Contenuto |
| --- | --- |
| [Manuale](docs/HANDBUCH.md) | Utilizzo in dettaglio (in tedesco; anche nell'app, in sei lingue) |
| [API](docs/API.md) | Tutti gli endpoint e i campi di configurazione |
| [Sviluppo](docs/ENTWICKLUNG.md) | Compilare, inviare, risolvere problemi, struttura del codice |
| [Modifiche](CHANGELOG.md) | Cosa è cambiato in quale versione |
| [Registro delle decisioni](docs/ERWEITERUNGEN.md) | Perché le cose sono fatte così, cosa è stato misurato |
| [Componenti di terzi](THIRD_PARTY_NOTICES.md) | Codice ripreso e relative licenze |
| [Contribuire](CONTRIBUTING.md) · [Sicurezza](SECURITY.md) | Contributi e segnalazione di vulnerabilità |

<details>
<summary><b>Compilare dal codice sorgente</b></summary>

Serve il [PS5 payload SDK](https://github.com/ps5-payload-dev/sdk) **v0.42 o successivo** (testato con v0.44); la compilazione lo verifica e si ferma con un SDK più vecchio. Su Windows basta Git Bash con LLVM 21:

```bash
tools/build-windows.sh
```

Su Linux o WSL:

```bash
export PS5_PAYLOAD_SDK=$HOME/ps5sdk/sdk
make
```

Il risultato è `PS5_Cooling_Center.elf`; `tools/make_starter.sh` crea il riquadro di avvio. Il resto è in [docs/ENTWICKLUNG.md](docs/ENTWICKLUNG.md).
</details>

## Note legali ed esclusione di responsabilità

> [!CAUTION]
> **Non è un prodotto Sony, niente pirateria, nessuna responsabilità.** Questo progetto non ha legami con Sony. Non contiene giochi, firmware, chiavi Sony (solo una chiave pubblicamente nota per pacchetti di prova autocostruiti, vedi THIRD_PARTY_NOTICES.md) né codice per aggirare protezioni anticopia, e non scarica nulla da Internet tranne, se li attivi, gli aggiornamenti ufficiali dei tuoi giochi dai server di Sony. Modificare una console può costare garanzia e account; il rischio è di chi lo fa. Il programma è fornito **senza alcuna garanzia**.

<details>
<summary>Testo completo</summary>

> Questa sezione è un'informazione generale e non una consulenza legale. Chi usa il progetto è responsabile di verificare che l'uso sia consentito nel proprio paese e nei confronti dei propri partner contrattuali.

- **Non è un prodotto Sony.** "PlayStation", "PS5" e i relativi loghi sono marchi di Sony Interactive Entertainment Inc. Questo progetto non ha legami con Sony e non è né supportato né approvato da Sony. Tutti gli altri nomi appartengono ai rispettivi proprietari.
- **La tua console, la tua responsabilità.** Il programma funziona solo su una console modificata dal proprietario. La modifica può violare le condizioni d'uso e portare alla perdita della garanzia o al blocco dell'account o della console, e in alcuni paesi a conseguenze legali. Il rischio è di chi modifica la console e usa questo programma.
- **Niente pirateria.** Questo progetto **non** serve a procurarsi, distribuire o usare copie pirata, e non lo sostiene. Copiare, spostare e convertire agiscono solo su giochi già presenti sulla propria console e servono per i backup di giochi acquistati legalmente. Trovare, dividere, installare e montare pacchetti da USB agiscono solo su pacchetti messi da te sulla console o su un'unità; l'app non ne procura. La distribuzione di contenuti protetti dal diritto d'autore è un reato nella maggior parte dei paesi. Qui non c'è aiuto in merito, nemmeno nelle issue.
- **Nessun file Sony nel repository.** Librerie di sistema, firmware e file proprietari dell'SDK non sono né forniti né accettati (vedi [CONTRIBUTING](CONTRIBUTING.md)). Le 30 immagini del profilo sono state create dall'autore.
- **Nessuna garanzia, nessuna responsabilità.** Il programma interviene sul controllo della ventola e sulle funzioni di sistema della console. È fornito così com'è, **senza alcuna garanzia** (GNU GPL, sezioni 15 e 16). L'autore non risponde di danni a console, dati o account. Una console impolverata raffredda peggio, comunque sia regolata la ventola.
- **Privacy.** Storico, registro e impostazioni restano sulla console in `/data/PS5-Cooling-Center/`. L'app non invia dati a Internet; solo con gli aggiornamenti dei giochi attivi interroga i server di Sony e i provider DNS sugli ID titolo dei tuoi giochi. Unica eccezione nel browser: la pagina "Riconoscimenti" carica una volta la piccola icona da github.com per vedere se il browser ha Internet; solo allora i link sono cliccabili.
- **I progetti di terzi** sono sotto le proprie licenze, vedi [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
</details>

## Contribuire e segnalare errori

Errori e richieste sono benvenuti come [issue](../../issues), con modello della console, firmware, versione e registro esportato (pagina "Registro"). Contributi: [CONTRIBUTING.md](CONTRIBUTING.md). Le vulnerabilità vanno segnalate come descritto in [SECURITY.md](SECURITY.md).

## Riconoscimenti e ringraziamenti

Questo progetto poggia sulle spalle della comunità homebrew della PS5. **Grazie!** Un ringraziamento speciale a **[Gezine](https://github.com/Gezine)**: senza il lavoro di Gezine su molte console non girerebbe nessun homebrew, e quindi nemmeno questa app.

<details>
<summary><b>Piattaforma e strumenti</b></summary>

- **John Törnblom e tutti i collaboratori di [ps5-payload-dev](https://github.com/ps5-payload-dev)**: il [PS5 payload SDK](https://github.com/ps5-payload-dev/sdk), il loader [elfldr](https://github.com/ps5-payload-dev/elfldr), [klogsrv](https://github.com/ps5-payload-dev/klogsrv), [ftpsrv](https://github.com/ps5-payload-dev/ftpsrv) e [websrv](https://github.com/ps5-payload-dev/websrv).
- **Gli sviluppatori di kstuff**: [sleirsgoevy](https://github.com/sleirsgoevy), [EchoStretch](https://github.com/EchoStretch) e [drakmor](https://github.com/drakmor) ([kstuff-lite](https://github.com/EchoStretch/kstuff-lite)). Senza kstuff non ci sarebbe accesso al controller della ventola.
</details>

<details>
<summary><b>Codice da altri progetti (portato o incluso)</b></summary>

- **[gasb07](https://github.com/gasb07), [PS4Mount](https://github.com/gasb07/ps4mount)** (GPL-3.0): PKG falsi PS4 da USB senza installazione; il codice è integrato come scheda "PS4 da USB".
- **[RenanGBarreto](https://github.com/RenanGBarreto), [rdmrocha](https://github.com/rdmrocha) e i collaboratori di [MkPFS](https://github.com/PSBrew/MkPFS)** ([PSBrew](https://github.com/PSBrew), GPL-3.0): immagini exFAT, PFS e PFSC.
- **[SvenGDK](https://github.com/SvenGDK), [UFS2Tool](https://github.com/SvenGDK/UFS2Tool)** (BSD-2-Clause): modello per lo scrittore UFS2/ffpkg.
- **[drakmor](https://github.com/drakmor)**: [ampr_emu](https://github.com/drakmor/ampr_emu) (asset pack LZ4, APR-EMU), [pgo_stub](https://github.com/drakmor/pgo_stub) e [dlc_emu](https://github.com/drakmor/dlc_emu) (versioni integrate, GPL-3.0).
- **[BlackBearReloaded](https://github.com/BlackBearReloaded), ps5-native-app-boilerplate** (GPL-3.0): come il riquadro di avvio è costruito come titolo nativo.
- **[itsPLK](https://github.com/itsPLK), [ps5-pkg-manager](https://github.com/itsPLK/ps5-pkg-manager)** (GPL-3.0): struttura dei pacchetti e procedura di installazione (codice proprio su questo modello).
- **[phantomptr](https://github.com/phantomptr), [ps5upload](https://github.com/phantomptr/ps5upload)** (GPL-3.0): funzione del profilo e formato dell'immagine del profilo.
- **[slopmaster33](https://github.com/slopmaster33), [webhb](https://github.com/slopmaster33/webhb)** (GPL-3.0): codificatore QR.
- **[danthespal](https://github.com/danthespal), [MkPFS.CSharp](https://github.com/danthespal/MkPFS.CSharp)** (GPL-3.0): i quattro profili LZ4 testati (ASTRO BOT, EA Sports UFC 6, motore di Insomniac, motore Unity), inclusi senza modifiche.
- **[Eric Biggers](https://github.com/ebiggers), [libdeflate](https://github.com/ebiggers/libdeflate)** (MIT) · **[Dave Gamble](https://github.com/DaveGamble), [cJSON](https://github.com/DaveGamble/cJSON)** (MIT) · **[LZ4](https://github.com/lz4/lz4)** (BSD-2-Clause).
</details>

<details>
<summary><b>Collaborazione, conoscenze e modelli</b></summary>

- **[drakmor](https://github.com/drakmor)**: [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus) (montare, spostare, estrarre giochi) e [ps5-hwinfo](https://github.com/drakmor/ps5-hwinfo) (linee di alimentazione, frequenze).
- **V-10, [exFAT2AMPR](https://github.com/exFAT2AMPR/exfat2ampr)**: elenco per "Automatico", registrazione per creare profili, manutenzione dei plugin dei giochi.
- **theghostonline (Husam Osman), [PS5 LZ4 Compression](https://github.com/theghostonline/PS5-LZ4-Compression)**: file che devono restare sciolti nella compressione, permessi 0777, riparazione durante il controllo dei pacchetti, scelta dei profili testati.
- **[kerrdec97](https://github.com/kerrdec97), [exFAT Image Builder](https://github.com/kerrdec97/ps5-exfat-builder)**: parametri per immagini `.ffpkg`.
- **[itsPLK](https://github.com/itsPLK)**: [ps5-unified-autoloader](https://github.com/itsPLK/ps5-unified-autoloader) e [ps5-payload-manager](https://github.com/itsPLK/ps5-payload-manager).
- **[StonedModder](https://github.com/StonedModder), [ps-game-state-lib](https://github.com/StonedModder/ps-game-state-lib)** (MIT): riconoscimento del gioco in corso.
- **[etaHEN](https://github.com/etaHEN/etaHEN)** e **[onionHEN](https://github.com/aydencharles/onionHEN)**: chiamate di sistema, valori, frame rate.
- **[Soniciso](https://git.etawen.dev/soniciso), [Elf Arsenal](https://git.etawen.dev/soniciso/elf-arsenal)**: modello per molte funzioni (codice riscritto ogni volta).
- **BestPig, [BackPork](https://github.com/BestPig/BackPork)**: il principio delle librerie sostitutive.
- **Juma Sayeh e Osama Abualia, PS5 Game Compressor**: idee per verifica e conversione (tutto riscritto; il suo codice non ha licenza).
- **Driver Linux `hid-playstation`** (batteria del DualSense) · **Xbox 360 DashLaunch** (l'idea della regolazione silenziosa).
</details>

<details>
<summary><b>Grazie alla comunità</b></summary>

Il loro codice non è in questa app, ma senza il loro lavoro condiviso la scena non esisterebbe così: [owendswang](https://github.com/owendswang), [LightningMods](https://github.com/LightningMods), [Andy Nguyen / TheFloW](https://github.com/TheOfficialFloW), [Specter](https://github.com/Cryptogenic), [ChendoChap](https://github.com/ChendoChap), [idlesauce](https://github.com/idlesauce), [flatz](https://github.com/flatz), [Al Azif](https://github.com/Al-Azif), [zecoxao](https://github.com/zecoxao), [ArkSama](https://github.com/ArkSama), [nexgen999](https://github.com/nexgen999), [matem6](https://github.com/matem6), [Phoenixx1202](https://github.com/Phoenixx1202), [SiSTR0](https://github.com/SiSTR0) – e a tutti gli altri che contribuiscono con codice, test, guide o risposte. Nell'app tutti sono elencati con un'immagine nella pagina "Riconoscimenti".
</details>

Chi non si trova qui o si trova descritto in modo errato può segnalarlo: l'elenco sarà volentieri ampliato.

## Licenza

Copyright © 2026 strongt1me · **GNU General Public License v3.0 o successiva**, vedi [LICENSE](LICENSE). Fino alla 1.45.1 il progetto era MIT; dalla 1.46.0 è GPL-3.0-or-later perché contiene parti di progetti GPL (dettagli in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).

<sub>Questo README è disponibile anche in [Deutsch](README.md), [English](README.en.md), [Español](README.es.md), [Français](README.fr.md) e [Русский](README.ru.md). Manuale e FAQ accompagnano ogni release in tutte e sei le lingue.</sub>
