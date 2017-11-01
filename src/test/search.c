#include <stdio.h>
#include <lxi.h>

// Example - Search for LXI instruments (VXI11 discovery)

void broadcast(char *address, char *interface)
{
    printf("Broadcasting on interface %s\n", interface);
}

void device(char *address, char *id)
{
    printf(" Found %s on address %s\n", id, address);
}

int main()
{
    struct lxi_info_t info;

    // Initialize LXI library
    lxi_init();

    // Set up search information callbacks
    info.broadcast = &broadcast;
    info.device = &device;

    printf("Searching for LXI devices - please wait...\n\n");

    // Search for LXI devices, 1 second timeout
    lxi_discover(&info, 1000);

    return 0;
}
