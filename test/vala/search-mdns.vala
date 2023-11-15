// Example - Search for LXI instruments using mDNS discovery

int SearchMDNS()
{
  print("SearchMDNS test\n");

  var info = LXI.Info();

  // Initialize LXI library
  LXI.Init();

  // Set up search information callbacks
  info.service   = (address, id, service, port) => { print("  Found \"%s\" on address %s\n%s service on port %u\n", id, address, service, port); };
  info.broadcast = (address, iface)                       => { print("  Broadcasting on interface %s\n", iface); };

  print("Searching for LXI devices via mDNS - please wait...\n");

  // Search for LXI devices, 1 second timeout
  LXI.Discover(info, 1000, LXI.EDiscover.MDNS);

  return 0;
}
