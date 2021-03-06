#include <poll.h>

#include "common.h"

int network_socket_fd = -1;
int local_socket_fd = -1;

typedef struct{
    char *name;
    int sock_fd;
    int opp_index;
    kPingStatus ping;
    kStatus status;
    struct sockaddr addr;
} Client;
Client clients[MAX_CLIENTS];


pthread_mutex_t client_array_mutex = PTHREAD_MUTEX_INITIALIZER;

int accept2(){
    struct pollfd fds[2] = {};
    fds[0].fd = network_socket_fd;
    fds[0].events = POLLIN;
    fds[1].fd = local_socket_fd;
    fds[1].events = POLLIN;

    poll(fds, 2, -1);

    for(int i = 0; i < 2; i++) {
        if(fds[i].revents & POLLIN){
            return fds[i].fd;
        }
    }

    return -1;
}

void send_message(int fd, struct sockaddr *addr, kCommand c, const char* arg){
    char msg[MAX_MESSAGE_LEN];
    snprintf(msg, MAX_MESSAGE_LEN, "%d|%s", c, arg);
    sendto(fd, msg, MAX_MESSAGE_LEN, 0, addr, sizeof(struct sockaddr));
}

void signal_handler(){
    exit(0);
}

void cleanup(void){
    if(network_socket_fd != -1){
        close_socket(network_socket_fd);
    }

    if(local_socket_fd != -1){
        close_socket(local_socket_fd);
    }

    for(int i = 0; i < MAX_CLIENTS; i++){
        if(clients[i].status == CONNECTED){
            free(clients[i].name);
            send_message(clients[i].sock_fd, &clients[i].addr, SERVER_SHUTDOWN, NULL);
            close_socket(clients[i].sock_fd);
        }
    }

    exit(0);
}

void init_local_socket(const char *path){
    struct sockaddr_un serveraddr;

    local_socket_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sun_family = AF_UNIX;
    strcpy(serveraddr.sun_path, path);

    unlink(path);

    bind(local_socket_fd, (const struct sockaddr *)&serveraddr, SUN_LEN(&serveraddr));
}

void init_network_socket(const char * port){
    struct sockaddr_in desc;

    desc.sin_family = AF_INET;
    desc.sin_addr.s_addr = inet_addr("0.0.0.0");
    desc.sin_port = htons(atoi(port));

    network_socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(network_socket_fd == -1){
        perror("Init socket");
        exit(EXIT_FAILURE);
    }

    if(bind(network_socket_fd,  (struct sockaddr *)&desc, sizeof(struct sockaddr)) == -1){
        perror("bind");
        exit(EXIT_FAILURE);
    }
}

void init_sockets(const char * port, const char *path){
    init_local_socket(path);
    init_network_socket(port);

    if(local_socket_fd == -1 && network_socket_fd == -1){
        printf("init_socket failed\n");
        exit(EXIT_FAILURE);
    }
}

int get_free_slot(){
    for(int i = 0; i < MAX_CLIENTS; i++){
        if(clients[i].status == NOT_CONNECTED){
            return i;
        }
    }

    return -1;
}

int get_client_by_name(const char *name){
    if(name == NULL){
        return -1;
    }

    for(int i = 0; i < MAX_CLIENTS; i++){
        if(clients[i].status == CONNECTED && clients[i].name != NULL && strcmp(clients[i].name, name) == 0){
            return i;
        }
    }

    return -1;
}

void remove_client(int cid, bool removeOpp){
    if(clients[cid].status == NOT_CONNECTED){
        return;
    }

    int opp = clients[cid].opp_index;
    if(removeOpp && opp != NO_OPP){
        printf("OPP_DISCONNECTED: %s\n", clients[opp].name);
        send_message(clients[opp].sock_fd, &clients[opp].addr, OPP_DISCONNECTED, NULL);
        free(clients[opp].name);
        close_socket(clients[opp].sock_fd);
        memset(&clients[opp], 0, sizeof(Client));
        clients[opp].opp_index = NO_OPP;
    }

    free(clients[cid].name);
    close_socket(clients[cid].sock_fd);
    memset(&clients[cid], 0, sizeof(Client));
    clients[cid].opp_index = NO_OPP;
}

void assign_opp(int cid){
    int found_opp = -1;
    for(int i = 0; i < MAX_CLIENTS; i++){
        if(clients[i].status == CONNECTED && i != cid && clients[i].opp_index == NO_OPP){
            found_opp = i;
            break;
        }
    }

    if(found_opp != -1){
        clients[found_opp].opp_index = cid;
        clients[cid].opp_index = found_opp;
        send_message(clients[cid].sock_fd, &clients[cid].addr, START_GAME, /*clients[found_opp].name*/"X");
        send_message(clients[found_opp].sock_fd, &clients[found_opp].addr, START_GAME, /*clients[cid].name*/"O");
        printf("START_GAME: %s vs %s\n", clients[cid].name, clients[found_opp].name);
    }else{
        printf("'%s' waiting for game\n", clients[cid].name);
        send_message(clients[cid].sock_fd, &clients[cid].addr, WAITING_FOR_OPPONENT, NULL);
    }
}

void *ping_thread(){
    while(1){
        pthread_mutex_lock(&client_array_mutex);

        for(int i = 0 ; i < MAX_CLIENTS; i++){
            if(clients[i].status == CONNECTED && clients[i].ping == PING_SENT){
                send_message(clients[i].sock_fd, &clients[i].addr, TIMEOUT, NULL);
                printf("Dropped client %s\n", clients[i].name);
                remove_client(i, true);
            }
        }

        for(int i = 0 ; i < MAX_CLIENTS; i++){
            if(clients[i].status == CONNECTED && clients[i].ping == PING_GOT){
                send_message(clients[i].sock_fd, &clients[i].addr, PING, NULL);
                printf("PING: %s\n", clients[i].name);
                clients[i].ping = PING_SENT;
            }
        }

        pthread_mutex_unlock(&client_array_mutex);

        sleep(2);
    }
}

int main(int argc, char **argv){
    if(argc != 3){
        puts("server port socket_name");
        return 1;
    }

    atexit(cleanup);
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    char *port = argv[1];
    char *socket_path = argv[2];

    init_sockets(port, socket_path);
    printf("SERVER STARTED\n");

    pthread_t ping_thread_id;
    pthread_create(&ping_thread_id, NULL, ping_thread, NULL);

    char buffer[MAX_MESSAGE_LEN] = {};
    memset(clients, 0, sizeof(Client) * MAX_CLIENTS);
    while(1){
        int remote_fd = accept2();
        if(remote_fd == -1){
            continue;
        }

        struct sockaddr addr;
        socklen_t len = sizeof(struct sockaddr);
        if(recvfrom(remote_fd, buffer, MAX_MESSAGE_LEN, 0, &addr, &len) == 0){
            continue;
        }

        kCommand cmd = atoi(strtok(&buffer[0], "|"));
        const char *name = strtok(NULL, "|");
        const char *args = strtok(NULL, "|");
        int cid = get_client_by_name(name);

//        printf("parsed: %d '%s' '%s'\n", cmd, name, (args == NULL) ? "NULL" : args);

        pthread_mutex_lock(&client_array_mutex);

        switch (cmd) {
            case REGISTER_CLIENT:{
                if(cid != -1){
                    send_message(remote_fd, &addr, NAME_IN_USE, "");
                    close(remote_fd);
                    break;
                }

                int idx = get_free_slot();
                if(idx != -1){
                    printf("REGISTER_CLIENT: %s, idx=%d, fd=%d\n", name, idx, remote_fd);
                    clients[idx].status = CONNECTED;
                    clients[idx].name = calloc(MAX_MESSAGE_LEN, sizeof(char));
                    clients[idx].sock_fd = remote_fd;
                    clients[idx].opp_index = NO_OPP;
                    memcpy(&clients[idx].addr, &addr, len);
                    strncpy(clients[idx].name, name, MAX_MESSAGE_LEN);

                    assign_opp(idx);
                }else{
                    send_message(remote_fd, &addr, NO_FREE_SLOTS, "");
                    close(remote_fd);
                }
                break;
            }

            case DISCONNECT:{
                if(cid != -1){
                    printf("DISCONNECT: %s, fd=%d\n", name, remote_fd);
                    bool removeOpp = atoi(args) == GAME_FINISHED ? false : true;
                    remove_client(cid, removeOpp);
                }
                break;
            }

            case PING:{
                if(cid != -1){
                    printf("PING REPLY cid=%d\n", cid);
                    clients[cid].status = CONNECTED;
                    clients[cid].ping = PING_GOT;
                }
                break;
            }

            case MOVE:{
                printf("MOVE: %d -> %s\n", cid, args);
                int opp = clients[cid].opp_index;
                send_message(clients[opp].sock_fd, &clients[opp].addr, MOVE, args);
                break;
            }

            default:
                break;
        }

        pthread_mutex_unlock(&client_array_mutex);
    }
}