#include <stdio.h>
#include <lxi.h>

// Example - Search for LXI instruments using mDNS discovery

static void service_callback(const char *address, const char *id, const char *service, int port)
{
  printf("  Found \"%s\" on address %s\n    %s service on port %u\n\n", id, address, service, port);
}

static void broadcast_callback(const char *address, const char *interface)
{
  printf("Broadcasting on interface %s\n\n", interface);
}

int main()
{
  lxi_info_t info;

  // Initialize LXI library
  lxi_init();

  // Set up search information callbacks
  info.service = &service_callback;
  info.broadcast = &broadcast_callback;

  printf("Searching for LXI devices - please wait...\n\n");

  // Search for LXI devices, 1 second timeout
  lxi_discover(&info, 1000, DISCOVER_MDNS);

  return 0;
}
