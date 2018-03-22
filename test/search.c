#include <stdio.h>
#include <lxi.h>

// Example - Search for LXI instruments using VXI-11 discovery

void broadcast(const char *address, const char *interface)
{
    printf("Broadcasting on interface %s\n", interface);
}

void device(const char *address, const char *id)
{
    printf(" Found %s on address %s\n", id, address);
}

int main()
{
    lxi_info_t info;

    // Initialize LXI library
    lxi_init();

    // Set up search information callbacks
    info.broadcast = &broadcast;
    info.device = &device;

    printf("Searching for LXI devices - please wait...\n\n");

    // Search for LXI devices, 1 second timeout
    lxi_discover(&info, 1000, DISCOVER_VXI11);

    return 0;
}
