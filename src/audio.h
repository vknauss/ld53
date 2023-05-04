#pragma once

#ifdef __cplusplus
extern "C" {
#endif 

#include <stdbool.h>

typedef struct Audio Audio;
typedef struct Sound Sound;

Audio* newAudio(void);
void freeAudio(Audio* audio);

bool initAudio(Audio* audio);
void cleanupAudio(Audio* audio);

bool startAudioStream(Audio* audio);
bool stopAudioStream(Audio* audio);

Sound* newSound(const char* filename, bool loop);
void freeSound(Sound* sound);

void audioUpdate(Audio* audio);
void audioPlaySound(Audio* audio, Sound* sound, bool loop);

#ifdef __cplusplus
}
#endif 

