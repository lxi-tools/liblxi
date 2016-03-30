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
#include "list.h"
#include <lxi/lxi.h>

#define RPC_PORT 111
#define ID_REQ_STRING "*IDN?\n"
#define SESSIONS_MAX 50

struct session_t
{
    bool allocated;
    bool connected;
    CLIENT *rpc_client;
    Create_LinkResp *link_resp;
};

static struct session_t session[SESSIONS_MAX] = {};
static pthread_mutex_t session_mutex = PTHREAD_MUTEX_INITIALIZER;
static list_p device_list = NULL;
static list_iter_p device_iter;
static bool first_get_info_call = true;

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

    return 0;
}

int lxi_connect(char *address)
{
    Create_LinkParms link_params;
    Create_LinkResp *link_resp;
    bool session_available = false;
    int i;

    // Assign session ID
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
    link_params.lock_timeout = 10000; // ?
    link_params.lockDevice = 0;
    link_params.device = "device";
    session[i].link_resp = create_link_1(&link_params, session[i].rpc_client);
    if (session[i].link_resp == NULL)
        goto error_link;

    session[i].allocated = true;
    session[i].connected = true;

    pthread_mutex_unlock(&session_mutex);

    return i;

error_link:
    clnt_destroy(session[i].rpc_client);
error_client:
error_session:
    pthread_mutex_unlock(&session_mutex);
    return -1;
}

int lxi_disconnect(int device)
{
    if (device > SESSIONS_MAX)
        return 1;

    if (session[device].connected)
        clnt_destroy(session[device].rpc_client);

    session[device].connected = false;
    session[device].allocated = false;

    return 0;
}

int lxi_send(int device, char *message, int length, int timeout)
{
    Device_WriteParms write_params;
    Device_WriteResp *write_resp;

    // Configure VXI11 write parameters
    write_params.lid = session[device].link_resp->lid;
    write_params.lock_timeout = timeout*1000;
    write_params.io_timeout = timeout*1000;
    write_params.flags = 0x9;
    write_params.data.data_len = length;
    write_params.data.data_val = message;
    write_resp = device_write_1(&write_params, session[device].rpc_client);
    if (write_resp == NULL)
        return -1;

    return 0;
}

int lxi_receive(int device, char *message, int *length, int timeout)
{
    Device_ReadParms read_params;
    Device_ReadResp *read_resp;
    int offset = 0;

    // Configure VXI11 read parameters
    read_params.lid = session[device].link_resp->lid;
    read_params.lock_timeout = timeout*1000;
    read_params.io_timeout = timeout*1000;
    read_params.flags = 0x9;
    read_params.termChar = '\n';
    read_params.requestSize = LXI_MESSAGE_LENGTH_MAX;

    // Receive until done
    do
    {
        read_resp = device_read_1(&read_params, session[device].rpc_client);
        if (read_resp == NULL)
            return -1;

        if (read_resp->data.data_len > 0)
        {
            memcpy(message+offset, read_resp->data.data_val, read_resp->data.data_len);
            *length += read_resp->data.data_len;
            offset += read_resp->data.data_len;
        }
        else
            return -1;
    } while (read_resp->reason == 0);

    return 0;
}
static void get_avail_broadcast_addrs(list_p broadcast_addr_list)
{
    struct ifaddrs *ifap;

    if (getifaddrs(&ifap) == 0)
    {
        struct ifaddrs *p = ifap;

        while (p)
        {
            if ((p->ifa_addr) && (p->ifa_addr->sa_family == AF_INET))
                list_add(broadcast_addr_list, p->ifa_broadaddr, sizeof(struct sockaddr_in));

            p = p->ifa_next;
        }

        freeifaddrs(ifap);
    }
}

static int get_device_id(char *address, char *id, int timeout)
{
    int length;
    int device;

    device = lxi_connect(address);
    if (device >= 0)
    {
        lxi_send(device, ID_REQ_STRING, strlen(ID_REQ_STRING), timeout);
        lxi_receive(device, id, &length, timeout);
        return 0;
    }

    return 1;
}

static void discover_devices(struct sockaddr_in *broadcast_addr, list_p device_list, int timeout)
{
    int sockfd;
    struct sockaddr_in send_addr;
    struct sockaddr_in recv_addr;
    int broadcast = true;
    int count;
    char buffer[LXI_MESSAGE_LENGTH_MAX];
    struct timeval tv;
    socklen_t addrlen;
    lxi_device_t device;

    // Create a socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1)
    {
        perror("Socket creation error");
        exit(1);
    }

    // Set socket options - broadcast
    if((setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST,
                    &broadcast,sizeof (broadcast))) == -1)
    {
        perror("setsockopt - SO_SOCKET");
        exit(1);
    }

    // Set socket options - timeout
    tv.tv_sec = timeout;
    tv.tv_usec = 0;
    if ((setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv))) == -1)
    {
        perror("setsockopt - SO_RCVTIMEO");
        exit(1);
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
        count = recvfrom(sockfd, buffer, LXI_MESSAGE_LENGTH_MAX, 0,
                (struct sockaddr*)&recv_addr, &addrlen);
        if (count > 0)
        {
            // Add device if an LXI/SCPI ID string is returned
            char *address = inet_ntoa(recv_addr.sin_addr);
            if (get_device_id(address, device.id, timeout) == 0)
            {
                strcpy(device.address, address);
                list_add(device_list, &device, sizeof(lxi_device_t));
            }
        }
    } while (count > 0);
}

int lxi_discover_devices(lxi_devices_t **devices, int timeout, int verbose)
{
    struct sockaddr_in *broadcast_addr;
    list_iter_p iter;

    list_p broadcast_addr_list = create_list();

    // Free old device list if any
    if (device_list != NULL)
        destroy_list(device_list);

    // Create device list
    device_list = create_list();

    // Find available broadcast addresses
    get_avail_broadcast_addrs(broadcast_addr_list);

    // Find LXI devices
    iter = list_iterator(broadcast_addr_list, FRONT);
    while (list_next(iter) != NULL)
    {
        broadcast_addr = (struct sockaddr_in *) list_current(iter);
        if (verbose)
            printf("Broadcasting on %s\n", inet_ntoa(broadcast_addr->sin_addr));
        discover_devices(broadcast_addr, device_list, timeout);
    }

    // Return list of any devices found
    if (list_empty(device_list))
        *devices = NULL;
    else
        *devices = device_list;

    destroy_list(broadcast_addr_list);

    first_get_info_call = true;

    return 0;
}

int lxi_get_device_info(void *devices, lxi_device_t *device)
{
    list_p device_list = (list_p) devices;
    lxi_device_t *device_p;

    if (devices == NULL)
        return 1;

    if (first_get_info_call)
    {
        device_iter = list_iterator(device_list, FRONT);
        first_get_info_call = false;
    }

    if (list_next(device_iter) != NULL)
    {
        device_p = (lxi_device_t *) list_current(device_iter);
        strncpy(device->address, device_p->address, 15);
        strncpy(device->id, device_p->id, LXI_ID_LENGTH_MAX);
    }
    else
    {
        memset(device->address, 0, 15);
        memset(device->id, 0, LXI_ID_LENGTH_MAX);
        return 1;
    }

    return 0;
}
