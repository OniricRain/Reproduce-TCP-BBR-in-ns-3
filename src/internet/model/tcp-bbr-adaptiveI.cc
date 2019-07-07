/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2018 NITK Surathkal
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors: Vivek Jain <jain.vivek.anand@gmail.com>
 *          Viyom Mittal <viyommittal@gmail.com>
 *          Mohit P. Tahiliani <tahiliani@nitk.edu.in>
 */

#include "tcp-bbr-adaptiveI.h"
#include "ns3/log.h"
#include "ns3/tcp-socket-base.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("TcpBbrAdaptiveI");
NS_OBJECT_ENSURE_REGISTERED (TcpBbrAdaptiveI);

double pacing_gainI = 1.25;
double drainI = 0.75;
std::vector<double> pacing_gain_cycleI = {pacing_gainI, drainI, 1, 1, 1, 1, 1, 1} ;
std::vector<uint64_t> past_valuesI = {};
bool isNewCycleI; // NEW
uint16_t my_counterI = 0;

TypeId
TcpBbrAdaptiveI::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TcpBbrAdaptiveI")
    .SetParent<TcpCongestionOps> ()
    .AddConstructor<TcpBbrAdaptiveI> ()
    .SetGroupName ("Internet")
    .AddAttribute ("HighGain",
                   "Value of high gain",
                   DoubleValue (2.89),
                   MakeDoubleAccessor (&TcpBbrAdaptiveI::m_highGain),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("BwWindowLength",
                   "Length of bandwidth windowed filter",
                   UintegerValue (10),
                   MakeUintegerAccessor (&TcpBbrAdaptiveI::m_bandwidthWindowLength),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("RttWindowLength",
                   "Length of bandwidth windowed filter",
                   TimeValue (Seconds (10)),
                   MakeTimeAccessor (&TcpBbrAdaptiveI::m_rtPropFilterLen),
                   MakeTimeChecker ())
    .AddAttribute ("ProbeRttDuration",
                   "Length of bandwidth windowed filter",
                   TimeValue (MilliSeconds (200)),
                   MakeTimeAccessor (&TcpBbrAdaptiveI::m_probeRttDuration),
                   MakeTimeChecker ())
  ;
  return tid;
}

//NEW: compute mean
double
TcpBbrAdaptiveI::computeMean(std::vector<uint64_t> vec) {
    double sum = 0;
    for(double a : vec)
        sum += a;
    return (double)sum/vec.size();
}
//NEW: compute standard deviation
double 
TcpBbrAdaptiveI::computeStd(std::vector<uint64_t> vec) {
    double mean = computeMean(vec);
    double temp = 0;
    for(double a :vec)
        temp += (a-mean)*(a-mean);
    double var = (double)temp/(vec.size()-1);
    return std::sqrt(var);
}

TcpBbrAdaptiveI::TcpBbrAdaptiveI ()
  : TcpCongestionOps ()
{
  NS_LOG_FUNCTION (this);
  m_uv = CreateObject<UniformRandomVariable> ();
}

TcpBbrAdaptiveI::TcpBbrAdaptiveI (const TcpBbrAdaptiveI &sock)
  : TcpCongestionOps (sock),
    m_bandwidthWindowLength (sock.m_bandwidthWindowLength),
    m_pacingGain (sock.m_pacingGain),
    m_cWndGain (sock.m_cWndGain),
    m_highGain (sock.m_highGain),
    m_isPipeFilled (sock.m_isPipeFilled),
    m_minPipeCwnd (sock.m_minPipeCwnd),
    m_roundCount (sock.m_roundCount),
    m_roundStart (sock.m_roundStart),
    m_nextRoundDelivered (sock.m_nextRoundDelivered),
    m_probeRttDuration (sock.m_probeRttDuration),
    m_probeRtPropStamp (sock.m_probeRtPropStamp),
    m_probeRttDoneStamp (sock.m_probeRttDoneStamp),
    m_probeRttRoundDone (sock.m_probeRttRoundDone),
    m_packetConservation (sock.m_packetConservation),
    m_priorCwnd (sock.m_priorCwnd),
    m_idleRestart (sock.m_idleRestart),
    m_targetCWnd (sock.m_targetCWnd),
    m_fullBandwidth (sock.m_fullBandwidth),
    m_fullBandwidthCount (sock.m_fullBandwidthCount),
    m_rtProp (Time::Max ()),
    m_sendQuantum (sock.m_sendQuantum),
    m_cycleStamp (sock.m_cycleStamp),
    m_cycleIndex (sock.m_cycleIndex),
    m_rtPropExpired (sock.m_rtPropExpired),
    m_rtPropFilterLen (sock.m_rtPropFilterLen),
    m_rtPropStamp (sock.m_rtPropStamp),
    m_isInitialized (sock.m_isInitialized)
{
  NS_LOG_FUNCTION (this);
  m_uv = CreateObject<UniformRandomVariable> ();
}

int64_t
TcpBbrAdaptiveI::AssignStreams (int64_t stream)
{
  NS_LOG_FUNCTION (this << stream);
  m_uv->SetStream (stream);
  return 1;
}

void
TcpBbrAdaptiveI::InitRoundCounting ()
{
  NS_LOG_FUNCTION (this);
  m_nextRoundDelivered = 0;
  m_roundStart = false;
  m_roundCount = 0;
}

void
TcpBbrAdaptiveI::InitFullPipe ()
{
  NS_LOG_FUNCTION (this);
  m_isPipeFilled = false;
  m_fullBandwidth = 0;
  m_fullBandwidthCount = 0;
}

void
TcpBbrAdaptiveI::InitPacingRate (Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this << tcb);

  if (!tcb->m_pacing)
    {
      NS_LOG_WARN ("BBR must use pacing");
      tcb->m_pacing = true;
    }
  Time rtt = tcb->m_lastRtt != Time::Max () ? tcb->m_lastRtt : MilliSeconds (1);
  DataRate nominalBandwidth (tcb->m_initialCWnd * tcb->m_segmentSize * 8 / rtt.GetSeconds ());
  tcb->m_currentPacingRate = DataRate (m_pacingGain * nominalBandwidth.GetBitRate ());
}

void
TcpBbrAdaptiveI::EnterStartup ()
{
  NS_LOG_FUNCTION (this);
  SetBbrState (BbrMode_t::BBR_STARTUP);
  m_pacingGain = m_highGain;
  m_cWndGain = m_highGain;
}

void
TcpBbrAdaptiveI::HandleRestartFromIdle (Ptr<TcpSocketState> tcb, const RateSample * rs)
{
  NS_LOG_FUNCTION (this << tcb << rs);
  if (tcb->m_bytesInFlight == 0 && rs->m_isAppLimited)
    {
      m_idleRestart = true;
      if (m_state == BbrMode_t::BBR_PROBE_BW)
        {
          SetPacingRate (tcb, 1);
        }
    }
}

void
TcpBbrAdaptiveI::SetPacingRate (Ptr<TcpSocketState> tcb, double gain)
{
  NS_LOG_FUNCTION (this << tcb << gain);
  DataRate rate (gain * m_maxBwFilter.GetBest ().GetBitRate ());
  rate = std::min (rate, tcb->m_maxPacingRate);
  if (m_isPipeFilled || rate > tcb->m_currentPacingRate)
    {
      tcb->m_currentPacingRate = rate;
    }
}

uint32_t
TcpBbrAdaptiveI::InFlight (Ptr<TcpSocketState> tcb, double gain)
{
  NS_LOG_FUNCTION (this << tcb << gain);
  if (m_rtProp == Time::Max ())
    {
      return tcb->m_initialCWnd * tcb->m_segmentSize;
    }
  double quanta = 3 * m_sendQuantum;
  double estimatedBdp = m_maxBwFilter.GetBest () * m_rtProp / 8.0;
  return gain * estimatedBdp + quanta;
}

void
TcpBbrAdaptiveI::AdvanceCyclePhase ()
{
  NS_LOG_FUNCTION (this);
  m_cycleStamp = Simulator::Now ();
  m_cycleIndex = (m_cycleIndex + 1) % pacing_gain_cycleI.size();
  m_pacingGain = pacing_gain_cycleI [m_cycleIndex];
}

bool
TcpBbrAdaptiveI::IsNextCycle () // NEW to control if a new cycle has started
{
  NS_LOG_FUNCTION (this);
  if (m_cycleIndex == 0) // check on the index of the cycle
  {
    return true;
  }
  else
  {
    return false;
  }
}

bool
TcpBbrAdaptiveI::IsNextCyclePhase (Ptr<TcpSocketState> tcb, const struct RateSample * rs)
{
  NS_LOG_FUNCTION (this << tcb << rs);
  bool isFullLength = (Simulator::Now () - m_cycleStamp) > m_rtProp;
  if (m_pacingGain == 1)
    {
      return isFullLength;
    }
  else if (m_pacingGain > 1)
    {
      return isFullLength && (rs->m_packetLoss > 0 || tcb->m_priorInFlight >= InFlight (tcb, m_pacingGain));
    }
  else
    {
      return isFullLength || tcb->m_priorInFlight <= InFlight (tcb, 1);
    }
}

void
TcpBbrAdaptiveI::CheckCyclePhase (Ptr<TcpSocketState> tcb, const struct RateSample * rs)
{
  NS_LOG_FUNCTION (this << tcb << rs);
  if (m_state == BbrMode_t::BBR_PROBE_BW && IsNextCyclePhase (tcb, rs))
    {
      AdvanceCyclePhase ();
      isNewCycleI = IsNextCycle();
    }
}

void
TcpBbrAdaptiveI::CheckFullPipe (const struct RateSample * rs)
{
  NS_LOG_FUNCTION (this << rs);
  if (m_isPipeFilled || !m_roundStart || rs->m_isAppLimited)
    {
      return;
    }

  /* Check if Bottleneck bandwidth is still growing*/
  if (m_maxBwFilter.GetBest ().GetBitRate () >= m_fullBandwidth.GetBitRate () * 1.25)
    {
      m_fullBandwidth = m_maxBwFilter.GetBest ();
      m_fullBandwidthCount = 0;
      return;
    }

  m_fullBandwidthCount++;
  if (m_fullBandwidthCount >= 3)
    {
      NS_LOG_DEBUG ("Pipe filled");
      m_isPipeFilled = true;
    }
}

void
TcpBbrAdaptiveI::EnterDrain ()
{
  NS_LOG_FUNCTION (this);
  SetBbrState (BbrMode_t::BBR_DRAIN);
  m_pacingGain = 1.0 / m_highGain;
  m_cWndGain = m_highGain;
}

void
TcpBbrAdaptiveI::EnterProbeBW ()
{
  NS_LOG_FUNCTION (this);
  SetBbrState (BbrMode_t::BBR_PROBE_BW);
  m_pacingGain = 1;
  m_cWndGain = 2;
  m_cycleIndex = pacing_gain_cycleI.size() - 1 - (int) m_uv->GetValue (0, pacing_gain_cycleI.size());// Modification: If pacing_gain_cycleI size goes under 8 this still holds
  AdvanceCyclePhase ();
  // IsNextCycle(); ??? FIXME:Consider if legit
}

void
TcpBbrAdaptiveI::CheckDrain (Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this << tcb);
  if (m_state == BbrMode_t::BBR_STARTUP && m_isPipeFilled)
    {
      EnterDrain ();
    }

  if (m_state == BbrMode_t::BBR_DRAIN && tcb->m_bytesInFlight <= InFlight (tcb, 1))
    {
      EnterProbeBW ();
    }
}

void
TcpBbrAdaptiveI::UpdateRTprop (Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this << tcb);
  m_rtPropExpired = Simulator::Now () > (m_rtPropStamp + m_rtPropFilterLen);
  if (tcb->m_lastRtt >= Seconds (0) && (tcb->m_lastRtt <= m_rtProp || m_rtPropExpired))
    {
      m_rtProp = tcb->m_lastRtt;
      m_rtPropStamp = Simulator::Now ();
    }
}

void
TcpBbrAdaptiveI::EnterProbeRTT ()
{
  NS_LOG_FUNCTION (this);
  SetBbrState (BbrMode_t::BBR_PROBE_RTT);
  m_pacingGain = 1;
  m_cWndGain = 1;
}

void
TcpBbrAdaptiveI::SaveCwnd (Ptr<const TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this << tcb);
  if (tcb->m_congState != TcpSocketState::CA_RECOVERY && m_state != BbrMode_t::BBR_PROBE_RTT)
    {
      m_priorCwnd = tcb->m_cWnd;
    }
  else
    {
      m_priorCwnd = std::max (m_priorCwnd, tcb->m_cWnd.Get ());
    }
}

void
TcpBbrAdaptiveI::RestoreCwnd (Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this << tcb);
  tcb->m_cWnd = std::max (m_priorCwnd, tcb->m_cWnd.Get ());
}

void
TcpBbrAdaptiveI::ExitProbeRTT ()
{
  NS_LOG_FUNCTION (this);
  if (m_isPipeFilled)
    {
      EnterProbeBW ();
    }
  else
    {
      EnterStartup ();
    }
}

void
TcpBbrAdaptiveI::HandleProbeRTT (Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this << tcb);
  tcb->m_appLimited = (tcb->m_delivered + tcb->m_bytesInFlight) ? : 1;

  if (m_probeRttDoneStamp == Seconds (0) && tcb->m_bytesInFlight <= m_minPipeCwnd)
    {
      m_probeRttDoneStamp = Simulator::Now () + m_probeRttDuration;
      m_probeRttRoundDone = false;
      m_nextRoundDelivered = tcb->m_delivered;
    }
  else if (m_probeRttDoneStamp != Seconds (0))
    {
      if (m_roundStart)
        {
          m_probeRttRoundDone = true;
        }
      if (m_probeRttRoundDone && Simulator::Now () > m_probeRttDoneStamp)
        {
          m_rtPropStamp = Simulator::Now ();
          RestoreCwnd (tcb);
          ExitProbeRTT ();
        }
    }
}

void
TcpBbrAdaptiveI::CheckProbeRTT (Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this << tcb);
  NS_LOG_DEBUG (Simulator::Now () << "WhichState " << WhichState (m_state) << " m_rtPropExpired " << m_rtPropExpired << " !m_idleRestart " << !m_idleRestart);
  if (m_state != BbrMode_t::BBR_PROBE_RTT && m_rtPropExpired && !m_idleRestart)
    {
      EnterProbeRTT ();
      SaveCwnd (tcb);
      m_probeRttDoneStamp = Seconds (0);
    }

  if (m_state == BbrMode_t::BBR_PROBE_RTT)
    {
      HandleProbeRTT (tcb);
    }

  m_idleRestart = false;
}

void
TcpBbrAdaptiveI::SetSendQuantum (Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this << tcb);
  m_sendQuantum = 1 * tcb->m_segmentSize;
/*TODO
  Since TSO can't be implemented in ns-3
  if (tcb->m_currentPacingRate < DataRate ("1.2Mbps"))
    {
      m_sendQuantum = 1 * tcb->m_segmentSize;
    }
  else if (tcb->m_currentPacingRate < DataRate ("24Mbps"))
    {
      m_sendQuantum  = 2 * tcb->m_segmentSize;
    }
  else
    {
      m_sendQuantum = std::min (tcb->m_currentPacingRate.GetBitRate () * MilliSeconds (1).GetMilliSeconds () / 8, (uint64_t) 64000);
    }*/
}

void
TcpBbrAdaptiveI::UpdateTargetCwnd (Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this << tcb);
  m_targetCWnd = InFlight (tcb, m_cWndGain);
}

void
TcpBbrAdaptiveI::ModulateCwndForRecovery (Ptr<TcpSocketState> tcb, const struct RateSample * rs)
{
  NS_LOG_FUNCTION (this << tcb << rs);
  if ( rs->m_packetLoss > 0)
    {
      tcb->m_cWnd = std::max (tcb->m_cWnd.Get () - rs->m_packetLoss, tcb->m_segmentSize);
    }

  if (m_packetConservation)
    {
      tcb->m_cWnd = std::max (tcb->m_cWnd.Get (), tcb->m_bytesInFlight + tcb->m_lastAckedSackedBytes);
    }
}

void
TcpBbrAdaptiveI::ModulateCwndForProbeRTT (Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this << tcb);
  if (m_state == BbrMode_t::BBR_PROBE_RTT)
    {
      tcb->m_cWnd = std::min (tcb->m_cWnd.Get (), m_minPipeCwnd);
    }
}

void
TcpBbrAdaptiveI::SetCwnd (Ptr<TcpSocketState> tcb, const struct RateSample * rs)
{
  NS_LOG_FUNCTION (this << tcb << rs);
  UpdateTargetCwnd (tcb);

  if (tcb->m_congState == TcpSocketState::CA_RECOVERY)
    {
      ModulateCwndForRecovery (tcb, rs);
    }

  if (!m_packetConservation)
    {
      if (m_isPipeFilled)
        {
          tcb->m_cWnd = std::min (tcb->m_cWnd.Get () + (uint32_t) tcb->m_lastAckedSackedBytes, m_targetCWnd);
        }
      else if (tcb->m_cWnd < m_targetCWnd || tcb->m_delivered < tcb->m_initialCWnd * tcb->m_segmentSize)
        {
          tcb->m_cWnd = tcb->m_cWnd.Get () + tcb->m_lastAckedSackedBytes;
        }
      tcb->m_cWnd = std::max (tcb->m_cWnd.Get (), m_minPipeCwnd);
    }
  ModulateCwndForProbeRTT (tcb);
  if (tcb->m_congState == TcpSocketState::CA_RECOVERY)
    {
      m_packetConservation = false;
    }
}

void
TcpBbrAdaptiveI::UpdateRound (Ptr<TcpSocketState> tcb, const struct RateSample * rs)
{
  NS_LOG_FUNCTION (this << tcb << rs);
  if (tcb->m_txItemDelivered >= m_nextRoundDelivered)
    {
      m_nextRoundDelivered = tcb->m_delivered;
      m_roundCount++;
      m_roundStart = true;
    }
  else
    {
      m_roundStart = false;
    }
}

void
TcpBbrAdaptiveI::UpdateBtlBw (Ptr<TcpSocketState> tcb, const struct RateSample * rs)
{
  //NEW
  /*
  double past_std;
  double new_std;
  bool my_condition;
  bool decreaseLength;
  bool increaseLength;
  */

  NS_LOG_FUNCTION (this << tcb << rs);
  if (rs->m_deliveryRate == 0)
    {
      return;
    }

  UpdateRound (tcb, rs);

  // my_condition is true every 3 cycles
  /*if (my_counterI % 3 == 0)
  {
    my_condition = true;
    my_counterI = 1;    
  }
  else 
  {
  	my_condition = false;
  	my_counterI += 1;
  }
*/
  if (!rs->m_isAppLimited)
  {
    /*if (my_condition)
    {
      past_std = computeStd(past_valuesI);
    }*/
   
    past_valuesI.push_back(rs->m_deliveryRate.GetBitRate ()); //add an element at the end

    if (past_valuesI.size() > MY_SIZEI)
    {
      past_valuesI.erase(past_valuesI.begin()); //remove expired values
    }
    /*if (my_condition)
    {
      new_std = computeStd(past_valuesI);
    }*/
  }

  if ( (rs->m_deliveryRate.GetBitRate () >= 1.20*m_maxBwFilter.GetBest().GetBitRate () && pacing_gainI < 1.80) || !rs->m_isAppLimited)
    {
      pacing_gainI += 0.15;
      drainI -= 0.15;
    }
  else
    {
      pacing_gainI = 1.25;
      drainI = 0.75;
    }

  if (rs->m_deliveryRate >= m_maxBwFilter.GetBest () || !rs->m_isAppLimited)
    {
      m_maxBwFilter.Update (rs->m_deliveryRate, m_roundCount);
    }

/*
  increaseLength = (new_std/past_std < 0.75 && my_condition) ? true : false;
  decreaseLength = (new_std/past_std > 2 && my_condition) ? true : false;

  if (isNewCycleI && increaseLength && pacing_gain_cycleI.size() < 8 && past_valuesI.size() == MY_SIZEI){
    pacing_gain_cycleI.push_back(1.0); //stretch the cycle (the cycle can't grow too much)
  }
  if (isNewCycleI && decreaseLength && pacing_gain_cycleI.size() > 4 && past_valuesI.size() == MY_SIZEI){
    pacing_gain_cycleI.pop_back(); //shrink the cycle (the cycle can't shrink too much)
  }*/
}


void
TcpBbrAdaptiveI::UpdateModelAndState (Ptr<TcpSocketState> tcb, const struct RateSample * rs)
{
  NS_LOG_FUNCTION (this << tcb << rs);
  UpdateBtlBw (tcb, rs);
  CheckCyclePhase (tcb, rs);
  CheckFullPipe (rs);
  CheckDrain (tcb);
  UpdateRTprop (tcb);
  CheckProbeRTT (tcb);
}

void
TcpBbrAdaptiveI::UpdateControlParameters (Ptr<TcpSocketState> tcb, const struct RateSample * rs)
{
  NS_LOG_FUNCTION (this << tcb << rs);
  SetPacingRate (tcb, m_pacingGain);
  SetSendQuantum (tcb);
  SetCwnd (tcb, rs);
}

std::string 
TcpBbrAdaptiveI::WhichState (BbrMode_t mode) const
{
  switch (mode)
    {
    case 0:
      return "BBR_STARTUP";
    case 1:
      return "BBR_DRAIN";
    case 2:
      return "BBR_PROBE_BW";
    case 3:
      return "BBR_PROBE_RTT";
    }
  NS_ASSERT (false);
}

void
TcpBbrAdaptiveI::SetBbrState (BbrMode_t mode)
{
  NS_LOG_FUNCTION (this << mode);
  NS_LOG_DEBUG (Simulator::Now () << " Changing from " << WhichState (m_state) << " to " << WhichState (mode));
  m_state = mode;
}

uint32_t
TcpBbrAdaptiveI::GetBbrState ()
{
  NS_LOG_FUNCTION (this);
  return m_state;
}

double
TcpBbrAdaptiveI::GetCwndGain ()
{
  NS_LOG_FUNCTION (this);
  return m_cWndGain;
}

double
TcpBbrAdaptiveI::GetPacingGain ()
{
  NS_LOG_FUNCTION (this);
  return m_pacingGain;
}

std::string
TcpBbrAdaptiveI::GetName () const
{
  return "TcpBbrAdaptiveI";
}

bool
TcpBbrAdaptiveI::HasCongControl () const
{
  NS_LOG_FUNCTION (this);
  return true;
}

void
TcpBbrAdaptiveI::PktsAcked (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked,
                        const Time& rtt)
{
  NS_LOG_FUNCTION (this << tcb << segmentsAcked << rtt);
  CongControl (tcb, &tcb->m_rs);
}

void
TcpBbrAdaptiveI::CongControl (Ptr<TcpSocketState> tcb, const struct RateSample *rs)
{
  NS_LOG_FUNCTION (this << tcb << rs);
  UpdateModelAndState (tcb, rs);
  UpdateControlParameters (tcb, rs);
}

void
TcpBbrAdaptiveI::CongestionStateSet (Ptr<TcpSocketState> tcb,
                            const TcpSocketState::TcpCongState_t newState)
{
  NS_LOG_FUNCTION (this << tcb << newState);
  if (newState == TcpSocketState::CA_OPEN && !m_isInitialized)
    {
      NS_LOG_DEBUG ("CongestionStateSet triggered to CA_OPEN :: " << newState);
      m_rtProp = tcb->m_lastRtt != Time::Max () ? tcb->m_lastRtt : Time::Max ();
      m_rtPropStamp = Simulator::Now ();
      m_priorCwnd = tcb->m_initialCWnd * tcb->m_segmentSize;
      m_targetCWnd = tcb->m_initialCWnd * tcb->m_segmentSize;
      m_minPipeCwnd = 4 * tcb->m_segmentSize;
      m_sendQuantum = 1 * tcb->m_segmentSize;
//      m_maxBwFilter = MaxBandwidthFilter_t (m_bandwidthWindowLength,
//                                            DataRate (tcb->m_initialCWnd * tcb->m_segmentSize * 8 / m_rtProp.GetSeconds ())
//                                            , 0);
      m_maxBwFilter = MaxBandwidthFilter_t (m_bandwidthWindowLength, DataRate (0), 0);
      InitRoundCounting ();
      InitFullPipe ();
      EnterStartup ();
      InitPacingRate (tcb);
      m_isInitialized = true;
    }
  else if (newState == TcpSocketState::CA_LOSS)
    {
      NS_LOG_DEBUG ("CongestionStateSet triggered to CA_LOSS :: " << newState);
      SaveCwnd (tcb);
      m_roundStart = true;
    }
  else if (newState == TcpSocketState::CA_RECOVERY)
    {
      NS_LOG_DEBUG ("CongestionStateSet triggered to CA_RECOVERY :: " << newState);
      SaveCwnd (tcb);
      tcb->m_cWnd = tcb->m_bytesInFlight + std::max (tcb->m_lastAckedSackedBytes, tcb->m_segmentSize);
      m_packetConservation = true;
    }
}

void
TcpBbrAdaptiveI::CwndEvent (Ptr<TcpSocketState> tcb,
                   const TcpSocketState::TcpCAEvent_t event)
{
  NS_LOG_FUNCTION (this << tcb << event);
  if (event == TcpSocketState::CA_EVENT_COMPLETE_CWR)
    {
      NS_LOG_DEBUG ("CwndEvent triggered to CA_EVENT_COMPLETE_CWR :: " << event);
      m_packetConservation = false;
      RestoreCwnd (tcb);
    }
  else if (event == TcpSocketState::CA_EVENT_TX_START)
    {
      NS_LOG_DEBUG ("CwndEvent triggered to CA_EVENT_TX_START :: " << event);
      if (tcb->m_bytesInFlight == 0 && tcb->m_appLimited)
        {
          m_idleRestart = true;
          if (m_state == BbrMode_t::BBR_PROBE_BW && tcb->m_appLimited)
            {
              SetPacingRate (tcb, 1);
            }
        }
    }
}

uint32_t
TcpBbrAdaptiveI::GetSsThresh (Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight)
{
  NS_LOG_FUNCTION (this << tcb << bytesInFlight);
  SaveCwnd (tcb);
  return tcb->m_initialSsThresh;
}

Ptr<TcpCongestionOps>
TcpBbrAdaptiveI::Fork (void)
{
  return CopyObject<TcpBbrAdaptiveI> (this);
}

} // namespace ns3
