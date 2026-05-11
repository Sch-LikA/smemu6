# Émulateur Smaky 6 — Guide d'utilisation

Ce guide explique comment compiler, lancer et utiliser l'**émulateur Smaky 6**.
Pour la documentation sur l'ordinateur Smaky 6 lui-même (commandes, OS, matériel),
voir `docs/SMAKY6_USER_GUIDE_FR.md`.

---

## Table des matières

1. [Prérequis](#1-prérequis)
2. [Compilation](#2-compilation)
3. [Fichiers ROM](#3-fichiers-rom)
4. [Démarrage rapide](#4-démarrage-rapide)
5. [Référence des options](#5-référence-des-options)
6. [Mapping du clavier](#6-mapping-du-clavier)
7. [Options d'affichage](#7-options-daffichage)
8. [Images disquette](#8-images-disquette)
9. [Automatisation et scripts](#9-automatisation-et-scripts)
10. [Débogage et traces](#10-débogage-et-traces)
11. [Dumps mémoire](#11-dumps-mémoire)
12. [Résolution de problèmes](#12-résolution-de-problèmes)

---

## 1. Prérequis

| Dépendance | Version minimum | Utilité |
|------------|----------------|---------|
| CMake | 3.16 | Système de compilation |
| SDL2 | 2.0 | Fenêtre, clavier, affichage |
| Compilateur C | C11 (gcc / clang) | Compilation |
| git | quelconque | FetchContent pour le cœur Z80 |

Optionnel (uniquement pour générer les PDF) :

| Dépendance | Utilité |
|------------|---------|
| pandoc | Conversion Markdown → PDF |
| lualatex | Moteur PDF utilisé par pandoc |

---

## 2. Compilation

```bash
git clone https://github.com/Sch-LikA/smemu6
cd smemu6
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Pour une version release :

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Le binaire résultant est `build/smemu6`.

---

## 3. Fichiers ROM

Placez les fichiers ROM dans `roms/` (relatif à la racine du dépôt).
Le système de compilation les copie automatiquement dans `build/roms/`.

| Fichier | Taille | Requis | Description |
|---------|--------|--------|-------------|
| `roms/samos_sys17.rom` | 2 Ko | **Oui** | ROM Phantom (SYS17 TMS2716) |
| `roms/chargen.rom` | 2 Ko | Optionnel | PROM de génération de caractères |

> **Note :** Si `chargen.rom` est absent, l'émulateur utilise une table de
> caractères synthétique intégrée. Le texte sera lisible mais peut légèrement
> différer du matériel original.
>
> Si `samos_sys17.rom` est absent, l'émulateur affiche un avertissement et le
> CPU exécute de la mémoire indéfinie — rien d'utile ne se produira.

---

## 4. Démarrage rapide

**Démarrer dans le moniteur machine (sans disquette) :**

```bash
cd build
./smemu6
```

**Démarrer depuis une image disquette (démarrage automatique) :**

```bash
./smemu6 -floppy ../floppies/sys.img
```

**Démarrer avec deux lecteurs disquette :**

```bash
./smemu6 -floppy ../floppies/sys.img -floppy2 ../floppies/data.img
```

**Démarrer depuis le Winchester (DX0) avec une disquette accessible en DX1 :**

```bash
./smemu6 -harddisk ../harddisks/SM6WIN0.DSK -floppy2 ../floppies/data.img
```

> Sur les Smaky 6 équipés d'un Winchester, le disque dur **est** DX0.
> Le lecteur de disquettes (s'il est installé) occupe l'emplacement DX1.
> Combiner `-floppy` (DX0) avec `-harddisk` ne correspond pas au matériel réel ;
> seul `-harddisk` + `-floppy2` correspond à la configuration matérielle réelle.



## 5. Référence des options

### Lecteurs disquette

| Option | Description |
|--------|-------------|
| `-floppy <img>` | Monter une image disquette sur le lecteur **DX0:** |
| `-floppy2 <img>` | Monter une image disquette sur le lecteur **DX1:** |

### Disque dur (Winchester)

| Option | Description |
|--------|-------------|
| `-harddisk <img>` | Monter une image Winchester sur le lecteur dur 0 (SM6WIN0) |
| `-harddisk2 <img>` | Monter une image Winchester sur le lecteur dur 1 (SM6WIN1) |

**Note matériel :** Sur un Smaky 6 équipé d'un Winchester, le disque dur occupe
l'emplacement DX0 et la disquette (si présente) occupe DX1. Les combinaisons
valides sont :

| Configuration | Options |
|---------------|---------|
| Disquette seule (DX0) | `-floppy <img>` |
| Deux disquettes (DX0 + DX1) | `-floppy <img> -floppy2 <img>` |
| Winchester (DX0) + disquette (DX1) | `-harddisk <img> -floppy2 <img>` |
| Winchester seul | `-harddisk <img>` |

### Contrôle du démarrage

| Option | Description |
|--------|-------------|
| `-break-to-monitor` | Injecte SHIFT+BREAK pour entrer dans le moniteur SYSMON au démarrage |

### Injection de chaîne

| Option | Description |
|--------|-------------|
| `-inject-str <s>` | Injecte une chaîne dès que l'invite `>` de SAMOS est détectée. Utiliser `\n` pour Entrée. |
| `-inject-delay <f>` | Accepté pour compatibilité ascendante ; n'a plus aucun effet. |
| `-inject-via-fifo` | Route `-inject-str` par le FIFO clavier matériel au lieu du chemin rapide |

**Exemple — exécuter `LIST` automatiquement :**

```bash
./smemu6 -floppy ../floppies/sys.img -inject-str "LIST\n"
```

### Affichage

| Option | Description |
|--------|-------------|
| `-vmode <m>` | Forcer le mode vidéo : `alpha` (texte seul), `graphic` (graphique seul), `super` (texte + graphique) |
| `-gfxbits <b>` | Ordre des bits du bitmap : `lsb` (défaut) ou `msb` |
| `-scale <n>` | Zoom entier de la fenêtre 1–8 (défaut `1` → 512 × 508 pixels) |
| `-scanlines` | Superpose un effet de lignes de balayage CRT (assombrit une ligne sur deux) |
| `-phosphor <c>` | Couleur du phosphore : `green` (défaut, P31 `#00E700`) ou `white` (`#E8E8E8`) |
| `-phosphor-decay <f>` | Facteur de décroissance de la persistance par trame `0.0`–0.99 (défaut `0.70` ≈ P31) ; simule la rémanence du phosphore entre les trames d'extinction d'écran |
| `-no-phosphor` | Désactive la persistance du phosphore (décroissance instantanée) |
| `-no-display-off` | Ignore les écritures d'extinction d'écran sur le port `0x00` ; l'écran reste visible en permanence |

### Timing et timeouts

| Option | Description |
|--------|-------------|
| `-timeout <s>` | Timeout global en secondes (`0` = désactivé ; défaut 45 s quand `-trace` est actif) |

### Son

| Option | Défaut | Description |
|--------|--------|-------------|
| `-no-beeper` | — | Coupe le buzzer 1-bit de la machine (le buzzer est **actif** par défaut) |
| `-drive-sound` | — | Active les sons synthétisés du lecteur de disquettes : ronronnement moteur, clics de pas de tête et tics de trou de secteur (désactivé par défaut) |

---

## 6. Mapping du clavier

Le Smaky 6 possède un clavier QWERTZ suisse avec des touches de fonction
spéciales. L'émulateur les associe aux touches PC standard comme suit.

### Touches spéciales

| Touche PC | Fonction Smaky 6 |
|-----------|------------------|
| `Ctrl gauche` | Touche de fonction **CURSOR** |
| `Alt gauche` | Touche de fonction **COPY** |
| `Windows gauche` / `Super` | Touche de fonction **KILL** |
| `AltGr` (Alt droit) | Touche de fonction **PROGRA** |
| `F10` | Touche de fonction **SHOW** |
| `Menu` / `App` | Touche de fonction **SEARCH** |
| `Ctrl droit` | Touche de fonction **CHANGE** |
| `F8` | **MACRO** — rejoue une séquence de touches enregistrée (code `«` 0x1E) |
| `F9` | **DEFINE** — enregistre une séquence de touches (code `»` 0x1F) |
| `F11` ou `Pause` | **BREAK** (touche en haut à droite) — déclenche une NMI → entre dans le moniteur SYSMON |
| `Shift+F11` ou `Shift+Pause` | **SHIFT+BREAK** — réinitialisation matérielle (redémarre depuis DX0:) |
| `Escape` | **ESC / UNDO** (touche en haut à gauche) — sur une ligne CLI **vide** : rappelle la commande précédente (la re-saisit pour permettre l'édition) ; sur une ligne **non vide** : annule/vide la ligne |
| `Tab` | Insère `DX1:` dans la ligne de commande CLI (`0x09`) |
| `Backspace` | Smaky BS (`0x08`) |
| `Delete` | Smaky DEL (`0x7F`) |

### Caractères accentués franco-suisses

Les 15 caractères accentués franco-suisses sont intégralement supportés.
Saisissez-les avec les méthodes habituelles de votre OS (touches mortes,
touche de composition, etc.) ; l'émulateur les reçoit en UTF-8 et les
traduit vers les codes chargen Smaky 6 correspondants :

| Caractère | Code Smaky | | Caractère | Code Smaky |
|-----------|------------|--|-----------|------------|
| ü / Ü | `0x0F` | | ô / Ô | `0x18` |
| à / À | `0x10` | | ù / Ù | `0x19` |
| â / Â | `0x11` | | û / Û | `0x1A` |
| é / É | `0x12` | | ä / Ä | `0x1B` |
| è / È | `0x13` | | ö / Ö | `0x1C` |
| ë / Ë | `0x14` | | ç / Ç | `0x1D` |
| ê / Ê | `0x15` | | | |
| ï / Ï | `0x16` | | | |
| î / Î | `0x17` | | | |

### Barre de touches de fonction

La bande inférieure de la fenêtre de l'émulateur contient 7 boutons
cliquables rouge foncé, un par touche de fonction Smaky 6 (CURSOR, COPY, KILL,
PROGRA, SHOW, SEARCH, CHANGE).  Cliquez et maintenez pour activer ;
relâcher désactive.  Ces touches sont **exclusivement des modificateurs** —
aucun caractère n'est écrit à l'écran, conformément au comportement du
matériel réel.

### Touches standard

Tous les caractères ASCII imprimables sont transmis directement.
Le Smaky 6 utilise un clavier **QWERTZ** (allemand suisse) — si votre clavier
PC est QWERTY ou AZERTY, certaines touches de ponctuation peuvent différer.

---

## 7. Options d'affichage

Le Smaky 6 possède deux couches d'affichage indépendantes :

- **Couche Alpha** — affichage texte 64 × 20 caractères
- **Couche Graphique** — bitmap monochrome (>30 000 pixels)

L'option `-vmode` contrôle les couches affichées :

| Valeur | Ce qui est affiché |
|--------|-------------------|
| `alpha` | Couche texte seule |
| `graphic` | Couche graphique seule |
| `super` | Les deux couches superposées (fonctionnement normal) |

L'option `-scale` définit le niveau de zoom entier. Le défaut est 1 (512 × 508) ;
le zoom 2 donne une fenêtre de 1024 × 1016 pixels, confortable sur la plupart
des moniteurs. Ajoutez `-scanlines` pour un effet de lignes de balayage CRT.

La persistance du phosphore (affichage vert P31) est simulée par défaut avec
un facteur de décroissance de 0.70 par trame.  Utilisez
`-phosphor-decay <0.0..0.99>` pour ajuster l'intensité de la persistance,
ou `-no-phosphor` pour la désactiver complètement.

---

## 8. Images disquette

### Format supporté

L'émulateur lit les **images de secteurs bruts Micropolis** en deux tailles :

| Taille de l'image | Géométrie                  | Notes                        |
|-------------------|----------------------------|------------------------------|
| 163 840 octets    | 40 pistes × 16 × 256 o     | Simple face standard 5,25"  |
| 315 392 octets    | 77 pistes × 16 × 256 o     | Étendu (lecteurs 77 pistes)  |

Le nombre de pistes est auto-détecté à partir de la taille de l'image.

Placez les fichiers image n'importe où et passez le chemin à `-floppy` / `-floppy2`.
Le répertoire `floppies/` du dépôt est l'emplacement conventionnel.

### Extraire des fichiers d'une disquette

Utilisez les outils du projet compagnon `../smaky6-tools/` :

```bash
python3 ../smaky6-tools/smaky6_samos.py disk ../floppies/sys.dsk list
python3 ../smaky6-tools/smaky6_samos.py disk ../floppies/sys.dsk extract-all private/extracted/
```

### Sous-répertoires (fichiers `.DR`)

SAMOS supporte les répertoires imbriqués stockés comme fichiers avec l'extension `.DR`.
La commande CLI `CDIR NOM.DR` entre dans un sous-répertoire ; `CDIR` seul
affiche le répertoire courant ; `CLEAR` revient à la racine.

Dans l'émulateur, les sous-répertoires fonctionnent de manière transparente —
l'image disquette contient tous les secteurs et aucun traitement spécial n'est
nécessaire.  Lors du listage d'une image avec `smaky6_samos.py`, les entrées
enfants sont affichées avec un préfixe `> ` :

```
17  U          DR  Directory   509  609 …
    > EDISK    SM  SMILE prog  512  525 …
    > TDISK    SM  SMILE prog  525  531 …
```

---

## 9. Son

L'émulateur reproduit deux catégories de son via la sortie audio SDL2.

### Buzzer

Le Smaky 6 dispose d'un buzzer 1-bit piloté par le port `0x03`. Les
logiciels produisent des tonalités en basculant ce port dans une boucle
serrée ; chaque écriture inverse l'état du haut-parleur. L'émulateur modélise
cela avec une onde carrée unipolaire à 44 100 Hz.

Le buzzer est **actif par défaut**. Passez `-no-beeper` pour le couper.

### Sons du lecteur de disquettes

L'émulateur peut synthétiser l'environnement sonore du lecteur
Micropolis 5.25" à secteurs fixes :

- **Ronronnement moteur** — bruit filtré passe-bande (300–1 500 Hz) qui
  monte en puissance au démarrage de la broche et décroît ~0,8 s après la
  dernière activité de positionnement.
- **Clic de pas de tête** — claquement sec (bruit passe-bande 600–3 000 Hz
  avec décroissance exponentielle) produit à chaque pas de piste lors d'un
  positionnement.
- **Tic de trou de secteur** — bref bruit produit à chaque trou de secteur
  au passage devant le capteur optique (16 tics par tour à ~300 tr/min).

Les sons du lecteur sont **désactivés par défaut** (sons synthétisés en
cours de développement ; de vrais échantillons seront ajoutés plus tard).
Activez avec `-drive-sound`.

**Exemple — démarrer avec buzzer et sons du lecteur :**

```bash
./smemu6 -floppy sys.img -drive-sound
```

**Exemple — tout couper :**

```bash
./smemu6 -floppy sys.img -no-beeper
```

---

## 10. Automatisation et scripts

L'émulateur peut être piloté de manière non interactive en combinant
`-inject-str` et `-timeout`.

**Exécuter une commande et capturer la sortie écran :**

```bash
./smemu6 \
    -floppy ../floppies/sys.img \
    -inject-str "LIST\n" \
    -timeout 20 \
    -scrdump 2>ecran.txt
```

**Exécuter avec traçage pour le débogage :**

```bash
./smemu6 \
    -floppy ../floppies/sys.img \
    -trace \
    -timeout 15 2>trace.log
```

**Dumper la RAM en fin d'exécution :**

```bash
./smemu6 \
    -floppy ../floppies/sys.img \
    -inject-str "BASIC\n" \
    -timeout 30 \
    -dump-ram basic_init.bin
```

---

## 11. Débogage et traces

Ces options sont destinées au développement de l'émulateur et à la
rétro-ingénierie. Elles produisent leur sortie sur **stderr**.

| Option | Ce qui est tracé |
|--------|-----------------|
| `-trace` | Compteur de programme Z80 aux jalons clés du démarrage |
| `-traceflow` | Flux de contrôle en RAM basse après la remise en main par l'OS |
| `-tracekbd` | Chaque lecture du port statut clavier et écriture CLA |
| `-tracesnd` | Chaque écriture sur le port `0x03` (buzzer) |
| `-trace08` | Tout le trafic `IN`/`OUT` sur le port `0x08` |
| `-trace11` | Toutes les lectures du port `0x11` |
| `-trace19` | Toutes les écritures sur le port `0x19` (contrôle disquette) |
| `-tracecd` | Toutes les lectures du port `0xCD` (registre DMA/statut Winchester) |
| `-trace-win` | Chaque commande du contrôleur Winchester (RESTORE, SEEK, READ, WRITE) avec CHS et LBA |
| `-tracefdc` | Événements ciblés du flux ID/checksum disquette |
| `-scrdump` | Lignes d'écran modifiées affichées sur stderr à chaque image |

**Astuce :** Combinez avec la redirection shell pour capturer les traces
sans les mélanger à la sortie de l'émulateur :

```bash
./smemu6 -floppy sys.img -tracekbd 2>clavier.log
```

---

## 12. Dumps mémoire

**Dump automatique à la fin :**

```bash
./smemu6 -floppy sys.img -timeout 10 -dump-ram snapshot.bin
```

**Dump interactif pendant une session en cours :**

Appuyez sur `Ctrl+D` dans le terminal, ou envoyez `SIGUSR1` au processus :

```bash
kill -SIGUSR1 $(pgrep smemu6)
```

Ceci écrit un fichier nommé `smaky6_ram_NNNN_pcXXXX.bin` dans le répertoire
courant, où `NNNN` est un numéro de séquence et `XXXX` la valeur du PC Z80
au moment du dump.

Le dump de 64 Ko peut être inspecté avec n'importe quel éditeur hexadécimal
ou désassembleur :

```bash
xxd snapshot.bin | less
objdump -b binary -m z80 -D snapshot.bin | less
```

---

## 13. Résolution de problèmes

**Écran noir / pas de vidéo après le démarrage**

Essayez de forcer un mode vidéo :

```bash
./smemu6 -floppy sys.img -vmode alpha
```

**Les graphiques apparaissent inversés ou brouillés**

Le plan graphique utilise un encodage nibble-entrelacé avec MSB à gauche par défaut
(vérifié sur le matériel réel). Pour forcer l'ordre inverse :

```bash
./smemu6 -floppy sys.img -gfxbits lsb
```

**L'émulateur quitte immédiatement avec l'erreur 043**

L'image disquette est peut-être illisible ou dans le mauvais format.
Vérifiez la taille du fichier : une image valide fait 163 840 octets (40 pistes) ou 315 392 octets (77 pistes).

```bash
wc -c monimage.dsk
```

**Les entrées clavier n'atteignent pas l'émulateur**

Assurez-vous que la fenêtre SDL a le focus (cliquez dessus). L'émulateur
ne traite les événements clavier que lorsque sa fenêtre est au premier plan.

**`-inject-str` ne se déclenche pas**

Vérifiez que l'invite `>` de SAMOS est bien visible au moment attendu.
L'émulateur n'injecte que lorsque `machine_cli_prompt_visible()` détecte
l'invite dans la mémoire vidéo.

**Comment générer / mettre à jour les PDF des manuels**

```bash
./tools/generate_pdfs.sh
```
