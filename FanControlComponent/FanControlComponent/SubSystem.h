/*
* Class: TempMonitorGenerator
*
* Description: This is not used or needed by the FanControl. Intended to 
*     help facilitate demo and testing. Connects to the TempMonitor gRPC
*     server and sends a <SubSystemID / temperature> pair to the server.
*
*/

#pragma once

#include "GeneralConstants.h"
#include "UiUpdater.h"

#include <grpcpp/grpcpp.h>
#include "TempMonitor.grpc.pb.h"

class SubSystem final
{
   int subSystemId{INT_MIN};

   std::unique_ptr<TempMonitorSink::TempMonitorServer::Stub> stub_;
   UiUpdater*              uiUpdater{nullptr};
   
   std::thread             subSysThread;
   std::condition_variable subSysThreadCond;
   std::mutex              subSysThreadMux;
   std::atomic<bool>       subSysThreadKeepAlive{ false };
   std::atomic<bool>       subSysThreadRun{ false };
   void SubSytemThread();

public:
   SubSystem(std::shared_ptr<grpc::Channel> channel, int ssid, UiUpdater* updater = nullptr);
   ~SubSystem();

   void sendTemp( float temp );

   GeneralConstants::ReturnCodes initialize();
   GeneralConstants::ReturnCodes start();
   GeneralConstants::ReturnCodes stop();

};