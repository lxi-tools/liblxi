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
#include <fcntl.h>

int tcp_connect(void *data, const char *address, int port, const char *name, int timeout)
{
    struct sockaddr_in server_address;
    struct hostent *host;
    struct timeval tv;
    int result, opt;
    fd_set wait_set;
    socklen_t len;

    tcp_data_t *tcp_data = (tcp_data_t *) data;

    // Create a TCP/IP stream socket
    if ((tcp_data->server_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
        error_printf("socket() call failed\n");
        return -1;
    }

    // Get socket flags
    if ((opt = fcntl(tcp_data->server_socket, F_GETFL, NULL)) < 0)
    {
      error_printf("%s\n", strerror(errno));
      return -1;
    }
    // Set socket non-blocking
    if ((fcntl(tcp_data->server_socket, F_SETFL, opt | O_NONBLOCK)) < 0)
    {
      error_printf("%s\n", strerror(errno));
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

    tv.tv_sec = timeout / 1000;
    tv.tv_usec = (timeout % 1000) * 1000;

    // Establish connection to server
    result = connect(tcp_data->server_socket, (struct sockaddr *) &server_address, sizeof(server_address));
    if (result < 0)
    {
      if (errno == EINPROGRESS)
      {
        FD_ZERO(&wait_set);
        FD_SET(tcp_data->server_socket, &wait_set);
        // Wait for socket to be writable up to timeout duration
        result = select(tcp_data->server_socket + 1, NULL, &wait_set, NULL, &tv);
      }
    }
    else
    {
      result = 1;
    }

    // Reset socket flags
    if (fcntl(tcp_data->server_socket, F_SETFL, opt) < 0)
    {
      error_printf("%s\n", strerror(errno));
      close(tcp_data->server_socket);
      return -1;
    }

    if (result < 0)
    {
      error_printf("connect() call failed\n");
      close(tcp_data->server_socket);
      return -1;
    }
    else if (result == 0)
    {
      error_printf("connect() call timed out\n");
      close(tcp_data->server_socket);
      return -1;
    }
    else
    {
      // Check for socket errors
      len = sizeof(opt);
      if (getsockopt(tcp_data->server_socket, SOL_SOCKET, SO_ERROR, &opt, &len) != 0)
      {
        error_printf("%s\n", strerror(errno));
        close(tcp_data->server_socket);
        return -1;
      }
    }

    return 0;
}

int tcp_disconnect(void *data)
{
    tcp_data_t *tcp_data = (tcp_data_t *) data;

    close(tcp_data->server_socket);

    return 0;
}

int tcp_send(void *data, const char *message, int length, int timeout)
{
    int status;
    struct timeval tv;
    fd_set wdfs;
    int n = 0, bytes_sent = 0;

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
    {
        // Send until all data is sent
        do
        {
            n = send(tcp_data->server_socket, message + n, length, 0);
            length -= n;
            bytes_sent += n;
        }
        while (length > 0);

        return bytes_sent;
    }
    else
        error_printf("Timeout\n");

    return -1;
}

static int tcp_receive_(void *data, char *message, int length, int timeout, int flags)
{
    int status;
    struct timeval tv;
    fd_set rdfs;
    int n = 0, bytes_received = 0;

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
    {
        // Receive until all data is received
        do
        {
            n = recv(tcp_data->server_socket, message + n, length, flags);
            if (n < 0)
                break;
            length -= n;
            bytes_received += n;
        }
        while (n > 0);

        return bytes_received;
    }
    else
        error_printf("Timeout\n");

    return -1;

}

int tcp_receive(void *data, char *message, int length, int timeout)
{
    return tcp_receive_(data, message, length, timeout, MSG_DONTWAIT);
}

int tcp_receive_wait(void *data, char *message, int length, int timeout)
{
    return tcp_receive_(data, message, length, timeout, 0);
}
