/*
 * Copyright (c) 2017  Martin Lund
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
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>
#include <lxi.h>
#include "error.h"
#include "avahi.h"

static AvahiSimplePoll *simple_poll = NULL;
static AvahiServiceBrowser *sb[10] = {};
static lxi_info_t *lxi_info;
static int timeout_discover = 0;
static int count = 0;

static void avahi_resolve_callback(
        AvahiServiceResolver *r,
        AVAHI_GCC_UNUSED AvahiIfIndex interface,
        AVAHI_GCC_UNUSED AvahiProtocol protocol,
        AvahiResolverEvent event,
        const char *name,
        const char *type,
        const char *domain,
        const char *host_name,
        const AvahiAddress *address,
        uint16_t port,
        AvahiStringList *txt,
        AvahiLookupResultFlags flags,
        AVAHI_GCC_UNUSED void* userdata)
{
    assert(r);

    /* Called whenever a service has been resolved successfully or timed out */
    switch (event)
    {
        case AVAHI_RESOLVER_FAILURE:
            error_printf("(Resolver) Failed to resolve service '%s' of type '%s' in domain '%s': %s\n",
                    name, type, domain, avahi_strerror(avahi_client_errno(avahi_service_resolver_get_client(r))));
            break;
        case AVAHI_RESOLVER_FOUND:
            {
                char a[AVAHI_ADDRESS_STR_MAX] = "Unknown";
                char *service_type = "Unknown";

                // Pretty print service type
                if (strcmp(type, "_lxi._tcp") == 0)
                    service_type = "lxi";
                else if (strcmp(type, "_vxi-11._tcp") == 0)
                    service_type = "vxi-11";
                else if (strcmp(type, "_scpi-raw._tcp") == 0)
                    service_type = "scpi-raw";
                else if (strcmp(type, "_scpi-telnet._tcp") == 0)
                    service_type = "scpi-telnet";
                else if (strcmp(type, "_hislip._tcp") == 0)
                    service_type = "hislip";

                avahi_address_snprint(a, sizeof(a), address);
                lxi_info->service(a, (char *) name, service_type, port);
            }
    }
    avahi_service_resolver_free(r);
}

static void avahi_browse_callback(
        AvahiServiceBrowser *b,
        AvahiIfIndex interface,
        AvahiProtocol protocol,
        AvahiBrowserEvent event,
        const char *name,
        const char *type,
        const char *domain,
        AVAHI_GCC_UNUSED AvahiLookupResultFlags flags,
        void* userdata)
{
    AvahiClient *c = userdata;

    assert(b);

    /* Called whenever a new services becomes available on the LAN or is removed from the LAN */
    switch (event)
    {
        case AVAHI_BROWSER_FAILURE:
            error_printf("(Avahi) %s\n", avahi_strerror(avahi_client_errno(avahi_service_browser_get_client(b))));
            avahi_simple_poll_quit(simple_poll);
            return;
        case AVAHI_BROWSER_NEW:
            if (!(avahi_service_resolver_new(c, interface, protocol, name, type, domain, AVAHI_PROTO_INET, 0, avahi_resolve_callback, c)))
                error_printf("Failed to resolve service '%s': %s\n", name, avahi_strerror(avahi_client_errno(c)));
            break;
        case AVAHI_BROWSER_REMOVE:
        case AVAHI_BROWSER_ALL_FOR_NOW:
        case AVAHI_BROWSER_CACHE_EXHAUSTED:
            break;
    }
}

static void avahi_client_callback(AvahiClient *c, AvahiClientState state, AVAHI_GCC_UNUSED void * userdata)
{
    assert(c);

    /* Called whenever the client or server state changes */
    if (state == AVAHI_CLIENT_FAILURE)
    {
        error_printf("Server connection failure: %s\n", avahi_strerror(avahi_client_errno(c)));
        avahi_simple_poll_quit(simple_poll);
    }
}

static int create_service_browser(AvahiClient *client, char *service)
{
    if (!(sb[count++] = avahi_service_browser_new(client, AVAHI_IF_UNSPEC, AVAHI_PROTO_INET, service, NULL, 0, avahi_browse_callback, client)))
    {
        error_printf("Failed to create service browser: %s\n", avahi_strerror(avahi_client_errno(client)));
        return 1;
    }
    return 0;
}

int avahi_discover_(lxi_info_t *info, int timeout)
{
    AvahiClient *client = NULL;
    int status = 1;
    int error;

    /* Setup callback structure and timeout for avahi service callback */
    lxi_info = info;
    timeout_discover = timeout * 1000;

    /* Allocate main loop object */
    if (!(simple_poll = avahi_simple_poll_new()))
    {
        error_printf("Failed to create simple poll object.\n");
        goto fail;
    }

    /* Allocate a new client */
    client = avahi_client_new(avahi_simple_poll_get(simple_poll), 0, avahi_client_callback, NULL, &error);

    /* Check whether creating the client object succeeded */
    if (!client)
    {
        error_printf("Failed to create client: %s\n", avahi_strerror(error));
        goto fail;
    }

    /* Create the service browsers */
    if (create_service_browser(client, "_lxi._tcp"))
        goto fail_sb;
    if (create_service_browser(client, "_vxi-11._tcp"))
        goto fail_sb;
    if (create_service_browser(client, "_scpi-raw._tcp"))
        goto fail_sb;
    if (create_service_browser(client, "_scpi-telnet._tcp"))
        goto fail_sb;
    if (create_service_browser(client, "_hislip._tcp"))
        goto fail_sb;

    /* Run the main loop */
    avahi_simple_poll_loop(simple_poll);
    status = 0;

fail_sb:
    while (--count)
        avahi_service_browser_free(sb[count]);
fail:
    if (client)
        avahi_client_free(client);
    if (simple_poll)
        avahi_simple_poll_free(simple_poll);
    return status;
}

typedef struct
{
    lxi_info_t *info;
    int timeout;
} data_t;

void *discover(void *userdata)
{
    data_t *data = userdata;

    avahi_discover_(data->info, data->timeout);
}

int avahi_discover(lxi_info_t *info, int timeout)
{
    struct timespec ts;
    pthread_t thread;
    data_t data;
    int status;

    data.info = info;
    data.timeout = timeout;

    if (clock_gettime(CLOCK_REALTIME, &ts) == -1)
    {
       error_printf("Failed to read system clock\n");
       return -1;
    }

    ts.tv_sec += timeout / 1000;

    status = pthread_create(&thread, NULL, discover, (void*) &data);
    if (status != 0)
    {
        error_printf("Failed to create discover thread\n");
        return -1;
    }

    pthread_timedjoin_np(thread, NULL, &ts);
    if (status != 0)
    {
        error_printf("Timeout waiting for discover thread\n");
        return -1;
    }
}
