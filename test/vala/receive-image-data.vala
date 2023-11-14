// Example - send SCPI screenshot command to instrument and receive image data using RAW protocol
//
// Tested with Rohde & Schwarz RTB2004 DSO
//
// Similar operation can be done using netcat (nc):
//
// time echo "HCOPy:DATA?" | nc -w1 192.168.0.157 5025 | dd bs=1 skip=7 of=image.png

int ReciveImageData()
{
  string response = "";
  int device, timeout = 1000;
  string command = "HCOPy:DATA?\n";

  // Initialize LXI library
  LXI.Init();

  // Connect LXI device
  device = LXI.Connect("192.168.0.157", 5025, null, timeout, LXI.EProtocol.RAW);
  if (device < 0)
  {
    error("Unable to connect\n");
  }

  // Send SCPI command to request screenshot image data
  var sent_bytes = LXI.Send(device, command, timeout);
  if (sent_bytes <= 0)
  {
    error("Send failure\n");
  }

  // Keep receiving until all image data is received (meaning, until timeout)
  int total_recv_bytes = 0;
  while (true)
  {
    uint8[] response_part = new uint8[65536*2];

    var recv_bytes = LXI.Receive(device, response_part, timeout);
    if (recv_bytes <= 0)
      break;

    total_recv_bytes += recv_bytes;

    // \todo I'm guessing this should work
    response += (string) response_part;
  }

  print("Received %d bytes\n", total_recv_bytes);

  // Add code here to skip meta header and save only PNG image data to file

  // Disconnect
  LXI.Disconnect(device);

  return 0;
}
