/**********
This library is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the
Free Software Foundation; either version 2.1 of the License, or (at your
option) any later version. (See <http://www.gnu.org/copyleft/lesser.html>.)

This library is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
more details.

You should have received a copy of the GNU Lesser General Public License
along with this library; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
**********/
// Copyright (c) 1996-2010, Live Networks, Inc.  All rights reserved
// A common framework, used for the "openRTSP" and "playSIP" applications
// Implementation
#ifndef _OPENRTSP_H_
#define _OPENRTSP_H_

#ifdef __cplusplus
extern "C"{
#endif

#define RTSP_ERROR		(-1)
#define RTSP_SUCCESS	(0)

#define RTSP_TYPE_AUDIO	0
#define RTSP_TYPE_VIDEO	1

///video codec////
#define RTSP_CODEC_H264		0x01
#define RTSP_CODEC_MPEG4	0x02
#define RTSP_CODEC_MJPG		0x03

///audio codec////
#define RTSP_CODEC_MP4A	0xA1	//AAC
#define RTSP_CODEC_PCMU 0xA2	//U-Law PCM

typedef struct
{
	char			rtspURL[255];	//rtsp://
	char			audHdr[255];
	char			vidHdr[255];

	int				id;
	int 			tcpConnect;
    int 			isVfirst;
	int				firstAudio;
	int				audHdrSize;
	int				vidHdrSize;
	int				streamType;	//Audio or Video
	int				isKeyFrame;
	int				codecType;
	int				readSizeV;
	int				readSizeA;
	int             rVBTail;
    int             isFirstFrameBuffer;
	unsigned char	*readVidBuf;	//must allocated by caller.
	unsigned char	*origReadVidBuf;
	unsigned char	*osaFirstReadVidBuf;
	unsigned char	*readAudBuf;	//must allocated by caller.
	unsigned long	ts;			//milisecond
    void			*p_sys;		//rtsp_system
    
} rtsp_object_t; 


int rtspOpen(rtsp_object_t *p_obj, int tcpConnect);
int rtspRead(rtsp_object_t *p_obj);
int rtspClose(rtsp_object_t *p_obj);

#ifdef __cplusplus
}
#endif

#endif
