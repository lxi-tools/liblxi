/*
 * Copyright (c) 2016, Martin Lund
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
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <rpc/rpc.h>
#include <pthread.h>
#include "vxi11core.h"
#include <lxi.h>

#define RPC_PORT 111
#define SESSIONS_MAX 256
#define ID_REQ_STRING "*IDN?\n"

struct session_t
{
    bool allocated;
    bool connected;
    CLIENT *rpc_client;
    Create_LinkResp *link_resp;
};

static struct session_t session[SESSIONS_MAX] = {};
static pthread_mutex_t session_mutex = PTHREAD_MUTEX_INITIALIZER;

// Payload representing GETPORT RPC call
static char rpc_GETPORT_msg[] =
{
    0x00, 0x00, 0x03, 0xe8, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x86, 0xa0,
    0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x06, 0x07, 0xaf, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00
};

int lxi_init(void)
{
    int i;

    // Initialize session structures
    for (i = 0; i < SESSIONS_MAX; i++)
    {
        session[i].allocated = false;
        session[i].connected = false;
    }

    return LXI_OK;
}

int lxi_connect(char *address)
{
    Create_LinkParms link_params;
    Create_LinkResp *link_resp;
    bool session_available = false;
    int i;

    pthread_mutex_lock(&session_mutex);

    // Find a free session entry
    for (i=0; i<SESSIONS_MAX; i++)
    {
        if (session[i].allocated == false)
        {
            session_available=true;
            break;
        }
    }

    // Return error if no session can be allocated
    if (session_available == false)
    {
        printf("Error: Too many active sessions!\n");
        goto error_session;
    }

    // Set up client
    session[i].rpc_client = clnt_create(address, DEVICE_CORE, DEVICE_CORE_VERSION, "tcp");
    if (session[i].rpc_client == NULL)
        goto error_client;

    // Set up link
    link_params.clientId = (unsigned long) session[i].rpc_client;
    link_params.lock_timeout = 0;
    link_params.lockDevice = 0;
    link_params.device = "device";
    session[i].link_resp = create_link_1(&link_params, session[i].rpc_client);
    if (session[i].link_resp == NULL)
        goto error_link;

    session[i].allocated = true;
    session[i].connected = true;

    pthread_mutex_unlock(&session_mutex);

    // Return session handle
    return i;

error_link:
    clnt_destroy(session[i].rpc_client);
error_client:
error_session:
    pthread_mutex_unlock(&session_mutex);
    return LXI_ERROR;
}

int lxi_disconnect(int device)
{
    if (device > SESSIONS_MAX)
        return LXI_ERROR;

    pthread_mutex_lock(&session_mutex);

    // Free resources
    if (session[device].connected)
        clnt_destroy(session[device].rpc_client);

    session[device].connected = false;
    session[device].allocated = false;

    pthread_mutex_unlock(&session_mutex);

    return LXI_OK;
}

int lxi_send(int device, char *message, int length, int timeout)
{
    Device_WriteParms write_params;
    Device_WriteResp *write_resp;
    struct timeval tv;

    // Configure VXI11 write parameters
    write_params.lid = session[device].link_resp->lid;
    write_params.lock_timeout = 0;
    write_params.io_timeout = 0;
    write_params.flags = 0x9;
    write_params.data.data_len = length;
    write_params.data.data_val = message;

    // Configure client timeout
    tv.tv_sec = timeout / 1000;
    tv.tv_usec = (timeout % 1000) * 1000;
    clnt_control(session[device].rpc_client, CLSET_TIMEOUT, (char *) &tv);

    // Send
    write_resp = device_write_1(&write_params, session[device].rpc_client);
    if (write_resp == NULL)
        return LXI_ERROR;

    // Return number of bytes sent
    return write_resp->size;
}

int lxi_receive(int device, char *message, int length, int timeout)
{
    Device_ReadParms read_params;
    Device_ReadResp *read_resp;
    int offset = 0;
    int response_length = 0;
    struct timeval tv;

    // Configure VXI11 read parameters
    read_params.lid = session[device].link_resp->lid;
    read_params.lock_timeout = 0;
    read_params.io_timeout = 0;
    read_params.flags = 0x9;
    read_params.termChar = '\n';
    read_params.requestSize = length;

    // Configure client timeout
    tv.tv_sec = timeout / 1000;
    tv.tv_usec = (timeout % 1000) * 1000;
    clnt_control(session[device].rpc_client, CLSET_TIMEOUT, (char *) &tv);

    // Receive until done
    do
    {
        read_resp = device_read_1(&read_params, session[device].rpc_client);
        if (read_resp == NULL)
            return LXI_ERROR;

        if (read_resp->data.data_len > 0)
        {
            response_length += read_resp->data.data_len;

            // Return error if provided receive message buffer is too small
            if (response_length > length)
                return LXI_ERROR;

            memcpy(message+offset, read_resp->data.data_val, read_resp->data.data_len);
            offset += read_resp->data.data_len;
        }
        else
            return LXI_ERROR;

    } while (read_resp->reason == 0);

    // Return number of bytes received
    return response_length;
}

static int get_device_id(char *address, char *id, int timeout)
{
    int length;
    int device;

    device = lxi_connect(address);
    if (device < 0)
        goto error_connect;

    length = lxi_send(device, ID_REQ_STRING, strlen(ID_REQ_STRING), timeout);
    if (length < 0)
        goto error_send;

    length = lxi_receive(device, id, LXI_ID_LENGTH_MAX, timeout);
    if (length < 0)
        goto error_receive;

    lxi_disconnect(device);

    // Strip newline
    if (id[strlen(id)-1] == '\n')
        id[strlen(id)-1] = 0;

    return LXI_OK;

error_receive:
error_send:
    lxi_disconnect(device);
error_connect:
    return LXI_ERROR;
}

static int discover_devices(struct sockaddr_in *broadcast_addr, struct lxi_info_t *info, int timeout)
{
    int sockfd;
    struct sockaddr_in send_addr;
    struct sockaddr_in recv_addr;
    int broadcast = true;
    int count;
    char buffer[LXI_ID_LENGTH_MAX];
    char id[LXI_ID_LENGTH_MAX];
    struct timeval tv;
    socklen_t addrlen;

    // Create a socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1)
    {
        perror("Socket creation error");
        return LXI_ERROR;
    }

    // Set socket options - broadcast
    if((setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST,
                    &broadcast,sizeof (broadcast))) == -1)
    {
        perror("setsockopt - SO_SOCKET");
        goto socket_options_error;
    }

    // Set socket options - timeout
    tv.tv_sec = timeout / 1000;
    tv.tv_usec = (timeout % 1000) * 1000;
    if ((setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv))) == -1)
    {
        perror("setsockopt - SO_RCVTIMEO");
        goto socket_options_error;
    }

    // Senders address
    send_addr.sin_family = AF_INET;
    send_addr.sin_addr.s_addr = INADDR_ANY;
    send_addr.sin_port = 0;     // 0 = random sender port

    // Bind socket to address
    bind(sockfd, (struct sockaddr*)&send_addr, sizeof(send_addr));

    // Receivers address
    recv_addr.sin_family = AF_INET;
    recv_addr.sin_addr.s_addr = broadcast_addr->sin_addr.s_addr;
    recv_addr.sin_port = htons(RPC_PORT);

    // Broadcast RPC GETPORT message
    sendto(sockfd, rpc_GETPORT_msg, sizeof(rpc_GETPORT_msg), 0,
            (struct sockaddr*)&recv_addr, sizeof(recv_addr));

    addrlen = sizeof(recv_addr);

    // Go through received responses
    do
    {
        count = recvfrom(sockfd, buffer, LXI_ID_LENGTH_MAX, 0,
                (struct sockaddr*)&recv_addr, &addrlen);
        if (count > 0)
        {
            // Add device if an LXI/SCPI ID string is returned
            char *address = inet_ntoa(recv_addr.sin_addr);
            if (get_device_id(address, id, timeout) == LXI_OK)
            {
                // Notify device found via callback
                if (info->device != NULL)
                    info->device(address, id);
            }
        }
    } while (count > 0);

    return LXI_OK;

socket_options_error:
    // Shutdown socket
    shutdown(sockfd, SHUT_RDWR);

    return LXI_ERROR;
}

int lxi_discover(struct lxi_info_t *info, int timeout)
{
    struct sockaddr_in *broadcast_addr;
    struct ifaddrs *ifap;
    int status;

    // Go through available broadcast addresses
    if (getifaddrs(&ifap) == 0)
    {
        struct ifaddrs *ifap_p = ifap;

        while (ifap_p)
        {
            if ((ifap_p->ifa_addr) && (ifap_p->ifa_addr->sa_family == AF_INET))
            {
                broadcast_addr = (struct sockaddr_in *) ifap_p->ifa_broadaddr;

                // Notify current broadcast address and network interface via callback
                if (info->broadcast != NULL)
                    info->broadcast(inet_ntoa(broadcast_addr->sin_addr), ifap_p->ifa_name);

                // Find LXI devices via broadcast address
                status = discover_devices(broadcast_addr, info, timeout);

            }
            ifap_p = ifap_p->ifa_next;
        }
        freeifaddrs(ifap);
    }

    return LXI_OK;
}
