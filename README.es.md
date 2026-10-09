<div align="center">

<img src="web/img/app-mark.png" alt="" width="96" height="96">

# PS5 Cooling & System Center - Pro

**Control del ventilador, supervisión de temperatura y centro de sistema para la PlayStation 5 con jailbreak, con interfaz web en la red doméstica en seis idiomas.**

![Versión](https://img.shields.io/badge/Versi%C3%B3n-1.55.0-1f6feb)
![Licencia](https://img.shields.io/badge/Licencia-GPL--3.0--or--later-blue)
![Plataforma](https://img.shields.io/badge/Plataforma-PS5%20Payload-003791)
![Idiomas](https://img.shields.io/badge/Idiomas-DE%20%C2%B7%20EN%20%C2%B7%20IT%20%C2%B7%20ES%20%C2%B7%20FR%20%C2%B7%20RU-lightgrey)

[Funciones](#funciones) · [Instalación](#instalación) · [Uso](#uso) · [Documentación](#documentación) · [Aviso legal](#aviso-legal-y-exención-de-responsabilidad) · [Créditos](#créditos-y-agradecimientos)

[Deutsch](README.md) · [English](README.en.md) · [Italiano](README.it.md) · **Español** · [Français](README.fr.md) · [Русский](README.ru.md)

</div>

---

PS5 Cooling & System Center - Pro es un payload homebrew (ELF) para una PlayStation 5 con jailbreak. Lee los sensores de temperatura de la consola, regula el ventilador según una curva de confort tranquila y configurable, e incluye una interfaz web que puedes abrir desde cualquier dispositivo de la red doméstica: móvil, tableta o PC. Además ofrece gestión de juegos, gestión de payloads y la gestión de perfiles de la consola. Todo se ejecuta en la propia PS5, sin PC y sin Internet.

El proyecto es el sucesor de *PS5 Temperature Manager*; el backend (C) y la interfaz se han reescrito desde cero.

<p align="center">
  <img src="docs/images/kuehlung.jpg" alt="Refrigeración: temperatura, ventilador, historial y sensores" width="860">
</p>

<table>
  <tr>
    <td width="50%"><img src="docs/images/payloads.jpg" alt="Gestión de payloads"></td>
    <td width="50%"><img src="docs/images/profil.jpg" alt="Perfil con 30 imágenes de perfil integradas"></td>
  </tr>
  <tr>
    <td align="center"><sub>Iniciar, copiar y finalizar payloads (datos de ejemplo)</sub></td>
    <td align="center"><sub>Elegir la imagen de perfil entre 30 imágenes integradas</sub></td>
  </tr>
</table>

<details>
<summary>También se puede usar desde el móvil</summary>
<p align="center"><img src="docs/images/kuehlung-mobil.jpg" alt="Refrigeración en el móvil" width="300"></p>
</details>

## Funciones

**Refrigeración**

- **Control de confort del ventilador.** Mantiene una temperatura objetivo (66 °C por defecto, ajustable de 60 a 91 °C; 91 °C es el valor de la propia consola) con media móvil, zona muerta y tendencia, y cambia la velocidad solo en pasos pequeños. El objetivo no es la temperatura más baja, sino un ventilador que suene tranquilo y uniforme. Modos de funcionamiento (silencioso, equilibrado, fresco), selección rápida y reglas propias por juego.
- **La seguridad, lo primero.** A partir de la temperatura de seguridad (78 °C por defecto) solo cuenta el hardware. Si la app no se está ejecutando, la consola regula con su propia curva.
- **Mediciones.** Procesador, chip principal, gráficos (solo PS5 Pro), velocidad del ventilador, carga de todos los núcleos de la CPU, frecuencia en directo, consumo de los raíles de alimentación, frecuencia de imagen en juego y batería del mando. Historiales de 2 minutos y de 24 horas, y una evaluación semanal del rendimiento de refrigeración.
- **Notificaciones en el televisor.** Al iniciar, ante advertencias y, si se desea, de forma periódica. Dos pulsaciones breves del botón de micrófono del mando muestran la temperatura del procesador y el ventilador.

**Gestión**

- **Juegos.** Los juegos de la pantalla de inicio con portada, tiempo de juego, formato y ubicación. Iniciarlos directamente (si se está ejecutando otro juego, un aviso lo indica; «Cerrar juego» lo cierra al instante y después el siguiente se inicia con un toque), copiarlos a otras unidades, moverlos o extraerlos con ShadowMountPlus y convertirlos sin PC en imágenes exFAT, ffpkg y ffpfsc. Una segunda pestaña registra el tiempo de juego: cuándo se jugó, cuánto tiempo y cuánto se calentó la consola, con totales, barras diarias y clasificación. Una tercera hace copias de seguridad de los datos guardados en una memoria USB, un disco o el almacenamiento de la consola y, si se desea, restaura un título: sin modificarlos y cifrados, con cada archivo releído y verificado; antes de restaurar se hace una copia de seguridad aparte del estado actual. Las copias y las imágenes se releen por completo y se verifican tras escribirlas; junto a ellas queda un archivo `.sha256` que más tarde se puede volver a comprobar en el PC con `sha256sum -c`. Un interruptor «Guardar portadas y metadatos» guarda en la consola las portadas y los datos de los juegos que tardan en obtenerse (carpeta `covers_and_more`), para que la lista cargue más rápido. Una cuarta pestaña, «Paquetes», encuentra los paquetes de juegos (`.pkg`) en memorias USB, discos y en el almacenamiento de la consola, los muestra con imagen, versión y tipo, divide paquetes grandes en partes para memorias USB FAT32 o discos (releídas y verificadas; el paquete no se modifica) y, si se desea, instala un paquete mediante la propia instalación de la consola: la app solo se lo pone a su disposición, comprueba antes qué podría impedirlo, muestra el progreso y no elimina ni sobrescribe nada.
- **Archivos.** Un gestor de archivos para las carpetas de la consola: ver, descargar, subir, crear carpetas, cambiar nombre, copiar, mover y eliminar (modificar solo en las unidades y en `/data`; eliminar pide confirmación dos veces). La lista se puede ordenar por nombre, tamaño o fecha, se pueden seleccionar todas las entradas a la vez, el tamaño de una carpeta se calcula a petición y «Ver» muestra imágenes y archivos de texto directamente en el navegador.
- **Payloads.** Ver y finalizar los payloads en ejecución. Iniciar archivos `.elf` propios desde una carpeta de la consola o desde una memoria USB, o copiarlos a esa carpeta, sin necesidad de PC.
- **Perfil.** Cambiar el nombre visible; imagen de perfil entre 30 imágenes integradas o desde un archivo propio, con copia de seguridad de la imagen anterior.
- **Sistema.** Modelo, firmware, tiempo en marcha, almacenamiento y red. Sensores en bruto y diagnóstico en el modo experto. Registro de eventos exportable.
- **Barra superior.** En cada página: modo de reposo, reiniciar, apagar y modo seguro (cada uno tras dos clics, agrupados en el centro), pantalla completa y modo experto.
- **Idiomas.** La interfaz está disponible en alemán, inglés, italiano, español, francés y ruso; el idioma se elige arriba a la derecha (en la primera visita se usa el del navegador). El manual y las FAQ están en los seis idiomas en la app y en PDF para descargar. Los mensajes que la propia consola muestra en el televisor están en alemán.
- **Icono de la pantalla de inicio.** Abre la interfaz directamente en el navegador de la consola.

**Técnica.** Servidor HTTP propio sin bibliotecas de terceros (los archivos de la interfaz se envían comprimidos por la red), [interfaz JSON](docs/API.md) documentada, ajustes en `/data/PS5-Cooling-Center/config.json`, sin acceso a Internet (la app solo se conecta con programas de la propia consola).

## Requisitos

| | |
| --- | --- |
| **Consola** | PS5 con jailbreak y un cargador de ELF en el **puerto 9021** ([elfldr](https://github.com/ps5-payload-dev/elfldr) o equivalente). Probado en una **PS5 Pro (CFI-7021) con firmware 12.00**. Varios usuarios informan de que la app también funciona en una PS5 con **firmware 13.60**. Otros modelos y versiones de firmware no se han probado. La temperatura de gráficos solo está disponible en la Pro. |
| **Firmware** | El ELF se compila con el PS5-Payload-SDK **v0.43**, cuyo código de arranque conoce el firmware **hasta la 13.60**. Con un firmware que el código de arranque no conoce, no se llega a `main()`: el programa ni siquiera se inicia y no escribe nada en el registro. Varios usuarios informan de que la app funciona en la **13.60**; de la 13.00 a la 13.40 no hay informes (el código de arranque las conoce). |
| **kstuff** | Necesario para el control del ventilador (`/dev/icc_fan`). Sin kstuff, los sensores y la interfaz siguen funcionando y la regulación indica «no disponible». |
| **Red** | Un navegador en la misma red que la consola. |
| **opcional** | [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus) para mover y extraer juegos, y para detectar el formato y la ubicación. |

## Instalación

1. Descarga de las **Releases** el archivo `PS5_Cooling_System_Center_v<Version>.elf`.
2. Envía el ELF a la consola, puerto **9021**, con cualquier emisor de payloads o por línea de comandos:

   ```bash
   nc -q0 <PS5-IP> 9021 < PS5_Cooling_System_Center_v1.55.0.elf
   ```

3. En el televisor aparece una notificación con la dirección. Ábrela en el navegador: **`http://<PS5-IP>:8086`**
4. El programa crea por sí mismo el icono de la pantalla de inicio (sección «Multimedia») en el primer inicio; no hace falta ningún instalador. El icono sobrevive a los reinicios, pero no inicia el programa: solo abre la interfaz. Si más adelante falta, «Instalar icono» en la página Sistema lo recupera; como alternativa se incluye `cooling-center-launcher-installer_v<Version>.elf`.

Tras cada reinicio de la consola hay que volver a enviar el ELF, por ejemplo con un autoloader. Solo debe ejecutarse **una** instancia: dos instancias regularían el ventilador una contra otra. Una instancia antigua se puede finalizar en la página «Payloads». Las instrucciones detalladas se incluyen en la publicación como [LIESMICH](docs/LIESMICH.txt).

## Uso

| Página | Contenido |
| --- | --- |
| **Perfil** | Nombre visible e imagen de perfil de la consola |
| **Juegos** | Iniciar, copiar, mover y convertir juegos; tiempo de juego con temperaturas; copia de seguridad y restauración de datos guardados |
| **Archivos** | Gestor de archivos: ver carpetas, subir y descargar archivos, copiar, mover, eliminar |
| **Payloads** | payloads en ejecución, guardados y en USB |
| **Refrigeración** | Estado, historial, sensores, temperatura objetivo, modo de funcionamiento, perfiles de juego |
| **Sistema** | Consola, almacenamiento, red, diagnóstico, apagado y reinicio |
| **Registro** | Eventos de la app, exportables como `.log`, y el registro del kernel de la consola en directo con filtro, pausa, guardado en la consola y grabación |
| **Créditos** | Agradecimiento a los desarrolladores cuyo trabajo está en la app; además, **Manual** y **FAQ** en tu idioma |

Todo lo demás está en el [manual](docs/HANDBUCH.md): el control de confort y sus parámetros, iniciar payloads, imágenes de perfil, icono, indicador en pantalla y ajustes.

**Acceso en la red doméstica.** La interfaz no tiene inicio de sesión: cualquier dispositivo de la red doméstica puede leer y modificar. La consola no debe quedar expuesta a Internet, así que **no configures ningún reenvío de puertos** al puerto 8086. Si no quieres esto, pon `bind_address` en `127.0.0.1`. El servidor rechaza las solicitudes que claramente proceden de páginas web ajenas. Los detalles están en el [manual](docs/HANDBUCH.md#zugriff-und-sicherheit-im-heimnetz) y en la [política de seguridad](SECURITY.md).

## Compilar desde el código fuente

Se necesita el [PS5-Payload-SDK](https://github.com/ps5-payload-dev/sdk) **v0.42 o posterior** (compilado y probado con **v0.43**): su código de arranque decide en qué firmware llega a arrancar el ELF, y v0.41 termina en la 13.40. La compilación lo comprueba (`tools/check_sdk_firmware.py`) y se interrumpe con un SDK demasiado antiguo, y también tras el enlazado si el ELF terminado no contiene el caso de la 13.60. En Windows basta con Git Bash y LLVM 21 (no 18):

```bash
tools/build-windows.sh
```

En Linux o WSL:

```bash
export PS5_PAYLOAD_SDK=$HOME/ps5sdk/sdk
make
```

El resultado es `PS5_Cooling_Center.elf`. Todos los pasos, la resolución de problemas, el proceso de publicación y la estructura del código fuente se describen en [docs/ENTWICKLUNG.md](docs/ENTWICKLUNG.md).

## Documentación

| Documento (en alemán) | Contenido |
| --- | --- |
| [Manual](docs/HANDBUCH.md) | Uso en detalle (también en la app y en PDF en seis idiomas; ver Releases) |
| [API](docs/API.md) | Todos los endpoints y campos de configuración |
| [Desarrollo](docs/ENTWICKLUNG.md) | Compilar, enviar, resolución de problemas, estructura del código fuente |
| [Notas de la versión](docs/RELEASE_NOTES.md) | Qué ha cambiado en cada versión |
| [Registro de decisiones](docs/ERWEITERUNGEN.md) | Por qué algo está hecho así, qué se ha medido, qué está pendiente |
| [LIESMICH](docs/LIESMICH.txt) | Guía breve incluida en cada publicación |
| [Componentes de terceros](THIRD_PARTY_NOTICES.md) | Código adoptado y sus licencias |
| [Colaborar](CONTRIBUTING.md) · [Seguridad](SECURITY.md) | Contribuciones y notificación de vulnerabilidades |

## Aviso legal y exención de responsabilidad

> Esta sección es información general y no constituye asesoramiento jurídico. Quien usa el proyecto es responsable de que su uso esté permitido en su país y frente a sus contrapartes contractuales.

- **No es un producto de Sony.** «PlayStation», «PS5» y los logotipos correspondientes son marcas comerciales de Sony Interactive Entertainment Inc. Este proyecto no tiene ninguna relación con Sony y no cuenta con su apoyo ni con su aprobación. Todos los demás nombres pertenecen a sus respectivos titulares.
- **Tu consola, tu responsabilidad.** El programa solo funciona en una consola que su propietario ha modificado. La modificación puede infringir las condiciones de uso y provocar la pérdida de la garantía o el bloqueo de la cuenta o de la consola, y en algunos países también tener consecuencias legales. El riesgo lo asume quien modifica la consola y usa este programa.
- **Nada de piratería.** Este proyecto **no** está pensado para obtener, distribuir ni usar copias piratas, ni lo apoya. No contiene juegos, firmware, claves ni código para eludir la protección anticopia o el DRM. No descarga nada de Internet. Copiar, mover y convertir solo actúan sobre juegos que ya están como carpeta o imagen en la propia consola, y están pensados para copias de seguridad de juegos adquiridos legalmente. Buscar, dividir e instalar paquetes solo actúan sobre paquetes que tú mismo has puesto en la consola o en una unidad conectada; la app no obtiene ninguno. Distribuir contenido protegido por derechos de autor es delito en la mayoría de los países. Aquí no se ofrece ayuda para ello, tampoco en los issues.
- **Ningún archivo de Sony en el repositorio.** No se incluyen ni se aceptan bibliotecas del sistema, firmware ni archivos propietarios del SDK (ver [CONTRIBUTING](CONTRIBUTING.md)). Las 30 imágenes de perfil las ha generado el propio autor.
- **Sin garantía, sin responsabilidad.** El programa interviene en el control del ventilador y en funciones del sistema de la consola. Se proporciona tal cual, **sin ninguna garantía** (GNU GPL, secciones 15 y 16). El autor no se hace responsable de daños en la consola, los datos o las cuentas. El programa no sustituye al mantenimiento: una consola llena de polvo refrigera peor, se regule como se regule el ventilador.
- **Privacidad.** Los historiales, el registro y los ajustes se quedan en la consola, en `/data/PS5-Cooling-Center/`. La app no envía datos a Internet y solo se conecta con programas de la propia consola (cargador de payloads, ShadowMountPlus) y con los navegadores de la red doméstica que abren la interfaz. Única excepción, en el navegador y no en la app: al abrirse, la página «Créditos» carga una vez la pequeña imagen de icono de github.com para comprobar si el navegador tiene Internet; solo entonces se pueden pulsar los enlaces a GitHub. Las imágenes de perfil de los desarrolladores están integradas en la app y no se cargan de Internet.
- **Proyectos de terceros.** Están sujetos a sus propias licencias; ver [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) y los créditos más abajo.

## Colaborar y notificar errores

Los errores y las sugerencias son bienvenidos como [issue](../../issues). Indica el modelo de consola, el firmware, la versión del programa y, si lo tienes, el registro exportado (página «Registro»). Contribuciones: [CONTRIBUTING.md](CONTRIBUTING.md). No informes de vulnerabilidades públicamente, sino como se describe en [SECURITY.md](SECURITY.md).

## Créditos y agradecimientos

Este proyecto se apoya en los hombros de la comunidad homebrew de PS5. Sin el trabajo de estas desarrolladoras y desarrolladores no existirían ni la plataforma ni parte de las funciones. **¡Muchas gracias!**

**Plataforma y herramientas**

- **John Törnblom y todos los colaboradores de [ps5-payload-dev](https://github.com/ps5-payload-dev)**: el [PS5-Payload-SDK](https://github.com/ps5-payload-dev/sdk), con el que se compila cada ELF de este proyecto; el cargador de payloads [elfldr](https://github.com/ps5-payload-dev/elfldr) en el puerto 9021; [klogsrv](https://github.com/ps5-payload-dev/klogsrv) y [ftpsrv](https://github.com/ps5-payload-dev/ftpsrv) para el desarrollo; y [websrv](https://github.com/ps5-payload-dev/websrv), cuya forma de iniciar juegos sirvió aquí de modelo.
- **Los desarrolladores de kstuff**: [sleirsgoevy](https://github.com/sleirsgoevy) (autor principal), [EchoStretch](https://github.com/EchoStretch) y [drakmor](https://github.com/drakmor) (los mayores colaboradores de [kstuff-lite](https://github.com/EchoStretch/kstuff-lite)); repositorios, entre otros, en [ps5-payload-dev](https://github.com/ps5-payload-dev/kstuff) y [EchoStretch](https://github.com/EchoStretch/kstuff). Sin kstuff no habría acceso al controlador del ventilador.

**Código de otros proyectos (portado o integrado)**

- **[RenanGBarreto](https://github.com/RenanGBarreto), [rdmrocha](https://github.com/rdmrocha) y los colaboradores de [MkPFS](https://github.com/PSBrew/MkPFS)** ([PSBrew](https://github.com/PSBrew), GPL-3.0): creación de imágenes exFAT, PFS y PFSC. La conversión en la consola es un port a C.
- **[SvenGDK](https://github.com/SvenGDK), [UFS2Tool](https://github.com/SvenGDK/UFS2Tool)** (BSD-2-Clause): modelo para el generador UFS2/ffpkg.
- **[itsPLK](https://github.com/itsPLK), [ps5-pkg-manager](https://github.com/itsPLK/ps5-pkg-manager)** (GPL-3.0): estructura de los paquetes de PS4/PS5, el formato de partes (`PS5MPKG1`), el plan de carpetas de la búsqueda de paquetes y el proceso de instalación (cómo se llama a la biblioteca del sistema de la consola y de dónde lee el paquete). Las funciones de paquetes de esta app son código propio basado en ese modelo; nada de él está integrado.
- **[phantomptr](https://github.com/phantomptr), [ps5upload](https://github.com/phantomptr/ps5upload)** (GPL-3.0): estructura de la función de perfil y formato de la imagen de perfil.
- **[Eric Biggers](https://github.com/ebiggers), [libdeflate](https://github.com/ebiggers/libdeflate)** (MIT): compresión rápida para las imágenes.
- **[Dave Gamble](https://github.com/DaveGamble) y colaboradores, [cJSON](https://github.com/DaveGamble/cJSON)** (MIT): JSON.

**Colaboración, conocimientos y modelos**

- **[drakmor](https://github.com/drakmor)**: [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus) (montar, mover y extraer juegos; la página Juegos se basa en él; su README indica la estructura recomendada para las imágenes `.ffpkg`: bloques de 64 KiB) y [ps5-hwinfo](https://github.com/drakmor/ps5-hwinfo) (orden de los raíles de alimentación, frecuencias).
- **[kerrdec97](https://github.com/kerrdec97), [exFAT Image Builder](https://github.com/kerrdec97/ps5-exfat-builder)**: mostró con qué parámetros se crea un `.ffpkg` para ShadowMountPlus (bloques y fragmentos de 64 KiB, sin espacio reservado, densidad de inodos 262144, tamaño de sector 512) y que un tamaño de sector de 4096 produce allí (en Windows) imágenes defectuosas. Solo conocimientos, nada de código.
- **[itsPLK](https://github.com/itsPLK)**: [ps5-unified-autoloader](https://github.com/itsPLK/ps5-unified-autoloader) (cerrar el navegador de la consola) y [ps5-payload-manager](https://github.com/itsPLK/ps5-payload-manager), modelo de la gestión de payloads y de la regla sobre qué procesos se pueden finalizar.
- **[slopmaster33](https://github.com/slopmaster33), [webhb](https://github.com/slopmaster33/webhb)** (GPL-3.0): el codificador QR que muestra la dirección de la interfaz web como código (`src/qr.c`, adoptado y comprobado con un lector).
- **[StonedModder](https://github.com/StonedModder), [ps-game-state-lib](https://github.com/StonedModder/ps-game-state-lib)** (MIT): patrones del registro del kernel con los que se puede reconocer el juego en ejecución.
- **El proyecto [etaHEN](https://github.com/etaHEN/etaHEN)** y **[onionHEN](https://github.com/aydencharles/onionHEN)** (aydencharles): código fuente y documentación sobre llamadas al sistema, mediciones y la consulta de la frecuencia de imagen.
- **[Soniciso](https://git.etawen.dev/soniciso), [Elf Arsenal](https://git.etawen.dev/soniciso/elf-arsenal)** (sucesor de [Sonic Loader](https://git.etawen.dev/soniciso/sonicloader)): forma de las llamadas para la instalación del icono y modelo para muchas funciones (cerrar juego, registro del kernel en directo, tiempo de juego, datos guardados, gestor de archivos, eliminar juegos); el código se ha reescrito en cada caso.
- **BestPig y [BackPork](https://github.com/BestPig/BackPork)**: el principio de las bibliotecas sustitutas (fakelibs), que la página Juegos detecta y muestra.
- **Juma Sayeh (desarrollador) y Osama Abualia (pruebas), PS5 Game Compressor**: modelo para la comprobación del SDK respecto al firmware 13.60 al compilar y para cinco ideas al copiar y convertir: releer por completo y verificar las copias de seguridad tras escribirlas, impedir el modo de reposo durante operaciones largas, leer y escribir a la vez solo entre dos unidades distintas, rellenar explícitamente con ceros los huecos de las imágenes y dejar sin comprimir los bloques que ahorran menos del 5 %. El código fuente de Game Compressor no tiene licencia, por eso no se ha adoptado nada de él: todo está reescrito.
- **Kernel de Linux, controlador `hid-playstation`**: documentación del byte de estado del DualSense para el nivel de batería.
- **Xbox 360 DashLaunch**: modelo para la idea de una regulación de temperatura tranquila y pausada.

**Un agradecimiento muy especial a [Gezine](https://github.com/Gezine)**: su trabajo es fundamental para la comunidad homebrew de PS5; sin él, en muchas consolas no funcionaría ningún homebrew, y por tanto tampoco esta app.

**Gracias a la comunidad.** Su código no está en esta app, pero sin el trabajo que comparten la escena no sería como es: [owendswang](https://github.com/owendswang) ([ps5-web-file-manager](https://github.com/owendswang/ps5-web-file-manager), [ps5-fan-control](https://github.com/owendswang/ps5-fan-control)), [LightningMods](https://github.com/LightningMods) (etaHEN, [Itemzflow](https://github.com/LightningMods/Itemzflow)), [Andy Nguyen / TheFloW](https://github.com/TheOfficialFloW) ([PPPwn](https://github.com/TheOfficialFloW/PPPwn)), [Specter](https://github.com/Cryptogenic) ([PS5-IPV6-Kernel-Exploit](https://github.com/Cryptogenic/PS5-IPV6-Kernel-Exploit)), [ChendoChap](https://github.com/ChendoChap) ([pOOBs4](https://github.com/ChendoChap/pOOBs4)), [idlesauce](https://github.com/idlesauce) ([umtx2](https://github.com/idlesauce/umtx2)), [flatz](https://github.com/flatz) ([pkg_pfs_tool](https://github.com/flatz/pkg_pfs_tool)), [Al Azif](https://github.com/Al-Azif) ([ps4-exploit-host](https://github.com/Al-Azif/ps4-exploit-host)), [zecoxao](https://github.com/zecoxao) – y a todos los demás que contribuyen con código, pruebas, guías o respuestas. En la app aparecen todos con imagen en la página «Créditos».

Las 30 imágenes de perfil las ha generado el propio autor. Si echas en falta tu nombre o crees que la descripción no es correcta, avísanos y con gusto completaremos la lista.

## Licencia

Copyright © 2026 strongt1me

Este programa es software libre: se puede redistribuir y modificar según los términos de la **GNU General Public License**, versión 3 o (a tu elección) cualquier versión posterior; ver [LICENSE](LICENSE). Se proporciona sin ninguna garantía.

Hasta la 1.45.1 incluida, el proyecto estaba bajo la licencia MIT. Desde la 1.46.0 rige GPL-3.0-or-later: la conversión de juegos es un port de [MkPFS](https://github.com/PSBrew/MkPFS) (GPL-3.0), y el PS5-Payload-SDK, con el que se compila cada ELF, está a su vez bajo GPLv3+. Qué partes de terceros incluye y bajo qué licencia se indica en [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

---

## Idiomas

Este README también está disponible en [Deutsch](README.md), [English](README.en.md), [Italiano](README.it.md), [Français](README.fr.md) y [Русский](README.ru.md). El original es el README en alemán; las traducciones se basan en él. El registro de decisiones, los documentos para desarrolladores y la descripción de la API están en alemán; el manual y las FAQ se incluyen en cada publicación en los seis idiomas, en PDF y HTML, y están integrados en la app. Las traducciones que falten o suenen poco naturales se pueden notificar como issue («Translation»); cómo completarlas se explica en [docs/ENTWICKLUNG.md](docs/ENTWICKLUNG.md#übersetzungen).
