#include <stdio.h>
#include <string.h>
#include <lxi.h>

// Example - send SCPI to instrument using VXI11 protocol

int main()
{
    char response[65536];
    int device, length, timeout = 3000;
    char *command = "*IDN?";

    // Initialize LXI library
    lxi_init();

    // Connect LXI device
    device = lxi_connect("10.42.0.42", 0, NULL, timeout, VXI11);

    // Send SCPI command
    lxi_send(device, command, strlen(command), timeout);

    // Wait for response
    lxi_receive(device, response, sizeof(response), timeout);

    printf("%s\n", response);

    // Disconnect
    lxi_disconnect(device);
}
