#include <stdio.h>
#include <stdint.h>
#include "proxy.h"

#define MAX_CLIENTS 40
#define TIMEOUT 15 // seconds
#define CLIENT_PORT 19132 // port that the Xbox One attempts to connect to, do not change

int main(int argc, char* argv[]) {
  int ret, timeout, max_clients;
  uint32_t server_port;

  if(argc < 3){
    printf("Not enough arguments supplied.\nUsage:\nbedrock-proxy <server-address> <server-port> <max_clients> <timeout>\nServer address and port arguments are required.\nMax clients and timeout(in seconds) arguments are optional and default to 40 and 15 respectively.\nExample:\nbedrock-proxy exampleserver.com 19132\n");
    return(0);
  }
  else if(argc == 3) {
    server_port = (unsigned)atoi(argv[2]);
    max_clients = MAX_CLIENTS;
    timeout = TIMEOUT;
  }
  else if(argc == 4) {
    server_port = (unsigned)atoi(argv[2]);
    max_clients = atoi(argv[3]);
    timeout = TIMEOUT;
  }
  else if(argc == 5) {
    server_port = (unsigned)atoi(argv[2]);
    max_clients = atoi(argv[3]);
    timeout = atoi(argv[4]);
  }
  else {
    printf("Too many arguments supplied.\n");
    return(0);
  }
  ret = start_proxy(CLIENT_PORT, server_port, argv[1], timeout, max_clients, NULL, NULL);
  if(ret) {
    printf("Error! %i\n", ret);
  }
  return ret;
}
