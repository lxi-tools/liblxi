#include <stdio.h>
#include <dns_sd.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netdb.h>
#include <sys/select.h>
#include "lxi.h"

typedef struct
{
    lxi_info_t *info;
    char *servicename;
    char *regtype;
} browse_data_t;

void resolve_ip_address(const char *hostname, uint16_t port, char *ip_address)
{
    struct addrinfo hints, *servinfo, *p;
    char port_str[6];
    sprintf(port_str, "%u", port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    getaddrinfo(hostname, port_str, &hints, &servinfo);
    for (p = servinfo; p != NULL; p = p->ai_next)
    {
        struct sockaddr_in *ipv4 = (struct sockaddr_in *)p->ai_addr;
        inet_ntop(p->ai_family, &(ipv4->sin_addr), ip_address, INET_ADDRSTRLEN);
        break;
    }

    freeaddrinfo(servinfo);
}

void resolve_callback(
    DNSServiceRef sdRef,
    DNSServiceFlags flags,
    uint32_t interfaceIndex,
    DNSServiceErrorType errorCode,
    const char *fullname,
    const char *hosttarget,
    uint16_t port,
    uint16_t txtLen,
    const unsigned char *txtRecord,
    void *context)
{
    if (errorCode != kDNSServiceErr_NoError)
    {
        fprintf(stderr, "Bonjour Resolve Callback, DNSServiceErrorType %d", errorCode);
        return;
    }

    char ip_address[INET_ADDRSTRLEN];
    resolve_ip_address(hosttarget, port, ip_address);

    browse_data_t *browse_data = (browse_data_t *)context;
    lxi_info_t *info = (lxi_info_t *)browse_data->info;

    // Pretty print service type
    const char *service_type = "Unknown";
    for (lxi_service_t *s = lxi_services; s->broadcast_type != NULL; s++)
    {
        if (strcmp(browse_data->regtype, s->broadcast_type) == 0)
        {
            service_type = s->service_name;
            break;
        }
    }

    info->service(ip_address, browse_data->servicename, service_type, ntohs(port));

    free(browse_data->regtype);
    free(browse_data->servicename);
    free(browse_data);
}

void browse_callback(
    DNSServiceRef service,
    DNSServiceFlags flags,
    uint32_t interfaceIndex,
    DNSServiceErrorType errorCode,
    const char *serviceName,
    const char *regtype,
    const char *replyDomain,
    void *context)
{
    if (errorCode != kDNSServiceErr_NoError)
    {
        fprintf(stderr, "Bonjour browse Callback, DNSServiceErrorType %d", errorCode);
        return;
    }

    char if_name[IF_NAMESIZE];
    if_indextoname((unsigned int)interfaceIndex, if_name);

    lxi_info_t *info = (lxi_info_t *)context;
    info->broadcast(serviceName, if_name);

    browse_data_t *browse_data = (browse_data_t *)malloc(sizeof(browse_data_t));
    if (!browse_data)
    {
        printf("Browse data struct memory allocation failed\n");
        return;
    }

    browse_data->info = info;
    browse_data->servicename = strdup(serviceName);
    browse_data->regtype = strdup(regtype);

    DNSServiceRef resolveService;
    DNSServiceErrorType error;

    error = DNSServiceResolve(&resolveService, 0, interfaceIndex, serviceName, regtype, replyDomain, resolve_callback, browse_data);
    if (error != kDNSServiceErr_NoError)
    {
        fprintf(stderr, "DNSServiceResolve() failed: %d", error);
        return;
    }

    error = DNSServiceProcessResult(resolveService);
    if (error != kDNSServiceErr_NoError)
    {
        fprintf(stderr, "DNSServiceProcessResult() failed: %d", error);
        return;
    }

    DNSServiceRefDeallocate(resolveService);
}

void browse_lxi_services(lxi_info_t *info, int timeout_ms)
{
    DNSServiceRef service;
    DNSServiceErrorType error;

    struct timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;           // convert milliseconds to seconds
    timeout.tv_usec = (timeout_ms % 1000) * 1000; // remainder in microseconds

    for (lxi_service_t *s = lxi_services; s->broadcast_type != NULL; s++)
    {
        error = DNSServiceBrowse(&service, 0, 0, s->broadcast_type, NULL, browse_callback, info);
        if (error != kDNSServiceErr_NoError)
        {
            fprintf(stderr, "DNSServiceBrowse() failed: %d\n", error);
            return;
        }

        int dns_sd_fd = DNSServiceRefSockFD(service);
        int nfds = dns_sd_fd + 1;
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(dns_sd_fd, &readfds);

        int result = select(nfds, &readfds, (fd_set *)NULL, (fd_set *)NULL, &timeout);

        if (result > 0)
        {

            error = DNSServiceProcessResult(service);

            if (error != kDNSServiceErr_NoError)
            {
                fprintf(stderr, "DNSServiceProcessResult() failed: %d\n", error);
                return;
            }
        }
        else if (result == 0)
        {
            fprintf(stderr, "DNSServiceProcessResult() timed out\n");
            continue;
        }
        else
        {
            fprintf(stderr, "select() failed\n");
            return;
        }
    }
    DNSServiceRefDeallocate(service);
}

void bonjour_discover(lxi_info_t *info, int timeout_ms)
{
    browse_lxi_services(info, timeout_ms);
}
