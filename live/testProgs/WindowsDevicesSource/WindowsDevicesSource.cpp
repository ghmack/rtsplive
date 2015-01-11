// WindowsDevicesSource.cpp : 定义控制台应用程序的入口点。
//

#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"
#include "GroupsockHelper.hh"

//#include "stdafx.h"


#pragma comment(lib,"ws2_32.lib")
#pragma comment(lib,"BasicUsageEnvironment.lib")
#pragma comment(lib,"UsageEnvironment.lib")
#pragma comment(lib,"groupsock.lib")
#pragma comment(lib,"liveMedia.lib")

#include "DShowDevicesInfo.h"




UsageEnvironment* env;
Boolean iFramesOnly = False;
static char newDemuxWatchVariable;

static void announceStream(RTSPServer* rtspServer, ServerMediaSession* sms,
	char const* streamName, char const* inputFileName); // fwd

class DshowCapH264Source :public FramedSource
{
public:
	DshowCapH264Source(UsageEnvironment& env,
		char const* streamName,CDeviceCapture* pCap):FramedSource(env){

			//m_task = BasicTaskScheduler::createNew();
			setDeviceCap(pCap);

	}
	void setDeviceCap(CDeviceCapture* pDevice)
	{
		m_pDshowCap = pDevice;
		//pDevice->startCap();
	}
	static DshowCapH264Source* createNew(UsageEnvironment& env, char const* streamName,CDeviceCapture* pcap){
		DshowCapH264Source* newSource = new DshowCapH264Source(env,streamName,pcap);
		return newSource;

	}
private:
	virtual void doGetNextFrame()
	{
		while (m_Nals.size() < fMaxSize )
		{
			//Sleep(10);
			fillNals();			
		}
		memcpy(fTo,m_Nals.data(),fMaxSize);
		fFrameSize = fMaxSize;
		m_Nals = m_Nals.substr(fMaxSize);
		gettimeofday(&fPresentationTime, NULL);

		FramedSource::afterGetting(this);
	}

	static void getData(void* pParam)
	{
		DshowCapH264Source* pThis = (DshowCapH264Source*)pParam;
		return pThis->fillNals();
	}

	void fillNals()
	{
		string data = m_pDshowCap->getNalsData();
		m_Nals.append((const char*)data.data(),data.size());
		return ;
	}
private:
	CDeviceCapture* m_pDshowCap;
	string m_Nals;
	TaskScheduler* m_task;
};

class H264DshowCapMediaServerSubsession :public H264VideoFileServerMediaSubsession
{
public:
	static H264DshowCapMediaServerSubsession* createNew(UsageEnvironment& env,
		char const* streamName,CDeviceCapture* pCap) {			
			return new H264DshowCapMediaServerSubsession(env, streamName,pCap);
	}
	H264DshowCapMediaServerSubsession(UsageEnvironment& env,
		char const* streamName,CDeviceCapture* pCap):H264VideoFileServerMediaSubsession(env,
		streamName,  True){
			m_pCap = pCap;
	}

protected:
	virtual FramedSource* createNewStreamSource(unsigned clientSessionId,
		unsigned& estBitrate){

			// Create the video source:
			DshowCapH264Source* memSource = DshowCapH264Source::createNew(envir(), fFileName,m_pCap);
			if (memSource == NULL) return NULL;

			// Create a framer for the Video Elementary Stream:
			return H264VideoStreamFramer::createNew(envir(), memSource);
	}

private:
	CDeviceCapture* m_pCap;
};


//#pragma comment(lib,"libmsvcrt.a")

int _tmain(int argc, TCHAR* argv[])
{
	try{
	   char* str = "1234";
	   fprintf(stderr, "%s", str);
		OutPacketBuffer::maxSize = 200000;
		TaskScheduler* scheduler = BasicTaskScheduler::createNew();
		env = BasicUsageEnvironment::createNew(*scheduler);
		*env<<"mack is a good boy";
		string s = "0123456789";
		s = s.substr(8);
		::CoInitialize(NULL);
		CDshowCapInfoMgr* mgr = new CDshowCapInfoMgr();
		mgr->enumAllCapInfo();
		mgr->printCapDetail();
		//CDShowCapInfo* pInfo = mgr->getVideoInfo(0);	
		//CDeviceCapture* device = new CDeviceCapture(DEVICE_CAP_VIDEO_TYPE,pInfo->getFriendlyName(),pInfo->getMediaOption(0));
		CDShowCapInfo* pInfo = mgr->getAudioInfo(0);	
		CDeviceCapture* device = new CDeviceCapture(DEVICE_CAP_AUDIO_TYPE,pInfo->getFriendlyName(),pInfo->getMediaOption(8));

		device->startCap();
		
		
		UserAuthenticationDatabase* authDB = NULL;
		RTSPServer* rtspServer = RTSPServer::createNew(*env, 8554, authDB);
		if (rtspServer == NULL) {
			*env << "Failed to create RTSP server: " << env->getResultMsg() << "\n";
			exit(1);
		}

		char const* descriptionString
			= "Session streamed by \"testOnDemandRTSPServer\"";

		{
			device->startCap();
			char const* streamName = "h264ESVideoTest";
			char const* inputFileName = "test.264";
			ServerMediaSession* sms
				= ServerMediaSession::createNew(*env, streamName, streamName,
				descriptionString);
			sms->addSubsession(H264DshowCapMediaServerSubsession
				::createNew(*env, inputFileName,device));
			rtspServer->addServerMediaSession(sms);

			announceStream(rtspServer, sms, streamName, inputFileName);
		}
		env->taskScheduler().doEventLoop(); // does not return
		while (true)
		{
			Sleep(1000);
		}
	}
	catch(std::exception e)
	{
		printf(e.what());
	}
	return 0;
}

static void announceStream(RTSPServer* rtspServer, ServerMediaSession* sms,
	char const* streamName, char const* inputFileName) {
		char* url = rtspServer->rtspURL(sms);
		UsageEnvironment& env = rtspServer->envir();
		env << "\n\"" << streamName << "\" stream, from the file \""
			<< inputFileName << "\"\n";
		env << "Play this stream using the URL \"" << url << "\"\n";
		delete[] url;
}