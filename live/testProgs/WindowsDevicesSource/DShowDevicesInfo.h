#ifndef  __DSHOW_DEVICES_MGR_H
#define  __DSHOW_DEVICES_MGR_H

extern "C" {;
#include "libavutil/avutil.h"
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"
}
#include <atlbase.h>
#include <windows.h>
#include <dshow.h>
#include <streams.h>
#include <dvdmedia.h>
#include <vector>
#include <string>
#include <assert.h>
#include <qedit.h>

#include "FaacEncoder.h"
#include "X264Encoder.h"
using namespace std;





#define __LOGOUT(s) 

#define ThrowStdExp(s) { \
	char expMsg[4096];\
	sprintf(expMsg,"%s:%s %d",s,__FILE__,__LINE__);\
	throw std::exception(expMsg); \
}

#define DEVICE_CAP_VIDEO_TYPE 0 
#define DEVICE_CAP_AUDIO_TYPE 1

struct MediaInfoContext
{
	int iPixFormat;
	int iWidth;
	int iHeight;


};

void printAmInfo(AM_MEDIA_TYPE* p,bool bVideo = true);


void rgbReverse(uint8_t* data,int width, int height,int bitCount);



class CDShowCapInfo
{
public:
	CDShowCapInfo(void)
	{

	}
	~CDShowCapInfo(void)
	{
		realseOptions();
	}
	 void setType(int iType)
	{
		m_iType = iType;
	}

	int getType()
	{
		return m_iType;
	}

	void setFriendlyName(wstring wszName)
	{
		m_wszFriendlyName = wszName;
	}
	wstring getFriendlyName()
	{
		return m_wszFriendlyName;
	}

	int getMediaOptionCount()
	{
		return m_vecMediaInfo.size();
	}

	void putMediaOption(AM_MEDIA_TYPE* mediaInfo)
	{
		m_vecMediaInfo.push_back(mediaInfo);
	}

	//获取output pin 支持的媒体类型
	AM_MEDIA_TYPE* getMediaOption(int iIndex)
	{
		if (iIndex < m_vecMediaInfo.size())
		{
			AM_MEDIA_TYPE* info = m_vecMediaInfo.at(iIndex);
			return info;
		}
		else
		{
			return NULL;
		}
	}

	 void realseOptions()
	 {
		 for (int i=0;i<m_vecMediaInfo.size();i++)
		 {
			 AM_MEDIA_TYPE* mType = m_vecMediaInfo.at(i);
			 if (mType)
			 {
				 DeleteMediaType(mType);
				 mType = NULL;
			 }
		 }
		 m_vecMediaInfo.clear();
	 }
private:
	int m_iType;
    wstring  m_wszFriendlyName;
	vector<AM_MEDIA_TYPE*> m_vecMediaInfo;
};

class CDshowCapInfoMgr
{
public:
	CDshowCapInfoMgr()
	{
	
	}
	~CDshowCapInfoMgr()
	{
		releaseInfos();
	}

	CDShowCapInfo* getVideoInfo(int iIndex)
	{
		if(iIndex < m_vecVideoCap.size())
		{
			return m_vecVideoCap.at(iIndex);
		}
		
		return NULL;
	}

	CDShowCapInfo* getAudioInfo(int iIndex)
	{
		if(iIndex < m_vecAudioCap.size())
		{
			return m_vecAudioCap.at(iIndex);
		}

		return NULL;
	}
	bool enumAllCapInfo();
	bool refreshDevicesCapInfo()
	{

	}
	void releaseInfos()
	{
		int i=0;
		for (i=0;i<m_vecVideoCap.size();i++)
		{
			CDShowCapInfo* pInfo = m_vecVideoCap.at(i);
			if (pInfo)
			{
				delete pInfo;
				pInfo = NULL;
			}
		}
		for (i=0;i<m_vecAudioCap.size();i++)
		{
			CDShowCapInfo* pInfo = m_vecAudioCap.at(i);
			if (pInfo)
			{
				delete pInfo;
				pInfo = NULL;
			}
		}
		m_vecAudioCap.clear();
		m_vecAudioCap.clear();
	}



private:
	bool enumDevicesCapInfo(bool audio);

	HRESULT GetPinCategory(IPin *pPin, GUID *pPinCategory)
	{
		HRESULT hr;
		IKsPropertySet *pKs;
		hr = pPin->QueryInterface(IID_IKsPropertySet, (void **)&pKs);
		if (FAILED(hr))
		{
			// The pin does not support IKsPropertySet.
			return hr;
		}
		// Try to retrieve the pin category.
		DWORD cbReturned;
		hr = pKs->Get(AMPROPSETID_Pin, AMPROPERTY_PIN_CATEGORY, NULL, 0, 
			pPinCategory, sizeof(GUID), &cbReturned);

		// If this succeeded, pPinCategory now contains the category GUID.

		pKs->Release();
		return hr;
	}
public:
	void printCapDetail()
	{
		int i = 0;
		for (i =0;i<m_vecVideoCap.size();i++)
		{
			CDShowCapInfo* pVcap = m_vecVideoCap.at(i);
			wprintf(L"video device name: %s\r\n",pVcap->getFriendlyName().c_str());
			for(int j=0;j<pVcap->getMediaOptionCount();j++)
			{
				printf("%d: ",j);
				AM_MEDIA_TYPE* pMtype = pVcap->getMediaOption(j);
				if (pMtype->formattype == FORMAT_VideoInfo)
				{
					VIDEOINFOHEADER* vInfo1 = (VIDEOINFOHEADER*)pMtype->pbFormat;
					 char fourCC[5] = {0};
					 memcpy(fourCC,&vInfo1->bmiHeader.biCompression,4);
					printf("pixformat: %s fps: %lld  width: %d height: %d planes: %d",
						fourCC, 
						10000000LL/vInfo1->AvgTimePerFrame,
						vInfo1->bmiHeader.biWidth,
						vInfo1->bmiHeader.biHeight,
						vInfo1->bmiHeader.biPlanes
						);
				}
				else if (pMtype->formattype == FORMAT_VideoInfo2)
				{
					VIDEOINFOHEADER2* vInfo1 = (VIDEOINFOHEADER2*)pMtype->pbFormat;
					char fourCC[5] = {0};
					memcpy(fourCC,&vInfo1->bmiHeader.biCompression,4);
					printf("pix format: %s fps: %lld width: %d height: %d planers: %d",
						fourCC, 10000000LL/vInfo1->AvgTimePerFrame,
						vInfo1->bmiHeader.biWidth,
						vInfo1->bmiHeader.biHeight,
						vInfo1->bmiHeader.biPlanes
						);
				}
				else
				{
					printf("unknown type...");
				}

				printf("\r\n");
			}
		}

		for (i =0;i<m_vecAudioCap.size();i++)
		{
			CDShowCapInfo* pAcap = m_vecAudioCap.at(i);
			wprintf(L"audio device name: %s\r\n",pAcap->getFriendlyName().c_str());

			for(int j=0;j<pAcap->getMediaOptionCount();j++)
			{
				printf("%d: ",j);
				AM_MEDIA_TYPE* pMtype = pAcap->getMediaOption(j);
				if (pMtype->formattype == FORMAT_WaveFormatEx)
				{
					WAVEFORMATEX* Info = (WAVEFORMATEX*)pMtype->pbFormat;
					printf("channel: %d sample: %d bitrate: %d",Info->nChannels,Info->wBitsPerSample,Info->nSamplesPerSec);
				}
				printf("\r\n");
			}

		}
	}

private:
	vector<CDShowCapInfo*> m_vecVideoCap;
	vector<CDShowCapInfo*> m_vecAudioCap;

};
class CMyAutoLock
{
public:
	CMyAutoLock(CRITICAL_SECTION* lock)
	{
		m_lock = lock;
		::EnterCriticalSection(lock);
	}
	~CMyAutoLock()
	{
		::LeaveCriticalSection(m_lock);
	}
private:
	CRITICAL_SECTION* m_lock;
};

class CDeviceCapture : public ISampleGrabberCB
{
public:
	STDMETHODIMP_(ULONG) AddRef() { return 2; }
	STDMETHODIMP_(ULONG) Release() { return 1; }
	virtual STDMETHODIMP SampleCB( double SampleTime, IMediaSample *pSample ){ return S_OK; };
	virtual STDMETHODIMP BufferCB( double SampleTime, BYTE *pBuffer, long BufferLen );
	virtual STDMETHODIMP QueryInterface( REFIID riid, void ** ppv )
	{
		if( riid == IID_ISampleGrabberCB || riid == IID_IUnknown ){ 
			*ppv = (void *) static_cast<ISampleGrabberCB*> ( this );
			return NOERROR;
		} 
		return E_NOINTERFACE;
	};
public:
	CDeviceCapture(int iType,wstring wszFriendlyName,AM_MEDIA_TYPE* pAmType)
	{
		 m_faacEncoder = NULL;
		m_iType = iType;
		m_pAmType = NULL;
		m_cacheCount = 0;
		setAmType(pAmType);
		m_wszFriendlyName = wszFriendlyName;
		m_swsCtxt = NULL;
		buildDshowCapGraph(wszFriendlyName);
		m_pX264 = new X264Encoder();
		m_pX264Param = m_pX264->getParamContext();
		 ::InitializeCriticalSection(&m_lock);

		
		
	}
	~CDeviceCapture()
	{
		::DeleteCriticalSection(&m_lock);
	}


	AM_MEDIA_TYPE* getAmType()
	{
		return m_pAmType;
	}
	
	void startCap()
	{
		m_pControl->Run();
	}

	void stopCap()
	{
		m_pControl->Stop();
	}

	void pauseCap()
	{
		m_pControl->Pause();
	}

	string getNalsData()
	{
		CMyAutoLock autoLock(&m_lock);
		string data =  m_dataNals;
		m_dataNals.clear();
		return data;
	}

private:

	void setAmType(AM_MEDIA_TYPE* amType)
	{
		m_iType==DEVICE_CAP_VIDEO_TYPE?printAmInfo(amType):printAmInfo(amType,false);
		if(m_pAmType)
		{
			DeleteMediaType(m_pAmType);
			m_pAmType = NULL;
		}
		m_pAmType = CreateMediaType(amType);
		//FreeMediaType(*m_pAmType);
		//CopyMediaType(m_pAmType,amType);

	}
	bool createDeciceCap(wstring wszFridlyName,IBaseFilter*& device);

	bool buildDshowCapGraph(wstring wszFridlyName);




private:
	int m_iType;
	CComPtr<ICaptureGraphBuilder2> m_pCapGraph2;
	CComPtr<IMediaControl> m_pControl;
	AM_MEDIA_TYPE* m_pAmType;
	wstring m_wszFriendlyName;
	CComPtr<ISampleGrabber> m_pSampleGrab;

	SwsContext* m_swsCtxt;
	uint8_t* m_data[4];
	int      m_iLineSize[4];

	X264Encoder* m_pX264;
	X264ParamContext* m_pX264Param;

	string m_dataNals;
	CRITICAL_SECTION m_lock;
	int m_cacheCount; //缓冲图片个数

	FaacEncoder* m_faacEncoder;


};
























#endif