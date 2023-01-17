#include <stdio.h>
#include <unistd.h>
#include <libc.h>
#include "chatServer.h"


static int end_server = 0;

void intHandler(int SIG_INT) {
    /* use a flag to end_server to break the main loop */
}

int main(int argc, char *argv[]) {
    if(argc!= 2) {
        printf("Usage: server <port>\n");
        return 1;
    }
    int port = atoi(argv[1]);
    if(port<1 || port>65535) {
        printf("Usage: server <port>\n");
        return 1;
    }

    signal(SIGINT, intHandler);

    conn_pool_t *pool = malloc(sizeof(conn_pool_t));
    init_pool(pool);
    char readBuffer[BUFFER_SIZE]={0};

    /*************************************************************/
    /* Create an AF_INET stream socket to receive incoming      */
    /* connections on                                            */
    /*************************************************************/

    int listen_sd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sd < 0) {
        perror("socket() failed");
        exit(-1);
    }


    /*************************************************************/
    /* Set socket to be nonblocking. All of the sockets for      */
    /* the incoming connections will also be nonblocking since   */
    /* they will inherit that state from the listening socket.   */
    /*************************************************************/
    int on = 1;
    int rc = ioctl(listen_sd, FIONBIO, (char *) &on);
    if (rc < 0) {
        perror("ioctl() failed");
        close(listen_sd);
        exit(-1);
    }

    /*************************************************************/
    /* Bind the socket                                           */
    /*************************************************************/
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    rc = bind(listen_sd,
              (struct sockaddr *) &addr,
              sizeof(addr));
    if (rc < 0) {
        perror("bind() failed");
        close(listen_sd);
        exit(-1);
    }

    /*************************************************************/
    /* Set the listen back log                                   */
    /*************************************************************/
    rc = listen(listen_sd, 32);
    if (rc < 0) {
        perror("listen() failed");
        close(listen_sd);
        exit(-1);
    }
    /*************************************************************/
    /* Initialize fd_sets  			                             */
    /*************************************************************/
    FD_ZERO(&pool->read_set);
    FD_ZERO(&pool->write_set);
    FD_ZERO(&pool->ready_read_set);
    FD_ZERO(&pool->ready_write_set);
    /*************************************************************/
    /* Loop waiting for incoming connects, for incoming data or  */
    /* to write data, on any of the connected sockets.           */
    /*************************************************************/
    do {
        /**********************************************************/
        /* Copy the master fd_set over to the working fd_set.     */
        /**********************************************************/
        pool->ready_read_set = pool->read_set;
        pool->ready_write_set = pool->write_set;
        /**********************************************************/
        /* Call select() 										  */
        /**********************************************************/
        printf("Waiting on select()...\nMaxFd %d\n", pool->maxfd);
        pool->nready = select(pool->maxfd + 1, &pool->ready_read_set, &pool->ready_write_set, NULL, NULL);
        if (pool->nready < 0) {
            perror("select() failed");
            break;
        }

        /**********************************************************/
        /* One or more descriptors are readable or writable.      */
        /* Need to determine which ones they are.                 */
        /**********************************************************/

        for (int curr_fd=listen_sd; curr_fd<=pool->maxfd; curr_fd++) {

            /* Each time a ready descriptor is found, one less has  */
            /* to be looked for.  This is being done so that we     */
            /* can stop looking at the working set once we have     */
            /* found all of the descriptors that were ready         */

            /*******************************************************/
            /* Check to see if this descriptor is ready for read   */
            /*******************************************************/
            if (FD_ISSET(curr_fd,&pool->ready_read_set)) {
                /***************************************************/
                /* A descriptor was found that was readable		   */
                /* if this is the listening socket, accept one      */
                /* incoming connection that is queued up on the     */
                /*  listening socket before we loop back and call   */
                /* select again. 						            */
                /****************************************************/
                if(curr_fd==listen_sd){
                    printf("New incoming connection on sd %d\n", curr_fd);
                 int new_sd= accept(listen_sd, NULL, NULL);
                    if (new_sd < 0) {
                        perror("accept() failed");
                        end_server = 1;
                        break;
                    }
                    add_conn(new_sd, pool);
                }else {
                    /****************************************************/
                    /* If this is not the listening socket, an 			*/
                    /* existing connection must be readable				*/
                    /* Receive incoming data his socket             */
                    /****************************************************/
                    printf("Descriptor %d is readable\n", curr_fd);
                    int len = read(curr_fd, readBuffer, BUFFER_SIZE);
                    printf("%d bytes received from sd %d\n", len, curr_fd);

                    /* If the connection has been closed by client 		*/
                    /* remove the connection (remove_conn(...))    		*/
                    if(len==0){
                        printf("Connection closed for sd %d\n",curr_fd);
                        close(curr_fd);
                        remove_conn(curr_fd, pool);
                    }else
                    /**********************************************/
                    /* Data was received, add msg to all other    */
                    /* connectios					  			  */
                    /**********************************************/
                        add_msg(curr_fd, readBuffer, len, pool);
                }
            } /* End of if (FD_ISSET()) */
            /*******************************************************/
            /* Check to see if this descriptor is ready for write  */
            /*******************************************************/
            if (FD_ISSET(curr_fd,&pool->ready_write_set)) {
                /* try to write all msgs in queue to sd */
                write_to_client(curr_fd, pool);
            }
            /*******************************************************/


        } /* End of loop through selectable descriptors */

    } while (end_server == 0);

    /*************************************************************/
    /* If we are here, Control-C was typed,						 */
    /* clean up all open connections					         */
    /*************************************************************/

    while (pool->nr_conns>0){
        remove_conn(pool->conn_head->fd,pool);
    }
    free(pool);
    return 0;
}


int init_pool(conn_pool_t *pool) {
    //initialized all fields
    pool->maxfd = -1;
    pool->conn_head =NULL;
    pool->nready = 0;
    pool->nr_conns = 0;
    return 0;
}

int add_conn(int sd, conn_pool_t *pool) {
    /*
     * 1. allocate connection and init fields
     * 2. add connection to pool
     * */
    conn_t *conn = calloc(1, sizeof(conn_t));
    conn->fd = sd;

    if (pool->conn_head == NULL) {
        pool->conn_head = conn;
    } else {
        conn->next = pool->conn_head->next;
        pool->conn_head->next = conn;
    }
    pool->nr_conns++;
    if (sd > pool->maxfd) {
        pool->maxfd = sd;
    }
    FD_SET(sd, &pool->read_set);
    FD_SET(sd, &pool->write_set);
    return 0;
}


int remove_conn(int sd, conn_pool_t *pool) {
    /*
    * 1. remove connection from pool
    * 2. deallocate connection
    * 3. remove from sets
    * 4. update max_fd if needed
    */
    conn_t *conn = pool->conn_head;
    while (conn->next != NULL) {
        if (conn->next->fd == sd) {
            conn_t *temp = conn->next;
            conn->next = conn->next->next;
            free(temp);
            pool->nr_conns--;
            break;
        }
        conn = conn->next;
    }
    //remove from sets
    FD_CLR(sd, &pool->read_set);
    FD_CLR(sd, &pool->write_set);
    //update max_fd if needed
    if (sd == pool->maxfd) {
        pool->maxfd = -1;
        conn_t *conn = pool->conn_head;
        while (conn->next != NULL) {
            if (conn->next->fd > pool->maxfd) {
                pool->maxfd = conn->next->fd;
            }
            conn = conn->next;
        }
    }
    printf("removing connection with sd %d \n", sd);


    return 0;
}

int add_msg(int sd, char *buffer, int len, conn_pool_t *pool) {

    /*
     * 1. add msg_t to write queue of all other connections
     * 2. set each fd to check if ready to write
     */
    len++;
    conn_t *conn = pool->conn_head;
    while (conn->next != NULL) {
        if (conn->next->fd != sd) {
            msg_t *msg = calloc(1, sizeof(msg_t));
            msg->size = len;
            msg->message = calloc(len, sizeof(char));
            memcpy(msg->message, buffer, len);
            conn->write_msg_tail->next = msg;
            conn->write_msg_tail = msg;
            FD_SET(conn->next->fd, &pool->write_set);
        }
        conn = conn->next;
    }


    return 0;
}

int write_to_client(int sd, conn_pool_t *pool) {

    /*
     * 1. write all msgs in queue
     * 2. deallocate each writen msg
     * 3. if all msgs were writen successfully, there is nothing else to write to this fd...
     */
    conn_t *conn = pool->conn_head;
    while (conn->next != NULL) {
        if (conn->next->fd == sd) {
            msg_t *msg = conn->next->write_msg_head->next;
            while (msg != NULL) {
                int n = write(sd, msg->message, msg->size);
                if (n < 0) {
                    return -1;
                }
                msg_t *temp = msg;
                msg = msg->next;
                free(temp->message);
                free(temp);
            }
            conn->next->write_msg_head->next = NULL;
            conn->next->write_msg_tail = conn->next->write_msg_head;
            FD_CLR(sd, &pool->write_set);
            break;
        }
        conn = conn->next;
    }


    return 0;
}

