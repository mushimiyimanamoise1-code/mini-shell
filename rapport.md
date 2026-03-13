<div align="center" style="margin-top: 150px;">

<h1>Rapport de Projet : Système et Réseau</h1>

<h2>Conception et Implémentation d'un Mini-Shell Unix en C</h2>

<br><br><br><br><br>

**Réalisé par :** <br>
Moïse MUSHIMIYIMANA

Num etu : 22201338

<br><br><br><br>

**Ecole :** Université Jean Jaurès <br><br>

**Plateforme de rendu :** IRIS <br>
**Année universitaire :** 2025-2026

</div>

<div style="page-break-after: always;"></div>

# Rapport de Projet : Conception et Implémentation d'un MiniShell Unix en C

Ce document présente la conception, l'architecture et les fonctionnalités d'un interpréteur de commandes interactif (MiniShell) développé en langage C. L'objectif principal de ce projet est de reproduire le comportement fondamental d'un shell Unix (tel que Bash ou Zsh) en exploitant directement les appels système POSIX. Ce rapport détaille les choix techniques effectués pour la gestion des processus, la manipulation des flux d'entrée/sortie, la communication inter-processus et la gestion asynchrone des signaux.

---
<br>

## 1. Instructions de Compilation et d'Exécution

### Prérequis

Le programme utilise des appels système spécifiques à la norme POSIX (`fork`, `execvp`, `waitpid`, `pipe`, `sigaction`). Il doit impérativement être compilé et exécuté sur un environnement **Linux** (natif, machine virtuelle, ou Windows Subsystem for Linux - WSL).

### Compilation

Ouvrez votre terminal dans le répertoire contenant le fichier source `mini_shell.c` et utilisez le compilateur GCC avec la commande suivante :

```bash
gcc minishell.c -o minishell

```

### Exécution

Une fois la compilation réussie (génération de l'exécutable `minishell` sans erreurs ni avertissements bloquants), lancez le programme avec :

```bash
./minishell Moise 
```
NB: le nom de l'utilisateur après la commande ./minishell n'est pas obligatoire


Pour quitter proprement l'interpréteur à tout moment, tapez la commande `exit` ou `quit`.

---

<br>

## 2. Architecture Globale et Boucle Principale

Le cœur du MiniShell repose sur le paradigme REPL (Read-Eval-Print Loop) :

1. **Read (Lecture) :** Le programme affiche un prompt dynamique incluant le nom de l'utilisateur et le répertoire de travail courant (récupéré via `getcwd`). L'entrée utilisateur est lue de manière sécurisée grâce à `fgets`, prévenant ainsi les débordements de tampon (Buffer Overflow).
2. **Parsing (Analyse lexicale) :** La chaîne de caractères est nettoyée (suppression du saut de ligne `\n`) puis découpée en "jetons" (tokens) via la fonction `strtok`, en utilisant l'espace comme délimiteur. Le premier jeton, représentant la commande, est converti en minuscules pour assurer une insensibilité à la casse.
3. **Eval/Print (Évaluation et Exécution) :** Le tableau d'arguments généré est passé au moteur d'exécution (`execute_pipeline` puis `executer_commande`) qui détermine la nature de la commande (interne, externe, pipeline) et l'exécute en conséquence.

---

<br>

## 3. Fonctionnalités Implémentées

### 3.1. Commandes Internes (Built-ins)

Les commandes internes sont exécutées directement par le processus parent (le MiniShell lui-même), sans création de processus enfant. Cela est indispensable pour les commandes qui modifient l'état de l'environnement global du shell.

- **`cd` (Change Directory) :** Utilise l'appel système `chdir()`. Si aucun argument n'est fourni, la commande lit la variable d'environnement `HOME` via `getenv()` pour ramener l'utilisateur à son répertoire personnel.
- **`pwd` (Print Working Directory) :** Affiche le chemin absolu du répertoire courant en utilisant `getcwd()`.
- **`mkdir` :** Crée un nouveau répertoire avec les permissions standards (0755) via l'appel système `mkdir()`.
- **`exit` / `quit` :** Renvoie un signal d'arrêt à la boucle principale pour terminer proprement le processus du shell.
- **`jobs` :** Parcourt la structure de données interne pour afficher la liste des processus actuellement en cours d'exécution en arrière-plan.

### 3.2. Commandes Externes

Conformément au cahier des charges, toute commande externe (les binaires situés dans le système, comme `ls`, `cat`, ou `sleep`) doit être préfixée par un point d'exclamation `!`. Le shell nettoie ce caractère avant de lancer la commande.

L'exécution s'appuie sur le triptyque classique d'Unix :

1. **`fork()` :** Le shell clone son propre processus pour créer un processus enfant.
2. **`execvp()` :** Dans le processus enfant, cette fonction remplace l'image mémoire du clone par le programme demandé. Elle parcourt automatiquement la variable d'environnement `PATH` pour trouver l'exécutable.
3. **`waitpid()` :** Dans le processus parent, le shell se met en pause et attend la terminaison de son enfant avant de réafficher le prompt.

### 3.3. Exécution Asynchrone et Gestion des Zombies

Si une commande externe se termine par le symbole d'esperluette `&` (ex: `!sleep 20 &`), le shell bascule en mode asynchrone (arrière-plan).

- **Comportement :** Le processus parent ne fait pas appel à `waitpid()` de manière bloquante. Il rend immédiatement la main à l'utilisateur et enregistre le Process ID (PID) et le nom de la commande dans un tableau global (`liste_jobs`).
- **Nettoyage automatique (Signal Handler) :** Pour éviter que les processus en arrière-plan ne deviennent des processus "zombies" (terminés mais occupant toujours des ressources dans la table des processus de l'OS), un gestionnaire de signaux a été implémenté via `sigaction`.

Lorsqu'un enfant meurt, le système d'exploitation envoie le signal `SIGCHLD` au parent. La fonction `gerer_sigchld` intercepte ce signal et utilise `waitpid(-1, &status, WNOHANG)` pour récolter silencieusement le code de retour du zombie et le retirer de la liste des `jobs` actifs.

### 3.4. Redirections des Flux d'Entrée et de Sortie (`<` et `>`)

Le shell permet de dévier les flux standards (entrée clavier et sortie écran) vers des fichiers texte, offrant ainsi la possibilité de sauvegarder les résultats ou d'automatiser des lectures.

L'implémentation repose sur la manipulation des descripteurs de fichiers :

1. Le tableau d'arguments est scanné à la recherche des symboles `<` ou `>`.
2. Le fichier cible est ouvert avec l'appel système `open()`. Pour la sortie (`>`), les drapeaux `O_WRONLY | O_CREAT | O_TRUNC` sont utilisés pour créer ou écraser le fichier avec les permissions `0644`.
3. L'appel système **`dup2()`** est exécuté pour forcer le descripteur de fichier standard (`STDOUT_FILENO` ou `STDIN_FILENO`) à pointer vers le fichier nouvellement ouvert.
4. Le symbole de redirection et le nom du fichier sont remplacés par `NULL` dans le tableau d'arguments pour cacher ces éléments à `execvp()`.

### 3.5. Tubes de Communication Inter-Processus (Pipes `|`)

La fonctionnalité la plus avancée du MiniShell est la capacité de chaîner l'exécution de deux commandes, la sortie standard de la première devenant l'entrée standard de la seconde (ex: `!ls -l | !grep .c`).

Cette fonctionnalité est gérée par une fonction récursive `execute_pipeline` :

1. **Découpage :** Si un caractère `|` est détecté, le tableau d'arguments est scindé en deux commandes distinctes (`cmd1` et `cmd2`).
2. **Création du Tube :** L'appel système `pipe(fd)` crée un canal de communication unidirectionnel en mémoire vive, fournissant deux descripteurs : `fd[0]` pour la lecture et `fd[1]` pour l'écriture.
3. **Double Clonage :** Le shell crée **deux** processus enfants consécutifs.

- Le premier enfant dévie sa sortie standard (`STDOUT`) vers l'entrée du tube (`fd[1]`) via `dup2`, puis exécute `cmd1`.
- Le second enfant dévie son entrée standard (`STDIN`) vers la sortie du tube (`fd[0]`), puis exécute `cmd2`.

4. **Sécurité :** Le processus parent ferme impérativement les deux extrémités du tube pour ne pas bloquer les enfants (règle du End-of-File), puis attend la fin des deux processus via deux appels consécutifs à `waitpid()`.
