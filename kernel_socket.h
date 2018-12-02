#ifndef __KERNEL_SOCKET_H
#define __KERNEL_SOCKET_H

#include "tinyos.h"
#include "kernel_dev.h"
#include "kernel_streams.h"


typedef enum{
	UNBOUND,
	LISTENER,
	PEER
}Socket_type;

typedef struct unbound_socket{
    rlnode u_node;
}unbound_socket;

typedef struct listener_socket{

    CondVar reqavailable_cv;
    rlnode Queue;
    SCB* server;

}listener_socket;

typedef struct peer_socket{

    SCB* connected_peer;
    PipeCB* pipe_read;
    PipeCB* pipe_write;
    int shutdown_read;
    int shutdown_write;
    int shutdown_both;

}peer_socket;

typedef struct socket_control_block{

    int ref_count;
    FCB* fcb_owner;
    Fid_t fid;
    Socket_type s_type;
    int port;
    int ispeer;
    int connected;

    union{
        unbound_socket u_socket;
        listener_socket l_socket;
        peer_socket p_socket;
    };

}SCB;

typedef struct request_control_block{

    SCB* client_socket;
    CondVar req_cv;
    int admit_flag;
    rlnode req_node;

}RCB;

SCB* initiallize_socket(Fid_t fid, port_t port);
int socket_read(void* dev, char* buf, unsigned int size);
int socket_write(void* dev, const char* buf, unsigned int size);
int listener_close(void* dev);
int unbound_close(void* dev);
int peer_close(void* dev);

#endif