#include "audio.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <portaudio.h>
#include <vorbis/vorbisfile.h>

struct Sound
{
    float* samples;
    uint32_t numFrames;
    uint32_t numChannels;
};

typedef struct PlayingSoundInfo
{
    const Sound* sound;
    uint32_t currentFrame;
    bool loop;
    bool finished;
} PlayingSoundInfo;

typedef struct PlayingSoundsBuffer
{
    PlayingSoundInfo* sounds[256];
    bool finished[256];
    uint32_t numSounds;
} PlayingSoundsBuffer;

struct Audio
{
    PaStream* stream;
    uint32_t numChannels;
    bool dirty;
    PlayingSoundsBuffer buffers[2];
    PlayingSoundsBuffer masterBuffer;
    int playing;
    int lastPlaying;
};

Audio* newAudio(void)
{
    return malloc(sizeof(Audio));
}

void freeAudio(Audio* audio)
{
    free(audio);
}

PlayingSoundsBuffer* volatile currentBuffer = NULL;
PlayingSoundsBuffer* volatile confirmCurrentBuffer = NULL;

static int streamCallback(const void* inputBuffer, void* outputBuffer, unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags statusFlags, void* userData)
{
    Audio* audio = userData;

    if (confirmCurrentBuffer != currentBuffer)
    {
        printf("new buffer received. num sounds: %d\n", currentBuffer->numSounds);
    }

    PlayingSoundsBuffer* soundBuffer = confirmCurrentBuffer = currentBuffer;
    memset(outputBuffer, 0, sizeof(float) * audio->numChannels * framesPerBuffer);

    for (uint32_t i = 0; i < soundBuffer->numSounds; ++i)
    {
        if (soundBuffer->finished[i])
        {
            continue;
        }

        PlayingSoundInfo* info = soundBuffer->sounds[i];
        if (!info->sound || info->sound->numFrames == 0)
        {
            info->finished = true;
            soundBuffer->finished[i] = true;
            continue;
        }

        for (uint32_t i = 0; i < framesPerBuffer; ++i, ++info->currentFrame)
        {
            if (info->currentFrame >= info->sound->numFrames)
            {
                if (info->loop)
                {
                    info->currentFrame = 0;
                }
                else
                {
                    info->finished = true;
                    soundBuffer->finished[i] = true;
                    break;
                }
            }

            for (uint32_t j = 0; j < audio->numChannels; ++j)
            {
                uint32_t channel = j < info->sound->numChannels ? j : info->sound->numChannels - 1;
                uint64_t sampleIndex = info->currentFrame * info->sound->numChannels + channel;
                ((float*)outputBuffer)[i * audio->numChannels + j] += 0.1f * info->sound->samples[sampleIndex];
            }
        }
    }

    return paContinue;
}

// #define USE_PREFERRED
#ifdef USE_PREFERRED
/* On my machine the default device is HDMI out of my graphics card via raw dog ALSA,
 * so this is needed to select a higher-level host API by default.
 * I figure if you have pipewire or jack, you probably want to use them.
 * If not, fall back to pulse.
 * Presumably this won't affect platforms besides Linux.
 */
static const char* const PREFERRED_DEVICES[] = {
/*     "pipewire", */
    "jack",
    "pulse"
};
#endif

bool initAudio(Audio* audio)
{
    memset(audio, 0, sizeof(*audio));

    audio->playing = 0;
    audio->lastPlaying = 1;
    currentBuffer = &audio->buffers[audio->playing];

    PaError error;
    if ((error = Pa_Initialize()) != paNoError)
    {
        fprintf(stderr, "Failed to initialize PortAudio: %s\n", Pa_GetErrorText(error));
        return false;
    }

    PaDeviceIndex numDevices = Pa_GetDeviceCount();
    if (numDevices < 0)
    {
        fprintf(stderr, "Failed to enumerate audio devices\n");
        return false;
    }

    PaDeviceIndex deviceIndex = Pa_GetDefaultOutputDevice();
#ifdef USE_PREFERRED
    uint32_t numPreferredDevices = sizeof(PREFERRED_DEVICES) / sizeof(*PREFERRED_DEVICES);
    uint32_t preferredDeviceIndex = numPreferredDevices;
    for (int i = 0; i < numDevices; ++i)
    {
        const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(i);
        if (deviceInfo->maxOutputChannels <= 0)
        {
            continue;
        }
        for (uint32_t j = 0; j < preferredDeviceIndex; ++j)
        {
            if (strcmp(deviceInfo->name, PREFERRED_DEVICES[j]) == 0)
            {
                preferredDeviceIndex = j;
                deviceIndex = i;
                break;
            }
        }
    }
#endif

    const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(deviceIndex);
    if (!deviceInfo)
    {
        fprintf(stderr, "Failed to select an audio output device\n");
        return false;
    }

    /* printf("Selected audio output device: %s\n", deviceInfo->name);
    printf("\tChannels: %d\n\tSample rate: %f\n", deviceInfo->maxOutputChannels, deviceInfo->defaultSampleRate);
    printf("\tDefault latencies: Low: %f, High: %f\n", deviceInfo->defaultLowOutputLatency, deviceInfo->defaultHighOutputLatency); */

    audio->numChannels = deviceInfo->maxOutputChannels > 2 ? 2 : deviceInfo->maxOutputChannels;

    PaStreamParameters streamParameters;
    memset(&streamParameters, 0, sizeof(streamParameters));
    streamParameters.channelCount = audio->numChannels;
    streamParameters.device = deviceIndex;
    streamParameters.sampleFormat = paFloat32;
    streamParameters.suggestedLatency = deviceInfo->defaultHighOutputLatency;

    if ((error = Pa_OpenStream(&audio->stream, NULL, &streamParameters, deviceInfo->defaultSampleRate, paFramesPerBufferUnspecified, 0, streamCallback, audio)) != paNoError)
    {
        fprintf(stderr, "Failed to open audio output stream: %s\n", Pa_GetErrorText(error));
        return false;
    }

    return true;
}

void cleanupAudio(Audio* audio)
{
    if (!audio)
    {
        return;
    }

    // printf("closing stream\n");

    PaError error;
    if ((error = Pa_CloseStream(audio->stream)) != paNoError)
    {
        fprintf(stderr, "Failed to close audio output stream: %s\n", Pa_GetErrorText(error));
    }

    // printf("terminating portaudio\n");

    if ((error = Pa_Terminate()) != paNoError)
    {
        fprintf(stderr, "Failed to terminate PortAudio: %s\n", Pa_GetErrorText(error));
    }

    for (int i = 0; i < 2; ++i)
    {
        /* for (uint32_t j = 0; j < audio->buffers[i].numSounds; ++j)
        {
            free(audio->buffers[i].sounds[j]);
            audio->buffers[i].sounds[j] = NULL;
        } */
        audio->buffers[i].numSounds = 0;
    }
    for (uint32_t i = 0; i < audio->masterBuffer.numSounds; ++i)
    {
        free(audio->masterBuffer.sounds[i]);
    }
    audio->masterBuffer.numSounds = 0;
}

bool startAudioStream(Audio* audio)
{
    if (audio)
    {
        PaError error = Pa_StartStream(audio->stream);
        if (error == paNoError)
        {
            return true;
        }
        fprintf(stderr, "Failed to start audio output stream\n");
    }
    return false;
}

bool stopAudioStream(Audio* audio)
{
    if (audio)
    {
        // printf("stopping audio stream\n");
        PaError error = Pa_StopStream(audio->stream);
        if (error == paNoError)
        {
            return true;
        }
        fprintf(stderr, "Failed to stop audio output stream\n");
    }
    return false;
}

Sound* newSound(const char* filename, bool loop)
{
    OggVorbis_File file;
    if (ov_fopen(filename, &file) != 0)
    {
        fprintf(stderr, "Failed to open OggVorbis file: %s\n", filename);
        return NULL;
    }

    vorbis_info* info = ov_info(&file, -1);

    /* printf("channels: %d, rate: %ld, bitrate_lower: %ld, bitrate_nominal: %ld, bitrate_upper: %ld\n", info->channels, info->rate, info->bitrate_lower, info->bitrate_nominal, info->bitrate_upper);

    printf("pcm total: %ld\n", ov_pcm_total(&file, -1));
    printf("time total: %f\n", ov_time_total(&file, -1)); */

    Sound* sound = malloc(sizeof(Sound));
    memset(sound, 0, sizeof(*sound));
    sound->numFrames = ov_pcm_total(&file, -1);
    sound->numChannels = info->channels;
    sound->samples = malloc(sizeof(float) * sound->numChannels * sound->numFrames);
    /* sound->loop = loop;
    sound->finished = false; */

    bool error = false;
    size_t totalRead = 0;
    float** pcmChannels = NULL;
    while (totalRead < sound->numFrames)
    {
        int bitstream;
        long numRead = ov_read_float(&file, &pcmChannels, sound->numFrames - totalRead, &bitstream);
        if (numRead <= 0)
        {
            error = true;
            break;
        }

        for (int i = 0; i < numRead; ++i, ++totalRead)
        {
            for (uint32_t j = 0; j < sound->numChannels; ++j)
            {
                sound->samples[sound->numChannels * totalRead + j] = pcmChannels[j][i];
            }
        }
    }
    ov_clear(&file);

    if (error)
    {
        fprintf(stderr, "Failed to read PCM data from file: %s", filename);
        free(sound->samples);
        free(sound);
        return NULL;
    }

    return sound;
}

void freeSound(Sound* sound)
{
    if (!sound)
    {
        return;
    }
    free(sound->samples);
    free(sound);
}

void audioUpdate(Audio* audio)
{
    if (audio->dirty)
    {
        while (confirmCurrentBuffer != currentBuffer);
        printf("swapping sound buffers\n");

        PlayingSoundsBuffer* nextPlaying = &audio->buffers[audio->lastPlaying];
        uint32_t count = 0;
        for (uint32_t i = 0; i < audio->masterBuffer.numSounds; ++i)
        {
            if (audio->masterBuffer.sounds[i]->finished)
            {
                free(audio->masterBuffer.sounds[i]);
            }
            else
            {
                audio->masterBuffer.sounds[count] = audio->masterBuffer.sounds[i];
                nextPlaying->sounds[count] = audio->masterBuffer.sounds[i];
                ++count;
            }
        }
        audio->masterBuffer.numSounds = count;
        nextPlaying->numSounds = count;

        int lastPlaying = audio->playing;
        audio->playing = audio->lastPlaying;
        audio->lastPlaying = lastPlaying;
        currentBuffer = nextPlaying;
        audio->dirty = false;
    }
}

void audioPlaySound(Audio* audio, Sound* sound, bool loop)
{
    // PlayingSoundsBuffer* buffer = &audio->buffers[audio->lastPlaying];
    PlayingSoundsBuffer* buffer = &audio->masterBuffer;
    if (buffer->numSounds < 256)
    {
        printf("adding sound\n");
        PlayingSoundInfo* info = malloc(sizeof(PlayingSoundInfo));
        memset(info, 0, sizeof(*info));
        info->sound = sound;
        info->loop = loop;
        buffer->sounds[buffer->numSounds++] = info;
        audio->dirty = true;
    }
}
