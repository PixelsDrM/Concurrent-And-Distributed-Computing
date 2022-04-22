#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>

#define BUFFER_SIZE 64
#define PIPE_NAME "/tmp/ObserverPipe"

// Create and Read from the ObserverPipe, write content in trace.txt and prints the data to stdout
int main(int argc, char *argv[])
{   
    int fd;
    char buffer[BUFFER_SIZE];
    int n;

    // Create the pipe
    if (mkfifo(PIPE_NAME, 0666) == -1)
    {
        if (errno != EEXIST)
        {
            printf("\nImpossible de créer le tube nommé %s\n", PIPE_NAME);
            exit(1);
        }
    }

    printf("\nL'observeur est lancé...\n\n");

    // Open the pipe
    if ((fd = open(PIPE_NAME, O_RDONLY)) == -1)
    {
        printf("\nImpossible d'ouvir le tube nommé %s\n", PIPE_NAME);
        exit(1);
    }

    // Open the trace file
    FILE *fp = fopen("trace.txt", "a");

    // Read from the pipe
    while (1)
    {
        if ((n = read(fd, buffer, BUFFER_SIZE)) == -1)
        {
            printf("\nImpossible de lire le tube nommé %s\n", PIPE_NAME);
            exit(1);
        }

        if (n == 0)
        {
            printf("\nIl n'y a plus aucune app en cours d'execution ! Fin de l'observation.\n");
            unlink(PIPE_NAME);
            fclose(fp);
            exit(0);
        }

        buffer[n] = '\0';

        // Write pipe content to file trace.txt
        fprintf(fp, "%s", buffer);
        // Print pipe content to stdout
        printf("%s", buffer);
    }

    return 0;
}