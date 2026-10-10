<div align="center">

<img src="web/img/app-mark.png" alt="" width="96" height="96">

# PS5 Cooling & System Center - Pro

**Contrôle du ventilateur, surveillance de la température et centre système pour la PlayStation 5 jailbreakée, avec une interface web sur le réseau domestique en six langues.**

![Version](https://img.shields.io/badge/Version-1.60.0-1f6feb)
![Licence](https://img.shields.io/badge/Licence-GPL--3.0--or--later-blue)
![Plateforme](https://img.shields.io/badge/Plateforme-PS5%20Payload-003791)
![Langues](https://img.shields.io/badge/Langues-DE%20%C2%B7%20EN%20%C2%B7%20IT%20%C2%B7%20ES%20%C2%B7%20FR%20%C2%B7%20RU-lightgrey)

[Fonctions](#fonctions) · [Installation](#installation) · [Utilisation](#utilisation) · [Documentation](#documentation) · [Mentions légales](#mentions-légales-et-clause-de-non-responsabilité) · [Crédits](#crédits-et-remerciements)

[Deutsch](README.md) · [English](README.en.md) · [Italiano](README.it.md) · [Español](README.es.md) · **Français** · [Русский](README.ru.md)

</div>

---

PS5 Cooling & System Center - Pro est un payload homebrew (ELF) pour une PlayStation 5 jailbreakée. Il lit les capteurs de température de la console, régule le ventilateur selon une courbe confort calme et librement réglable, et fournit une interface web que vous ouvrez depuis n’importe quel appareil du réseau domestique : téléphone, tablette ou PC. S’y ajoutent une gestion des jeux, une gestion des payloads et la gestion du profil de la console. Tout tourne sur la PS5 elle-même, sans PC et sans Internet.

Le projet succède au *PS5 Temperature Manager* ; le backend (C) et l’interface ont été entièrement réécrits.

<p align="center">
  <img src="docs/images/kuehlung.jpg" alt="Refroidissement : température, ventilateur, historique et capteurs" width="860">
</p>

<table>
  <tr>
    <td width="50%"><img src="docs/images/payloads.jpg" alt="Gestion des payloads"></td>
    <td width="50%"><img src="docs/images/profil.jpg" alt="Profil avec 30 images de profil intégrées"></td>
  </tr>
  <tr>
    <td align="center"><sub>Lancer, copier et arrêter des payloads (données d’exemple)</sub></td>
    <td align="center"><sub>Choisir une image de profil parmi 30 images intégrées</sub></td>
  </tr>
</table>

<details>
<summary>Utilisable aussi sur un téléphone</summary>
<p align="center"><img src="docs/images/kuehlung-mobil.jpg" alt="Refroidissement sur un téléphone" width="300"></p>
</details>

## Fonctions

**Refroidissement**

- **Régulation confort du ventilateur.** Maintient une température cible (66 °C par défaut, réglable de 60 à 91 °C ; 91 °C est la valeur de la console elle-même) avec une moyenne glissante, une zone neutre et la tendance, et ne change la vitesse que par petits paliers. Le but n’est pas la température la plus basse, mais un ventilateur au son calme et régulier. Modes de fonctionnement (silencieux, équilibré, frais), choix rapide et règles propres à chaque jeu.
- **La sécurité d’abord.** À partir de la température de sécurité (78 °C par défaut), seul le matériel décide. Si l’app ne tourne pas, la console régule avec sa propre courbe.
- **Mesures.** Processeur, puce principale, graphique (PS5 Pro uniquement), vitesse du ventilateur, charge de tous les cœurs du CPU, fréquence en direct, consommation des rails d’alimentation, fréquence d’images en jeu et batterie de la manette. Historiques sur 2 minutes, sur 24 heures et évaluation hebdomadaire de l’efficacité du refroidissement.
- **Messages sur le téléviseur.** Au démarrage, lors d’avertissements et, si vous le souhaitez, régulièrement. Deux appuis brefs sur la touche du micro de la manette affichent la température du processeur et le ventilateur.

**Gestion**

- **Jeux.** Les jeux de l’écran d’accueil avec jaquette, temps de jeu, format et emplacement. Lancement direct (si un autre jeu est lancé, une remarque le signale ; « Fermer le jeu » le ferme immédiatement, puis le suivant se lance d’une simple touche), copie vers d’autres lecteurs, déplacement ou extraction avec ShadowMountPlus et conversion sans PC en images exFAT, ffpkg et ffpfsc. Un deuxième onglet enregistre le temps de jeu : quand vous avez joué, combien de temps et à quel point la console a chauffé, avec totaux, barres quotidiennes et classement. Un troisième fait une copie de sécurité des données sauvegardées sur une clé USB, un disque ou le stockage de la console et restaure un titre à la demande : sans modification et chiffrées, chaque fichier relu et vérifié, et avant la restauration, une copie de sécurité distincte de l’état actuel est faite. Les copies et les images sont entièrement relues et vérifiées après l’écriture ; à côté se trouve un fichier `.sha256` que l’on peut revérifier plus tard sur PC avec `sha256sum -c`. Un interrupteur « Enregistrer jaquettes et métadonnées » stocke sur la console les jaquettes et les informations des jeux longues à obtenir (dossier `covers_and_more`), pour que la liste se charge plus vite. Un quatrième onglet, « Paquets », trouve les paquets de jeux (`.pkg`) sur les clés USB, les disques et dans le stockage de la console, les affiche avec image, version et type, divise les gros paquets en parties pour clés USB FAT32 ou disques optiques (relues et vérifiées ; le paquet reste inchangé) et installe un paquet à la demande via l’installation de la console elle-même : l’app se contente de le lui mettre à disposition, vérifie au préalable ce qui pourrait poser problème, affiche la progression et ne supprime ni n’écrase rien.
- **Fichiers.** Un gestionnaire de fichiers pour les dossiers de la console : afficher, télécharger, téléverser, créer un dossier, renommer, copier, déplacer et supprimer (modification uniquement sur les lecteurs et dans `/data` ; la suppression demande deux confirmations). La liste peut être triée par nom, taille ou date, toutes les entrées peuvent être sélectionnées d’un coup, la taille d’un dossier est calculée à la demande, et « Afficher » montre les images et les fichiers texte directement dans le navigateur.
- **Payloads.** Voir et arrêter les payloads en cours. Lancer vos propres fichiers `.elf` depuis un dossier de la console ou une clé USB, ou les copier dans le dossier, sans aucun PC.
- **Profil.** Modifier le nom affiché ; image de profil parmi 30 images intégrées ou depuis votre propre fichier, avec copie de sécurité de l’image précédente.
- **Système.** Modèle, firmware, durée de fonctionnement, stockage et réseau. Capteurs bruts et diagnostic en mode expert. Journal des événements exportable.
- **Barre d’en-tête.** Sur chaque page : mode repos, redémarrage, extinction et mode sans échec (chacun après deux clics, regroupés au centre), plein écran et mode expert.
- **Langues.** L’interface existe en allemand, anglais, italien, espagnol, français et russe ; vous choisissez la langue en haut à droite (lors de la première visite, celle du navigateur s’applique). Le manuel et la FAQ sont disponibles dans les six langues, dans l’app et en PDF à télécharger. Les messages que la console affiche elle-même sur le téléviseur sont en allemand.
- **Vignette de l’écran d’accueil.** Ouvre l’interface directement dans le navigateur de la console.

**Technique.** Serveur HTTP maison sans bibliothèque tierce (les fichiers de l’interface sont transmis compressés sur le réseau), [interface JSON](docs/API.md) documentée, paramètres dans `/data/PS5-Cooling-Center/config.json`, aucun accès à Internet (l’app se connecte uniquement à des programmes de la console elle-même).

## Prérequis

| | |
| --- | --- |
| **Console** | PS5 jailbreakée avec un chargeur d’ELF sur le **port 9021** ([elfldr](https://github.com/ps5-payload-dev/elfldr) ou équivalent). Testé sur une **PS5 Pro (CFI-7021) avec le firmware 12.00**. Plusieurs utilisateurs signalent que l’app fonctionne aussi sur une PS5 avec le **firmware 13.60**. Les autres modèles et versions de firmware ne sont pas testés. La température graphique n’existe que sur la Pro. |
| **Firmware** | L’ELF est compilé avec le SDK de payloads PS5 **v0.43**, dont le code de démarrage connaît les firmwares **jusqu’à 13.60**. Avec un firmware que le code de démarrage ne connaît pas, le programme n’atteint pas `main()` : il ne démarre pas du tout et n’écrit rien dans le journal. Plusieurs utilisateurs signalent que l’app fonctionne sur la **13.60** ; rien n’a été signalé pour 13.00 à 13.40 (le code de démarrage les connaît). |
| **kstuff** | Nécessaire pour le contrôle du ventilateur (`/dev/icc_fan`). Sans kstuff, les capteurs et l’interface continuent de fonctionner, la régulation indique « indisponible ». |
| **Réseau** | Un navigateur sur le même réseau que la console. |
| **facultatif** | [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus) pour le déplacement et l’extraction de jeux, ainsi que la détection du format et de l’emplacement. |

## Installation

1. Téléchargez le fichier `PS5_Cooling_System_Center_v<Version>.elf` depuis les **Releases**.
2. Envoyez l’ELF à la console, port **9021**, avec l’outil d’envoi de payloads de votre choix ou en ligne de commande :

   ```bash
   nc -q0 <PS5-IP> 9021 < PS5_Cooling_System_Center_v1.60.0.elf
   ```

3. Un message avec l’adresse apparaît sur le téléviseur. Ouvrez dans le navigateur : **`http://<PS5-IP>:8086`**
4. Le programme crée lui-même la vignette de l’écran d’accueil (section « Multimédia ») au premier lancement ; aucun installateur n’est nécessaire. La vignette survit aux redémarrages, mais ne lance pas le programme : elle ouvre seulement l’interface. Si elle disparaît plus tard, « Installer la vignette » sur la page Système la rétablit ; en solution de secours, `cooling-center-launcher-installer_v<Version>.elf` est fourni.

Après chaque redémarrage de la console, l’ELF doit être renvoyé, par exemple via un autoloader. Une **seule** instance doit tourner à la fois : deux instances régleraient le ventilateur l’une contre l’autre. Une ancienne instance peut être arrêtée sur la page « Payloads ». Le guide détaillé est joint à la publication sous forme de fichier [LIESMICH](docs/LIESMICH.txt).

## Utilisation

| Page | Contenu |
| --- | --- |
| **Profil** | Nom affiché et image de profil de la console |
| **Jeux** | Lancer, copier, déplacer, convertir des jeux ; temps de jeu avec températures ; copie de sécurité et restauration des données sauvegardées |
| **Fichiers** | Gestionnaire de fichiers : parcourir les dossiers, téléverser et télécharger des fichiers, copier, déplacer, supprimer |
| **Payloads** | payloads en cours, enregistrés et présents sur USB |
| **Refroidissement** | État, historique, capteurs, température cible, mode de fonctionnement, profils de jeu |
| **Système** | Console, stockage, réseau, diagnostic, extinction et redémarrage |
| **Journal** | Événements de l’app, exportables en `.log`, et journal du noyau de la console en direct avec filtre, pause, enregistrement sur la console et capture |
| **Crédits** | Remerciements aux développeurs dont le travail est intégré à l’app ; ainsi que le **Manuel** et la **FAQ** dans votre langue |

Tout le reste figure dans le [manuel](docs/HANDBUCH.md) : la régulation confort et ses paramètres, le lancement des payloads, les images de profil, la vignette, l’affichage à l’écran et les paramètres.

**Accès sur le réseau domestique.** L’interface n’a aucune authentification : tout appareil du réseau domestique peut lire et modifier. La console n’a rien à faire sur l’Internet ouvert : ne configurez donc **aucune redirection de port** vers le port 8086. Si vous ne le souhaitez pas, réglez `bind_address` sur `127.0.0.1`. Le serveur rejette les requêtes qui proviennent manifestement de pages web tierces. Les détails figurent dans le [manuel](docs/HANDBUCH.md#zugriff-und-sicherheit-im-heimnetz) et dans la [politique de sécurité](SECURITY.md).

## Compiler depuis le code source

Il faut le [SDK de payloads PS5](https://github.com/ps5-payload-dev/sdk) **v0.42 ou ultérieur** (compilé et testé avec **v0.43**) : son code de démarrage détermine sur quel firmware l’ELF peut démarrer, et v0.41 s’arrête à 13.40. La compilation le vérifie (`tools/check_sdk_firmware.py`) et s’interrompt avec un SDK trop ancien, de même qu’après l’édition des liens si l’ELF final ne contient pas le cas de 13.60. Sous Windows, Git Bash avec LLVM 21 (pas 18) suffit :

```bash
tools/build-windows.sh
```

Sous Linux ou WSL :

```bash
export PS5_PAYLOAD_SDK=$HOME/ps5sdk/sdk
make
```

Le résultat est `PS5_Cooling_Center.elf`. Toutes les étapes, le dépannage, le processus de publication et la structure du code source sont décrits dans [docs/ENTWICKLUNG.md](docs/ENTWICKLUNG.md).

## Documentation

| Document (en allemand) | Contenu |
| --- | --- |
| [Manuel](docs/HANDBUCH.md) | L’utilisation en détail (également dans l’app et en PDF en six langues, voir les Releases) |
| [API](docs/API.md) | Tous les points d’accès et champs de configuration |
| [Développement](docs/ENTWICKLUNG.md) | Compiler, envoyer, dépanner, structure du code source |
| [Notes de version](docs/RELEASE_NOTES.md) | Ce qui a changé dans chaque version |
| [Journal des décisions](docs/ERWEITERUNGEN.md) | Pourquoi les choses sont construites ainsi, ce qui a été mesuré, ce qui reste ouvert |
| [LIESMICH](docs/LIESMICH.txt) | Guide rapide joint à chaque publication |
| [Composants tiers](THIRD_PARTY_NOTICES.md) | Code repris et ses licences |
| [Contribuer](CONTRIBUTING.md) · [Sécurité](SECURITY.md) | Contributions et signalement de failles de sécurité |

## Mentions légales et clause de non-responsabilité

> Cette section est une information générale et non un conseil juridique. Chaque utilisateur du projet est lui-même responsable de s’assurer que son utilisation est autorisée dans son pays et vis-à-vis de ses partenaires contractuels.

- **Pas un produit Sony.** « PlayStation », « PS5 » et les logos associés sont des marques de Sony Interactive Entertainment Inc. Ce projet n’a aucun lien avec Sony et n’est ni soutenu ni approuvé par Sony. Tous les autres noms appartiennent à leurs propriétaires respectifs.
- **Votre console, votre responsabilité.** Le programme ne fonctionne que sur une console que son propriétaire a lui-même modifiée. La modification peut enfreindre des conditions d’utilisation et entraîner la perte de la garantie, le bannissement du compte ou de la console, et dans certains pays avoir aussi des conséquences juridiques. Le risque incombe à celui qui modifie la console et utilise ce programme.
- **Pas de piratage.** Ce projet n’est **pas** conçu pour obtenir, diffuser ou utiliser des copies pirates, et il ne le soutient pas. Il ne contient aucun jeu, aucun firmware, aucune clé ni aucun code permettant de contourner une protection contre la copie ou un DRM. Il ne télécharge rien depuis Internet. La copie, le déplacement et la conversion ne s’appliquent qu’aux jeux déjà présents sous forme de dossier ou d’image sur votre propre console, et sont destinés aux copies de sécurité de jeux acquis légalement. La recherche, la division et l’installation de paquets ne s’appliquent qu’aux paquets que vous avez vous-même placés sur la console ou sur un lecteur branché ; l’app n’en procure aucun. La diffusion de contenus protégés par le droit d’auteur est punissable dans la plupart des pays. Aucune aide à ce sujet n’est fournie ici, pas même dans les issues.
- **Aucun fichier de Sony dans le dépôt.** Les bibliothèques système, le firmware et les fichiers de SDK propriétaires ne sont ni fournis ni acceptés (voir [CONTRIBUTING](CONTRIBUTING.md)). Les 30 images de profil ont été créées par l’auteur lui-même.
- **Aucune garantie, aucune responsabilité.** Le programme intervient sur le contrôle du ventilateur et sur des fonctions système de la console. Il est fourni tel quel, **sans aucune garantie** (GNU GPL, sections 15 et 16). L’auteur n’est pas responsable des dommages causés à la console, aux données ou aux comptes. Le programme ne remplace pas l’entretien : une console encrassée de poussière refroidit moins bien, quelle que soit la régulation du ventilateur.
- **Protection des données.** Les historiques, le journal et les paramètres restent sur la console, dans `/data/PS5-Cooling-Center/`. L’app n’envoie aucune donnée sur Internet et se connecte uniquement à des programmes de la console elle-même (chargeur de payloads, ShadowMountPlus) et aux navigateurs du réseau domestique qui ouvrent l’interface. Seule exception, dans le navigateur et non dans l’app : la page « Crédits » charge une fois, à l’ouverture, la petite icône de github.com pour savoir si le navigateur a accès à Internet ; c’est seulement dans ce cas que les liens vers GitHub sont cliquables. Les images de profil des développeurs sont intégrées à l’app et ne sont pas chargées depuis Internet.
- **Projets tiers.** Ils sont soumis à leurs propres licences, voir [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) et les crédits ci-dessous.

## Contribuer et signaler des bugs

Les bugs et suggestions sont les bienvenus sous forme d'[issue](../../issues). Merci d’indiquer le modèle de console, le firmware, la version du programme et, si possible, le journal exporté (page « Journal »). Contributions : [CONTRIBUTING.md](CONTRIBUTING.md). Ne signalez pas les failles de sécurité publiquement, mais comme décrit dans [SECURITY.md](SECURITY.md).

## Crédits et remerciements

Ce projet repose sur les épaules de la communauté homebrew PS5. Sans le travail de ces développeuses et développeurs, il n’y aurait ni la plateforme ni une partie des fonctions. **Merci beaucoup !**

**Plateforme et outils**

- **John Törnblom et tous les contributeurs de [ps5-payload-dev](https://github.com/ps5-payload-dev)** : le [SDK de payloads PS5](https://github.com/ps5-payload-dev/sdk), avec lequel chaque ELF de ce projet est compilé, le chargeur de payloads [elfldr](https://github.com/ps5-payload-dev/elfldr) sur le port 9021, [klogsrv](https://github.com/ps5-payload-dev/klogsrv) et [ftpsrv](https://github.com/ps5-payload-dev/ftpsrv) pour le développement, et [websrv](https://github.com/ps5-payload-dev/websrv), dont la méthode de lancement des jeux a servi de modèle ici.
- **Les développeurs de kstuff** : [sleirsgoevy](https://github.com/sleirsgoevy) (auteur principal), [EchoStretch](https://github.com/EchoStretch) et [drakmor](https://github.com/drakmor) (principaux contributeurs de [kstuff-lite](https://github.com/EchoStretch/kstuff-lite)) ; dépôts notamment chez [ps5-payload-dev](https://github.com/ps5-payload-dev/kstuff) et [EchoStretch](https://github.com/EchoStretch/kstuff). Sans kstuff, il n’y aurait pas d’accès au contrôleur du ventilateur.

**Code d’autres projets (porté ou intégré)**

- **[RenanGBarreto](https://github.com/RenanGBarreto), [rdmrocha](https://github.com/rdmrocha) et les contributeurs de [MkPFS](https://github.com/PSBrew/MkPFS)** ([PSBrew](https://github.com/PSBrew), GPL-3.0) : construction d’images exFAT, PFS et PFSC. La conversion sur la console en est une adaptation en C.
- **[SvenGDK](https://github.com/SvenGDK), [UFS2Tool](https://github.com/SvenGDK/UFS2Tool)** (BSD-2-Clause) : modèle de l’écriture UFS2/ffpkg.
- **[itsPLK](https://github.com/itsPLK), [ps5-pkg-manager](https://github.com/itsPLK/ps5-pkg-manager)** (GPL-3.0) : structure des paquets PS4/PS5, le format des parties (`PS5MPKG1`), le plan des dossiers de la recherche de paquets et le déroulement de l’installation (comment la bibliothèque système de la console est appelée et d’où elle lit le paquet). Les fonctions Paquets de cette app sont du code propre écrit d’après ce modèle ; rien n’en est intégré.
- **[phantomptr](https://github.com/phantomptr), [ps5upload](https://github.com/phantomptr/ps5upload)** (GPL-3.0) : structure de la fonction de profil et format de l’image de profil.
- **[Eric Biggers](https://github.com/ebiggers), [libdeflate](https://github.com/ebiggers/libdeflate)** (MIT) : compression rapide pour les images.
- **[Dave Gamble](https://github.com/DaveGamble) et contributeurs, [cJSON](https://github.com/DaveGamble/cJSON)** (MIT) : JSON.

**Interopérabilité, connaissances et modèles**

- **[drakmor](https://github.com/drakmor)** : [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus) (monter, déplacer et extraire des jeux ; la page Jeux s’appuie dessus ; son README indique la structure recommandée pour les images `.ffpkg` : blocs de 64 KiB) et [ps5-hwinfo](https://github.com/drakmor/ps5-hwinfo) (ordre des rails d’alimentation, fréquences).
- **[kerrdec97](https://github.com/kerrdec97), [exFAT Image Builder](https://github.com/kerrdec97/ps5-exfat-builder)** : a montré avec quels paramètres un `.ffpkg` est construit pour ShadowMountPlus (blocs et fragments de 64 KiB, pas d’espace réservé, densité d’inodes 262144, taille de secteur 512) et que la taille de secteur 4096 y produit (sous Windows) des images défectueuses. Uniquement des connaissances, pas de code.
- **[itsPLK](https://github.com/itsPLK)** : [ps5-unified-autoloader](https://github.com/itsPLK/ps5-unified-autoloader) (fermer le navigateur de la console) et [ps5-payload-manager](https://github.com/itsPLK/ps5-payload-manager), modèle de la gestion des payloads et de la règle qui définit quels processus peuvent être arrêtés.
- **[slopmaster33](https://github.com/slopmaster33), [webhb](https://github.com/slopmaster33/webhb)** (GPL-3.0) : l’encodeur QR qui affiche l’adresse de l’interface web sous forme de code (`src/qr.c`, repris et vérifié avec un lecteur).
- **[StonedModder](https://github.com/StonedModder), [ps-game-state-lib](https://github.com/StonedModder/ps-game-state-lib)** (MIT) : motifs du journal du noyau qui permettent de reconnaître le jeu en cours.
- **Le projet [etaHEN](https://github.com/etaHEN/etaHEN)** et **[onionHEN](https://github.com/aydencharles/onionHEN)** (aydencharles) : code source et documentation sur les appels système, les mesures et la requête de fréquence d’images.
- **[Soniciso](https://git.etawen.dev/soniciso), [Elf Arsenal](https://git.etawen.dev/soniciso/elf-arsenal)** (successeur de [Sonic Loader](https://git.etawen.dev/soniciso/sonicloader)) : forme des appels pour l’installation de la vignette et modèle pour de nombreuses fonctions (fermer le jeu, journal du noyau en direct, temps de jeu, données sauvegardées, gestionnaire de fichiers, suppression de jeux) ; le code a chaque fois été réécrit.
- **BestPig et [BackPork](https://github.com/BestPig/BackPork)** : le principe des bibliothèques de remplacement (fakelibs), que la page Jeux détecte et affiche.
- **Juma Sayeh (développeur) et Osama Abualia (tests), PS5 Game Compressor** : modèle pour la vérification du SDK pour le firmware 13.60 lors de la compilation et pour cinq idées lors de la copie et de la conversion : relire et vérifier entièrement les copies de sécurité après l’écriture, empêcher le mode repos pendant les longues opérations, ne lire et écrire simultanément qu’entre deux lecteurs différents, remplir explicitement de zéros les lacunes des images et laisser non compressés les blocs qui économisent moins de 5 %. Le code source du Game Compressor ne porte aucune licence, c’est pourquoi rien n’en a été repris : tout a été réécrit.
- **Noyau Linux, pilote `hid-playstation`** : documentation de l’octet d’état de la DualSense pour le niveau de batterie.
- **Xbox 360 DashLaunch** : modèle pour l’idée d’une régulation de température calme et lente.

**Un merci tout particulier à [Gezine](https://github.com/Gezine)** : son travail est fondamental pour la communauté homebrew PS5 – sans lui, aucun homebrew ne tournerait sur de nombreuses consoles, et donc cette app non plus.

**Merci à la communauté.** Leur code n’est pas dans cette app, mais sans leur travail partagé, la scène n’existerait pas sous cette forme : [owendswang](https://github.com/owendswang) ([ps5-web-file-manager](https://github.com/owendswang/ps5-web-file-manager), [ps5-fan-control](https://github.com/owendswang/ps5-fan-control)), [LightningMods](https://github.com/LightningMods) (etaHEN, [Itemzflow](https://github.com/LightningMods/Itemzflow)), [Andy Nguyen / TheFloW](https://github.com/TheOfficialFloW) ([PPPwn](https://github.com/TheOfficialFloW/PPPwn)), [Specter](https://github.com/Cryptogenic) ([PS5-IPV6-Kernel-Exploit](https://github.com/Cryptogenic/PS5-IPV6-Kernel-Exploit)), [ChendoChap](https://github.com/ChendoChap) ([pOOBs4](https://github.com/ChendoChap/pOOBs4)), [idlesauce](https://github.com/idlesauce) ([umtx2](https://github.com/idlesauce/umtx2)), [flatz](https://github.com/flatz) ([pkg_pfs_tool](https://github.com/flatz/pkg_pfs_tool)), [Al Azif](https://github.com/Al-Azif) ([ps4-exploit-host](https://github.com/Al-Azif/ps4-exploit-host)), [zecoxao](https://github.com/zecoxao) – et à tous les autres qui contribuent avec du code, des tests, des guides ou des réponses. Dans l’app, tous figurent avec leur image sur la page « Crédits ».

Les 30 images de profil ont été créées par l’auteur lui-même. Si vous ne vous trouvez pas ici ou si vous êtes mal décrit : signalez-le, la liste sera volontiers complétée.

## Licence

Copyright © 2026 strongt1me

Ce programme est un logiciel libre : il peut être redistribué et modifié selon les termes de la **GNU General Public License**, version 3 ou (à votre choix) toute version ultérieure, voir [LICENSE](LICENSE). Il est fourni sans aucune garantie.

Jusqu’à la version 1.45.1 incluse, le projet était sous licence MIT. À partir de 1.46.0, c’est la GPL-3.0-or-later qui s’applique : la conversion de jeux est adaptée de [MkPFS](https://github.com/PSBrew/MkPFS) (GPL-3.0), et le SDK de payloads PS5, avec lequel chaque ELF est compilé, est lui-même sous GPLv3+. Les composants tiers inclus et leurs licences sont indiqués dans [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

---

## Langues

Ce README existe aussi en [Deutsch](README.md), [English](README.en.md), [Italiano](README.it.md), [Español](README.es.md) et [Русский](README.ru.md). La version originale de ce README est la version allemande. Le journal des décisions, les documents pour développeurs et la description de l’interface de programmation (API) sont en allemand ; le manuel et la FAQ sont joints à chaque publication dans les six langues, en PDF et en HTML, et sont intégrés à l’app. Signalez volontiers les traductions manquantes ou maladroites par une issue (« Translation ») ; pour savoir comment les compléter, voir [docs/ENTWICKLUNG.md](docs/ENTWICKLUNG.md#übersetzungen).
