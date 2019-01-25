
#ifndef PROXY_H
#define PROXY_H

int start_proxy(unsigned int client_port, unsigned int server_port, const char* server_address, int timeout, int max_clients, void (*connect_cb)(int), void (*disconnect_cb)(int));

#endif
