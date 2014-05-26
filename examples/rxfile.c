#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>

char *file_name;
char *final_name;
char *socket_path;
bool verbose = false;

int wiSocket;
FILE *fd;

#define RETRIES 100
#define TIMEOUT_MSEC 25
#define HEADER_SIZE 4
#define FRAGMENT_SIZE 1482
#define PACKET_SIZE HEADER_SIZE+FRAGMENT_SIZE
char sBuffer[PACKET_SIZE];
char aBuffer[HEADER_SIZE];
unsigned long fragment;
unsigned long ack;

void fail();

int main(int argc, char **argv) {
    extern char *optarg;
    extern int optind;

    int o, err = 0;
    while ((o = getopt(argc, argv, "v::")) != -1) {
        switch (o) {
            case 'v':
                verbose = true;
                break;

            case '?':
                err = 1;
                break;
        }
    }

    if ((optind+1) > argc || err) {
        if ((optind+2) > argc) {
            printf("No file specified\n");
        }
        if ((optind+1) > argc) {
            printf("No WiPacket socket specified\n");
        }
        if (err) {
            //printf("Invalid option specified\n");
        }
        printf("Receives a file using WiPacket\n");
        printf("Usage: rxfile [-v] socket file\n");
        exit(1);
    } else {
        socket_path = argv[argc-2];
        final_name = argv[argc-1];
        size_t size = strlen(final_name)+10;
        char tmp[size];
        snprintf(tmp, sizeof(tmp), "%s.transfer", final_name);
        file_name = malloc(size);
        memcpy(file_name, tmp, size);
    }

    fd = fopen(file_name, "w+");
    if (fd == NULL) {
        printf("Could not open output file\n");
        exit(1);
    }

    struct sockaddr_un remote;
    wiSocket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (wiSocket == -1) {
        printf("Could not create socket\n");
        fail();
    }

    remote.sun_family = AF_UNIX;
    strcpy(remote.sun_path, socket_path);
    int len = strlen(remote.sun_path) + sizeof(remote.sun_family);
    if (connect(wiSocket, (struct sockaddr*)&remote, len) == -1) {
        perror("connect");
        printf("Could not connect to WiPacket socket\n");
        fail();
    }

    if (verbose) printf("Connected to WiPacket\n");
    printf("Waiting for file...\n");

    fd_set wiSocketSet;
    struct timeval timeout;

    fragment = 0;
    ack = 0;
    int retries = 0;
    int sRead;
    bool done = false;
    while (!done && retries < RETRIES) {
        retries++;

        FD_ZERO(&wiSocketSet);
        FD_SET(wiSocket, &wiSocketSet);
        timeout.tv_sec = 0;
        timeout.tv_usec = TIMEOUT_MSEC*1000;
        
        int socketReady = select(wiSocket+1, &wiSocketSet, NULL, NULL, &timeout);
        if (socketReady < 0) {
            printf("Error reading from WiPacket while waiting for data\n");
            fail();
        }

        if (socketReady == 1) {
            sRead = recv(wiSocket, sBuffer, PACKET_SIZE, 0);
            if (sRead != 0) {
                memcpy(&fragment, sBuffer, HEADER_SIZE);

                if (fragment == ack+1) {
                    if (verbose) printf("Got fragment %lu (%d bytes)\n", fragment, sRead-HEADER_SIZE);
                    ack++;
                    retries = 0;
                    if ((fwrite(sBuffer+HEADER_SIZE, 1, sRead - HEADER_SIZE, fd)) != sRead - HEADER_SIZE) {
                        printf("Error while writing received data to file\n");
                        fail();
                    }
                }

                if (fragment == ack) {
                    memcpy(aBuffer, &ack, HEADER_SIZE);
                    if (send(wiSocket, aBuffer, HEADER_SIZE, 0) < 0) {
                        printf("Error writing to WiPacket socket while sending ACK\n");
                        fail();
                    }
                }

                if (ack > 0 && fragment == 0) {
                    printf("File received\n");
                    ack = 0;
                    memcpy(aBuffer, &ack, HEADER_SIZE);
                    if (send(wiSocket, aBuffer, HEADER_SIZE, 0) < 0) {
                        printf("Error writing to WiPacket socket while sending ACK\n");
                        fail();
                    }
                    done = true;
                }
            }
        }
    }

    if (!done) {
        printf("Transfer failed due to timeout\n");
    }

    fclose(fd);

    unlink(final_name);
    rename(file_name, final_name);


    return 0;
}

void fail() {
    fclose(fd);
    unlink(file_name);
    exit(1);
}