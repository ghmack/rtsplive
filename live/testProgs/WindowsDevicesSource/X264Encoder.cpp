#include "X264Encoder.h"

int __cdecl __ms_vsnprintf(
	char *buffer,
	size_t count,
	const char *format,
	va_list argptr
	)
{
	return vsnprintf_s(buffer, count, _TRUNCATE, format, argptr);
}
int __cdecl __mingw_vsscanf(
	const char *s,
	const char *fmt,
	va_list ap
	)
{
	void *a[5];
	int i;
	for (i=0; i < sizeof(a)/sizeof(a[0]); i++)
		a[i] = va_arg(ap, void *);
	return sscanf(s, fmt, a[0], a[1], a[2], a[3], a[4]);
}




X264Encoder::X264Encoder(void):m_x264Param(NULL),m_pictureIn(NULL),m_x264Encoder(NULL)
{
	m_paramCtxt.preset = "superfast";
	m_paramCtxt.profile = "main";
	m_paramCtxt.bitrate = 500;
	m_paramCtxt.fps = 25;
	m_paramCtxt.b_cbr = 1;
	m_paramCtxt.width = 640;
	m_paramCtxt.height = 480;
	m_paramCtxt.hasBframe = 0;
	m_paramCtxt.quality = 6;
	m_paramCtxt.keyInterval = m_paramCtxt.fps;
	m_paramCtxt.bRepeatHeader = 1;
	m_paramCtxt.bAnnexb = 1;
}


X264Encoder::~X264Encoder(void)
{
	fini();
}

void X264Encoder::fini()
{
	if(m_pictureIn){
	delete m_pictureIn;
	m_pictureIn = NULL;
	}
	if (m_x264Encoder) {
		x264_encoder_close(m_x264Encoder);
	}
	if(m_x264Param){
	 delete m_x264Param;
	 m_x264Param = NULL;
	}

}


X264ParamContext* X264Encoder::getParamContext()
{
	return &m_paramCtxt;
}

int  X264Encoder::init()
{
	string presetName  = m_paramCtxt.preset;
	string tuneName    = m_paramCtxt.tune;
	string profileName = m_paramCtxt.profile;
	int fps = m_paramCtxt.fps;
	int bps = m_paramCtxt.bitrate;
	int width = m_paramCtxt.width;
	int height = m_paramCtxt.height;

	int maxBitRate = bps;
	int bufferSize = maxBitRate;
	bool bUseCBR = m_paramCtxt.b_cbr!=0?true:false;
	int quality = m_paramCtxt.quality;
	int KeyFrameInterval = m_paramCtxt.keyInterval;
	int threadCount = m_paramCtxt.threadCount;
	bool enableBFrame = m_paramCtxt.hasBframe!=0?true:false;
	m_x264Param = new x264_param_t;

	if (m_paramCtxt.tune == "Default" || tuneName.empty()) {
		x264_param_default_preset(m_x264Param , presetName.c_str(), NULL);
	} else {
		x264_param_default_preset(m_x264Param , presetName.c_str(), tuneName.c_str());
	}

	if (profileName != "Default"|| profileName.empty()) {
		x264_param_apply_profile(m_x264Param, profileName.c_str());
	}

	if(bUseCBR)
	{
		m_x264Param->rc.i_bitrate          = maxBitRate;
		m_x264Param->rc.i_vbv_max_bitrate  = maxBitRate; // vbv-maxrate
		m_x264Param->rc.i_vbv_buffer_size  = bufferSize; // vbv-bufsize
		m_x264Param->i_nal_hrd             = X264_NAL_HRD_CBR;
		m_x264Param->rc.i_rc_method        = X264_RC_ABR;
		m_x264Param->rc.f_rf_constant      = 0.0f;
	}
	else
	{
		m_x264Param->rc.i_vbv_max_bitrate  = maxBitRate;  // vbv-maxrate
		m_x264Param->rc.i_vbv_buffer_size  = bufferSize;  // vbv-bufsize
		m_x264Param->rc.i_rc_method        = X264_RC_CRF; // X264_RC_CRF;
		m_x264Param->rc.f_rf_constant      = quality;

	}

	m_x264Param->b_vfr_input           = 1;
	m_x264Param->i_keyint_max          = fps ;
	m_x264Param->i_width               = width;
	m_x264Param->i_height              = height;
	m_x264Param->vui.b_fullrange       = 0;          //specify full range input levels

	m_x264Param->i_fps_num = fps;
	m_x264Param->i_fps_den = 1;

	m_x264Param->i_timebase_num = m_x264Param->i_fps_den;
	m_x264Param->i_timebase_den = m_x264Param->i_fps_num;

	// disable start code 00 00 00 01 before NAL
	// instead of nalu size
	m_x264Param->b_repeat_headers = m_paramCtxt.bRepeatHeader;
	m_x264Param->b_annexb = m_paramCtxt.bAnnexb;

	if (enableBFrame)
		m_x264Param->i_bframe = 3;
	else
		m_x264Param->i_bframe = 0;

	if (threadCount > 0)
		m_x264Param->i_threads = threadCount;

	// @note
	// never use cpu capabilities.
	// let libx264 to choose.
#if 0
	m_x264Param->cpu = 0;
	m_x264Param->cpu |=X264_CPU_MMX;
	m_x264Param->cpu |=X264_CPU_MMXEXT;
	m_x264Param->cpu |=X264_CPU_SSE;
#endif

	m_x264Encoder = x264_encoder_open(m_x264Param);

	// update video sh
	x264_nal_t *nalOut;
	int nalNum;
	x264_encoder_headers(m_x264Encoder, &nalOut, &nalNum);

	for (int i = 0; i < nalNum; ++i) {
		x264_nal_t &nal = nalOut[i];
		if (nal.i_type == NAL_SPS) {
			m_sps.append((const char*)nal.p_payload,nal.i_payload);

			//the PPS always comes after the SPS
			x264_nal_t &pps = nalOut[++i];
			m_pps.append((const char*)pps.p_payload,pps.i_payload);
		}
	}

	m_pictureIn = new x264_picture_t;
	m_pictureIn->i_pts = 0;

	return 0;
}

int X264Encoder::encode(uint8_t *rawframe, uint64_t pts,string &nals)
{
	//printf("time %lld\r",pts);
	unsigned char *src_buf = rawframe;
	x264_picture_init(m_pictureIn);

	m_pictureIn->img.i_csp = X264_CSP_I420;
	m_pictureIn->img.i_plane = 3;
	m_pictureIn->i_type = X264_TYPE_AUTO;
	m_pictureIn->i_qpplus1 = 0;

	m_pictureIn->i_pts = pts; //pts 必须递增，否则警告： x264 [warning]: non-strictly-monotonic PTS，视频也不对

	m_pictureIn->img.plane[0] = src_buf;
	m_pictureIn->img.plane[1] = src_buf + m_x264Param->i_height * m_x264Param->i_width;
	m_pictureIn->img.plane[2] = src_buf + m_x264Param->i_height * m_x264Param->i_width * 5 / 4;
	m_pictureIn->img.i_stride[0] = m_x264Param->i_width;
	m_pictureIn->img.i_stride[1] = m_x264Param->i_width >> 1;
	m_pictureIn->img.i_stride[2] = m_x264Param->i_width >> 1;

	x264_picture_t picOut;
	int nalNum;
	x264_nal_t* nalOut;
	int len = x264_encoder_encode(m_x264Encoder, &nalOut, &nalNum, m_pictureIn, &picOut);
	if (len < 0) {
		return -1;
	}


	if (nalNum <= 0) {
		return -2;
	}

	for (int i = 0; i < nalNum; ++i) {
		x264_nal_t &nal = nalOut[i];
		nals.append((const char*)nal.p_payload, nal.i_payload);
		//printf("the nalsize is %d \r\n",nal.i_payload);
	}
	//printf("the totol nalsize is %d \r\n",nals.size());

	return 0;
}