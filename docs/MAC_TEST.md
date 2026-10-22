# Test locale su macOS

Questa procedura serve a compilare il port Neo Geo MVS e lanciarlo in MAME.

## 1. Clona il repository

```bash
git clone https://github.com/commodoreforce/ghostsngoal-mvs.git
cd ghostsngoal-mvs
```

## 2. Installa la toolchain

Rendi eseguibili gli script se necessario:

```bash
chmod +x scripts/*.sh
```

Poi:

```bash
./scripts/bootstrap-ngdevkit-macos.sh
```

Lo script installa/tenta di installare Homebrew packages per ngdevkit, GNU make, MAME e gli strumenti richiesti.

La documentazione ufficiale ngdevkit su macOS richiede Xcode Command Line Tools, Homebrew, Python di Homebrew e `gmake`.

## 3. Prepara l'ambiente ngdevkit

```bash
./scripts/prepare-ngdevkit-project.sh
```

Questo clona `dciabrin/ngdevkit-examples` dentro `.ngdevkit-examples`, esegue `autoreconf`, `./configure` e crea un progetto `ghostsngoal-mvs` usando il template ufficiale.

## 4. Compila

```bash
cd .ngdevkit-examples/ghostsngoal-mvs
gmake
```

Se la build è corretta vengono prodotti P-ROM/S-ROM/C-ROM/M-ROM necessari al test.

## 5. Avvia in MAME MVS

Dalla stessa cartella:

```bash
gmake mame-mvs
```

Se MAME non parte dal target ma è installato, controlla:

```bash
which mame
mame -version
```

## 6. Alternativa: GnGeo

```bash
gmake gngeo-mvs
```

Su macOS recenti, System Integrity Protection può impedire l'avvio di GnGeo direttamente dal makefile. La documentazione ngdevkit suggerisce in quel caso:

```bash
eval $(gmake -n gngeo-mvs)
```

## Controlli attuali

- joystick/frecce: movimento
- A: salto
- B: calcio
- coin/start: gestiti dal BIOS MVS

## Cosa aspettarsi in questa milestone

La milestone hardware corrente valida:

- boot Neo Geo
- MVS attract/title lifecycle
- coin/start
- loop 60 Hz/VBlank
- FIX-layer title/HUD
- collegamento del core C

Gli sprite e i background reali non sono ancora la parte da giudicare in questa build; verranno convertiti in C-ROM nelle milestone grafiche successive.
