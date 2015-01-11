#pragma once

#include <string>

#include "faac.h"

using namespace std;


class FaacEncoder
{
public:
	FaacEncoder(void);
	~FaacEncoder(void);

public:


	virtual bool init(int samplerate, int channel, int bitPerSample,int bitrate=100);
	virtual void fini();
	virtual bool encode(string &data, string &output);
		virtual int getFrameSize();
	virtual float getFrameDuration();

	string getHeader();

public:
	int m_samplerate;
	int m_channels;
	int m_bitPerSample;
	int m_bitrate;
	int  m_encoderType;

	//int sample_rate;
	int m_nFrameSize;
	//int m_channels;

	unsigned long m_samplesInputSize;
	unsigned long m_maxOutputSize;

	faacEncHandle   m_faacHandle;
	int*            m_pInBuf;
	unsigned char *m_outputBuffer;
	string m_header;

};

