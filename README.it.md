<div align="center">

<img src="web/img/app-mark.png" alt="" width="96" height="96">

# PS5 Cooling & System Center - Pro

**Controllo della ventola, monitoraggio della temperatura e centro di sistema per la PlayStation 5 con jailbreak, con interfaccia web nella rete domestica in sei lingue.**

![Versione](https://img.shields.io/badge/Versione-1.54.1-1f6feb)
![Licenza](https://img.shields.io/badge/Licenza-GPL--3.0--or--later-blue)
![Piattaforma](https://img.shields.io/badge/Piattaforma-PS5%20Payload-003791)
![Lingue](https://img.shields.io/badge/Lingue-DE%20%C2%B7%20EN%20%C2%B7%20IT%20%C2%B7%20ES%20%C2%B7%20FR%20%C2%B7%20RU-lightgrey)

[Funzioni](#funzioni) · [Installazione](#installazione) · [Utilizzo](#utilizzo) · [Documentazione](#documentazione) · [Note legali](#note-legali-ed-esclusione-di-responsabilità) · [Riconoscimenti](#riconoscimenti-e-ringraziamenti)

[Deutsch](README.md) · [English](README.en.md) · **Italiano** · [Español](README.es.md) · [Français](README.fr.md) · [Русский](README.ru.md)

</div>

---

PS5 Cooling & System Center - Pro è un payload homebrew (ELF) per una PlayStation 5 con jailbreak. Legge i sensori di temperatura della console, regola la ventola secondo una curva di comfort tranquilla e liberamente impostabile e include un'interfaccia web che apri da qualsiasi dispositivo della rete domestica: smartphone, tablet o PC. In più ci sono una gestione dei giochi, una gestione dei payload e la gestione del profilo della console. Tutto gira sulla PS5 stessa, senza PC e senza Internet.

Il progetto è il successore di *PS5 Temperature Manager*; backend (C) e interfaccia sono stati riscritti da zero.

<p align="center">
  <img src="docs/images/kuehlung.jpg" alt="Raffreddamento: temperatura, ventola, andamento e sensori" width="860">
</p>

<table>
  <tr>
    <td width="50%"><img src="docs/images/payloads.jpg" alt="Gestione dei payload"></td>
    <td width="50%"><img src="docs/images/profil.jpg" alt="Profilo con 30 immagini del profilo integrate"></td>
  </tr>
  <tr>
    <td align="center"><sub>Avviare, copiare e terminare payload (dati di esempio)</sub></td>
    <td align="center"><sub>Scegliere l'immagine del profilo tra 30 immagini integrate</sub></td>
  </tr>
</table>

<details>
<summary>Utilizzabile anche dallo smartphone</summary>
<p align="center"><img src="docs/images/kuehlung-mobil.jpg" alt="Raffreddamento sullo smartphone" width="300"></p>
</details>

## Funzioni

**Raffreddamento**

- **Regolazione comfort della ventola.** Mantiene una temperatura obiettivo (predefinita 66 °C, impostabile da 60 a 91 °C; 91 °C è il valore della console stessa) con media mobile, zona morta e tendenza, e cambia la velocità solo a piccoli passi. L'obiettivo non è la temperatura più bassa, ma una ventola dal suono tranquillo e uniforme. Modalità operative (silenzioso, bilanciato, fresco), scelta rapida e regole proprie per ogni gioco.
- **Prima la sicurezza.** Dalla temperatura di sicurezza (predefinita 78 °C) conta solo l'hardware. Se l'app non è in esecuzione, la console regola la ventola con la propria curva.
- **Valori misurati.** Processore, chip principale, grafica (solo PS5 Pro), velocità della ventola, carico di tutti i core della CPU, frequenza in tempo reale, assorbimento delle linee di alimentazione, frequenza dei fotogrammi in gioco e batteria del controller. Andamenti su 2 minuti, 24 ore e come valutazione settimanale dell'efficacia del raffreddamento.
- **Notifiche sul televisore.** All'avvio, in caso di avvisi e, se vuoi, a intervalli regolari. Due brevi pressioni del tasto microfono del controller mostrano la temperatura del processore e la ventola.

**Gestione**

- **Giochi.** I giochi della schermata Home con copertina, tempo di gioco, formato e posizione. Avvio diretto (se è in esecuzione un altro gioco, un avviso lo segnala; «Chiudi il gioco» lo chiude subito, poi il successivo si avvia con un tocco), copia su altre unità, spostamento o estrazione con ShadowMountPlus e conversione senza PC in immagini exFAT, ffpkg e ffpfsc. Una seconda scheda registra il tempo di gioco: quando si è giocato, per quanto tempo e quanto si è scaldata la console, con totali, barre giornaliere e classifica. Una terza esegue il backup dei dati salvati su una chiavetta, un disco o la memoria della console e, se vuoi, ripristina un titolo: invariati e crittografati, ogni file riletto e verificato, e prima del ripristino viene fatto un backup separato dello stato attuale. Copie e immagini vengono rilette per intero e verificate dopo la scrittura; accanto c'è un file `.sha256` che in seguito si può verificare sul PC con `sha256sum -c`. Un interruttore «Salva copertine e metadati» salva sulla console le copertine e i dati dei giochi che richiedono tempo per essere determinati (cartella `covers_and_more`), così l'elenco si carica più in fretta. Una quarta scheda, «Pacchetti», trova i pacchetti di gioco (`.pkg`) su chiavette, dischi e nella memoria della console, li mostra con immagine, versione e tipo, divide i pacchetti grandi in parti per chiavette FAT32 o dischi ottici (rilette e verificate; il pacchetto resta invariato) e, se vuoi, installa un pacchetto tramite l'installazione della console stessa: l'app glielo mette solo a disposizione, verifica prima cosa sarebbe d'ostacolo, mostra l'avanzamento e non elimina né sovrascrive nulla.
- **File.** Una gestione file per le cartelle della console: visualizzare, scaricare, caricare, creare nuove cartelle, rinominare, copiare, spostare ed eliminare (modifiche solo sulle unità e in `/data`; l'eliminazione chiede conferma due volte). L'elenco si può ordinare per nome, dimensione o data, tutte le voci si possono selezionare in una volta, la dimensione di una cartella viene calcolata su richiesta, e «Visualizza» mostra immagini e file di testo direttamente nel browser.
- **Payload.** Visualizzare e terminare i payload in esecuzione. Avviare i propri file `.elf` da una cartella della console o da una chiavetta USB, oppure copiarli nella cartella, senza alcun PC.
- **Profilo.** Cambiare il nome visualizzato; immagine del profilo tra 30 immagini integrate o da un proprio file, con backup dell'immagine precedente.
- **Sistema.** Modello, firmware, tempo di attività, memoria e rete. Sensori grezzi e diagnostica in modalità esperto. Registro degli eventi esportabile.
- **Intestazione.** Su ogni pagina: modalità riposo, riavvio, spegnimento e modalità provvisoria (ognuno dopo due clic, come gruppo al centro), schermo intero e modalità esperto.
- **Lingue.** L'interfaccia è disponibile in tedesco, inglese, italiano, spagnolo, francese e russo; scegli la lingua in alto a destra (alla prima visita vale quella del browser). Manuale e FAQ sono disponibili in tutte e sei le lingue nell'app e in PDF da scaricare. I messaggi che la console stessa mostra sul televisore sono in tedesco.
- **Riquadro nella schermata Home.** Apre l'interfaccia direttamente nel browser della console.

**Tecnica.** Server HTTP proprio senza librerie esterne (i file dell'interfaccia viaggiano compressi sulla rete), [API JSON](docs/API.md) documentata, impostazioni in `/data/PS5-Cooling-Center/config.json`, nessun accesso a Internet (l'app si collega solo a programmi sulla console stessa).

## Requisiti

| | |
| --- | --- |
| **Console** | PS5 con jailbreak e un caricatore ELF sulla **porta 9021** ([elfldr](https://github.com/ps5-payload-dev/elfldr) o equivalente). Testato su una **PS5 Pro (CFI-7021) con firmware 12.00**. Diversi utenti riferiscono che l'app funziona anche su una PS5 con **firmware 13.60**. Altri modelli e versioni del firmware non sono testati. La temperatura grafica è disponibile solo sulla Pro. |
| **Firmware** | L'ELF è compilato con il PS5-Payload-SDK **v0.43**, il cui codice di avvio conosce i firmware **fino a 13.60**. Con un firmware che il codice di avvio non conosce, il programma non arriva a `main()`: non si avvia affatto e non scrive nulla nel registro. Diversi utenti riferiscono che l'app funziona sul **13.60**; per 13.00-13.40 non ci sono segnalazioni (il codice di avvio li conosce). |
| **kstuff** | Necessario per il controllo della ventola (`/dev/icc_fan`). Senza kstuff sensori e interfaccia continuano a funzionare, la regolazione segnala «non disponibile». |
| **Rete** | Un browser nella stessa rete della console. |
| **facoltativo** | [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus) per spostare ed estrarre i giochi e per riconoscerne formato e posizione. |

## Installazione

1. Scarica dalle **Releases** il file `PS5_Cooling_System_Center_v<Version>.elf`.
2. Invia l'ELF alla console, porta **9021**, con un qualsiasi strumento di invio payload o da riga di comando:

   ```bash
   nc -q0 <PS5-IP> 9021 < PS5_Cooling_System_Center_v1.54.1.elf
   ```

3. Sul televisore compare una notifica con l'indirizzo. Apri nel browser: **`http://<PS5-IP>:8086`**
4. Il riquadro nella schermata Home (sezione «Contenuti multimediali») viene creato dal programma stesso al primo avvio, non serve un installer. Il riquadro sopravvive ai riavvii, ma non avvia il programma: apre solo l'interfaccia. Se in seguito manca, «Installa riquadro» nella pagina Sistema lo ripristina; come soluzione di riserva è incluso `cooling-center-launcher-installer_v<Version>.elf`.

Dopo ogni riavvio della console l'ELF va inviato di nuovo, ad esempio con un autoloader. Deve essere sempre in esecuzione **una sola** istanza: due istanze regolerebbero la ventola l'una contro l'altra. Un'istanza vecchia si può terminare nella pagina «Payload». La guida dettagliata è allegata alla release come [LIESMICH](docs/LIESMICH.txt).

## Utilizzo

| Pagina | Contenuto |
| --- | --- |
| **Profilo** | Nome visualizzato e immagine del profilo della console |
| **Giochi** | Avviare, copiare, spostare, convertire giochi; tempo di gioco con temperature; backup e ripristino dei dati salvati |
| **File** | Gestione file: visualizzare cartelle, caricare e scaricare file, copiare, spostare, eliminare |
| **Payload** | Payload in esecuzione, salvati e presenti su USB |
| **Raffreddamento** | Stato, andamento, sensori, temperatura obiettivo, modalità operativa, profili di gioco |
| **Sistema** | Console, memoria, rete, diagnostica, spegnimento e riavvio |
| **Registro** | Eventi dell'app, esportabili come `.log`, e il registro del kernel della console in tempo reale con filtro, pausa, salvataggio sulla console e registrazione |
| **Riconoscimenti** | Ringraziamenti agli sviluppatori il cui lavoro è incluso nell'app; inoltre **Manuale** e **FAQ** nella tua lingua |

Tutto il resto è nel [Manuale](docs/HANDBUCH.md): la regolazione comfort e i suoi parametri, l'avvio dei payload, le immagini del profilo, il riquadro, le notifiche sullo schermo e le impostazioni.

**Accesso nella rete domestica.** L'interfaccia non richiede alcun accesso: ogni dispositivo della rete domestica può leggere e modificare. La console non va esposta su Internet, quindi **non impostare un inoltro delle porte** sulla porta 8086. Chi non lo vuole imposta `bind_address` su `127.0.0.1`. Il server respinge le richieste che provengono chiaramente da siti web esterni. I dettagli si trovano nel [Manuale](docs/HANDBUCH.md#zugriff-und-sicherheit-im-heimnetz) e nella [politica di sicurezza](SECURITY.md).

## Compilare dal codice sorgente

Serve il [PS5-Payload-SDK](https://github.com/ps5-payload-dev/sdk) **dalla v0.42** (compilato e testato con **v0.43**): il suo codice di avvio decide su quale firmware l'ELF si avvia, e la v0.41 si ferma a 13.40. La compilazione lo verifica (`tools/check_sdk_firmware.py`) e si interrompe con un SDK troppo vecchio, così come dopo il linking, se l'ELF finito non contiene il caso per 13.60. Su Windows basta Git Bash con LLVM 21 (non 18):

```bash
tools/build-windows.sh
```

Su Linux o WSL:

```bash
export PS5_PAYLOAD_SDK=$HOME/ps5sdk/sdk
make
```

Il risultato è `PS5_Cooling_Center.elf`. Tutti i passaggi, la ricerca degli errori, la procedura di release e la struttura del codice sorgente sono descritti in [docs/ENTWICKLUNG.md](docs/ENTWICKLUNG.md).

## Documentazione

| Documento (in tedesco) | Contenuto |
| --- | --- |
| [Manuale](docs/HANDBUCH.md) | Utilizzo nel dettaglio (anche nell'app e in PDF in sei lingue, vedi Releases) |
| [API](docs/API.md) | Tutti gli endpoint e i campi di configurazione |
| [Sviluppo](docs/ENTWICKLUNG.md) | Compilazione, invio, ricerca degli errori, struttura del codice sorgente |
| [Note di rilascio](docs/RELEASE_NOTES.md) | Cosa è cambiato in quale versione |
| [Registro delle decisioni](docs/ERWEITERUNGEN.md) | Perché qualcosa è fatto così, cosa è stato misurato, cosa resta aperto |
| [LIESMICH](docs/LIESMICH.txt) | Guida rapida allegata a ogni release |
| [Componenti di terzi](THIRD_PARTY_NOTICES.md) | Codice ripreso e relative licenze |
| [Contribuire](CONTRIBUTING.md) · [Sicurezza](SECURITY.md) | Contributi e segnalazione di vulnerabilità |

## Note legali ed esclusione di responsabilità

> Questa sezione è un'informazione generale e non una consulenza legale. Chi usa il progetto è responsabile in prima persona del fatto che il proprio utilizzo sia consentito nel suo paese e nei confronti delle sue controparti contrattuali.

- **Non è un prodotto Sony.** «PlayStation», «PS5» e i relativi loghi sono marchi di Sony Interactive Entertainment Inc. Questo progetto non ha alcun legame con Sony e non è né supportato né approvato da Sony. Tutti gli altri nomi appartengono ai rispettivi proprietari.
- **Console propria, responsabilità propria.** Il programma funziona solo su una console modificata dal proprietario stesso. La modifica può violare le condizioni d'uso e comportare la perdita della garanzia, la sospensione dell'account o della console e, in alcuni paesi, anche conseguenze legali. Il rischio è di chi modifica la console e usa questo programma.
- **Nessuna pirateria.** Questo progetto **non** è pensato per procurare, diffondere o usare copie pirata e non lo supporta. Non contiene giochi, firmware, chiavi né codice per aggirare protezioni dalla copia o DRM. Non scarica nulla da Internet. Copia, spostamento e conversione agiscono solo su giochi già presenti come cartella o immagine sulla propria console e sono pensati per i backup di giochi acquistati legalmente. La ricerca, la divisione e l'installazione dei pacchetti agiscono solo su pacchetti che hai messo tu sulla console o su un'unità collegata; l'app non ne procura. La diffusione di contenuti protetti dal diritto d'autore è un reato nella maggior parte dei paesi. Qui non si fornisce aiuto in merito, nemmeno nelle issue.
- **Nessun file di Sony nel repository.** Librerie di sistema, firmware e file proprietari degli SDK non vengono né forniti né accettati (vedi [CONTRIBUTING](CONTRIBUTING.md)). Le 30 immagini del profilo sono state create dall'autore stesso.
- **Nessuna garanzia, nessuna responsabilità.** Il programma interviene sul controllo della ventola e su funzioni di sistema della console. Viene fornito così com'è, **senza alcuna garanzia** (GNU GPL, sezioni 15 e 16). L'autore non risponde di danni a console, dati o account. Il programma non sostituisce la manutenzione: una console impolverata si raffredda peggio, comunque venga regolata la ventola.
- **Protezione dei dati.** Andamenti, registro e impostazioni restano sulla console in `/data/PS5-Cooling-Center/`. L'app non invia dati su Internet e si collega solo a programmi sulla console stessa (caricatore di payload, ShadowMountPlus) e ai browser della rete domestica che aprono l'interfaccia. Unica eccezione nel browser, non nell'app: la pagina «Riconoscimenti», all'apertura, carica una volta la piccola icona da github.com per vedere se il browser ha accesso a Internet; solo in quel caso i link a GitHub sono cliccabili. Le immagini del profilo degli sviluppatori sono integrate nell'app e non vengono caricate da Internet.
- **Progetti di terzi.** Sono soggetti alle proprie licenze, vedi [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) e i riconoscimenti più sotto.

## Contribuire e segnalare errori

Errori e richieste sono benvenuti come [issue](../../issues). Indica il modello della console, il firmware, la versione del programma e, se disponibile, il registro esportato (pagina «Registro»). Contributi: [CONTRIBUTING.md](CONTRIBUTING.md). Non segnalare le vulnerabilità di sicurezza pubblicamente, ma come descritto in [SECURITY.md](SECURITY.md).

## Riconoscimenti e ringraziamenti

Questo progetto poggia sulle spalle della comunità homebrew della PS5. Senza il lavoro di queste sviluppatrici e di questi sviluppatori non esisterebbero né la piattaforma né parte delle funzioni. **Grazie mille!**

**Piattaforma e strumenti**

- **John Törnblom e tutti i collaboratori di [ps5-payload-dev](https://github.com/ps5-payload-dev)**: il [PS5-Payload-SDK](https://github.com/ps5-payload-dev/sdk), con cui è compilato ogni ELF di questo progetto, il caricatore di payload [elfldr](https://github.com/ps5-payload-dev/elfldr) sulla porta 9021, [klogsrv](https://github.com/ps5-payload-dev/klogsrv) e [ftpsrv](https://github.com/ps5-payload-dev/ftpsrv) per lo sviluppo e [websrv](https://github.com/ps5-payload-dev/websrv), il cui metodo di avvio dei giochi è servito qui da modello.
- **Gli sviluppatori di kstuff**: [sleirsgoevy](https://github.com/sleirsgoevy) (autore principale), [EchoStretch](https://github.com/EchoStretch) e [drakmor](https://github.com/drakmor) (maggiori collaboratori di [kstuff-lite](https://github.com/EchoStretch/kstuff-lite)); repository, tra gli altri, presso [ps5-payload-dev](https://github.com/ps5-payload-dev/kstuff) ed [EchoStretch](https://github.com/EchoStretch/kstuff). Senza kstuff non ci sarebbe accesso al controller della ventola.

**Codice da altri progetti (portato o integrato)**

- **[RenanGBarreto](https://github.com/RenanGBarreto), [rdmrocha](https://github.com/rdmrocha) e i collaboratori di [MkPFS](https://github.com/PSBrew/MkPFS)** ([PSBrew](https://github.com/PSBrew), GPL-3.0): struttura delle immagini exFAT, PFS e PFSC. La conversione sulla console ne è una trasposizione in C.
- **[SvenGDK](https://github.com/SvenGDK), [UFS2Tool](https://github.com/SvenGDK/UFS2Tool)** (BSD-2-Clause): modello per lo scrittore UFS2/ffpkg.
- **[itsPLK](https://github.com/itsPLK), [ps5-pkg-manager](https://github.com/itsPLK/ps5-pkg-manager)** (GPL-3.0): struttura dei pacchetti PS4/PS5, il formato delle parti (`PS5MPKG1`), lo schema delle cartelle della ricerca dei pacchetti e la procedura di installazione (come viene chiamata la libreria di sistema della console e da dove legge il pacchetto). Le funzioni dei pacchetti di questa app sono codice proprio basato su questo modello; nulla di esso è incorporato.
- **[phantomptr](https://github.com/phantomptr), [ps5upload](https://github.com/phantomptr/ps5upload)** (GPL-3.0): struttura della funzione profilo e formato dell'immagine del profilo.
- **[Eric Biggers](https://github.com/ebiggers), [libdeflate](https://github.com/ebiggers/libdeflate)** (MIT): compressione veloce per le immagini.
- **[Dave Gamble](https://github.com/DaveGamble) e collaboratori, [cJSON](https://github.com/DaveGamble/cJSON)** (MIT): JSON.

**Collaborazione, conoscenze e modelli**

- **[drakmor](https://github.com/drakmor)**: [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus) (montare, spostare ed estrarre i giochi; la pagina Giochi si basa su di esso; il suo README indica la struttura consigliata per le immagini `.ffpkg`: blocchi da 64 KiB) e [ps5-hwinfo](https://github.com/drakmor/ps5-hwinfo) (ordine delle linee di alimentazione, valori di frequenza).
- **[kerrdec97](https://github.com/kerrdec97), [exFAT Image Builder](https://github.com/kerrdec97/ps5-exfat-builder)**: ha mostrato con quali parametri si crea un `.ffpkg` per ShadowMountPlus (blocchi e frammenti da 64 KiB, nessuno spazio riservato, densità degli inode 262144, dimensione del settore 512) e che la dimensione del settore 4096 lì (su Windows) produce immagini difettose. Solo conoscenze, nessun codice.
- **[itsPLK](https://github.com/itsPLK)**: [ps5-unified-autoloader](https://github.com/itsPLK/ps5-unified-autoloader) (chiusura del browser della console) e [ps5-payload-manager](https://github.com/itsPLK/ps5-payload-manager), modello per la gestione dei payload e per la regola su quali processi si possono terminare.
- **[slopmaster33](https://github.com/slopmaster33), [webhb](https://github.com/slopmaster33/webhb)** (GPL-3.0): l’encoder QR che mostra l’indirizzo dell’interfaccia web come codice (`src/qr.c`, ripreso e verificato con un lettore).
- **[StonedModder](https://github.com/StonedModder), [ps-game-state-lib](https://github.com/StonedModder/ps-game-state-lib)** (MIT): gli schemi nel registro del kernel con cui si riconosce il gioco in esecuzione.
- **Il progetto [etaHEN](https://github.com/etaHEN/etaHEN)** e **[onionHEN](https://github.com/aydencharles/onionHEN)** (aydencharles): codice sorgente e documentazione su chiamate di sistema, valori misurati e rilevazione della frequenza dei fotogrammi.
- **[Soniciso](https://git.etawen.dev/soniciso), [Elf Arsenal](https://git.etawen.dev/soniciso/elf-arsenal)** (successore di [Sonic Loader](https://git.etawen.dev/soniciso/sonicloader)): forma delle chiamate per l'installazione del riquadro e modello per molte funzioni (chiudere il gioco, registro del kernel in tempo reale, tempo di gioco, dati salvati, gestione file, eliminazione dei giochi); il codice è stato riscritto da zero in ogni caso.
- **BestPig e [BackPork](https://github.com/BestPig/BackPork)**: il principio delle librerie sostitutive (fakelib), che la pagina Giochi riconosce e mostra.
- **Juma Sayeh (sviluppatore) e Osama Abualia (test), PS5 Game Compressor**: modello per la verifica dell'SDK sul firmware 13.60 durante la compilazione e per cinque idee nella copia e nella conversione: rileggere per intero e verificare i backup dopo la scrittura, impedire la modalità riposo durante le operazioni lunghe, leggere e scrivere contemporaneamente solo tra due unità diverse, riempire esplicitamente di zeri i vuoti nelle immagini e lasciare non compressi i blocchi che risparmiano meno del 5 %. Il codice sorgente del Game Compressor non ha licenza, perciò non ne è stato ripreso nulla: tutto è stato riscritto da zero.
- **Kernel Linux, driver `hid-playstation`**: documentazione del byte di stato del DualSense per il livello di carica.
- **Xbox 360 DashLaunch**: modello per l'idea di una regolazione della temperatura calma e lenta.

**Un ringraziamento speciale a [Gezine](https://github.com/Gezine)**: il suo lavoro è fondamentale per la comunità homebrew della PS5 – senza di esso su molte console non girerebbe alcun homebrew, e quindi nemmeno questa app.

**Grazie alla comunità.** Il loro codice non è incluso in questa app, ma senza il loro lavoro condiviso la scena non esisterebbe in questa forma: [owendswang](https://github.com/owendswang) ([ps5-web-file-manager](https://github.com/owendswang/ps5-web-file-manager), [ps5-fan-control](https://github.com/owendswang/ps5-fan-control)), [LightningMods](https://github.com/LightningMods) (etaHEN, [Itemzflow](https://github.com/LightningMods/Itemzflow)), [Andy Nguyen / TheFloW](https://github.com/TheOfficialFloW) ([PPPwn](https://github.com/TheOfficialFloW/PPPwn)), [Specter](https://github.com/Cryptogenic) ([PS5-IPV6-Kernel-Exploit](https://github.com/Cryptogenic/PS5-IPV6-Kernel-Exploit)), [ChendoChap](https://github.com/ChendoChap) ([pOOBs4](https://github.com/ChendoChap/pOOBs4)), [idlesauce](https://github.com/idlesauce) ([umtx2](https://github.com/idlesauce/umtx2)), [flatz](https://github.com/flatz) ([pkg_pfs_tool](https://github.com/flatz/pkg_pfs_tool)), [Al Azif](https://github.com/Al-Azif) ([ps4-exploit-host](https://github.com/Al-Azif/ps4-exploit-host)), [zecoxao](https://github.com/zecoxao) – e a tutti gli altri che contribuiscono con codice, test, guide o risposte. Nell'app sono tutti elencati con immagine nella pagina «Riconoscimenti».

Le 30 immagini del profilo sono state create dall'autore stesso. Se non ti trovi in elenco o sei descritto in modo errato, segnalacelo: la lista verrà integrata volentieri.

## Licenza

Copyright © 2026 strongt1me

Questo programma è software libero: può essere ridistribuito e modificato secondo i termini della **GNU General Public License**, versione 3 o (a tua scelta) qualsiasi versione successiva, vedi [LICENSE](LICENSE). Viene fornito senza alcuna garanzia.

Fino alla 1.45.1 inclusa il progetto era distribuito con licenza MIT. Dalla 1.46.0 vale GPL-3.0-or-later: la conversione dei giochi è trasposta da [MkPFS](https://github.com/PSBrew/MkPFS) (GPL-3.0), e il PS5-Payload-SDK, con cui viene compilato ogni ELF, è a sua volta sotto GPLv3+. Quali parti di terzi sono incluse e con quale licenza è indicato in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

---

## Lingue

Questo README è disponibile anche in [Deutsch](README.md), [English](README.en.md), [Español](README.es.md), [Français](README.fr.md) e [Русский](README.ru.md); l'originale è il README in tedesco. Il registro delle decisioni, i documenti per gli sviluppatori e la descrizione dell'API sono in tedesco; manuale e FAQ sono allegati a ogni release in tutte e sei le lingue, in PDF e HTML, e sono integrati nell'app. Segnala pure traduzioni mancanti o poco scorrevoli come issue («Translation»); i dettagli su come integrarle sono in [docs/ENTWICKLUNG.md](docs/ENTWICKLUNG.md#übersetzungen).
