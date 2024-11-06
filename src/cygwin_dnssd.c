#include <stdio.h>
#include "cygwin/dns_sd.h"
#include <string.h>
#include <stdlib.h>
#include "lxi.h"
#include <dlfcn.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <netioapi.h>
#include <winsock.h>

typedef struct
{
    lxi_info_t *info;
    char *servicename;
    char *regtype;
} browse_data_t;



#define DNSSD_DLL "dnssd.dll"
#define LONG_TIME 100000000

lxi_service_t lxi_services[] = {
    {"_lxi._tcp.", "lxi"},
    {"_vxi-11._tcp.", "vxi-11"},
    {"_scpi-raw._tcp.", "scpi-raw"},
    {"_scpi-telnet._tcp.", "scpi-telnet"},
    {"_hislip._tcp.", "hislip"},
    {NULL, NULL}};


static void *hDnssdDLL = NULL;

typedef DNSServiceErrorType (DNSSD_API *DNSS_SERVICE_BROWSE) ( DNSServiceRef*,  DNSServiceFlags, uint32_t,  const char*,  const char*, DNSServiceBrowseReply, void*);
typedef DNSServiceErrorType  (DNSSD_API *DNSS_SERVICE_RESOLVE)  (DNSServiceRef*, DNSServiceFlags, uint32_t, const char*, const char*, const char*, DNSServiceResolveReply,  void*);
typedef DNSServiceErrorType  (DNSSD_API *DNS_SERVICE_PROCESS_RESULT) (DNSServiceRef);
typedef void  (DNSSD_API *DNS_SERVICE_REF_DEALLOCATE) (DNSServiceRef);
typedef dnssd_sock_t  (DNSSD_API *DNS_SERVICE_REF_SOCK_FD) (DNSServiceRef);

DNSS_SERVICE_BROWSE DNSServiceBrowse_Dll = NULL;
DNSS_SERVICE_RESOLVE DNSServiceResolve_Dll = NULL;
DNS_SERVICE_PROCESS_RESULT DNSServiceProcessResult_Dll = NULL;
DNS_SERVICE_REF_DEALLOCATE DNSServiceRefDeallocate_Dll = NULL;
DNS_SERVICE_REF_SOCK_FD DNSServiceRefSockFD_Dll = NULL;

#define DNSServiceBrowse DNSServiceBrowse_Dll
#define DNSServiceRefSockFD DNSServiceRefSockFD_Dll
#define DNSServiceProcessResult DNSServiceProcessResult_Dll
#define DNSServiceRefDeallocate DNSServiceRefDeallocate_Dll
#define DNSServiceResolve DNSServiceResolve_Dll


int load_cygwin_dnssd_dll()
{
    if(hDnssdDLL != NULL)
    {
        fprintf(stderr, "%s already loaded\n", DNSSD_DLL);
        return 0;
    }

    hDnssdDLL = dlopen(DNSSD_DLL, RTLD_LAZY);
    if (!hDnssdDLL) {
        fprintf(stderr, "%s\n", dlerror());
        return -1;
    }
    DNSServiceBrowse_Dll = (DNSS_SERVICE_BROWSE)dlsym(hDnssdDLL, "DNSServiceBrowse");
    if (DNSServiceBrowse_Dll == NULL)
    {
        fprintf(stderr, "%s[%d]: Function not found: %s\n", __FUNCTION__, __LINE__, "DNSServiceBrowse");
        goto error_load;
    }

    DNSServiceResolve_Dll = (DNSS_SERVICE_RESOLVE)dlsym(hDnssdDLL, "DNSServiceResolve");
    if (DNSServiceResolve_Dll == NULL)
    {
        fprintf(stderr, "%s[%d]: Function not found: %s\n", __FUNCTION__, __LINE__, "DNSServiceResolve");
        goto error_load;
    }

    DNSServiceProcessResult_Dll = (DNS_SERVICE_PROCESS_RESULT)dlsym(hDnssdDLL, "DNSServiceProcessResult");
    if (DNSServiceProcessResult_Dll == NULL)
    {
        fprintf(stderr, "%s[%d]: Function not found: %s\n", __FUNCTION__, __LINE__, "DNSServiceProcessResult");
        goto error_load;
    }

    DNSServiceRefDeallocate_Dll = (DNS_SERVICE_REF_DEALLOCATE)dlsym(hDnssdDLL, "DNSServiceRefDeallocate");
    if (DNSServiceRefDeallocate_Dll == NULL)
    {
        fprintf(stderr, "%s[%d]: Function not found: %s\n", __FUNCTION__, __LINE__, "DNSServiceRefDeallocate");
        goto error_load;
    }

    DNSServiceRefSockFD_Dll = (DNS_SERVICE_REF_SOCK_FD)dlsym(hDnssdDLL, "DNSServiceRefSockFD");
    if (DNSServiceRefSockFD_Dll == NULL){
        fprintf(stderr, "%s[%d]: Function not found: %s\n", __FUNCTION__, __LINE__, "DNSServiceRefSockF");
        goto error_load;
    }

    return 0;
error_load:
    dlclose(hDnssdDLL);
    DNSServiceBrowse_Dll = NULL;
    DNSServiceResolve_Dll = NULL;
    DNSServiceProcessResult_Dll = NULL;
    DNSServiceRefDeallocate_Dll = NULL;
    DNSServiceRefSockFD_Dll = NULL;
    hDnssdDLL = NULL;
   return -1;
}

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

    const TIMEVAL timeout = {(timeout_ms / 1000), (timeout_ms % 1000) * 1000};
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
            fprintf(stderr, "\n\n%s  SUCCESS\n\n", s->broadcast_type);
        }
        else if (result == 0)
        {
            fprintf(stderr, "\n\n%s  timed out...\n\n", s->broadcast_type);
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

int cygwin_dnssd_discover(lxi_info_t *info, int timeout_ms)
{
    if(load_cygwin_dnssd_dll()!=0)
    {
        fprintf(stderr, "error load dll %s\n", DNSSD_DLL);
        return -1;
    }
    browse_lxi_services(info, timeout_ms);

    return 0;
}
