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
#include <pthread.h>
#include <lxi.h>
#include "error.h"
#include "session.h"
#include "vxi11.h"
#include "tcp.h"
#include "mdns.h"

static struct session_t session[SESSIONS_MAX] = {};
static pthread_mutex_t session_mutex = PTHREAD_MUTEX_INITIALIZER;

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

int lxi_connect(char *address, int port, char *name, int timeout, lxi_protocol_t protocol)
{
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
        error_printf("Too many active sessions!\n");
        goto error_session;
    }

    // Set up protocol backend
    switch (protocol)
    {
        case VXI11:
            session[i].connect = vxi11_connect;
            session[i].send = vxi11_send;
            session[i].receive = vxi11_receive;
            session[i].disconnect = vxi11_disconnect;
            session[i].data = malloc(sizeof(vxi11_data_t));
            break;
        case RAW:
            session[i].connect = tcp_connect;
            session[i].send = tcp_send;
            session[i].receive = tcp_receive;
            session[i].disconnect = tcp_disconnect;
            session[i].data = malloc(sizeof(tcp_data_t));
            break;
        case HISLIP:
            // Error: Not yet supported
            goto error_protocol;
            break;
        default:
            // Error: Unknown protocol
            goto error_protocol;
            break;
    }

    // Connect
    if (session[i].connect(session[i].data, address, port, name, timeout) != 0)
        goto error_connect;

    session[i].allocated = true;
    session[i].connected = true;

    pthread_mutex_unlock(&session_mutex);

    // Return session handle
    return i;

error_connect:
error_protocol:
error_session:
    pthread_mutex_unlock(&session_mutex);
    return LXI_ERROR;
}

int lxi_disconnect(int device)
{
    if (device > SESSIONS_MAX)
        return LXI_ERROR;

    pthread_mutex_lock(&session_mutex);

    // Disconnect
    if (session[device].connected)
        session[device].disconnect(session[device].data);

    // Free resources
    free(session[device].data);

    session[device].connected = false;
    session[device].allocated = false;

    pthread_mutex_unlock(&session_mutex);

    return LXI_OK;
}

int lxi_send(int device, char *message, int length, int timeout)
{
    int bytes_sent;

    // Send
    bytes_sent = session[device].send(session[device].data, message, length, timeout);
    if (bytes_sent < 0)
        return LXI_ERROR;

    // Return number of bytes sent
    return bytes_sent;
}

int lxi_receive(int device, char *message, int length, int timeout)
{
    int bytes_received;

    // Receive
    bytes_received = session[device].receive(session[device].data, message, length, timeout);
    if (bytes_received < 0)
        return LXI_ERROR;

    // Return number of bytes received
    return bytes_received;
}

int lxi_discover(lxi_info_t *info, int timeout, lxi_discover_t type)
{
    switch (type)
    {
        case DISCOVER_VXI11:
            vxi11_discover(info, timeout);
            break;
        case DISCOVER_MDNS:
            mdns_discover(info, timeout);
            break;
        default:
            error_printf("Unknown discover type (%d)\n", type);
            return LXI_ERROR;
    }

    return LXI_OK;
}
