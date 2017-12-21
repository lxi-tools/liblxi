/*
 * Copyright (c) 2016-2017, Martin Lund
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
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
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

int vxi11_connect(void *data, char *address, int port, char *name, int timeout)
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
        link_params.device = name; // Use provided device name

    if (create_link_1(&link_params, &vxi11_data->link_resp, vxi11_data->rpc_client) != RPC_SUCCESS)
        goto error_link;

    return 0;

error_link:
    clnt_destroy(vxi11_data->rpc_client);
error_client:
    return -1;
}

int vxi11_disconnect(void *data)
{
    Device_Error device_error;

    vxi11_data_t *vxi11_data = (vxi11_data_t *) data;

    destroy_link_1(&vxi11_data->link_resp.lid, &device_error, vxi11_data->rpc_client);
    clnt_destroy(vxi11_data->rpc_client);

    return 0;
}

int vxi11_send(void *data, char *message, int length, int timeout)
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
    write_params.data.data_val = message;

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
        else
            return -1;

        // Stop if we have reached end of receive operation
        if ((read_resp.reason & RECEIVE_END_BIT) || (read_resp.reason & RECEIVE_TERM_CHAR_BIT))
            break;

    } while (read_resp.reason == 0);

    // Return number of bytes received
    return response_length;
}

int vxi11_lock(void *data)
{

}

int vxi11_unlock(void *data)
{

}

static int get_device_id(char *address, char *id, int timeout)
{
    vxi11_data_t data;
    int length;
    int device;
    FILE *fd;

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
            id[length-1] = 0;
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
        fd = freopen("/dev/null", "w", stderr);

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

        xmlChar *get_element_value(xmlDocPtr doc, xmlChar *element)
        {
            xmlNodePtr node;
            xmlChar *value;
            node = xmlDocGetRootElement(doc);
            node = node->xmlChildrenNode;
            while (node != NULL)
            {
                if ((!xmlStrcmp(node->name, element)))
                {
                    value = xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
                    return value;
                }
                node = node->next;
            }
        }

        // Assemble ID string
        id[0] = 0;

        value = get_element_value(doc, "Manufacturer");
        strcat(id, (char *) value);
        strcat(id, ",");
        xmlFree(value);

        value = get_element_value(doc, "Model");
        strcat(id, (char *) value);
        strcat(id, ",");
        xmlFree(value);

        value = get_element_value(doc, "SerialNumber");
        strcat(id, (char *) value);
        strcat(id, ",");
        xmlFree(value);

        value = get_element_value(doc, "FirmwareRevision");
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
    fd = freopen("/dev/tty", "w", stderr);
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
        perror("Socket creation error");
        return -1;
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

                // Find VXI11 devices via broadcast address
                status = discover_devices(broadcast_addr, info, timeout);

            }
            ifap_p = ifap_p->ifa_next;
        }
        freeifaddrs(ifap);
    }

    return 0;
}
