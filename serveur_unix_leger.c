#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>

#define MESSAGE_SIZE 2000
#define FILENAME "/tmp/socketLocale.txt"

void *connection_handler(void *server_socket)
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

int check(int socket_state, const char *message)
{
    if (socket_state == -1)
    {
        perror(message);
        exit(1);
    }
    return socket_state;
}

int main(int argc, char *argv[])
{
    int server_socket, addr_size, new_socket, *new_sock;
    struct sockaddr_un server, client;
    char *message;

    check(server_socket = socket(AF_UNIX, SOCK_STREAM, 0), "Impossible de creer le socket");

    server.sun_family = AF_UNIX;
    strcpy(server.sun_path, FILENAME);
    addr_size = strlen(server.sun_path) + sizeof(server.sun_family);

    check(bind(server_socket, (struct sockaddr *)&server, addr_size), "Erreur lors du bind du socket");
    printf("Bind effectue\n");

    check(listen(server_socket, 3), "Erreur lors de l'ecoute du socket");

    printf("En attente de connexions entrantes...\n");

    while ((new_socket = accept(server_socket, (struct sockaddr *)&client, (socklen_t *)&addr_size)))
    {
        printf("Connexion établie\n");

        message = "Serveur: Connexion etablie\n";
        write(new_socket, message, strlen(message));

        pthread_t sniffer_thread;
        new_sock = malloc(sizeof(int));
        *new_sock = new_socket;

        check(pthread_create(&sniffer_thread, NULL, *connection_handler, (void *)new_sock), "Impossible de creer le thread");

        printf("Thread assigne\n");
    }

    check(new_socket, "Le serveur n'a pas reussit a accepter la connexion");

    return 0;
}