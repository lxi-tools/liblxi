[CCode (cheader_filename = "lxi.h")]
namespace LXI {
  [CCode (cname = "lxi_protocol_t",  cprefix="", has_type_id = false)]
  public enum EProtocol {
    VXI11,
    RAW,
    HISLIP
  }

  [CCode (cname = "lxi_discover_t", cprefix="DISCOVER_", has_type_id = false)]
  public enum EDiscover {
    VXI11,
    MDNS
  }
  
  [CCode (cname = "lxi_service_t", destroy_function = "", has_type_id = false)]
  public struct Service {
    [CCode (cname = "broadcast_type")]
    string BroadcastType;
    [CCode (cname = "service_name")]
    string ServiceName;
  }

  [CCode (cname = "broadcast", has_target=false)]
  public delegate void BroadcastDelegate (string address, string interface);
  [CCode (cname = "device", has_target=false)]
  public delegate void DeviceDelegate (string address, string id);
  [CCode (cname = "service", has_target=false)]
  public delegate void ServiceDelegate (string address, string id, string service, int port);

  [CCode (cname = "lxi_info_t", destroy_function = "", has_type_id = false)]
  public struct Info {  
    [CCode (cname = "broadcast")]
    BroadcastDelegate broadcast;
    [CCode (cname = "device")]
    DeviceDelegate device;
    [CCode (cname = "service")]
    ServiceDelegate service;
  }
  
  [CCode (cname = "lxi_init")]
  public int Init();
  
  [CCode (cname = "lxi_discover")]
  public int Discover(Info info, int timeout, EDiscover type);
  
  [CCode (cname = "lxi_discover_if")]
  public int DiscoverIf(Info info, string ifname, int timeout, EDiscover type);
  
  [CCode (cname = "lxi_connect")]
  public int Connect(string address, int port, string? name, int timeout, EProtocol protocol);
  
  [CCode (cname = "lxi_send")]
  private int _Send(int device, uint8[] message, int timeout);
  [CCode (cname = "vala_lxi_send")]
  public int Send(int device, string message, int timeout) {
    return _Send(device, message.data, timeout);
  }
  
  [CCode (cname = "lxi_receive")]
  public int Receive(int device, uint8[] message, int timeout);
  
  [CCode (cname = "lxi_disconnect")]
  public int Disconnect(int device);

  /* \todo this doesn´t work
  [CCode (cname = "lxi_services", array_null_terminated = true)]
  public Service[] Services;
  */
}
