#include <liblog/log.h>
#include <stdbool.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


bool QUIT = false;

const char *SEED_NODE = "peercoin.peercoin-library.org";
const char *SEED_PORT = "9901";

pthread_mutex_t SEED_MUTEX;
int SEED_HEIGHT = 0;
char *SEED_VERSION = NULL;

typedef struct coin_node {
    struct sockaddr address;
    int last_contact;
    struct coin_node* next;
} coin_node_s;

pthread_mutex_t GOOD_MUTEX;
coin_node_s *GOOD_NODES = NULL;

pthread_mutex_t CHECK_MUTEX;
coin_node_s *CHECK_NODES = NULL;


int connect_to_seed_node() {
    log_trace("connect_to_seed_node()");

    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *result;
    int error_number = getaddrinfo(SEED_NODE, SEED_PORT, &hints, &result);
    if (error_number != 0) {
        log_error("getaddrinfo(): %s\n", gai_strerror(error_number));
        return -1;
    }

    int socket_number;
    struct addrinfo *info;
    for (info = result; info != NULL; info = info->ai_next) {
        socket_number = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
        if (socket_number == -1) {
            continue;
        }

        log_debug("Trying IP address: %s", inet_ntoa(((struct sockaddr_in *) info->ai_addr)->sin_addr));
        if (connect(socket_number, info->ai_addr, info->ai_addrlen) == -1) {
            close(socket_number);
            continue;
        }

        break; // Connection established!
    }

    if (info == NULL) {
        log_error("Could not connect.\n");
        freeaddrinfo(result);
        return -1;
    }

    log_debug("Connected to IP address: %s", inet_ntoa(((struct sockaddr_in *) info->ai_addr)->sin_addr));
    freeaddrinfo(result);
    return socket_number;
}


void *seed_thread(void *vin) {
    log_trace("Entering seed_node_thread()");

    while(!QUIT) {
        // Need to make a tcp connection
        int tcp_socket = connect_to_seed_node();
        log_debug("Connected to SEED_NODE on socket: %d", tcp_socket);

        // Maybe hints on reading / writing to the socket! https://www.geeksforgeeks.org/socket-programming-cc/

        // Need to send some messages

        // Need to get block chain height, user agent, version, etc.

        // Use this info to judge other nodes :)

        // Need to get peer addresses

        // Use this to populate a queue (linked list?)
        close(tcp_socket);

        QUIT = true;
    }

    log_trace("Exiting seed_node_thread()");
    return NULL;
}


void *dns_thread() {
    log_trace("Entering dns_server_thread()");

    while(!QUIT) {
        // Need to listen for incoming UDP requests and respond appropriately...
        log_info("I AM DNS THREAD LOL");
    }

    log_trace("Exiting dns_server_thread()");
    return NULL;
}


// Two ideas, then move on to C++
// One: capture ctrl-c and shutdown gracefully
// Two: get the handshake to work?


int main (int argc, char *argv[])
{
    log_set_level(LOG_TRACE);

    pthread_t seed, dns;
    pthread_create(&seed, NULL, &seed_thread, NULL);
    pthread_create(&dns, NULL, &dns_thread, NULL);

    void *_retval;
    pthread_join(seed, &_retval);
    pthread_join(dns, &_retval);
}
