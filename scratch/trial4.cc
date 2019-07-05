#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("BBR_Example");

bool firstCwnd = true;
bool firstSshThr = true;
bool firstRtt = true;
bool firstRto = true;
bool firstMinRtt = true;
Ptr<OutputStreamWrapper> cWndStream;
Ptr<OutputStreamWrapper> ssThreshStream;
Ptr<OutputStreamWrapper> rttStream;
Ptr<OutputStreamWrapper> rtoStream;
Ptr<OutputStreamWrapper> inFlightStream;
uint32_t cWndValue;
uint32_t ssThreshValue;
//bool m_state = false;

/*
static void ChangeDataRate ()
{
      Config::Set ("/NodeList/0/DeviceList/0/DataRate", StringValue ("20Mbps"));
      Config::Set ("/NodeList/1/DeviceList/0/DataRate", StringValue ("20Mbps"));
}
*/
static void
CwndTracer (uint32_t oldval, uint32_t newval)
{
  if (firstCwnd)
    {
      *cWndStream->GetStream () << "0.0 " << oldval << std::endl;
      firstCwnd = false;
    }
  *cWndStream->GetStream () << Simulator::Now ().GetSeconds () << " " << newval << std::endl;
  cWndValue = newval;

  if (!firstSshThr)
    {
      *ssThreshStream->GetStream () << Simulator::Now ().GetSeconds () << " " << ssThreshValue << std::endl;
    }
}

static void
SsThreshTracer (uint32_t oldval, uint32_t newval)
{
  if (firstSshThr)
    {
      *ssThreshStream->GetStream () << "0.0 " << oldval << std::endl;
      firstSshThr = false;
    }
  *ssThreshStream->GetStream () << Simulator::Now ().GetSeconds () << " " << newval << std::endl;
  ssThreshValue = newval;

  if (!firstCwnd)
    {
      *cWndStream->GetStream () << Simulator::Now ().GetSeconds () << " " << cWndValue << std::endl;
    }
}

static void
RtoTracer (Time oldval, Time newval)
{
  if (firstRto)
    {
      *rtoStream->GetStream () << "0.0 " << oldval.GetSeconds () << std::endl;
      firstRto = false;
    }
  *rtoStream->GetStream () << Simulator::Now ().GetSeconds () << " " << newval.GetSeconds () << std::endl;
}

static void
InFlightTracer (uint32_t old, uint32_t inFlight)
{
  *inFlightStream->GetStream () << Simulator::Now ().GetSeconds () << " " << inFlight << std::endl;
}

static void
RttTracer (Time oldval, Time newval)
{
  if (firstRtt)
    {
      *rttStream->GetStream () << "0.0 " << oldval.GetSeconds () << std::endl;
      firstRtt = false;
    }
  *rttStream->GetStream () << Simulator::Now ().GetSeconds () << " " << newval.GetSeconds () << std::endl;
}

static void
TraceCwnd (std::string cwnd_tr_file_name)
{
  AsciiTraceHelper ascii;
  cWndStream = ascii.CreateFileStream (cwnd_tr_file_name.c_str ());
  Config::ConnectWithoutContext ("/NodeList/0/$ns3::TcpL4Protocol/SocketList/0/CongestionWindow", MakeCallback (&CwndTracer));
}

static void
TraceSsThresh (std::string ssthresh_tr_file_name)
{
  AsciiTraceHelper ascii;
  ssThreshStream = ascii.CreateFileStream (ssthresh_tr_file_name.c_str ());
  Config::ConnectWithoutContext ("/NodeList/0/$ns3::TcpL4Protocol/SocketList/0/SlowStartThreshold", MakeCallback (&SsThreshTracer));
}

static void
TraceRtt (std::string rtt_tr_file_name)
{
  AsciiTraceHelper ascii;
  rttStream = ascii.CreateFileStream (rtt_tr_file_name.c_str ());
  Config::ConnectWithoutContext ("/NodeList/0/$ns3::TcpL4Protocol/SocketList/0/RTT", MakeCallback (&RttTracer));
}

static void
TraceRto (std::string rto_tr_file_name)
{
  AsciiTraceHelper ascii;
  rtoStream = ascii.CreateFileStream (rto_tr_file_name.c_str ());
  Config::ConnectWithoutContext ("/NodeList/0/$ns3::TcpL4Protocol/SocketList/0/RTO", MakeCallback (&RtoTracer));
}

static void
TraceInFlight (std::string &in_flight_file_name)
{
  AsciiTraceHelper ascii;
  inFlightStream = ascii.CreateFileStream (in_flight_file_name.c_str ());
  Config::ConnectWithoutContext ("/NodeList/0/$ns3::TcpL4Protocol/SocketList/0/BytesInFlight", MakeCallback (&InFlightTracer));
}

int
main (int argc, char *argv[])
{

  double minRto = 0.2;
  uint32_t initialCwnd = 10;
  //ouble error_p = 0.00;
  double start_time = 0.01;
  double stop_time = 100;
  double data_mbytes = 0;
  uint32_t mtu_bytes = 536;
  std::string bandwidth = "1Mbps";
  std::string delay = "40ms";
  std::string transport_prot = "TcpBbr";

  time_t rawtime;
  struct tm * timeinfo;
  char buffer[80];

  time (&rawtime);
  timeinfo = localtime(&rawtime);

  strftime(buffer,sizeof(buffer),"%d-%m-%Y-%I-%M-%S",timeinfo);
  std::string currentTime (buffer);

  LogComponentEnable ("UdpEchoClientApplication", LOG_LEVEL_INFO);
  LogComponentEnable ("UdpEchoServerApplication", LOG_LEVEL_INFO);

  CommandLine cmd;
  cmd.AddValue ("bandwidth", "Bottleneck bandwidth", bandwidth);
  cmd.AddValue ("delay", "Bottleneck delay", delay);
  cmd.AddValue ("mtu", "Size of IP packets to send in bytes", mtu_bytes);
  cmd.AddValue ("data", "Number of Megabytes of data to transmit", data_mbytes);
  //cmd.AddValue ("error_p", "Packet error rate", error_p);
  cmd.AddValue ("start_time", "Start Time", start_time);
  cmd.AddValue ("stop_time", "Stop Time", stop_time);
  //cmd.AddValue ("scenario", "Scenario", scenario);
  cmd.AddValue ("initialCwnd", "Initial Cwnd", initialCwnd);
  cmd.AddValue ("minRto", "Minimum RTO", minRto);
  cmd.AddValue ("transport_prot", "Transport protocol to use: TcpNewReno, "
                "TcpHybla, TcpHighSpeed, TcpHtcp, TcpVegas, TcpScalable, TcpVeno, "
                "TcpBic, TcpYeah, TcpIllinois, TcpWestwood, TcpWestwoodPlus, TcpLedbat, "
                "TcpLp, TcpBbr", transport_prot);
  cmd.Parse (argc, argv);

  // Calculate the ADU size
  Header* temp_header = new Ipv4Header ();
  uint32_t ip_header = temp_header->GetSerializedSize ();
  delete temp_header;

  temp_header = new TcpHeader ();
  uint32_t tcp_header = temp_header->GetSerializedSize ();
  delete temp_header;
  uint32_t tcp_adu_size = mtu_bytes - 20 - (ip_header + tcp_header);

  Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (tcp_adu_size));
  
  //Time::SetResolution (Time::NS);

  Config::SetDefault ("ns3::TcpSocket::InitialCwnd", UintegerValue (initialCwnd));
  Config::SetDefault ("ns3::TcpSocketBase::MinRto", TimeValue (Seconds (minRto)));

  transport_prot = std::string ("ns3::") + transport_prot;
  // Select TCP variant
  if (transport_prot.compare ("ns3::TcpWestwoodPlus") == 0)
    { 
      // TcpWestwoodPlus is not an actual TypeId name; we need TcpWestwood here
      Config::SetDefault ("ns3::TcpL4Protocol::SocketType", TypeIdValue (TcpWestwood::GetTypeId ()));
      // the default protocol type in ns3::TcpWestwood is WESTWOOD
      Config::SetDefault ("ns3::TcpWestwood::ProtocolType", EnumValue (TcpWestwood::WESTWOODPLUS));
    }
  else
    {
      TypeId tcpTid;
      NS_ABORT_MSG_UNLESS (TypeId::LookupByNameFailSafe (transport_prot, &tcpTid), "TypeId " << transport_prot << " not found");
      Config::SetDefault ("ns3::TcpL4Protocol::SocketType", TypeIdValue (TypeId::LookupByName (transport_prot)));
    }


  NodeContainer nodes;
  nodes.Create (2);

  PointToPointHelper pointToPoint;
  pointToPoint.SetDeviceAttribute  ("DataRate", StringValue (bandwidth));
  pointToPoint.SetChannelAttribute ("Delay", StringValue (delay));
  //pointToPoint.SetDeviceAttribute ("ReceiveErrorModel", PointerValue (&error_model));

  NetDeviceContainer devices;
  devices = pointToPoint.Install (nodes);

  // Install Stack
  InternetStackHelper stack;
  stack.Install (nodes);

  // Assign IP Addresses
  Ipv4AddressHelper address;
  address.SetBase ("10.1.1.0", "255.255.255.0");

  Ipv4InterfaceContainer interfaces = address.Assign (devices);

  uint16_t port = 8080;
  AddressValue remoteAddress (InetSocketAddress (interfaces.GetAddress (1), port));
  OnOffHelper onOffHelper ("ns3::TcpSocketFactory", Address ());
  onOffHelper.SetConstantRate (DataRate ("1Mbps"));
  onOffHelper.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=1]"));
  onOffHelper.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0]"));
  onOffHelper.SetAttribute ("Remote", remoteAddress);

  ApplicationContainer sourceApp;

  sourceApp = onOffHelper.Install (nodes.Get (0));
  sourceApp.Start (Seconds (start_time));
  sourceApp.Stop (Seconds (stop_time - 1));
 
 //
 // Create a PacketSinkApplication and install it on node 1
 //
   PacketSinkHelper sink ("ns3::TcpSocketFactory",
                          InetSocketAddress (Ipv4Address::GetAny (), port));
   ApplicationContainer sinkApps = sink.Install (nodes.Get (1));
   sinkApps.Start (Seconds (0.0));
  sinkApps.Stop (Seconds (100.0));

  // Set up the acutal simulation
  std::string dir = "results/" + transport_prot.substr(5, transport_prot.length()) + "/" + currentTime + "/";
  std::string dirToSave = "mkdir -p " + dir;
  system (dirToSave.c_str ());
  Simulator::Schedule (Seconds (start_time + 0.000001), &TraceCwnd, dir + "cwnd.data");
  Simulator::Schedule (Seconds (start_time + 0.000001), &TraceSsThresh, dir + "ssth.data");
  Simulator::Schedule (Seconds (start_time + 0.000001), &TraceRtt, dir + "rtt.data");
  Simulator::Schedule (Seconds (start_time + 0.000001), &TraceRto, dir + "rto.data");
  Simulator::Schedule (Seconds (start_time + 0.000001), &TraceInFlight, dir + "inflight.data");

  //Simulator::Schedule (Seconds(20) , &ChangeDataRate);

  pointToPoint.EnablePcapAll (dir + "p", nodes.Get (0));


  Simulator::Stop (Seconds (stop_time + 1));

  Simulator::Run ();
  Simulator::Destroy ();
  return 0;

}
