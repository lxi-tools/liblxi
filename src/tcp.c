/*
 * Copyright (c) 2017, Martin Lund
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include "tcp.h"
#include "error.h"

int tcp_connect(void *data, char *address, int port, char *name, int timeout)
{
    struct sockaddr_in server_address;
    struct hostent *host;

    tcp_data_t *tcp_data = (tcp_data_t *) data;

    // Create a TCP/IP stream socket
    if ((tcp_data->server_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
        error_printf("socket() call failed\n");
        return -1;
    }

    // Construct the server address structure
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family      = AF_INET;
    server_address.sin_port        = htons(port);
    server_address.sin_addr.s_addr = inet_addr(address);

    if (server_address.sin_addr.s_addr == (unsigned long) INADDR_NONE)
    {
        // Look up host address
        host = gethostbyname(address);

        if (host == (struct hostent *) NULL)
        {
            error_printf("Host not found\n");
            close(tcp_data->server_socket);
            return -1;
        }

        memcpy(&server_address.sin_addr, host->h_addr, sizeof(server_address.sin_addr));
    }

    // Establish connection to server
    if (connect(tcp_data->server_socket, (struct sockaddr *) &server_address, sizeof(server_address)) < 0)
    {
        error_printf("connect() call failed\n");
        close(tcp_data->server_socket);
        return -1;
    }

    return 0;
}

int tcp_disconnect(void *data)
{
    tcp_data_t *tcp_data = (tcp_data_t *) data;

    close(tcp_data->server_socket);

    return 0;
}

int tcp_send(void *data, char *message, int length, int timeout)
{
    int status;
    struct timeval tv;
    fd_set wdfs;

    tcp_data_t *tcp_data = (tcp_data_t *) data;

    // Set timeout
    tv.tv_sec = 0;
    tv.tv_usec = timeout * 1000;

    FD_ZERO(&wdfs);
    FD_SET(tcp_data->server_socket, &wdfs);

    status = select(tcp_data->server_socket + 1, NULL, &wdfs, NULL, &tv);
    if (status == -1)
        return -1;
    else if (status)
        return write(tcp_data->server_socket, message, length);
    else
        error_printf("Timeout\n");

    return -1;
}

int tcp_receive(void *data, char *message, int length, int timeout)
{
    int status;
    struct timeval tv;
    fd_set rdfs;

    tcp_data_t *tcp_data = (tcp_data_t *) data;

    // Set timeout
    tv.tv_sec = 0;
    tv.tv_usec = timeout * 1000;

    FD_ZERO(&rdfs);
    FD_SET(tcp_data->server_socket, &rdfs);

    status = select(tcp_data->server_socket + 1, &rdfs, NULL, NULL, &tv);
    if (status == -1)
        return -1;
    else if (status)
        return read(tcp_data->server_socket, message, length);
    else
        error_printf("Timeout\n");

    return -1;
}
