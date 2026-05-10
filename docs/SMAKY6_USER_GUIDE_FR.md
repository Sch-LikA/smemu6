# Smaky 6 — Guide de l'utilisateur

*Smaky 6 rev 2-8, SAMOS OS, CLI.SY*

---

## Table des matières

1. [Présentation](#1-présentation)
2. [Mise en route](#2-mise-en-route)
3. [La ligne de commande](#3-la-ligne-de-commande)
4. [Gestion des fichiers](#4-gestion-des-fichiers)
5. [Répertoires](#5-répertoires)
6. [Exécution de programmes](#6-exécution-de-programmes)
7. [Gestion des disquettes](#7-gestion-des-disquettes)
8. [Mode d'affichage](#8-mode-daffichage)
9. [Date et heure](#9-date-et-heure)
10. [Transferts et périphériques](#10-transferts-et-périphériques)
11. [Monitor système](#11-monitor-système)
12. [Touches spéciales](#12-touches-spéciales)
13. [Extensions de fichiers](#13-extensions-de-fichiers)
14. [Messages d'erreur](#14-messages-derreur)
15. [Tableau de référence](#15-tableau-de-référence)

---

## 1. Présentation

Le **Smaky 6** est un micro-ordinateur suisse conçu par Jean-Daniel Nicoud à l'EPFL
(Lausanne) vers 1978–1982.  Il est équipé d'un processeur **Zilog Z80 à 2,5 MHz**,
de **64 Ko de RAM** et d'un **écran texte de 20 lignes × 64 caractères** combiné à
une couche graphique superposable de plus de 30 000 points.

Le système d'exploitation s'appelle **SAMOS** (*SMAky OS*).  L'interface utilisateur
est assurée par **CLI.SY**, un interpréteur de commandes en ligne chargé depuis la
disquette système au démarrage.

**Caractéristiques matérielles :**

| Composant           | Détail                                                           |
|---------------------|------------------------------------------------------------------|
| CPU                 | Zilog Z80, 2,5 MHz                                               |
| RAM                 | 64 Ko                                                            |
| ROM (Phantom)       | 2 Ko (gestionnaire de démarrage)                                 |
| Écran texte         | 20 lignes × 64 colonnes                                          |
| Écran graphique     | > 30 000 points, superposable                                    |
| Clavier             | QWERTZ suisse, 57 touches + 7 touches de fonction                |
| Disquette           | Micropolis, 77 pistes × 16 secteurs × 256 octets ≈ 315 Ko/disque |
| Lecteurs            | DX0: (inférieur), DX1: (supérieur)                               |
| Interfaces série    | 2 × USART (bande de papier, modem)                               |
| Interface parallèle | 1 (imprimante, périphérique externe)                             |
| Haut-parleur        | Haut-parleur programmable (buzzer bit-bangé)                     |
| Horloge             | Horloge temps réel avec batterie de secours                      |

---

## 2. Mise en route

Mettez une **disquette système** dans le lecteur **DX0:** (lecteur inférieur) et
allumez le Smaky 6.  La ROM Phantom affiche une invite de démarrage.

**Touches au démarrage :**

| Combinaison             | Effet                                              |
|-------------------------|----------------------------------------------------|
| `SHIFT-BREAK`           | Démarrage normal depuis **DX0:**                   |
| `FUNCTION-SHIFT-BREAK`  | Démarrage depuis **DX1:**                          |
| `BREAK`                 | Chargeur format PDP-11 (depuis l'USART 14)         |
| `FUNCTION-BREAK`        | Test mémoire (POST)                                |

Après le chargement, l'invite **`>`** apparaît — le système est prêt.

*Séquence de démarrage typique :*

```
ROM de chargement rev 1-7
SAMOS  rev 2-8
DX0:
>
```

---

## 3. La ligne de commande

L'invite **`>`** signale que le système attend une commande.  Les commandes ne
sont **pas** sensibles à la casse — `LIST`, `list` et `List` sont équivalents.

**Raccourcis clavier :**

| Touche                | Effet                                                         |
|-----------------------|---------------------------------------------------------------|
| `TAB`                 | Insère `DX1:` dans la ligne de commande                       |
| `BREAK` (ESC)         | Annule la ligne courante ; si vide, rappelle la dernière      |
| `KILL` (touche fonct.)| Interrompt le transfert de périphérique en cours              |
| `SHIFT-BREAK`         | Réinitialisation matérielle → redémarrage depuis DX0:         |

**Syntaxe générale :**

```
> COMMANDE [argument]
```

Les arguments contenant une espace doivent être encadrés par des apostrophes (`'`).

---

## 4. Gestion des fichiers

### 4.1 Lister les fichiers

```
> LIST
> LIST DX1:
> LIST MONFICHIER.SM
```

Affiche le contenu du répertoire courant, ou d'un lecteur/fichier précis.
Les colonnes indiquent : nom, taille en octets, attributs de protection.

Équivalents : DOS `DIR` — Unix `ls -l`

---

### 4.2 Copier un fichier

```
> COPY SOURCE.SM
> COPY SOURCE.SM DEST.SM
> COPY DX0:SOURCE.SM DX1:DEST.SM
```

Copie un fichier sur le même lecteur ou d'un lecteur à l'autre.
Si le fichier destination n'est pas spécifié, le nom source est conservé.

**Astuce :** Appuyez sur `TAB` pour insérer `DX1:` automatiquement.

Équivalents : DOS `COPY SOURCE DEST` — Unix `cp SOURCE DEST`

---

### 4.3 Supprimer un fichier

```
> DELETE MONFICHIER.SM
```

Supprime définitivement un fichier.  Aucune confirmation n'est demandée.
Un fichier marqué **permanent** (`ERROR 4`) ne peut pas être supprimé.

Équivalents : DOS `DEL` — Unix `rm`

---

### 4.4 Afficher le contenu d'un fichier

```
> TYPE MONFICHIER.BS
```

Affiche le contenu d'un fichier texte à l'écran.  Pour interrompre, appuyez sur `KILL`.

Équivalents : DOS `TYPE` — Unix `cat` / `less`

---

### 4.5 Imprimer un fichier

```
> PRINT MONFICHIER.BS
> PRINT MONFICHIER.BS $LP
```

Imprime un fichier sur l'imprimante connectée (nécessite `LP.SY` chargé).

Équivalents : DOS `PRINT` — Unix `lpr`

---

### 4.6 Concaténer des fichiers

```
> APPEND FICHIER1.BS FICHIER2.BS
```

Ajoute le contenu de `FICHIER2.BS` à la fin de `FICHIER1.BS`.

Équivalents : DOS `COPY A+B A` — Unix `cat B >> A`

---

### 4.7 Renommer un fichier

```
> SET ANCIEN.SM NOUVEAU.SM
```

Renomme (ou déplace) un fichier.  Peut aussi modifier les attributs de protection.

Équivalents : DOS `REN` — Unix `mv`

---

### 4.8 Compacter l'espace disque

```
> COMPRESS
```

Compacte le répertoire en supprimant les entrées effacées et en regroupant l'espace libre.
À effectuer périodiquement pour maintenir les performances.

Équivalents : DOS `DEFRAG` (partiellement) — Unix sans équivalent direct.

---

## 5. Répertoires

Le Smaky 6 utilise des **sous-répertoires** stockés comme des fichiers portant
l'extension `.DR`.

```
> CDIR              — affiche le répertoire courant
> CDIR PROJETS.DR   — entre dans le sous-répertoire PROJETS
> CDIR ..           — remonte d'un niveau (si supporté)
> CLEAR             — efface l'écran et revient au répertoire racine
```

Équivalents : DOS `CD` / `DIR` — Unix `cd` / `ls`

---

## 6. Exécution de programmes

Pour exécuter un programme (fichier `.SM`), il suffit de taper son nom sans extension :

```
> SMILE           — lance l'assembleur SMILE
> PASCAL          — lance l'interpréteur Pascal
> BASIC           — lance l'interpréteur BASIC
```

Les fichiers `.MC` sont des **macros CLI** (scripts de commandes) et s'exécutent
de la même façon.  Le système les distingue automatiquement.

Équivalents : DOS `.EXE` / `.COM` — Unix `./programme`

### LOAD

```
> LOAD
```

Recharge le dernier programme exécuté depuis la disquette.  Utile après un plantage
ou pour relancer rapidement le même programme.

---

## 7. Gestion des disquettes

### 7.1 Initialiser une disquette

```
> INIT DX1:
```

Formate et initialise une disquette vierge dans le lecteur **DX1:**.
**Toutes les données sont effacées.**  La disquette doit être présente dans le lecteur.

Équivalents : DOS `FORMAT A:` — Unix `mkfs`

---

### 7.2 Choisir le lecteur par défaut

```
> DX0:
> DX1:
```

Sélectionne le lecteur par défaut pour les opérations suivantes.
La touche `TAB` insère `DX1:` directement dans la ligne de commande.

Équivalents : DOS `A:` / `B:` — Unix `cd /mnt/dx0`

---

## 8. Mode d'affichage

Le Smaky 6 possède deux couches vidéo indépendantes : **texte (alpha)** et **graphique**.
La commande `MODE` permet de les activer séparément ou ensemble.

```
> MODE          — mode texte seul (identique à MODE A)
> MODE A        — texte uniquement
> MODE G        — graphique uniquement
> MODE G P      — graphique, points fins
> MODE 2        — texte ET graphique superposés
> MODE 2 P      — texte + graphique, points fins
```

---

## 9. Date et heure

```
> STIME  15:30     — règle l'heure à 15h30
> SDATE  09/05/83  — règle la date au 9 mai 1983
> SDAY   SAMEDI    — règle le jour de la semaine
```

Équivalents : DOS `TIME` / `DATE` — Unix `date -s "..."`

---

## 10. Transferts et périphériques

Les périphériques sont désignés par un préfixe `$` dans les commandes `COPY`,
`TYPE`, `PRINT`, `APPEND` :

| Nom    | Dir.    | Matériel                                    |
|--------|---------|---------------------------------------------|
| `$PR`  | Entrée  | Lecteur de bande perforée (USART 4, 20 mA)  |
| `$PP`  | Sortie  | Perforateur de bande (USART 4)              |
| `$PI`  | Entrée  | Interface parallèle                         |
| `$PO`  | Sortie  | Interface parallèle                         |
| `$MI`  | Entrée  | Modem (USART 6)                             |
| `$MO`  | Sortie  | Modem (USART 6)                             |
| `$LP`  | Sortie  | Imprimante ligne (overlay LP.SY requis)     |
| `$KEY` | Entrée  | Clavier                                     |
| `$DIS` | Sortie  | Écran                                       |

**Exemples :**

```
> COPY MONFICHIER.BS $LP        — imprime sur l'imprimante
> COPY $PR MONFICHIER.BS        — lit la bande perforée vers un fichier
> COPY MONFICHIER.BS $MO        — envoie un fichier via le modem
```

Pour interrompre un transfert, appuyez sur la touche **KILL**.

---

## 11. Monitor système

```
> MON
```

Entre dans le **moniteur machine** (SYSMON).  Ce mode bas-niveau permet d'examiner
et de modifier la mémoire, d'exécuter du code machine, et de déboguer les programmes.

Pour revenir à la ligne de commande CLI, tapez la commande de sortie du moniteur.

Équivalents : DOS `DEBUG` — Unix `gdb` / `xxd`

---

## 12. Touches spéciales

| Touche                  | Contexte        | Effet                                             |
|-------------------------|-----------------|---------------------------------------------------|
| `TAB`                   | Ligne commande  | Insère `DX1:` dans la ligne                       |
| `BREAK` / `ESC`         | Ligne commande  | Annule la ligne ; si vide, rappelle la précédente  |
| `KILL`                  | Tout            | Interrompt le transfert ou l'impression en cours   |
| `SHIFT-BREAK`           | Boot / runtime  | Réinitialisation → redémarrage depuis DX0:         |
| `FUNCTION-SHIFT-BREAK`  | Boot            | Redémarrage depuis DX1:                            |
| `FUNCTION-BREAK`        | Boot            | Test mémoire (POST)                                |
| `CHANGE`  (F1)          | Programmes      | Touche de fonction programme                       |
| `SEARCH`  (F2)          | Programmes      | Touche de fonction programme                       |
| `SHOW`    (F3)          | Programmes      | Touche de fonction programme                       |
| `COPY`    (F4)          | Programmes      | Touche de fonction programme                       |
| `CURSOR`  (F5)          | Programmes      | Touche de fonction programme                       |
| `PROGRA`  (F6)          | Programmes      | Touche de fonction programme                       |
| `KILL`    (F7)          | Programmes      | Touche de fonction / arrêt transfert               |

---

## 13. Extensions de fichiers

| Extension | Type                                          |
|-----------|-----------------------------------------------|
| `.SY`     | Fichier système (OS)                          |
| `.SM`     | Programme exécutable                          |
| `.MC`     | Macro CLI (script de commandes)               |
| `.BS`     | Source BASIC                                  |
| `.SR`     | Source assembleur SMILE                       |
| `.DR`     | Répertoire (sous-dossier)                     |
| `.LS`     | Listing assembleur                            |
| `.ST`     | Table de symboles                             |
| `.IM`     | Image (bitmap 1 bit par pixel)                |
| `.HP`     | Aide (fichier texte HELP)                     |

---

## 14. Messages d'erreur

Les erreurs sont affichées sous la forme `ERROR nnn` où `nnn` est le code en **octal**.

| Déc | Oct  | Message anglais (manuel)  | Message français (ER.SY)         |
|-----|------|---------------------------|----------------------------------|
|   1 | 001  | Write protect file        | fichier protégé écriture         |
|   2 | 002  | Read protect file         | fichier protégé lecture          |
|   4 | 004  | Permanent file            | fichier permanent                |
|   5 | 005  | Line too long             | ligne trop longue                |
|   6 | 006  | End of file               | fin de fichier                   |
|   7 | 007  | File end overflow         | dépassement fin de fichier       |
|  10 | 012  | File in use for writing   | fichier ouvert en écriture       |
|  11 | 013  | File already exists       | fichier déjà existant            |
|  12 | 014  | File does not exist       | fichier inexistant               |
|  13 | 015  | Illegal filename          | nom de fichier illégal           |
|  14 | 016  | Illegal reservation       | réservation illégale             |
|  16 | 020  | Cannot load file          | chargement impossible            |
|  17 | 021  | Out of file               | plus de fichier                  |
|  20 | 024  | File in use for reading   | fichier ouvert en lecture        |
|  21 | 025  | Unknown device            | périphérique inconnu             |
|  22 | 026  | Channel error             | erreur de canal                  |
|  23 | 027  | File(s) in use            | fichier(s) en cours              |
|  24 | 030  | All channels in use       | tous les canaux occupés          |
|  25 | 031  | Directory full            | répertoire plein                 |
|  26 | 032  | Disk full                 | disque plein                     |
|  30 | 036  | Device timeout            | timeout périphérique             |
|  31 | 037  | Write protect tab set     | languette de protection          |
|  32 | 040  | Write error               | erreur d'écriture                |
|  33 | 041  | Read error                | erreur de lecture                |
|  34 | 042  | No starting address       | pas d'adresse de départ          |
|  35 | 043  | Bad load                  | chargement erroné                |
|  36 | 044  | Buffer full               | tampon plein                     |
| 110 | 156  | Illegal order             | ordre illégal                    |
| 114 | 162  | System error              | erreur système                   |
| 115 | 163  | Map error                 | erreur de map                    |

**Exemple :** `ERROR 043` = octal 043 = décimal 35 = *Bad load* (disquette illisible).

---

## 15. Tableau de référence

| Commande Smaky 6 | Description                       | DOS              | Unix/Linux           |
|------------------|-----------------------------------|------------------|----------------------|
| `LIST`           | Lister le répertoire              | `DIR`            | `ls -l`              |
| `LIST DX1:`      | Lister le lecteur DX1:            | `DIR B:`         | `ls /mnt/dx1`        |
| `CDIR NOM.DR`    | Changer de répertoire             | `CD NOM`         | `cd NOM`             |
| `COPY SRC DEST`  | Copier un fichier                 | `COPY SRC DEST`  | `cp SRC DEST`        |
| `DELETE NOM`     | Supprimer un fichier              | `DEL NOM`        | `rm NOM`             |
| `SET OLD NEW`    | Renommer un fichier               | `REN OLD NEW`    | `mv OLD NEW`         |
| `TYPE NOM`       | Afficher le contenu               | `TYPE NOM`       | `cat NOM`            |
| `PRINT NOM`      | Imprimer                          | `PRINT NOM`      | `lpr NOM`            |
| `APPEND A B`     | Concaténer des fichiers           | `COPY A+B A`     | `cat B >> A`         |
| `COMPRESS`       | Compacter le disque               | `DEFRAG`         | *(n/d)*              |
| `INIT DX1:`      | Formater une disquette            | `FORMAT B:`      | `mkfs /dev/fd1`      |
| `MODE A`         | Mode texte seul                   | `MODE CO80`      | *(n/d)*              |
| `MODE G`         | Mode graphique seul               | `MODE BW320`     | *(n/d)*              |
| `MODE 2`         | Texte + graphique                 | *(n/d)*          | *(n/d)*              |
| `STIME HH:MM`    | Régler l'heure                    | `TIME`           | `date -s "HH:MM"`    |
| `SDATE JJ/MM/AA` | Régler la date                    | `DATE`           | `date -s "..."`      |
| `HELP`           | Aide en ligne                     | `HELP`           | `man` / `--help`     |
| `MON`            | Moniteur machine                  | `DEBUG`          | `gdb` / `xxd`        |
| `LOAD`           | Recharger le dernier programme    | *(n/d)*          | *(n/d)*              |
| `STP`            | Arrêter le système                | *(n/d)*          | `halt`               |
| `NOMFICH`        | Lancer un programme               | `NOMFICH.EXE`    | `./nomfich`          |
| `DX0:` / `DX1:` | Choisir le lecteur par défaut     | `A:` / `B:`      | `cd /mnt/dx0`        |
| `SHIFT-BREAK`    | Réinitialisation matérielle       | `Ctrl+Alt+Suppr` | `reboot`             |

---

*Document établi à partir du manuel original Smaky 6 rev 2-8 (EPFL/Epsitec),
de l'analyse de CLI.SY et SYS.SY par rétro-ingénierie, et de l'émulateur Smemu6.*
