#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <fcntl.h>

#define MESSAGE_SIZE 2000
#define BUFFER_SIZE 32

sem_t* clientFull;
sem_t* clientEmpty;
sem_t* clientMutex;

char toSend[BUFFER_SIZE];
char toReceive[BUFFER_SIZE];

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

void init_semaphores()
{
    // mac os require named semaphores
    char buffer[BUFFER_SIZE];

    snprintf(buffer, BUFFER_SIZE, "/clientFull%d", ID);
    sem_unlink(buffer);
    if ((clientFull = sem_open(buffer, O_CREAT, 0644, 0)) == SEM_FAILED) {
        perror("sem_open");
        exit(EXIT_FAILURE);
    }

    snprintf(buffer, BUFFER_SIZE, "/clientEmpty%d", ID);
    sem_unlink(buffer);
    if ((clientEmpty = sem_open(buffer, O_CREAT, 0644, 1)) == SEM_FAILED) {
        perror("sem_open");
        exit(EXIT_FAILURE);
    }

    snprintf(buffer, BUFFER_SIZE, "/clientMutex%d", ID);
    sem_unlink(buffer);
    if ((clientMutex = sem_open(buffer, O_CREAT, 0644, 1)) == SEM_FAILED) {
        perror("sem_open");
        exit(EXIT_FAILURE);
    }
}

void *remote_handler(void *server_socket)
{
    int sock = *(int *)server_socket;
    unsigned int message_size;
    char remote_message[MESSAGE_SIZE];

    while ((message_size = recv(sock, remote_message, MESSAGE_SIZE, 0)) > 0)
    {
        snprintf(toReceive, BUFFER_SIZE, "%s", remote_message);
        memset(remote_message, 0, sizeof remote_message);
    }

    if (message_size == 0)
    {
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
    
    char buffer[BUFFER_SIZE];
    snprintf(buffer, BUFFER_SIZE, "/tmp/Socket%d", ID);
    // Remove server socket if it exists
    unlink(buffer);
    strcpy(server.sun_path, buffer);
    
    addr_size = strlen(server.sun_path) + sizeof(server.sun_family) + 1;

    check(bind(server_socket, (struct sockaddr *)&server, addr_size), "Erreur lors du bind du socket");

    check(listen(server_socket, 3), "Erreur lors de l'écoute du socket");

    while ((new_socket = accept(server_socket, (struct sockaddr *)&remote, (socklen_t *)&addr_size)))
    {
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
        sem_wait(clientFull); 
        sem_wait(clientMutex);

        int socket_desc, addr_size;
        struct sockaddr_un server;
        char server_reply[MESSAGE_SIZE];

        socket_desc = socket(AF_UNIX, SOCK_STREAM, 0);

        if (socket_desc == -1)
        {
            printf("Erreur lors de la création du socket");
        }

        server.sun_family = AF_UNIX;

        char buffer[BUFFER_SIZE];
        snprintf(buffer, BUFFER_SIZE, "/tmp/Socket%d", remoteID);
        strcpy(server.sun_path, buffer);

        addr_size = strlen(server.sun_path) + sizeof(server.sun_family) + 1;

        if (connect(socket_desc, (struct sockaddr *)&server, addr_size) < 0)
        {
            perror("Erreur de connexion");
        }

        char message[MESSAGE_SIZE];

        strcpy(message, toSend);

        if (send(socket_desc, message, strlen(message), 0) < 0)
        {
            perror("Erreur d'envoi du message");
        }
        bzero(message, sizeof(message));

        close(socket_desc);

        sem_post(clientMutex);
        sem_post(clientEmpty);
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
        sleep(1);

        //Action 1 (vérification des messages reçus)
        if(strcmp(toReceive, "") != 0){
            printf("Message reçu: %s\n\n", toReceive);
            bzero(toReceive, sizeof(toReceive));
        }

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
            //Produce new message
            //printf("Envoie d'un message vers un processus remote...\n");
            sem_wait(clientEmpty);
            sem_wait(clientMutex);
            snprintf(toSend, BUFFER_SIZE, "Bonjour de remote %d, %d", ID, rand() % 100);
            sem_post(clientMutex);
            sem_post(clientFull);
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

    // Create semaphores
    init_semaphores();
        
    // Start server thread
    pthread_t server_thread;
    check(pthread_create(&server_thread, NULL, *server_handler, NULL), "Impossible de créer le thread serveur");

    // Start client thread
    pthread_t client_thread;
    check(pthread_create(&client_thread, NULL, *client_handler, NULL), "Impossible de créer le thread client");

    //Start compute thread
    pthread_t compute_thread;
    check(pthread_create(&compute_thread, NULL, *compute_handler, NULL), "Impossible de créer le thread compute");

    // Wait for compute thread to finish
    pthread_join(compute_thread, NULL);
    
    return 0;
}