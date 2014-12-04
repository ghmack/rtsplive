#ifndef __X264ENCODER_H
#define __X264ENCODER_H



extern "C"
{
#include <stdio.h>	
#include <stdint.h>
#include "x264.h"

	//编译报错，找不到一下函数
	int __cdecl __ms_vsnprintf(
		char *buffer,
		size_t count,
		const char *format,
		va_list argptr
		);
	int __cdecl __mingw_vsscanf(
		const char *s,
		const char *fmt,
		va_list ap
		);

};

#include <string>
using namespace std;


struct X264ParamContext
{
	string profile;
	string tune;
	string preset;
	int fps;
	int bitrate;
	int width;
	int height;
	int b_cbr;
	int hasBframe;
	int quality;
	int keyInterval;
	int threadCount;
	int bRepeatHeader;
	int bAnnexb;
};
typedef struct X264ParamContext X264ParamContext;

class X264Encoder
{
public:
	X264Encoder(void);
	~X264Encoder(void);

	X264ParamContext* getParamContext();
	int init();
	void fini();
	int encode(uint8_t *rawframe, uint64_t pts,string &nals);

private:
	x264_param_t * m_x264Param;
	x264_picture_t *m_pictureIn;
	x264_t *m_x264Encoder;
	//int delayOffset;
	//bool bFirstFrameProcessed;
	//int frameShift;

	string m_sps;
	string m_pps;
	X264ParamContext m_paramCtxt;
};




#endif