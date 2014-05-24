#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <unistd.h>

#include <net/ethernet.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <linux/if_arp.h>

#include "if_helper.h"

// Forward declarations
void init();
void registerSockets();
void configureInterface();
void interfaceInfo(int sd, struct ifreq *req);
void prepeareBroadcastHeader();
bool transmit(char *payload, size_t len);
void cleanup();
void sigHandler(int s);

// Domain socket path & descriptor
#define SOCKET_PATH "./wipacket.sock"
int domainSocket;

// Ethernet protocol ID
#define PROTOCOL_IDENTIFIER 0x9f77

// Max payload length
#define PAYLOAD_LENGTH 4096

// ESSID and frequency for network
#define ESSID "wipkt"
#define FREQUENCY 2412

// The ethernet broadcast address
const unsigned char broadcast_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

// Socket descriptor for network access
static int netSocket;
// Interface name
static char *if_name;
static size_t if_name_len;
// Interface index of selected interface
static int if_index;
// Hardware (MAC) address of selected interface
static unsigned char hw_addr[ETHER_ADDR_LEN];
// A sockaddr_ll struct for creating frame headers
static struct sockaddr_ll broadcast = {0};

// Whether to keep running
bool run = true;

int main(int argc, char **argv) {
    // Get interface name from command line argument
    if (argc > 1) {
        if_name = argv[1];
        if_name_len = strlen(if_name);
    } else {
        printf("No interface specified\nUsage: wipacket [interface]\n");
        exit(1);
    }

    init();
    configureInterface();
    registerSockets();

    char incomingBuffer[PAYLOAD_LENGTH];
    while (run) {
        int connection;
        struct sockaddr_un remote;
        int structLen = sizeof(remote);
        printf("Waiting for connection...\n");
        connection = accept(domainSocket, (struct sockaddr*)&remote, &structLen);
        if (connection == -1) {
            if (run) printf("Error while accepting client connection\n");
            cleanup();
            exit(1);
        } else {
            printf("Accepted client connection\n");
            int readLength = 0;
            bool connectionOk = true;
            while (connectionOk) {
                readLength = recv(connection, incomingBuffer, PAYLOAD_LENGTH, 0);
                if (readLength < 0) {
                    printf("Error while reading from client\n");
                    connectionOk = false;
                } else {
                    if (readLength > 0) {
                        transmit(incomingBuffer, readLength);
                    }
                    if (readLength == 0) {
                        printf("Client disconnect\n");
                        connectionOk = false;
                    }
                }
            }
        }

    }

    cleanup();
    exit(0);
}

void init() {
    // Register signal handler
    struct sigaction handler;
    handler.sa_handler = sigHandler;
    sigemptyset(&handler.sa_mask);
    handler.sa_flags = 0;
    sigaction(SIGINT, &handler, NULL);

    // Open raw AF_PACKET socket
    netSocket = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));

    if (netSocket == -1) {
        printf("Error creating network socket\n");
        exit(1);
    }

    // Get info for selected interface
    struct ifreq interfaceRequest;
    size_t max_if_name_len = sizeof(interfaceRequest.ifr_name);
    if (if_name_len > max_if_name_len) {
        printf("Interface name is too long (max length is %lu)\n", max_if_name_len);
        exit(1);
    }

    // Copy name into interface request
    memcpy(interfaceRequest.ifr_name, if_name, if_name_len);
    interfaceRequest.ifr_name[if_name_len] = 0;

    // Get interface info
    struct ifreq *requestPointer = &interfaceRequest;
    interfaceInfo(netSocket, requestPointer);
    
    printf("Selecting interface: %s (interface index %d)\n", if_name, if_index);
    printf("Current physical address : %.2x:%.2x:%.2x:%.2x:%.2x:%.2x\n" , hw_addr[0], hw_addr[1], hw_addr[2], hw_addr[3], hw_addr[4], hw_addr[5]);

    prepeareBroadcastHeader();
}

void registerSockets() {
    struct sockaddr_un local;
    int len;

    domainSocket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (domainSocket == -1) {
        printf("Could not create domain socket\n");
        cleanup();
        exit(1);
    }



    local.sun_family = AF_UNIX;
    strcpy(local.sun_path, SOCKET_PATH);
    unlink(local.sun_path);
    len = strlen(local.sun_path) + sizeof(local.sun_family);
    
    if (bind(domainSocket, (struct sockaddr *)&local, len) == -1) {
        printf("Could not bind to domain socket\n");
        cleanup();
        exit(1);
    }

    if (listen(domainSocket, 1) == -1) {
        printf("Unable to start listening on domain socket\n");
        cleanup();
        exit(1);
    }
}

bool transmit(char *payload, size_t len) {
    size_t frameLen = len+2*ETHER_ADDR_LEN+2;
    char *buffer = malloc(frameLen);

    memcpy(buffer, broadcast_addr, ETHER_ADDR_LEN);
    memcpy(buffer+ETHER_ADDR_LEN, &hw_addr, ETHER_ADDR_LEN);
    memcpy(buffer+ETHER_ADDR_LEN*2+2, payload, len);
    memcpy(buffer+ETHER_ADDR_LEN*2, &broadcast.sll_protocol, 2);

    int result = sendto(netSocket, buffer, frameLen, 0, (struct sockaddr*)&broadcast, sizeof(broadcast));

    free(buffer);
    
    if (result == -1) {
        return false;
    } else {
        return true;
    }
}

void interfaceInfo(int sd, struct ifreq *req) {
    // Query for interface index
    if (ioctl(sd, SIOCGIFINDEX, req)==-1) {
        printf("%s\n", strerror(errno));
        exit(1);
    }
    if_index = req->ifr_ifindex;

    // Query for hardware address
    if (ioctl(sd, SIOCGIFHWADDR, req)==-1) {
        printf("%s\n", strerror(errno));
        exit(1);
    }
    unsigned char *mac = (unsigned char *)req->ifr_hwaddr.sa_data;
    memcpy(hw_addr, mac, ETHER_ADDR_LEN);
}

void prepeareBroadcastHeader() {
    broadcast.sll_family = AF_PACKET;
    broadcast.sll_ifindex = if_index;
    broadcast.sll_halen = ETHER_ADDR_LEN;
    broadcast.sll_protocol = htons(PROTOCOL_IDENTIFIER);
    memcpy(broadcast.sll_addr, broadcast_addr, ETHER_ADDR_LEN);
}

void configureInterface() {
    int status;
    printf("Configuring interface %s...\n", if_name);
    if_down(if_name);
    if_enable_ibss(if_name);
    if_up(if_name);
    if_join_ibss(if_name, ESSID, FREQUENCY);
}

void cleanup() {
    close(netSocket);
    close(domainSocket);
    unlink(SOCKET_PATH);
}

void sigHandler(int s) {
    if (s==2) {
        printf("\nCaught SIGINT, exiting...\n");
        run = false;
    }
}