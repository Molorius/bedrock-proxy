
#include "proxy.h"
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>

#define PROXY_DEBUG
#ifdef PROXY_DEBUG
#define DEBUG(...) printf("proxy: "__VA_ARGS__)
#else
#define DEBUG(...)
#endif

//#define PACKET_DEBUG
#ifdef PACKET_DEBUG
#define PDEBUG(...) printf("proxy: "__VA_ARGS__)
#else
#define PDEBUG(...)
#endif

#ifndef IRAM_ATTR // for esp-idf
#define IRAM_ATTR
#endif

#define BUFSIZE 2048
//#define max_clients 20
//#define TIMEOUT 10 // seconds

typedef struct _msg_queue {
  unsigned char* msg;
  int len;
  struct _msg_queue* next;
} msg_queue_t;

typedef struct  {
  int fd_server; // socket file descriptor for connection to server
  struct sockaddr_in addr_server; // external address for server socket
  struct sockaddr_in addr_client; // external address for client socket
  msg_queue_t* queue_to_client; // message queue from server to client
  msg_queue_t* queue_to_server; // message queue from client to server
  struct timeval time; // last time since read/write (for timeouts)
} proxy_t;

void print_errno() {
  switch(errno) {
    case EAFNOSUPPORT:
      DEBUG("Addresses in the specified address family cannot be used with this socket.\n");
      break;
    case EAGAIN:
    //case EWOULDBLOCK:
      DEBUG("The socket's file descriptor is marked O_NONBLOCK and the requested operation would block.\n");
      break;
    case EBADF:
      DEBUG("The socket argument is not a valid file descriptor.\n");
      break;
    case ECONNRESET:
      DEBUG("A connection was forcibly closed by a peer.\n");
      break;
    case EINTR:
      DEBUG("A signal interrupted sendto() before any data was transmitted.\n");
      break;
    case EMSGSIZE:
      DEBUG("The message is too large to be sent all at once, as the socket requires.\n");
      break;
    case ENOTCONN:
      DEBUG("The socket is connection-mode but is not connected.\n");
      break;
    case ENOTSOCK:
      DEBUG("The socket argument does not refer to a socket.\n");
      break;
    case EOPNOTSUPP:
      DEBUG("The socket argument is associated with a socket that does not support one or more of the values set in flags.\n");
      break;
    case EPIPE:
      DEBUG("The socket is shut down for writing, or the socket is connection-mode and is no longer connected. In the latter case, and if the socket is of type SOCK_STREAM, the SIGPIPE signal is generated to the calling thread.\n");
      break;
    case EIO:
      DEBUG("An I/O error occurred while reading from or writing to the file system.\n");
      break;
    case ELOOP:
      DEBUG("A loop exists in symbolic links encountered during resolution of the pathname in the socket address.\n");
      break;
    case ENAMETOOLONG:
      DEBUG("A component of a pathname exceeded {NAME_MAX} characters, or an entire pathname exceeded {PATH_MAX} characters.\n");
      break;
    case ENOENT:
      DEBUG("A component of the pathname does not name an existing file or the pathname is an empty string.\n");
      break;
    case ENOTDIR:
      DEBUG("A component of the path prefix of the pathname in the socket address is not a directory.\n");
      break;
    case EACCES:
      DEBUG("Search permission is denied for a component of the path prefix; or write access to the named socket is denied.\n");
      break;
    case EDESTADDRREQ:
      DEBUG("The socket is not connection-mode and does not have its peer address set, and no destination address was specified.\n");
      break;
    case EHOSTUNREACH:
      DEBUG("The destination host cannot be reached (probably because the host is down or a remote router cannot reach it).\n");
      break;
    case EINVAL:
      DEBUG("The dest_len argument is not a valid length for the address family.\n");
      break;
    case EISCONN:
      DEBUG("A destination address was specified and the socket is already connected. This error may or may not be returned for connection mode sockets.\n");
      break;
    case ENETDOWN:
      DEBUG("The local network interface used to reach the destination is down.\n");
      break;
    case ENETUNREACH:
      DEBUG("No route to the network is present.\n");
      break;
    case ENOBUFS:
      DEBUG("Insufficient resources were available in the system to perform the operation.\n");
      break;
    case ENOMEM:
      DEBUG("Insufficient memory was available to fulfill the request.\n");
      break;
    default:
      DEBUG("Failed for unknown reason.\n");
      break;
    }
}

int IRAM_ATTR add_to_queue(msg_queue_t** queue, unsigned char* msg, int len) {
  msg_queue_t* new_item;
  msg_queue_t** tracer = queue; // make a tracer
  int ret = 1;
  //DEBUG("Adding to queue\n");

  while(*tracer) { // go to end of queue
    tracer = &(*tracer)->next;
    ret++;
  }
  new_item = malloc(sizeof(msg_queue_t));
  new_item->msg = malloc(len);
  memcpy(new_item->msg, msg, len);
  new_item->len = len;
  new_item->next = NULL;
  *tracer = new_item;
  return ret;
}

int IRAM_ATTR get_from_queue(msg_queue_t** queue, unsigned char* msg, int* len) {
  msg_queue_t** tracer = queue;
  msg_queue_t* old;

  if(*tracer) {
    old = *tracer;
    *tracer = (*tracer)->next;
    memcpy(msg, old->msg, old->len);
    *len = old->len;
    free(old->msg);
    free(old);
    return 0;
  }
  return 1;
}

int delete_proxy_connection(proxy_t* proxy) {
  if(proxy) {
    close(proxy->fd_server); // close the socket
    proxy->fd_server = 0; // set to 0 so there's no question
    memset((char *) &proxy->addr_server, 0, sizeof(proxy->addr_server)); // clear client and server connections
    memset((char *) &proxy->addr_client, 0, sizeof(proxy->addr_client));
    while(!get_from_queue(&proxy->queue_to_client, NULL, NULL)); // empty both queues
    while(!get_from_queue(&proxy->queue_to_server, NULL, NULL));
    //free(proxy);
  }
  return 0;
}

int make_proxy_connection(proxy_t* proxy, struct sockaddr_in client_addr, unsigned char* buf, int buflen, const char* server_addr, uint32_t server_port) {

  struct hostent* he;

  //proxy = (proxy_t *) malloc(sizeof(proxy_t));

  // create socket for connection to server
  if((proxy->fd_server = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    DEBUG("Couldn't create socket for server connection\n");
    delete_proxy_connection(proxy);
    return -1;
  }
  fcntl(proxy->fd_server, F_SETFL, O_NONBLOCK); // set to nonblocking

  // resolve hostname
  if((he = gethostbyname(server_addr)) == NULL) {
    DEBUG("Couldn't resolve hostname\n");
    delete_proxy_connection(proxy);
    return -1;
  }
  // create external address for server socket
  memset((char *) &proxy->addr_server, 0, sizeof(proxy->addr_server));
  memcpy(&proxy->addr_server.sin_addr, he->h_addr_list[0], he->h_length);
  proxy->addr_server.sin_family = AF_INET;
  proxy->addr_server.sin_port = htons(server_port);
  //proxy->addr_server.sin_port = 0; // allow on any port

  proxy->addr_client = client_addr;
  add_to_queue(&proxy->queue_to_server, buf, buflen);

  gettimeofday(&proxy->time, NULL);

  return 0;
}

int add_proxy_to_list(proxy_t* proxy_list, int proxy_max, struct sockaddr_in client_addr, unsigned char* buf, int buflen, const char* server_addr, uint32_t server_port) {
  for(int i=0; i<proxy_max; i++) {
    if(proxy_list[i].fd_server==0) {
      make_proxy_connection(&proxy_list[i], client_addr, buf, buflen, server_addr, server_port);
      return i;
    }
  }
  return -1;
}

int IRAM_ATTR get_queue_to_client(proxy_t* proxy_list, int proxy_max, unsigned char* buf, int* len) {
  for(int i=0; i<proxy_max; i++) {
    if(!get_from_queue(&proxy_list[i].queue_to_client, buf, len)) {
      return i;
    }
  }
  return -1;
}

int IRAM_ATTR get_queue_to_server(proxy_t* proxy_list, int proxy_max, unsigned char* buf, int* len) {
  for(int i=0; i<proxy_max; i++) {
    if(!get_from_queue(&proxy_list[i].queue_to_server, buf, len)) {
      return i;
    }
  }
  return -1;
}

int IRAM_ATTR client_in_list(proxy_t* proxy_list, int proxy_max, struct sockaddr_in client_addr) {
  int client_ip = client_addr.sin_addr.s_addr;
  int client_port = client_addr.sin_port;
  for(int i=0; i<proxy_max; i++) {
    int old_ip = proxy_list[i].addr_client.sin_addr.s_addr;
    int old_port = proxy_list[i].addr_client.sin_port;
    if((client_ip==old_ip) && (client_port==old_port)) {
      return i;
    }
  }
  return -1;
}

int IRAM_ATTR server_in_list(proxy_t* proxy_list, int proxy_max, int fd) {
  for(int i=0; i<proxy_max; i++) {
    if(fd == proxy_list[i].fd_server) {
      return i;
    }
  }
  return -1;
}

int IRAM_ATTR handle_timeouts(proxy_t* proxy_list, int proxy_max, int timeout_s) {
  struct timeval time;
  int seconds;
  int ret = 0;

  gettimeofday(&time, NULL);
  for(int i=0; i<proxy_max; i++) {
    if(proxy_list[i].fd_server > 0) {
      seconds = time.tv_sec - proxy_list[i].time.tv_sec;
      if(seconds > timeout_s) {
        DEBUG("Connection %i timed out, disconnecting.\n", i);
        delete_proxy_connection(&proxy_list[i]);
        ret++;
      }
    }
  }
  return ret;
}

int IRAM_ATTR highest_fd(int client_fd, proxy_t* proxy_list, int proxy_max) {
  int ret = client_fd;
  for(int i=0; i<proxy_max; i++) {
    if(proxy_list[i].fd_server > ret) {
      ret = proxy_list[i].fd_server;
    }
  }
  return ret;
}

int IRAM_ATTR start_proxy(uint32_t client_port, uint32_t server_port, const char* server_address, int timeout, int max_clients, void (*connect_cb)(int), void (*disconnect_cb)(int)) {
  int client_fd; // socket for clients to connect to
  struct sockaddr_in client_connection; // connection for cpu to talk to proxies
  struct sockaddr_in client_addr; // client address
  //struct sockaddr_in server_addr; // server address
  unsigned char buf[BUFSIZE];
  int buflen;
  socklen_t addrlen = sizeof(client_addr);
  proxy_t proxy_list[max_clients];
  int err;
  fd_set master_read;
  fd_set master_write;
  fd_set read_fds;
  fd_set write_fds;
  int fdmax;
  struct timeval select_timeout;
  struct timeval time;
  int seconds;

  DEBUG("Creating socket\n");
  if ((client_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    DEBUG("Cannot create socket.\n");
    return -1;
  }
  fcntl(client_fd, F_SETFL, O_NONBLOCK); // set to nonblocking

  memset((char *) &client_connection, 0, sizeof(client_connection));
  client_connection.sin_family = AF_INET;
  client_connection.sin_addr.s_addr = htonl(INADDR_ANY); // receive from anyone
  client_connection.sin_port = htons(client_port); // set port

  // Get all proxy connections cleared
  for(int i=0; i<max_clients; i++) {
    memset((char *) &proxy_list[i], 0, sizeof(proxy_list[i]));
  }

  DEBUG("Binding socket to port %i\n", client_port);
  if(bind(client_fd, (struct sockaddr *) &client_connection, sizeof(client_connection)) < 0) {
    DEBUG("Could not bind to port %i.\n", client_port);
    return -1;
  }

  //DEBUG("Socket file descriptor: %i\n", client_fd);


  FD_ZERO(&master_read);
  FD_ZERO(&master_write);
  FD_ZERO(&read_fds);
  FD_ZERO(&write_fds);
  FD_SET(client_fd, &master_read);
  fdmax = client_fd;

  DEBUG("Connections will be to server at %s port %i\n", server_address, server_port);
  DEBUG("Max clients %i, timeout %i seconds\n", max_clients, timeout);
  DEBUG("Started, waiting for clients.\n");
  for(;;) {
    read_fds = master_read;
    write_fds = master_write;
    select_timeout.tv_sec = 3;
    select_timeout.tv_usec = 0;
    if(select(fdmax+1, &read_fds, &write_fds, NULL, &select_timeout) == -1) { // wait forever
      DEBUG("Error on select function\n");
      return -1;
    }
    for(int i=0; i<=fdmax; i++) {
      if(FD_ISSET(i, &read_fds)) { // something can be read
        //DEBUG("Client can be read!\n");
        //buflen = recvfrom(client_fd, buf, BUFSIZE, 0, (struct sockaddr *) &client_addr, &addrlen);
        if(i == client_fd) { // if it's from a client
          buflen = recvfrom(client_fd, buf, BUFSIZE, 0, (struct sockaddr *) &client_addr, &addrlen);
          PDEBUG("Client %s:%i - %i bytes\n", inet_ntoa(client_addr.sin_addr), client_addr.sin_port, buflen);
          err = client_in_list(proxy_list, max_clients, client_addr);
          if(err < 0) { // if it's a new client
            //PDEBUG("New client, adding to list\n");
            err = add_proxy_to_list(proxy_list, max_clients, client_addr, buf, buflen, server_address, server_port); // add proxy to list with message in buffer
            if(err >= 0) { // if it was added
              DEBUG("New client from %s:%i added to slot %i\n", inet_ntoa(client_addr.sin_addr), client_addr.sin_port, err);
              FD_SET(proxy_list[err].fd_server, &master_read); // add server connection to read and write lists
              FD_SET(proxy_list[err].fd_server, &master_write);
              if(proxy_list[err].fd_server > fdmax) fdmax = proxy_list[err].fd_server;
              if(connect_cb) connect_cb(err);
            }
            else {
              DEBUG("All slots are full, client not added.\n");
            }
          }
          else { // it's already a known client
            add_to_queue(&proxy_list[err].queue_to_server, buf, buflen);
            gettimeofday(&proxy_list[err].time, NULL); // update time
            FD_SET(proxy_list[err].fd_server, &master_write); // tell the master it can be written
          }
        }
        else { // if it's from the server

          err = server_in_list(proxy_list, max_clients, i);
          if(err >= 0) { // if the server is in the list
            buflen = recvfrom(i, buf, BUFSIZE, 0, (struct sockaddr *) &proxy_list[err].addr_server, &addrlen);
            PDEBUG("Server %s:%i - %i bytes\n", inet_ntoa(proxy_list[err].addr_client.sin_addr), proxy_list[err].addr_client.sin_port, buflen);
            add_to_queue(&proxy_list[err].queue_to_client, buf, buflen); // add to queue
            gettimeofday(&proxy_list[err].time, NULL); // update time
            FD_SET(client_fd, &master_write); // add client socket to write list
          }
          else { // there's a rogue connection?...
            DEBUG("Unknown connection......\n");
            FD_CLR(i, &master_read);
          }
        }
      }
      if(FD_ISSET(i, &write_fds)) { // we can write to the connection
        if(i == client_fd) { // if a client is avaible
          err = get_queue_to_client(proxy_list, max_clients, buf, &buflen); // get the first queue
          if(err >= 0) { // if there's something in the queue
            gettimeofday(&proxy_list[err].time, NULL); // update time
            //PDEBUG("Sending %i bytes to client\n", buflen);
            if((sendto(i, buf, buflen, 0, (struct sockaddr *) &proxy_list[err].addr_client, sizeof(proxy_list[err].addr_client)))<0) {
              DEBUG("Error sending message to client %s:%i slot %i\n", inet_ntoa(client_addr.sin_addr), client_addr.sin_port, err);
              print_errno();
            }
          }
          else { // all queues are empty, remove from write list
            FD_CLR(i, &master_write);
          }
        }
        else { // if a server connection is available to write
          err = server_in_list(proxy_list, max_clients, i); // find which server connection we want
          if(err >= 0) { // if the server is known
            int server = err;
            err = get_from_queue(&proxy_list[server].queue_to_server, buf, &buflen); // get from a queue
            if(!err) { // if there's something in the queue
              //PDEBUG("Sending %i bytes to server\n", buflen);
              gettimeofday(&proxy_list[err].time, NULL); // update time
              if((sendto(i, buf, buflen, 0, (struct sockaddr *) &proxy_list[err].addr_server, sizeof(proxy_list[err].addr_server)))<0) {
                DEBUG("Error sending message to server from %s:%i slot %i\n", inet_ntoa(client_addr.sin_addr), client_addr.sin_port, err);
                print_errno();
              }
            }
            else { // if the queue is empty
              FD_CLR(i, &master_write);
            }
          }
          else {
            FD_CLR(i, &master_write);
          }
        }
      }
    }

    // find timeouts
    gettimeofday(&time, NULL);
    for(int i=0; i<max_clients; i++) {
      if(proxy_list[i].fd_server > 0) {
        seconds = time.tv_sec - proxy_list[i].time.tv_sec;
        if(seconds > timeout) {
          DEBUG("Client %s:%i slot %i timed out (%i s), disconnecting.\n", inet_ntoa(client_addr.sin_addr), client_addr.sin_port, i, seconds);
          FD_CLR(proxy_list[i].fd_server, &master_read);
          FD_CLR(proxy_list[i].fd_server, &master_write);
          delete_proxy_connection(&proxy_list[i]);
          fdmax = highest_fd(client_fd, proxy_list, max_clients);
          if(disconnect_cb) disconnect_cb(i);
        }
      }
    }
  }
  return 0;
}
