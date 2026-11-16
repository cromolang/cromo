// The part of std.audio in C (D547): the sounds, decoded from WAV by SDL and from Ogg
// Vorbis by stb_vorbis, and their voices, each one a stream of SDL bound to the device
// of a player, which SDL mixes with the others. A short sound is decoded once, into
// samples of 16 bits; a long one in Ogg Vorbis, as a music, is decoded while it plays,
// by the callback of its stream on the thread of the audio of SDL.
// Without the C library: the headers are those of runtime/libc, as for the images
// (runtime/gui/image.c), the mathematics and the memory come from SDL, and stb_vorbis
// takes all its memory from a block given to it, so it calls neither malloc nor alloca.
// In an object of its own, which only the programs that import std.audio link.

#include <stddef.h>

typedef unsigned char u8;
typedef short i16;
typedef int i32;
typedef unsigned int u32;
typedef long long i64;
typedef float f32;
typedef double f64;

// ---- SDL ----

typedef struct {
    u32 format;
    i32 channels;
    i32 freq;
} AudioSpec;

#define AUDIO_S16 0x8010u               // SDL_AUDIO_S16LE
#define INIT_AUDIO 0x10u                // SDL_INIT_AUDIO
#define DEFAULT_PLAYBACK 0xFFFFFFFFu    // SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK

void* SDL_malloc(size_t size);
void* SDL_calloc(size_t count, size_t size);
void* SDL_realloc(void* memory, size_t size);
void SDL_free(void* memory);
i32 SDL_memcmp(const void* a, const void* b, size_t size);
void SDL_qsort(void* base, size_t count, size_t size, i32 (*compare)(const void*, const void*));
double SDL_sin(double x);
double SDL_cos(double x);
double SDL_exp(double x);
double SDL_log(double x);
double SDL_pow(double x, double y);
double SDL_floor(double x);
double SDL_scalbn(double x, i32 n);
_Bool SDL_InitSubSystem(u32 flags);
void SDL_QuitSubSystem(u32 flags);
const char* SDL_GetError(void);
u32 SDL_OpenAudioDevice(u32 device, const AudioSpec* spec);
void SDL_CloseAudioDevice(u32 device);
_Bool SDL_PauseAudioDevice(u32 device);
_Bool SDL_ResumeAudioDevice(u32 device);
_Bool SDL_SetAudioDeviceGain(u32 device, f32 gain);
void* SDL_CreateAudioStream(const AudioSpec* source, const AudioSpec* target);
_Bool SDL_BindAudioStream(u32 device, void* stream);
_Bool SDL_SetAudioStreamGain(void* stream, f32 gain);
_Bool SDL_PutAudioStreamData(void* stream, const void* data, i32 size);
i32 SDL_GetAudioStreamAvailable(void* stream);
_Bool SDL_FlushAudioStream(void* stream);
_Bool SDL_SetAudioStreamGetCallback(void* stream, void (*callback)(void*, void*, i32, i32), void* data);
void SDL_DestroyAudioStream(void* stream);
void* SDL_IOFromConstMem(const void* memory, size_t size);
_Bool SDL_LoadWAV_IO(void* source, _Bool close, AudioSpec* spec, u8** data, u32* size);
_Bool SDL_ConvertAudioSamples(const AudioSpec* source, const u8* data, i32 size, const AudioSpec* target, u8** out, i32* outSize);
void* SDL_CreateMutex(void);
void SDL_LockMutex(void* mutex);
void SDL_UnlockMutex(void* mutex);
void SDL_DestroyMutex(void* mutex);
i32 SDL_AddAtomicInt(i32* value, i32 amount);

void* memcpy(void* target, const void* source, size_t size);  // in the runtime (D19)
void* memset(void* target, i32 value, size_t size);

// ---- stb_vorbis ----

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#define malloc(size) SDL_malloc(size)  // not called: its memory is the block given to it
#define realloc(memory, size) SDL_realloc(memory, size)
#define free(memory) SDL_free(memory)
#undef alloca
#define alloca(size) __builtin_alloca(size)
#undef assert
#define assert(x) ((void)0)
#define memcmp(a, b, size) SDL_memcmp(a, b, size)
#define qsort(base, count, size, compare) SDL_qsort(base, count, size, compare)
#define sin(x) SDL_sin(x)
#define cos(x) SDL_cos(x)
#define exp(x) SDL_exp(x)
#define log(x) SDL_log(x)
#define pow(x, y) SDL_pow(x, y)
#define floor(x) SDL_floor(x)
#define ldexp(x, n) SDL_scalbn(x, n)
#define abs(x) ((x) < 0 ? -(x) : (x))
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"  // the code of others, as it is
#include "stb_vorbis.c"
#pragma clang diagnostic pop
#undef malloc
#undef realloc
#undef free

// ---- The sounds ----

#define DECODED_SECONDS 10  // an Ogg Vorbis up to these seconds is decoded once
#define CHUNK 4096          // the frames that a voice decodes at a time

typedef struct {
    i32 uses;      // the program and the voices that play it
    i32 rate;      // frames a second
    i32 channels;
    i64 frames;
    i16* samples;  // decoded: frames * channels of them; or
    u8* file;      // the file in Ogg Vorbis, decoded while it plays
    i64 fileSize;
} Sound;

// Writes a reason of a failure in `reason`, a string of C of `room` bytes.
static void tell(char* reason, i64 room, const char* text) {
    i64 n = 0;
    while (text[n] && n < room - 1) {
        reason[n] = text[n];
        ++n;
    }
    reason[n] = 0;
}

// A decoder of stb_vorbis on a file in memory, with all its memory in a block: tried
// larger until it is enough. 0 when the file is not Ogg Vorbis.
static stb_vorbis* decoderOf(const u8* file, i64 size, void** block) {
    for (i32 room = 256 * 1024; room <= 16 * 1024 * 1024; room *= 2) {
        stb_vorbis_alloc memory = {SDL_malloc((size_t)room), room};
        if (!memory.alloc_buffer) return 0;
        i32 error = 0;
        stb_vorbis* decoder = stb_vorbis_open_memory(file, (i32)size, &error, &memory);
        if (decoder) {
            *block = memory.alloc_buffer;
            return decoder;
        }
        SDL_free(memory.alloc_buffer);
        if (error != VORBIS_outofmem) return 0;
    }
    return 0;
}

static void soundFree(Sound* sound) {
    if (SDL_AddAtomicInt(&sound->uses, -1) != 1) return;  // the value before
    SDL_free(sound->samples);
    SDL_free(sound->file);
    SDL_free(sound);
}

// A sound from the bytes of a WAV or Ogg Vorbis file; 0 when it cannot be read, with
// the reason in `reason`.
void* cromo_audio_sound(const u8* bytes, i64 size, char* reason, i64 room) {
    tell(reason, room, "");
    if (size < 12 || size > 0x7FFFFFFF) {
        tell(reason, room, size < 12 ? "the file is too short for a sound" : "the file is too large");
        return 0;
    }
    Sound* sound = SDL_calloc(1, sizeof(Sound));
    if (!sound) {
        tell(reason, room, "out of memory");
        return 0;
    }
    sound->uses = 1;
    if (SDL_memcmp(bytes, "OggS", 4) == 0) {
        void* block = 0;
        stb_vorbis* decoder = decoderOf(bytes, size, &block);
        if (!decoder) {
            SDL_free(sound);
            tell(reason, room, "the file is not Ogg Vorbis, or it is damaged");
            return 0;
        }
        stb_vorbis_info info = stb_vorbis_get_info(decoder);
        sound->rate = (i32)info.sample_rate;
        sound->channels = info.channels > 2 ? 2 : info.channels;  // more are mixed down to two
        sound->frames = stb_vorbis_stream_length_in_samples(decoder);
        if (sound->frames <= (i64)DECODED_SECONDS * sound->rate) {
            sound->samples = SDL_malloc((size_t)(sound->frames + 1) * sound->channels * sizeof(i16));
            i64 done = 0;
            while (sound->samples && done < sound->frames) {
                i32 got = stb_vorbis_get_samples_short_interleaved(decoder, sound->channels, sound->samples + done * sound->channels,
                                                                   (i32)((sound->frames - done) * sound->channels));
                if (got <= 0) break;
                done += got;
            }
            sound->frames = done;
        } else {
            sound->file = SDL_malloc((size_t)size);
            if (sound->file) memcpy(sound->file, bytes, (size_t)size);
            sound->fileSize = size;
        }
        SDL_free(block);
        if (!sound->samples && !sound->file) {
            SDL_free(sound);
            tell(reason, room, "out of memory");
            return 0;
        }
        return sound;
    }
    AudioSpec spec;
    u8* data = 0;
    u32 length = 0;
    if (!SDL_LoadWAV_IO(SDL_IOFromConstMem(bytes, (size_t)size), 1, &spec, &data, &length)) {
        SDL_free(sound);
        tell(reason, room, SDL_memcmp(bytes, "RIFF", 4) == 0 ? SDL_GetError() : "the file is neither WAV nor Ogg Vorbis");
        return 0;
    }
    AudioSpec wanted = {AUDIO_S16, spec.channels > 2 ? 2 : spec.channels, spec.freq};
    u8* converted = 0;
    i32 convertedSize = 0;
    _Bool fine = SDL_ConvertAudioSamples(&spec, data, (i32)length, &wanted, &converted, &convertedSize);
    SDL_free(data);
    if (!fine) {
        SDL_free(sound);
        tell(reason, room, SDL_GetError());
        return 0;
    }
    sound->rate = wanted.freq;
    sound->channels = wanted.channels;
    sound->samples = (i16*)converted;
    sound->frames = convertedSize / (i64)(sizeof(i16) * wanted.channels);
    return sound;
}

void cromo_audio_sound_free(void* sound) { soundFree(sound); }

f64 cromo_audio_sound_seconds(void* data) {
    const Sound* sound = data;
    return sound->rate > 0 ? (f64)sound->frames / sound->rate : 0;
}

// ---- The players and their voices ----

typedef struct Voice {
    struct Voice* next;
    i64 id;
    void* stream;
    Sound* sound;
    i64 at;               // the frame of a decoded sound to give next
    stb_vorbis* decoder;  // of a sound decoded while it plays
    void* block;          // its memory
    i32 loop;
    i32 ended;            // all its frames are given to the stream
} Voice;

typedef struct {
    u32 device;
    void* lock;  // of its voices
    Voice* voices;
    i64 next;
} Player;

// SDL asks a voice for more: on the thread of the audio, with the stream locked.
static void feed(void* data, void* stream, i32 additional, i32 total) {
    (void)total;
    Voice* voice = data;
    Sound* sound = voice->sound;
    i32 frame = (i32)sizeof(i16) * sound->channels;
    i16 decoded[CHUNK * 2];
    while (additional > 0 && !voice->ended) {
        i32 given = 0;
        if (voice->decoder) {
            given = stb_vorbis_get_samples_short_interleaved(voice->decoder, sound->channels, decoded, CHUNK * sound->channels);
            if (given > 0) SDL_PutAudioStreamData(stream, decoded, given * frame);
        } else {
            i64 left = sound->frames - voice->at;
            given = (i32)(left < CHUNK ? left : CHUNK);
            if (given > 0) SDL_PutAudioStreamData(stream, sound->samples + voice->at * sound->channels, given * frame);
            voice->at += given;
        }
        additional -= given * frame;
        if (given > 0) continue;
        if (voice->loop && sound->frames > 0) {  // from its start again
            voice->at = 0;
            if (voice->decoder) stb_vorbis_seek_start(voice->decoder);
        } else {
            voice->ended = 1;
            SDL_FlushAudioStream(stream);
        }
    }
}

static void voiceFree(Voice* voice) {
    SDL_DestroyAudioStream(voice->stream);  // waits for its callback
    if (voice->decoder) stb_vorbis_close(voice->decoder);
    SDL_free(voice->block);
    soundFree(voice->sound);
    SDL_free(voice);
}

// Whether SDL has still something of a voice to play.
static i32 sounding(Voice* voice) { return !voice->ended || SDL_GetAudioStreamAvailable(voice->stream) > 0; }

// Frees the voices that ended, or the one of this number, or all with -1. With the
// lock of the player.
static void sweep(Player* player, i64 id) {
    Voice** link = &player->voices;
    while (*link) {
        Voice* voice = *link;
        if (id == -1 || voice->id == id || !sounding(voice)) {
            *link = voice->next;
            voiceFree(voice);
        } else {
            link = &voice->next;
        }
    }
}

static Voice* voiceOf(Player* player, i64 id) {
    for (Voice* voice = player->voices; voice; voice = voice->next) {
        if (voice->id == id) return voice;
    }
    return 0;
}

// A player on the default device that plays; 0 when the system has none.
void* cromo_audio_open(void) {
    if (!SDL_InitSubSystem(INIT_AUDIO)) return 0;
    u32 device = SDL_OpenAudioDevice(DEFAULT_PLAYBACK, 0);
    Player* player = device ? SDL_calloc(1, sizeof(Player)) : 0;
    if (player) player->lock = SDL_CreateMutex();
    if (!player || !player->lock) {
        if (device) SDL_CloseAudioDevice(device);
        SDL_free(player);
        SDL_QuitSubSystem(INIT_AUDIO);
        return 0;
    }
    player->device = device;
    return player;
}

void cromo_audio_close(void* data) {
    Player* player = data;
    SDL_LockMutex(player->lock);
    sweep(player, -1);
    SDL_UnlockMutex(player->lock);
    SDL_CloseAudioDevice(player->device);
    SDL_DestroyMutex(player->lock);
    SDL_free(player);
    SDL_QuitSubSystem(INIT_AUDIO);
}

// Starts a voice that plays a sound, once or again and again, and gives its number;
// 0 when it cannot.
i64 cromo_audio_play(void* data, void* what, f32 volume, i32 loop) {
    Player* player = data;
    Sound* sound = what;
    Voice* voice = SDL_calloc(1, sizeof(Voice));
    if (!voice) return 0;
    AudioSpec spec = {AUDIO_S16, sound->channels, sound->rate};
    voice->stream = SDL_CreateAudioStream(&spec, 0);
    if (sound->file) voice->decoder = decoderOf(sound->file, sound->fileSize, &voice->block);
    if (!voice->stream || (sound->file && !voice->decoder)) {
        if (voice->stream) SDL_DestroyAudioStream(voice->stream);
        SDL_free(voice);
        return 0;
    }
    SDL_AddAtomicInt(&sound->uses, 1);
    voice->sound = sound;
    voice->loop = loop;
    SDL_SetAudioStreamGain(voice->stream, volume < 0 ? 0 : volume);
    SDL_SetAudioStreamGetCallback(voice->stream, feed, voice);
    SDL_LockMutex(player->lock);
    sweep(player, 0);
    voice->id = ++player->next;
    voice->next = player->voices;
    player->voices = voice;
    i64 id = voice->id;
    SDL_BindAudioStream(player->device, voice->stream);
    SDL_UnlockMutex(player->lock);
    return id;
}

// Stops a voice, or all of them with -1.
void cromo_audio_stop(void* data, i64 id) {
    Player* player = data;
    SDL_LockMutex(player->lock);
    if (id != 0) sweep(player, id);
    SDL_UnlockMutex(player->lock);
}

void cromo_audio_voice_volume(void* data, i64 id, f32 volume) {
    Player* player = data;
    SDL_LockMutex(player->lock);
    Voice* voice = voiceOf(player, id);
    if (voice) SDL_SetAudioStreamGain(voice->stream, volume < 0 ? 0 : volume);
    SDL_UnlockMutex(player->lock);
}

i32 cromo_audio_playing(void* data, i64 id) {
    Player* player = data;
    SDL_LockMutex(player->lock);
    Voice* voice = voiceOf(player, id);
    i32 result = voice && sounding(voice);
    SDL_UnlockMutex(player->lock);
    return result;
}

void cromo_audio_volume(void* data, f32 volume) {
    Player* player = data;
    SDL_SetAudioDeviceGain(player->device, volume < 0 ? 0 : volume);
}

void cromo_audio_pause(void* data, i32 paused) {
    Player* player = data;
    if (paused) {
        SDL_PauseAudioDevice(player->device);
    } else {
        SDL_ResumeAudioDevice(player->device);
    }
}
