// uiWin32.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "LXZWindowAPI.h"
#include "zmouse.h"
#include <MMSYSTEM.H>
#include "WINUSER.H"
#include <stdlib.h>
#include <stdio.h>
#include <Psapi.h>
#pragma comment (lib,"Psapi.lib")
#include <list>
#include "LXZLock.h"

#ifdef WIN32
//#pragma comment(lib,"alut.lib ")
#pragma comment(lib,"OpenAL32.lib")
#endif

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/pixfmt.h"
//新版里的图像转换结构需要引入的头文件
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "al.h"
#include "alc.h"
};

struct _call_vframe{
	uint8_t* data;
	int      width;
	int      height;
	int      win_width;
	int      win_height;
};

struct _call_aframe{
	uint8_t* data;
	int      size;
};

#define STATE_STOP  0
#define STATE_PAUSE 1
#define STATE_PLAY  2

struct _Play{
	char file[256];
	int  state;
};

static _Play playCtrl;
static void* m_ctx = NULL;

static int decoded_frame = 0;
static Linux_Win_Lock vlock;
static std::list<_call_vframe*> vList;

static Linux_Win_Lock vlock_alloc;
static std::list<_call_vframe*> vallocList;
_call_vframe* alloc_vframe(int w, int h){
	_call_vframe* frame = NULL;
	vlock_alloc.Linux_Win_Locked();
	if (vallocList.size()){
		frame = vallocList.front();
		vallocList.pop_front();
	}
	vlock_alloc.Linux_Win_UnLocked();
	if (frame == NULL){
		frame = new _call_vframe;
		frame->height = h;
		frame->width = w;
		frame->data = (uint8_t*)new RGBA[w*h];
	}
	return frame;
}

void delete_vframe(_call_vframe* frame){
	vlock_alloc.Linux_Win_Locked();
	vallocList.push_back(frame);
	vlock_alloc.Linux_Win_UnLocked();
}


static void CallbackUpdateTexture(const char* cmd, void* ctx, void* data){
	_call_vframe* frame = (_call_vframe*)data;
	LXZAPI_UpdateTexture(cmd, (RGBA*)frame->data, frame->width, frame->height,false);	
	//delete frame;
	decoded_frame--;
	delete_vframe(frame);
}

static ALuint  wBitsPerSample = 16;
static ALsizei dwSampleRate = 44100;
static ALshort  wChannels = 2;
static ALenum format = AL_FORMAT_STEREO16;
static ALenum sample = 0;
static ALenum layout = 0;
static int    vfps = 25;
static int    afps = 25;

void display_video(){

	static LXZuint32 updateTime = 0;
	while (playCtrl.state != STATE_STOP){
		if (LXZAPI_timeGetSystemTime() < updateTime){
			Sleep(1);
			continue;
		}

		_call_vframe* frame = NULL;
		vlock.Linux_Win_Locked();
		int size = vList.size();		
		if (size == 0){
			vlock.Linux_Win_UnLocked();
			Sleep(1);
			continue;
		}
		else{
			frame = vList.front();
			vList.pop_front();
			vlock.Linux_Win_UnLocked();
		}

		updateTime = LXZAPI_timeGetSystemTime() + 1000.0f/vfps;
		

		LXZAPI_AsyncCallback(CallbackUpdateTexture, "video.png", m_ctx, frame);
	}

}



ALfloat SourcePos[3] = { 0.0, 0.0, 0.0 };                        //?¨???¨???¨???¨?￡?¨???¨?????√?√??à?√∫?¨???¨?????√?√??à?????¨?????√?√??à??￠?à???????√?√??à?????à??????√?√??à?√??à??à????√?√????√???????√?√??à??∞?¨???¨?????√?√??à?√∫?¨???¨???¨???¨?μ???√?√??à?????à?√∫???√?√????√￠????¨???¨???¨???¨?￡?¨???¨???¨???¨?????√?√??à?√??à??à????√?√??à?√′???√?√????√??￥???√?√??à?????à??à????√?√??¨??￠???√?√??à?√??à??à????√?√??à?√′???√?√????√￠???¨???¨?????√?√??à?√??¨?￠?¨???¨???¨???¨?μ???√?√??à?????à?√∫???√?√????√￠??????√?√??à?√??à??à????√?√????√￠??￠???√?√??¨√ü?¨???¨?????√?√??à??a§?¨???¨???¨???¨?±???√?√??à?????à??à????√?√??à?√o
ALfloat SourceVel[3] = { 0.0, 0.0, 0.0 };
static std::list<_call_aframe*> sndLst;
static Linux_Win_Lock lock;

#define NUMBUFFERS              (4)
ALuint	uiBuffers[NUMBUFFERS] = {-1,-1,-1,-1};

static void play_sound(){

	alGenBuffers(NUMBUFFERS, uiBuffers);

	static ALuint buffer = uiBuffers[0];
	if (buffer == (ALuint)-1){
		return;
	}

	static ALuint source = -1;
	if (source != (ALuint)-1){
	/*	ALint state;
		alGetSourcei(source, AL_SOURCE_STATE, &state);
		if (state == AL_PLAYING){
			return;
		}
		else{
			ALint iBuffersProcessed = 0;
			alGetSourcei(source, AL_BUFFERS_PROCESSED, &iBuffersProcessed);
			if (iBuffersProcessed == 0){
				ALint iQueuedBuffers = 0;
				alGetSourcei(source, AL_BUFFERS_QUEUED, &iQueuedBuffers);
				if (iQueuedBuffers)
				{
					alSourcePlay(source);
					return;
				}
			}
		}

		alSourceUnqueueBuffers(source, 1, &buffer);
		alDeleteSources(1, &source);
		source = -1;*/
	}

	alGenSources(1, &source);
	ALenum err = alGetError();	
	if (source == (ALuint)-1 && err != AL_NO_ERROR){
		ALenum err=alGetError();
		return;
	}

	static LXZuint32 updateTime = 0;
	while (playCtrl.state != STATE_STOP){
		if (LXZAPI_timeGetSystemTime() < updateTime){
		//	Sleep(1);
		//	continue;
		}

		ALint state;
		alGetSourcei(source, AL_SOURCE_STATE, &state);
		if (state == AL_PLAYING){
			Sleep(1);
			continue;
		}
		else if (updateTime!=0){
			ALint iBuffersProcessed = 0;
			alGetSourcei(source, AL_BUFFERS_PROCESSED, &iBuffersProcessed);
			if (iBuffersProcessed == 0){
				Sleep(0);
				continue;
			}

			alSourceUnqueueBuffers(source, 1, &buffer);
		//	alDeleteSources(1, &source);
		//	alGenSources(1, &source);
		}

		_call_aframe* frame = NULL;
		lock.Linux_Win_Locked();
		int size = sndLst.size();		
		if (size == 0){
			lock.Linux_Win_UnLocked();
			Sleep(0);
			continue;
		}

		frame=sndLst.front();
		sndLst.pop_front();
		lock.Linux_Win_UnLocked();

		alSourcei(source, AL_SOURCE_RELATIVE, AL_TRUE);
		alSourcei(source, AL_ROLLOFF_FACTOR, 0);
		alSourcef(source, AL_GAIN, 1.0f);
		
		updateTime = LXZAPI_timeGetSystemTime() + 1000.0f / afps;

		size = frame->size;
		alBufferData(buffer, format, frame->data, size, dwSampleRate);
		err = alGetError();
		alSourceQueueBuffers(source, 1, &buffer);
		err = alGetError();
		alSourcePlay(source);
		err = alGetError();
		alGetSourcei(source, AL_SOURCE_STATE, &state);
		delete[]frame->data;
		delete frame;
	}
}

void printf_msg(const char* msg){
	MessageBox(NULL, msg, "msg", MB_OK);
}

static ALCdevice *device = NULL;
void InitOPENAL()
{
	//
	int			i4Ret = 0;
	ALenum		error;
	ALCcontext	*context = NULL;

	device = alcOpenDevice(NULL);
	//assert(NULL != device);
	if (NULL != device)
	{
		error = alcGetError(device);
		context = alcCreateContext(device, 0);
	//	assert(NULL != context);
		if (NULL != context)
		{
			alcMakeContextCurrent(context);
		}
		else {
			error = alGetError();
			printf("Error Create Context! %x\n", error);
		}		
	}
	else
	{
		error = alGetError();
		printf("Error Open Device! %x\n", error);
	}

}


#define AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

#ifndef AL_SOFT_buffer_samples
/* Sample types */
#define AL_BYTE                                  0x1400
#define AL_UNSIGNED_BYTE                         0x1401
#define AL_SHORT                                 0x1402
#define AL_UNSIGNED_SHORT                        0x1403
#define AL_INT                                   0x1404
#define AL_UNSIGNED_INT                          0x1405
#define AL_FLOAT                                 0x1406
#define AL_DOUBLE                                0x1407

/* Channel configurations */
#define AL_MONO                                  0x1500
#define AL_STEREO                                0x1501
#define AL_REAR                                  0x1502
#define AL_QUAD                                  0x1503
#define AL_5POINT1                               0x1504 /* (WFX order) */
#define AL_6POINT1                               0x1505 /* (WFX order) */
#define AL_7POINT1                               0x1506 /* (WFX order) */
#endif


/* Returns information about the given audio stream. Returns 0 on success. */
int getAVAudioInfo(AVCodecContext* CodecCtx, ALuint *rate, ALenum *channels, ALenum *type)
{
	if (!CodecCtx || CodecCtx->codec_type != AVMEDIA_TYPE_AUDIO)
		return 1;

	if (type)
	{
		if (CodecCtx->sample_fmt == AV_SAMPLE_FMT_U8)
			*type = AL_UNSIGNED_BYTE;
		else if (CodecCtx->sample_fmt == AV_SAMPLE_FMT_S16)
			*type = AL_SHORT;
		else if (CodecCtx->sample_fmt == AV_SAMPLE_FMT_S32)
			*type = AL_INT;
		else if (CodecCtx->sample_fmt == AV_SAMPLE_FMT_FLT)
			*type = AL_FLOAT;
		else if (CodecCtx->sample_fmt == AV_SAMPLE_FMT_DBL)
			*type = AL_DOUBLE;
		else
			return 1;
	}
	if (channels)
	{
		if (CodecCtx->channel_layout == AV_CH_LAYOUT_MONO)
			*channels = AL_MONO;
		else if (CodecCtx->channel_layout == AV_CH_LAYOUT_STEREO)
			*channels = AL_STEREO;
		else if (CodecCtx->channel_layout == AV_CH_LAYOUT_QUAD)
			*channels = AL_QUAD;
		else if (CodecCtx->channel_layout == AV_CH_LAYOUT_5POINT1)
			*channels = AL_5POINT1;
		else if (CodecCtx->channel_layout == AV_CH_LAYOUT_7POINT1)
			*channels = AL_7POINT1;
		else
			return 1;
	}
	if (rate) *rate = CodecCtx->sample_rate;

	return 0;
}

int ffmpeg_init(){
	av_register_all();
	avformat_network_init();
	AVFormatContext* fmtctx = avformat_alloc_context();
	if (avformat_open_input(&fmtctx, playCtrl.file, NULL, NULL) != 0){
		printf_msg("cannot open file.\n");
		return -1;
	}

	if (av_find_stream_info(fmtctx)<0)
	{
		printf_msg("Couldn't find stream information.\n");
		return -1;
	}

	int videoindex = -1;
	int audioindex = -1;
	for (int i = 0; i < fmtctx->nb_streams; i++){
		if (fmtctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO&&videoindex<0){
			videoindex = i;			
		}
		else if (fmtctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO&&audioindex < 0){
			audioindex = i;
		}
	}

	if (videoindex == -1||audioindex==-1){
		return -1;
	}
	
	// video
	AVCodecContext* vCodectx = fmtctx->streams[videoindex]->codec;
	AVCodec* vCodec = avcodec_find_decoder(vCodectx->codec_id);
	if (vCodec == NULL){
		printf_msg("Codec not found.\n");
		return -1;
	}

	if (avcodec_open(vCodectx, vCodec)<0){
		printf_msg("Could not open codec.\n");
		return -1;
	}
		
	AVFrame* pFrameYUV = avcodec_alloc_frame();
	AVFrame* pFrameRGB = avcodec_alloc_frame();
	uint8_t *out_buffer = new uint8_t[avpicture_get_size(PIX_FMT_BGR24, vCodectx->width, vCodectx->height)];
	uint8_t *rgba_buffer = new uint8_t[vCodectx->width*vCodectx->height*sizeof(RGBA)];
	avpicture_fill((AVPicture *)pFrameRGB, out_buffer, PIX_FMT_BGR24, vCodectx->width, vCodectx->height);

	// audio
	AVCodecContext* aCodectx = fmtctx->streams[audioindex]->codec;
	AVCodec* aCodec = avcodec_find_decoder(aCodectx->codec_id);
	if (aCodec == NULL){
		printf_msg("Codec not found.\n");
		return -1;
	}

	if (avcodec_open2(aCodectx, aCodec,NULL)<0){
		printf_msg("Could not open codec.\n");
		return -1;
	}

	if (aCodectx->channel_layout == 0) {
	//	if (aCodectx->channels == 1) aCodectx->channel_layout = AV_CH_LAYOUT_MONO;
	//	if (aCodectx->channels == 2) aCodectx->channel_layout = AV_CH_LAYOUT_STEREO;
	//	if (aCodectx->channel_layout == 0) printf_msg("WARNING! channel_layout is 0.\n");
	}

	if (aCodectx->sample_fmt != AV_SAMPLE_FMT_S16){
		aCodectx->request_sample_fmt = AV_SAMPLE_FMT_S16;
	}


	format = AL_FORMAT_MONO16;



	//int16_t* audio_buf = new int16_t[AUDIO_BUFFER_SIZE*2];
	AVFrame* pFrameAudio = avcodec_alloc_frame();

	//
	vfps = fmtctx->streams[videoindex]->r_frame_rate.num / fmtctx->streams[videoindex]->r_frame_rate.den;
	afps = vfps;// fmtctx->streams[audioindex]->r_frame_rate.num / fmtctx->streams[audioindex]->r_frame_rate.den;
	dwSampleRate = aCodectx->sample_rate;
	wChannels = aCodectx->channels;
	

	//
	//Out Audio Param  
	uint64_t out_channel_layout = AV_CH_LAYOUT_STEREO;
	//AAC:1024  MP3:1152  
	int out_nb_samples = aCodectx->frame_size;
	AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;
	int out_sample_rate = 44100;
	int out_channels = av_get_channel_layout_nb_channels(out_channel_layout);
	//Out Buffer Size  
	int out_buffer_size = av_samples_get_buffer_size(NULL, out_channels, out_nb_samples, out_sample_fmt, 1);
	uint8_t *aout_buffer = (uint8_t *)av_malloc(MAX_AUDIO_FRAME_SIZE*2);
	dwSampleRate = out_sample_rate;
	sample = out_sample_fmt;
	wChannels = out_channels;

	wBitsPerSample = av_get_bytes_per_sample(out_sample_fmt) * 8;
	if (wBitsPerSample == 8)
	{
		if (wChannels == 1)
			format = AL_FORMAT_MONO8;
		else if (wChannels == 2)
			format = AL_FORMAT_STEREO8;
	}
	else if (wBitsPerSample == 16)
	{
		if (wChannels == 1)
			format = AL_FORMAT_MONO16;
		else if (wChannels == 2)
			format = AL_FORMAT_STEREO16;
	}


	//FIX:Some Codec's Context Information is missing  
	int64_t in_channel_layout = av_get_default_channel_layout(aCodectx->channels);
	//Swr  
	struct SwrContext *au_convert_ctx;
	au_convert_ctx = swr_alloc();
	au_convert_ctx = swr_alloc_set_opts(au_convert_ctx, out_channel_layout, out_sample_fmt, out_sample_rate,
		in_channel_layout, aCodectx->sample_fmt, aCodectx->sample_rate, 0, NULL);
	swr_init(au_convert_ctx);
	
	//
	int ret, got_picture;
	struct SwsContext *img_convert_ctx;
	img_convert_ctx = sws_getContext(vCodectx->width, vCodectx->height, vCodectx->pix_fmt, vCodectx->width, vCodectx->height, PIX_FMT_RGB24, SWS_BICUBIC, NULL, NULL, NULL);

	int y_size = vCodectx->width*vCodectx->height*sizeof(RGBA)+MAX_AUDIO_FRAME_SIZE*2;

	AVPacket *packet = (AVPacket *)malloc(sizeof(AVPacket));
	av_new_packet(packet, y_size);

	//输出一下信息-----------------------------
	//printf_msg("文件信息-----------------------------------------\n");
	av_dump_format(fmtctx, 0, playCtrl.file, 0);
	//printf_msg("-------------------------------------------------\n");

	//
	while (playCtrl.state!=STATE_STOP){
		if (playCtrl.state == STATE_PAUSE){
			Sleep(1);
			continue;
		}

		if (decoded_frame >= 4) {
			Sleep(1);
			continue;
		}

		if (packet->data==NULL){
			av_new_packet(packet, y_size);
		}

		if (av_read_frame(fmtctx, packet) < 0){
			Sleep(1);
			continue;
		}

		if (packet->stream_index == videoindex){
			ret = avcodec_decode_video2(vCodectx, pFrameYUV, &got_picture, packet);
			if (ret < 0){
				printf_msg("decode error\n");
				return -1;
			}

			if (got_picture)
			{
				_call_vframe* frame = alloc_vframe(vCodectx->width, vCodectx->height);

				// YUV to RGB				
				sws_scale(img_convert_ctx, (const uint8_t* const*)pFrameYUV->data, pFrameYUV->linesize, 0, vCodectx->height, pFrameRGB->data, pFrameRGB->linesize);

				//	
				byte* s = out_buffer;
				byte* d = (byte*)frame->data;
				for (int h = 0; h < vCodectx->height; h++){
					for (int w = 0; w < vCodectx->width; w++){
						*d++ = *s++;
						*d++ = *s++;
						*d++ = *s++;
						*d++ = 0xFF;
					}
				}

				//
				decoded_frame++;				
				vlock.Linux_Win_Locked();
				vList.push_back(frame);
				vlock.Linux_Win_UnLocked();

			}
		}
		else if (packet->stream_index == audioindex){			
			
			while (packet->size > 0){
				// Initialize SWR context
			/*	SwrContext* swrContext = swr_alloc_set_opts(NULL,
					aCodectx->channel_layout, AV_SAMPLE_FMT_U8, aCodectx->sample_rate,
					aCodectx->channel_layout, aCodectx->sample_fmt, aCodectx->sample_rate,
					0, NULL);
				int result = swr_init(swrContext);

				// Create destination sample buffer
				uint8_t* destBuffer[8] = { NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL};
				int destBufferLinesize=pFrameAudio->linesize[0];					
				av_samples_alloc(destBuffer,
					&destBufferLinesize,
					wChannels,
					2048,
					AV_SAMPLE_FMT_U8,
					1);*/

				// Decode audio frame
				int got_frame = 0;
				int decoded = avcodec_decode_audio4(aCodectx, pFrameAudio, &got_frame, packet);
				if (decoded < 0)
				{
					printf_msg("Error decoding audio frame.");
					break;
				}

				if (decoded < packet->size)
				{
					/* Move the unread data to the front and clear the end bits */
					int remaining = packet->size - decoded;
					memmove(packet->data, &packet->data[decoded], remaining);
					av_shrink_packet(packet, remaining);
				}

				// Frame is complete, store it in audio frame queue
				if (got_frame)
				{
					//	int outputSamples = swr_convert(p_swrContext,
					//		p_destBuffer, p_destLinesize,
					//		(const uint8_t**)p_frame->extended_data, p_frame->nb_samples);
					memset(aout_buffer, 0x00, MAX_AUDIO_FRAME_SIZE * 2);
					int outputSamples = swr_convert(au_convert_ctx,
						&aout_buffer, MAX_AUDIO_FRAME_SIZE,
						(const uint8_t**)&pFrameAudio->data[0], pFrameAudio->linesize[0]);

					int bufferSize = av_get_bytes_per_sample(AV_SAMPLE_FMT_S16)
						* outputSamples;

					//swr_convert(au_convert_ctx, &aout_buffer, MAX_AUDIO_FRAME_SIZE, (const uint8_t **)pFrameAudio->data, pFrameAudio->nb_samples);

					//int bufferSize = pFrameAudio->linesize[0];// av_get_bytes_per_sample(AV_SAMPLE_FMT_U8) * wChannels
					//* outputSamples;

					int64_t duration = pFrameAudio->pkt_dts;
					int64_t dts = pFrameAudio->pkt_dts;

					// Create the audio frame
					_call_aframe* frame = new _call_aframe;
					frame->size = bufferSize;
					frame->data = new uint8_t[bufferSize];
					memcpy(frame->data, aout_buffer, bufferSize);
					//LXZAPI_AsyncCallback(CallbackPlaySound, "sound", m_ctx, frame);

					lock.Linux_Win_Locked();
					sndLst.push_back(frame);
					lock.Linux_Win_UnLocked();
				}
							
				packet->size -= decoded;
				//packet->data += ret;
			}			
	
		}
		else{
			av_free_packet(packet);
			continue;
		}
		av_free_packet(packet);
		Sleep(5);
				
	}

	delete[] rgba_buffer;
	delete[] out_buffer;
	av_free(pFrameYUV);
	av_free(pFrameRGB);
	av_free(pFrameAudio);
	avcodec_close(vCodectx);
	avformat_close_input(&fmtctx);
	return 0;
}

//LXZAPI_UpdateTexture("___destop", buf, _destop.GetWidht(), _destop.GetHeight());
extern "C" LXZuint32 Win32KeyToLXZKey(LXZuint32 wkey);
static LRESULT CALLBACK LXZGuiProc(HWND window,
	UINT msg,
	WPARAM wParam,
	LPARAM lParam)
{
	// root
	uiHWND hRoot = LXZWindowMgr_GetRoot();

	switch (msg)
	{
	case WM_CLOSE:
	{
					 PostQuitMessage(0);
					 DestroyWindow(window);
					 break;
	}
	case WM_CREATE:
	{
					  int scrWidth, scrHeight;
					  RECT rect;
					  scrWidth = GetSystemMetrics(SM_CXSCREEN);
					  scrHeight = GetSystemMetrics(SM_CYSCREEN);
					  GetWindowRect(window, &rect);
					  rect.left = (scrWidth - rect.right) / 2;
					  rect.top = (scrHeight - rect.bottom) / 2;
					  //移动窗口到指定的位置
					  SetWindowPos(window, HWND_TOP, rect.left, rect.top, rect.right, rect.bottom, SWP_SHOWWINDOW);

					  /*

					  ILXZCoreCfg* cfg = LXZGetCfg();
					  uiHWND hRoot = LXZWindowMgr_GetRoot();
					  int w = LXZWindow_GetWidth(hRoot);
					  int h = LXZWindow_GetHeight(hRoot);

					  char buf[1024] = {0};
					  sprintf(buf,"ScreenW:%d ScreenH:%d ScaleX:%f ScaleY:%f nOffsetX:%d nOffsetY:%d w:%d h:%d\r\n",
					  cfg->nScreenWidth,
					  cfg->nScreenHeight,
					  cfg->fAutoScaleX,
					  cfg->fAutoScaleY,
					  cfg->nAutoOffsetX,
					  cfg->nAutoOffsetY,
					  w,
					  h);

					  MessageBox(NULL, buf, "log", MB_OK);*/
	}
		break;
	case (WM_USER + 300) :
	{
	 ICGuiRender();
	 break;
	}
	case WM_ERASEBKGND:
		return TRUE;
		break;
	case WM_CAPTURECHANGED:
	{
	}
		break;
	case WM_MOVE:
	{
	}
		break;
	case WM_PAINT:
	{
		RECT rect;
		if (GetUpdateRect(window, &rect, FALSE) == TRUE)
		{
			_LXZRect rc;
			//COPY_RECT(rc, rect);
			rc.left = rect.left;
			rc.top = rect.top;
			rc.right = rect.right;
			rc.bottom = rect.bottom;
			LXZWindowInvalidate(hRoot, rc, uitrue);
			ICGUIDCInvalidate();
		}
		break;
	}
	case WM_SIZE:
	{
		static bool bPause = false;
		switch (wParam)
		{
		case SIZE_RESTORED:
		{
			int w = LOWORD(lParam);
			int h = HIWORD(lParam);
			ILXZCoreCfg* cfg = LXZGetCfg();
			if (bPause)
			{
				bPause = false;
				ICGUIDCResume();
				ICGUIDCCreate(w, h, window);
				ICGuiResume();
			}
			else
			{
				ICGUIDCCreate(w, h, window);
			}
		}
			break;
		case SIZE_MINIMIZED:
		{
								bPause = true;
								ICGuiPause();
		}
			break;
		default:
		{
					int w = LOWORD(lParam);
					int h = HIWORD(lParam);
					ICGUIDCCreate(w, h, window);
		}
			break;
		}

		break;
	}
	case WM_LBUTTONDOWN:
	{
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		LXZWindowMgr_OnLClickDown(x, y);
		break;
	}
	case WM_LBUTTONUP:
	{
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		LXZWindowMgr_OnLClickUp(x, y);

		//
		/*	static int ref = -1;
		CLuaArgs args;
		LuaCallRet ret;
		uiHWND hRoot = LXZWindowMgr_GetRoot();
		LXZAPI_RunObjFunc(hRoot,"CLXZWindow", ref,"GetWidth",args,&ret);

		char buf[32];
		sprintf(buf, "width=%d", ret.i);
		MessageBox(NULL, buf, "Width", MB_OK);*/

		//
		break;
	}
	case WM_LBUTTONDBLCLK:
	{
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		//LXZWindowMgr_OnLDBClick(x, y);
		break;
	}
	case WM_RBUTTONDOWN:
	{
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		LXZWindowMgr_OnRClickDown(x, y);
		break;
	}
	case WM_RBUTTONUP:
	{
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		LXZWindowMgr_OnRClickUp(x, y);
		break;
	}
	case WM_RBUTTONDBLCLK:
	{
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		LXZWindowMgr_OnRDBClick(x, y);
		break;
	}
	case WM_MOUSEMOVE:
	{
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		LXZWindowMgr_OnMouseMove(x, y);
		break;
	}
	case WM_MOUSEWHEEL:
	{
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		LXZWindowMgr_OnMouseWheel(x, y, wParam);
		break;
	}
	case WM_KEYDOWN:
	{
		LXZWindowMgr_OnKeyDown(wParam);
		break;
	}
	case WM_KEYUP:
	{
		LXZWindowMgr_OnKeyUp(wParam);
		break;
	}
	case WM_CHAR:
	{
		LXZWindowMgr_OnChar(wParam);
		break;
	}
	}

	return DefWindowProc(window, msg, wParam, lParam);
}


void ThreadPlay(void* data){
	ffmpeg_init();
}

void ThreadPlayAudio(void* data){
	play_sound();
}


void ThreadPlayVideo(void* data){
	display_video();
}

static void ccPlay(const char* param, ILXZAlloc* strResult){
	playCtrl.state = STATE_PLAY;
	strcpy(playCtrl.file, param);
	
	m_ctx = LXZAPI_GetNowLaeContext();	
	LXZAPI_NewThread(ThreadPlay, &playCtrl);
	LXZAPI_NewThread(ThreadPlayAudio, &playCtrl);
	LXZAPI_NewThread(ThreadPlayVideo, &playCtrl);
}

static void ccStop(const char* param, ILXZAlloc* strResult){
	playCtrl.state = STATE_STOP;
}

static void ccResume(const char* param, ILXZAlloc* strResult){
	playCtrl.state = STATE_PLAY;
}


static void ccPause(const char* param, ILXZAlloc* strResult){
	playCtrl.state = STATE_PAUSE;
}

template<typename T, typename U>
void assign(T& dest, U src)
{
	dest = (T&)src;
}

int APIENTRY WinMain(HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPSTR     lpCmdLine,
	int       nCmdShow)
{

	// Load  resource
	//LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_MAINFRAME));
	HCURSOR hArrow = LoadCursor(NULL, IDC_ARROW);
	SetCursor(hArrow);

	//
	int w = 0;
	int h = 0;

	int iscaption = -1;
	int len = strlen(lpCmdLine);
	ILXZCoreCfg* cfg = LXZGetCfg();
	if (len>3)
	{
		int island = 0;
		char* szDebugUI = new char[len + 1];
		memset(szDebugUI, 0x00, len + 1);
		sscanf(lpCmdLine, "%s %dx%d %d %d", szDebugUI, &w, &h, &island, &iscaption);

		char szTmp[256];
		char fileName[1024];
		char szBasePath[1024] = { 0 };
		GetModuleFileName(NULL, fileName, MAX_PATH);
		_splitpath(szDebugUI, szBasePath, szTmp, NULL, NULL);
		strcat(szBasePath, szTmp);
		LXZFileSystem_SetBase(szBasePath);

		//ILXZCoreCfg* cfg = LXZGetCfg();
		if (island == 0)
		{
			cfg->SetInt("nScreenHeight", NULL, h);
			cfg->SetInt("nScreenWidth", NULL, w);
		}
		else
		{
			cfg->SetInt("nScreenHeight", NULL, w);
			cfg->SetInt("nScreenWidth", NULL, h);
		}

		//	strcpy(szDebugUI, lpCmdLine);
		cfg->SetCString("DebugUI", NULL, (const char*)szDebugUI);
		cfg->SetInt("nDPI", NULL, ICGuiGetSystemDPI());

		//char buf[256];
		//sprintf(buf, "%s, %d, %d", szDebugUI, w, iscaption);
		//MessageBox(NULL, buf, "log", MB_OK);

		//
		if (iscaption == 1){
			SetCfgBool(notcaption, false);
		}
		else{
			SetCfgBool(notcaption, true);
		}
	}

	//	
	HANDLE hProcess = ::GetCurrentProcess();
	TCHAR* procName = new TCHAR[MAX_PATH];
	memset(procName, 0x00, sizeof(TCHAR)*MAX_PATH);
	GetProcessImageFileName(hProcess, procName, MAX_PATH);
	int  s = strlen(procName);
	s--;
	while (s > 0){
		if (procName[s] == '\\' || procName[s] == '/'){
			break;
		}

		s--;
	}

	//
	char* shortName = new TCHAR[MAX_PATH]; //[MAX_PATH] = { 0 };
	memset(shortName, 0x00, sizeof(TCHAR)*MAX_PATH);
	memcpy(shortName, &procName[s + 1], strlen(&procName[s + 1]) - 4);
	//MessageBox(NULL, shortName, "log", MB_OK);

	char iconfile[256] = { 0 };
	strcpy(iconfile, shortName);
	strcat(iconfile, ".ico");

	//
	HICON hIcon = (HICON)LoadImage( // returns a HANDLE so we have to cast to HICON
		NULL,             // hInstance must be NULL when loading from a file
		iconfile,         // the icon file name
		IMAGE_ICON,       // specifies that the file is an icon
		0,                // width of the image (we'll specify default later on)
		0,                // height of the image
		LR_LOADFROMFILE |  // we want to load a file (as opposed to a resource) LR_DEFAULTSIZE |   // default metrics based on the type (IMAGE_ICON, 32x32)		
		LR_SHARED         // let the system release the handle when it's no longer used
		);

	// register the window class
	WNDCLASSEX wc;
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS | CS_OWNDC;
	wc.lpfnWndProc = LXZGuiProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = GetModuleHandle(NULL);;
	wc.hIcon = hIcon;
	wc.hCursor = hArrow;
	wc.hbrBackground = NULL;
	wc.lpszMenuName = NULL;
	wc.lpszClassName = "LXZGame";
	wc.hIconSm = hIcon;

	if (RegisterClassEx(&wc) == 0)
	{
		MessageBox(NULL, "Error: Could not register the window class", "Engulf", MB_OK);
		return 0;
	}

	GetCfgInt(xxnScreenWidth, nScreenWidth);
	GetCfgInt(xxnScreenHeight, nScreenHeight);

	if (strcmp(shortName, "LXZWin32R") == 0){
		ICGuiRun(eOpenGLES, false, "default.cfg");
	}
	else{
		char cfg_name[MAX_PATH];
		strcpy(cfg_name, shortName);
		strcat(cfg_name, ".cfg");
		ICGuiRun(eOpenGLES, false, cfg_name);
	}

	LXZAPI_SetFrameTime(1000/60);
	LXZOutputDebugStr("RegisterClass Success!\r\n");

	uiHWND hRoot = LXZWindowMgr_GetRoot();
	//	ILXZCoreCfg* cfg = LXZGetCfg();
	SetCfgBool(IsAutoScale, true);
	//GetCfgInt(_xnScreenHeight, nScreenHeight);
	if (xxnScreenWidth <= 0)
	{
		float fScale = LXZAPI_GetDPIScale();
		cfg->SetInt("nScreenHeight", NULL, LXZWindowGetHeight(hRoot)*fScale);
		cfg->SetInt("nScreenWidth", NULL, LXZWindowGetWidth(hRoot)*fScale);
	}

	//
	InitOPENAL();
	LXZSystem_RegisterAPI("Play", ccPlay);
	LXZSystem_RegisterAPI("Pause", ccPause);
	LXZSystem_RegisterAPI("Stop", ccStop);
	LXZSystem_RegisterAPI("Resume", ccResume);
	


	GetCfgBool(notcaption, notcaption);
	//char buf[256];
	//sprintf(buf, "%d, %d", notcaption, iscaption);
	//MessageBox(NULL, buf, "log", MB_OK);

	DWORD style = WS_POPUP;
	if (iscaption == -1){
		if (!notcaption){
			style = WS_CAPTION | WS_POPUPWINDOW | WS_MINIMIZEBOX;
		}
		else if (notcaption){
			iscaption = 0;
		}
	}
	else{
		if (iscaption){
			style = WS_CAPTION | WS_POPUPWINDOW | WS_MINIMIZEBOX;
		}
	}

	// create the game window
	//GetCfgInt(nScreenWidth, nScreenWidth);
	int nScreenWidth = cfg->GetInt("nScreenWidth", NULL);
	int nScreenHeight = cfg->GetInt("nScreenHeight", NULL);
	HWND hWnd = CreateWindowEx(WS_EX_APPWINDOW | WS_EX_WINDOWEDGE,
		"LXZGame",
		shortName,
		style,
		CW_USEDEFAULT,
		0,
		(iscaption == 1) ? nScreenWidth + 8 : nScreenWidth,
		(iscaption == 1) ? nScreenHeight + 30 : nScreenHeight,
		NULL,
		NULL,
		GetModuleHandle(NULL),
		NULL);


	HWND hDesktop = GetDesktopWindow();
	LXZWindowOnLoad(hRoot);
	ICGuiAutoScale();
	ICGuiCheckLanguage();

	//
	LXZSystem_SetKeyTransferFunc(Win32KeyToLXZKey);

	SetForegroundWindow(hWnd);
	SetFocus(hWnd);
	SetTimer(hWnd, WM_TIMER, 60, NULL);
	SetCfgObj(hwnd, hWnd);
	SetCfgBool(IsBlendDestop, false);
	
	while (TRUE)
	{
		MSG		msg;
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
				break;

			TranslateMessage(&msg);
			DispatchMessage(&msg);
			continue;
		}
		else
		{
			ICGuiUpdateState();
			Sleep(10);
		}
	}

	ICGuiDestroy();

	return 0;
}



