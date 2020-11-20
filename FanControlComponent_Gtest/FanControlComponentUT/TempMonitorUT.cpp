#include "gtest/gtest.h"
#include "TempMonitor.h"
#include "SubSystem.h"

namespace
{
   class GenericListener final : public TempMonitorListener
   {
      float curTemp{ 0 };
   public:
      GenericListener(){};
      void notifyNewMaxTemp(float temp)
      { 
         curTemp = temp;
      };

      float getCurTemp()
      {
         return curTemp;
      };
   };
};

TEST(TempMonitorUT, ctor)
{
   const std::vector<int> ssIds{1,2,3,4,5,6,7,8,9,10};

   ASSERT_NO_THROW(
      { 
         TempMonitor tm{ ssIds };
      } );

}

TEST(TempMonitorUT, initialize)
{
   const std::vector<int> ssIds{ 1,2,3,4,5,6,7,8,9,10 };
   ASSERT_NO_THROW(
   {
      TempMonitor tm{ ssIds };
      ASSERT_EQ(GeneralConstants::ReturnCodes::SUCCESS, tm.initialize() );
   });
}

TEST(TempMonitorUT, RegUnRegListener)
{
   const std::vector<int> ssIds{ 1,2,3,4,5,6,7,8,9,10 };
   TempMonitor tm{ ssIds };

   GenericListener gl;
   ASSERT_EQ(GeneralConstants::ReturnCodes::SUCCESS, tm.registerListener(gl)   );
   ASSERT_EQ(GeneralConstants::ReturnCodes::SUCCESS, tm.unregisterListener(gl) );
}

TEST(TempMonitorUT, SendReceiveTemp)
{
   const std::vector<int> ssIds{ 1,2,3,4,5,6,7,8,9,10 };
   TempMonitor tm{ ssIds };

   ASSERT_EQ(GeneralConstants::ReturnCodes::SUCCESS, tm.initialize());
   
   auto channel{ grpc::CreateChannel("localhost:50051", grpc::InsecureChannelCredentials()) };
   SubSystem tmg(channel, ssIds[0]);

   GenericListener gl;
   ASSERT_EQ(GeneralConstants::ReturnCodes::SUCCESS, tm.registerListener(gl));
   
   auto temp{37.48f};
   tmg.sendTemp(temp);

   ASSERT_EQ(temp, gl.getCurTemp());

   ASSERT_EQ(GeneralConstants::ReturnCodes::SUCCESS, tm.unregisterListener(gl));
}

TEST(TempMonitorUT, CheckMaxTempNotification)
{
   const std::vector<int> ssIds{ 1,2,3,4,5,6,7,8,9,10 };
   TempMonitor tm{ ssIds };

   ASSERT_EQ(GeneralConstants::ReturnCodes::SUCCESS, tm.initialize());

   auto channel{ grpc::CreateChannel("localhost:50051", grpc::InsecureChannelCredentials()) };
   SubSystem tmg1(channel, ssIds[0]);
   SubSystem tmg2(channel, ssIds[1]);
   SubSystem tmg3(channel, ssIds[2]);
   SubSystem tmg4(channel, ssIds[3]);
   SubSystem tmg5(channel, ssIds[3]);

   GenericListener gl;
   ASSERT_EQ(GeneralConstants::ReturnCodes::SUCCESS, tm.registerListener(gl));

   // Max is testTemp
   auto testTemp{ 37.48f };
   tmg1.sendTemp(testTemp);
   ASSERT_EQ(testTemp, gl.getCurTemp());

   // Max is testTemp
   auto testTemp2{ 37.00f };
   tmg2.sendTemp(testTemp2);
   ASSERT_EQ(testTemp, gl.getCurTemp());

   // Max is testTemp
   tmg3.sendTemp(testTemp2);
   ASSERT_EQ(testTemp, gl.getCurTemp());

   // Max is testTemp
   tmg4.sendTemp(testTemp2);
   ASSERT_EQ(testTemp, gl.getCurTemp());

   // Max is testTemp3: Overwrite ssid 2 with testTemp3
   auto testTemp3{ 40.00f };
   tmg2.sendTemp(testTemp3);
   ASSERT_EQ(testTemp3, gl.getCurTemp());

   // Max is testTemp again: Overwrite ssid 2 with testTemp2
   tmg2.sendTemp(testTemp2);
   ASSERT_EQ(testTemp, gl.getCurTemp());

   // New Max is testTemp4: new ssid with highest temp.
   auto testTemp4{ 75.00f };
   tmg5.sendTemp(testTemp4);
   ASSERT_EQ(testTemp4, gl.getCurTemp());

   ASSERT_EQ(GeneralConstants::ReturnCodes::SUCCESS, tm.unregisterListener(gl));
}