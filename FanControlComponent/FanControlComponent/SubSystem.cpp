#include "SubSystem.h"
#include "log.h"

namespace
{
   const int TEMP_GENERATION_INTERVAL_MS{ 300 };
}

//
// Name: SubSystem (ctor)
//
// Description: Constructor
//
// Params: channel - The gRPC Channel to use to communicate with gRpc server.
//         ssid - The SubSystem ID to use..
//         updater - Callback to the UI, to update the SubSystem Temps.
//
SubSystem::SubSystem(std::shared_ptr<grpc::Channel> channel, int ssid, UiUpdater* updater )
   : stub_(TempMonitorSink::TempMonitorServer::NewStub(channel))
   , uiUpdater(updater)
   , subSystemId(ssid)
{
   // Empty
}

//
// Name: ~SubSystem (dtor)
//
// Description: Destructor, causes the SubSystem thread to exit.
//
SubSystem::~SubSystem()
{
   DEBUG_STD_OUT("SubSystem::~dtor() - SSID=[" << subSystemId << "] - ENTER");

   if (subSysThread.joinable())
   {
      stop();
      subSysThreadKeepAlive.store(false);
      start();
      subSysThread.join();
   }

   DEBUG_STD_OUT("SubSystem::~dtor() - SSID=[" << subSystemId << "] - EXIT");
}

//
// Name: initialize
//
// Description: Creates the SubSystem thread.
//
// Return: GeneralConstants::ReturnCodes
//
GeneralConstants::ReturnCodes SubSystem::initialize()
{
   subSysThreadKeepAlive.store(true);
   subSysThread = std::thread(&SubSystem::SubSytemThread, this);

   return GeneralConstants::ReturnCodes::SUCCESS;
}

//
// Name: start
//
// Description: Causes the thread to start running.
//
// Return: GeneralConstants::ReturnCodes
//
GeneralConstants::ReturnCodes SubSystem::start()
{
   subSysThreadRun.store(true);
   subSysThreadCond.notify_one();
   return GeneralConstants::ReturnCodes::UNKNOWN_ERROR;
}

//
// Name: stop
//
// Description: Causes the thread to stop running.
//
// Return: GeneralConstants::ReturnCodes
//
GeneralConstants::ReturnCodes SubSystem::stop()
{
   subSysThreadRun.store(false);
   return GeneralConstants::ReturnCodes::SUCCESS;
}

//
// Name: sendTemp
//
// Description: Sends the provided temp to the RPC Server.
//
// Param: temp - The temperature to send.
//
void SubSystem::sendTemp( float temp )
{
	// Data we are sending to the server.
	TempMonitorSink::SubSysIdAndTemp data;
	data.set_subsysid(subSystemId);
	data.set_temp(temp);

	TempMonitorSink::empty_param emptyRVal;

	// Context for the client. It could be used to convey extra information to
	// the server and/or tweak certain RPC behaviors.
	grpc::ClientContext context;

	// The actual RPC.
	grpc::Status status = stub_->UpdateSubSystemTemp(&context, data, &emptyRVal);

	// Act upon its status.
	if( !status.ok() )
	{
      PRINT_STD_OUT( "SubSystem::SubSytemThread() - ERROR: ["<< status.error_code() << "] -  " << status.error_message());
	}
}

//
// Name: SubSytemThread
//
// Description: SubSystemThread's main. Adjusts the temperature and then
//    sends the updated temperature to the gRPC server.
//
void SubSystem::SubSytemThread()
{
   DEBUG_STD_OUT("SubSystem::ctor() - SSID=[" << subSystemId << "] - ENTER")

   int tempIncDirection{ 1 };
   float temp{30.0f};
   float tempInc{0.1f};

   while (subSysThreadKeepAlive.load())
   {
      std::unique_lock<std::mutex> lock(subSysThreadMux);
      subSysThreadCond.wait(lock, [this]() { return subSysThreadRun.load(); });

      if (!subSysThreadKeepAlive.load())
      {
         break;
      }

      sendTemp(temp);

      if( uiUpdater )
      {
         uiUpdater->updateSubSystemTemp( subSystemId, temp);
      }

      tempIncDirection = rand() % 3;

      switch (tempIncDirection)
      {
      case 0:
      {
         temp -= tempInc;
         break;
      }
      case 1:
      {
         temp += tempInc;
         break;
      }
      case 2: // Fallthrough
      default:
         // no change
         break;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(TEMP_GENERATION_INTERVAL_MS));
   }

   DEBUG_STD_OUT("SubSystem::ctor() - SSID=[" << subSystemId << "] - EXIT");
}