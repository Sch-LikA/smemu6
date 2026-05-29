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
./smemu6 -floppy <disque.dsk>
```

**Démarrer avec deux lecteurs disquette :**

```bash
./smemu6 -floppy <disque.dsk> -floppy2 <disque2.dsk>
```

**Démarrer depuis le Winchester (DX0) avec une disquette accessible en DX1 :**

```bash
./smemu6 -harddisk ../harddisks/SM6WIN0.DSK -floppy2 <disque2.dsk>
```

**Démarrer depuis le Winchester (DX0) avec une disquette de développement DX1 générée depuis un répertoire hôte :**

```bash
./smemu6 -harddisk ../harddisks/SM6WIN0.DSK -floppy2-hostdir floppies/DX1
```

**Démarrer depuis une exportation en répertoire hôte préservant les métadonnées sur DX0 :**

```bash
python3 ../tools/extract_samos_image.py ../floppies/Sys2-2.dsk ../tmp/Sys2-2-hostdir
./smemu6 -floppy-hostdir ../tmp/Sys2-2-hostdir
```

`../tools/extract_samos_image.py` conserve cette interface historique, mais
agit maintenant comme un wrapper autour du flux unifié
`../smaky6-tools/smaky6_samos.py extract-all ... --metadata --clear`.

> Sur les Smaky 6 équipés d'un Winchester, le disque dur **est** DX0.
> Le lecteur de disquettes (s'il est installé) occupe l'emplacement DX1.
> Combiner `-floppy` (DX0) avec `-harddisk` ne correspond pas au matériel réel ;
> seul `-harddisk` + `-floppy2` correspond à la configuration matérielle réelle.
>
> Les images Winchester sont actuellement montées avec une surcouche
> d'écriture en mémoire. Les modifications invitées sont visibles pendant la
> session en cours, mais le fichier `.DSK` de base reste inchangé et la
> surcouche est perdue au remontage ou à la fermeture de l'émulateur.

## 5. Référence des options

### Lecteurs disquette

| Option | Description |
|--------|-------------|
| `-floppy <img>` | Monter une image disquette sur le lecteur **DX0:** |
| `-floppy-hostdir <dir>` | Construire au démarrage une surcouche DX0 inscriptible en mémoire depuis un répertoire hôte ; builds natifs uniquement |
| `-floppy2 <img>` | Monter une image disquette sur le lecteur **DX1:** |
| `-floppy2-hostdir <dir>` | Construire au démarrage une surcouche DX1 inscriptible en mémoire depuis un répertoire hôte ; builds natifs uniquement |
| `-dump-vfd-manifest <file>` | Exporter en JSON le layout prévu pour la disquette virtuelle issue d'un répertoire hôte ; nécessite `-floppy-hostdir` ou `-floppy2-hostdir` ; utiliser `-` pour stdout |

Limites actuelles des disquettes issues d'un répertoire hôte :

- Builds bureau natifs uniquement ; la version web ne prend pas cette fonction en charge.
- Cette première tranche reconstruit au montage initial et lors d'un rafraîchissement explicite (`Ctrl+R` ou `SIGUSR2`) ; elle ne surveille pas encore automatiquement le répertoire hôte.
- Les écritures invitées ne touchent que la surcouche en mémoire. Elles sont
  visibles pendant la session en cours, puis perdues au rafraîchissement,
  remontage ou à la fermeture de l'émulateur ; le répertoire hôte reste intact.
- Un média DX0 amorçable issu d'un répertoire hôte doit préserver les
  métadonnées et l'ordre SAMOS. `tools/extract_samos_image.py` exporte un
  arbre adapté depuis une image `.dsk` existante comme `Sys2-2.dsk`, avec des
  sidecars contenant `flags`, `load`, `entry`, les dates et `start_sector`.
  Ce script délègue désormais à l'implémentation unifiée
  `smaky6_samos.py extract-all ... --metadata --clear`.
- Les noms de fichiers doivent actuellement suivre `NAME.TT` avec un nom de base de 1 à 8 caractères, `_` autorisé, et un type Smaky à 2 caractères connu.
- L'arborescence hôte peut aussi contenir des répertoires imbriqués `NAME.DR/`. Les entrées à l'intérieur d'un conteneur `.DR` sont encodées avec des numéros de secteur relatifs au début du conteneur, comme sur disque sous SAMOS.
- Les sidecars optionnels `NAME.TT.meta.json` sont pris en charge pour les fichiers ordinaires et `NAME.DR.meta.json` pour les conteneurs de répertoire. Ils peuvent fournir `type` (validation uniquement), `flags`, `load`, `entry`, `date_month`, `date_year` et `start_sector`.
- Dans l'état actuel des builds natifs, des commandes invitées comme
  `LIST DX1:`, `LIST DX1:BOX`, `TYPE DX1:TEXTFILE.BS` et
  `TYPE DX1:BOX:INNER.BS` fonctionnent sur le média virtuel DX1. Une règle CLI
  est maintenant fixée : les noms de répertoire omettent aussi `.DR`, y compris
  pour `CDIR`. Il ne faut donc pas utiliser des sondes comme
  `CDIR DX1:BOX.DR` ; un vrai média disquette rejette aussi cette forme
  (`CDIR DX1:M.DR` sur `Burotic.dsk` renvoie `fichier existant`).
- `CDIR` est une commande de création de répertoire, pas une sonde fiable pour
  changer de répertoire. Sur une image de disquette writable,
  `CDIR DX1:NOUVDIR` crée `NOUVDIR.DR`. La tranche DX1 montée depuis un
  répertoire hôte offre maintenant le même comportement via une surcouche
  inscriptible en mémoire, sans modifier les fichiers hôte.

Sur les builds bureau natifs, `Ctrl+R` rafraîchit sur place toute disquette
virtuelle montée depuis un répertoire hôte. En mode headless ou scripté, on
peut faire la même chose avec `SIGUSR2`.

Pour l'inspection et les tests, `-dump-vfd-manifest <file>` exporte le layout
exact que l'émulateur prévoit de construire pour la disquette issue du
répertoire hôte sélectionné, avec les secteurs, la
taille, les champs de métadonnées issus des sidecars, ainsi que les valeurs de
secteur absolues et encodées pour les entrées imbriquées `.DR`.

Exemple de sidecar :

```json
{
  "type": "SM",
  "flags": 4660,
  "load": "0x5600",
  "entry": "0x5678",
  "date_month": 12,
  "date_year": 82
}
```

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
| `-no-launcher` | Ignore la boîte de dialogue de démarrage et lance directement avec les médias/options fournis |

### Injection de chaîne

| Option | Description |
|--------|-------------|
| `-inject-str <s>` | Injecte une chaîne dès que l'invite `>` de SAMOS est détectée. Utiliser `\n` pour Entrée et `\f` pour attendre l'invite CLI suivante avant de continuer. |
| `-inject-keycode <hex>` | Injecte un code clavier brut par le chemin CLA bas niveau une fois l'invite CLI stable. Les touches maintenues injectées post-boot utilisent maintenant le meilleur modèle matériel actuel de l'émulateur : la première lecture CLA renvoie un code régulier avec bit 7 positionné, ce qui peut produire du texte visible dans la CLI. |
| `-inject-at-prompt <n>` | Déclenche l'injection configurée à la `n`ième apparition visible de l'invite CLI au lieu de la première (défaut `1`). |
| `-inject-at-frame <n>` | Déclenche `-inject-keycode` ou `-inject-str` à la trame absolue `n` au lieu d'attendre une invite CLI. |
| `-inject-delay <f>` | Attend `f` trames après l'apparition de l'invite CLI avant de déclencher `-inject-str` ou `-inject-keycode`. |
| `-inject-hold-frames <f>` | Maintient `-inject-keycode` actif pendant `f` trames d'ISR avant relâchement (défaut `1`). |

`-inject-str` reste la voie normale pour taper automatiquement une commande. `-inject-keycode` suit désormais assez fidèlement le modèle CLA bas niveau pour reproduire le chemin visible post-boot de `A` dans les audits, mais cela reste un outil de reverse engineering / de test bas niveau, pas la voie normale de saisie de commandes.

**Exemple — exécuter `LIST` automatiquement :**

```bash
./smemu6 -floppy <disque.dsk> -no-launcher -inject-str "LIST\n"
```

**Exemple — exécuter deux commandes sur deux invites successives :**

```bash
./smemu6 -harddisk ../harddisks/SM6WIN0.DSK -floppy2 <disque.dsk> \
  -no-launcher -inject-str "LIST DX1:\n\fLIST DX1:\n"
```

### Affichage

| Option | Description |
|--------|-------------|
| `-vmode <m>` | Forcer le mode vidéo : `alpha` (texte seul), `graphic` (graphique seul), `super` (texte + graphique) |
| `-gfxbits <b>` | Ordre des bits du bitmap : `msb` (défaut, conforme au matériel) ou `lsb` |
| `-scale <n>` | Zoom entier de la fenêtre 1–8 (défaut `1` → 512 × 508 pixels) |
| `-scanlines` | Superpose un effet de lignes de balayage CRT (assombrit une ligne sur deux) |
| `-phosphor <c>` | Couleur du phosphore : `green` (défaut, P31 `#00E700`) ou `white` (`#E8E8E8`) |
| `-phosphor-decay <f>` | Facteur de décroissance de la persistance par trame `0.0`–0.99 (défaut `0.70` ≈ P31) ; simule la rémanence du phosphore entre les trames d'extinction d'écran |
| `-no-phosphor` | Désactive la persistance du phosphore (décroissance instantanée) |
| `-no-display-off` | Ignore les écritures d'extinction d'écran sur le port `0x00` ; l'écran reste visible en permanence |
| `-verbose-video` | Affiche sur stderr les changements d'état d'écran (allumage, extinction, mode vidéo) |

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
spéciales. L'émulateur résout maintenant les touches ordinaires à partir de la
position de la scancode hôte via la table S471 auditée, puis les fait passer par
le chemin strict CLA / `SYS.SY`.

### Touches spéciales

| Touche PC | Fonction Smaky 6 |
|-----------|------------------|
| `F1` | Touche de fonction **CURSOR** |
| `F2` | Touche de fonction **COPY** |
| `F3` | Touche de fonction **KILL** |
| `F4` | Touche de fonction **PROGRA** |
| `F5` | Touche de fonction **SHOW** |
| `F6` | Touche de fonction **SEARCH** |
| `F7` | Touche de fonction **CHANGE** |
| Build web | Les navigateurs peuvent réserver `F1`..`F7` ; dans l'interface web, préférez les boutons dédiés **CURSOR / COPY / KILL / PROGRA / SHOW / SEARCH / CHANGE** du panneau frontal |
| `Flèches` | Alias hôte pour les combinaisons documentées `CURSOR+r/d/f/c` (`Haut/Gauche/Droite/Bas`) |
| `F9` | **DEFINE** (`0x1F`) via le mapping strict actuel de la matrice |
| `Fin` (End) | Position de touche ordinaire 30 auditée (`0x04` normal, `0x05` avec Shift) |
| `F11` ou `Pause` | **BREAK** (touche en haut à droite) — déclenche une NMI → entre dans le moniteur SYSMON |
| `Shift+F11` ou `Shift+Pause` | **SHIFT+BREAK** — réinitialisation matérielle (redémarre depuis DX0:) |
| `F12` | Bascule la **fenêtre de débogage native**. Elle est désactivée par défaut, s'ouvre à la demande seulement. Dans le build web, utilisez plutôt le panneau `DEBUG` intégré à la page |
| `Escape` | **ESC / UNDO** (touche en haut à gauche) — le mapping de travail courant émet `0x06` |

Les anciens alias de convenance comme `F8`, `Inser`, `Orig`, `Alt gauche`,
`Ctrl gauche`, `Windows gauche / Super`, `AltGr` et `Delete` ne font plus partie
de la base stricte actuellement documentée et ne doivent pas être considérés
comme pris en charge tant qu'ils ne sont pas réintroduits explicitement dans le code.

### Touches ordinaires

Les touches ordinaires actuellement prises en charge par le chemin strict de la
matrice incluent les positions auditées pour `A`..`Z`, `0`..`9`, `Space`,
`Backspace`, `Tab`, `Return`, crochets, barre oblique inverse, virgule, point
et tiret. `Shift` sélectionne la couche Shift S471, et `Caps Lock` sélectionne
la couche « caps-like » auditée.

Les appuis imprimables qui se chevauchent sont maintenant mis en file puis
promus un par un, ce qui permet à une frappe rapide normale d'atteindre la CLI
dans l'ordre au lieu de s'arrêter après la première touche imprimable.

Le clavier Smaky 6 utilise une disposition **QWERTZ** (allemand suisse) — si
votre clavier PC est QWERTY ou AZERTY, certaines positions de ponctuation
diffèrent, car le mapping est désormais basé sur la position et non sur la
saisie de texte de l'OS hôte.

Note matérielle : le dump complet de la ROM clavier S471 est maintenant
disponible. L'émulateur correspond déjà aux sorties spéciales confirmées qu'il
expose directement, notamment `Escape -> 0x06`, `Backspace -> 0x08`,
`Tab -> 0x09`, `Return -> 0x0D` et `Space -> 0x20`. C'est maintenant une base
strictement CLA-centrique pour les touches non textuelles, tandis que le texte
imprimable continue de passer par la compatibilité `SDL_TEXTINPUT`. Cette voie
de compatibilité inclut maintenant un repli « one-shot » pour les caractères
accentués composés, de sorte que des dispositions hôtes produisant `ü`, `ö`,
`ä`, `é`, `è`, `ê` ou `ç` atteignent de nouveau la CLI même si SDL n'expose pas
de scancode texte frais et réclamable pour l'événement composé.

### Barre de touches de fonction

La bande inférieure de la fenêtre de l'émulateur contient 7 boutons
cliquables, un par touche de fonction Smaky 6 (CURSOR, COPY, KILL,
PROGRA, SHOW, SEARCH, CHANGE).

- **Clic gauche maintenu** pour activer une touche de fonction ; relâcher désactive.
- **Clic droit** bascule un **verrouillage persistant** : le bouton reste actif
  (affiché en jaune) jusqu'au prochain clic droit, permettant les combinaisons
  modificateur+touche d'une seule main.

Ces touches sont **exclusivement des modificateurs** — aucun caractère n'est
écrit à l'écran, conformément au comportement du matériel réel. Les programmes
qui en ont besoin lisent directement cet état ; la CLI ne les traite pas comme
du texte saisi.

### Barre de disques / boutons RESET et NMI

La bande centrale affiche les LEDs d'activité des lecteurs de disquettes et
Winchester.  À droite se trouvent deux boutons :
- **BREAK** — clic gauche déclenche un NMI (identique à `F11` / `Pause`).
- **RESET** — nécessite **deux clics** : le premier arme le bouton (il clignote
  en orange pendant 3 s) ; le second confirme la réinitialisation matérielle.
  Cliquer ailleurs annule l'état armé.

### Fenêtre de débogage native

Appuyez sur `F12` pour ouvrir une seconde fenêtre SDL affichant les registres
Z80 en direct, les drapeaux, une vue de désassemblage compacte centrée sur le
PC courant, un éditeur mémoire hexadécimal/ASCII sur 256 octets et des
commandes d'exécution. `Space` met l'exécution en pause ou la relance, `S` ou
`F6` exécute une instruction, `Shift+F7` exécute un « step over » sur les
instructions `CALL` / `RST` / `DJNZ` en courant jusqu'au `PC` séquentiel
suivant (et retombe sinon sur un simple pas d'instruction), et `F7` exécute
une trame 50 Hz complète tout en laissant le débogueur ouvert ; cela fait donc avancer en
général plusieurs instructions jusqu'à la prochaine limite vidéo / IRQ. Dans le panneau mémoire, les flèches déplacent
le curseur, `PageUp` / `PageDown` changent de page, les touches hexadécimales
éditent les nibbles, `G` lance un saut vers une adresse sur 4 chiffres hex,
`P` recale le curseur sur le PC courant, et `O` bascule l'affichage des octets
entre l'hexadécimal et l'octal. Le mode octal reste pour l'instant un mode
d'affichage ; l'édition en place demeure hexadécimale. Dans la vue de
désassemblage, `Shift+Haut` / `Shift+Bas` déplacent la sélection, `Shift+F6`
recule la sélection d'une instruction décodée sans exécuter la machine, `F8` lance l'exécution
jusqu'à l'adresse sélectionnée, et `F9` active ou retire un point d'arrêt à
cette adresse. Si `F8` démarre alors que l'exécution est déjà arrêtée sur le
point d'arrêt courant, le débogueur réarme temporairement ce cas précis pour
laisser l'ordre "aller au curseur" progresser vers la ligne sélectionnée au
lieu de se réarrêter immédiatement sur le même `PC`. `Ctrl+A` et `Ctrl+V`
placent directement la vue mémoire sur les plans alpha et graphique. `Tab` et
`Shift+Tab` font tourner l'emplacement de surveillance actif dans le pied du
panneau mémoire, et `W` redirige cette surveillance sélectionnée vers une nouvelle
adresse hexadécimale sur 4 chiffres. Quand le curseur de désassemblage reste
recalé sur la machine, la ligne du `PC` actif reste proche du milieu du
panneau ; quand vous parcourez avec `Shift+F6` ou `Shift+Haut` / `Shift+Bas`,
la vue se recentre autour de la ligne sélectionnée pour qu'elle reste visible
pendant le parcours avant ou arrière. `Entrée` sur une instruction suivable
déplace le curseur de désassemblage vers sa destination décodée pour les
transferts de contrôle directs `call` / `jp` / `jr` / `djnz` / `rst`, et
`Retour arrière` remonte cette petite pile d'historique de suivi. Un résumé
compact de cible dans la barre d'état supérieure montre la destination suivable
actuellement sélectionnée avant d'appuyer sur `Entrée`.

**Panneau de débogage web :** dans le build web, utilisez le bouton `DEBUG`
intégré à la page pour ouvrir la première tranche du débogueur HTML. Elle
expose pour l'instant pause/reprise, pas d'instruction, pas de trame,
rafraîchissement manuel, un résumé compact de l'arrêt et des registres, ainsi
qu'un instantané live du désassemblage exporté par le backend du débogueur.
Cette première tranche reste volontairement plus petite que la fenêtre SDL
native : elle n'expose pas encore l'édition des points d'arrêt, l'édition
mémoire ou le parcours du curseur.

Le panneau STATE montre toujours les mots bruts en haut de pile, et l'en-tête
du désassemblage porte maintenant un indice compact de symboles pour les
premiers mots de pile quand ils correspondent à des labels FLO connus. Les
touches `0` à `3` déplacent directement le curseur de désassemblage vers ces
quatre mots de pile et empilent l'ancienne position dans le même petit
historique que `Entrée` / `Retour arrière`, ce qui facilite la lecture des
adresses de retour sans ajouter un nouveau panneau.
Ce même en-tête montre aussi le symbole FLO le plus proche du `PC` actif,
avec son décalage, afin que le pas à pas reste ancré sur la routine courante
même quand l'adresse exacte de l'instruction ne tombe pas sur une frontière de
symbole.

Si un pas `F6` revient sur la même ligne `djnz`, cela signifie en général que
l'instruction a bien été exécutée mais qu'elle a rebouclé sur la même adresse
en ne modifiant visiblement que le registre `B`. `Shift+F7` est alors le moyen
le plus rapide pour franchir cette boucle comptée et s'arrêter à l'adresse de
retombée.

Dans les builds natifs, le débogueur utilise maintenant l'en-tête généré des
exports FLO directement compilé dans l'émulateur ; il ne dépend donc plus d'un
fichier de symboles externe au moment de l'exécution. L'en-tête du
désassemblage affiche aussi un petit marqueur `FLO` et chaque ligne peut
montrer deux indices supplémentaires : une courte colonne de symbole quand
l'adresse de l'instruction correspond exactement à un export FLO, et un suffixe
final `;NOM` pour les cibles suivables de contrôle direct qui
correspondent à un symbole FLO connu. Dans le code threadé de style CALM,
les vecteurs de service sur 2 octets absorbent aussi l'octet de service suivant
dans la même ligne, par exemple `E7 5E` pour `?TEXTIM` ou `D7 14` pour
`?OPEN`, ce qui évite d'afficher une suite trompeuse de faux `RST` bruts.

La partie gauche du débogueur est divisée en deux résumés compacts :

- **CPU REGISTERS** montre l'ensemble principal des registres Z80 `AF`, `BC`,
  `DE`, `HL`, plus l'ensemble alternatif `AF'`, `BC'`, `DE'`, `HL'`, ainsi que
  les registres d'index / pile `IX`, `IY` et `SP`. `AF` signifie accumulateur +
  drapeaux ; les registres avec apostrophe sont la banque alternative du Z80,
  utilisée par `EX AF,AF'` et `EXX`.
- **STATE** montre le contexte d'exécution autour de ces registres. `PC` est
  l'adresse de la prochaine instruction. `I / R` sont les registres de vecteur
  d'interruption et de rafraîchissement. `IFF1/IFF2/IM` indique si les
  interruptions masquables sont actives et quel mode d'interruption (`0`, `1`
  ou `2`) est sélectionné. `EXEC` indique si le débogueur est en pause ou en
  cours d'exécution. `T-STATES` correspond à la taille de la tranche
  d'exécution la plus récente, et non à un total cumulé. `FRAMES` est le
  compteur de trames émulées du débogueur. La ligne `STACK` montre les quatre
  premiers mots 16 bits à partir du `SP` courant, ce qui permet de voir le haut
  de la pile de retour sans quitter le résumé d'exécution.
- **FLAGS** et **FLAGS'** décodent l'octet `F` courant dans `AF` et l'octet
  `F` alternatif dans `AF'`. L'ordre est `SZ5H3PNC` : Signe, Zéro, bit 5 non
  documenté, Half-carry, bit 3 non documenté, Parité/Débordement,
  Addition/Soustraction, Carry. Un `-` signifie que le bit correspondant est
  actuellement à zéro.
- Dans la liste de désassemblage, les marqueurs à gauche servent de légende
  compacte : `>` marque l'instruction qui contient actuellement le `PC` réel,
  `*` marque la ligne sélectionnée par le curseur du débogueur, et `B` marque
  un point d'arrêt à cette adresse. Une même ligne peut afficher plusieurs
  marqueurs en même temps, par exemple si l'instruction courante est aussi la
  ligne sélectionnée avec point d'arrêt.
- Si les symboles FLO sont chargés, la courte colonne placée juste après
  l'adresse montre le meilleur export correspondant à cette adresse exacte,
  par exemple `?OPEN:` ou `OUTCAR:`. Le suffixe optionnel après le mnémonique
  indique qu'une cible de contrôle direct correspond à un export FLO connu.
- Le panneau **MEMORY** affiche une page de 256 octets à la fois sous forme de
  grille `16 x 16`. La marge de gauche indique l'adresse de base de chaque
  ligne. Les colonnes d'octets sont regroupées par blocs de quatre pour
  faciliter la lecture. La case surlignée correspond au curseur mémoire
  courant ; `CURSOR=` dans l'en-tête du panneau donne son adresse exacte.
- Juste au-dessus du dernier résumé `MEM ...`, le pied du panneau mémoire
  affiche aussi trois petits emplacements de surveillance. L'emplacement
  sélectionné est marqué par `>`. Par défaut, les trois emplacements
  commencent sur `0x457E` (octet ordinaire en attente), `0x4580` (espace de
  travail des touches de fonction) et `0x45C0` (premier octet visible de la
  ligne CLI), mais chacun peut être redirigé depuis le débogueur.
- En vue hexadécimale, chaque octet est suivi à droite par un miroir ASCII :
  les octets imprimables sont montrés comme caractères, les octets non
  imprimables comme `.`. En vue octale, les mêmes octets sont affichés en
  octal sur trois chiffres et le miroir ASCII est masqué pour garder une mise
  en page lisible.
- Les couleurs mémoire portent aussi une information : les octets venant de la
  ROM sont dessinés différemment de la RAM inscriptible, et la case active du
  curseur reçoit une surbrillance distincte. Les touches hexadécimales éditent
  l'octet sélectionné nibble par nibble ; même lorsque le panneau affiche
  l'octal, l'édition reste hexadécimale.

### Touches standard

Le clavier Smaky 6 utilise une disposition **QWERTZ** (allemand suisse) — si
votre clavier PC est QWERTY ou AZERTY, certaines positions de ponctuation
peuvent différer, car le mapping est désormais basé sur la position et non sur
la saisie de texte de l'OS hôte.

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
python3 ../smaky6-tools/smaky6_samos.py list ../floppies/Sys1-H.dsk
python3 ../smaky6-tools/smaky6_samos.py extract-all ../floppies/Sys1-H.dsk private/extracted/ --metadata --clear
```

### Sous-répertoires (fichiers `.DR`)

SAMOS supporte les répertoires imbriqués stockés comme fichiers avec l'extension `.DR`.
Sur disque, le conteneur s'appelle `NOM.DR`, mais dans les commandes CLI le
composant de chemin omet le suffixe `.DR`. On utilise donc des formes comme
`LIST NOM`, `TYPE NOM:INNER.BS` ou `LIST DX1:NOM` pour accéder à un
sous-répertoire. `CDIR` seul affiche le répertoire courant ; `CLEAR` revient à
la racine.

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
./smemu6 -floppy <disque.dsk> -drive-sound
```

**Exemple — tout couper :**

```bash
./smemu6 -floppy <disque.dsk> -no-beeper
```

---

## 10. Automatisation et scripts

L'émulateur peut être piloté de manière non interactive en combinant
`-inject-str` et `-timeout`.

**Exécuter une commande et capturer la sortie écran :**

```bash
./smemu6 \
  -floppy <disque.dsk> \
    -no-display-off \
    -inject-str "LIST\n" \
    -timeout 20 \
    -scrdump 2>ecran.txt
```

`-scrdump` est surtout utile avec `-no-display-off` ; de nombreux chemins de
démarrage et de CLI de SAMOS éteignent l'affichage entre deux mises à jour,
donc l'association des deux options garde l'invite visible et rend les lignes
dumpées lisibles.

**Exécuter avec traçage pour le débogage :**

```bash
./smemu6 \
  -floppy <disque.dsk> \
    -trace \
    -timeout 15 2>trace.log
```

**Dumper la RAM en fin d'exécution :**

```bash
./smemu6 \
  -floppy <disque.dsk> \
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
./smemu6 -floppy <disque.dsk> -tracekbd 2>clavier.log
```

Pour un test de régression Linux/X11 ciblé du chemin des touches imprimables,
exécutez `tools/check_keyboard_asd_trace.sh` depuis la racine du dépôt. Le
script lance l'émulateur, attend que l'invite CLI soit visible, injecte des
événements `keydown`/`keyup` qui se chevauchent pour `a s d`, puis vérifie que
la CLI reçoit bien les trois insertions visibles dans l'ordre. Le nettoyage du
script relâche aussi les touches injectées pour ne pas laisser l'état clavier
de l'hôte bloqué.

---

## 12. Dumps mémoire

**Dump automatique à la fin :**

```bash
./smemu6 -floppy <disque.dsk> -timeout 10 -dump-ram snapshot.bin
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
./smemu6 -floppy <disque.dsk> -vmode alpha
```

**Les graphiques apparaissent inversés ou brouillés**

Le plan graphique utilise un encodage nibble-entrelacé avec MSB à gauche par défaut
(vérifié sur le matériel réel). Pour forcer l'ordre inverse :

```bash
./smemu6 -floppy <disque.dsk> -gfxbits lsb
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

**La sortie `-scrdump` est pauvre ou l'invite ne devient jamais lisible**

Utilisez `-no-display-off -scrdump` ensemble. `-scrdump` seul ne rapporte que
les lignes modifiées, et le blanking vidéo normal de SAMOS peut masquer
l'invite entre les trames.

**Comment générer / mettre à jour les PDF des manuels**

```bash
./tools/generate_pdfs.sh
```
