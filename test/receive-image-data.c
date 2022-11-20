#include <stdio.h>
#include <string.h>
#include <lxi.h>

// Example - send SCPI screenshot command to instrument and receive image data using RAW protocol
//
// Tested with Rohde & Schwarz RTB2004 DSO
//
// Similar operation can be done using netcat (nc):
//
// time echo "HCOPy:DATA?" | nc -w1 192.168.0.157 5025 | dd bs=1 skip=7 of=image.png

int main()
{
    char response[65536*2];
    int device, timeout = 1000;
    char *command = "HCOPy:DATA?\n";
    int recv_bytes, sent_bytes, total_recv_bytes = 0;
    int buffer_size_free = sizeof(response);

    // Initialize LXI library
    lxi_init();

    // Connect LXI device
    device = lxi_connect("192.168.0.157", 5025, NULL, timeout, RAW);
    if (device < 0)
    {
        perror("Unable to connect\n");
        return -1;
    }

    // Send SCPI command to request screenshot image data
    sent_bytes = lxi_send(device, command, strlen(command), timeout);
    if (sent_bytes <= 0)
    {
        perror("Send failure\n");
        return -1;
    }

    // Keep receiving until all image data is received (meaning, until timeout)
    while (1)
    {
        recv_bytes = lxi_receive(device, response + total_recv_bytes, buffer_size_free, timeout);
        if (recv_bytes <= 0)
            break;
        total_recv_bytes += recv_bytes;
        buffer_size_free -= recv_bytes;
    }

    printf("Received %d bytes\n", total_recv_bytes);

    // Add code here to skip meta header and save only PNG image data to file

    // Disconnect
    lxi_disconnect(device);
}
