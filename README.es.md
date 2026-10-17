<div align="center">

<img src="web/img/app-mark.png" alt="" width="96" height="96">

# PS5 Cooling & System Center - Pro

**Control del ventilador, supervisión de temperatura y centro de sistema para la PlayStation 5 con jailbreak,<br>desde el navegador de la consola o cualquier dispositivo de la red doméstica, en seis idiomas.**

![Versión](https://img.shields.io/badge/Versi%C3%B3n-1.69.0-1f6feb)
![Licencia](https://img.shields.io/badge/Licencia-GPL--3.0--or--later-blue)
![Plataforma](https://img.shields.io/badge/Plataforma-PS5%20Payload-003791)
![Idiomas](https://img.shields.io/badge/Idiomas-DE%20%C2%B7%20EN%20%C2%B7%20IT%20%C2%B7%20ES%20%C2%B7%20FR%20%C2%B7%20RU-lightgrey)

[Funciones](#funciones) · [Inicio rápido](#inicio-rápido) · [Requisitos](#requisitos) · [Uso](#uso) · [Documentación](#documentación) · [Aviso legal](#aviso-legal-y-exención-de-responsabilidad) · [Créditos](#créditos-y-agradecimientos)

[Deutsch](README.md) · [English](README.en.md) · [Italiano](README.it.md) · **Español** · [Français](README.fr.md) · [Русский](README.ru.md)

<img src="docs/images/kuehlung.jpg" alt="Refrigeración: temperatura, ventilador, juego en marcha, temperatura objetivo y modo" width="860">

</div>

Un payload homebrew (ELF) que lee los sensores de temperatura de la PS5, regula el ventilador con una curva de confort silenciosa y ajustable e incluye una interfaz web. Además: gestión de juegos, archivos, payloads y perfil. Todo funciona en la propia consola, sin PC y sin Internet.

## Funciones

<table>
  <tr>
    <td width="33%" valign="top">
      <h3>🌡️ Refrigeración</h3>
      Regulación silenciosa del ventilador hacia una temperatura objetivo (60–91 °C), modos, perfiles por juego, modo nocturno con objetivo más silencioso, autotest del ventilador, límite de seguridad en el hardware.
    </td>
    <td width="33%" valign="top">
      <h3>📊 Mediciones</h3>
      CPU, chip principal, GPU (Pro), ventilador, carga de núcleos, frecuencias, líneas de alimentación, fotogramas y batería del mando, con historial.
    </td>
    <td width="33%" valign="top">
      <h3>🎮 Juegos</h3>
      Iniciar, cerrar, copiar, mover, convertir a exFAT/ffpkg/ffpfsc, asset packs LZ4, APR-EMU, PlayGo.
    </td>
  </tr>
  <tr>
    <td valign="top">
      <h3>💾 PS4 desde USB</h3>
      Jugar PKG falsos de PS4 directamente desde una unidad USB sin instalarlos (PS4Mount integrado), con actualizaciones y DLC.
    </td>
    <td valign="top">
      <h3>📦 Paquetes y partidas</h3>
      Buscar, dividir e instalar PKG; copiar y restaurar partidas guardadas; tiempo de juego con temperaturas.
    </td>
    <td valign="top">
      <h3>📁 Archivos y payloads</h3>
      Gestor de archivos para la consola y las unidades; iniciar y detener payloads, cargarlos en secuencia como perfiles.
    </td>
  </tr>
  <tr>
    <td valign="top">
      <h3>🖥️ Sistema y registro</h3>
      Modelo, firmware, red, pantalla; registro de eventos y registro del kernel en directo con grabación.
    </td>
    <td valign="top">
      <h3>🕹️ Atajos del mando</h3>
      L1/R1 cambian de página, L2/R2 desplazan; pulsar dos veces Silencio muestra temperatura y ventilador en la TV.
    </td>
    <td valign="top">
      <h3>🚀 Mosaico de inicio</h3>
      Una carpeta para <code>/data/homebrew</code>: un toque en su mosaico inicia la app y abre la interfaz.
    </td>
  </tr>
</table>

### Pantallas

<table>
  <tr>
    <td width="50%"><img src="docs/images/spiele.jpg" alt="Biblioteca de juegos"></td>
    <td width="50%"><img src="docs/images/ps4-von-usb.jpg" alt="Juegos de PS4 desde USB"></td>
  </tr>
  <tr>
    <td align="center"><sub>Juegos: biblioteca con iniciar, copiar, convertir</sub></td>
    <td align="center"><sub>PS4 desde USB: PKG falsos sin instalar</sub></td>
  </tr>
  <tr>
    <td><img src="docs/images/spielzeit.jpg" alt="Tiempo de juego"></td>
    <td><img src="docs/images/dateimanager.jpg" alt="Gestor de archivos"></td>
  </tr>
  <tr>
    <td align="center"><sub>Tiempo de juego con gráfico diario y temperaturas</sub></td>
    <td align="center"><sub>Gestor de archivos para la consola y unidades USB</sub></td>
  </tr>
  <tr>
    <td><img src="docs/images/payloads.jpg" alt="Payloads"></td>
    <td><img src="docs/images/system.jpg" alt="Sistema"></td>
  </tr>
  <tr>
    <td align="center"><sub>Payloads: perfiles, en marcha y guardados</sub></td>
    <td align="center"><sub>Sistema: consola, red, pantalla</sub></td>
  </tr>
</table>

<details>
<summary><b>Más pantallas</b> (refrigeración en detalle, partidas, paquetes, perfil, registro, móvil)</summary>
<br>
<table>
  <tr>
    <td width="50%"><img src="docs/images/kuehlung-diagramme.jpg" alt="Historial de temperatura, sensores y perfiles por juego"></td>
    <td width="50%"><img src="docs/images/spielstaende.jpg" alt="Copia de partidas guardadas"></td>
  </tr>
  <tr>
    <td><img src="docs/images/pakete.jpg" alt="Instalar paquetes"></td>
    <td><img src="docs/images/profil.jpg" alt="Perfil con 30 imágenes integradas"></td>
  </tr>
  <tr>
    <td><img src="docs/images/protokoll.jpg" alt="Registro"></td>
    <td align="center"><img src="docs/images/kuehlung-mobil.jpg" alt="Refrigeración en el móvil" width="240"></td>
  </tr>
</table>
<sub>Todas las imágenes con datos de ejemplo y juegos inventados; la interfaz se muestra en alemán.</sub>
</details>

<details>
<summary><b>Todas las funciones en detalle</b></summary>

**Refrigeración**
- Mantiene una temperatura objetivo (66 °C por defecto, ajustable 60–91 °C) con media móvil, zona muerta y tendencia, y cambia la velocidad solo en pasos pequeños.
- Modos silencioso, equilibrado y fresco, selección rápida y reglas por juego.
- Por encima de la temperatura de seguridad (78 °C por defecto) solo decide el hardware; si la app no está en marcha, la consola regula sola.

**Juegos**
- Iniciar (desde el navegador de la consola la página se cierra al instante), cerrar, copiar a otras unidades, mover o descomprimir con ShadowMountPlus.
- Convertir a imágenes exFAT, ffpkg y ffpfsc sin PC; todo se relee y comprueba tras escribir, con un archivo `.sha256`.
- Asset packs LZ4, índice AMPR, APR-EMU, stub de PlayGo y módulos DLC por juego; comprobar, descomprimir y reparar imágenes.
- PS4 desde USB: PKG falsos de PS4 desde una unidad USB a la biblioteca sin instalarlos (PS4Mount de gasb07).
- Tiempo de juego con temperaturas, copia y restauración de partidas, buscar, dividir e instalar paquetes, historial de todas las tareas.

**Gestión**
- Gestor de archivos: ver, descargar y subir, crear carpetas, renombrar, copiar, mover, borrar (cambios solo en unidades y en `/data`).
- Payloads: ver y detener los que están en marcha, iniciar `.elf` propios desde una carpeta o un USB, perfiles como secuencia de inicio.
- Perfil: nombre visible e imagen de perfil (30 imágenes integradas o un archivo propio).
- Sistema: modelo, firmware, tiempo activo, almacenamiento, red; sensores en bruto y diagnóstico en modo experto.
- Cabecera en cada página: modo reposo, reiniciar, apagar, modo seguro (dos clics cada uno).

**Técnica**
- Servidor HTTP propio sin bibliotecas externas, [interfaz JSON](docs/API.md) documentada, ajustes en `/data/PS5-Cooling-Center/config.json`.
- Sin acceso a Internet: la app solo habla con programas de la consola y con los navegadores de la red doméstica.

</details>

## Inicio rápido

1. Descargar `PS5_Cooling_System_Center_v<versión>.elf` de las [Releases](../../releases).
2. Enviarlo a la consola (cargador ELF, puerto **9021**), por ejemplo:
   ```bash
   nc -q0 <IP-PS5> 9021 < PS5_Cooling_System_Center_v1.69.0.elf
   ```
3. Abrir **`http://<IP-PS5>:8086`** en el navegador (la notificación arriba a la izquierda en la TV también muestra la dirección).

> [!TIP]
> **Iniciar sin enviar:** descomprimir `PPSA86921.zip` de la release y copiar la carpeta a `/data/homebrew/`. ShadowMountPlus crea un mosaico; un toque inicia la app y abre la interfaz.

> [!IMPORTANT]
> Solo puede funcionar **una** instancia a la vez; si no, dos programas regulan el ventilador uno contra otro. Una instancia antigua se detiene en la página "Payloads". Más en el [LIESMICH](docs/LIESMICH.txt) (en alemán).

## Requisitos

| | |
| --- | --- |
| **Consola** | PS5 con jailbreak y cargador ELF en el puerto 9021 ([elfldr](https://github.com/ps5-payload-dev/elfldr) o similar) |
| **Probado** | PS5 Pro (CFI-7021), firmware 12.00; varios usuarios informan que también funciona en 13.60 |
| **Ventilador** | [kstuff](https://github.com/EchoStretch/kstuff-lite) (sin él funcionan sensores e interfaz, la regulación no) |
| **Red** | Un navegador en la misma red, o el navegador de la consola |
| **Opcional** | [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus) para mover, descomprimir y el mosaico de inicio |

<details>
<summary>Firmware y SDK en detalle</summary>

El ELF se compila con el PS5 payload SDK **v0.43**, cuyo código de arranque conoce el firmware **hasta 13.60**. Un firmware que el código de arranque no conoce nunca llega a `main()`: el programa no arranca y no escribe nada en el registro. Para 13.00–13.40 aún no hay informes (el código de arranque los conoce). La temperatura de la GPU solo existe en la Pro.
</details>

## Uso

| Página | Contenido |
| --- | --- |
| **Refrigeración** | Estado, historial, sensores, temperatura objetivo, modo, perfiles por juego |
| **Juegos** | Biblioteca, tiempo de juego, partidas, paquetes, PS4 desde USB, copias, historial |
| **Gestor de archivos** | Carpetas de la consola y de las unidades |
| **Payloads** | Payloads en marcha, guardados y en USB, perfiles |
| **Sistema** | Consola, almacenamiento, red, diagnóstico |
| **Perfil** | Nombre visible e imagen de perfil |
| **Registro** | Eventos de la app y registro del kernel en directo |
| **Créditos** | Agradecimientos; además **manual** y **FAQ** en seis idiomas |

**Con el mando (navegador de la consola):** L1 / R1 página anterior / siguiente · L2 / R2 desplazar arriba / abajo · Silencio dos veces: temperatura y ventilador en la TV.

> [!WARNING]
> La interfaz **no tiene inicio de sesión**: cualquier dispositivo de la red doméstica puede leer y cambiar cosas. No configures reenvío del puerto 8086. Para permitir solo el navegador de la consola, pon `bind_address` en `127.0.0.1`. Detalles en el [manual](docs/HANDBUCH.md#zugriff-und-sicherheit-im-heimnetz) y en la [política de seguridad](SECURITY.md).

## Documentación

| Documento | Contenido |
| --- | --- |
| [Manual](docs/HANDBUCH.md) | Uso en detalle (en alemán; también en la app, en seis idiomas) |
| [API](docs/API.md) | Todos los endpoints y campos de configuración |
| [Desarrollo](docs/ENTWICKLUNG.md) | Compilar, enviar, resolver problemas, estructura del código |
| [Cambios](CHANGELOG.md) | Qué cambió en cada versión |
| [Registro de decisiones](docs/ERWEITERUNGEN.md) | Por qué las cosas son así, qué se midió |
| [Componentes de terceros](THIRD_PARTY_NOTICES.md) | Código adoptado y sus licencias |
| [Colaborar](CONTRIBUTING.md) · [Seguridad](SECURITY.md) | Aportaciones y aviso de vulnerabilidades |

<details>
<summary><b>Compilar desde el código fuente</b></summary>

Se necesita el [PS5 payload SDK](https://github.com/ps5-payload-dev/sdk) **v0.42 o posterior** (probado con v0.43); la compilación lo comprueba y se detiene con un SDK más antiguo. En Windows basta Git Bash con LLVM 21:

```bash
tools/build-windows.sh
```

En Linux o WSL:

```bash
export PS5_PAYLOAD_SDK=$HOME/ps5sdk/sdk
make
```

El resultado es `PS5_Cooling_Center.elf`; `tools/make_starter.sh` crea el mosaico de inicio. Todo lo demás en [docs/ENTWICKLUNG.md](docs/ENTWICKLUNG.md).
</details>

## Aviso legal y exención de responsabilidad

> [!CAUTION]
> **No es un producto de Sony, sin piratería, sin responsabilidad.** Este proyecto no tiene relación con Sony. No contiene juegos, firmware, claves de Sony (solo una clave públicamente conocida para paquetes de prueba propios, véase THIRD_PARTY_NOTICES.md) ni código para eludir protecciones anticopia, y no descarga nada de Internet. Modificar una consola puede costar la garantía y la cuenta; el riesgo es de quien lo hace. El programa se ofrece **sin ninguna garantía**.

<details>
<summary>Texto completo</summary>

> Esta sección es información general, no asesoramiento jurídico. Quien use el proyecto es responsable de que su uso esté permitido en su país y frente a sus socios contractuales.

- **No es un producto de Sony.** "PlayStation", "PS5" y los logotipos correspondientes son marcas de Sony Interactive Entertainment Inc. Este proyecto no tiene relación con Sony y no está respaldado ni aprobado por Sony. Los demás nombres pertenecen a sus respectivos titulares.
- **Tu consola, tu responsabilidad.** El programa solo funciona en una consola modificada por su propietario. Modificarla puede infringir las condiciones de uso y suponer la pérdida de la garantía o el bloqueo de la cuenta o de la consola, y en algunos países consecuencias legales. El riesgo es de quien modifica la consola y usa este programa.
- **Sin piratería.** Este proyecto **no** sirve para obtener, distribuir o usar copias piratas, y no lo apoya. Copiar, mover y convertir solo actúan sobre juegos que ya están en tu consola y sirven para copias de seguridad de juegos adquiridos legalmente. Buscar, dividir, instalar y montar paquetes desde USB solo actúan sobre paquetes que tú mismo pusiste en la consola o en una unidad; la app no consigue ninguno. Distribuir contenido protegido por derechos de autor es delito en la mayoría de los países. Aquí no hay ayuda para ello, tampoco en las issues.
- **Ningún archivo de Sony en el repositorio.** Bibliotecas del sistema, firmware y archivos propietarios del SDK no se incluyen ni se aceptan (ver [CONTRIBUTING](CONTRIBUTING.md)). Las 30 imágenes de perfil las creó el autor.
- **Sin garantía, sin responsabilidad.** El programa interviene en el control del ventilador y en funciones del sistema de la consola. Se ofrece tal cual, **sin ninguna garantía** (GNU GPL, secciones 15 y 16). El autor no responde de daños en consolas, datos o cuentas. Una consola con polvo refrigera peor, se regule como se regule el ventilador.
- **Privacidad.** Historial, registro y ajustes se quedan en la consola en `/data/PS5-Cooling-Center/`. La app no envía datos a Internet. Única excepción en el navegador: la página "Créditos" carga una vez el pequeño icono de github.com para ver si el navegador tiene Internet; solo entonces los enlaces se pueden pulsar.
- **Los proyectos de terceros** tienen sus propias licencias, ver [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
</details>

## Colaborar y notificar errores

Errores y deseos son bienvenidos como [issue](../../issues), con modelo de consola, firmware, versión y el registro exportado (página "Registro"). Aportaciones: [CONTRIBUTING.md](CONTRIBUTING.md). Las vulnerabilidades se notifican como se describe en [SECURITY.md](SECURITY.md).

## Créditos y agradecimientos

Este proyecto se apoya en los hombros de la comunidad homebrew de PS5. **¡Gracias!** Un agradecimiento muy especial a **[Gezine](https://github.com/Gezine)**: sin el trabajo de Gezine no funcionaría ningún homebrew en muchas consolas, y por tanto tampoco esta app.

<details>
<summary><b>Plataforma y herramientas</b></summary>

- **John Törnblom y todos los colaboradores de [ps5-payload-dev](https://github.com/ps5-payload-dev)**: el [PS5 payload SDK](https://github.com/ps5-payload-dev/sdk), el cargador [elfldr](https://github.com/ps5-payload-dev/elfldr), [klogsrv](https://github.com/ps5-payload-dev/klogsrv), [ftpsrv](https://github.com/ps5-payload-dev/ftpsrv) y [websrv](https://github.com/ps5-payload-dev/websrv).
- **Los desarrolladores de kstuff**: [sleirsgoevy](https://github.com/sleirsgoevy), [EchoStretch](https://github.com/EchoStretch) y [drakmor](https://github.com/drakmor) ([kstuff-lite](https://github.com/EchoStretch/kstuff-lite)). Sin kstuff no habría acceso al controlador del ventilador.
</details>

<details>
<summary><b>Código de otros proyectos (portado o incluido)</b></summary>

- **[gasb07](https://github.com/gasb07), [PS4Mount](https://github.com/gasb07/ps4mount)** (GPL-3.0): PKG falsos de PS4 desde USB sin instalar; el código está integrado como pestaña "PS4 desde USB".
- **[RenanGBarreto](https://github.com/RenanGBarreto), [rdmrocha](https://github.com/rdmrocha) y los colaboradores de [MkPFS](https://github.com/PSBrew/MkPFS)** ([PSBrew](https://github.com/PSBrew), GPL-3.0): imágenes exFAT, PFS y PFSC.
- **[SvenGDK](https://github.com/SvenGDK), [UFS2Tool](https://github.com/SvenGDK/UFS2Tool)** (BSD-2-Clause): modelo del escritor UFS2/ffpkg.
- **[drakmor](https://github.com/drakmor)**: [ampr_emu](https://github.com/drakmor/ampr_emu) (asset packs LZ4, APR-EMU), [pgo_stub](https://github.com/drakmor/pgo_stub) y [dlc_emu](https://github.com/drakmor/dlc_emu) (versiones integradas, GPL-3.0).
- **[BlackBearReloaded](https://github.com/BlackBearReloaded), ps5-native-app-boilerplate** (GPL-3.0): cómo se construye el mosaico de inicio como título nativo.
- **[itsPLK](https://github.com/itsPLK), [ps5-pkg-manager](https://github.com/itsPLK/ps5-pkg-manager)** (GPL-3.0): estructura de los paquetes y proceso de instalación (código propio según este modelo).
- **[phantomptr](https://github.com/phantomptr), [ps5upload](https://github.com/phantomptr/ps5upload)** (GPL-3.0): función de perfil y formato de la imagen de perfil.
- **[slopmaster33](https://github.com/slopmaster33), [webhb](https://github.com/slopmaster33/webhb)** (GPL-3.0): codificador QR.
- **[danthespal](https://github.com/danthespal), [MkPFS.CSharp](https://github.com/danthespal/MkPFS.CSharp)** (GPL-3.0): los cuatro perfiles LZ4 probados (ASTRO BOT, EA Sports UFC 6, motor de Insomniac, motor Unity), integrados sin cambios.
- **[Eric Biggers](https://github.com/ebiggers), [libdeflate](https://github.com/ebiggers/libdeflate)** (MIT) · **[Dave Gamble](https://github.com/DaveGamble), [cJSON](https://github.com/DaveGamble/cJSON)** (MIT) · **[LZ4](https://github.com/lz4/lz4)** (BSD-2-Clause).
</details>

<details>
<summary><b>Colaboración, conocimientos y modelos</b></summary>

- **[drakmor](https://github.com/drakmor)**: [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus) (montar, mover, descomprimir juegos) y [ps5-hwinfo](https://github.com/drakmor/ps5-hwinfo) (líneas de alimentación, frecuencias).
- **V-10, [exFAT2AMPR](https://github.com/exFAT2AMPR/exfat2ampr)**: lista para "Automático", grabación para crear perfiles, mantenimiento de los plugins de juegos.
- **theghostonline (Husam Osman), [PS5 LZ4 Compression](https://github.com/theghostonline/PS5-LZ4-Compression)**: archivos que deben quedar sueltos al empaquetar, permisos 0777, reparación al comprobar los paquetes, elección de los perfiles probados.
- **[kerrdec97](https://github.com/kerrdec97), [exFAT Image Builder](https://github.com/kerrdec97/ps5-exfat-builder)**: parámetros para imágenes `.ffpkg`.
- **[itsPLK](https://github.com/itsPLK)**: [ps5-unified-autoloader](https://github.com/itsPLK/ps5-unified-autoloader) y [ps5-payload-manager](https://github.com/itsPLK/ps5-payload-manager).
- **[StonedModder](https://github.com/StonedModder), [ps-game-state-lib](https://github.com/StonedModder/ps-game-state-lib)** (MIT): detección del juego en marcha.
- **[etaHEN](https://github.com/etaHEN/etaHEN)** y **[onionHEN](https://github.com/aydencharles/onionHEN)**: llamadas al sistema, mediciones, fotogramas.
- **[Soniciso](https://git.etawen.dev/soniciso), [Elf Arsenal](https://git.etawen.dev/soniciso/elf-arsenal)**: modelo de muchas funciones (código reescrito cada vez).
- **BestPig, [BackPork](https://github.com/BestPig/BackPork)**: el principio de las bibliotecas sustitutas.
- **Juma Sayeh y Osama Abualia, PS5 Game Compressor**: ideas para comprobar y convertir (todo reescrito; su código no tiene licencia).
- **Controlador Linux `hid-playstation`** (batería del DualSense) · **Xbox 360 DashLaunch** (la idea de la regulación tranquila).
</details>

<details>
<summary><b>Gracias a la comunidad</b></summary>

Su código no está en esta app, pero sin su trabajo compartido la escena no existiría así: [owendswang](https://github.com/owendswang), [LightningMods](https://github.com/LightningMods), [Andy Nguyen / TheFloW](https://github.com/TheOfficialFloW), [Specter](https://github.com/Cryptogenic), [ChendoChap](https://github.com/ChendoChap), [idlesauce](https://github.com/idlesauce), [flatz](https://github.com/flatz), [Al Azif](https://github.com/Al-Azif), [zecoxao](https://github.com/zecoxao), [ArkSama](https://github.com/ArkSama), [nexgen999](https://github.com/nexgen999), [matem6](https://github.com/matem6), [Phoenixx1202](https://github.com/Phoenixx1202), [SiSTR0](https://github.com/SiSTR0) – y a todos los demás que contribuyen con código, pruebas, guías o respuestas. En la app aparecen todos con imagen en la página "Créditos".
</details>

Si falta alguien o está mal descrito, avísanos: la lista se ampliará con gusto.

## Licencia

Copyright © 2026 strongt1me · **GNU General Public License v3.0 o posterior**, ver [LICENSE](LICENSE). Hasta la 1.45.1 el proyecto era MIT; desde la 1.46.0 es GPL-3.0-or-later porque contiene partes de proyectos GPL (detalles en [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).

<sub>Este README también está en [Deutsch](README.md), [English](README.en.md), [Italiano](README.it.md), [Français](README.fr.md) y [Русский](README.ru.md). El manual y las FAQ acompañan a cada release en los seis idiomas.</sub>
