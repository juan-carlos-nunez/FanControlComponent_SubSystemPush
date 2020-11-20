/*
* Class: TempMonitor
*
* Description: Reponsible for receiving temperatures from multiple subsystems
*     asynchronously. It makes use of a double buffer (with a queue as the buffer)
*     to allow the draining of one buffer while the second buffer is being filled.
* 
*     It is also responsible for monitoring the max temp across all subsystems.
*     When a new max temp is identified, it will notify all the listeners of the 
*     the new max temp value.
*
*/

#pragma once
#include "TempMonitorListener.h"
#include "GeneralConstants.h"

#include <unordered_map>   // LUT of SS & Temps
#include <set>             // Order set of temps.
#include <vector>          // Listeners list.
#include <queue>           // {TECH_DEBT} Would be better to use a circular buffer with re-usable elements.
#include <mutex>
#include <atomic>
#include <thread>

#include <grpcpp/grpcpp.h>
#include "TempMonitor.grpc.pb.h"

class TempMonitor final : public TempMonitorSink::TempMonitorServer::Service
{
   struct QueueElement
   {
      int   subSysId{ INT_MIN };
      float temp{ 0.0f };

      QueueElement() {};
      QueueElement(int ssid, float t) : subSysId(ssid), temp(t) {};
   };

   const std::vector<int>         subSystemIds;
   std::unordered_map<int, float> subSystemTemps;
   std::multiset<float>           curTemps;
   std::queue<QueueElement>       queue;
                                     
   float                          curMaxTemp{ 0.0 };
                                     
   std::thread             tempThread;
   std::condition_variable tempThreadCond;
   std::mutex              tempThreadMux;
   std::atomic<bool>       tempThreadKeepAlive{ false };

   std::vector<TempMonitorListener*> listeners;
   std::mutex                        listenersMux;
  
   bool updateCurMaxTemp();
   void notifyNewMaxTemp();
   void updateTempTables(const std::pair<int,float>& newTemp );
   void updateCurTemps(QueueElement& tempData);

   void updateTempsThread();

   grpc::ServerBuilder           builder;
   std::unique_ptr<grpc::Server> server;

   grpc::Status UpdateSubSystemTemp(grpc::ServerContext* context, const TempMonitorSink::SubSysIdAndTemp* idTemp, TempMonitorSink::empty_param* noResponse) override;
   void RunServer();

public:
   TempMonitor( const std::vector<int>& subSystemIds );
   ~TempMonitor();

   GeneralConstants::ReturnCodes initialize();

   GeneralConstants::ReturnCodes registerListener(TempMonitorListener& listener);
   GeneralConstants::ReturnCodes unregisterListener(TempMonitorListener& listener);
};

