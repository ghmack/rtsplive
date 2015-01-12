#include "DShowDevicesInfo.h"




void printAmInfo(AM_MEDIA_TYPE* p,bool bVideo)
{
	if(bVideo){
	VIDEOINFOHEADER* vInfo1 = (VIDEOINFOHEADER*)p->pbFormat;
	if (p->formattype == FORMAT_VideoInfo)
	{
		VIDEOINFOHEADER* vInfo1 = (VIDEOINFOHEADER*)p->pbFormat;
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
	else if (p->formattype == FORMAT_VideoInfo2)
	{
		VIDEOINFOHEADER2* vInfo1 = (VIDEOINFOHEADER2*)p->pbFormat;
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

	}
	else
	{
		if (p->formattype == FORMAT_WaveFormatEx)
		{
			WAVEFORMATEX* Info = (WAVEFORMATEX*)p->pbFormat;
			printf("channel: %d sample: %d bitrate: %d",Info->nChannels,Info->wBitsPerSample,Info->nSamplesPerSec);
		}
		else
		{
			printf("unknown type...");
		}
	}
}

//window系统的RGB是从下向上扫描从上向下显示，所以数据给到第三方时需要倒转一下
void rgbReverse(uint8_t* data,int width, int height,int bitCount)
{
	int lineSize = width*bitCount;
	for (int i=0;i<height/2;i++)
	{
		for (int j=0;j<lineSize;j++)
		{
			uint8_t color = data[lineSize*i + j];
			data[lineSize*i + j] = data[lineSize*(height-i-1) + j] ;
			data[lineSize*(height-i-1) + j] =color;
		}
	}
}

bool CDshowCapInfoMgr::enumAllCapInfo()
{
	return enumDevicesCapInfo(false) && enumDevicesCapInfo(true);
}

bool CDshowCapInfoMgr::enumDevicesCapInfo(bool audio)
{
	//枚举的文档资料: http://msdn.microsoft.com/en-us/library/dd377566(v=vs.85).aspx
	CComPtr<ICreateDevEnum> de;
	HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER,IID_ICreateDevEnum, (void**)&de);
	if (hr != NOERROR) {
		__LOGOUT("Can't create CLSID_SystemDeviceEnum object");
		return false;
	}

	CComPtr<IEnumMoniker> em;
	if (audio) {
		hr = de->CreateClassEnumerator(CLSID_AudioInputDeviceCategory,&em, 0);
	} else {
		hr = de->CreateClassEnumerator(CLSID_VideoInputDeviceCategory,&em, 0);
	}
	if (hr != NOERROR) {
		__LOGOUT("Can't create CLSID_***InputDeviceCategory object");
		return false;
	}

	ULONG got;
	IMoniker *m;
	em->Reset();
	while(hr = em->Next(1, &m, &got), hr==S_OK)
	{
		IPropertyBag *pb;
		hr = m->BindToStorage(0, 0, IID_IPropertyBag, (void **)&pb);
		if(!SUCCEEDED(hr)) continue ;
		CDShowCapInfo* pCapInfo = new CDShowCapInfo();
		VARIANT var;	
		var.vt = VT_BSTR;
		hr = pb->Read(L"FriendlyName", &var, NULL);
		if (hr == S_OK) {
			wstring szName;
			szName.append((wchar_t*)var.bstrVal);
			pCapInfo->setFriendlyName(szName);
			SysFreeString(var.bstrVal);
		}
		CComPtr<IBaseFilter> device;
		hr = m->BindToObject(NULL,NULL,IID_IBaseFilter,(void**)&device);
		if (hr != S_OK)
		{
			__LOGOUT("Create Device faild, BindToObjcet error");
		}
		CComPtr<IEnumPins> emp;
		hr = device->EnumPins(&emp);
		if (FAILED(hr)) {
			throw std::exception("EnumPins failed");
		}

		AM_MEDIA_TYPE* canVideoMt=0;
		AM_MEDIA_TYPE* canAudioMt=0;
		IPin* pin;
		ULONG nPin;
		while(emp->Next(1, &pin, &nPin) == S_OK) {
			PIN_DIRECTION pinDir;
			hr = pin->QueryDirection(&pinDir);
			if (FAILED(hr)) {
				pin->Release();
				throw std::exception("QueryDirection failed");
			}
			if (pinDir != PINDIR_OUTPUT) {
				pin->Release();
				continue;
			}
			CComPtr<IEnumMediaTypes> emt;
			hr = pin->EnumMediaTypes(&emt);
			if (FAILED(hr)) {
				pin->Release();
				throw std::exception("EnumMediaTypes failed");
			}
			GUID pinCat;
			if (GetPinCategory(pin, &pinCat) == S_OK) {
				if (pinCat != PIN_CATEGORY_CAPTURE) continue;
			}
			pin->Release();

			AM_MEDIA_TYPE* mt=0;
			ULONG nMt=0;
			while(emt->Next(1, &mt, &nMt) == S_OK) {
				if(mt)
				{	pCapInfo->putMediaOption(mt); 	}
			}

		}

		pb->Release();
		m->Release();
		if (!pCapInfo->getFriendlyName().empty())
		{
			if (audio)
			{
				pCapInfo->setType(DEVICE_CAP_AUDIO_TYPE);
				m_vecAudioCap.push_back(pCapInfo);
			}
			else
			{
				pCapInfo->setType(DEVICE_CAP_VIDEO_TYPE);
				m_vecVideoCap.push_back(pCapInfo);
			}
		}
		else
		{	delete pCapInfo;	}
	}
	return true;

}




bool CDeviceCapture::createDeciceCap(wstring wszFridlyName,IBaseFilter*& device)
{
	bool audio = m_iType== DEVICE_CAP_AUDIO_TYPE?true:false;
	CComPtr<ICreateDevEnum> de;
	HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER,IID_ICreateDevEnum, (void**)&de);
	if (hr != NOERROR) {
		__LOGOUT("Can't create CLSID_SystemDeviceEnum object");
		return false;
	}

	CComPtr<IEnumMoniker> em;
	if (audio) {
		hr = de->CreateClassEnumerator(CLSID_AudioInputDeviceCategory,&em, 0);
	} else {
		hr = de->CreateClassEnumerator(CLSID_VideoInputDeviceCategory,&em, 0);
	}
	if (hr != NOERROR) {
		__LOGOUT("Can't create CLSID_***InputDeviceCategory object");
		return false;
	}

	ULONG got;
	IMoniker *m;
	em->Reset();
	while(hr = em->Next(1, &m, &got), hr==S_OK)
	{
		IPropertyBag *pb;
		hr = m->BindToStorage(0, 0, IID_IPropertyBag, (void **)&pb);
		if(!SUCCEEDED(hr)) continue ;
		CDShowCapInfo* pCapInfo = new CDShowCapInfo();
		VARIANT var;	
		var.vt = VT_BSTR;
		hr = pb->Read(L"FriendlyName", &var, NULL);
		if (hr == S_OK) {
			wstring szName;
			szName.append((wchar_t*)var.bstrVal);
			pCapInfo->setFriendlyName(szName);
			SysFreeString(var.bstrVal);
			if(szName == wszFridlyName)
			{
				IBaseFilter* dev=NULL;
				hr = m->BindToObject(NULL,NULL,IID_IBaseFilter,(void**)&device);
				if (hr != S_OK)
				{ __LOGOUT("Create Device faild, BindToObjcet error");		}
				//device = dev;
				return true;
			}
			
		}
		
	}

	return false;
}

bool CDeviceCapture::buildDshowCapGraph(wstring wszFridlyName)
{
	bool audio = m_iType== DEVICE_CAP_AUDIO_TYPE?true:false;
	CComPtr<ICaptureGraphBuilder2> graphBuilder;
	CComPtr<IGraphBuilder> graph;
	CComPtr<IBaseFilter> device ;
	HRESULT hr;
	IBaseFilter** dev = NULL;
	//创建设备filter
	if (!createDeciceCap(wszFridlyName, (IBaseFilter*&)device)) { 
		ThrowStdExp("Can't create AV device filter");
	}

	assert(device);

	//创建filtergraph, capturegraphbuilder2对象
	hr = CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER, IID_IGraphBuilder, (void **)&graph);
	if (FAILED(hr)) {
		ThrowStdExp("Can't create CLSID_FilterGraph, Direct X hasn't installed?");
	}
	hr = CoCreateInstance(CLSID_CaptureGraphBuilder2, NULL, CLSCTX_INPROC_SERVER, IID_ICaptureGraphBuilder2, (void **)&graphBuilder);
	if (FAILED(hr)) {
		ThrowStdExp("Can't create CLSID_CaptureGraphBuilder2, Direct X hasn't installed?");
	}
	hr = graphBuilder->SetFiltergraph(graph);
	if (FAILED(hr)) {
		ThrowStdExp("SetFiltergraph failed");
	}
	//隐藏窗口
	CComQIPtr<IMediaControl> control = graph;
	CComQIPtr<IVideoWindow> window = graph;
	window->put_Visible(OAFALSE);

	//加入source filter
	hr = graph->AddFilter(device, NULL);
	if (FAILED(hr)) {
		ThrowStdExp("source AddFilter failed");
	}
	CComPtr<IBaseFilter> nullRender;
	hr = CoCreateInstance(CLSID_NullRenderer,NULL,CLSCTX_INPROC_SERVER,IID_IBaseFilter,(void**)&nullRender);
	if(FAILED(hr)){
		ThrowStdExp("Create Interface error");
	}
	hr = graph->AddFilter(nullRender, NULL);
	if (FAILED(hr)) {
		ThrowStdExp("source AddFilter failed");
	}

	CComPtr<ISampleGrabber> sampleGrabber;
	hr = CoCreateInstance(CLSID_SampleGrabber,NULL,CLSCTX_INPROC_SERVER,IID_ISampleGrabber,(void**)&sampleGrabber);
	if(FAILED(hr)){
		ThrowStdExp("Create Interface error");
	}
	CComPtr<IBaseFilter> filterGrab;
	filterGrab = sampleGrabber;
	hr = graph->AddFilter(filterGrab, NULL);
	if (FAILED(hr)) {
		ThrowStdExp("source AddFilter failed");
	}
    const	GUID  gType = audio?MEDIATYPE_Audio:MEDIATYPE_Video;
	CComPtr<IAMStreamConfig> config;			
	hr = graphBuilder->FindInterface(&PIN_CATEGORY_CAPTURE,
		&gType,
		device, IID_IAMStreamConfig, (void **)&config);
	if(FAILED(hr)){
		ThrowStdExp("Create Interface error");
	}

	if (audio)
	{
		//CComPtr<IID_IAMBufferNegotiation> nego;
		IAMBufferNegotiation* pNeg = NULL;
		graphBuilder->FindInterface(&PIN_CATEGORY_CAPTURE,
			&gType,
			device, IID_IAMBufferNegotiation, (void **)&pNeg);
		if(FAILED(hr)){
			ThrowStdExp("Create Interface error");
		}

		if (!m_faacEncoder)
		{
			WAVEFORMATEX* Info = (WAVEFORMATEX*)m_pAmType->pbFormat;
			m_faacEncoder = new FaacEncoder();
			bool bOK = m_faacEncoder->init(Info->nSamplesPerSec,Info->nChannels,Info->wBitsPerSample);
			if(!bOK)
				ThrowStdExp("Create Interface error");
		}

		// Set the buffer size based on selected settings
		ALLOCATOR_PROPERTIES prop={0};
		prop.cbBuffer = m_faacEncoder->getFrameSize();
		prop.cBuffers = -1;
		prop.cbAlign = -1;
		hr = pNeg->SuggestAllocatorProperties(&prop);
		pNeg->Release(); 
	}

	hr = config->SetFormat(m_pAmType);           //设置input pin的media type ，传null重置pin为默认type
	if (FAILED(hr)) {
		int iErr = ::GetLastError();
		ThrowStdExp("Can't Pause for grabber");
	}
	hr =sampleGrabber->SetMediaType(m_pAmType); //设置后，运行时会检查output pin 如果类型不符合会拒绝连接，可不调用

	hr = graphBuilder->RenderStream(&PIN_CATEGORY_CAPTURE,&gType,device,filterGrab,nullRender);
	if(FAILED(hr)){
		ThrowStdExp("Create Interface error");
	}
	AM_MEDIA_TYPE iAmType;
	hr = sampleGrabber->GetConnectedMediaType(&iAmType);
	if (FAILED(hr)) {
		ThrowStdExp("Can't GetConnectedMediaType for grabber");
	}
    this->setAmType(&iAmType);
	FreeMediaType(iAmType);


	sampleGrabber->SetCallback(this,1);

	hr = control->Pause();
	if (FAILED(hr)) {
		int iErr = ::GetLastError();
		ThrowStdExp("Can't Pause for grabber");
	}
	 m_pCapGraph2 = graphBuilder;
	 m_pControl = control;
	 m_pSampleGrab = sampleGrabber;
	 return true;

}


STDMETHODIMP CDeviceCapture::BufferCB( double SampleTime, BYTE *pBuffer, long BufferLen )
{
	static FILE* hFile = NULL;
	static int yuvSize = 0;
	static double tempSize = 0;
	static int tempTicket = ::GetTickCount();
	static int iPlus = 0;
	//printf("time: %f, buffer size: %ld ",SampleTime,BufferLen);
	if(m_iType == DEVICE_CAP_VIDEO_TYPE){	
	//printAmInfo(m_pAmType);
	VIDEOINFOHEADER* info = (VIDEOINFOHEADER*)m_pAmType->pbFormat;
	//AM_MEDIA_TYPE amType ;
	//m_pSampleGrab->GetConnectedMediaType(&amType);
	//printAmInfo(&amType);
	if (!m_swsCtxt)
	{
		hFile = fopen("d:\\testYuv.yuv","wb");
		m_swsCtxt = sws_getContext(info->bmiHeader.biWidth,info->bmiHeader.biHeight,AV_PIX_FMT_BGR24,
			info->bmiHeader.biWidth,info->bmiHeader.biHeight,AV_PIX_FMT_YUV420P,SWS_BILINEAR,NULL,NULL,NULL);
		 yuvSize = av_image_alloc(m_data, m_iLineSize,
			info->bmiHeader.biWidth, info->bmiHeader.biHeight,
			AV_PIX_FMT_YUV420P, 1);
		 m_pX264Param->width = info->bmiHeader.biWidth;
		 m_pX264Param->height = info->bmiHeader.biHeight;
		 m_pX264->init();
	}
	rgbReverse(pBuffer,info->bmiHeader.biWidth,info->bmiHeader.biHeight,3);
	uint8_t* data[4] = {0,0,0,0};
	int line[4] = {0}; 
	int ret = 0;
	ret = av_image_fill_linesizes(line, AV_PIX_FMT_BGR24,info->bmiHeader.biWidth);
	ret =av_image_fill_pointers(data,AV_PIX_FMT_BGR24,info->bmiHeader.biHeight,pBuffer,line);
	ret =sws_scale(m_swsCtxt, data,line,0,info->bmiHeader.biHeight,
		m_data,m_iLineSize);


	string nals;
	//ret = m_pX264->encode(m_data[0],SampleTime*1000,nals);
	ret = m_pX264->encode(m_data[0],iPlus++,nals);
	if(ret == -1) printf("encoder error!!!\r\n");
	if(nals.size()>0){
		int iSize = nals.size();
	ret = fwrite(nals.data(),1,iSize,hFile);
	fflush(hFile);
	//printf("write %d size to file \r\n",iSize);
	}
	if(nals.size() > 0)
	{
		CMyAutoLock lock(&m_lock);
		if(m_cacheCount > 30)
		{
			m_dataNals.clear();
			m_cacheCount = 0;
		}
		m_dataNals.append(nals);
		m_cacheCount++;
	}
	uint64_t timeStamp = (uint64_t)((SampleTime-tempSize)*1000);
	printf("time %lld nals size %d  GetTickCount: %d \r",timeStamp,m_dataNals.size(), GetTickCount()-tempTicket);
	ret = 0;
	tempSize = SampleTime;

	}
	else
	{
		//printAmInfo(m_pAmType,false);
		if (m_pAmType->formattype == FORMAT_WaveFormatEx)
		{
			WAVEFORMATEX* Info = (WAVEFORMATEX*)m_pAmType->pbFormat;
			//printf("buffer size: %d channel: %d sample: %d bitrate: %d",BufferLen,Info->nChannels,Info->wBitsPerSample,Info->nSamplesPerSec);
			if (!hFile)
			{
				hFile = fopen("d:\\dshow.aac","wb");
			}

			string inputAudio,outputAudio;
			inputAudio.append((char*)pBuffer,BufferLen);

			m_faacEncoder->encode(inputAudio,outputAudio);
			if(outputAudio.size() > 0)
			{
				//fwrite(inputAudio.data(),1,inputAudio.size(),hFile);
				fwrite(outputAudio.data(),1,outputAudio.size(),hFile);
				fflush(hFile);
			}
			
		}

	}
	//printf("\r\n");
	return S_OK;
}