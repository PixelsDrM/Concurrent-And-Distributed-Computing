#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>

#define MESSAGE_SIZE 2000

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

    snprintf(buffer, 32, "Bonjour de remote %d", ID);
    strcpy(message, buffer);

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

    return 0;
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

    // Remove server socket if it exists
    char buffer[32];
    snprintf(buffer, 32, "/tmp/Socket%d", ID);
    unlink(buffer);

    // Start server thread
    pthread_t server_thread;
    check(pthread_create(&server_thread, NULL, *server_handler, NULL), "Impossible de créer le thread serveur");

    // Set seed for random number generator
    srand(time(NULL));

    // Start computing loop
    while(1){
        sleep(2);

        int random = rand() % 2;
        if (random == 0)
        {
            printf("Action locale\n\n");
            clockCounter ++;
            printf("Clock : %i\n\n", clockCounter);
        }
        else
        {
            //Start client thread
            printf("Envoie d'un message vers un processus remote...\n");
            pthread_t client_thread;
            check(pthread_create(&client_thread, NULL, *client_handler, NULL), "Impossible de créer le thread client");
        }
    }
    
    return 0;
}