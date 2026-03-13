#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// Nouvelles bibliothèques indispensables pour ton code :
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>

//-------------------------------------------------------------------------------------------------- limite de commandes ou tampon (fgets)
#define TAILLE_MAX 1024 // Taille maximale d'une ligne de commande


//-------------------------------------------------------------------------------------------------- limite de jobs
#define MAX_JOBS 50

//-------------------------------------------------------------------------------------------------- definition de jobs
typedef struct {
    pid_t pid;
    char nom_commande[TAILLE_MAX];
    int actif; // 1 si le processus tourne, 0 s'il est terminé
} Job;

Job liste_jobs[MAX_JOBS]; // Notre tableau global pour stocker les jobs

//-------------------------------------------------------------------------------------------------- fonction ajouter job
// Fonction pour ajouter un job au tableau
void ajouter_job(pid_t pid, char *nom) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (liste_jobs[i].actif == 0) {
            liste_jobs[i].pid = pid;
            strcpy(liste_jobs[i].nom_commande, nom);
            liste_jobs[i].actif = 1;
            return;
        }
    }
}

//-------------------------------------------------------------------------------------------------- fonction supprimer job
// Fonction pour retirer un job du tableau quand il est terminé
void supprimer_job(pid_t pid) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (liste_jobs[i].actif == 1 && liste_jobs[i].pid == pid) {
            liste_jobs[i].actif = 0; // On libère la place !
            return;
        }
    }
}

//-------------------------------------------------------------------------------------------------- fonction gerer_sigchild
// LE NETTOYEUR (Signal Handler)
void gerer_sigchld(int sig) {
    int status;
    pid_t pid;
    
    // WNOHANG permet de "ramasser" les zombies sans bloquer le shell
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        supprimer_job(pid); // On met à jour notre tableau
    }
}
//-------------------------------------------------------------------------------------------------- 
//                                                                                                   fonction execute commande
//--------------------------------------------------------------------------------------------------
// Fonction qui prend le tableau d'arguments et le nombre d'arguments en paramètres
// Renvoie 1 si le shell doit continuer, 0 si l'utilisateur a tapé "exit"
int executer_commande(char *arguments[], int nb_arguments) {
    
    // 1. Traitement des commandes externes
    if (arguments[0][0] == '!') { 
        
        // 1.1. Gestion de l'arrière-plan obligatoire avec ('&')
        int arriere_plan = 0;

        if (nb_arguments > 1 && strcmp(arguments[nb_arguments - 1], "&") == 0) {
            arriere_plan = 1;
            arguments[nb_arguments - 1] = NULL; // On efface le '&'
        }   
        else if (nb_arguments == 1 && strcmp(arguments[0], "&") == 0) {
            printf("Erreur : La commande ne peut pas être uniquement '&'.\n");
            return 1; // On retourne 1 pour continuer la boucle du shell
        }
        
        // 1.2. Enlèvement du caractère '!'
        arguments[0]++; 

        // 1.3. Création du processus enfant
        pid_t pid = fork();

        if (pid < 0) {
            perror("Erreur fork, impossible de créer un processus enfant");
        } 
        else if (pid == 0) {
            // --- PROCESSUS ENFANT ---
            
            // 1.3.1 Gestion des redirections (< et >)
            int index_fin_arguments = nb_arguments; // Pour savoir où couper le tableau

            for (int j = 0; j < nb_arguments; j++) {
                if (arguments[j] != NULL) {
                    
                    // Si on trouve une redirection de SORTIE (>)
                    if (strcmp(arguments[j], ">") == 0) {
                        // On ouvre le fichier (arguments[j+1] est le nom du fichier)
                        // Droits 0644 : rw-r--r--
                        int fd_out = open(arguments[j+1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
                        if (fd_out < 0) {
                            perror("Erreur d'ouverture du fichier de sortie");
                            exit(EXIT_FAILURE);
                        }
                        dup2(fd_out, STDOUT_FILENO); // On branche la sortie sur le fichier
                        close(fd_out);
                        
                        // On retient l'endroit où couper les arguments
                        if (j < index_fin_arguments) index_fin_arguments = j;
                    }
                    
                    // Si on trouve une redirection d'ENTRÉE (<)
                    else if (strcmp(arguments[j], "<") == 0) {
                        int fd_in = open(arguments[j+1], O_RDONLY); // Lecture seule
                        if (fd_in < 0) {
                            perror("Erreur d'ouverture du fichier d'entrée");
                            exit(EXIT_FAILURE);
                        }
                        dup2(fd_in, STDIN_FILENO); // On branche l'entrée sur le fichier
                        close(fd_in);
                        
                        // On retient l'endroit où couper les arguments
                        if (j < index_fin_arguments) index_fin_arguments = j;
                    }
                }
            }
            
            // On coupe le tableau pour cacher les chevrons et les fichiers à execvp
            arguments[index_fin_arguments] = NULL; 

            // 1.3.2 Exécution de la commande
            if (execvp(arguments[0], arguments) == -1) {
                perror("Erreur d'exécution de la commande externe");
                exit(EXIT_FAILURE); 
            }
        }
        else {
            // --- PROCESSUS PARENT ---
            if (arriere_plan == 0) {
                waitpid(pid, NULL, 0); 
            } else {
                // NOUVEAU : On ajoute le job dans notre tableau !
                ajouter_job(pid, arguments[0]);
                printf("[Processus en arrière-plan lancé avec le PID: %d]\n", pid);
            }
        }
        return 1; // La commande externe est terminée, on continue le shell
    }
    
    // 2. Traitement des commandes internes
    else {
        // 2.1. Condition de sortie : on renvoie 0 pour casser la boucle du main !
        if (strcmp(arguments[0], "exit") == 0 || strcmp(arguments[0], "quit") == 0) {
            return 0; 
        }

        // 2.2. pwd
        else if (strcmp(arguments[0], "pwd") == 0) {
            char cwd[TAILLE_MAX];
            if (getcwd(cwd, sizeof(cwd)) != NULL) {
                printf("%s\n", cwd);
            } else {
                perror("pwd");
            }
        }

        // 2.3. mkdir
        else if (strcmp(arguments[0], "mkdir") == 0) {
            if (arguments[1] == NULL) {
                printf("mkdir: argument manquant\n");
            } else {
                if (mkdir(arguments[1], 0755) != 0) {
                    perror("mkdir");
                }
            }       
        }

        // 2.4. cd
        else if (strcmp(arguments[0], "cd") == 0) {
            if (arguments[1] == NULL) {
                // L'utilisateur a tapé "cd" tout seul : on cherche le dossier HOME
                char *home_dir = getenv("HOME");
                if (home_dir != NULL) {
                    if (chdir(home_dir) != 0) {
                        perror("cd");
                    }
                } else {
                    printf("cd: variable HOME introuvable\n");
                }
            } else {
                // L'utilisateur a tapé "cd dossier" : on va dans le dossier demandé
                if (chdir(arguments[1]) != 0) {
                    perror("cd: commande non executée");
                }
            }
        }

        // 2.5. jobs
        else if (strcmp(arguments[0], "jobs") == 0) {
            int aucun_job = 1;
            for (int i = 0; i < MAX_JOBS; i++) {
                if (liste_jobs[i].actif == 1) {
                    printf("[%d] En cours   %s (PID: %d)\n", i, liste_jobs[i].nom_commande, liste_jobs[i].pid);
                    aucun_job = 0;
                }
            }
            if (aucun_job) {
                printf("Aucun processus en arrière-plan.\n");
            }
        }
        
        else {// la commande n'existe pas !!
            printf("Commande interne non reconnue : %s\n", arguments[0]);
        }
        
        return 1; // On dit au shell de continuer à tourner !
        
    } 
} 
//-------------------------------------------------------------------------------------------------- 
//                                                                                                   fonction en minuscules
//--------------------------------------------------------------------------------------------------
void en_minuscules(char *s) {
    for (int i = 0; s[i] != '\0'; i++) {
        s[i] = tolower((unsigned char)s[i]);
    }
}
//-------------------------------------------------------------------------------------------------- 
//                                                                                                   fonction execute pipline
//--------------------------------------------------------------------------------------------------
int execute_pipeline(char **arguments, int n) {
    int pipe_index = -1;

    // Recherche d'un pipe (|)
    for (int i = 0; i < n; i++) {
        if (strcmp(arguments[i], "|") == 0) { 
            pipe_index = i;
            break;
        }
    }

    // CAS 1 : pas de pipe trouvé
    if (pipe_index == -1) {
        // On renvoie directement le résultat (0 ou 1) au main !
        return executer_commande(arguments, n);
    }

    // CAS 2 : pipe trouvé
    arguments[pipe_index] = NULL; // On coupe le tableau en deux

    char **cmd1 = arguments;
    char **cmd2 = &arguments[pipe_index + 1];

    int fd[2];
    if (pipe(fd) == -1) {
        perror("pipe");
        return 1;
    }

    // --- ENFANT 1 (Gère la commande de gauche) ---
    pid_t pid1 = fork();
    if (pid1 < 0) perror("fork 1");
    if (pid1 == 0) {
        dup2(fd[1], STDOUT_FILENO); // Sa sortie va dans le pipe
        close(fd[0]); // Il ferme le côté lecture
        close(fd[1]);

        executer_commande(cmd1, pipe_index);
        exit(EXIT_SUCCESS);
    }

    // --- ENFANT 2 (Gère la commande de droite) ---
    pid_t pid2 = fork();
    if (pid2 < 0) perror("fork 2");
    if (pid2 == 0) {
        dup2(fd[0], STDIN_FILENO); // Son entrée vient du pipe
        close(fd[1]); // Il ferme le côté écriture
        close(fd[0]);

        execute_pipeline(cmd2, n - pipe_index - 1);
        exit(EXIT_SUCCESS);
    }

    // --- PROCESSUS PARENT ---
    close(fd[0]);
    close(fd[1]);

    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);
    
    // S'il y avait un pipe, le shell doit toujours continuer après
    return 1; 
}


//--------------------------------------------------------------------------------------------------
//                                                                                                     fonction main
//--------------------------------------------------------------------------------------------------
int main(int argc, char *argv[]) {
    struct sigaction sa;
    sa.sa_handler = gerer_sigchld; // On lui donne notre fonction nettoyeur
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    
    // On installe l'action pour le signal SIGCHLD
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("Erreur sigaction");
        exit(1);
    }
    
    char tampon[TAILLE_MAX]; // Buffer pour stocker la ligne de commande entrée par l'utilisateur
    char *commande; // Pointeur pour stocker la ligne de commande lue par fgets
    char *arguments[TAILLE_MAX /4]; // Pointeur pour stocker les arguments de la commande

    // Boucle principale du MiniShell (note: le mini-shell continue de fonctionner jusqu'à ce que l'utilisateur décide de quitter)
    while (1) {

        // 1. Affichage du prompt (ici, on peut personnaliser le prompt comme on le souhaite de facon dynamique)
        char cwd_prompt[TAILLE_MAX];
        if (getcwd(cwd_prompt, sizeof(cwd_prompt)) != NULL) {
            // Affiche ton nom et le dossier actuel !
            printf("%s@Mini_Shell:[%s]> ", argv[0], cwd_prompt);
        } 
        
        else {
            // Au cas où getcwd échoue, on garde un prompt par défaut
            printf("%s@Mini_Shell> ", argv[0]);
        }
        fflush(stdout); // Affichage du prompt immédiatement avant d'attendre l'entrée de l'utilisateur

        // 2. Lecture de la ligne de commande entrée par l'utilisateur (avec fgets pour éviter les débordements de tampon)
        commande = fgets(tampon, TAILLE_MAX, stdin);

        if (commande == NULL) {
            printf("\n"); // Affiche une nouvelle ligne quand EOF est atteint (par exemple, si l'utilisateur appuie sur Ctrl+D)
            break; // Sort de la boucle principale pour terminer le programme
        }

        // 3. verification si la ligne est coupee ou non (quand elle depasse la taille maximale du tampon)
        if (strchr(tampon, '\n') == NULL) {
            int c;
            while ((c = getchar()) != '\n' && c != EOF); // Vider le tampon d'entrée standard (stdin) jusqu'à la fin de la ligne ou jusqu'à EOF
            printf("Erreur : La commande est trop longue. Veuillez entrer une commande de moins de %d caractères.\n", TAILLE_MAX - 1);
            continue; // Recommencer la boucle pour demander une nouvelle commande
        }

        // 4. Nettoyage de la chaîne (remplacement des sauts de ligne '\n' par le caractère de fin de chaîne '\0')
        tampon[strcspn(tampon, "\n")] = '\0';

        if (strlen(tampon) == 0) continue; // Si aucune commande n'est entrée, on ignore et on recommence

        

        // 5. Decoupage de la ligne de commande lue (du tampon) en liste de jetons (ou arguments) (en utilisant strtok pour séparer les mots)
        int i = 0;
        char *jeton = strtok(tampon, " "); // separation du premier mot de la commande du reste des arguments

        if (jeton == NULL) continue; // ver

        en_minuscules(jeton); // Convertion de jeton en minuscules pour une comparaison insensible à la casse seulement pour le premier jeton (la commande elle-même), les arguments restent inchangés

        while (jeton != NULL && i < (TAILLE_MAX / 4) - 1) { // Limitation du nombre d'arguments à (taille_Max / 4) pour éviter les débordements

            arguments[i++] = jeton; // Stocke chaque argument dans le tableau d'arguments (les arguments sont stokes a partir de l'index 0 et incrementé à chaque itération a la fin de la boucle)
            jeton = strtok(NULL, " "); // Continue à séparer les arguments jusqu'à ce qu'il n'y en ait plus
        }

        arguments[i] = NULL; // Terminer le tableau d'arguments par un pointeur NULL pour indiquer la fin des arguments

    
        // recuperation de l'etat dexecution et On lance la fonction qui gère les pipes (et qui appellera executer_commande si besoin)
        int statut = execute_pipeline(arguments, i);

        if (statut == 0) {
            break; // Si on a tapé exit, on casse la boucle !
        }    
    }

    printf("Au revoir [%s] !\n", argv[0]);
    return 0;
}

