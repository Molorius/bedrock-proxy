
#include <stdio.h>
#include "proxy.h"

#define MAX_CLIENTS 40
#define TIMEOUT 15 // seconds
#define CLIENT_PORT 19132 // port that the Xbox One attempts to connect to, do not change
#define SERVER_PORT 19132
#define SERVER_ADDR "exampleserver.com"

int main(int argc, char* argv[]) {
  int ret;

  ret = start_proxy(CLIENT_PORT, SERVER_PORT, SERVER_ADDR, TIMEOUT, MAX_CLIENTS, NULL, NULL);
  if(ret) {
    printf("Error! %i\n", ret);
  }
  return ret;
}
