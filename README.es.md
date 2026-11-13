# STAR WARS RACER - RECOMP

Recompilación estática de **Star Wars Racer Arcade** (Sega Hikaru, 2000) y **Star Wars Episode I Racer**
(Dreamcast, PAL) para Windows y Linux / Steam Deck.
Hecho por **Pocket_lucho** con Claude. [Read in English](README.md)

El código máquina SH-4 de cada juego se traduce por adelantado a C (una función por bloque básico, comprobada
por hash al ejecutar) y se compila a código nativo; lo que no esté traducido lo ejecuta un intérprete exacto
al ciclo, así que el juego entero funciona siempre.

* **Recreativa**: todo lo demás es una implementación propia de la placa Hikaru escrita para este proyecto:
  los dos SH-4, la placa de E/S MIE (Z80 + JVS), las dos placas de sonido AICA (ARM7), el procesador de
  comandos y la GPU sobre Vulkan, las capas 2D y las EEPROM.
* **Dreamcast**: el mismo núcleo SH-4 conectado a [Flycast](https://github.com/flyinghead/flycast) como
  ejecutor de CPU; Flycast pone el hardware de Dreamcast (PowerVR2, GD-ROM, AICA, mandos, BIOS HLE).

Si quieres apoyarme: https://buymeacoffee.com/pocket_lucho

## Qué necesitas

* Tu propio romset de MAME de la recreativa: `podrace.zip` o `swracer.zip` (con la BIOS de Hikaru
  `epr-23400a.ic94` dentro), y/o
* tu propia imagen del juego de Dreamcast, versión PAL T-23001D-50 (`.chd` o `.gdi`).
* Windows 10/11 de 64 bits o Linux x86_64, con un driver Vulkan 1.1.

**Ni este repositorio ni los paquetes publicados incluyen código, gráficos, sonido o BIOS de los juegos.**
El código recompilado se genera en tu PC a partir de tus propios archivos.

## Primeros pasos (Windows)

1. Descomprime el paquete.
2. Copia `podrace.zip` (o `swracer.zip`) en `roms\` y/o la imagen de Dreamcast en `dreamcast\`.
3. Ejecuta **`CONSTRUIR_JUEGO.bat`** una vez. Este programa:
   * descarga el compilador gratuito [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) (180 MB, sólo la
     primera vez, comprobado con SHA-256),
   * arranca el juego sin ventana con el intérprete para leer su programa de la memoria y lo traduce a C
     (unos 27.000 bloques la recreativa y 38.000 el de Dreamcast),
   * lo compila en `swr_game.dll` / `swrdc_game.dll` (unos minutos),
   * borra los fuentes generados.
4. Ejecuta **`SWRacer-Launcher.exe`** y elige juego (o arranca directamente `StarWarsRacer-Recomp.exe` /
   `SWRacer-DC-Recomp.exe`). F10 abre las opciones de la recreativa y Tab las de Dreamcast.

Sin el paso 3 los juegos funcionan igualmente, con el intérprete (más lento; lo avisa el título de la ventana).

## Primeros pasos (Linux / Steam Deck)

Descomprime el tarball, copia la ROM como arriba, ejecuta una vez `./build_game.sh` (necesita `cc` y `make`:
`sudo apt install build-essential`) y después `./swracer`. Necesita SDL2 (`libsdl2-2.0-0`) y un driver Vulkan.
El motor de Dreamcast para Linux se compila desde el código (ver `dreamcast/setup.sh`).

## Funciones

* Rendimiento nativo (código recompilado, sin JIT) con respaldo de un intérprete exacto.
* Recreativa: resolución interna 1x-8x, MSAA, **panorámico 16:9** con vista más ancha real, pantalla
  completa, pausa, capturas, teclado y mando XInput/SDL con palancas analógicas, menú de servicio/test con
  ajustes que se guardan.
* **Selector de región** (Japón, EE. UU., Exportación/Europa, Australia, Corea).
* **Trucos**: desbloqueo de los pilotos ocultos Ben Quadinaros y Gasgano (el mueble los desbloquea tras 50
  finales en Expert / 700 partidas). Dreamcast: el menú de trucos de Flycast.
* Menús en **español e inglés** (automático o elegido en las opciones).
* Desenfoque de movimiento del turbo (el mezclador de framebuffer de Hikaru), destellos, polvo, niebla,
  iluminación por vértice.
* Dreamcast: panorámico mediante un gancho en la cámara (más campo de visión, no un estirado) y todo lo que
  ofrece Flycast.

## Compilar desde el código

Ver [README.md](README.md#building-from-source) y [docs/TECHNICAL.md](docs/TECHNICAL.md).
Historial de desarrollo: [docs/STATUS.es.md](docs/STATUS.es.md).

## Aspectos legales

* Licencia **GNU GPL v2** (ver `LICENSE`); el motor de Dreamcast es un parche para Flycast (GPL v2).
  `third_party/miniz` (MIT) y `third_party/volk` (MIT) conservan sus licencias.
* **No contiene material con copyright de Sega ni de Lucasfilm**: ni ROM, ni BIOS, ni imagen de disco, ni
  gráficos, sonidos, música o código del juego traducido. Gráficos, sonido y música se leen siempre en tiempo
  de ejecución de la ROM del usuario; el código recompilado (`swr_game.dll`, `swrdc_game.dll`,
  `libswr_game.so`, `libswrdc_game.so`) se genera en el PC del usuario y **no debe redistribuirse**.
* `builder/seeds_*.txt` sólo contienen direcciones de puntos de entrada de código.
* Los iconos son dibujos originales; no se usan logotipos ni personajes oficiales.
* "Star Wars", "Star Wars Racer Arcade", "Episode I Racer", "SEGA", "Hikaru" y "Dreamcast" son marcas de sus
  propietarios y sólo se usan para indicar con qué es compatible este software. El proyecto no está afiliado
  ni respaldado por ellos.
* Esto no es asesoramiento legal.
