#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define BUFFER_SIZE 64 // Maximum size of a message
#define MAX_CLIENTS 10 // Maximum number of clients
#define MAX_STRINGS_TO_RECEIVE MAX_CLIENTS*3 // Maximum number of strings to receive

sem_t* clientFull;
sem_t* clientEmpty;
sem_t* clientMutex;

sem_t* serverMutex;

pthread_t server_thread;
pthread_t client_thread;
pthread_t compute_thread;

typedef struct {
    char* message;
    int remoteID;
} Message;

char toSend[BUFFER_SIZE]; // Buffer for the string to send
char toReceive[MAX_STRINGS_TO_RECEIVE][BUFFER_SIZE]; // Buffer for the strings to receive

int ID = 0; // Local ID
int remoteIDs[MAX_CLIENTS-1] = {0}; // Remote IDs
int remoteClientsNumber = 0; // Number of remote clients

int localClock = 0; // Clock counter

bool waitingForCS = false; // Waiting for critical section
int replyCount = 0; // Number of replies received

// Array of clients waiting for critical section with their clock and ID
typedef struct {
    int clock;
    int ID;
} ClientWaitingForCS;

ClientWaitingForCS clientsWaitingForCSArray[MAX_CLIENTS+1] = { [0 ... MAX_CLIENTS] = {2147483647, MAX_CLIENTS+1} };

// Add a client to the clientsWaitingForCSArray and sort it by clock and then ID
void addClientToCSArray(int clock, int ID) {
    // Add the client to the array
    clientsWaitingForCSArray[MAX_CLIENTS-1].clock = clock;
    clientsWaitingForCSArray[MAX_CLIENTS-1].ID = ID;

    // Sort the array by clock and then ID
    for (int i = 0; i < MAX_CLIENTS; i++) {
        for (int j = 0; j < MAX_CLIENTS - i - 1; j++) {
            if (clientsWaitingForCSArray[j].clock > clientsWaitingForCSArray[j+1].clock) {
                ClientWaitingForCS temp = clientsWaitingForCSArray[j];
                clientsWaitingForCSArray[j] = clientsWaitingForCSArray[j+1];
                clientsWaitingForCSArray[j+1] = temp;
            } 
            else if (clientsWaitingForCSArray[j].clock == clientsWaitingForCSArray[j+1].clock) {
                if (clientsWaitingForCSArray[j].ID > clientsWaitingForCSArray[j+1].ID) {
                    ClientWaitingForCS temp = clientsWaitingForCSArray[j];
                    clientsWaitingForCSArray[j] = clientsWaitingForCSArray[j+1];
                    clientsWaitingForCSArray[j+1] = temp;
                }
            }
        }
    }    
}

// Remove a client from the clientsWaitingForCSArray
void removeClientFromCSArray(int ID) {
    // Remove the client from the array
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clientsWaitingForCSArray[i].ID == ID) {
            for (int j = i; j < MAX_CLIENTS - i - 1; j++) {
                clientsWaitingForCSArray[j] = clientsWaitingForCSArray[j+1];
            }
            break;
        }
    }
}

// Create named semaphores
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

    snprintf(buffer, BUFFER_SIZE, "/serverMutex%d", ID);
    sem_unlink(buffer);
    if ((serverMutex = sem_open(buffer, O_CREAT, 0644, 1)) == SEM_FAILED) {
        perror("sem_open");
        exit(EXIT_FAILURE);
    }
}

// Destroy named semaphores
void destroy_semaphores()
{
    char buffer[BUFFER_SIZE];

    snprintf(buffer, BUFFER_SIZE, "/clientFull%d", ID);
    sem_unlink(buffer);
    sem_close(clientFull);

    snprintf(buffer, BUFFER_SIZE, "/clientEmpty%d", ID);
    sem_unlink(buffer);
    sem_close(clientEmpty);

    snprintf(buffer, BUFFER_SIZE, "/clientMutex%d", ID);
    sem_unlink(buffer);
    sem_close(clientMutex);

    snprintf(buffer, BUFFER_SIZE, "/serverMutex%d", ID);
    sem_unlink(buffer);
    sem_close(serverMutex);
}

// Clean everything and exit
void cleanup(){
    // Cleanup
    printf("\nCleaning up...\n");

    // Stop threads
    pthread_cancel(server_thread);
    pthread_cancel(client_thread);
    pthread_cancel(compute_thread);

    // Destroy named semaphores
    destroy_semaphores();

    // Delete UNIX socket
    unlink("/tmp/SocketCS");
    char buffer[BUFFER_SIZE];
    snprintf(buffer, BUFFER_SIZE, "/tmp/Socket%d", ID);
    unlink(buffer);

    printf("Done!\n");
    exit(0);
}

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

// Receive a message from a remote client
void *reception_handler(void *server_socket)
{
    int sock = *(int *)server_socket;
    unsigned int message_size;
    char remote_message[BUFFER_SIZE];

    while ((message_size = recv(sock, remote_message, BUFFER_SIZE, 0)) > 0)
    {
        sem_wait(serverMutex);
        for(int i = 0; i < MAX_STRINGS_TO_RECEIVE; i++)
        {
            if(strcmp(toReceive[i], "") == 0)
            {
                strcpy(toReceive[i], remote_message);
                break;
            }
        }
        sem_post(serverMutex);
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

    close(sock);
    free(server_socket);

    return 0;
}

// Wait for connections from remote client and pass them to a reception_handler thread
void *server_handler()
{
    int server_socket, addr_size, new_socket, *new_sock;
    struct sockaddr_un server, remote;

    check(server_socket = socket(AF_UNIX, SOCK_STREAM, 0), "Impossible de créer le socket");

    server.sun_family = AF_UNIX;
    
    char buffer[BUFFER_SIZE];
    snprintf(buffer, BUFFER_SIZE, "/tmp/Socket%d", ID);
    unlink(buffer);
    strcpy(server.sun_path, buffer);
    
    addr_size = strlen(server.sun_path) + sizeof(server.sun_family) + 1;

    check(bind(server_socket, (struct sockaddr *)&server, addr_size), "Erreur lors du bind du socket");

    check(listen(server_socket, 3), "Erreur lors de l'écoute du socket");

    while ((new_socket = accept(server_socket, (struct sockaddr *)&remote, (socklen_t *)&addr_size)))
    {
        pthread_t reception_thread;
        new_sock = malloc(sizeof(int));
        *new_sock = new_socket;

        check(pthread_create(&reception_thread, NULL, *reception_handler, (void *)new_sock), "Impossible de créer le thread");
    }

    check(new_socket, "Le serveur n'a pas réussit à accepter la connexion");

    close(server_socket);

    return 0;
}

// Send a random message to a random client (Consume messages produced by the compute thread)
void *random_client_handler(){
    while(1){
        sem_wait(clientFull); 
        sem_wait(clientMutex);

        int socket_desc, addr_size;
        struct sockaddr_un server;

        socket_desc = socket(AF_UNIX, SOCK_STREAM, 0);

        if (socket_desc == -1)
        {
            printf("Erreur lors de la création du socket");
        }

        server.sun_family = AF_UNIX;

        int targetClient = rand() % remoteClientsNumber;
        printf("Clock: %d >> Envoie de '%s' vers le processus %d\n\n", localClock, toSend, remoteIDs[targetClient]);

        char buffer[BUFFER_SIZE];
        snprintf(buffer, BUFFER_SIZE, "/tmp/Socket%d", remoteIDs[targetClient]);
        strcpy(server.sun_path, buffer);

        addr_size = strlen(server.sun_path) + sizeof(server.sun_family) + 1;

        if (connect(socket_desc, (struct sockaddr *)&server, addr_size) < 0)
        {
            perror("Erreur de connexion");
        }

        if (send(socket_desc, toSend, strlen(toSend), 0) < 0)
        {
            perror("Erreur d'envoi du message");
        }

        close(socket_desc);

        sem_post(clientMutex);
        sem_post(clientEmpty);
    }

    return 0;
}

// Send a message to a specific client
void *client_handler(void *message)
{
    Message *msg = (Message *)message;

    int socket_desc, addr_size;
    struct sockaddr_un server;

    socket_desc = socket(AF_UNIX, SOCK_STREAM, 0);

    if (socket_desc == -1)
    {
        printf("Erreur lors de la création du socket");
    }

    server.sun_family = AF_UNIX;

    char buffer[BUFFER_SIZE];
    snprintf(buffer, BUFFER_SIZE, "/tmp/Socket%d", msg->remoteID);
    strcpy(server.sun_path, buffer);

    addr_size = strlen(server.sun_path) + sizeof(server.sun_family) + 1;

    if (connect(socket_desc, (struct sockaddr *)&server, addr_size) < 0)
    {
        perror("Erreur de connexion");
    }

    char messageToSend[BUFFER_SIZE];
    snprintf(messageToSend, BUFFER_SIZE, "%d;%d;%s", ID, localClock, msg->message);

    if (send(socket_desc, messageToSend, strlen(messageToSend), 0) < 0)
    {
        perror("Erreur d'envoi du message");
    }

    close(socket_desc);
    free(message);

    return 0;
}

// Send a message to all clients
void *broadcast_handler(void *message)
{
    char *messageToSend = (char *)message;
 
    for(int i = 0; i < remoteClientsNumber; i++)
    {
        Message *msg = malloc(sizeof(Message));
        msg->message = messageToSend;
        msg->remoteID = remoteIDs[i];

        pthread_t client_thread;
        check(pthread_create(&client_thread, NULL, *client_handler, (void *)msg), "Impossible de créer le thread");
        pthread_join(client_thread, NULL);
    }

    return 0;
}

// Critical section
void *CS_handler(){
    printf("Clock: %d >> Entrer en section critique\n\n", localClock);

    // Error if multiple clients try to enter the CS at the same time    
    int server_socket, addr_size;
    struct sockaddr_un server;
    check(server_socket = socket(AF_UNIX, SOCK_STREAM, 0), "Impossible de créer le socket");
    server.sun_family = AF_UNIX;
    strcpy(server.sun_path, "/tmp/SocketCS");
    addr_size = strlen(server.sun_path) + sizeof(server.sun_family) + 1;
    check(bind(server_socket, (struct sockaddr *)&server, addr_size), "Erreur lors du bind du socket");
    check(listen(server_socket, 3), "Erreur lors de l'écoute du socket");
    sleep((rand() % remoteClientsNumber) + 1);
    unlink("/tmp/SocketCS");
    
    printf("Clock: %d >> Sortie de section critique\n\n", localClock);

    pthread_t release_thread;
    pthread_create(&release_thread, NULL, broadcast_handler, "RELEASE");
    pthread_join(release_thread, NULL);

    removeClientFromCSArray(ID);
    waitingForCS = false;

    return 0;
}

// Read messages, reply to requests, make local actions, and send messages to other clients (Produce messages consumed by the random client thread)
void *compute_handler()
{
    // Set seed for random number generator
    srand(time(NULL)+ID);

    // Start computing loop
    while(1){
        // Sleep for a random amount of time in seconds
        sleep((rand() % remoteClientsNumber) + 1);

        //Action 1 (vérification des messages reçus)
        sem_wait(serverMutex);
        for(int i = 0; i < MAX_STRINGS_TO_RECEIVE; i++)
        {
            if(strcmp(toReceive[i], "") != 0)
            {
                printf("Message reçu > %s\n\n", toReceive[i]);

                int senderID = 0;
                int senderClock = 0;
                char *token = strtok(toReceive[i], ";");
                
                for(int j = 0; j < 3; j++)
                {
                    if(j == 0)
                    {
                        senderID = atoi(token);
                    }
                    if(j == 1)
                    {
                        senderClock = atoi(token);
                        if(senderClock > localClock){
                            localClock = senderClock;
                        }
                        localClock++;
                    }
                    if(j == 2)
                    {
                        // Check if message is a request
                        if(strcmp(token, "REQUEST") == 0)
                        {
                            addClientToCSArray(senderClock, senderID);

                            Message *msg = malloc(sizeof(Message));
                            msg->message = "REPLY";
                            msg->remoteID = senderID;

                            // Send reply
                            pthread_t client_thread;
                            check(pthread_create(&client_thread, NULL, *client_handler, (void *)msg), "Impossible de créer le thread");
                            pthread_join(client_thread, NULL);
                        }
                        // Check if message is a reply
                        else if(strcmp(token, "REPLY") == 0)
                        {
                            replyCount++;
                            if(replyCount == remoteClientsNumber && clientsWaitingForCSArray[0].ID == ID)
                            {
                                replyCount = 0;
                                pthread_t CS_thread;
                                check(pthread_create(&CS_thread, NULL, *CS_handler, NULL), "Impossible de créer le thread");
                            }
                        }
                        // Check if message is a release
                        else if(strcmp(token, "RELEASE") == 0)
                        {
                            removeClientFromCSArray(senderID);
                            if(replyCount == remoteClientsNumber && clientsWaitingForCSArray[0].ID == ID)
                            {
                                replyCount = 0;
                                pthread_t CS_thread;
                                check(pthread_create(&CS_thread, NULL, *CS_handler, NULL), "Impossible de créer le thread");
                            }
                        }
                    }
                    token = strtok(NULL, ";");
                }
                memset(toReceive[i], 0, sizeof toReceive[i]);
            }
        }
        sem_post(serverMutex);

        //Action 2 (tirage d’un nombre au hasard qui permet de savoir quelle action réaliser si pas en section critique)
        int random = rand() % 3;
        if (random == 0 && waitingForCS == false)
        {
            localClock++;
            printf("Clock: %d >> Action locale\n\n", localClock);
        }
        else if (random == 1 && waitingForCS == false)
        {
            localClock++;

            //Produce new message
            sem_wait(clientEmpty);
            sem_wait(clientMutex);
            snprintf(toSend, BUFFER_SIZE, "%d;%d;%d", ID, localClock, rand() % 100);
            sem_post(clientMutex);
            sem_post(clientFull);
        }
        else if (random == 2 && waitingForCS == false)
        {
            localClock++;
            waitingForCS = true;

            printf("Clock: %d >> Envoie de Request\n\n", localClock);

            pthread_t request_thread;
            check(pthread_create(&request_thread, NULL, *broadcast_handler, "REQUEST"), "Impossible de créer le thread");
            pthread_join(request_thread, NULL);

            addClientToCSArray(localClock, ID);
        }
    }
}

// Start all threads
void init_threads()
{
    // Start server thread
    check(pthread_create(&server_thread, NULL, *server_handler, NULL), "Impossible de créer le thread serveur");

    // Start client thread
    check(pthread_create(&client_thread, NULL, *random_client_handler, NULL), "Impossible de créer le thread client");

    //Start compute thread
    check(pthread_create(&compute_thread, NULL, *compute_handler, NULL), "Impossible de créer le thread compute");
}

int main(int argc, char *argv[])
{
    // Handle CTRL+C
    signal(SIGINT, cleanup);
    
    // Usage: ./app <ID1> <remoteID2> <remoteID3> ... <remoteIDn>
    if (argc < 3 || argc-1 > MAX_CLIENTS)
    {
        printf("Usage: ./app <ID1> <remoteID2> <remoteID3> ... <remoteID%d>\n", MAX_CLIENTS);
        return 1;
    }

    // Get arguments
    ID = atoi(argv[1]);
    remoteClientsNumber = argc - 2;
    for(int i = 2; i < argc; i++)
    {
        remoteIDs[i-2] = atoi(argv[i]);
    }
    
    // Create semaphores
    init_semaphores();
    
    // Create threads
    init_threads();

    // Wait for compute thread to finish
    pthread_join(compute_thread, NULL);

    return 0;
}