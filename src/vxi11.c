/*
 * Copyright (c) 2016-2022  Martin Lund
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

#include <errno.h>
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
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <pthread.h>
#include <signal.h>
#include "vxi11core.h"
#include "vxi11.h"
#include "tcp.h"
#include "error.h"

#define PORT_HTTP                80
#define PORT_RPC                111
#define ID_REQ_HTTP "GET /lxi/identification HTTP/1.0\r\n\r\n";
#define ID_REQ_SCPI       "*IDN?\n"
#define ID_LENGTH_MAX         65536
#define RECEIVE_END_BIT        0x04 // Receive end indicator
#define RECEIVE_TERM_CHAR_BIT  0x02 // Receive termination character


typedef struct
{
    void *data;
    const char *address;
    int port;
    const char *name;
    int timeout;
} thread_vxi11_connect_args_t;

typedef struct
{
    int joined;
    pthread_t td;
    pthread_mutex_t mtx;
    pthread_cond_t cond;
    void **res;
} thread_vxi11_wrapper_args_t;


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


// A POSIX compatible pthread_timedjoin_np
static void *_pthread_waiter(void *ap)
{
    thread_vxi11_wrapper_args_t *args = ap;
    pthread_join(args->td, args->res);
    pthread_mutex_lock(&args->mtx);
    args->joined = 1;
    pthread_mutex_unlock(&args->mtx);
    pthread_cond_signal(&args->cond);
    return 0;
}

static int _pthread_timedjoin(pthread_t td, void **res, struct timespec *ts)
{
    pthread_t tmp;
    int ret;
    thread_vxi11_wrapper_args_t args = { .td = td, .res = res };

    pthread_mutex_init(&args.mtx, 0);
    pthread_cond_init(&args.cond, 0);
    pthread_mutex_lock(&args.mtx);

    ret = pthread_create(&tmp, 0, _pthread_waiter, &args);
    if (ret == 0) {
      do {
        ret = pthread_cond_timedwait(&args.cond, &args.mtx, ts);
      } while (!args.joined && ret != ETIMEDOUT);
    }

    pthread_cancel(tmp);
    pthread_join(tmp, 0);

    pthread_cond_destroy(&args.cond);
    pthread_mutex_destroy(&args.mtx);

    return args.joined ? 0 : ret;
}

static int _vxi11_connect(void *data, const char *address, int port, const char *name, int timeout)
{
    Create_LinkParms link_params;

    vxi11_data_t *vxi11_data = (vxi11_data_t *) data;

    // Set up client
    vxi11_data->rpc_client = clnt_create(address, DEVICE_CORE, DEVICE_CORE_VERSION, "tcp");
    if (vxi11_data->rpc_client == NULL)
        goto error_client;

    // Set up link
    link_params.clientId = (unsigned long) vxi11_data->rpc_client;
    link_params.lockDevice = 0; // No lock
    link_params.lock_timeout = 0;

    if (name == NULL)
        link_params.device = "inst0"; // Use default device name
    else
        link_params.device = (char *) name; // Use provided device name

    if (create_link_1(&link_params, &vxi11_data->link_resp, vxi11_data->rpc_client) != RPC_SUCCESS)
        goto error_link;

    return 0;

error_link:
    clnt_destroy(vxi11_data->rpc_client);
error_client:
    return -1;
}

static void *thread_vxi11_connect(void *ptr)
{
    int *status;
    thread_vxi11_connect_args_t *args = (thread_vxi11_connect_args_t *) ptr;

    status = malloc(sizeof(int)); // Automatically freed when thread is killed
    *status = _vxi11_connect(args->data, args->address, args->port, args->name, args->timeout);

    pthread_exit(status);
}

int vxi11_connect(void *data, const char *address, int port, const char *name, int timeout)
{
    struct timespec timeout_tv;
    pthread_t thread;
    int status;
    int *thread_status;

    thread_vxi11_connect_args_t args =
    {
        .data = data,
        .address = address,
        .port = port,
        .name = name,
        .timeout = timeout,
    };

    status = clock_gettime(CLOCK_REALTIME, &timeout_tv);
    if (status != 0)
    {
        error_printf("Error clock_gettime()\n");
        return -1;
    }

    // Convert timeout in ms to timespec
    timeout_tv.tv_sec += timeout / 1000;
    timeout_tv.tv_nsec += (timeout % 1000) * 1000;

    // Start thread that will perform the connect action
    status = pthread_create(&thread, NULL, thread_vxi11_connect, &args);
    if (status != 0)
    {
        error_printf("Error pthread_create()\n");
        return -1;
    }

    // Wait for thread to terminate or timeout
    status = _pthread_timedjoin(thread, (void *)&thread_status, &timeout_tv);
    if (status != 0)
    {
        // Timeout reached
        pthread_cancel(thread);

        // Wait for child thread to end before returning
        pthread_join(thread, NULL);

        return -1;
    }

    return *thread_status;
}

int vxi11_disconnect(void *data)
{
    Device_Error device_error;

    vxi11_data_t *vxi11_data = (vxi11_data_t *) data;

    destroy_link_1(&vxi11_data->link_resp.lid, &device_error, vxi11_data->rpc_client);
    clnt_destroy(vxi11_data->rpc_client);

    return 0;
}

int vxi11_send(void *data, const char *message, int length, int timeout)
{
    Device_WriteParms write_params;
    Device_WriteResp write_resp;

    vxi11_data_t *vxi11_data = (vxi11_data_t *) data;

    // Configure VXI11 write parameters
    write_params.lid = vxi11_data->link_resp.lid;
    write_params.lock_timeout = 0;
    write_params.io_timeout = timeout;
    write_params.flags = 0x9;
    write_params.data.data_len = length;
    write_params.data.data_val = (char *) message;

    // Send
    if (device_write_1(&write_params, &write_resp, vxi11_data->rpc_client) != RPC_SUCCESS)
        return -1;

    // Return number of bytes sent
    return write_resp.size;
}

int vxi11_receive(void *data, char *message, int length, int timeout)
{
    Device_ReadParms read_params;
    Device_ReadResp read_resp;
    int response_length = 0;
    int offset = 0;

    vxi11_data_t *vxi11_data = (vxi11_data_t *) data;

    // Configure VXI11 read parameters
    read_params.lid = vxi11_data->link_resp.lid;
    read_params.lock_timeout = 0;
    read_params.io_timeout = timeout;
    read_params.flags = 0;
    read_params.termChar = 0;
    read_params.requestSize = length;

    // Receive until done
    do
    {
        // Prepare for (repeated) read operation
        memset(&read_resp, 0, sizeof(read_resp));
        read_resp.data.data_val = message + offset;
        read_params.requestSize = length - offset;

        if (device_read_1(&read_params, &read_resp, vxi11_data->rpc_client) != RPC_SUCCESS)
            return -1;

        if (read_resp.error != 0)
        {
            if (read_resp.error == 15)
                error_printf("Read error (timeout)\n"); // Most common error explained
            else
                error_printf("Read error (response error code %d)\n", (int) read_resp.error);
            return -1;
        }

        if (read_resp.data.data_len > 0)
        {
            response_length += read_resp.data.data_len;

            // Return error if provided receive message buffer is too small
            if (response_length > length)
            {
                error_printf("Read error (receive message buffer too small)\n");
                return -1;
            }

            offset += read_resp.data.data_len;
        }

        // Stop if we have reached end of receive operation
        if ((read_resp.reason & RECEIVE_END_BIT) || (read_resp.reason & RECEIVE_TERM_CHAR_BIT))
            break;

    } while (read_resp.reason == 0);

    // Return number of bytes received
    return response_length;
}

int vxi11_lock(void *data)
{
    return 0;
}

int vxi11_unlock(void *data)
{
    return 0;
}

static xmlChar *get_element_value(xmlDocPtr doc, xmlChar *element)
{
    xmlNodePtr node;
    xmlChar *value = NULL;
    node = xmlDocGetRootElement(doc);
    node = node->xmlChildrenNode;
    while (node != NULL)
    {
        if ((!xmlStrcmp(node->name, element)))
        {
            value = xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            break;
        }
        node = node->next;
    }

    return value;
}

static int get_device_id(char *address, char *id, int timeout)
{
    vxi11_data_t data;
    int length;
    int device;

    device = vxi11_connect(&data, address, 0, NULL, timeout);
    if (device < 0)
        goto error_connect;

    length = vxi11_send(&data, ID_REQ_SCPI, strlen(ID_REQ_SCPI), timeout);
    if (length < 0)
        goto error_send;

    length = vxi11_receive(&data, id, ID_LENGTH_MAX, timeout);
    if (length < 0)
        goto error_receive;

    vxi11_disconnect(&data);

    // Terminate string
    id[length] = 0;

    if (length > 0)
    {
        // Strip newline
        if (id[length-1] == '\n')
            id[--length] = 0;

        // Strip carriage return
        if (id[length-1] == '\r')
            id[--length] = 0;
    } else
    {
        // Fallback - try retrieve ID via HTTP/XML
        char *request = ID_REQ_HTTP;
        char response[4096];
        char *response_xml;
        tcp_data_t tcp_data;
        xmlDocPtr doc;
        xmlChar *value;

        // Mute stderr temporarily
        freopen("/dev/null", "w", stderr);

        // Get XML identification file
        tcp_connect(&tcp_data, address, PORT_HTTP, NULL, timeout);
        tcp_send(&tcp_data, request, strlen(request), timeout);
        tcp_receive_wait(&tcp_data, response, 4096, timeout);
        tcp_disconnect(&tcp_data);

        // Find start of XML
        response_xml = strstr(response, "<?xml");
        if (response_xml == NULL)
            goto error_xml_response;

        // Read XML from memory
        doc = xmlRecoverMemory(response_xml, strlen(response_xml));
        if (doc == NULL)
            goto error_xml_read;

        // Assemble ID string
        id[0] = 0;

        value = get_element_value(doc, (xmlChar *)"Manufacturer");
        strcat(id, (char *) value);
        strcat(id, ",");
        xmlFree(value);

        value = get_element_value(doc, (xmlChar *)"Model");
        strcat(id, (char *) value);
        strcat(id, ",");
        xmlFree(value);

        value = get_element_value(doc, (xmlChar *)"SerialNumber");
        strcat(id, (char *) value);
        strcat(id, ",");
        xmlFree(value);

        value = get_element_value(doc, (xmlChar *)"FirmwareRevision");
        strcat(id, (char *) value);
        xmlFree(value);

        xmlFreeDoc(doc);
        xmlCleanupParser();
    }

    return 0;

error_receive:
error_send:
    vxi11_disconnect(&data);
error_connect:
    return -1;

error_xml_read:
error_xml_response:
    // Restore stderr
    freopen("/dev/tty", "w", stderr);
    return -1;
}

static int discover_devices(struct sockaddr_in *broadcast_addr, lxi_info_t *info, int timeout)
{
    int sockfd;
    struct sockaddr_in send_addr;
    struct sockaddr_in recv_addr;
    int broadcast = true;
    int count;
    char buffer[ID_LENGTH_MAX];
    char id[ID_LENGTH_MAX];
    struct timeval tv;
    socklen_t addrlen;

    // Create a socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1)
    {
        error_printf("Socket creation error");
        return -1;
    }

    // Set socket options - broadcast
    if((setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST,
                    &broadcast,sizeof (broadcast))) == -1)
    {
        error_printf("setsockopt - SO_SOCKET");
        goto socket_options_error;
    }

    // Set socket options - timeout
    tv.tv_sec = timeout / 1000;
    tv.tv_usec = (timeout % 1000) * 1000;
    if ((setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv))) == -1)
    {
        error_printf("setsockopt - SO_RCVTIMEO");
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
    recv_addr.sin_port = htons(PORT_RPC);

    // Broadcast RPC GETPORT message
    sendto(sockfd, rpc_GETPORT_msg, sizeof(rpc_GETPORT_msg), 0,
            (struct sockaddr*)&recv_addr, sizeof(recv_addr));

    addrlen = sizeof(recv_addr);

    // Go through received responses
    do
    {
        count = recvfrom(sockfd, buffer, ID_LENGTH_MAX, 0,
                (struct sockaddr*)&recv_addr, &addrlen);
        if (count > 0)
        {
            // Add device if an LXI ID string is returned
            char *address = inet_ntoa(recv_addr.sin_addr);
            if (get_device_id(address, id, timeout) == 0)
            {
                // Notify device found via callback
                if (info->device != NULL)
                    info->device(address, id);
            }
        }
    } while (count > 0);

    return 0;

socket_options_error:
    // Shutdown socket
    shutdown(sockfd, SHUT_RDWR);

    return -1;
}

int vxi11_discover(lxi_info_t *info, int timeout)
{
    struct sockaddr_in *broadcast_addr;
    struct ifaddrs *ifap;

    // Go through available broadcast addresses
    if (getifaddrs(&ifap) == 0)
    {
        struct ifaddrs *ifap_p = ifap;

        while (ifap_p)
        {
            if ((ifap_p->ifa_addr) && (ifap_p->ifa_broadaddr) && (ifap_p->ifa_addr->sa_family == AF_INET))
            {
                broadcast_addr = (struct sockaddr_in *) ifap_p->ifa_broadaddr;

                // Notify current broadcast address and network interface via callback
                if ((info->broadcast != NULL) && (ifap_p->ifa_name != NULL))
                    info->broadcast(inet_ntoa(broadcast_addr->sin_addr), ifap_p->ifa_name);

                // Find VXI11 devices via broadcast address
                discover_devices(broadcast_addr, info, timeout);

            }
            ifap_p = ifap_p->ifa_next;
        }
        freeifaddrs(ifap);
    }

    return 0;
}

int vxi11_discover_if(lxi_info_t *info, const char *ifname, int timeout)
{
    struct sockaddr_in *broadcast_addr;
    struct ifaddrs *ifap;
    int status = 0;

    // Go through available broadcast addresses
    if (getifaddrs(&ifap) == 0)
    {
        struct ifaddrs *ifap_p = ifap;

        while (ifap_p)
        {
            if ((ifap_p->ifa_addr) && (ifap_p->ifa_addr->sa_family == AF_INET) && (strcmp(ifap_p->ifa_name, ifname) == 0))
            {
                broadcast_addr = (struct sockaddr_in *) ifap_p->ifa_broadaddr;

                // Notify current broadcast address and network interface via callback
                if (info->broadcast != NULL)
                    info->broadcast(inet_ntoa(broadcast_addr->sin_addr), ifap_p->ifa_name);

                // Find VXI11 devices via broadcast address
                status = discover_devices(broadcast_addr, info, timeout);

            }
            ifap_p = ifap_p->ifa_next;
        }
        freeifaddrs(ifap);
    }

    return status;
}
