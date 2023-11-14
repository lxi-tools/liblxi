// Example - send SCPI to instrument using VXI-11 protocol

int SCPI()
{
  uint8[] response = new uint8[65536];
  int device, timeout = 3000;
  string command = "*IDN?";

  // Initialize LXI library
  LXI.Init();

  // Connect LXI device
  device = LXI.Connect("10.42.0.42", 0, null, timeout, LXI.EProtocol.VXI11);
  if (device < 0)
  {
    error("Unable to connect\n");
  }

  // Send SCPI command
  LXI.Send(device, command, timeout);

  // Wait for response
  LXI.Receive(device, response, timeout);

  print("%s\n", (string) response);

  // Disconnect
  LXI.Disconnect(device);
  
  return 0;
}
