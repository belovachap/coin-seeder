#include <stdio.h>
#include <liblog/log.h>

// DNS Server

// Peercoin Crawler
//   * Seed node
//   * Good nodes


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

#define BUF_SIZE 500


char *SEED_NODE = "peercoin.peercoin-library.org";
char *SEED_PORT = "9901";


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


void seed_node_thread() {
    log_trace("seed_node_thread()");

    // Need to make a tcp connection
    int tcp_socket = connect_to_seed_node();
    log_debug("Connected to SEED_NODE on socket: %d", tcp_socket);

    // Maybe hints on reading / writing to the socket!

    // Need to send some messages

    // Need to get block chain height, user agent, version, etc.

    // Use this info to judge other nodes :)

    // Need to get peer addresses

    // Use this to populate a queue (linked list?)
    close(tcp_socket);
}


int main (int argc, char *argv[])
{
    log_set_level(LOG_TRACE);
    seed_node_thread();
}
