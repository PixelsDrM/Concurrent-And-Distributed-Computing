#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>

#define MESSAGE_SIZE 2000

int ID = 0;

int check(int status, const char *message)
{
    if (status == -1)
    {
        perror(message);
        exit(1);
    }
    return status;
}

void *client_handler(void *server_socket)
{
    int sock = *(int *)server_socket;
    unsigned int message_size;
    char client_message[MESSAGE_SIZE];

    while ((message_size = recv(sock, client_message, MESSAGE_SIZE, 0)) > 0)
    {
        printf("Client envoi: %s", client_message);

        const char OK_MESSAGE[] = "Serveur: message reçu\n";
        write(sock, OK_MESSAGE, strlen(OK_MESSAGE));

        memset(client_message, 0, sizeof client_message);
    }

    if (message_size == 0)
    {
        puts("Client déconnecté");
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
    struct sockaddr_un server, client;
    char *message;

    check(server_socket = socket(AF_UNIX, SOCK_STREAM, 0), "Impossible de créer le socket");

    server.sun_family = AF_UNIX;
    if(ID == 1){
        strcpy(server.sun_path, "/tmp/Socket");
    }
    else if (ID == 2){
        strcpy(server.sun_path, "/tmp/SocketSocket");
    }
    addr_size = strlen(server.sun_path) + sizeof(server.sun_family);

    printf("%s\n", server.sun_path);

    check(bind(server_socket, (struct sockaddr *)&server, addr_size), "Erreur lors du bind du socket");
    printf("Bind effectue\n");

    check(listen(server_socket, 3), "Erreur lors de l'ecoute du socket");

    printf("En attente de connexions entrantes...\n");

    while ((new_socket = accept(server_socket, (struct sockaddr *)&client, (socklen_t *)&addr_size)))
    {
        printf("Connexion établie\n");

        message = "Serveur: Connexion établie\n";
        write(new_socket, message, strlen(message));

        pthread_t client_thread;
        new_sock = malloc(sizeof(int));
        *new_sock = new_socket;

        check(pthread_create(&client_thread, NULL, *client_handler, (void *)new_sock), "Impossible de créer le thread");

        printf("Thread assigne\n");
    }

    check(new_socket, "Le serveur n'a pas réussit à accepter la connexion");

    return 0;
}

void *send_handler(){
    int socket_desc, addr_size;
    struct sockaddr_un server;
    char server_reply[MESSAGE_SIZE];

    socket_desc = socket(AF_UNIX, SOCK_STREAM, 0);

    if (socket_desc == -1)
    {
        printf("Erreur lors de la création du socket");
    }

    server.sun_family = AF_UNIX;
    if(ID == 1){
        strcpy(server.sun_path, "/tmp/SocketSocket");
    }
    else if (ID == 2){
        strcpy(server.sun_path, "/tmp/Socket");
    }
    addr_size = strlen(server.sun_path) + sizeof(server.sun_family);

    if (connect(socket_desc, (struct sockaddr *)&server, addr_size) < 0)
    {
        perror("Erreur de connexion");
    }

    if (recv(socket_desc, server_reply, sizeof(server_reply), 0) < 0)
    {
        puts("Erreur de réception du message depuis le serveur");
    }
    puts(server_reply);
    bzero(server_reply, sizeof(server_reply));

    char message[MESSAGE_SIZE];

    if(ID == 1){
        strcpy(message, "Client 1: Bonjour\n");
    }
    else if (ID == 2){
        strcpy(message, "Client 2: Bonjour\n");
    }

    if (send(socket_desc, message, strlen(message), 0) < 0)
    {
        perror("Erreur d'envoi du message");
    }
    bzero(message, sizeof(message));

    if (recv(socket_desc, server_reply, sizeof(server_reply), 0) < 0)
    {
        puts("Erreur de réception du message depuis le serveur");
    }
    puts(server_reply);
    bzero(server_reply, sizeof(server_reply));

    close(socket_desc);

    return 0;
}

int main(int argc, char *argv[])
{
    if(argc != 2){
        printf("Usage: %s <ID>\n", argv[0]);
        return 1;
    }
    ID = atoi(argv[1]);

    pthread_t server_thread;
    check(pthread_create(&server_thread, NULL, *server_handler, NULL), "Impossible de créer le thread serveur");

    srand(time(NULL));
    while(1){
        sleep(2);

        int random = rand() % 2;
        if (random == 0)
        {
            printf("Action locale\n");
        }
        else
        {
            printf("Envoie d'un message vers un client aléatoire\n");
            pthread_t send_thread;
            check(pthread_create(&send_thread, NULL, *send_handler, NULL), "Impossible de créer le thread d'envoie");
        }
    }
    
    return 0;
}