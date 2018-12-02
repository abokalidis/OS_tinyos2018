
#include "tinyos.h"
#include "kernel_streams.h"
#include "kernel_socket.h"
#include "kernel_cc.h"
#include "kernel_pipe.h"
#include <assert.h>

SCB* PORT_MAP[MAX_PORT];

static file_ops listener_socket_ops = {
        .Open  = NULL,
        .Read  = NULL,
        .Write = NULL,
        .Close = listener_close
};

static file_ops unbound_socket_ops = {
        .Open  = NULL,
        .Read  = NULL,
        .Write = NULL,
        .Close = unbound_close
};

static file_ops peer_socket_ops = {
        .Open  = NULL,
        .Read  = socket_read,
        .Write = socket_write,
        .Close = peer_close
};
/**************************************************************
***************************************************************/
Fid_t sys_Socket(port_t port)
{
	if(port < NOPORT || port > MAX_PORT) return NOFILE; //check if fiven port exists

	FCB* fcb;
	Fid_t fid;
	int reserved = FCB_reserve(1,&fid,&fcb); //reserve an empty FCB
    
    if(reserved==0) return NOFILE;

    SCB* socket = initallize_socket(fid,port);

    return socket->fid;
}

int sys_Listen(Fid_t sock)
{
	FCB* fcb = get_fcb(sock); //take fcb of the given fid_t

    if(sock == NOFILE || fcb == NULL) return -1; //we check possible error return
    
	if(fcb != NULL && fcb->streamfunc != &unbound_socket_ops) return -1; //we check if fcb is a socket
	
	SCB* socket = (SCB*) fcb->streamobj; //we take the socket from given fid_t-->fcb

	if(socket->ispeer==1) return -1; //we check that socket is not a peer

    SCB* free_sock = PORT_MAP[socket->port]; //we acquire a scb and check that is free

    if(free_sock!=NULL || socket->port== NOPORT) return -1;

    //we give to socket listener's properties
    socket->s_type=LISTENER;
    socket->l_socket.reqavailable_cv=COND_INIT;
    rlnode_init(&socket->l_socket.Queue, NULL);
    socket->fcb_owner->streamfunc=&listener_socket_ops;

    PORT_MAP[socket->port] = socket; //insert listener to PORT_MAP

    return 0;
}


Fid_t sys_Accept(Fid_t lsock)
{
	FCB* fcb = get_fcb(lsock);

	if(sock == NOFILE || fcb == NULL) return NOFILE; //we check possible error return
    
	if(fcb != NULL && fcb->streamfunc != &listener_socket_ops) return NOFILE; //we check if fcb is a socket

	SCB* listener = (SCB*) fcb->streamobj; //get the socket

	while(is_rlist_empty(&listener->l_socket.Queue))//check if the queue of listener is empty so as to wait for new requests
		{ kernel_wait(&listener->l_socket.reqavailable_cv,SCHED_USER);}

	rlnode* request = rlist_pop_front(&listener->l_socket.Queue); //take first request

	request->rcb->admit_flag=1; //accept it

    //reserve a new fcb-fid for our server and then make listener hold this server
	FCB* fcb1;
	Fid_t fid1;

	int reserved = FCB_reserve(1,&fid1,&fcb1);

	if(reserved==0) return NOFILE;

	SCB* server = initallize_socket(fid1,listener->port);

	server->ispeer=1;
	server->connected=1;

	listener->l_socket.server= server;

	kernel_signal(&request->rcb->req_cv); //signal requests to take place for other admissions

	return server->fid;
}


int sys_Connect(Fid_t sock, port_t port, timeout_t timeout)
{
	return -1;
}


int sys_ShutDown(Fid_t sock, shutdown_mode how)
{
	return -1;
}
/**************************************************************
***************************************************************/

SCB* initiallize_socket(Fid_t fid, port_t port){
	SCB* socket = (SCB*) xmalloc(sizeof(SCB));

    socket->ref_count = 0;
    socket->type = UNBOUND;
    socket->port = port;
    socket->connected = 0;
    socket->ispeer = 0;
    socket->fid = fid;
    socket->fcb_owner = get_fcb(fid);
    socket->fcb_owner->streamobj = socket;
    socket->fcb_owner->streamfunc = &unbound_socket_ops;

    return socket;
}
