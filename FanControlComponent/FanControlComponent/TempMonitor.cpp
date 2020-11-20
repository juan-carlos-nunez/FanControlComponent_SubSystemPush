#include "TempMonitor.h"
#include "log.h"

#include <utility>

//
// Name: TempMonitor (ctor)
//
// Description: Constructor
//
// Params: ssids - Vector of subsystem ids.
//
TempMonitor::TempMonitor( const std::vector<int>& ssIds ) : subSystemIds( ssIds )
{
   DEBUG_STD_OUT("TempMonitor::ctor() - EXIT");
}

//
// Name: ~TempMonitor (dtor)
//
// Description: Destructor, causes the tempThread to exit and stops
//    the gRPC server.
//
TempMonitor::~TempMonitor()
{
   DEBUG_STD_OUT("TempMonitor::dtor() - ENTER");

   if (tempThread.joinable())
   {
      tempThreadKeepAlive.store(false);
      
      queue.push(QueueElement());

      tempThreadCond.notify_one();
      tempThread.join();
   }

   if( nullptr != server )
   {
      server->Shutdown();
      server->Wait();
   }
   DEBUG_STD_OUT("TempMonitor::dtor() - EXIT");
}

//
// Name: initialize
//
// Description: Starts the tempThread and the gRPC server.
//
// Return: GeneralConstants::ReturnCodes
//
GeneralConstants::ReturnCodes TempMonitor::initialize()
{
   tempThreadKeepAlive.store(true);
   tempThread = std::thread(&TempMonitor::updateTempsThread, this);
   
   RunServer();

   return GeneralConstants::ReturnCodes::SUCCESS;
}

//
// Name: updateTempsThread
//
// Description: Main for the tempThread. Pulls a
//    temp from the queue, releasing the mutex lock
//    and then passing the temp on for analysis.
//
// {HAZARD}: If updateCurTemps is too slow to process the influx of temps,
//         the queues will slowly become deeper and deeper, the temperatures
//         being processed will be stale and memory utilization will become high.
//
// {HAZARD_MITIGATION}:
//         1. Perform analysis to characterize this processing path.
//         2. Consider adding a watchdog to log whenever the queue
//            reach certain size (50%, 80%, etc).
//
// {HAZARD_TODO}: Hazard Mitigation.
//
void TempMonitor::updateTempsThread()
{
   while( tempThreadKeepAlive.load() )
   {
      std::unique_lock<std::mutex> lock(tempThreadMux);
      tempThreadCond.wait(lock, [this]() { return !queue.empty(); });

      if (!tempThreadKeepAlive.load())
      {
         break;
      }

      auto newTemp = queue.front();
      queue.pop();
      lock.unlock();

      updateCurTemps( newTemp );
   }
}

// Name: updateCurTemps
//
// Description: Updates the subsystem temp table and the sorted temps list. 
//    It checks if there is a new max temp and if there is, notify listeners. 
//
// Params: newTemp - The new temperature to process.
//
void TempMonitor::updateCurTemps(QueueElement& newTemp )
{
   if( INT_MIN != newTemp.subSysId )
   {
      updateTempTables( std::make_pair(newTemp.subSysId, newTemp.temp) );
   }

   if( updateCurMaxTemp() )
   {
      // New Max temp.
      notifyNewMaxTemp();
   }
}

//
// Name: updateTempTables
//
// Description: Check if the temp changed. If it did, remove the old temp 
// from the sorted list and insert the new temp.
//
// Params: newTemp - The new temperature to process. <SubSystemId,Temp>.
//
// Notes: Find T: O(logn), Erase Itr T: O(1), Insert is T: O(logn)
//
void TempMonitor::updateTempTables(const std::pair<int, float>& newTemp)
{
   if (subSystemTemps[newTemp.first] != newTemp.second)
   {
      auto Itr{ curTemps.find(subSystemTemps[newTemp.first]) };
      if( curTemps.end() != Itr )
      {
         curTemps.erase(Itr); // Old
      }
      curTemps.insert(newTemp.second); // New

      subSystemTemps[newTemp.first] = newTemp.second;
   }
}

//
// Name: updateCurMaxTemp
//
// Description: Checks to see if there is a new max temp.
//
// Return: bool - True if a new max temp was identified. False otherwise.
//
// Notes: Find & Erase is T: O(logn). Insert is T: O(logn)
//
bool TempMonitor::updateCurMaxTemp()
{
   auto rVal{ false };
   auto maxTemp{ *(--curTemps.end()) };
   if( maxTemp != curMaxTemp )
   {
      curMaxTemp = maxTemp;
      rVal = true;
   }
   return rVal;
}

//
// Name: notifyNewMaxTemp
//
// Description: Notifies registered listeners of a new max temperature.
//
// {RISK} As the number of listeners increase, the longer it will take
//    for this thread to finish. Also, any listener not returning immediatly
//    will cause delays and interrupt processing of temperatures.
//
// {RISK_MITIGATION} Consider a seperate thread (e.g. async). However,
//    care needs to be taken since, while the thread runs, a new max temp may be
//    identified.
//
void TempMonitor::notifyNewMaxTemp()
{
   const std::lock_guard<std::mutex> lock(listenersMux); 
   for( auto listener : listeners )
   {
      listener->notifyNewMaxTemp(curMaxTemp);
   }
}

//
// Name: registerListener
//
// Description: API Implementation that adds the listeners to the list of listeners.
//
// Return: GeneralConstants::ReturnCodes
//
GeneralConstants::ReturnCodes TempMonitor::registerListener(TempMonitorListener& listener)
{
   auto rVal{ GeneralConstants::ReturnCodes::TEMP_MONITOR_LISTENER_REG_FAILED };

   const std::lock_guard<std::mutex> lock(listenersMux);

   auto Itr{ std::find(listeners.begin(), listeners.end(), &listener) };
   if (listeners.end() == Itr)
   {
      listeners.push_back( &listener );
      rVal = GeneralConstants::ReturnCodes::SUCCESS;
   }
   return rVal;
}

//
// Name: unregisterListener
//
// Description: API Implementation that removes the listener to the list of listeners.
//
// Return: GeneralConstants::ReturnCodes
//
GeneralConstants::ReturnCodes TempMonitor::unregisterListener(TempMonitorListener& listener)
{
   auto rVal{ GeneralConstants::ReturnCodes::TEMP_MONITOR_LISTENER_UNREG_FAILED };
   
   const std::lock_guard<std::mutex> lock(listenersMux);

   auto Itr{ std::find(listeners.begin(), listeners.end(), &listener) };
   if (listeners.end() != Itr)
   {
      listeners.erase(Itr);
      rVal = GeneralConstants::ReturnCodes::SUCCESS;
   }
   return rVal;
}

//
// Name: UpdateSubSystemTemp
//
// Description: RPC Interface, allowing subsystems to send in their temperatures.
//
// {HAZARD}: Subsystem Id is not in the list provided. System may be improperly setup.
//           Plan is needed to properly handle.
//
// {HAZARD_MITIGATION}: Speak with stakeholders to come up with plan.
//                   1. Process request. Worste case, fans run at max speed.
//                   2. Drop request, at risk of this subsystem overheating.
//
//                   Make sure to log using system-level logging methods (care not to flood the logs).
//
// {HAZARD_TODO}: Come up with plan. For now, will process request to prevent subsystem from overheating.
//
grpc::Status TempMonitor::UpdateSubSystemTemp( grpc::ServerContext* context, 
                                               const TempMonitorSink::SubSysIdAndTemp* idTemp, 
                                               TempMonitorSink::empty_param* noResponse )
{
   DEBUG_STD_OUT( "TempMonitor::UpdateSubSystemTemp[" << idTemp->subsysid() << ", " << idTemp->temp() << "]" );

   auto Itr { find(subSystemIds.begin(), subSystemIds.end(), idTemp->subsysid()) };
   if( subSystemIds.end() == Itr )
   {
      // {HAZARD_TODO} Execute system-level logging (Beware of flooding logs).
      PRINT_STD_OUT( "TempMonitor::UpdateSubSystemTemp - ERROR: Received temp for an unknown SubSystemID ID:[" << idTemp->subsysid() << "], Temp[" << idTemp->temp() << "]");
   }

   {
      std::unique_lock<std::mutex> lock(tempThreadMux);
      queue.push( QueueElement(idTemp->subsysid(), idTemp->temp()) );
      tempThreadCond.notify_one();
   }

   return grpc::Status::OK;
}

//
// Name: RunServer
//
// Description: Creates and starts the gRPC server.
//
// {HAZARD_TODO} Setup server credentials. For this exercise, server is using simple insecure credentials. 
//
void TempMonitor::RunServer()
{
   builder.AddListeningPort(GeneralConstants::GRPC_SERVER_ADDRESS, grpc::InsecureServerCredentials());

   builder.RegisterService(this);

   server = builder.BuildAndStart();
   
   DEBUG_STD_OUT( "TempMonitor::RunServer() - Server listening on " << GeneralConstants::GRPC_SERVER_ADDRESS );
}