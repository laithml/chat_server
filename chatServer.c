# include "chatServer.h"
# include <stdio.h>
# include <stdlib.h>
# include <netinet/in.h>
# include <sys/socket.h>
# include <unistd.h>
# include <string.h>
# include <signal.h>
# include <sys/ioctl.h>


static int end_server = 0;

void intHandler(int SIG_INT) {
    end_server = 1;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: server <port>");
        exit(EXIT_FAILURE);
    }
    char readBuffer[BUFFER_SIZE];
    int port = atoi(argv[1]);

    signal(SIGINT, intHandler);

    conn_pool_t *pool = malloc(sizeof(conn_pool_t));
    if (pool == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    init_pool(pool);

    struct sockaddr_in addr;
    int listen_sd = socket(AF_INET, SOCK_STREAM, 0);

    if (listen_sd < 0) {
        perror("error making main socket");
        return -1;
    }
    int on = 1;
    ioctl(listen_sd, (int) FIONBIO, (char *) &on);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(listen_sd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        perror("error on binding");
        return -1;
    }
    if (listen(listen_sd, 5)<0){
        perror("error on listen");
        return -1;
    }
    FD_ZERO(&(pool->read_set));
    FD_ZERO(&(pool->write_set));
    FD_ZERO(&(pool->ready_read_set));
    FD_ZERO(&(pool->ready_write_set));
    FD_SET(listen_sd, &(pool->read_set));
    FD_SET(listen_sd, &(pool->write_set));

    do {
        if (pool->maxfd == 0) {
            pool->maxfd = listen_sd;
        }

        pool->ready_read_set = pool->read_set;
        pool->ready_write_set = pool->write_set;


        printf("Waiting on select()...\nMaxFd %d\n", pool->maxfd);
        pool->nready = select(pool->maxfd + 1, &(pool->ready_read_set), &(pool->ready_write_set), NULL, NULL);


        for (int sd = listen_sd; sd <= pool->maxfd; sd++) {

            if (FD_ISSET(sd, &(pool->ready_read_set))) {
                if (sd == listen_sd) {
                    int new_sd = accept(listen_sd, NULL, NULL);
                    if (new_sd == -1) {
                        end_server = 1;
                        break;
                    }
                    printf("New incoming connection on sd %d\n", sd);
                    add_conn(new_sd, pool);
                    break;

                } else {
                    printf("Descriptor %d is readable\n", sd);
                    int len = read(sd, readBuffer, BUFFER_SIZE - 1);
                    printf("%d bytes received from sd %d\n", len, sd);
                    if (len == 0) {
                        printf("Connection closed for sd %d\n",sd);
                        remove_conn(sd, pool);
                        break;

                    } else {
                        add_msg(sd, readBuffer, len, pool);
                    }
                }
            }
            if (FD_ISSET(sd, &(pool->ready_write_set))) {
                write_to_client(sd, pool);
            }

        }
    } while (end_server == 0);

    while (pool->nr_conns > 0) {

        remove_conn(pool->conn_head->fd, pool);

    }

    free(pool);

    return 0;
}

int init_pool(conn_pool_t *pool) {
    pool->conn_head = NULL;
    pool->nr_conns = 0;
    pool->nready = 0;
    pool->maxfd = 0;
    return 0;
}

int add_conn(int sd, conn_pool_t *pool) {
    /*
     * 1. allocate connection and init fields
     * 2. add connection to pool
     * */
    if (pool->conn_head == NULL) {
        pool->conn_head = (conn_t *) malloc(sizeof(conn_t));
        if (pool->conn_head == NULL) {
            printf("error malloc");
            return -1;
        }
        pool->nr_conns++;
        pool->conn_head->fd = sd;
        pool->conn_head->prev = NULL;
        pool->conn_head->next = NULL;
        pool->conn_head->write_msg_head = NULL;
        pool->conn_head->write_msg_tail = NULL;
    } else {
        conn_t *currConn, *prevConn;

        for (currConn = pool->conn_head; currConn != NULL; currConn = currConn->next) {
            prevConn = currConn;
        }

        currConn = (conn_t *) malloc(sizeof(conn_t));
        if (currConn == NULL) {
            return -1;
        }

        pool->nr_conns++;
        currConn->prev = prevConn;
        currConn->next = NULL;

        prevConn->next = currConn;

        currConn->fd = sd;

        currConn->write_msg_head = NULL;
        currConn->write_msg_tail = NULL;
    }
    if (sd > pool->maxfd) {
        pool->maxfd = sd;
    }
    FD_SET(sd, &(pool->read_set));
    return 0;
}

int remove_conn(int sd, conn_pool_t *pool) {
    /*
    * 1. remove connection from pool
    * 2. deallocate connection
    * 3. remove from sets
    * 4. update max_fd if needed
    */
    conn_t *currConn, *prevConn;
    for (currConn = pool->conn_head; currConn != NULL; currConn = currConn->next) {
        if (sd == currConn->fd) {
            prevConn = currConn->prev;
            if (currConn->next != NULL && currConn->prev != NULL) {

                currConn->next->prev = prevConn;
                prevConn->next = currConn->next;
            } else if (currConn->next == NULL && currConn->prev != NULL) {
                prevConn->next = NULL;
            } else if (currConn->prev == NULL && currConn->next != NULL) {

                currConn->next->prev = NULL;
                pool->conn_head = currConn->next;
            } else {

                pool->conn_head = NULL;
            }
            if (currConn->write_msg_head != NULL) {
                for (msg_t *currMsg = currConn->write_msg_head; currMsg != NULL; currMsg = currMsg->next) {
                    if (currMsg->prev != NULL) {
                        free(currMsg->prev);
                    }
                    if (currMsg->message != NULL) {
                        free(currMsg->message);
                    }
                    if (currMsg->next == NULL) {
                        free(currMsg);
                        break;
                    }
                }
            }
        }
    }
    FD_CLR(sd, &(pool->read_set));
    FD_CLR(sd, &(pool->write_set));
    FD_CLR(sd, &(pool->ready_read_set));
    FD_CLR(sd, &(pool->ready_write_set));
    pool->nr_conns--;
    if (sd == pool->maxfd) {
        pool->maxfd = 0;

        if (pool->nr_conns > 0) {
            for (currConn = pool->conn_head; currConn != NULL; currConn = currConn->next) {
                if (currConn->fd > pool->maxfd) {
                    pool->maxfd = currConn->fd;
                }
            }
        }
    }
    printf("removing connection with sd %d \n", sd);
    close(sd);
    free(currConn);
    return 0;
}

int add_msg(int sd, char *buffer, int len, conn_pool_t *pool) {

    /*
     * 1. add msg_t to write queue of all other connections
     * 2. set each fd to check if ready to write
     */
    conn_t *currConn;


    for (currConn = pool->conn_head; currConn != NULL; currConn = currConn->next) {
        if (currConn->fd != sd) {
            FD_SET(currConn->fd, &(pool->write_set));
            msg_t *currMsg = (msg_t *) malloc(sizeof(msg_t));
            currMsg->prev = currConn->write_msg_tail;
            currMsg->next = NULL;
            currMsg->message = (char *) malloc((len + 1));
            strcpy(currMsg->message, buffer);
            currMsg->message[len] = '\0';
            currMsg->size = len;
            currConn->write_msg_tail = currMsg;
            if (currConn->write_msg_head == NULL) {
                currConn->write_msg_head = currMsg;
            }

        }
    }
    return 0;
}

int write_to_client(int sd, conn_pool_t *pool) {

    /*
     * 1. write all msgs in queue
     * 2. deallocate each writen msg
     * 3. if all msgs were writen successfully, there is nothing else to write to this fd... */
    conn_t *currCon;
    for (currCon = pool->conn_head; currCon != NULL; currCon = currCon->next) {
        if (currCon->fd == sd) {
            break;
        }
    }
    if (currCon->write_msg_head == NULL) {
        return 0;
    }
    msg_t *currMsg;
    for (currMsg = currCon->write_msg_head; currMsg != NULL; currMsg = currMsg->next) {

        if (currMsg->prev != NULL) {
            free(currMsg->prev);
        }
        if (write(currCon->fd, currMsg->message, currMsg->size) < 0) {
            return -1;
        }
        free(currMsg->message);

        if (currMsg->next == NULL) {
            free(currMsg);
            break;
        }
    }
    FD_CLR(sd, &(pool->write_set));
    FD_CLR(sd, &(pool->ready_write_set));
    currCon->write_msg_head = NULL;
    currCon->write_msg_tail = NULL;
    return 0;
}