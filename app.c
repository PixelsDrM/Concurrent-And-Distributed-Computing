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

struct Message {
    char* message;
    int remoteID;
};

typedef struct {
    size_t head;
    size_t tail;
    size_t size;
    int* data;
} queue_t;

char toSend[BUFFER_SIZE]; // Buffer for the string to send
char toReceive[MAX_STRINGS_TO_RECEIVE][BUFFER_SIZE]; // Buffer for the strings to receive

int ID = 0; // Local ID
int remoteIDs[MAX_CLIENTS-1] = {0}; // Remote IDs
int remoteClientsNumber = 0; // Number of remote clients

int localClock = 0; // Clock counter

bool waitingForCS = false; // Waiting for critical section
int replyCount = 0; // Number of replies received
queue_t clientsWaitingForCS; // Clients waiting for critical section

// Initialize a queue with a given size 
int queue_init(queue_t* q, size_t size) {
    q->data = (int*) malloc(size * sizeof(int));
    if (!q->data) {
        return -1;
    }
    q->size = size;
    q->head = q->tail = 0;
    return 0;
}

// Add an element at the end of the queue
int queue_insert(queue_t *queue, int handle) {
    if (((queue->head + 1) % queue->size) == queue->tail) {
        return -1;
    }
    queue->data[queue->head] = handle;
    queue->head = (queue->head + 1) % queue->size;
    return 0;
}

// Remove an element from the queue by its value
int queue_remove(queue_t *queue, int handle) {
    if (queue->tail == queue->head) {
        return -1;
    }
    int i = queue->tail;
    while (i != queue->head) {
        if (queue->data[i] == handle) {
            queue->data[i] = 0;
            queue->tail = (queue->tail + 1) % queue->size;
            return 0;
        }
        i = (i + 1) % queue->size;
    }
    return -1;
}

// Get the first element of the queue without removing it
int queue_peek(queue_t *queue) {
    if (queue->tail == queue->head) {
        return 0;
    }
    return queue->data[queue->tail];
} 

// Destroy the queue
int queue_destroy(queue_t *queue) {
    free(queue->data);
    return 0;
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

    // Destroy queue
    queue_destroy(&clientsWaitingForCS);

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
        printf("Clock: %d >> Envoie de '%s' vers le processus %d...\n\n", localClock, toSend, remoteIDs[targetClient]);

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
    struct Message *msg = (struct Message *)message;

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
        struct Message *msg = malloc(sizeof(struct Message));
        msg->message = messageToSend;
        msg->remoteID = remoteIDs[i];

        pthread_t client_thread;
        check(pthread_create(&client_thread, NULL, *client_handler, (void *)msg), "Impossible de créer le thread");
    }

    return 0;
}

// Critical section
void *CS_handler(){

    printf("Clock: %d >> Entrer en section critique\n\n", localClock);

    /*
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
    */

    sleep((rand() % remoteClientsNumber) + 1);
    printf("Clock: %d >> Sortie de section critique\n\n", localClock);

    pthread_t release_thread;
    pthread_create(&release_thread, NULL, broadcast_handler, "RELEASE");
    pthread_join(release_thread, NULL);

    queue_remove(&clientsWaitingForCS, ID);
    waitingForCS = false;
    replyCount = 0;

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
                printf("Clock: %d >> Message reçu > %s\n\n", localClock, toReceive[i]);

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
                    }
                    if(j == 2)
                    {
                        // Check if message is a request
                        if(strcmp(token, "REQUEST") == 0)
                        {
                            queue_insert(&clientsWaitingForCS, senderID);

                            struct Message *msg = malloc(sizeof(struct Message));
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
                            if(replyCount == remoteClientsNumber && queue_peek(&clientsWaitingForCS) == ID)
                            {
                                pthread_t CS_thread;
                                check(pthread_create(&CS_thread, NULL, *CS_handler, NULL), "Impossible de créer le thread");
                            }
                        }
                        // Check if message is a release
                        else if(strcmp(token, "RELEASE") == 0)
                        {
                            queue_remove(&clientsWaitingForCS, senderID);
                            if(replyCount == remoteClientsNumber && queue_peek(&clientsWaitingForCS) == ID)
                            {
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

            queue_insert(&clientsWaitingForCS, ID);
        }
    }
}

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
    
    // Create queue
    queue_init(&clientsWaitingForCS, MAX_CLIENTS+1);

    // Create semaphores
    init_semaphores();
    
    // Create threads
    init_threads();

    // Wait for compute thread to finish
    pthread_join(compute_thread, NULL);

    return 0;
}