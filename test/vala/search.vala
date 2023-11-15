// Example - Search for LXI instruments using VXI-11 discovery

int Search()
{
  print("Search test\n");

  var info = LXI.Info();

  // Initialize LXI library
  LXI.Init();

  // Set up search information callbacks
  info.broadcast = (address, iface) => { print("  Broadcasting on interface %s\n", iface); };
  info.device    = (address, id)    => { print("  Found %s on address %s\n", id, address); };

  print("Searching for LXI devices - please wait...\n");

  // Search for LXI devices, 1 second timeout
  LXI.Discover(info, 3000, LXI.EDiscover.VXI11);

  return 0;
}
