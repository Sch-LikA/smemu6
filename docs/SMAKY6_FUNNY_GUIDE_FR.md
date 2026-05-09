# Smaky 6 — Manuel du Programmeur Désespéré

*Pour ceux qui pensaient que l'informatique serait facile.*

> Ce manuel est rédigé dans l'hypothèse que vous avez déjà survécu à au moins
> un plantage DOS, un kernel panic Linux, ou une réunion sur les exigences logicielles.

---

## Chapitre 0 : Philosophie

Le Smaky 6 a été conçu à une époque bénie où :

- "Copier-coller" signifiait vraiment copier et coller (avec de la colle)
- 64 Ko de RAM était **considéré comme un luxe obscène**
- La notion de "plantage" n'existait pas — le programme *terminait avec grâce* sous la forme d'un joli `ERROR 043`
- Il n'y avait pas de pilotes graphiques à mettre à jour, pas de Windows Update qui redémarre au pire moment, et absolument **personne** n'avait d'opinion sur le framework JavaScript du moment

Si votre programme ne tient pas dans 64 Ko, c'est votre problème.
Si votre programme tient dans 64 Ko, c'est aussi votre problème, mais un problème *élégant*.

---

## Chapitre 1 : Mise en route — ou "Pourquoi ça ne démarre pas"

**Étape 1 : Insérer la disquette dans DX0:**

Ceci est la partie la plus difficile pour un développeur moderne.
Une *disquette* est un dispositif physique circulaire qui stocke des données de manière magnétique.
Elle ne se branche pas en USB. Elle n'a pas de pilote Bluetooth. Elle ne nécessite pas un compte cloud.
Elle nécessite en revanche d'être **insérée dans la bonne direction** — une compétence perdue depuis 1998.

**Étape 2 : Appuyer sur SHIFT-BREAK**

Notez que la touche s'appelle `BREAK`, pas `Ctrl+Alt+Suppr`, pas `sudo reboot`, pas
`systemctl reboot --force --force`. Juste `SHIFT-BREAK`. Profitez de cette simplicité.

**Étape 3 : Attendre le prompt `>`**

Si vous voyez `ERROR 043`, retournez à l'Étape 1.
Si vous voyez `ERROR 033`, retournez à l'Étape 1 avec une disquette différente.
Si vous ne voyez rien, vérifiez que l'écran est allumé.
(Ne riez pas. Des tickets de support ont été ouverts pour cette raison.)

```
ROM de chargement rev 1-7      ← Le système dit bonjour
SAMOS  rev 2-8                 ← L'OS dit bonjour
DX0:                           ← Le lecteur dit bonjour
>                              ← Votre clavier dit "à vous"
```

---

## Chapitre 2 : La Ligne de Commande — ou "Git n'existe pas encore, et c'est merveilleux"

L'invite **`>`** est l'ancêtre de votre terminal préféré.
Elle n'a pas de plugin oh-my-zsh. Elle n'affiche pas la branche git dans le prompt.
Elle n'a pas de thème Dracula. Elle est juste **`>`**.

C'est magnifique.

**Raccourci killer : la touche `TAB`**

Sur votre ordinateur moderne, `TAB` complète les chemins. Sur le Smaky 6, `TAB`
insère directement `DX1:` dans la ligne de commande — parce que le cas d'utilisation
le plus fréquent était de copier des fichiers vers le second lecteur, et les
concepteurs ont décidé de ne pas perdre votre temps.

Comparez avec l'autocomplétion de `npm` qui met 3 secondes à se charger.

**La touche `BREAK`** annule la ligne courante. Si la ligne est déjà vide, elle
rappelle la dernière commande. Cela correspond exactement à `Ctrl+C` et `↑` réunis en
une seule touche. Efficacité maximale. Fréquence cardiaque minimale.

---

## Chapitre 3 : Gestion des Fichiers — ou "Votre Système de Fichiers Tient dans 315 Ko"

**`LIST`** — Lister les fichiers

```
> LIST
```

Affiche tous vos fichiers. Il y en a peu. C'est voulu.
Pas de `node_modules`. Pas de `.git` avec 47 000 objets d'emballage.
Pas de dossier `__pycache__` mystérieux qui réapparaît tout seul.

Sur le Smaky 6, si vous avez 20 fichiers, vous êtes un utilisateur avancé.

*Équivalent moderne :* `ls -la` suivi de 47 colonnes dont vous lisez 2.

---

**`COPY SOURCE DEST`** — Copier un fichier

```
> COPY MONFIC.SM DX1:
```

Copie un fichier. Ça copie le fichier. C'est tout ce que ça fait.
Il ne crée pas 18 versions en cache. Il ne vous demande pas si vous voulez synchroniser
avec le cloud. Il ne vous suggère pas d'installer une extension.

Il copie le fichier.

Prenez un moment pour apprécier.

*Équivalent DOS :* `COPY` *(même chose, Smaky l'a eu en premier)*
*Équivalent UNIX :* `cp` *(même chose en moins de lettres)*

---

**`DELETE FICHIER`** — Supprimer un fichier

```
> DELETE HONTE.BS
```

Supprime le fichier. **Définitivement.** Aucune corbeille. Aucune confirmation.
Aucun "Êtes-vous sûr ? (o/n)" — ce qui est à la fois terrifiant et rafraîchissant.

*Note :* Si le fichier est marqué **permanent** (`ERROR 4`), le système refuse poliment
de le supprimer. C'est la seule protection qui existe. Appréciez-la.

*Équivalent moderne :* `rm -rf` mais pour gens sages qui n'ont pas de wildcard accidents.

---

**`COMPRESS`** — Compacter le disque

```
> COMPRESS
```

Réorganise le disque pour récupérer l'espace des fichiers effacés.
Appelez ça le "ramasse-miettes" des années 80.

*Note philosophique :* En 2026, les garbage collectors font ça automatiquement
en arrière-plan sur vos 32 Go de RAM pendant que vous regardez YouTube.
Sur le Smaky 6, vous deviez le faire manuellement, ce qui vous donnait un **sentiment
de contrôle total** et une vague satisfaction existentielle.

---

## Chapitre 4 : Les Extensions de Fichiers — ou "Moins de 3 Lettres et Fier de L'être"

Le Smaky 6 utilise des extensions de 2 lettres, car ses concepteurs savaient que la
vie est courte et les disquettes petites.

| Extension | Signification               | Commentaire de programmeur                                   |
|-----------|-----------------------------|--------------------------------------------------------------|
| `.SY`     | Fichier système             | Ne pas effacer. Non, vraiment. Ne pas effacer.               |
| `.SM`     | Exécutable (programme)      | Le `.exe` de l'époque, mais *sans* dialogue UAC              |
| `.MC`     | Macro (script CLI)          | Shell scripts, mais sans 40 ans de `bash` quirks             |
| `.BS`     | Source BASIC                | Le langage qui a enseigné l'informatique à une génération    |
| `.SR`     | Source assembleur SMILE     | Pour ceux qui trouvaient le BASIC trop "haut niveau"         |
| `.DR`     | Répertoire (dossier)        | Un dossier qui est aussi un fichier. Très zen.               |
| `.LS`     | Listing assembleur          | L'ancêtre du `make 2>&1 \| tee build.log`                   |
| `.ST`     | Table de symboles           | Ce que `nm` et `objdump` font, en moins bavard               |
| `.IM`     | Image bitmap 1 bpp          | La résolution est faible, mais les attentes aussi            |
| `.HP`     | Fichier d'aide              | Une aide qui aide vraiment — concept révolutionnaire         |

---

## Chapitre 5 : Les Touches de Fonction — ou "Sept Touches pour les Gouverner Toutes"

Le Smaky 6 possède **7 touches de fonction** physiques gravées dans le clavier.
Chaque programme leur donne le sens qu'il veut.

Comparez avec votre clavier moderne qui a 12 touches de fonction dont vous n'utilisez
que `F5` (actualiser), `F11` (plein écran), et parfois `F2` (renommer, si vous y
pensez).

| Touche   | Émul. PC | Ce que ça fait en théorie          | Ce que ça fait en pratique                  |
|----------|----------|------------------------------------|---------------------------------------------|
| `CHANGE` | F1       | Changer quelque chose              | Dépend du programme                         |
| `SEARCH` | F2       | Chercher quelque chose             | Dépend du programme                         |
| `SHOW`   | F3       | Montrer quelque chose              | Dépend du programme                         |
| `COPY`   | F4       | Copier quelque chose               | Dépend du programme                         |
| `CURSOR` | F5       | Déplacer le curseur                | Dépend du programme                         |
| `PROGRA` | F6       | Programmer quelque chose           | Dépend du programme (très pratique)         |
| `KILL`   | F7       | **Tuer quelque chose**             | Interrompt un transfert. Nom parfait.        |

La touche `KILL` est la seule dont le comportement est cohérent partout.
Elle tue les choses. C'est son travail. Elle est honnête.

---

## Chapitre 6 : Les Périphériques — ou "$LP n'est pas un Rappeur"

Les périphériques sont désignés par un `$` suivi de 2 lettres.
C'est la variable d'environnement de l'époque, mais utile.

```
> COPY RAPPORT.BS $LP          — Imprime sur l'imprimante
> COPY $PR PROGRAMME.SR        — Lit une bande perforée
> COPY DONNÉES.BS $MO          — Envoie des données au modem à 300 baud
```

**`$LP`** — Imprimante ligne *(Line Printer)*

Imprime sur une vraie imprimante matricielle bruyante qui fait "TRRRTRRRTRRR" et dont
chaque page sent l'encre et les espoirs.  Nécessite `LP.SY` chargé.

*Équivalent moderne :* Envoyer à l'imprimante du bureau et espérer que quelqu'un ne
l'a pas branchée à un Mac sur un réseau différent.

---

**`$PR` / `$PP`** — Lecteur/perforateur de bande papier

La bande perforée est l'ancêtre de l'USB. Sauf qu'elle ne se branche pas, qu'elle
se déroule, et que si vous la laissez tomber vous passez une heure à la rembobiner.
Connexion via boucle de courant 20 mA, USART 4.

*Statut en 2026 :* Absolument personne n'utilise ça. Absolument personne.

---

**`$MI` / `$MO`** — Modem

300 à 2400 bauds. Suffisant pour transmettre votre programme en 20 minutes.
Suffisant pour entendre "squeeee KRRR PING PONG squeeee" et appeler ça "se connecter".

*Note :* Le modem était optionnel. La patience était obligatoire.

---

## Chapitre 7 : Les Codes d'Erreur — ou "ERROR 043 et Moi"

Les erreurs sont affichées en **octal**. Parce que pourquoi pas.

Si vous avez grandi avec des messages d'erreur modernes comme :

```
TypeError: Cannot read properties of undefined (reading 'map')
    at Object.<anonymous> (/app/node_modules/some-lib/dist/index.js:1:42837)
    ... 47 lignes de stack trace dans du code minifié ...
```

…alors `ERROR 043` vous semblera d'une clarté cristalline.

**Tableau de traduction rapide :**

| Ce que vous voyez | Ce que ça veut dire            | Ce que vous devriez faire              |
|-------------------|--------------------------------|----------------------------------------|
| `ERROR 043`       | Bad load (mauvais chargement)  | Vérifier la disquette. Retenter. Prier.|
| `ERROR 033`       | Read error (erreur de lecture) | La disquette est probablement abîmée   |
| `ERROR 032`       | Disk full (disque plein)       | Supprimer ce que vous n'aimez pas      |
| `ERROR 031`       | Write protect tab set          | Retirer la languette de protection     |
| `ERROR 014`       | File does not exist            | Vérifier l'orthographe. C'est souvent ça. |
| `ERROR 013`       | File already exists            | Choisir un autre nom                   |
| `ERROR 004`       | Permanent file                 | Ce fichier ne veut pas mourir          |
| `ERROR 001`       | Write protect file             | Le fichier dit "non"                   |

**Note importante sur l'octal :** `ERROR 043` = octal 43 = décimal 35 = *Bad load*.
Pas decimal 43. Pas hexadécimal 43. **Octal.**
Bienvenue dans les années 80, où les bases de numération étaient un choix de style.

---

## Chapitre 8 : Le Monitor — ou "Bienvenue en Enfer Confortable"

```
> MON
```

Cette commande vous propulse dans le **SYSMON**, le moniteur machine.
C'est l'équivalent de `gdb` pour le Z80, sauf que vous travaillez en hexadécimal,
en octal, et parfois en souhaitant être devenu comptable.

Le SYSMON vous permet de :
- Lire et modifier n'importe quel octet de la RAM (toute la RAM, sans permission, sans sudo)
- Exécuter du code machine directement
- Déboguer vos programmes en regardant les registres un par un
- Sentir que vous contrôlez vraiment la machine

Pour les développeurs modernes : c'est comme avoir un accès direct à `/proc/mem`
mais **sans segfault**, sans AppArmor, et sans kernel OOM-killer qui tue votre
session au mauvais moment.

*Pour quitter le monitor :* tapez la commande de sortie du monitor.
*(Oui, nous ne la documentons pas ici. Découvrir la commande de sortie fait partie de l'initiation.)*

---

## Chapitre 9 : Le Son — ou "Bip"

Le Smaky 6 possède un **haut-parleur programmable**.

Il fait "bip".

Plus précisément, le code ROM effectue environ 96 écritures sur le port 0x03 à
intervalles de ~2139 cycles, produisant une onde carrée à ~584 Hz pendant ~82 ms.

En termes modernes : c'est un son de notification qui ne peut pas être désactivé,
ne peut pas être mis en sourdine, et ne peut pas être remplacé par une vibration.
C'est soit "bip", soit silence.

Les utilisateurs modernes qui se plaignent des sons de notification n'ont pas connu
les claviers mécaniques de 1982.

*Pour produire un son depuis votre programme :*
```
?BEEP       — un bip standard
?PLAY       — une mélodie (tableau fréquence/durée, terminé par 0)
```

*Équivalent moderne :* `console.log('\x07')` (le BEL ASCII — ça ne marche plus dans
la plupart des terminaux modernes, preuve que le progrès n'est pas toujours linéaire)

---

## Chapitre 10 : FAQ — Questions Fréquentes

**Q : Mon programme ne tient pas dans 64 Ko. Que faire ?**

R : Réécrire le programme. Supprimer les fonctionnalités. Apprendre à compter les
octets. Reconsidérer vos choix de vie.

---

**Q : Comment accéder à internet depuis le Smaky 6 ?**

R : Vous ne pouvez pas. C'est une fonctionnalité.

---

**Q : J'ai accidentellement supprimé `SYS.SY`. Comment récupérer ?**

R : Vous ne pouvez pas. C'est la raison pour laquelle on fait des sauvegardes.
*Bienvenue dans l'informatique des années 80.*

---

**Q : Le programme plante avec `ERROR 043`. Quel débogueur utiliser ?**

R : `MON`. Puis vos yeux. Puis la liste des instructions Z80.
Puis éventuellement du papier millimétré pour dessiner la pile d'appels à la main.

---

**Q : Comment installer des paquets supplémentaires ?**

R : On copie des fichiers depuis une autre disquette.
C'est ça, la gestion de paquets. Aucun `npm install` qui télécharge 847 Mo de
dépendances transitives.

---

**Q : La disquette fait un bruit bizarre. Est-ce normal ?**

R : Non. Sauvegardez maintenant. Sauvegardez tout. Sauvegardez deux fois.

---

**Q : Puis-je faire tourner Docker sur le Smaky 6 ?**

R : Docker n'existera pas avant 30 ans.
Profitez de cette paix.

---

## Épilogue

Le Smaky 6 a été conçu par des gens qui aimaient l'informatique et voulaient que
l'informatique soit **utile**, **compréhensible**, et **rapide** — dans une époque
où "rapide" signifiait "répond en moins d'une seconde sur du matériel que vous
pouvez réparer vous-même avec un fer à souder".

Quarante ans plus tard, nos machines sont un million de fois plus puissantes et
nos pages web mettent 8 secondes à charger.

Le Smaky 6 n'a aucune opinion là-dessus.
Il attend votre commande.

```
>
```

*Manuel rédigé avec affection pour une machine qui mérite mieux qu'un musée.*
