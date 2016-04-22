/*
 * This program is free software. It comes without any warranty, to
 * the extent permitted by applicable law. You can redistribute it
 * and/or modify it under the terms of the Do What The Fuck You Want
 * To Public License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details.
 */

/* ChangeLog:
 * 1 - Initial program
 * 2 - Changed getAVAudioData to not always grab another packet before decoding
 *     to prevent buffering more compressed data than needed
 * 3 - Update to use avcodec_decode_audio3 and fix for decoders that need
 *     aligned output pointers
 * 4 - Fixed bits/channels format assumption
 * 5 - Improve time handling
 * 6 - Remove use of ALUT
 */
#define inline __inline
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <assert.h>

#include <al.h>
#include <alc.h>
//#include <alext.h>

#ifndef _WIN32
#include <unistd.h>
#define Sleep(x) usleep((x)*1000)
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

/* Opaque handles to files and streams. The main app doesn't need to concern
 * itself with the internals */
typedef struct MyFile *FilePtr;
typedef struct MyStream *StreamPtr;

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

/**** Helper functions ****/

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


typedef struct PacketList {
    AVPacket pkt;
    struct PacketList *next;
} PacketList;

struct MyStream {
    AVCodecContext *CodecCtx;
    int StreamIdx;

    PacketList *Packets;

    char *DecodedData;
    size_t DecodedDataSize;

    FilePtr parent;
};

struct MyFile {
    AVFormatContext *FmtCtx;
    StreamPtr *Streams;
    size_t StreamsSize;
};

/* This opens a file with ffmpeg and sets up the streams' information */
FilePtr openAVFile(const char *fname)
{
    static int done = 0;
    FilePtr file;

    /* We need to make sure ffmpeg is initialized. Optionally silence warning
     * output from the lib */
    if(!done) {av_register_all();
    av_log_set_level(AV_LOG_ERROR);
    done = 1;}

    file = (FilePtr)calloc(1, sizeof(*file));
    if(file && avformat_open_input(&file->FmtCtx, fname, NULL, NULL) == 0)
    {
        /* After opening, we must search for the stream information because not
         * all formats will have it in stream headers */
        if(av_find_stream_info(file->FmtCtx) >= 0)
            return file;
        av_close_input_file(file->FmtCtx);
    }
    free(file);
    return NULL;
}

/* This closes/frees an opened file and any of its streams. Pretty self-
 * explanitory... */
void closeAVFile(FilePtr file)
{
    size_t i;

    if(!file) return;

    for(i = 0;i < file->StreamsSize;i++)
    {
        StreamPtr stream = file->Streams[i];

        while(stream->Packets)
        {
            PacketList *self = stream->Packets;
            stream->Packets = self->next;

            av_free_packet(&self->pkt);
            av_free(self);
        }

        avcodec_close(stream->CodecCtx);
        av_free(stream->DecodedData);
        free(stream);
    }
    free(file->Streams);

    av_close_input_file(file->FmtCtx);
    free(file);
}

/* This reports certain information from the file, eg, the number of audio
 * streams */
int getAVFileInfo(FilePtr file, int *numaudiostreams)
{
    unsigned int i;
    int audiocount = 0;

    if(!file) return 1;
    for(i = 0;i < file->FmtCtx->nb_streams;i++)
    {
        if(file->FmtCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
            audiocount++;
    }
    *numaudiostreams = audiocount;
    return 0;
}

/* This retrieves a handle for the given audio stream number (generally 0, but
 * some files can have multiple audio streams in one file) */
StreamPtr getAVAudioStream(FilePtr file, int streamnum)
{
    unsigned int i;
    if(!file) return NULL;
    for(i = 0;i < file->FmtCtx->nb_streams;i++)
    {
        if(file->FmtCtx->streams[i]->codec->codec_type != AVMEDIA_TYPE_AUDIO)
            continue;

        if(streamnum == 0)
        {
            StreamPtr stream;
            AVCodec *codec;
            void *temp;
            size_t j;

            /* Found the requested stream. Check if a handle to this stream
             * already exists and return it if it does */
            for(j = 0;j < file->StreamsSize;j++)
            {
                if(file->Streams[j]->StreamIdx == (int)i)
                    return file->Streams[j];
            }

            /* Doesn't yet exist. Now allocate a new stream object and fill in
             * its info */
            stream = (StreamPtr)calloc(1, sizeof(*stream));
            if(!stream) return NULL;

            stream->parent = file;
            stream->CodecCtx = file->FmtCtx->streams[i]->codec;
            stream->StreamIdx = i;

            /* Try to find the codec for the given codec ID, and open it */
            codec = avcodec_find_decoder(stream->CodecCtx->codec_id);
            if(!codec || avcodec_open(stream->CodecCtx, codec) < 0)
            {
                free(stream);
                return NULL;
            }

            /* Allocate space for the decoded data to be stored in before it
             * gets passed to the app */
            stream->DecodedData = (char*)av_malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);
            if(!stream->DecodedData)
            {
                avcodec_close(stream->CodecCtx);
                free(stream);
                return NULL;
            }

            /* Append the new stream object to the stream list. The original
             * pointer will remain valid if realloc fails, so we need to use
             * another pointer to watch for errors and not leak memory */
            temp = realloc(file->Streams, (file->StreamsSize+1) *
                                          sizeof(*file->Streams));
            if(!temp)
            {
                avcodec_close(stream->CodecCtx);
                av_free(stream->DecodedData);
                free(stream);
                return NULL;
            }
            file->Streams = (StreamPtr*)temp;
            file->Streams[file->StreamsSize++] = stream;
            return stream;
        }
        streamnum--;
    }
    return NULL;
}

/* Returns information about the given audio stream. Returns 0 on success. */
int getAVAudioInfo(StreamPtr stream, ALuint *rate, ALenum *channels, ALenum *type)
{
    if(!stream || stream->CodecCtx->codec_type != AVMEDIA_TYPE_AUDIO)
        return 1;

    if(type)
    {
        if(stream->CodecCtx->sample_fmt == AV_SAMPLE_FMT_U8)
            *type = AL_UNSIGNED_BYTE;
        else if(stream->CodecCtx->sample_fmt == AV_SAMPLE_FMT_S16)
            *type = AL_SHORT;
        else if(stream->CodecCtx->sample_fmt == AV_SAMPLE_FMT_S32)
            *type = AL_INT;
        else if(stream->CodecCtx->sample_fmt == AV_SAMPLE_FMT_FLT)
            *type = AL_FLOAT;
        else if(stream->CodecCtx->sample_fmt == AV_SAMPLE_FMT_DBL)
            *type = AL_DOUBLE;
        else
            return 1;
    }
    if(channels)
    {
		if (stream->CodecCtx->channel_layout == 0) {
			if (stream->CodecCtx->channels == 1) stream->CodecCtx->channel_layout = AV_CH_LAYOUT_MONO;
			if (stream->CodecCtx->channels == 2) stream->CodecCtx->channel_layout = AV_CH_LAYOUT_STEREO;
			if (stream->CodecCtx->channel_layout == 0) return 1;
		}

        if(stream->CodecCtx->channel_layout == AV_CH_LAYOUT_MONO)
            *channels = AL_MONO;
        else if(stream->CodecCtx->channel_layout == AV_CH_LAYOUT_STEREO)
            *channels = AL_STEREO;
        else if(stream->CodecCtx->channel_layout == AV_CH_LAYOUT_QUAD)
            *channels = AL_QUAD;
        else if(stream->CodecCtx->channel_layout == AV_CH_LAYOUT_5POINT1)
            *channels = AL_5POINT1;
        else if(stream->CodecCtx->channel_layout == AV_CH_LAYOUT_7POINT1)
            *channels = AL_7POINT1;
        else
            return 1;
    }
    if(rate) *rate = stream->CodecCtx->sample_rate;

    return 0;
}

/* Used by getAV*Data to search for more compressed data, and buffer it in the
 * correct stream. It won't buffer data for streams that the app doesn't have a
 * handle for. */
static int getNextPacket(FilePtr file, int streamidx)
{
    PacketList *packet = (PacketList*)av_malloc(sizeof(PacketList));
    packet->next = NULL;

next_pkt:
    while(av_read_frame(file->FmtCtx, &packet->pkt) >= 0)
    {
        StreamPtr *iter = file->Streams;
        StreamPtr *iter_end = iter + file->StreamsSize;

        /* Check each stream the user has a handle for, looking for the one
         * this packet belongs to */
        while(iter != iter_end)
        {
            if((*iter)->StreamIdx == packet->pkt.stream_index)
            {
                PacketList **last = &(*iter)->Packets;
                while(*last != NULL)
                    last = &(*last)->next;

                *last = packet;
                if((*iter)->StreamIdx == streamidx)
                    return 1;

                packet = (PacketList*)av_malloc(sizeof(PacketList));
                packet->next = NULL;
                goto next_pkt;
            }
            iter++;
        }
        /* Free the packet and look for another */
        av_free_packet(&packet->pkt);
    }

    av_free(packet);
    return 0;
}

/* The "meat" function. Decodes audio and writes, at most, length bytes into
 * the provided data buffer. Will only return less for end-of-stream or error
 * conditions. Returns the number of bytes written. */
int getAVAudioData(StreamPtr stream, void *data, int length)
{
    int dec = 0;

    if(!stream || stream->CodecCtx->codec_type != AVMEDIA_TYPE_AUDIO)
        return 0;

    while(dec < length)
    {
        /* If there's no decoded data, find some */
        if(stream->DecodedDataSize == 0)
        {
            int size;
            int len;

            /* If there's no more input data, break and return what we have */
            if(!stream->Packets &&
               !getNextPacket(stream->parent, stream->StreamIdx))
                break;

            /* Decode some data, and check for errors */
            size = AVCODEC_MAX_AUDIO_FRAME_SIZE;
            while((len=avcodec_decode_audio3(stream->CodecCtx,
                                             (int16_t*)stream->DecodedData, &size,
                                             &stream->Packets->pkt)) == 0)
            {
                PacketList *self;

                if(size > 0)
                    break;

                self = stream->Packets;
                stream->Packets = self->next;

                av_free_packet(&self->pkt);
                av_free(self);

                if(!stream->Packets)
                    break;
            }
            if(!stream->Packets)
                continue;

            if(len < 0)
                break;

            if(len > 0)
            {
                if(len < stream->Packets->pkt.size)
                {
                    /* Move the remaining data to the front and clear the end
                     * bits */
                    int remaining = stream->Packets->pkt.size - len;
                    memmove(stream->Packets->pkt.data,
                            &stream->Packets->pkt.data[len],
                            remaining);
                    memset(&stream->Packets->pkt.data[remaining], 0,
                           stream->Packets->pkt.size - remaining);
                    stream->Packets->pkt.size -= len;
                }
                else
                {
                    PacketList *self = stream->Packets;
                    stream->Packets = self->next;

                    av_free_packet(&self->pkt);
                    av_free(self);
                }
            }

            /* Set the output buffer size */
            stream->DecodedDataSize = size;
        }

        if(stream->DecodedDataSize > 0)
        {
            /* Get the amount of bytes remaining to be written, and clamp to
             * the amount of decoded data we have */
            size_t rem = length-dec;
            if(rem > stream->DecodedDataSize)
                rem = stream->DecodedDataSize;

            /* Copy the data to the app's buffer and increment */
            memcpy(data, stream->DecodedData, rem);
            data = (char*)data + rem;
            dec += rem;

            /* If there's any decoded data left, move it to the front of the
             * buffer for next time */
            if(rem < stream->DecodedDataSize)
                memmove(stream->DecodedData, &stream->DecodedData[rem],
                        stream->DecodedDataSize - rem);
            stream->DecodedDataSize -= rem;
        }
    }

    /* Return the number of bytes we were able to get */
    return dec;
}

/**** The main app ****/

/* Create a simple signal handler for SIGINT so ctrl-c cleanly exits. */
static volatile int quitnow = 0;
static void handle_sigint(int signum)
{
    quitnow = 1;
    signal(signum, SIG_DFL);
}

/* Some helper functions to get the name from the channel and type enums. */
static const char *ChannelsName(ALenum chans)
{
    switch(chans)
    {
    case AL_MONO: return "Mono";
    case AL_STEREO: return "Stereo";
    case AL_REAR: return "Rear";
    case AL_QUAD: return "Quadraphonic";
    case AL_5POINT1: return "5.1 Surround";
    case AL_6POINT1: return "6.1 Surround";
    case AL_7POINT1: return "7.1 Surround";
    }
    return "Unknown";
}

static const char *TypeName(ALenum type)
{
    switch(type)
    {
    case AL_BYTE: return "S8";
    case AL_UNSIGNED_BYTE: return "U8";
    case AL_SHORT: return "S16";
    case AL_UNSIGNED_SHORT: return "U16";
    case AL_INT: return "S32";
    case AL_UNSIGNED_INT: return "U32";
    case AL_FLOAT: return "Float32";
    case AL_DOUBLE: return "Float64";
    }
    return "Unknown";
}

/* Define the number of buffers and buffer size (in bytes) to use. 3 buffers is
 * a good amount (one playing, one ready to play, another being filled). 32256
 * is a good length per buffer, as it fits 1, 2, 4, 6, 7, 8, 12, 14, 16, 24,
 * 28, and 32 bytes-per-frame sizes. */
#define NUM_BUFFERS 3
#define BUFFER_SIZE 32256

int main(int argc, char **argv)
{
    /* The device and context handles to play with */
    ALCdevice *device;
    ALCcontext *ctx;

    /* Here are the buffers and source to play out through OpenAL with */
    ALuint buffers[NUM_BUFFERS];
    ALuint source;

    ALint state; /* This will hold the state of the source */
    ALbyte *data; /* A temp data buffer for getAVAudioData to write to and pass
                   * to OpenAL with */
    int count; /* The number of bytes read from getAVAudioData */
    int i; /* An iterator for looping over the filenames */

    /* Print out usage if no file was specified */
    if(argc < 2)
    {
        fprintf(stderr, "Usage: %s <filenames...>\n", argv[0]);
        return 1;
    }

    /* Set up our signal handler to run on SIGINT (ctrl-c) */
    if(signal(SIGINT, handle_sigint) == SIG_ERR)
    {
        fprintf(stderr, "Unable to set handler for SIGINT!\n");
        return 1;
    }

    /* Open and initialize a device with default settings */
    device = alcOpenDevice(NULL);
    if(!device)
    {
        fprintf(stderr, "Could not open a device!\n");
        return 1;
    }

    ctx = alcCreateContext(device, NULL);
    if(ctx == NULL || alcMakeContextCurrent(ctx) == ALC_FALSE)
    {
        if(ctx != NULL)
            alcDestroyContext(ctx);
        alcCloseDevice(device);
        fprintf(stderr, "Could not set a context!\n");
        return 1;
    }

    /* Generate the buffers and source */
    alGenBuffers(NUM_BUFFERS, buffers);
    if(alGetError() != AL_NO_ERROR)
    {
        alcMakeContextCurrent(NULL);
        alcDestroyContext(ctx);
        alcCloseDevice(device);
        fprintf(stderr, "Could not create buffers...\n");
        return 1;
    }
    alGenSources(1, &source);
    if(alGetError() != AL_NO_ERROR)
    {
        alDeleteBuffers(NUM_BUFFERS, buffers);
        alcMakeContextCurrent(NULL);
        alcDestroyContext(ctx);
        alcCloseDevice(device);
        fprintf(stderr, "Could not create source...\n");
        return 1;
    }

    /* Set parameters so mono sources won't distance attenuate */
    alSourcei(source, AL_SOURCE_RELATIVE, AL_TRUE);
    alSourcei(source, AL_ROLLOFF_FACTOR, 0);
    if(alGetError() != AL_NO_ERROR)
    {
        alDeleteSources(1, &source);
        alDeleteBuffers(NUM_BUFFERS, buffers);
        alcMakeContextCurrent(NULL);
        alcDestroyContext(ctx);
        alcCloseDevice(device);
        fprintf(stderr, "Could not set source parameters...\n");
        return 1;
    }

	data = (char*)malloc(BUFFER_SIZE);
    if(data == NULL)
    {
        alDeleteSources(1, &source);
        alDeleteBuffers(NUM_BUFFERS, buffers);
        alcMakeContextCurrent(NULL);
        alcDestroyContext(ctx);
        alcCloseDevice(device);
        fprintf(stderr, "Could not create temp buffer...\n");
        return 1;
    }

    /* Play each file listed on the command line */
    for(i = 1;i < argc && !quitnow;i++)
    {
        static ALenum old_format;
        static ALuint old_rate;
        /* The base time to use when determining the playback time from the
         * source. */
        static int64_t basetime;
        static int64_t filetime;
        /* Handles for the audio stream */
        FilePtr file;
        StreamPtr stream;
        /* The format of the output stream */
        ALenum format = 0;
        ALenum channels;
        ALenum type;
        ALuint rate;

        /* Open the file and get the first stream from it */
        file = openAVFile(argv[i]);
        stream = getAVAudioStream(file, 0);
        if(!stream)
        {
            closeAVFile(file);
            fprintf(stderr, "Could not open audio in %s\n", argv[i]);
            continue;
        }

        /* Get the stream format, and figure out the OpenAL format. We use the
         * AL_EXT_MCFORMATS extension to provide output of Quad, 5.1, and 7.1
         * audio streams */
        if(getAVAudioInfo(stream, &rate, &channels, &type) != 0)
        {
            closeAVFile(file);
            fprintf(stderr, "Error getting audio info for %s\n", argv[i]);
            continue;
        }

        if(type == AL_UNSIGNED_BYTE)
        {
            if(channels == AL_MONO) format = AL_FORMAT_MONO8;
            else if(channels == AL_STEREO) format = AL_FORMAT_STEREO8;
            else if(alIsExtensionPresent("AL_EXT_MCFORMATS"))
            {
                if(channels == AL_QUAD) format = alGetEnumValue("AL_FORMAT_QUAD8");
                else if(channels == AL_5POINT1) format = alGetEnumValue("AL_FORMAT_51CHN8");
                else if(channels == AL_7POINT1) format = alGetEnumValue("AL_FORMAT_71CHN8");
            }
        }
        else if(type == AL_SHORT)
        {
            if(channels == AL_MONO) format = AL_FORMAT_MONO16;
            else if(channels == AL_STEREO) format = AL_FORMAT_STEREO16;
            else if(alIsExtensionPresent("AL_EXT_MCFORMATS"))
            {
                if(channels == AL_QUAD) format = alGetEnumValue("AL_FORMAT_QUAD16");
                else if(channels == AL_5POINT1) format = alGetEnumValue("AL_FORMAT_51CHN16");
                else if(channels == AL_7POINT1) format = alGetEnumValue("AL_FORMAT_71CHN16");
            }
        }
        else if(type == AL_FLOAT && alIsExtensionPresent("AL_EXT_FLOAT32"))
        {
            if(channels == AL_MONO) format = alGetEnumValue("AL_FORMAT_MONO_FLOAT32");
            else if(channels == AL_STEREO) format = alGetEnumValue("AL_FORMAT_STEREO_FLOAT32");
            else if(alIsExtensionPresent("AL_EXT_MCFORMATS"))
            {
                if(channels == AL_QUAD) format = alGetEnumValue("AL_FORMAT_QUAD32");
                else if(channels == AL_5POINT1) format = alGetEnumValue("AL_FORMAT_51CHN32");
                else if(channels == AL_7POINT1) format = alGetEnumValue("AL_FORMAT_71CHN32");
            }
        }
        else if(type == AL_DOUBLE && alIsExtensionPresent("AL_EXT_DOUBLE"))
        {
            if(channels == AL_MONO) format = alGetEnumValue("AL_FORMAT_MONO_DOUBLE");
            else if(channels == AL_STEREO) format = alGetEnumValue("AL_FORMAT_STEREO_DOUBLE");
        }

        if(format == 0 || format == -1)
        {
            closeAVFile(file);
            fprintf(stderr, "Unhandled format (%s, %s) for %s",
                    ChannelsName(channels), TypeName(type), argv[i]);
            continue;
        }

        /* If the format of the last file matches the current one, we can skip
         * the initial load and let the processing loop take over (gap-less
         * playback!) */
        count = 1;
        if(format == old_format && rate == old_rate)
        {
            /* When skipping the initial load of a file (because the previous
             * one is using the same exact format), just remove the length of
             * the previous file from the base. This is so the timing will be
             * from the beginning of this file, which won't start playing until
             * the next buffer to get queued does */
            basetime -= filetime;
            filetime = 0;
        }
        else
        {
            int j;

            /* Wait for the last song to finish playing */
            do {
                Sleep(10);
                alGetSourcei(source, AL_SOURCE_STATE, &state);
            } while(alGetError() == AL_NO_ERROR && state == AL_PLAYING);
            /* Rewind the source position and clear the buffer queue */
            alSourceRewind(source);
            alSourcei(source, AL_BUFFER, 0);
            /* Reset old variables */
            basetime = 0;
            filetime = 0;
            old_format = format;
            old_rate = rate;

            /* Fill and queue the buffers */
            for(j = 0;j < NUM_BUFFERS;j++)
            {
                ALint size, numchans, numbits;

                /* Make sure we get some data to give to the buffer */
                count = getAVAudioData(stream, data, BUFFER_SIZE);
                if(count <= 0) break;

                /* Buffer the data with OpenAL and queue the buffer onto the
                 * source */
                alBufferData(buffers[j], format, data, count, rate);
                alSourceQueueBuffers(source, 1, &buffers[j]);

                /* For each successful buffer queued, increment the filetime */
                alGetBufferi(buffers[j], AL_SIZE, &size);
                alGetBufferi(buffers[j], AL_CHANNELS, &numchans);
                alGetBufferi(buffers[j], AL_BITS, &numbits);
                filetime += size / numchans * 8 / numbits;
            }

            /* Now start playback! */
            alSourcePlay(source);
            if(alGetError() != AL_NO_ERROR)
            {
                closeAVFile(file);
                fprintf(stderr, "Error starting playback...\n");
                continue;
            }
        }

        fprintf(stderr, "Playing %s (%s, %s, %dhz)\n", argv[i],
                TypeName(type), ChannelsName(channels), rate);
        while(count > 0 && !quitnow)
        {
            /* Check if any buffers on the source are finished playing */
            ALint processed = 0;
            alGetSourcei(source, AL_BUFFERS_PROCESSED, &processed);
            if(processed == 0)
            {
                /* All buffers are full. Check if the source is still playing.
                 * If not, restart it, otherwise, print the time and rest */
                alGetSourcei(source, AL_SOURCE_STATE, &state);
                if(alGetError() != AL_NO_ERROR)
                {
                    fprintf(stderr, "\nError checking source state...\n");
                    break;
                }
                if(state != AL_PLAYING)
                {
                    alSourcePlay(source);
                    if(alGetError() != AL_NO_ERROR)
                    {
                        closeAVFile(file);
                        fprintf(stderr, "\nError restarting playback...\n");
                        break;
                    }
                }
                else
                {
                    int64_t curtime = 0;
                    if(basetime >= 0)
                    {
                        ALint offset = 0;
                        alGetSourcei(source, AL_SAMPLE_OFFSET, &offset);
                        curtime = basetime + offset;
                    }
                    fprintf(stderr, "\rTime: %ld:%05.02f", (long)(curtime/rate/60),
                            (float)(curtime%(rate*60))/(float)rate);
                    Sleep(10);
                }
                continue;
            }
            /* Read the next chunk of data and refill the oldest buffer */
            count = getAVAudioData(stream, data, BUFFER_SIZE);
            if(count > 0)
            {
                ALuint buf = 0;
                alSourceUnqueueBuffers(source, 1, &buf);
                if(buf != 0)
                {
                    ALint size, numchans, numbits;

                    /* For each successfully unqueued buffer, increment the
                     * base time. */
                    alGetBufferi(buf, AL_SIZE, &size);
                    alGetBufferi(buf, AL_CHANNELS, &numchans);
                    alGetBufferi(buf, AL_BITS, &numbits);
                    basetime += size / numchans * 8 / numbits;

                    alBufferData(buf, format, data, count, rate);
                    alSourceQueueBuffers(source, 1, &buf);

                    alGetBufferi(buf, AL_SIZE, &size);
                    alGetBufferi(buf, AL_CHANNELS, &numchans);
                    alGetBufferi(buf, AL_BITS, &numbits);
                    filetime += size / numchans * 8 / numbits;
                }
                if(alGetError() != AL_NO_ERROR)
                {
                    fprintf(stderr, " !!! Error buffering data !!!\n");
                    break;
                }
            }
        }
        fprintf(stderr, "\rTime: %ld:%05.02f\n", (long)(filetime/rate/60),
                (float)(filetime%(rate*60))/(float)rate);

        /* All done with this file. Close it and go to the next */
        closeAVFile(file);
    }
    fprintf(stderr, "Done.\n");

    if(!quitnow)
    {
        /* All data has been streamed in. Wait until the source stops playing it */
        do {
            Sleep(10);
            alGetSourcei(source, AL_SOURCE_STATE, &state);
        } while(alGetError() == AL_NO_ERROR && state == AL_PLAYING);
    }

    /* All files done. Delete the source and buffers, and close OpenAL */
    free(data);
    alDeleteSources(1, &source);
    alDeleteBuffers(NUM_BUFFERS, buffers);

    alcMakeContextCurrent(NULL);
    alcDestroyContext(ctx);
    alcCloseDevice(device);

    return 0;
}