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

#include "inttypes.h"
#include <iostream>
#include <limits.h>
#include <assert.h>
#include <stdio.h>

#include "UsageEnvironment.hh"
#include "BasicUsageEnvironment.hh"
#include "GroupsockHelper.hh"
#include "liveMedia.hh"
#include "Base64.hh"
#include "MPEG4LATMAudioRTPSource.hh"

#include "openRTSP.h"

//////////////////////////////////////////////////////////
#define MAX_NO_DATA		5
//#define RTSPLIB_DEBUG


#ifdef RTSPLIB_DEBUG
#define TRACE0_DEC(msg, args...) printf("[RTSPLIB] - " msg, ##args)
#define TRACE1_DEC(msg, args...) printf("[RTSPLIB] - %s(%d):" msg, __FUNCTION__, __LINE__, ##args)
#define TRACE2_DEC(msg, args...) printf("[RTSPLIB] - %s(%d):\t%s:" msg, __FILE__, __LINE__, __FUNCTION__, ##args)
#else
#define TRACE0_DEC(msg, args,...) ((void)0)
#define TRACE1_DEC(msg, args,...) ((void)0)
#define TRACE2_DEC(msg, args,...) ((void)0)
#endif

class ourRTSPClient: public RTSPClient {
public:
	static ourRTSPClient* createNew(UsageEnvironment& env, char const* rtspURL,
		int verbosityLevel = 0,
		char const* applicationName = NULL,
		portNumBits tunnelOverHTTPPortNum = 0);

protected:
	ourRTSPClient(UsageEnvironment& env, char const* rtspURL,
		int verbosityLevel, char const* applicationName, portNumBits tunnelOverHTTPPortNum);
	// called only by createNew();
	virtual ~ourRTSPClient();

public:
	rtsp_object_t* rtspContext;
	int errCode ;
	char ev;
};

ourRTSPClient* ourRTSPClient::createNew(UsageEnvironment& env, char const* rtspURL,
	int verbosityLevel, char const* applicationName, portNumBits tunnelOverHTTPPortNum) {
		return new ourRTSPClient(env, rtspURL, verbosityLevel, applicationName, tunnelOverHTTPPortNum);
}

ourRTSPClient::ourRTSPClient(UsageEnvironment& env, char const* rtspURL,
	int verbosityLevel, char const* applicationName, portNumBits tunnelOverHTTPPortNum)
	: RTSPClient(env,rtspURL, verbosityLevel, applicationName, tunnelOverHTTPPortNum, -1),errCode(0),ev(0),rtspContext(NULL) {
}

ourRTSPClient::~ourRTSPClient() {
}

//////////////////////////////////////////////////////////
typedef struct
{
	unsigned int	i_codec;
    unsigned int   	i_bitrate; /**< bitrate of this ES */
	bool    		b_packetized;  /**< wether the data is packetized (ie. not truncated) */
    int     		i_extra;        /**< length in bytes of extra data pointer */
    void    		*p_extra;       /**< extra data needed by some decoders or muxers */
} es_format_t;

typedef struct
{
	rtsp_object_t	*p_demux;
	MediaSubsession	*sub;
	es_format_t     fmt;

    short			b_discard_trunc;
    short			b_rtcp_sync;
    char			waiting;
    unsigned long	i_pts;
    float			i_npt;

    unsigned char	*p_buffer;
    unsigned int	i_buffer;

} live_track_t;

struct demux_sys_t
{
    char            *p_sdp;    /* XXX mallocated */
    char            *psz_path; /* URL-encoded path */

    MediaSession     *ms;
    TaskScheduler    *scheduler;
    UsageEnvironment *env ;
    RTSPClient       *rtsp;

    /* */
    int              i_track;
    live_track_t     **track;   /* XXX mallocated */

    /* */
    int64_t          i_pcr; /* The clock */
    float            i_npt;
    float            i_npt_length;
    float            i_npt_start;

    /* */
    bool             b_no_data;     /* if we never received any data */
    int              i_no_data_ti;  /* consecutive number of TaskInterrupt */

    char             event;

    bool             b_paused;      /* Are we paused? */
    bool             b_error;

    float            f_seek_request;/* In case we receive a seek request while paused*/
};

static int Control( rtsp_object_t *, int, va_list );
static int Connect      ( rtsp_object_t * );
static int SessionsSetup( rtsp_object_t * );
static int Play         ( rtsp_object_t *);

static void StreamRead  ( void *, unsigned int, unsigned int,
                          struct timeval, unsigned int );
static void StreamClose ( void * );
static void TaskInterrupt( void * );

static unsigned char* parseH264ConfigStr( char const* configStr,
                                          unsigned int& configSize );

static size_t vlc_b64_decode_binary_to_buffer( uint8_t *p_dst, size_t i_dst, const char *p_src );
static int gFirstAud=0;
static int gTcpConnect = 0;

static void WaitResponse(rtsp_object_t* rtspCon)
{
	demux_sys_t* p_sys = (demux_sys_t*) rtspCon->p_sys;
	p_sys->scheduler->doEventLoop( &((ourRTSPClient*)p_sys->rtsp)->ev);
	((ourRTSPClient*)p_sys->rtsp)->ev = 0;
}


int rtspOpen(rtsp_object_t *p_obj, int tcpConnect)
{
	p_obj->p_sys 		= NULL;
	demux_sys_t *p_sys 	= NULL;

	p_obj->p_sys     = p_sys = (demux_sys_t*)malloc( sizeof( demux_sys_t ) );

	int i_return ;

	p_obj->tcpConnect 	= tcpConnect;

	memset(p_obj->audHdr, 0, 255);
	memset(p_obj->vidHdr, 0, 255);
	p_obj->isVfirst	 	= 0;
	p_obj->isFirstFrameBuffer = 1;
	p_obj->firstAudio 	= 0;
	p_obj->audHdrSize	= 0;
	p_obj->vidHdrSize	= 0;
	p_obj->rVBTail = 0;

	p_sys->i_track		= 0;
	p_sys->track		= NULL;

	p_sys->i_pcr 		= 0;
    p_sys->i_npt 		= 0.;
    p_sys->i_npt_start 	= 0.;
    p_sys->i_npt_length = 0.;
    p_sys->b_no_data 	= true;
    p_sys->i_no_data_ti = 0;

	p_sys->p_sdp		= NULL;
	p_sys->ms 			= NULL;
	p_sys->scheduler 	= NULL;
	p_sys->env			= NULL;
	p_sys->rtsp			= NULL;
	p_sys->b_error 		= false;

//	TRACE1_DEC("BasicTaskScheduler::createNew !!!\n" );
    if( ( p_sys->scheduler = BasicTaskScheduler::createNew() ) == NULL )
    {
        TRACE1_DEC("BasicTaskScheduler::createNew failed\n" );
        goto error;
    }

    if( !( p_sys->env = BasicUsageEnvironment::createNew(*p_sys->scheduler) ) )
    {
        TRACE1_DEC("BasicUsageEnvironment::createNew failed\n");
        goto error;
    }

	if( ( i_return = Connect( p_obj ) ) != RTSP_SUCCESS )
    {
        TRACE1_DEC( "Failed to connect with %s\n", p_obj->rtspURL );
        goto error;
    }

    if( p_sys->p_sdp == NULL )
    {
        TRACE1_DEC( "Failed to retrieve the RTSP Session Description\n" );
        goto error;
    }

    if( ( i_return = SessionsSetup( p_obj ) ) != RTSP_SUCCESS )
    {
        TRACE1_DEC( "Nothing to play for rtsp://%s\n", p_obj->rtspURL );
        goto error;
    }

    if( ( i_return = Play( p_obj ) ) != RTSP_SUCCESS )
        goto error;

    if( p_sys->i_track <= 0 )
        goto error;

	return RTSP_SUCCESS;


error:
	rtspClose(p_obj);
	return RTSP_ERROR;
}

int rtspRead(rtsp_object_t *p_obj)
{
	demux_sys_t    *p_sys = (demux_sys_t*)p_obj->p_sys;
	int             i, ret=0;
	TaskToken      task;

	if(p_sys != NULL){
	    /* First warn we want to read data */
	    p_sys->event = 0;
	    for( i = 0; i < p_sys->i_track; i++ )
	    {
	        live_track_t *tk = p_sys->track[i];
	        if( tk->waiting == 0 )
	        {
	            tk->waiting = 1;
				if(tk->fmt.i_codec == RTSP_CODEC_H264)
					tk->p_buffer = p_obj->readVidBuf;
				else if(tk->fmt.i_codec == RTSP_CODEC_MPEG4)
				{
					tk->p_buffer = p_obj->origReadVidBuf + tk->fmt.i_extra;
					memset(p_obj->origReadVidBuf,0,tk->fmt.i_extra);
				}
				else if(tk->fmt.i_codec == RTSP_CODEC_MJPG)
					tk->p_buffer = p_obj->origReadVidBuf;

	            tk->sub->readSource()->getNextFrame( tk->p_buffer, tk->i_buffer,
	                                          StreamRead, tk, StreamClose, tk );
	        }
	    }

	    /* Create a task that will be called if we wait more than 300ms */
//	    task = p_sys->scheduler->scheduleDelayedTask( 300000, TaskInterrupt, p_obj );
		//task = p_sys->scheduler->scheduleDelayedTask( 600000, TaskInterrupt, p_obj );

	    /* Do the read */
	    p_sys->scheduler->doEventLoop( &p_sys->event );

	    /* remove the task */
	    //p_sys->scheduler->unscheduleDelayedTask( task );

		p_sys->b_error ? ret = RTSP_ERROR : ret = RTSP_SUCCESS;
	}

	return ret;
}

int rtspClose(rtsp_object_t *p_obj)
{
	demux_sys_t *p_sys = NULL;

	p_sys = (demux_sys_t*)p_obj->p_sys;

    int i;

    for( i = 0; i < p_sys->i_track; i++ )
    {
        live_track_t *tk = p_sys->track[i];

		if( (tk->fmt.i_extra > 0) && (tk->fmt.p_extra != NULL) ) free( tk->fmt.p_extra );
        free( tk );
    }

    if( p_sys->i_track ) free( p_sys->track );
//	if(p_sys->i_no_data_ti == 0)	//sk_debug
	{
	   	if( p_sys->rtsp && p_sys->ms ) (p_sys->rtsp)->sendTeardownCommand( *p_sys->ms,NULL,NULL );
	    if( p_sys->ms ) Medium::close( p_sys->ms );
	}
    if( p_sys->rtsp ) RTSPClient::close( p_sys->rtsp );
    if( p_sys->env ) p_sys->env->reclaim();

    delete p_sys->scheduler;
	
    free( p_sys );
	
	return 1;
}

static void continueAfterDESCRIBE(RTSPClient* rtspClient, int resultCode, char* resultString) {
	ourRTSPClient* ourClient  = (ourRTSPClient*)rtspClient;
	demux_sys_t* p_sys = (demux_sys_t*)ourClient->rtspContext->p_sys;
	if(p_sys->p_sdp)
	{
		free( p_sys->p_sdp );
		p_sys->p_sdp = NULL;
	}
	p_sys->p_sdp = strdup(resultString);
	ourClient->ev = -1;
	if (resultCode != 0) {
		delete[] resultString;		
		p_sys->b_error = true;
		return;
	}
	delete[] resultString;		
}

static void continueAfterCheckResult(RTSPClient* rtspClient, int resultCode, char* resultString) {
	ourRTSPClient* ourClient  = (ourRTSPClient*)rtspClient;
	demux_sys_t* p_sys = (demux_sys_t*)ourClient->rtspContext->p_sys;
	if (resultCode !=0)
   {
	   p_sys->b_error = true;
   }
   ourClient->ev = -1;
}




/*****************************************************************************
 * Connect: connects to the RTSP server to setup the session DESCRIBE
 *****************************************************************************/
static int Connect( rtsp_object_t *p_demux )
{
	demux_sys_t *p_sys = (demux_sys_t*)p_demux->p_sys;


	int p_err = 0;
    char *p_sdp       = NULL;
    char *psz_options = NULL;
    char *psz_user    = NULL;
    char *psz_pwd     = NULL;
	char appName[1000];

    int  i_http_port  = 0;
    int  i_ret        = RTSP_SUCCESS;

	sprintf(appName, "LibRTSP%d", p_demux->id);
    if( ( p_sys->rtsp = ourRTSPClient::createNew( *p_sys->env,p_demux->rtspURL,
          1, appName, i_http_port ) ) == NULL )
    {
        TRACE1_DEC( "RTSPClient::createNew failed (%s)\n",
                 p_sys->env->getResultMsg() );

		i_ret = RTSP_ERROR;
		return i_ret;
    }

	((ourRTSPClient*)p_sys->rtsp)->rtspContext = p_demux;

 //	p_err = p_sys->rtsp->sendOptionsCommand( NULL, NULL);
 //	if(p_err == 0)
 //	{	
	//	TRACE1_DEC("RTSP Option commend error!!\n");
	//}



 	p_err = p_sys->rtsp->sendDescribeCommand(continueAfterDESCRIBE,NULL);
	WaitResponse(p_demux);
 	if(!p_err || p_sys->b_error)
	{
		const char *psz_error = p_sys->env->getResultMsg();
		TRACE1_DEC( "DESCRIBE failed with : %s\n", psz_error );
		if( p_sys->rtsp ) RTSPClient::close( p_sys->rtsp );
		p_sys->rtsp = NULL;
		i_ret = RTSP_ERROR;
		return i_ret;
	}
	



    return i_ret;
}


/*****************************************************************************
 * SessionsSetup: prepares the subsessions and does the SETUP
 *****************************************************************************/
static int SessionsSetup( rtsp_object_t *p_demux )
{
	demux_sys_t             *p_sys  = (demux_sys_t*)p_demux->p_sys;
    MediaSubsessionIterator *iter   = NULL;
    MediaSubsession         *sub    = NULL;

    bool           b_rtsp_tcp 		= (bool)p_demux->tcpConnect;
    int            i_client_port 	= 0;
    int            i_return 		= RTSP_SUCCESS;
    unsigned long   i_buffer 		= 0;
    unsigned const thresh 			= 200000; /* RTP reorder threshold .2 second (default .1) */
//    unsigned const thresh 			= 1000000;
    if( !( p_sys->ms = MediaSession::createNew( *p_sys->env, p_sys->p_sdp ) ) )
    {
        TRACE1_DEC( "Could not create the RTSP Session: %s\n", p_sys->env->getResultMsg() );
        return RTSP_ERROR;
    }

    /* Initialise each media subsession */
    iter = new MediaSubsessionIterator( *p_sys->ms );
    while( ( sub = iter->next() ) != NULL )
    {
        Boolean bInit;
        live_track_t *tk;

        /* Value taken from mplayer */
        if( !strcmp( sub->mediumName(), "audio" ) )
            i_buffer = 100000;
//			continue;
        else if( !strcmp( sub->mediumName(), "video" ) )
            i_buffer = 2000000;
        else if( !strcmp( sub->mediumName(), "text" ) )
            ;
        else continue;

        if( i_client_port != 0 )
        {
            sub->setClientPortNum( i_client_port );
            i_client_port += 2;
        }

        bInit = sub->initiate();

        if( !bInit )
        {
            TRACE1_DEC( "RTP subsession '%s/%s' failed (%s)\n",
                      sub->mediumName(), sub->codecName(), p_sys->env->getResultMsg() );
        }
        else
       	{
            if( sub->rtpSource() != NULL )
            {
                int fd = sub->rtpSource()->RTPgs()->socketNum();

                /* Increase the buffer size */
                if( i_buffer > 0 )
                    increaseReceiveBufferTo( *p_sys->env, fd, i_buffer );

                /* Increase the RTP reorder timebuffer just a bit */
                sub->rtpSource()->setPacketReorderingThresholdTime(thresh);

				setReceiveBufferTo(p_sys->rtsp->envir(), fd, 0x100000);
				printf("\nSocket Receive Buffer Size is %d \n", getReceiveBufferSize(p_sys->rtsp->envir(), fd));

            }

//            TRACE1_DEC( "RTP subsession '%s/%s'\n", sub->mediumName(), sub->codecName() );

            /* Issue the SETUP */
            if( p_sys->rtsp )
            {
                if( !p_sys->rtsp->sendSetupCommand( *sub, continueAfterCheckResult,False, b_rtsp_tcp, False,NULL ) )
                {
                    return RTSP_ERROR;
                }
				WaitResponse(p_demux);
				if (p_sys->b_error)
				{
					 return RTSP_ERROR;
				}
				

            }

            /* Check if we will receive data from this subsession for
             * this track */
            if( sub->readSource() == NULL ) continue;

            tk = (live_track_t*)malloc( sizeof( live_track_t ) );
            if( !tk )
            {
                delete iter;
                return RTSP_ERROR;
            }

			tk->p_demux			= p_demux;
            tk->sub         	= sub;
            tk->b_rtcp_sync 	= false;
            tk->b_discard_trunc = true;
            tk->waiting     	= 0;
			tk->fmt.i_extra     = 0;
			tk->fmt.p_extra     = NULL;
            tk->i_pts       	= -1;
            tk->i_npt       	= 0.;
            tk->i_buffer    	= 1*1024*1024;//65536;	//sk_debug
            tk->p_buffer        = p_demux->readVidBuf;
            if( !tk->p_buffer )
            {
            	printf("free07\n");
                free( tk );
                delete iter;
                return RTSP_ERROR;
            }

            /* Value taken from mplayer */
            if( !strcmp( sub->mediumName(), "audio" ) )
            {
				if( !strcmp( sub->codecName(), "MP4A-LATM" ) )
                {
                    unsigned int i_extra;
                    uint8_t      *p_extra;

                    tk->fmt.i_codec = RTSP_CODEC_MP4A;

                    if( ( p_extra = parseStreamMuxConfigStr( sub->fmtp_config(),
                                                             i_extra ) ) )
                    {
                        tk->fmt.i_extra = i_extra;
                        tk->fmt.p_extra = malloc( i_extra );
                        memcpy( tk->fmt.p_extra, p_extra, i_extra );
                        delete[] p_extra;

                        strcpy( p_demux->audHdr, (char*)tk->fmt.p_extra );
                        p_demux->audHdrSize = i_extra;
                    }

                    /* Because the "faad" decoder does not handle the LATM
                     * data length field at the start of each returned LATM
                     * frame, tell the RTP source to omit it. */
                    ((MPEG4LATMAudioRTPSource*)sub->rtpSource())->omitLATMDataLengthField();
                }
				else if( !strcmp( sub->codecName(), "PCMU" ) )
				{
					tk->fmt.i_extra = 0;
					tk->fmt.i_codec = RTSP_CODEC_PCMU;
				}
            }
            else if( !strcmp( sub->mediumName(), "video" ) )
            {
				if( !strcmp( sub->codecName(), "H264" ) )
                {
                    unsigned int i_extra = 0;
                    uint8_t      *p_extra = NULL;

                    tk->fmt.i_codec = RTSP_CODEC_H264;
                    tk->fmt.b_packetized = false;

                    if((p_extra=parseH264ConfigStr( sub->fmtp_spropparametersets(),
                                                    i_extra ) ) )
                    {
                        tk->fmt.i_extra = i_extra;
                        tk->fmt.p_extra = malloc( i_extra );
                        memcpy( tk->fmt.p_extra, p_extra, i_extra );
                        delete[] p_extra;

                        strcpy( p_demux->vidHdr, (char*)tk->fmt.p_extra );
                        p_demux->vidHdrSize = i_extra;
                    }
                }
                else if( !strcmp( sub->codecName(), "MP4V-ES" ) )
                {
                    unsigned int i_extra;
                    uint8_t      *p_extra;

                    tk->fmt.i_codec = RTSP_CODEC_MPEG4;

                    if( ( p_extra = parseGeneralConfigStr( sub->fmtp_config(),
                                                           i_extra ) ) )
                    {
                        tk->fmt.i_extra = i_extra;
                        tk->fmt.p_extra = malloc( i_extra );
                        memcpy( tk->fmt.p_extra, p_extra, i_extra );
                        delete[] p_extra;
                    }
                }
                else if( !strcmp( sub->codecName(), "JPEG" ) )
                {
                    tk->fmt.i_codec = RTSP_CODEC_MJPG;
                }
            }

            if( sub->rtcpInstance() != NULL )
            {
                sub->rtcpInstance()->setByeHandler( StreamClose, tk );
            }

			p_sys->track = (live_track_t**)realloc( p_sys->track, sizeof( live_track_t ) * ( p_sys->i_track + 1 ) );
			p_sys->track[p_sys->i_track++] = tk;

       	}
    }

    delete iter;
    if( p_sys->i_track <= 0 ) i_return = RTSP_ERROR;

    /* Retrieve the starttime if possible */
    p_sys->i_npt_start = p_sys->ms->playStartTime();

    /* Retrieve the duration if possible */
    p_sys->i_npt_length = p_sys->ms->playEndTime();

    /* */
//    TRACE1_DEC( "setup start: %f stop:%f\n", p_sys->i_npt_start, p_sys->i_npt_length );
	
	return i_return;
}


/*****************************************************************************
 * Play: starts the actual playback of the stream
 *****************************************************************************/
static int Play( rtsp_object_t *p_demux )
{
	demux_sys_t *p_sys = (demux_sys_t*)p_demux->p_sys;

    if( p_sys->rtsp )
    {
        /* The PLAY */
        if( !p_sys->rtsp->sendPlayCommand( *p_sys->ms,continueAfterCheckResult, p_sys->i_npt_start,-1,(1.0F),NULL))
        {
            TRACE1_DEC( "RTSP PLAY failed %s\n", p_sys->env->getResultMsg() );
            return RTSP_ERROR;;
        }
		WaitResponse(p_demux);
		if (p_sys->b_error)
		{
			return RTSP_ERROR;
		}

    }

    /* Retrieve the starttime if possible */
    p_sys->i_npt_start = p_sys->ms->playStartTime();
    if( p_sys->ms->playEndTime() > 0 )
        p_sys->i_npt_length = p_sys->ms->playEndTime();

    TRACE1_DEC( "play start: %f stop:%f\n", p_sys->i_npt_start, p_sys->i_npt_length );

    return RTSP_SUCCESS;
}


/*****************************************************************************
 *
 *****************************************************************************/
static void StreamRead( void *p_private, unsigned int i_size,
                        unsigned int i_truncated_bytes, struct timeval pts,
                        unsigned int duration )
{
	live_track_t   	*tk	 		= (live_track_t*)p_private;
	rtsp_object_t	*p_demux 	= tk->p_demux;
	demux_sys_t		*p_sys		= (demux_sys_t*)p_demux->p_sys;

	p_demux->isKeyFrame = 0;
	int jumpSize = 0;

	if( !strcmp( tk->sub->mediumName(), "audio" ) )
		p_demux->streamType = RTSP_TYPE_AUDIO;
	if( !strcmp( tk->sub->mediumName(), "video" ) )
		p_demux->streamType = RTSP_TYPE_VIDEO;

	p_demux->codecType 	= tk->fmt.i_codec;
	p_demux->ts 		= pts.tv_sec * 1000 + (pts.tv_usec/1000);

    /* Retrieve NPT for this pts */
    tk->i_npt = tk->sub->getNormalPlayTime(pts);

    /* grow buffer if it looks like buffer is too small, but don't eat
     * up all the memory on strange streams */
    if( i_truncated_bytes > 0 )
    {
    	TRACE1_DEC( "i_truncated_bytes > 0\n" );
        if( tk->b_discard_trunc )
        {
            p_sys->event = 0xff;
            tk->waiting = 0;
            return;
        }
#if 0   //This part removed to ensure smooth working of bitstream based buffer, use.
        if( tk->i_buffer < 2000000 )
        {
            void *p_tmp;
            TRACE1_DEC( "lost %d bytes\n", i_truncated_bytes );
            TRACE1_DEC( "increasing buffer size to %d\n", tk->i_buffer * 2 );
            p_tmp = realloc( tk->p_buffer, tk->i_buffer * 2 );
            if( p_tmp == NULL )
            {
                TRACE1_DEC( "realloc failed\n" );
            }
            else
            {
                tk->p_buffer = (uint8_t*)p_tmp;
                tk->i_buffer *= 2;
            }
        }
#endif
    }
	if( tk->fmt.i_codec == RTSP_CODEC_H264 )
    {
        if( (tk->p_buffer[0] & 0x1f) >= 24 )
            TRACE1_DEC( "unsupported NAL type for H264\n");

		if(p_demux->isVfirst == 0){
			TRACE1_DEC( "First frame for H264 i_extra [%d] i_size[%d]\n",tk->fmt.i_extra, i_size );
			p_demux->isVfirst = 1;
		}

		if(tk->p_buffer[0] == 0x00 && tk->p_buffer[1] == 0x00 && tk->p_buffer[2] == 0x4D && tk->p_buffer[3] == 0x45)
		{
			jumpSize=124;
		}

#if 0
if(p_demux->id == 1)
			printf("%d:Iframe scan: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x ....[%d]\n",__LINE__,
			tk->p_buffer[jumpSize],tk->p_buffer[jumpSize+1],tk->p_buffer[jumpSize+2],tk->p_buffer[jumpSize+3],tk->p_buffer[jumpSize+4]
			,tk->p_buffer[jumpSize+5],tk->p_buffer[jumpSize+6],tk->p_buffer[jumpSize+7],tk->p_buffer[jumpSize+8],tk->p_buffer[jumpSize+9]
			,tk->p_buffer[jumpSize+10],tk->p_buffer[jumpSize+11],tk->p_buffer[jumpSize+12],tk->p_buffer[jumpSize+13],tk->p_buffer[jumpSize+14]
			,tk->p_buffer[jumpSize+15],tk->p_buffer[jumpSize+16],tk->p_buffer[jumpSize+17],tk->p_buffer[jumpSize+18],tk->p_buffer[jumpSize+19], i_size);
#endif

		if(p_demux->readVidBuf != NULL)
		{
			p_demux->readSizeV = (i_size-jumpSize);
			if(p_demux->readVidBuf[p_demux->rVBTail] == 0x67 || p_demux->readVidBuf[p_demux->rVBTail] == 0x65 || p_demux->readVidBuf[p_demux->rVBTail] == 0x27)	//I frame
				p_demux->isKeyFrame = 1;
			else if(p_demux->readVidBuf[0] == 0x00 && 
     				p_demux->readVidBuf[1] == 0x00 &&
     				p_demux->readVidBuf[2] == 0x00 &&
     				p_demux->readVidBuf[3] == 0x01 &&
     				(p_demux->readVidBuf[4] == 0x67 || 
     				p_demux->readVidBuf[4] == 0x65 ||
     				p_demux->readVidBuf[4] == 0x27))
				{
     			    p_demux->isKeyFrame = 1;
				}
		}			
	}

	else if( tk->fmt.i_codec == RTSP_CODEC_MPEG4 )
	{
#if 0
		printf("Iframe scan: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x ....[%d]\n",
			tk->p_buffer[jumpSize],tk->p_buffer[jumpSize+1],tk->p_buffer[jumpSize+2],tk->p_buffer[jumpSize+3],tk->p_buffer[jumpSize+4]
			,tk->p_buffer[jumpSize+5],tk->p_buffer[jumpSize+6],tk->p_buffer[jumpSize+7],tk->p_buffer[jumpSize+8],tk->p_buffer[jumpSize+9]
			,tk->p_buffer[jumpSize+10],tk->p_buffer[jumpSize+11],tk->p_buffer[jumpSize+12],tk->p_buffer[jumpSize+13],tk->p_buffer[jumpSize+14]
			,tk->p_buffer[jumpSize+15],tk->p_buffer[jumpSize+16],tk->p_buffer[jumpSize+17],tk->p_buffer[jumpSize+18],tk->p_buffer[jumpSize+19], i_size);
#endif
		if(tk->p_buffer[0]==0x00 && tk->p_buffer[1]==0x00 && tk->p_buffer[2]==0x01 && tk->p_buffer[3]==0xb6)
		{
			if((tk->p_buffer[4] & 0xF0) <= 0x30)
				p_demux->isKeyFrame = 1;
		}
        if(p_demux->isKeyFrame && tk->fmt.i_extra > 0 && p_demux->readVidBuf != NULL)
       	{
          if (p_demux->isVfirst == 0)
          {
       		TRACE1_DEC( "First frame for MPEG4 i_extra [%d] i_size[%d]\n",tk->fmt.i_extra, i_size );
       		p_demux->isVfirst=1;
           }
			memcpy( &p_demux->origReadVidBuf[p_demux->rVBTail], tk->fmt.p_extra, tk->fmt.i_extra);
			//memcpy( &p_demux->readVidBuf[p_demux->rVBTail + tk->fmt.i_extra], tk->p_buffer, i_size );
			p_demux->readSizeV = i_size+tk->fmt.i_extra;
       	}
       	else
   		{
   			if(p_demux->readVidBuf != NULL)
			{
				//memcpy(&p_demux->readVidBuf[p_demux->rVBTail], tk->p_buffer, i_size );
 		        p_demux->readSizeV = i_size+tk->fmt.i_extra;
	    	}
   		}
	}
	else if( tk->fmt.i_codec == RTSP_CODEC_MJPG )
	{
		p_demux->isKeyFrame = 1;
        //memcpy( &p_demux->readVidBuf[p_demux->rVBTail], tk->p_buffer, i_size );
        p_demux->readSizeV = i_size;
	}
    else if( tk->fmt.i_codec == RTSP_CODEC_MP4A )
   	{
   		if(p_demux->firstAudio >= 6)
		{
	   		if(p_demux->firstAudio == 6 && tk->fmt.i_extra > 0 && p_demux->readAudBuf != NULL)
			{
				TRACE1_DEC( "First frame for MP4A i_extra [%d]\n",tk->fmt.i_extra );
				memcpy( &p_demux->readAudBuf[0], tk->fmt.p_extra, tk->fmt.i_extra);
				memcpy( &p_demux->readAudBuf[tk->fmt.i_extra], tk->p_buffer, i_size );
	       		p_demux->readSizeA = i_size+tk->fmt.i_extra;

	       		p_demux->firstAudio++;
			}
			else
			{
				if(p_demux->readAudBuf != NULL)
				{
		        	memcpy( p_demux->readAudBuf, tk->p_buffer, i_size );
		        	p_demux->readSizeA = i_size;
		    	}
			}
		}
		else
			p_demux->firstAudio++;
   	}
    else if(tk->fmt.i_codec == RTSP_CODEC_PCMU)
    {
        memcpy( p_demux->readAudBuf, tk->p_buffer, i_size );
        p_demux->readSizeA = i_size;
    }

    /* warn that's ok */
    p_sys->event = 0xff;

    /* we have read data */
    tk->waiting = 0;
    p_sys->b_no_data 		= false;
    p_sys->i_no_data_ti 	= 0;
}

/*****************************************************************************
 *
 *****************************************************************************/
static void StreamClose( void *p_private )
{
    live_track_t   *tk 		= (live_track_t*)p_private;
    rtsp_object_t  *p_demux = tk->p_demux;
    demux_sys_t    *p_sys 	= (demux_sys_t*)p_demux->p_sys;

    TRACE1_DEC( "StreamClose\n" );

    p_sys->event 	= 0xff;
    p_sys->b_error 	= true;
}

/*****************************************************************************
 *
 *****************************************************************************/
static void TaskInterrupt( void *p_private )
{
    rtsp_object_t 	*p_demux = (rtsp_object_t*)p_private;
	demux_sys_t 	*p_sys 	 = (demux_sys_t*)p_demux->p_sys;

    p_sys->i_no_data_ti++;
	TRACE1_DEC( "TaskInterrupt[%d] !! i_no_data_ti[%d]\n",p_demux->id, p_sys->i_no_data_ti );
    /* Avoid lock */

    if(p_sys->i_no_data_ti >= 20){
    	p_sys->b_error = true;
    }

    p_sys->event = 0xff;
}

static unsigned char* parseH264ConfigStr( char const* configStr,
                                          unsigned int& configSize )
{
    char *dup, *psz;
    size_t i_records = 1;

    configSize = 0;

    if( configStr == NULL || *configStr == '\0' )
        return NULL;

    psz = dup = strdup( configStr );

    /* Count the number of commas */
    for( psz = dup; *psz != '\0'; ++psz )
    {
        if( *psz == ',')
        {
            ++i_records;
            *psz = '\0';
        }
    }

    size_t configMax = 5*strlen(dup);
    unsigned char *cfg = new unsigned char[configMax];
    psz = dup;
    for( size_t i = 0; i < i_records; ++i )
    {
        cfg[configSize++] = 0x00;
        cfg[configSize++] = 0x00;
        cfg[configSize++] = 0x00;
        cfg[configSize++] = 0x01;

        configSize += vlc_b64_decode_binary_to_buffer( cfg+configSize,
                                          configMax-configSize, psz );
        psz += strlen(psz)+1;
    }

    free( dup );
    return cfg;
}

size_t vlc_b64_decode_binary_to_buffer( uint8_t *p_dst, size_t i_dst, const char *p_src )
{
    static const int b64[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* 00-0F */
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* 10-1F */
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,  /* 20-2F */
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,  /* 30-3F */
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,  /* 40-4F */
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,  /* 50-5F */
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,  /* 60-6F */
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,  /* 70-7F */
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* 80-8F */
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* 90-9F */
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* A0-AF */
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* B0-BF */
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* C0-CF */
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* D0-DF */
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* E0-EF */
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1   /* F0-FF */
    };
    uint8_t *p_start = p_dst;
    uint8_t *p = (uint8_t *)p_src;

    int i_level;
    int i_last;

    for( i_level = 0, i_last = 0; (size_t)( p_dst - p_start ) < i_dst && *p != '\0'; p++ )
    {
        const int c = b64[(unsigned int)*p];
        if( c == -1 )
            continue;

        switch( i_level )
        {
            case 0:
                i_level++;
                break;
            case 1:
                *p_dst++ = ( i_last << 2 ) | ( ( c >> 4)&0x03 );
                i_level++;
                break;
            case 2:
                *p_dst++ = ( ( i_last << 4 )&0xf0 ) | ( ( c >> 2 )&0x0f );
                i_level++;
                break;
            case 3:
                *p_dst++ = ( ( i_last &0x03 ) << 6 ) | c;
                i_level = 0;
        }
        i_last = c;
    }

    return p_dst - p_start;
}



int main(char argc,char** agrv)
{

	rtsp_object_t*  rtspContext = (rtsp_object_t*)malloc(sizeof(rtsp_object_t));
	rtspContext->readVidBuf = (unsigned char*)malloc(3000000);
	rtspContext->readAudBuf = (unsigned char*)malloc(100000);
	//strcpy(rtspContext->rtspURL,"rtsp://admin:12345@192.168.0.6:1025/av_stream/ch1/main");
	strcpy(rtspContext->rtspURL,"rtsp://192.168.0.22:554/stream.h264");

	rtspOpen(rtspContext,0);

	while(1)
	{
		rtspRead(rtspContext);

		printf("%02x ",rtspContext->readVidBuf[0]);
	}


	return 0;
}