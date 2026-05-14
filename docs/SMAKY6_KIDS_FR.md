# Le Smaky 6 expliqué aux enfants
## Comment fonctionne un vrai ordinateur rétro !

*Pour les curieux de 10 ans et plus qui veulent comprendre comment les ordinateurs fonctionnent vraiment.*

---

## Bienvenue !

Tu as devant toi le **Smaky 6**, un ordinateur fabriqué en Suisse dans les années
1980 — quand tes parents ou tes grands-parents étaient peut-être aussi jeunes que toi !

À cette époque, les ordinateurs n'avaient pas de souris, pas d'icônes à cliquer,
pas de vidéos YouTube. Pour faire quelque chose, il fallait **taper des commandes
au clavier**. C'est ce que tu vas apprendre dans ce guide !

---

## Partie 1 : Comment fonctionne un ordinateur ?

### Le cerveau de l'ordinateur : le processeur (CPU)

Imagine que l'ordinateur est une cuisine.

Le **processeur** (ou CPU) est le cuisinier.
Il suit des recettes (les programmes) et fait une chose à la fois, très très vite.

Le processeur du Smaky 6 s'appelle le **Zilog Z80**.
Il travaille à une vitesse de **2,5 millions d'opérations par seconde** !

C'est beaucoup ? Oui... en 1980.
Aujourd'hui, ton téléphone fait des **milliards** d'opérations par seconde.
Mais le Z80 suffisait pour faire des calculs, afficher du texte, jouer à des jeux,
et écrire des programmes. L'essentiel, quoi.

> **À retenir :**
> CPU = le cuisinier de l'ordinateur.
> Il exécute des instructions, une par une, très rapidement.

---

### La mémoire vive (RAM) : le plan de travail

Toujours dans notre cuisine : la **RAM** (mémoire vive) est le plan de travail.
C'est là où le cuisinier pose les ingrédients en cours d'utilisation.

Le Smaky 6 a **64 Ko de RAM**.
"Ko" veut dire "kilo-octets". 1 octet peut stocker une lettre ou un chiffre.
64 Ko = 64 000 lettres environ.

C'est comme si tu n'avais que deux pages A4 pour écrire **tout** ce dont ton
programme a besoin en même temps.

*Comparaison :* Ton ordinateur ou tablette a probablement 4 000 000 Ko (= 4 Go) de RAM.
C'est **65 000 fois plus** que le Smaky 6 !

> **Important :**
> La RAM est **temporaire**. Quand tu éteins l'ordinateur, tout ce qui était en RAM
> disparaît. C'est pourquoi on sauvegarde sur disquette !

---

### La mémoire morte (ROM) : le livre de recettes de base

La **ROM** (mémoire morte) est un livre de recettes qui ne change jamais.
Elle contient les instructions de démarrage de l'ordinateur.

Sur le Smaky 6, on l'appelle la **"ROM Phantom"** (ROM fantôme).
Elle contient un tout petit programme (2 Ko seulement !) qui sait comment
lire la disquette et démarrer le vrai système.

> **Différence ROM / RAM :**
> - ROM = lecture seule, ne change pas, survit au redémarrage
> - RAM = lecture et écriture, s'efface à l'extinction

---

### La disquette : le tiroir des recettes

La **disquette** est comme un tiroir plein de recettes.
Elle garde les informations même quand l'ordinateur est éteint.

Le Smaky 6 utilise des disquettes **Micropolis** :
- 77 pistes (comme les sillons d'un disque vinyle)
- 16 secteurs par piste
- 256 octets par secteur
- **Total ≈ 315 000 octets = 315 Ko**

Un programme comme Microsoft Word aujourd'hui fait environ 500 000 Ko.
Il ne tiendrait **pas du tout** sur une disquette Smaky 6 !

> **Astuce :** Le Smaky 6 a deux lecteurs de disquettes :
> - **DX0:** = le lecteur du bas (comme un tiroir principal)
> - **DX1:** = le lecteur du haut (comme un tiroir de sauvegarde)

---

### L'écran : la fenêtre

L'écran du Smaky 6 peut afficher :
- Du **texte** : 20 lignes de 64 lettres (c'est comme 20 rangées de 64 cases)
- Des **dessins** : plus de 30 000 points lumineux qu'on peut allumer ou éteindre

Il n'y a pas de couleurs — c'est en **noir et blanc** (ou vert sur fond noir,
selon le modèle d'écran).

---

## Partie 2 : Démarrer le Smaky 6

### Comment démarrer ?

1. Insère la **disquette système** dans le lecteur **DX0:** (en bas)
2. Allume l'ordinateur
3. Appuie sur **SHIFT-BREAK** (la touche SHIFT + la touche BREAK)
4. Attends que le message `>` apparaisse

Le `>` s'appelle une **invite de commande**. C'est l'ordinateur qui dit :
"Je suis prêt ! Qu'est-ce que tu veux faire ?"

### Ce que tu verras au démarrage :

```
ROM de chargement rev 1-7
SAMOS  rev 2-8
DX0:
>
```

- **ROM de chargement** : La ROM Phantom fait son travail
- **SAMOS** : Le système d'exploitation (comme Windows, mais en beaucoup plus simple)
- **DX0:** : L'ordinateur a chargé depuis le lecteur DX0:
- **`>`** : Prêt !

---

### Les touches magiques du démarrage

| Touche              | Ce qui se passe                                    |
|---------------------|----------------------------------------------------|
| `SHIFT-BREAK`       | Démarrage normal (depuis DX0:)                     |
| `FUNCTION-SHIFT-BREAK` | Démarrage depuis DX1: (l'autre disquette)       |
| `FUNCTION-BREAK`    | Test de la mémoire (vérifie que la RAM fonctionne) |

---

## Partie 3 : Tes premières commandes

### La commande LIST : voir tes fichiers

```
> LIST
```

Tape `LIST` et appuie sur ENTRÉE.
L'ordinateur te montre tous les fichiers sur la disquette.

C'est comme ouvrir un tiroir pour voir ce qu'il contient !

> **Sur ton ordinateur aujourd'hui :** Tu ferais double-clic sur un dossier pour voir son contenu.
> C'est pareil, mais en texte.

---

### La commande COPY : copier un fichier

```
> COPY MON_JEU.SM DX1:
```

Cette commande copie le fichier `MON_JEU.SM` du lecteur principal (DX0:)
vers le second lecteur (DX1:).

**Astuce super pratique :** Appuie sur la touche `TAB` et l'ordinateur
écrit automatiquement `DX1:` pour toi ! Les créateurs du Smaky 6 avaient
pensé à tout.

> **Sur ton ordinateur aujourd'hui :** Ctrl+C puis Ctrl+V — mais le Smaky 6
> n'avait pas de souris pour sélectionner les fichiers !

---

### La commande DELETE : supprimer un fichier

```
> DELETE VIEUX_FICHIER.BS
```

**Attention !** Sur le Smaky 6, il n'y a pas de corbeille.
Quand tu supprimes un fichier, il est **vraiment** supprimé, tout de suite.
L'ordinateur ne te demande pas "es-tu sûr ?"

C'est rapide... mais ça peut faire peur !

---

### La commande TYPE : lire un fichier texte

```
> TYPE RECETTE.BS
```

Affiche le contenu d'un fichier à l'écran, ligne par ligne.

> **Sur ton ordinateur aujourd'hui :** Tu ouvrirais le fichier dans un éditeur de texte
> ou tu ferais "Aperçu".

---

### Lancer un programme

Pour lancer un programme, tu tapes simplement son nom (sans l'extension `.SM`) :

```
> BASIC
```

Cette commande lance l'interpréteur BASIC — un langage de programmation
que beaucoup d'enfants de l'époque utilisaient pour faire leurs premiers programmes !

---

## Partie 4 : Les fichiers et leurs noms

### Comment s'appellent les fichiers ?

Sur le Smaky 6, chaque fichier a un **nom** et une **extension** séparés par un point.

| Exemple de nom | Ce que c'est                        |
|---------------|-------------------------------------|
| `JEU.SM`      | Un programme (jeu) à lancer         |
| `TEXTE.BS`    | Un texte écrit en BASIC             |
| `DESSIN.IM`   | Une image                           |
| `PROJETS.DR`  | Un dossier (sous-répertoire)        |
| `SYS.SY`      | Un fichier du système (important !) |

> **Important :** Ne supprime **jamais** les fichiers `.SY` !
> Ce sont les fichiers du système d'exploitation. Sans eux, l'ordinateur
> ne peut plus démarrer. C'est comme supprimer le cuisinier de la cuisine !

---

## Partie 5 : Qu'est-ce qu'un système d'exploitation ?

Tu as peut-être entendu parler de Windows, macOS ou Linux.
Ce sont des **systèmes d'exploitation** (OS en anglais).

Sur le Smaky 6, le système s'appelle **SAMOS** (SMAky OS).

### À quoi ça sert ?

Le système d'exploitation fait le lien entre **toi** (qui tapes des commandes)
et le **matériel** (le processeur, la disquette, l'écran).

Sans système d'exploitation, tu devrais dire à l'ordinateur exactement où
écrire chaque bit sur la disquette, comment allumer chaque point de l'écran...
C'est le travail du système d'exploitation de simplifier tout ça.

### Comment ça démarre ?

1. La **ROM Phantom** (2 Ko) démarre et cherche la disquette
2. Elle charge **SYS.SY** depuis la disquette en mémoire RAM
3. SYS.SY charge **CLI.SY** (l'interpréteur de commandes)
4. CLI.SY affiche `>` et attend tes commandes

C'est comme une chaîne de dominos : chaque étape en lance la suivante !

---

## Partie 6 : Les messages d'erreur

Parfois, quelque chose ne se passe pas comme prévu.
Le Smaky 6 affiche alors un message `ERROR` suivi d'un nombre.

Les nombres sont écrits en **octal** — c'est une façon de compter
avec des chiffres de 0 à 7 seulement (au lieu de 0 à 9).

| Message        | Ce que ça veut dire                  | Que faire ?                     |
|----------------|--------------------------------------|---------------------------------|
| `ERROR 014`    | Fichier introuvable                  | Vérifier le nom du fichier      |
| `ERROR 013`    | Le fichier existe déjà               | Choisir un autre nom            |
| `ERROR 032`    | Disquette pleine                     | Supprimer des fichiers          |
| `ERROR 043`    | Impossible de charger le fichier     | Vérifier la disquette           |
| `ERROR 004`    | Fichier protégé (permanent)          | Ne peut pas être supprimé       |

> **Pourquoi l'octal ?**
> Les premiers ingénieurs informaticiens travaillaient souvent en octal
> (base 8) parce que c'est pratique avec les groupes de 3 bits.
> Le binaire (0 et 1) est la langue de l'ordinateur,
> l'octal était une façon plus courte de l'écrire.

---

## Partie 7 : Le son

Le Smaky 6 a un **haut-parleur** qui peut produire des sons.

Quand le système fait "BIP !", voici ce qui se passe en vrai :

1. Le programme écrit une valeur sur le **port 0x03** (une adresse spéciale pour le son)
2. Ça fait bouger rapidement une petite membrane dans le haut-parleur
3. La membrane vibre et crée une onde sonore
4. Ton oreille entend "BIP !"

Le BIP du Smaky 6 est une onde **carrée** à environ **584 Hz** —
c'est entre le Ré et le Mi bémol sur un piano !

---

## Partie 8 : Le clavier spécial

Le Smaky 6 a un clavier un peu différent des claviers d'aujourd'hui.
En plus des lettres et chiffres normaux, il a **7 touches de fonction** spéciales :

| Touche   | Sur l'émulateur | Ce que ça fait               |
|----------|-----------------|------------------------------|
| `CURSOR` | F1              | Dépend du programme          |
| `SEARCH` | F2              | Dépend du programme          |
| `KILL`   | F3              | Arrête ce qui est en cours   |
| `PROGRA` | F4              | Dépend du programme          |
| `SHOW`   | F5              | Dépend du programme          |
| `COPY`   | F6              | Dépend du programme          |
| `CHANGE` | F7              | Dépend du programme          |

Chaque programme décide ce que font la plupart de ces touches.
Seule `KILL` (F3 dans le mapping de l'émulateur) signifie généralement
**arrêter**.

---

## Récapitulatif : les commandes essentielles

| Commande           | Ce que ça fait                         |
|--------------------|----------------------------------------|
| `LIST`             | Voir les fichiers                      |
| `COPY NOM DX1:`    | Copier un fichier vers DX1:            |
| `DELETE NOM`       | Supprimer un fichier                   |
| `TYPE NOM`         | Lire un fichier texte                  |
| `BASIC`            | Lancer l'interpréteur BASIC            |
| `SMILE`            | Lancer l'assembleur SMILE              |
| `INIT DX1:`        | Préparer une nouvelle disquette        |
| `MODE G`           | Afficher les graphiques                |
| `MODE A`           | Afficher du texte seulement            |

---

## Pour aller plus loin

Si tu veux en savoir encore plus sur comment les ordinateurs fonctionnent :

- **Le binaire :** Tous les ordinateurs ne comprennent que 0 et 1.
  La lettre "A" = `01000001` en binaire.

- **Le langage BASIC :** C'est un langage de programmation facile à apprendre.
  Sur le Smaky 6, tu pouvais écrire :
  ```
  10 PRINT "BONJOUR !"
  20 GOTO 10
  ```
  Et l'ordinateur affichait "BONJOUR !" indéfiniment !

- **L'assembleur :** C'est le langage le plus proche du langage machine.
  Tu écris directement les instructions que le Z80 comprend.
  C'est difficile mais très puissant !

---

*Ce guide a été écrit pour expliquer le fonctionnement du Smaky 6,
un vrai ordinateur suisse des années 1980 qui a été recréé dans un émulateur.
Le Smaky 6 original a été conçu par Jean-Daniel Nicoud à l'EPFL de Lausanne.*
