#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>

#define MESSAGE_SIZE 2000

#define N 32

sem_t* full;
sem_t* empty;
sem_t* mutex;

char to_send[N];
char to_receive[N];

sem_t* creerSemaphore (unsigned int _compteur)
{
    sem_t* semaphore = (sem_t*) malloc (sizeof(sem_t)); 
    
    if (sem_init (semaphore, 0, _compteur) == 0) 
    {
        printf("Semaphore créé\n");
    }
    else {
        perror("Echec dans la création du sémaphore ! \n");
    }
    return semaphore;
}

int detruireSemaphore (sem_t* _semaphore)
{
    unsigned int resultat = sem_destroy(_semaphore);
    free (_semaphore); 
    
    return resultat;
}

int ID = 0; // Local ID 
int remoteID = 0; // Remote ID
int clockCounter = 0;

// Display error message and exit
int check(int status, const char *message)
{
    if (status == -1)
    {
        perror(message);
        exit(1);
    }
    return status;
}

void *remote_handler(void *server_socket)
{
    int sock = *(int *)server_socket;
    unsigned int message_size;
    char remote_message[MESSAGE_SIZE];

    while ((message_size = recv(sock, remote_message, MESSAGE_SIZE, 0)) > 0)
    {
        printf("Message reçus: %s\n", remote_message);

        const char OK_MESSAGE[] = "Remote: Message bien reçu";
        write(sock, OK_MESSAGE, strlen(OK_MESSAGE));

        memset(remote_message, 0, sizeof remote_message);
    }

    if (message_size == 0)
    {
        printf("Remote déconnecté\n\n");
        fflush(stdout);
    }
    else if (message_size == -1)
    {
        perror("La réception à échoué");
    }

    free(server_socket);

    return 0;
}

void *server_handler()
{
    int server_socket, addr_size, new_socket, *new_sock;
    struct sockaddr_un server, remote;
    char *message;

    check(server_socket = socket(AF_UNIX, SOCK_STREAM, 0), "Impossible de créer le socket");

    server.sun_family = AF_UNIX;
    
    char buffer[32];
    snprintf(buffer, 32, "/tmp/Socket%d", ID);
    // Remove server socket if it exists
    unlink(buffer);
    strcpy(server.sun_path, buffer);
    
    addr_size = strlen(server.sun_path) + sizeof(server.sun_family) + 1;

    check(bind(server_socket, (struct sockaddr *)&server, addr_size), "Erreur lors du bind du socket");

    check(listen(server_socket, 3), "Erreur lors de l'écoute du socket");

    printf("En attente de connexions entrantes...\n\n");

    while ((new_socket = accept(server_socket, (struct sockaddr *)&remote, (socklen_t *)&addr_size)))
    {
        printf("Connexion entrante...\n");

        message = "Remote: Connexion établie";
        write(new_socket, message, strlen(message));

        pthread_t remote_thread;
        new_sock = malloc(sizeof(int));
        *new_sock = new_socket;

        check(pthread_create(&remote_thread, NULL, *remote_handler, (void *)new_sock), "Impossible de créer le thread");
    }

    check(new_socket, "Le serveur n'a pas réussit à accepter la connexion");

    return 0;
}

void *client_handler(){
    while(1){
        sem_wait(full); 
        sem_wait(mutex);

        int socket_desc, addr_size;
        struct sockaddr_un server;
        char server_reply[MESSAGE_SIZE];

        socket_desc = socket(AF_UNIX, SOCK_STREAM, 0);

        if (socket_desc == -1)
        {
            printf("Erreur lors de la création du socket");
        }

        server.sun_family = AF_UNIX;

        char buffer[32];
        snprintf(buffer, 32, "/tmp/Socket%d", remoteID);
        strcpy(server.sun_path, buffer);

        addr_size = strlen(server.sun_path) + sizeof(server.sun_family) + 1;

        if (connect(socket_desc, (struct sockaddr *)&server, addr_size) < 0)
        {
            perror("Erreur de connexion");
        }

        if (recv(socket_desc, server_reply, sizeof(server_reply), 0) < 0)
        {
            printf("Erreur de réception du message depuis le serveur");
        }
        printf("%s\n",server_reply);
        bzero(server_reply, sizeof(server_reply));

        char message[MESSAGE_SIZE];

        strcpy(message, to_send);

        if (send(socket_desc, message, strlen(message), 0) < 0)
        {
            perror("Erreur d'envoi du message");
        }
        bzero(message, sizeof(message));

        if (recv(socket_desc, server_reply, sizeof(server_reply), 0) < 0)
        {
            printf("Erreur de réception du message depuis le serveur");
        }
        printf("%s\n\n", server_reply);
        bzero(server_reply, sizeof(server_reply));

        close(socket_desc);

        sem_post(mutex);
        sem_post(empty);
    }

    return 0;
}

void *compute_handler()
{
    // Set seed for random number generator
    srand(time(NULL));

    // Start computing loop
    while(1){
        // Sleep for a random amount of time in seconds
        sleep(rand() % 5);

        //Action 1 (vérification des messages reçus)
        //todo

        //Action 2 (tirage d’un nombre au hasard qui permet de savoir quelle action réaliser)
        int random = rand() % 2;
        if (random == 0)
        {
            clockCounter ++;
            printf("Action locale\n\n");
        }
        else
        {
            clockCounter ++;
            //Start client thread
            printf("Envoie d'un message vers un processus remote...\n");
            sem_wait(empty);
            sem_wait(mutex);
            snprintf(to_send, 32, "Bonjour de remote %d, %d", ID, rand() % 100);
            sem_post(mutex);
            sem_post(full);
        }
    }
}

int main(int argc, char *argv[])
{
    // Usage: ./app <id> <remote_id>
    if (argc < 3)
    {
        printf("Usage: ./app <id> <remoteid>\n");
        return 1;
    }
    ID = atoi(argv[1]);
    remoteID = atoi(argv[2]);

    full = creerSemaphore (0);
    empty  = creerSemaphore (N);
    mutex = creerSemaphore (1);
        
    // Start server thread
    pthread_t server_thread;
    check(pthread_create(&server_thread, NULL, *server_handler, NULL), "Impossible de créer le thread serveur");

    // Start client thread
    pthread_t client_thread;
    check(pthread_create(&client_thread, NULL, *client_handler, NULL), "Impossible de créer le thread client");

    //Start compute thread
    pthread_t compute_thread;
    check(pthread_create(&compute_thread, NULL, *compute_handler, NULL), "Impossible de créer le thread compute");

    // Wait for threads to finish
    pthread_join(compute_thread, NULL);
    
    detruireSemaphore (full);
    detruireSemaphore (empty);
    detruireSemaphore (mutex);
    
    return 0;
}