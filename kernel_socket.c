
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
*********************SOCKET SYSTEM CALLS***********************
***************************************************************/

Fid_t sys_Socket(port_t port)
{
	if(port < NOPORT || port > MAX_PORT) return NOFILE; //check if fiven port exists

	FCB* fcb;
	Fid_t fid;
	int reserved = FCB_reserve(1,&fid,&fcb); //reserve an empty FCB
    
    if(reserved==0) return NOFILE;

    SCB* socket = initiallize_socket(fid,port);

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

	if(lsock == NOFILE || fcb == NULL) return NOFILE; //we check possible error return
    
	if(fcb != NULL && fcb->streamfunc != &listener_socket_ops) return NOFILE; //we check if fcb is a socket

	SCB* listener = (SCB*) fcb->streamobj; //get the socket

	if(exhausted_fids()) return NOFILE; //check if fids are exhausted

	while(is_rlist_empty(&listener->l_socket.Queue))//check if the queue of listener is empty so as to wait for new requests
		{ 
          if(listener->closed==1) return NOFILE;
          kernel_wait(&listener->l_socket.reqavailable_cv,SCHED_USER);
        }

	rlnode* request = rlist_pop_front(&listener->l_socket.Queue); //take first request

	request->rcb->admit_flag=1; //accept it

    //reserve a new fcb-fid for our server and then make listener hold this server
	FCB* fcb1;
	Fid_t fid1;

	int reserved = FCB_reserve(1,&fid1,&fcb1);

	if(reserved==0) return NOFILE;

	SCB* server = initiallize_socket(fid1,listener->port);

	server->ispeer=1;
	server->connected=1;

	listener->l_socket.server= server;

	kernel_broadcast(&request->rcb->req_cv); //signal requests to take place for other admissions

	return server->fid;
}


int sys_Connect(Fid_t sock, port_t port, timeout_t timeout)
{
	if((port < NOPORT || port > MAX_PORT) || PORT_MAP[port] == NULL) return -1; //check if Port is legal

	if(timeout < 1000) return -1; // check timeout

	if(exhausted_fids()) return NOFILE; //check if fids are exhausted

	SCB* listener = (SCB*) PORT_MAP[port]; //get listener from given port

	FCB* fcb_sock = get_fcb(sock); //check if fcb of given fid exists and then we get client as scb

    if(fcb_sock == NULL) return -1;

    if(fcb_sock != NULL && fcb_sock->streamfunc != &unbound_socket_ops) return -1; //check that we have an unbound socket

    SCB* client = (SCB*) fcb_sock->streamobj;

    if(client->connected) return -1; // if client is already connected

    client->ispeer=1; //connect client and make it peer
    client->connected=1;
    
    RCB* new_req = new_request(client); //Create a new request

    if(is_rlist_empty(&listener->l_socket.Queue)) {

    	kernel_broadcast(&listener->l_socket.reqavailable_cv);

    	rlnode_init(&new_req->req_node,new_req);
    	rlist_push_back(&listener->l_socket.Queue,&new_req->req_node);

    	while(new_req->admit_flag == 0) kernel_timedwait(&new_req->req_cv,SCHED_USER,timeout);

    	//Acquire server from listener 
        SCB* server = listener->l_socket.server;

        // Create two pipes 
        PipeCB* pipe1 = initialize_pipe();
        PipeCB* pipe2 = initialize_pipe();

        //Convert server and client UNBOUND to PEER sockets 
        server->s_type = PEER;
        server->p_socket.connected_peer = client;
        server->fcb_owner->streamfunc = &peer_socket_ops;
        server->p_socket.shutdown_read = 0;
        server->p_socket.shutdown_write = 0;
        server->p_socket.pipe_read = pipe2;
        server->p_socket.pipe_write = pipe1;

        client->s_type = PEER;
        client->p_socket.connected_peer = server;
        client->fcb_owner->streamfunc = &peer_socket_ops;
        client->p_socket.shutdown_read = 0;
        client->p_socket.shutdown_write = 0;
        client->p_socket.pipe_read = pipe1;
        client->p_socket.pipe_write = pipe2;

        return 0;
    }
    return -1;
}


int sys_ShutDown(Fid_t sock, shutdown_mode how)
{
	FCB* fcb = get_fcb(sock);

    if(sock == NOFILE || fcb == NULL) return -1;

    if(fcb != NULL && fcb->streamfunc != &peer_socket_ops) return -1;

    SCB* peer = (SCB*) fcb->streamobj; // Take peer from sock parameter 

    SCB* other_peer = peer->p_socket.connected_peer; // Take other peer via peer 

    // Now with if statement we check the exact argument that ShutDown function is called.In case of :
    /* 1)SHUTDOWN_READ : updating shutdown_read(for peer's and other_peer's) flag to '1' for stoping pipe's operation 
    /                    that peer reads
       2)SHUTDOWN_WRITE: updating shutdown_write(for peer's and other_peer's) flag to '1' for stoping pipe's operation 
    /                    that peer writes
       3)SHUTDOWN_BOTH : updating shutdown_read/write(for peer's and other_peer's) flag to '1' for stoping two opposite
                         pipes operation
    */
    if(how == SHUTDOWN_READ)
    {
        peer->p_socket.shutdown_read = 1;
        other_peer->p_socket.shutdown_write = 1;
        return 0;
    }
    
    else if(how == SHUTDOWN_WRITE)
    {
        peer->p_socket.shutdown_write = 1;
        return 0;
    }
    else
    {
        peer->p_socket.shutdown_read = 1;
        peer->p_socket.shutdown_write = 1;
        other_peer->p_socket.shutdown_write = 1;
        return 0;
    }

    return -1;
}
/**************************************************************
***************************************************************
***************************************************************/

SCB* initiallize_socket(Fid_t fid, port_t port){
	SCB* socket = (SCB*) xmalloc(sizeof(SCB));

    socket->ref_count = 0;
    socket->s_type = UNBOUND;
    socket->port = port;
    socket->connected = 0;
    socket->ispeer = 0;
    socket->fid = fid;
    socket->closed = 0;
    socket->fcb_owner = get_fcb(fid);
    socket->fcb_owner->streamobj = socket;
    socket->fcb_owner->streamfunc = &unbound_socket_ops;

    return socket;
}

int socket_write(void* dev, const char* buf, unsigned int size){

	SCB* fst_peer = (SCB*) dev; //catch given peer

	SCB* scnd_peer = fst_peer->p_socket.connected_peer; //catch it's connected peer

	if(fst_peer->p_socket.shutdown_write || scnd_peer->p_socket.shutdown_read) return -1; //peers must be ready to write-read

	PipeCB* conn_pipe =  fst_peer->p_socket.pipe_write; 

	return wpipe_write(conn_pipe,buf,size);
}

int socket_read(void* dev, char* buf, unsigned int size){

	SCB* fst_peer = (SCB*) dev; //catch given peer

	SCB* scnd_peer = fst_peer->p_socket.connected_peer; //catch it's connected peer

	if(fst_peer->p_socket.shutdown_read == 1) return -1; //peer must be ready to read

	PipeCB* conn_pipe =  fst_peer->p_socket.pipe_read; 

	while(scnd_peer->p_socket.shutdown_write && conn_pipe->unread_bytes > 0) //if second peer's write is off for given peer
	{                                                                        //but there are some bytes to read from buffer
		return rpipe_read(conn_pipe,buf,size);                               //go and read them to exhaust buffer
	}

	if(scnd_peer->p_socket.shutdown_write && conn_pipe->unread_bytes == 0){ //if second peer's write is off for given peer 
		return 0;                                                         //and there are no bytes to read from buffer,return fail
	}else{
		return rpipe_read(conn_pipe,buf,size);
	}

}

int listener_close(void* dev){
   
    SCB* socket = (SCB*) dev;
    
    if(PORT_MAP[socket->port] != NULL) PORT_MAP[socket->port] = NULL; //free Listener socket from PORT MAP 

    socket->closed = 1;

    kernel_broadcast(&socket->l_socket.reqavailable_cv);

    free(socket);

    return 0;
}
int unbound_close(void* dev){

	SCB* socket = (SCB*) dev;
    
    socket->closed = 1;

    free(socket);

    return 0;
}
int peer_close(void* dev){

	SCB* peer = (SCB*) dev;

    peer->closed = 1;

	PipeCB* conn_pipe = peer->p_socket.pipe_read; 
                                                //first we close reader from current pipe 
    int reader = rpipe_close(conn_pipe);        

    conn_pipe = peer->p_socket.pipe_write;
                                                //second we close reader from current pipe
    int writer = wpipe_close(conn_pipe);

    if(reader == 0 && writer == 0)              //if they are closed with success then we close peer with success
    {
    	return 0;
    
    }else{
    	
    	return -1;
    }
}

RCB* new_request(SCB* sock){

	RCB* req = (RCB*) xmalloc(sizeof(RCB));

    req->client_socket = sock;
    req->admit_flag = 0;
    req->req_cv = COND_INIT;

    return req;
}

int exhausted_fids(){

    FCB* fcb_; Fid_t fid_;

    int reserved = FCB_reserve(1,&fid_,&fcb_);

    if(!reserved) return 1;

    FCB_unreserve(1,&fid_,&fcb_);

    return 0;

}
