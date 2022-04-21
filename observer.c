#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>

#define BUFFER_SIZE 64
#define PIPE_NAME "/tmp/ObserverPipe"

void cleanup() {
    printf("Cleaning up...\n");
    unlink(PIPE_NAME);
    printf("Cleanup done.\n");
    exit(0);
}

int main(int argc, char *argv[])
{
    // Handle SIGINT
    signal(SIGINT, cleanup);
    
    int fd;
    char buffer[BUFFER_SIZE];
    int n;

    if (mkfifo(PIPE_NAME, 0666) == -1)
    {
        if (errno != EEXIST)
        {
            printf("\nCan't create named pipe %s\n", PIPE_NAME);
            exit(1);
        }
    }

    printf("\nObserver is running...\n\n");

    if ((fd = open(PIPE_NAME, O_RDONLY)) == -1)
    {
        printf("\nCan't open named pipe %s\n", PIPE_NAME);
        exit(1);
    }

    while (1)
    {
        if ((n = read(fd, buffer, BUFFER_SIZE)) == -1)
        {
            printf("\nCan't read from named pipe %s\n", PIPE_NAME);
            exit(1);
        }

        if (n == 0)
        {
            printf("\nEOF from named pipe %s\n", PIPE_NAME);
            unlink(PIPE_NAME);
            exit(0);
        }

        buffer[n] = '\0';
        // Write pipe content to file trace.txt
        FILE *fp = fopen("trace.txt", "a");
        fprintf(fp, "%s", buffer);
        fclose(fp);

        printf("%s", buffer);
    }

    return 0;
}