#include <netdb.h>
#include <pthread.h>
#include <signal.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <libcoin-seeder/coin-seeder.h>
#include <libdns/dns.h>
#include <liblog/log.h>

bool QUIT = false;

const char *SEED_NODE = "peercoin.peercoin-library.org";
const char *SEED_PORT = "9901";

pthread_mutex_t SEED_MUTEX;
int32_t SEED_HEIGHT = 0;
int32_t SEED_VERSION = 0;
uint64_t SEED_SERVICES = 0;
char *SEED_ADDRESS = NULL;
char *SEED_USER_AGENT = NULL;

typedef struct coin_node {
    char *node;
    time_t last_contact;
    struct coin_node *next;
} coin_node_s;

pthread_mutex_t GOOD_MUTEX;
coin_node_s *GOOD_NODES = NULL;

pthread_mutex_t CHECK_MUTEX;
coin_node_s *CHECK_NODES = NULL;

void gather_nodes_to_check(socketfd s) {

}

void write_version_message(socketfd s, struct sockaddr to, struct sockaddr from) {
    log_trace("Entering write_version_message()");

    uint32_t now = time(NULL);
    log_debug("now -> %u", now);
    char *addr;
    char *ip;
    addr = inet_ntoa(((struct sockaddr_in *)&to)->sin_addr);
    log_debug("to addr -> %s", addr);
    ip = address_to_ip(addr);
    net_addr_s addr_recv = new_net_addr(now, 1, ip, false);
    free(ip);

    addr = inet_ntoa(((struct sockaddr_in *)&from)->sin_addr);
    log_debug("from addr -> %s", addr);
    ip = address_to_ip(addr);
    net_addr_s addr_from = new_net_addr(now, 0, ip, false);
    free(ip);

    log_debug("making version_payload bytes");
    char *str = heap_string("coin-seeder");
    var_str_s user_agent = new_var_str(str);
    version_payload_s version_payload = new_version_payload(addr_recv, addr_from, user_agent);
    bytes_s bytes = serialize_version_payload(version_payload);
    free_version_payload(version_payload);

    log_debug("maing new_message");
    message_s message = new_message("version", bytes);
    write_message(s, message);
    free_message(message);

    log_trace("Exiting write_version_message()");
}

parsed_version_payload_s read_version_message(socketfd s) {
    log_trace("Entering read_version_message()");

    parsed_message_s parsed_message = read_message(s);
    if(parsed_message.parsed_bytes <= 0) {
        log_error("Failed to read_message.");
        log_trace("Exiting read_version_message()");
        return (parsed_version_payload_s){.parsed_bytes=-1};
    }

    if(strcmp(parsed_message.message.command, "version") != 0) {
        log_error("Message command was not \"version\".");
        log_trace("Exiting read_version_message()");
        return (parsed_version_payload_s){.parsed_bytes=-1};
    }

    bytes_s payload = {
        .length=parsed_message.message.length,
        .buffer=parsed_message.message.payload,
    };

    log_trace("Exiting read_version_message()");
    return parse_version_payload(payload);
}

void write_verack_message(socketfd s) {
    log_trace("Entering write_verack_message()");

    bytes_s empty = {.length=0, .buffer=NULL};
    message_s message = new_message("verack", empty);
    write_message(s, message);
    free_message(message);

    log_trace("Exiting write_verack_message()");
}

typedef struct connected_node {
    socketfd s;
    struct sockaddr *addr;
    version_payload_s version_payload;
} connected_node_s;

connected_node_s new_connected_node(socketfd s, struct addrinfo *info) {
    connected_node_s connected_node = {.s=s};
    connected_node.addr = malloc(sizeof(struct sockaddr));
    memcpy(connected_node.addr, info->ai_addr, sizeof(struct sockaddr));

    return connected_node;
}

void free_connected_node(connected_node_s connected_node) {
    close(connected_node.s);
    free_version_payload(connected_node.version_payload);
    free(connected_node.addr);
}

connected_node_s connect_to_node(const char *node) {
    log_trace("Entering connect_to_node()");

    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *result;
    int error_number = getaddrinfo(node, SEED_PORT, &hints, &result);
    if (error_number != 0) {
        log_error("getaddrinfo(): %s\n", gai_strerror(error_number));
        return (connected_node_s){.s=-1};
    }

    struct timeval timeout = {.tv_sec=1, .tv_usec=0};
    socketfd s;
    struct addrinfo *info;
    for (info = result; info != NULL; info = info->ai_next) {
        log_debug("Trying IP address: %s", inet_ntoa(((struct sockaddr_in *) info->ai_addr)->sin_addr));

        s = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
        if (s == -1) {
            continue;
        }

        int res, opt;

        // get socket flags
        if ((opt = fcntl(s, F_GETFL, NULL)) < 0) {
            continue;
        }

        // set socket non-blocking
        if (fcntl (s, F_SETFL, opt | O_NONBLOCK) < 0) {
            continue;
        }

        if ((res = connect(s, info->ai_addr, info->ai_addrlen)) < 0) {
            if (errno == EINPROGRESS) {
                fd_set wait_set;

                // make file descriptor set with socket
                FD_ZERO(&wait_set);
                FD_SET (s, &wait_set);

                // wait for socket to be writable; return after given timeout
                res = select(s + 1, NULL, &wait_set, NULL, &timeout);
            }
        }
        else {
            res = 1; // connection was successful immediately
        }

        // reset socket flags
        if (fcntl (s, F_SETFL, opt) < 0) {
            continue;
        }

        // an error occured in connect or select
        if (res <= 0) {
            continue;
        }

        break; // Connection established!
    }

    if (info == NULL) {
        log_error("Could not connect.");
        freeaddrinfo(result);
        return (connected_node_s){.s=-1};
    }

    connected_node_s connected_node = new_connected_node(s, info);
    freeaddrinfo(result);

    log_debug(
        "Connected to IP address: %s",
        inet_ntoa(((struct sockaddr_in *) connected_node.addr)->sin_addr)
    );

    struct sockaddr from;
    memset(&from, 0, sizeof(struct sockaddr));
    write_version_message(connected_node.s, *connected_node.addr, from);

    parsed_version_payload_s parsed = read_version_message(connected_node.s);
    if(parsed.parsed_bytes == -1) {
        log_error("Handshake failed.");
        free_connected_node(connected_node);
        return (connected_node_s){.s=-1};
    }

    write_verack_message(connected_node.s);
    connected_node.version_payload = parsed.version_payload;

    log_trace("Exiting connect_to_node()");
    return connected_node;
}

void write_getaddr_message(socketfd s) {
    log_trace("Entering write_getaddr_message()");

    bytes_s empty = {.length=0, .buffer=NULL};
    message_s message = new_message("getaddr", empty);
    write_message(s, message);
    free_message(message);

    log_trace("Exiting write_getaddr_message()");
}

void *seed_thread() {
    log_trace("Entering seed_thread()");

    while(!QUIT) {
        // Need to make a tcp connection
        connected_node_s connected_node = connect_to_node(SEED_NODE);
        if(connected_node.s <= 0) {
            log_warn("Failed to connect_to_node()");
        }
        else {
            log_debug("Connected to SEED_NODE");

            pthread_mutex_lock(&SEED_MUTEX);
            {
                SEED_HEIGHT = connected_node.version_payload.start_height;
                log_info("SEED_HEIGHT: %d", SEED_HEIGHT);
                SEED_VERSION = connected_node.version_payload.version;
                log_info("SEED_VERSION: %d", SEED_VERSION);
                SEED_SERVICES = connected_node.version_payload.services;
                log_info("SEED_SERVICES: %ld", SEED_SERVICES);
                free(SEED_USER_AGENT);
                SEED_USER_AGENT = strdup(connected_node.version_payload.user_agent.string);
                log_info("SEED_USER_AGENT: %s", SEED_USER_AGENT);
            }
            pthread_mutex_unlock(&SEED_MUTEX);

            write_getaddr_message(connected_node.s);

            gather_nodes_to_check(connected_node.s);

            char *seed_address = inet_ntoa(((struct sockaddr_in *) connected_node.addr)->sin_addr);
            log_debug("seed ip: %s", seed_address);
            int addr_count = 0;
            while(addr_count < 2) {
                parsed_message_s parsed = read_message(connected_node.s);
                if (strcmp(parsed.message.command, "addr") == 0) {
                    log_debug("got an addr message! need to parse and use it!");
                    addr_count++;
                    bytes_s window = {.length=parsed.message.length, .buffer=parsed.message.payload};
                    parsed_addr_payload_s parsed = parse_addr_payload(window);
                    log_debug("has %d addresses", parsed.addr_payload.count.value);

                    pthread_mutex_lock(&CHECK_MUTEX);
                    {
                        for(int i = 0; i < parsed.addr_payload.count.value; i++) {
                            net_addr_s net_addr = parsed.addr_payload.addr_list[i];
                            if(net_addr.port != 9901) {
                                log_debug("%d) bad port rejected", i);
                                continue;
                            }

                            char address[100];
                            inet_ntop(AF_INET, &net_addr.ip[12], address, 100);
                            if(strcmp(address, "127.0.0.1") == 0) {
                                log_debug("%d) %s, bad ip rejected", i, address);
                                continue;
                            }
                            else if (strcmp(address, "0.0.0.0") == 0) {
                                log_debug("%d) %s, bad ip rejected", i, address);
                                continue;
                            }
                            else if (strcmp(address, seed_address) == 0) {
                                log_debug("%d) %s, don't add seed node", i, address);
                                continue;
                            }

                            log_debug("%d) %s", i, address);
                            coin_node_s *n = malloc(sizeof(coin_node_s));
                            n->node=strdup(address);
                            n->last_contact = 0;
                            n->next=CHECK_NODES;
                            CHECK_NODES = n;
                        }
                    }
                    pthread_mutex_unlock(&CHECK_MUTEX);
                }
                else {
                    log_debug("got a %s message, not sure what to do with. dropping.", parsed.message.command);
                }
            }
        }

        free_connected_node(connected_node);

        // Sleep for 10 minutes (600 seconds)
        log_debug("Sleeping seed_thread() for 10 minutes");
        for(int i = 0; i < 600; i++) {
            if(QUIT) {
                break;
            }
            sleep(1);
        }
        log_debug("Waking seed_thread()");
    }

    log_trace("Exiting seed_thread()");
    return NULL;
}

void add_good_node(coin_node_s *n) {
    log_trace("Entering add_good_node()");

    n->last_contact = time(NULL);
    pthread_mutex_lock(&GOOD_MUTEX);
    {
        n->next = GOOD_NODES;
        GOOD_NODES = n;

        for(coin_node_s *node = n; node != NULL; node = node->next) {
            log_debug("Good node %s %d", node->node, node->last_contact);
        }
    }
    pthread_mutex_unlock(&GOOD_MUTEX);

    log_trace("Exit add_good_node()");
}

void *check_thread() {
    log_trace("Entering check_thread()");

    while(!QUIT) {

        // Pop a coin_node off the stack.
        coin_node_s *n = NULL;
        pthread_mutex_lock(&CHECK_MUTEX);
        {
            if(CHECK_NODES) {
                n = CHECK_NODES;
                CHECK_NODES = n->next;
                n->next = NULL;
            }
        }
        pthread_mutex_unlock(&CHECK_MUTEX);

        if(!n) {
            log_debug("No nodes to check.");
            sleep(1);
            continue;
        }

        log_debug("Checking node: %s...", n->node);
        // Connect to coin_node, check the version and block height.
        connected_node_s conn = connect_to_node(n->node);

        if(conn.s == -1) {
            log_debug("Connection to node failed.");
            free(n->node);
            free(n);
            continue;
        }

        int seed_height;
        char *seed_user_agent;
        pthread_mutex_lock(&SEED_MUTEX);
        {
            seed_height = SEED_HEIGHT;
            seed_user_agent = strdup(SEED_USER_AGENT);
        }
        pthread_mutex_unlock(&SEED_MUTEX);

        int same_user_agent = strcmp(conn.version_payload.user_agent.string, seed_user_agent);
        free(seed_user_agent);

        if(same_user_agent != 0) {
            log_debug("User agent doesn't match.");
            free(n->node);
            free(n);
            continue;
        }

        // Add to GOOD_NODES if it passes, free for now.
        if(conn.version_payload.start_height < seed_height) {
            log_debug("Too few blocks.");
            free(n->node);
            free(n);
            continue;
        }

        log_debug("Looks good!");
        add_good_node(n);
    }

    log_trace("Exiting check_thread()");
    return NULL;
}


void *dns_thread() {
    log_trace("Entering dns_thread()");

    while(!QUIT) {
        // Need to listen for incoming UDP requests and respond appropriately...
        sleep(1);
    }

    log_trace("Exiting dns_thread()");
    return NULL;
}

void handle_control_c(int _) {
    log_trace("Entering handle_control_c()");
    log_info("Shutting down");
    QUIT = true;
    log_trace("Exiting handle_control_c()");
}

int main (int argc, char *argv[])
{
    log_set_level(LOG_TRACE);

    struct sigaction act = {.sa_handler=handle_control_c};
    sigaction(SIGINT, &act, NULL);

    pthread_t check1, check2, seed, dns;

    log_info("Starting check1 thread");
    pthread_create(&check1, NULL, &check_thread, NULL);

    log_info("Starting check2 thread");
    pthread_create(&check2, NULL, &check_thread, NULL);

    log_info("Starting dns thread");
    pthread_create(&dns, NULL, &dns_thread, NULL);

    log_info("Starting seed thread");
    pthread_create(&seed, NULL, &seed_thread, NULL);

    void *_;
    pthread_join(check1, &_);
    log_info("Check1 thread rejoined the main thread");

    pthread_join(check2, &_);
    log_info("Check2 thread rejoined the main thread");

    pthread_join(dns, &_);
    log_info("Dns thread rejoined the main thread");

    pthread_join(seed, &_);
    log_info("Seed thread rejoined the main thread");
}
