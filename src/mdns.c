/*
 * Copyright (c) 2022  Valentin Belot
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
 
#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS 1
#endif

#include <stdio.h>

#include <errno.h>

#ifdef _WIN32
#include <winsock2.h>
#include <iphlpapi.h>
#define sleep(x) Sleep(x * 1000)
#else
#include <netdb.h>
#include <ifaddrs.h>
#include <net/if.h>
#endif

#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include "mdns.h"
#include "lxi.h"
#include "error.h"

static char entrybuffer[256];
static char namebuffer[256];

static struct sockaddr_in service_address_ipv4;
static struct sockaddr_in6 service_address_ipv6;

static int has_ipv4;
static int has_ipv6;

typedef struct {
    lxi_info_t* info;
    char ip[INET6_ADDRSTRLEN];
    char device_name[256];
    char service_type[256];
    int mdns_port;
    int service_info_found;
} device_info_t;

// Data for our service including the mDNS records
typedef struct {
    mdns_string_t service;
    mdns_string_t hostname;
    mdns_string_t service_instance;
    mdns_string_t hostname_qualified;
    struct sockaddr_in address_ipv4;
    struct sockaddr_in6 address_ipv6;
    int port;
    mdns_record_t record_ptr;
    mdns_record_t record_srv;
    mdns_record_t record_a;
    mdns_record_t record_aaaa;
    mdns_record_t txt_record[2];
} service_t;

static mdns_string_t
ipv4_address_to_string(char* buffer, size_t capacity, const struct sockaddr_in* addr,
                       size_t addrlen) {
    char host[NI_MAXHOST] = {0};
    char service[NI_MAXSERV] = {0};
    int ret = getnameinfo((const struct sockaddr*)addr, (socklen_t)addrlen, host, NI_MAXHOST,
                          service, NI_MAXSERV, NI_NUMERICSERV | NI_NUMERICHOST);
    int len = 0;
    if (ret == 0) {
        //        if (addr->sin_port != 0)
        //            len = snprintf(buffer, capacity, "%s:%s", host, service);
        //        else
        len = snprintf(buffer, capacity, "%s", host);
    }
    if (len >= (int)capacity)
        len = (int)capacity - 1;
    mdns_string_t str;
    str.str = buffer;
    str.length = len;
    return str;
}

static mdns_string_t
ipv6_address_to_string(char* buffer, size_t capacity, const struct sockaddr_in6* addr,
                       size_t addrlen) {
    char host[NI_MAXHOST] = {0};
    char service[NI_MAXSERV] = {0};
    int ret = getnameinfo((const struct sockaddr*)addr, (socklen_t)addrlen, host, NI_MAXHOST,
                          service, NI_MAXSERV, NI_NUMERICSERV | NI_NUMERICHOST);
    int len = 0;
    if (ret == 0) {
        //        if (addr->sin6_port != 0)
        //            len = snprintf(buffer, capacity, "[%s]:%s", host, service);
        //        else
        len = snprintf(buffer, capacity, "%s", host);
    }
    if (len >= (int)capacity)
        len = (int)capacity - 1;
    mdns_string_t str;
    str.str = buffer;
    str.length = len;
    return str;
}

static mdns_string_t
ip_address_to_string(char* buffer, size_t capacity, const struct sockaddr* addr, size_t addrlen) {
    if (addr->sa_family == AF_INET6)
        return ipv6_address_to_string(buffer, capacity, (const struct sockaddr_in6*)addr, addrlen);
    return ipv4_address_to_string(buffer, capacity, (const struct sockaddr_in*)addr, addrlen);
}

static int
port_to_int(const struct sockaddr* addr) {
    if (addr->sa_family == AF_INET6){
        if (((const struct sockaddr_in6*)addr)->sin6_port != 0)
            return htons(((const struct sockaddr_in6*)addr)->sin6_port);
        else
            return 0;
    }
    if (((const struct sockaddr_in*)addr)->sin_port != 0)
        return htons(((const struct sockaddr_in*)addr)->sin_port);
    return 0;
}

// Callback handling parsing answers to queries sent
static int
query_callback(int sock, const struct sockaddr* from, size_t addrlen, mdns_entry_type_t entry,
               uint16_t query_id, uint16_t rtype, uint16_t rclass, uint32_t ttl, const void* data,
               size_t size, size_t name_offset, size_t name_length, size_t record_offset,
               size_t record_length, void* user_data) {
    (void)sizeof(sock);
    (void)sizeof(query_id);
    (void)sizeof(name_length);
    (void)sizeof(user_data);
    device_info_t* service_query = user_data;
    char ip_s[INET6_ADDRSTRLEN];
    
    // Always reset namebuffer else string will contain content from the previous callback if shorter
    memset(namebuffer, 0, sizeof(namebuffer));
    
    ip_address_to_string(ip_s, sizeof(ip_s), from, addrlen);
    
    mdns_string_t entrystr =
    mdns_string_extract(data, size, &name_offset, entrybuffer, sizeof(entrybuffer));
    if ((rtype == MDNS_RECORDTYPE_PTR) && (entry == MDNS_ENTRYTYPE_ANSWER)) {
        mdns_string_t namestr = mdns_record_parse_ptr(data, size, record_offset, record_length,
                                                      namebuffer, sizeof(namebuffer));
        
        if (strstr((MDNS_STRING_FORMAT(entrystr)), "_services") != NULL){
            if ((strstr((MDNS_STRING_FORMAT(namestr)), "_lxi._tcp") != NULL)
                || (strstr((MDNS_STRING_FORMAT(namestr)), "_vxi-11._tcp") != NULL)
                || (strstr((MDNS_STRING_FORMAT(namestr)), "_scpi-raw._tcp") != NULL)
                || (strstr((MDNS_STRING_FORMAT(namestr)), "_scpi-telnet._tcp") != NULL)
                || (strstr((MDNS_STRING_FORMAT(namestr)), "_hislip._tcp") != NULL)){
                
                // Instrument service detected, now query the PTR record to get additional information as port and device name
                // We could skip the discovery step and directly query the PTR records but detection would only work on .local domain
                
                size_t internal_capacity = 2048;
                void* internal_buffer = malloc(internal_capacity);
                mdns_query_t query[1];
                static device_info_t discovered_service;
                int query_ptr_info_id;
                int res;
                
                // Prepare the query
                query[0].name = (MDNS_STRING_FORMAT(namestr));
                query[0].type = MDNS_RECORDTYPE_PTR;
                query[0].length = strlen(query[0].name);
                
                query_ptr_info_id = mdns_multiquery_send(sock, query, 1, internal_buffer, internal_capacity, 0);
                if (query_ptr_info_id < 0)
                    error_printf("Failed to send mDNS query: %s\n", strerror(errno));
                
                // Copy string as we need to keep the value itself and not a pointer as callback will be called several times
                strncpy(discovered_service.ip, ip_s, strlen(ip_s));
                strncpy(discovered_service.service_type,(MDNS_STRING_FORMAT(namestr)), strlen((MDNS_STRING_FORMAT(namestr))));
                discovered_service.ip[strlen(ip_s)] = '\0';
                discovered_service.service_type[strlen((MDNS_STRING_FORMAT(namestr)))] = '\0';
                
                // Usually mdns_port is always 5353
                discovered_service.mdns_port = port_to_int(from);
                discovered_service.info = service_query->info;
                discovered_service.service_info_found = -1;
                
                do {
                    struct timeval timeout;
                    timeout.tv_sec = 5;
                    timeout.tv_usec = 0;
                    
                    fd_set readfs;
                    FD_ZERO(&readfs);
                    FD_SET(sock, &readfs);
                    
                    res = select(sock+1, &readfs, 0, 0, &timeout);
                    if (res > 0 && FD_ISSET(sock, &readfs)){
                        mdns_query_recv(sock, internal_buffer, internal_capacity, query_callback, &discovered_service, query_ptr_info_id);
                    }
                } while (res > 0 && discovered_service.service_info_found < 0);
                
                free(internal_buffer);
                
                // If service was discovered without information report it anyway (but port and name are unknown)
                if (discovered_service.service_info_found < 0){
                    const char* service_type;
                    // Pretty print service type
                    if (strstr(discovered_service.service_type, "_lxi._tcp") != NULL)
                        service_type = "lxi";
                    else if (strstr(discovered_service.service_type, "_vxi-11._tcp") != NULL)
                        service_type = "vxi-11";
                    else if (strstr(discovered_service.service_type, "_scpi-raw._tcp") != NULL)
                        service_type = "scpi-raw";
                    else if (strstr(discovered_service.service_type, "_scpi-telnet._tcp") != NULL)
                        service_type = "scpi-telnet";
                    else if (strstr(discovered_service.service_type, "_hislip._tcp") != NULL)
                        service_type = "hislip";
                    
                    error_printf("Could not find service information.");
                    service_query->info->service(ip_s, "Unknown", service_type, 0);
                }
            }
        }
        else {
            // Receive device name (PTR record)
            if (service_query->service_info_found < 0){
                if ((strcmp(service_query->ip, ip_s) == 0) && (service_query->mdns_port == port_to_int(from))){
                    // Notify that device name was found
                    strncpy(service_query->device_name, namebuffer, strlen(namebuffer)-strlen(service_query->device_name));
                    service_query->device_name[strlen(namebuffer)-strlen(service_query->service_type)-1] = '\0';
                    service_query->service_info_found = 0;
                }
            }
        }
    } else if ((rtype == MDNS_RECORDTYPE_SRV) && (service_query->service_info_found == 0)) {
            
            mdns_record_srv_t srv = mdns_record_parse_srv(data, size, record_offset, record_length,
                                                          namebuffer, sizeof(namebuffer));

            // Only proceed query if it comes from same origin as the service query (prevent receiving unsolicited answser from another device)
            if ((strcmp(service_query->ip, ip_s) == 0) && (service_query->mdns_port == port_to_int(from))){
                // Notify that SRV was found to stop receiving
                service_query->service_info_found = 1;
                if (service_query->info->service != NULL) {

                    const char* service_type;
                    // Pretty print service type
                    if (strstr(service_query->service_type, "_lxi._tcp") != NULL)
                        service_type = "lxi";
                    else if (strstr(service_query->service_type, "_vxi-11._tcp") != NULL)
                        service_type = "vxi-11";
                    else if (strstr(service_query->service_type, "_scpi-raw._tcp") != NULL)
                        service_type = "scpi-raw";
                    else if (strstr(service_query->service_type, "_scpi-telnet._tcp") != NULL)
                        service_type = "scpi-telnet";
                    else if (strstr(service_query->service_type, "_hislip._tcp") != NULL)
                        service_type = "hislip";
                    
                    // Service callback
                    service_query->info->service(ip_s, service_query->device_name, service_type, srv.port);
                }
            }
        }
    return 0;
}

// Open sockets for sending one-shot multicast queries from an ephemeral port
static int
open_client_sockets(int* sockets, int max_sockets, int port, lxi_info_t *info, int timeout) {
    // When sending, each socket can only send to one network interface
    // Thus we need to open one socket for each interface and address family
    int num_sockets = 0;
    
#ifdef _WIN32
    
    IP_ADAPTER_ADDRESSES* adapter_address = 0;
    ULONG address_size = 8000;
    unsigned int ret;
    unsigned int num_retries = 4;
    do {
        adapter_address = (IP_ADAPTER_ADDRESSES*)malloc(address_size);
        ret = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_ANYCAST, 0,
                                   adapter_address, &address_size);
        if (ret == ERROR_BUFFER_OVERFLOW) {
            free(adapter_address);
            adapter_address = 0;
            address_size *= 2;
        } else {
            break;
        }
    } while (num_retries-- > 0);
    
    if (!adapter_address || (ret != NO_ERROR)) {
        free(adapter_address);
        error_printf("Failed to get network adapter addresses\n");
        return num_sockets;
    }
    
    int first_ipv4 = 1;
    int first_ipv6 = 1;
    for (PIP_ADAPTER_ADDRESSES adapter = adapter_address; adapter; adapter = adapter->Next) {
        if (adapter->TunnelType == TUNNEL_TYPE_TEREDO)
            continue;
        if (adapter->OperStatus != IfOperStatusUp)
            continue;
        
        for (IP_ADAPTER_UNICAST_ADDRESS* unicast = adapter->FirstUnicastAddress; unicast;
             unicast = unicast->Next) {
            if (unicast->Address.lpSockaddr->sa_family == AF_INET) {
                struct sockaddr_in* saddr = (struct sockaddr_in*)unicast->Address.lpSockaddr;
                if ((saddr->sin_addr.S_un.S_un_b.s_b1 != 127) ||
                    (saddr->sin_addr.S_un.S_un_b.s_b2 != 0) ||
                    (saddr->sin_addr.S_un.S_un_b.s_b3 != 0) ||
                    (saddr->sin_addr.S_un.S_un_b.s_b4 != 1)) {
                    int log_addr = 0;
                    if (first_ipv4) {
                        service_address_ipv4 = *saddr;
                        first_ipv4 = 0;
                        log_addr = 1;
                    }
                    has_ipv4 = 1;
                    if (num_sockets < max_sockets) {
                        saddr->sin_port = htons((unsigned short)port);
                        int sock = mdns_socket_open_ipv4(saddr);
                        if (sock >= 0) {
                            sockets[num_sockets++] = sock;
                            log_addr = 1;
                        } else {
                            log_addr = 0;
                        }
                    }
                    if (log_addr) {
                        // Notify current broadcast address and network interface via callback
                        if (info->broadcast != NULL){
                            info->broadcast(inet_ntoa(saddr->sin_addr), adapter->FriendlyName);
                        }
                    }
                }
            } else if (unicast->Address.lpSockaddr->sa_family == AF_INET6) {
                struct sockaddr_in6* saddr = (struct sockaddr_in6*)unicast->Address.lpSockaddr;
                // Ignore link-local addresses
                if (saddr->sin6_scope_id)
                    continue;
                static const unsigned char localhost[] = {0, 0, 0, 0, 0, 0, 0, 0,
                    0, 0, 0, 0, 0, 0, 0, 1};
                static const unsigned char localhost_mapped[] = {0, 0, 0,    0,    0,    0, 0, 0,
                    0, 0, 0xff, 0xff, 0x7f, 0, 0, 1};
                if ((unicast->DadState == NldsPreferred) &&
                    memcmp(saddr->sin6_addr.s6_addr, localhost, 16) &&
                    memcmp(saddr->sin6_addr.s6_addr, localhost_mapped, 16)) {
                    int log_addr = 0;
                    if (first_ipv6) {
                        service_address_ipv6 = *saddr;
                        first_ipv6 = 0;
                        log_addr = 1;
                    }
                    has_ipv6 = 1;
                    if (num_sockets < max_sockets) {
                        saddr->sin6_port = htons((unsigned short)port);
                        int sock = mdns_socket_open_ipv6(saddr);
                        if (sock >= 0) {
                            sockets[num_sockets++] = sock;
                            log_addr = 1;
                        } else {
                            log_addr = 0;
                        }
                    }
                    if (log_addr) {
                        char addr_s[INET6_ADDRSTRLEN];
                        // Notify current broadcast address and network interface via callback
                        inet_ntop(AF_INET6, &saddr->sin6_addr, addr_s, INET6_ADDRSTRLEN);
                        if (info->broadcast != NULL){
                            info->broadcast(addr_s, adapter->FriendlyName);
                        }
                    }
                }
            }
        }
    }
    
    free(adapter_address);
    
#else
    
    struct ifaddrs* ifaddr = 0;
    struct ifaddrs* ifa = 0;
    
    if (getifaddrs(&ifaddr) < 0)
        error_printf("Unable to get interface addresses\n");
    
    int first_ipv4 = 1;
    int first_ipv6 = 1;
    for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr)
            continue;
        if (!(ifa->ifa_flags & IFF_UP) || !(ifa->ifa_flags & IFF_MULTICAST))
            continue;
        if ((ifa->ifa_flags & IFF_LOOPBACK) || (ifa->ifa_flags & IFF_POINTOPOINT))
            continue;
        
        if (ifa->ifa_addr->sa_family == AF_INET) {
            struct sockaddr_in* saddr = (struct sockaddr_in*)ifa->ifa_addr;
            if (saddr->sin_addr.s_addr != htonl(INADDR_LOOPBACK)) {
                int log_addr = 0;
                if (first_ipv4) {
                    service_address_ipv4 = *saddr;
                    first_ipv4 = 0;
                    log_addr = 1;
                }
                has_ipv4 = 1;
                if (num_sockets < max_sockets) {
                    saddr->sin_port = htons(port);
                    int sock = mdns_socket_open_ipv4(saddr);
                    if (sock >= 0) {
                        sockets[num_sockets++] = sock;
                        log_addr = 1;
                    } else {
                        log_addr = 0;
                    }
                }
                if (log_addr) {
                    // Notify current broadcast address and network interface via callback
                    if (info->broadcast != NULL){
                        info->broadcast(inet_ntoa(saddr->sin_addr), ifa->ifa_name);
                    }
                }
            }
        } else if (ifa->ifa_addr->sa_family == AF_INET6) {
            struct sockaddr_in6* saddr = (struct sockaddr_in6*)ifa->ifa_addr;
            // Ignore link-local addresses
            if (saddr->sin6_scope_id)
                continue;
            static const unsigned char localhost[] = {0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 1};
            static const unsigned char localhost_mapped[] = {0, 0, 0,    0,    0,    0, 0, 0,
                0, 0, 0xff, 0xff, 0x7f, 0, 0, 1};
            if (memcmp(saddr->sin6_addr.s6_addr, localhost, 16) &&
                memcmp(saddr->sin6_addr.s6_addr, localhost_mapped, 16)) {
                int log_addr = 0;
                if (first_ipv6) {
                    service_address_ipv6 = *saddr;
                    first_ipv6 = 0;
                    log_addr = 1;
                }
                has_ipv6 = 1;
                if (num_sockets < max_sockets) {
                    saddr->sin6_port = htons(port);
                    int sock = mdns_socket_open_ipv6(saddr);
                    if (sock >= 0) {
                        sockets[num_sockets++] = sock;
                        log_addr = 1;
                    } else {
                        log_addr = 0;
                    }
                }
                if (log_addr) {
                    char addr_s[INET6_ADDRSTRLEN];
                    // Notify current broadcast address and network interface via callback
                    inet_ntop(AF_INET6, &saddr->sin6_addr, addr_s, INET6_ADDRSTRLEN);
                    if (info->broadcast != NULL){
                        info->broadcast(addr_s, ifa->ifa_name);
                    }
                }
            }
        }
    }
    
    freeifaddrs(ifaddr);
    
#endif
    
    return num_sockets;
}

// Send a DNS-SD query
static int
send_dns_sd(lxi_info_t *info, int timeout_user){
    int sockets[32];
    int num_sockets = open_client_sockets(sockets, sizeof(sockets) / sizeof(sockets[0]), 0, info, timeout_user);
    if (num_sockets <= 0) {
        error_printf("Failed to open any client sockets\n");
        return -1;
    }
    
    //printf("Opened %d socket%s for DNS-SD\n", num_sockets, num_sockets > 1 ? "s" : "");
    //printf("Sending DNS-SD discovery\n");
    
    for (int isock = 0; isock < num_sockets; ++isock) {
        if (mdns_discovery_send(sockets[isock])){
            error_printf("Failed to send DNS-DS discovery: %s\n", strerror(errno));
        }
    }
    
    size_t capacity = 2048;
    void* buffer = malloc(capacity);
    static device_info_t devinfo;
    devinfo.mdns_port = 0;
    devinfo.info = info;
    
    void* user_data = &devinfo;
    size_t records;
    
    int res;
    do {
        struct timeval timeout;
        timeout.tv_sec = timeout_user / 1000;
        timeout.tv_usec = 1000 * (timeout_user % 1000);
        
        int nfds = 0;
        fd_set readfs;
        FD_ZERO(&readfs);
        for (int isock = 0; isock < num_sockets; ++isock) {
            if (sockets[isock] >= nfds)
                nfds = sockets[isock] + 1;
            FD_SET(sockets[isock], &readfs);
        }
        
        records = 0;
        res = select(nfds, &readfs, 0, 0, &timeout);
        if (res > 0) {
            for (int isock = 0; isock < num_sockets; ++isock) {
                if (FD_ISSET(sockets[isock], &readfs)) {
                    records += mdns_discovery_recv(sockets[isock], buffer, capacity, query_callback,
                                                   user_data);
                }
            }
        }
    } while (res > 0);
    
    free(buffer);
    
    for (int isock = 0; isock < num_sockets; ++isock)
        mdns_socket_close(sockets[isock]);
    //printf("Closed socket%s\n", num_sockets ? "s" : "");
    
    return 0;
}

int mdns_discover(lxi_info_t *info, int timeout){
    
#ifdef _WIN32
    
    WORD versionWanted = MAKEWORD(1, 1);
    WSADATA wsaData;
    if (WSAStartup(versionWanted, &wsaData)) {
        error_printf("Failed to initialize WinSock\n");
        return -1;
    }
    
#endif
    int ret;
    ret = send_dns_sd(info, timeout);
    
#ifdef _WIN32
    WSACleanup();
#endif
    
    return 0;
}
