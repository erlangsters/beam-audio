/*
    Copyright (c) 2026, Byteplug LLC.

    This source file is part of a project made by the Erlangsters community and
    is released under the MIT license. Please refer to the LICENSE.md file that
    can be found at the root of the project repository.

    Written by Jonathan De Wachter <jonathan.dewachter@byteplug.io>
*/

#include <erl_nif.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "miniaudio/miniaudio.h"

#ifdef ERL_NIF_DIRTY_JOB_CPU_BOUND
#define DIRTY_CPU_NIF ERL_NIF_DIRTY_JOB_CPU_BOUND
#else
#define DIRTY_CPU_NIF 0
#endif

#define AUDIO_MAX_PCM_BYTES ((uint64_t)268435456)

typedef enum {
    AUDIO_LIVE = 0,
    AUDIO_UNINITING = 1,
    AUDIO_DEAD = 2
} audio_state_t;

typedef enum {
    PLAYER_CLIP = 0,
    PLAYER_FILE_STREAM = 1,
    PLAYER_QUEUE_STREAM = 2
} player_kind_t;

typedef enum {
    STREAM_FILE = 0,
    STREAM_QUEUE = 1
} stream_kind_t;

typedef struct {
    ma_data_source_base base;
    ma_pcm_rb *rb;
    volatile int *underrun;
} queue_ds_t;

typedef struct {
    ERL_NIF_TERM ok;
    ERL_NIF_TERM error;
    ERL_NIF_TERM badarg;
    ERL_NIF_TERM not_initialized;
    ERL_NIF_TERM device_failed;
    ERL_NIF_TERM no_device;
    ERL_NIF_TERM out_of_memory;
    ERL_NIF_TERM invalid_resource;
    ERL_NIF_TERM unsupported_format;
    ERL_NIF_TERM unsupported_channels;
    ERL_NIF_TERM decode_failed;
    ERL_NIF_TERM encode_failed;
    ERL_NIF_TERM format_mismatch;
    ERL_NIF_TERM full;
    ERL_NIF_TERM too_large;
    ERL_NIF_TERM in_use;
    ERL_NIF_TERM empty;
    ERL_NIF_TERM atom_true;
    ERL_NIF_TERM atom_false;
    ERL_NIF_TERM atom_undefined;
    ERL_NIF_TERM atom_null;
    ERL_NIF_TERM atom_default;
    ERL_NIF_TERM atom_f32;
    ERL_NIF_TERM atom_s16;
    ERL_NIF_TERM atom_playback;
    ERL_NIF_TERM atom_capture;
    ERL_NIF_TERM atom_id;
    ERL_NIF_TERM atom_name;
    ERL_NIF_TERM atom_kind;
    ERL_NIF_TERM atom_wasapi;
    ERL_NIF_TERM atom_coreaudio;
    ERL_NIF_TERM atom_pulseaudio;
    ERL_NIF_TERM atom_alsa;
    ERL_NIF_TERM atom_jack;
    ERL_NIF_TERM atom_dsound;
    ERL_NIF_TERM atom_winmm;
    ERL_NIF_TERM atom_aaudio;
    ERL_NIF_TERM atom_opensl;
    ERL_NIF_TERM atom_unknown;
    ErlNifResourceType *clip_type;
    ErlNifResourceType *player_type;
    ErlNifResourceType *stream_type;
    ErlNifResourceType *recorder_type;
    ErlNifMutex *lock;
    int started;
    ma_context context;
    int context_inited;
    ma_engine engine;
    int engine_inited;
    ma_backend backend;
    uint32_t sample_rate;
    uint32_t channels;
    float master_volume;
} priv_t;

typedef struct {
    audio_state_t state;
    ErlNifMutex *lock;
    unsigned player_keep_count;
    int owner_dead;
    int buffer_inited;
    float *pcm;
    uint32_t channels;
    uint32_t sample_rate;
    uint64_t frame_count;
    ma_format source_format;
} clip_t;

typedef struct stream_s stream_t;

typedef struct {
    audio_state_t state;
    ErlNifMutex *lock;
    player_kind_t kind;
    void *source_resource;
    int ref_inited;
    int sound_inited;
    ma_audio_buffer_ref buffer_ref;
    ma_sound sound;
    ma_sound *sound_ptr;
    uint32_t sample_rate;
    uint64_t frame_count;
    float volume;
    float pan;
    float pitch;
    int looping;
} player_t;

struct stream_s {
    audio_state_t state;
    ErlNifMutex *lock;
    unsigned player_keep_count;
    int owner_dead;
    stream_kind_t kind;
    int sound_inited;
    int ring_inited;
    int qds_inited;
    ma_sound sound;
    ma_pcm_rb ring;
    queue_ds_t qds;
    ma_format format;
    uint32_t channels;
    uint32_t sample_rate;
    uint32_t capacity_frames;
    volatile int underrun;
};

typedef struct {
    audio_state_t state;
    ErlNifMutex *lock;
    int device_inited;
    int ring_inited;
    ma_device device;
    ma_pcm_rb ring;
    ma_format format;
    uint32_t channels;
    uint32_t sample_rate;
    uint32_t buffer_frames;
    int running;
    volatile int overrun;
} recorder_t;

static priv_t *g_priv = NULL;

static int get_float_arg(ErlNifEnv *env, ERL_NIF_TERM term, double *out);

static ERL_NIF_TERM make_error(ErlNifEnv *env, priv_t *priv, ERL_NIF_TERM reason)
{
    return enif_make_tuple2(env, priv->error, reason);
}

static ERL_NIF_TERM make_ok_term(ErlNifEnv *env, priv_t *priv, ERL_NIF_TERM term)
{
    return enif_make_tuple2(env, priv->ok, term);
}

static int atom_eq(ErlNifEnv *env, ERL_NIF_TERM term, ERL_NIF_TERM atom)
{
    return enif_is_identical(term, atom);
}

static int is_bad_double(double x)
{
    return !isfinite(x);
}

static priv_t *get_priv(ErlNifEnv *env)
{
    return (priv_t *)enif_priv_data(env);
}

static ERL_NIF_TERM backend_atom(priv_t *priv, ma_backend backend)
{
    switch (backend) {
    case ma_backend_null:
        return priv->atom_null;
    case ma_backend_wasapi:
        return priv->atom_wasapi;
    case ma_backend_coreaudio:
        return priv->atom_coreaudio;
    case ma_backend_pulseaudio:
        return priv->atom_pulseaudio;
    case ma_backend_alsa:
        return priv->atom_alsa;
    case ma_backend_jack:
        return priv->atom_jack;
    case ma_backend_dsound:
        return priv->atom_dsound;
    case ma_backend_winmm:
        return priv->atom_winmm;
    case ma_backend_aaudio:
        return priv->atom_aaudio;
    case ma_backend_opensl:
        return priv->atom_opensl;
    default:
        return priv->atom_unknown;
    }
}

static ma_format parse_format(priv_t *priv, ErlNifEnv *env, ERL_NIF_TERM term, int *ok)
{
    *ok = 1;
    if (atom_eq(env, term, priv->atom_f32)) {
        return ma_format_f32;
    }
    if (atom_eq(env, term, priv->atom_s16)) {
        return ma_format_s16;
    }
    *ok = 0;
    return ma_format_unknown;
}

static ERL_NIF_TERM format_atom(priv_t *priv, ma_format format)
{
    if (format == ma_format_s16) {
        return priv->atom_s16;
    }
    return priv->atom_f32;
}

static uint32_t bytes_per_sample(ma_format format)
{
    return (format == ma_format_s16) ? 2u : 4u;
}

static void convert_to_f32(const void *src, ma_format src_format, uint32_t channels,
    uint64_t frames, float *dst)
{
    uint64_t n = frames * channels;
    uint64_t i;

    if (src_format == ma_format_f32) {
        memcpy(dst, src, (size_t)(n * 4));
        return;
    }
    {
        const int16_t *in = (const int16_t *)src;
        for (i = 0; i < n; i++) {
            dst[i] = ((float)in[i]) / 32768.0f;
        }
    }
}

static void convert_from_f32(const float *src, ma_format dst_format, uint32_t channels,
    uint64_t frames, void *dst)
{
    uint64_t n = frames * channels;
    uint64_t i;

    if (dst_format == ma_format_f32) {
        memcpy(dst, src, (size_t)(n * 4));
        return;
    }
    {
        int16_t *out = (int16_t *)dst;
        for (i = 0; i < n; i++) {
            float s = src[i];
            if (s > 1.0f) {
                s = 1.0f;
            } else if (s < -1.0f) {
                s = -1.0f;
            }
            out[i] = (int16_t)(s * 32767.0f);
        }
    }
}

static int begin_live(ErlNifMutex *lock, audio_state_t *state)
{
    enif_mutex_lock(lock);
    if (*state != AUDIO_LIVE) {
        enif_mutex_unlock(lock);
        return 0;
    }
    return 1;
}

static int begin_player(player_t *player)
{
    if (!begin_live(player->lock, &player->state)) {
        return 0;
    }
    if (player->sound_ptr == NULL) {
        enif_mutex_unlock(player->lock);
        return 0;
    }
    return 1;
}

static int begin_uninit(ErlNifMutex *lock, audio_state_t *state)
{
    enif_mutex_lock(lock);
    if (*state != AUDIO_LIVE) {
        enif_mutex_unlock(lock);
        return 0;
    }
    *state = AUDIO_UNINITING;
    enif_mutex_unlock(lock);
    return 1;
}

static void mark_dead(ErlNifMutex *lock, audio_state_t *state)
{
    enif_mutex_lock(lock);
    *state = AUDIO_DEAD;
    enif_mutex_unlock(lock);
}

static void destroy_mutex(ErlNifMutex **lock)
{
    if (*lock != NULL) {
        enif_mutex_destroy(*lock);
        *lock = NULL;
    }
}

static void clip_native_uninit(clip_t *clip)
{
    if (clip->pcm != NULL) {
        free(clip->pcm);
        clip->pcm = NULL;
    }
    clip->buffer_inited = 0;
}

static void maybe_uninit_clip(clip_t *clip)
{
    int do_uninit = 0;

    enif_mutex_lock(clip->lock);
    if (clip->player_keep_count == 0 && clip->owner_dead && clip->state == AUDIO_LIVE) {
        clip->state = AUDIO_UNINITING;
        do_uninit = 1;
    }
    enif_mutex_unlock(clip->lock);
    if (do_uninit) {
        clip_native_uninit(clip);
        mark_dead(clip->lock, &clip->state);
    }
}

static void stream_native_uninit(stream_t *stream)
{
    if (stream->sound_inited) {
        ma_sound_uninit(&stream->sound);
        stream->sound_inited = 0;
    }
    if (stream->qds_inited) {
        ma_data_source_uninit(&stream->qds.base);
        stream->qds_inited = 0;
    }
    if (stream->ring_inited) {
        ma_pcm_rb_uninit(&stream->ring);
        stream->ring_inited = 0;
    }
}

static void maybe_uninit_stream(stream_t *stream)
{
    int do_uninit = 0;

    enif_mutex_lock(stream->lock);
    if (stream->player_keep_count == 0 && stream->owner_dead && stream->state == AUDIO_LIVE) {
        stream->state = AUDIO_UNINITING;
        do_uninit = 1;
    }
    enif_mutex_unlock(stream->lock);
    if (do_uninit) {
        stream_native_uninit(stream);
        mark_dead(stream->lock, &stream->state);
    }
}

static void player_drop_source(player_t *player)
{
    void *source = player->source_resource;
    player_kind_t kind = player->kind;

    player->source_resource = NULL;
    if (source == NULL) {
        return;
    }
    if (kind == PLAYER_CLIP) {
        clip_t *clip = (clip_t *)source;
        enif_mutex_lock(clip->lock);
        if (clip->player_keep_count > 0) {
            clip->player_keep_count--;
        }
        enif_mutex_unlock(clip->lock);
        maybe_uninit_clip(clip);
        enif_release_resource(source);
    } else {
        stream_t *stream = (stream_t *)source;
        enif_mutex_lock(stream->lock);
        if (stream->player_keep_count > 0) {
            stream->player_keep_count--;
        }
        enif_mutex_unlock(stream->lock);
        maybe_uninit_stream(stream);
        enif_release_resource(source);
    }
}

static void player_native_uninit(player_t *player)
{
    if (player->kind == PLAYER_FILE_STREAM) {
        player->sound_inited = 0;
        player->sound_ptr = NULL;
    } else {
        if (player->sound_inited) {
            ma_sound_uninit(&player->sound);
            player->sound_inited = 0;
        }
        player->sound_ptr = NULL;
        if (player->ref_inited) {
            ma_audio_buffer_ref_uninit(&player->buffer_ref);
            player->ref_inited = 0;
        }
    }
    player_drop_source(player);
}

static void recorder_native_uninit(recorder_t *rec)
{
    if (rec->device_inited) {
        if (rec->running) {
            ma_device_stop(&rec->device);
            rec->running = 0;
        }
        ma_device_uninit(&rec->device);
        rec->device_inited = 0;
    }
    if (rec->ring_inited) {
        ma_pcm_rb_uninit(&rec->ring);
        rec->ring_inited = 0;
    }
}

static void clip_dtor(ErlNifEnv *env, void *obj)
{
    clip_t *clip = (clip_t *)obj;
    (void)env;
    destroy_mutex(&clip->lock);
}

static void player_dtor(ErlNifEnv *env, void *obj)
{
    player_t *player = (player_t *)obj;
    (void)env;
    destroy_mutex(&player->lock);
}

static void stream_dtor(ErlNifEnv *env, void *obj)
{
    stream_t *stream = (stream_t *)obj;
    (void)env;
    destroy_mutex(&stream->lock);
}

static void recorder_dtor(ErlNifEnv *env, void *obj)
{
    recorder_t *rec = (recorder_t *)obj;
    (void)env;
    destroy_mutex(&rec->lock);
}

static void engine_teardown_locked(priv_t *priv)
{
    if (priv->engine_inited) {
        ma_engine_uninit(&priv->engine);
        priv->engine_inited = 0;
    }
    if (priv->context_inited) {
        ma_context_uninit(&priv->context);
        priv->context_inited = 0;
    }
    priv->started = 0;
}

static void engine_teardown(priv_t *priv)
{
    enif_mutex_lock(priv->lock);
    engine_teardown_locked(priv);
    enif_mutex_unlock(priv->lock);
}

static ma_result queue_on_read(ma_data_source *pDataSource, void *pFramesOut, ma_uint64 frameCount, ma_uint64 *pFramesRead)
{
    queue_ds_t *ds = (queue_ds_t *)pDataSource;
    ma_uint32 bpf = ma_get_bytes_per_frame(ds->rb->format, ds->rb->channels);
    ma_uint64 total = 0;

    while (total < frameCount) {
        ma_uint32 want = (ma_uint32)(frameCount - total);
        void *src = NULL;
        ma_result r;

        if (want == 0) {
            break;
        }
        r = ma_pcm_rb_acquire_read(ds->rb, &want, &src);
        if (r != MA_SUCCESS || want == 0) {
            break;
        }
        memcpy((unsigned char *)pFramesOut + total * bpf, src, (size_t)want * bpf);
        ma_pcm_rb_commit_read(ds->rb, want);
        total += want;
    }
    if (total < frameCount) {
        size_t silence_bytes = (size_t)((frameCount - total) * bpf);
        memset((unsigned char *)pFramesOut + total * bpf, 0, silence_bytes);
        if (ds->underrun != NULL) {
            *ds->underrun = 1;
        }
        total = frameCount;
    }
    if (pFramesRead != NULL) {
        *pFramesRead = total;
    }
    return MA_SUCCESS;
}

static ma_result queue_on_seek(ma_data_source *pDataSource, ma_uint64 frameIndex)
{
    (void)pDataSource;
    (void)frameIndex;
    return MA_NOT_IMPLEMENTED;
}

static ma_result queue_on_get_data_format(ma_data_source *pDataSource, ma_format *pFormat, ma_uint32 *pChannels, ma_uint32 *pSampleRate, ma_channel *pChannelMap, size_t channelMapCap)
{
    queue_ds_t *ds = (queue_ds_t *)pDataSource;

    if (pFormat != NULL) {
        *pFormat = ds->rb->format;
    }
    if (pChannels != NULL) {
        *pChannels = ds->rb->channels;
    }
    if (pSampleRate != NULL) {
        *pSampleRate = ds->rb->sampleRate;
    }
    if (pChannelMap != NULL) {
        ma_channel_map_init_standard(ma_standard_channel_map_default, pChannelMap, channelMapCap, ds->rb->channels);
    }
    return MA_SUCCESS;
}

static ma_result queue_on_get_cursor(ma_data_source *pDataSource, ma_uint64 *pCursor)
{
    (void)pDataSource;
    if (pCursor != NULL) {
        *pCursor = 0;
    }
    return MA_NOT_IMPLEMENTED;
}

static ma_result queue_on_get_length(ma_data_source *pDataSource, ma_uint64 *pLength)
{
    (void)pDataSource;
    if (pLength != NULL) {
        *pLength = 0;
    }
    return MA_NOT_IMPLEMENTED;
}

static ma_data_source_vtable g_queue_vtable = {
    queue_on_read,
    queue_on_seek,
    queue_on_get_data_format,
    queue_on_get_cursor,
    queue_on_get_length,
    NULL,
    0
};

static void recorder_callback(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount)
{
    recorder_t *rec = (recorder_t *)pDevice->pUserData;
    ma_uint32 bpf;
    ma_uint32 incoming = frameCount;
    const unsigned char *src = (const unsigned char *)pInput;
    (void)pOutput;

    if (rec == NULL || pInput == NULL || incoming == 0 || !rec->ring_inited) {
        return;
    }
    bpf = ma_get_bytes_per_frame(rec->format, rec->channels);
    if (incoming > rec->buffer_frames) {
        src += (incoming - rec->buffer_frames) * bpf;
        incoming = rec->buffer_frames;
        rec->overrun = 1;
    }
    {
        ma_uint32 avail_w = ma_pcm_rb_available_write(&rec->ring);
        if (avail_w < incoming) {
            ma_uint32 drop = incoming - avail_w;
            ma_uint32 avail_r = ma_pcm_rb_available_read(&rec->ring);
            if (drop > avail_r) {
                drop = avail_r;
            }
            while (drop > 0) {
                void *dummy = NULL;
                ma_uint32 n = drop;
                if (ma_pcm_rb_acquire_read(&rec->ring, &n, &dummy) != MA_SUCCESS || n == 0) {
                    break;
                }
                ma_pcm_rb_commit_read(&rec->ring, n);
                drop -= n;
            }
            rec->overrun = 1;
        }
    }
    {
        ma_uint32 remaining = incoming;
        while (remaining > 0) {
            void *dst = NULL;
            ma_uint32 n = remaining;
            if (ma_pcm_rb_acquire_write(&rec->ring, &n, &dst) != MA_SUCCESS || n == 0) {
                rec->overrun = 1;
                break;
            }
            memcpy(dst, src, (size_t)n * bpf);
            ma_pcm_rb_commit_write(&rec->ring, n);
            src += n * bpf;
            remaining -= n;
        }
    }
}

typedef struct {
    unsigned char *data;
    size_t size;
    size_t cap;
    size_t cursor;
} mem_buf;

static ma_result enc_write(ma_encoder *pEncoder, const void *pBufferIn, size_t bytesToWrite, size_t *pBytesWritten)
{
    mem_buf *b = (mem_buf *)pEncoder->pUserData;

    if (b->cursor + bytesToWrite > b->cap) {
        size_t ncap = b->cap ? b->cap * 2 : 4096;
        void *p;
        while (ncap < b->cursor + bytesToWrite) {
            ncap *= 2;
        }
        p = realloc(b->data, ncap);
        if (p == NULL) {
            return MA_OUT_OF_MEMORY;
        }
        b->data = p;
        b->cap = ncap;
    }
    memcpy(b->data + b->cursor, pBufferIn, bytesToWrite);
    b->cursor += bytesToWrite;
    if (b->cursor > b->size) {
        b->size = b->cursor;
    }
    if (pBytesWritten != NULL) {
        *pBytesWritten = bytesToWrite;
    }
    return MA_SUCCESS;
}

static ma_result enc_seek(ma_encoder *pEncoder, ma_int64 offset, ma_seek_origin origin)
{
    mem_buf *b = (mem_buf *)pEncoder->pUserData;
    ma_int64 pos = (ma_int64)b->cursor;

    if (origin == ma_seek_origin_start) {
        pos = offset;
    } else if (origin == ma_seek_origin_current) {
        pos += offset;
    } else {
        pos = (ma_int64)b->size + offset;
    }
    if (pos < 0) {
        return MA_BAD_SEEK;
    }
    b->cursor = (size_t)pos;
    return MA_SUCCESS;
}

static ERL_NIF_TERM nif_engine_start(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    int use_null = 0;
    int sample_rate = 48000;
    int channels = 2;
    int period_frames = 0;
    int has_device = 0;
    ma_device_id device_id;
    ma_context_config ctx_config;
    ma_engine_config engine_config;
    ma_result result;
    (void)argc;

    memset(&device_id, 0, sizeof(device_id));
    if (atom_eq(env, argv[0], priv->atom_null)) {
        use_null = 1;
    } else if (!atom_eq(env, argv[0], priv->atom_default)) {
        return make_error(env, priv, priv->badarg);
    }
    if (!enif_get_int(env, argv[1], &sample_rate) || sample_rate < 1) {
        return make_error(env, priv, priv->badarg);
    }
    if (!enif_get_int(env, argv[2], &channels) || (channels != 1 && channels != 2)) {
        return make_error(env, priv, priv->unsupported_channels);
    }
    if (!enif_get_int(env, argv[3], &period_frames) || period_frames < 0) {
        return make_error(env, priv, priv->badarg);
    }
    if (atom_eq(env, argv[4], priv->atom_default)) {
        has_device = 0;
    } else if (enif_is_binary(env, argv[4])) {
        ErlNifBinary bin;
        if (!enif_inspect_binary(env, argv[4], &bin) || bin.size != sizeof(ma_device_id)) {
            return make_error(env, priv, priv->badarg);
        }
        memcpy(&device_id, bin.data, sizeof(ma_device_id));
        has_device = 1;
    } else {
        return make_error(env, priv, priv->badarg);
    }

    enif_mutex_lock(priv->lock);
    if (priv->started) {
        /* Worker kill skips terminate/2; drop the leftover engine. */
        engine_teardown_locked(priv);
    }

    ctx_config = ma_context_config_init();
    if (use_null) {
        ma_backend backends[1] = { ma_backend_null };
        result = ma_context_init(backends, 1, &ctx_config, &priv->context);
    } else {
        result = ma_context_init(NULL, 0, &ctx_config, &priv->context);
    }
    if (result != MA_SUCCESS) {
        enif_mutex_unlock(priv->lock);
        if (result == MA_NO_DEVICE) {
            return make_error(env, priv, priv->no_device);
        }
        return make_error(env, priv, priv->device_failed);
    }
    priv->context_inited = 1;
    priv->backend = priv->context.backend;

    engine_config = ma_engine_config_init();
    engine_config.pContext = &priv->context;
    engine_config.sampleRate = (ma_uint32)sample_rate;
    engine_config.channels = (ma_uint32)channels;
    if (period_frames > 0) {
        engine_config.periodSizeInFrames = (ma_uint32)period_frames;
    }
    if (has_device) {
        engine_config.pPlaybackDeviceID = &device_id;
    }
    result = ma_engine_init(&engine_config, &priv->engine);
    if (result != MA_SUCCESS) {
        ma_context_uninit(&priv->context);
        priv->context_inited = 0;
        enif_mutex_unlock(priv->lock);
        if (result == MA_NO_DEVICE) {
            return make_error(env, priv, priv->no_device);
        }
        return make_error(env, priv, priv->device_failed);
    }
    priv->engine_inited = 1;
    priv->started = 1;
    priv->sample_rate = ma_engine_get_sample_rate(&priv->engine);
    priv->channels = ma_engine_get_channels(&priv->engine);
    priv->master_volume = 1.0f;
    ma_engine_set_volume(&priv->engine, 1.0f);
    enif_mutex_unlock(priv->lock);
    return priv->ok;
}

static ERL_NIF_TERM nif_engine_stop(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    (void)argc;
    (void)argv;
    engine_teardown(priv);
    return priv->ok;
}

static ERL_NIF_TERM nif_engine_info(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    ERL_NIF_TERM keys[4];
    ERL_NIF_TERM vals[4];
    (void)argc;
    (void)argv;

    enif_mutex_lock(priv->lock);
    if (!priv->started) {
        enif_mutex_unlock(priv->lock);
        return make_error(env, priv, priv->not_initialized);
    }
    keys[0] = enif_make_atom(env, "backend");
    vals[0] = backend_atom(priv, priv->backend);
    keys[1] = enif_make_atom(env, "sample_rate");
    vals[1] = enif_make_uint(env, priv->sample_rate);
    keys[2] = enif_make_atom(env, "channels");
    vals[2] = enif_make_uint(env, priv->channels);
    keys[3] = enif_make_atom(env, "master_volume");
    vals[3] = enif_make_double(env, (double)priv->master_volume);
    enif_mutex_unlock(priv->lock);
    return make_ok_term(env, priv, enif_make_tuple4(env, vals[0], vals[1], vals[2], vals[3]));
}

static ERL_NIF_TERM nif_set_master_volume(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    double volume;
    (void)argc;

    if (!get_float_arg(env, argv[0], &volume)) {
        return make_error(env, priv, priv->badarg);
    }
    if (is_bad_double(volume) || volume < 0.0) {
        return make_error(env, priv, priv->badarg);
    }
    enif_mutex_lock(priv->lock);
    if (!priv->started) {
        enif_mutex_unlock(priv->lock);
        return make_error(env, priv, priv->not_initialized);
    }
    priv->master_volume = (float)volume;
    ma_engine_set_volume(&priv->engine, (float)volume);
    enif_mutex_unlock(priv->lock);
    return priv->ok;
}

static ERL_NIF_TERM nif_device_list(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    ma_device_info *playback = NULL;
    ma_device_info *capture = NULL;
    ma_uint32 playback_count = 0;
    ma_uint32 capture_count = 0;
    ma_result result;
    int want_playback;
    ma_device_info *infos;
    ma_uint32 count;
    ma_uint32 i;
    ERL_NIF_TERM list;
    (void)argc;

    want_playback = atom_eq(env, argv[0], priv->atom_playback);
    if (!want_playback && !atom_eq(env, argv[0], priv->atom_capture)) {
        return make_error(env, priv, priv->badarg);
    }
    enif_mutex_lock(priv->lock);
    if (!priv->started || !priv->context_inited) {
        enif_mutex_unlock(priv->lock);
        return make_error(env, priv, priv->not_initialized);
    }
    result = ma_context_get_devices(&priv->context, &playback, &playback_count, &capture, &capture_count);
    if (result != MA_SUCCESS) {
        enif_mutex_unlock(priv->lock);
        return make_error(env, priv, priv->device_failed);
    }
    infos = want_playback ? playback : capture;
    count = want_playback ? playback_count : capture_count;
    list = enif_make_list(env, 0);
    for (i = count; i-- > 0; ) {
        ERL_NIF_TERM map = enif_make_new_map(env);
        ERL_NIF_TERM id_bin;
        unsigned char *id_data;
        ERL_NIF_TERM name_bin;
        size_t name_len = strlen(infos[i].name);
        unsigned char *name_data;

        id_data = enif_make_new_binary(env, sizeof(ma_device_id), &id_bin);
        memcpy(id_data, &infos[i].id, sizeof(ma_device_id));
        name_data = enif_make_new_binary(env, name_len, &name_bin);
        memcpy(name_data, infos[i].name, name_len);
        enif_make_map_put(env, map, priv->atom_id, id_bin, &map);
        enif_make_map_put(env, map, priv->atom_name, name_bin, &map);
        enif_make_map_put(env, map, priv->atom_kind, want_playback ? priv->atom_playback : priv->atom_capture, &map);
        enif_make_map_put(env, map, priv->atom_default, infos[i].isDefault ? priv->atom_true : priv->atom_false, &map);
        list = enif_make_list_cell(env, map, list);
    }
    enif_mutex_unlock(priv->lock);
    return make_ok_term(env, priv, list);
}

static ERL_NIF_TERM nif_clip_with_samples(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    int format_ok = 0;
    ma_format format;
    int sample_rate;
    int channels;
    ErlNifUInt64 frame_count;
    ErlNifBinary data;
    uint64_t engine_bytes;
    clip_t *clip;
    (void)argc;

    format = parse_format(priv, env, argv[0], &format_ok);
    if (!format_ok) {
        return make_error(env, priv, priv->badarg);
    }
    if (!enif_get_int(env, argv[1], &sample_rate) || sample_rate < 1) {
        return make_error(env, priv, priv->badarg);
    }
    if (!enif_get_int(env, argv[2], &channels) || (channels != 1 && channels != 2)) {
        return make_error(env, priv, priv->unsupported_channels);
    }
    if (!enif_get_uint64(env, argv[3], &frame_count)) {
        return make_error(env, priv, priv->badarg);
    }
    if (!enif_inspect_binary(env, argv[4], &data)) {
        return make_error(env, priv, priv->badarg);
    }
    if (frame_count == 0) {
        return make_error(env, priv, priv->badarg);
    }
    engine_bytes = (uint64_t)frame_count * (uint64_t)channels * 4u;
    if (engine_bytes > AUDIO_MAX_PCM_BYTES) {
        return make_error(env, priv, priv->too_large);
    }
    if ((uint64_t)data.size != (uint64_t)frame_count * (uint64_t)channels * bytes_per_sample(format)) {
        return make_error(env, priv, priv->badarg);
    }
    enif_mutex_lock(priv->lock);
    if (!priv->started) {
        enif_mutex_unlock(priv->lock);
        return make_error(env, priv, priv->not_initialized);
    }
    enif_mutex_unlock(priv->lock);

    clip = enif_alloc_resource(priv->clip_type, sizeof(clip_t));
    if (clip == NULL) {
        return make_error(env, priv, priv->out_of_memory);
    }
    memset(clip, 0, sizeof(*clip));
    clip->lock = enif_mutex_create("audio_clip");
    if (clip->lock == NULL) {
        enif_release_resource(clip);
        return make_error(env, priv, priv->out_of_memory);
    }
    clip->pcm = (float *)malloc((size_t)engine_bytes);
    if (clip->pcm == NULL) {
        destroy_mutex(&clip->lock);
        enif_release_resource(clip);
        return make_error(env, priv, priv->out_of_memory);
    }
    convert_to_f32(data.data, format, (uint32_t)channels, (uint64_t)frame_count, clip->pcm);
    clip->state = AUDIO_LIVE;
    clip->player_keep_count = 0;
    clip->owner_dead = 0;
    clip->buffer_inited = 1;
    clip->channels = (uint32_t)channels;
    clip->sample_rate = (uint32_t)sample_rate;
    clip->frame_count = (uint64_t)frame_count;
    clip->source_format = format;
    {
        ERL_NIF_TERM term = enif_make_resource(env, clip);
        enif_release_resource(clip);
        return make_ok_term(env, priv, term);
    }
}

static ERL_NIF_TERM nif_clip_destroy(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    clip_t *clip;
    unsigned keep;
    (void)argc;

    if (!enif_get_resource(env, argv[0], priv->clip_type, (void **)&clip)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    enif_mutex_lock(clip->lock);
    if (clip->state != AUDIO_LIVE) {
        enif_mutex_unlock(clip->lock);
        return make_error(env, priv, priv->invalid_resource);
    }
    keep = clip->player_keep_count;
    if (keep > 0) {
        clip->owner_dead = 1;
        enif_mutex_unlock(clip->lock);
        return make_error(env, priv, priv->in_use);
    }
    clip->state = AUDIO_UNINITING;
    enif_mutex_unlock(clip->lock);
    clip_native_uninit(clip);
    mark_dead(clip->lock, &clip->state);
    return priv->ok;
}

static ERL_NIF_TERM nif_clip_info(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    clip_t *clip;
    (void)argc;

    if (!enif_get_resource(env, argv[0], priv->clip_type, (void **)&clip)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    if (!begin_live(clip->lock, &clip->state)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    {
        ERL_NIF_TERM tuple = enif_make_tuple4(env,
            format_atom(priv, clip->source_format),
            enif_make_uint(env, clip->sample_rate),
            enif_make_uint(env, clip->channels),
            enif_make_uint64(env, clip->frame_count));
        enif_mutex_unlock(clip->lock);
        return make_ok_term(env, priv, tuple);
    }
}

static ERL_NIF_TERM nif_clip_force_uninit(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    clip_t *clip;
    (void)argc;

    if (!enif_get_resource(env, argv[0], priv->clip_type, (void **)&clip)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    if (!begin_uninit(clip->lock, &clip->state)) {
        return priv->ok;
    }
    clip_native_uninit(clip);
    mark_dead(clip->lock, &clip->state);
    return priv->ok;
}

static ma_uint32 sound_flags(void)
{
    return MA_SOUND_FLAG_NO_SPATIALIZATION;
}

static ERL_NIF_TERM nif_player_with_clip(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    clip_t *clip;
    player_t *player;
    ma_result result;
    (void)argc;

    if (!enif_get_resource(env, argv[0], priv->clip_type, (void **)&clip)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    enif_mutex_lock(priv->lock);
    if (!priv->started) {
        enif_mutex_unlock(priv->lock);
        return make_error(env, priv, priv->not_initialized);
    }
    enif_mutex_unlock(priv->lock);
    if (!begin_live(clip->lock, &clip->state)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    enif_mutex_unlock(clip->lock);

    player = enif_alloc_resource(priv->player_type, sizeof(player_t));
    if (player == NULL) {
        return make_error(env, priv, priv->out_of_memory);
    }
    memset(player, 0, sizeof(*player));
    player->lock = enif_mutex_create("audio_player");
    if (player->lock == NULL) {
        enif_release_resource(player);
        return make_error(env, priv, priv->out_of_memory);
    }
    result = ma_audio_buffer_ref_init(ma_format_f32, clip->channels, clip->pcm, clip->frame_count, &player->buffer_ref);
    if (result != MA_SUCCESS) {
        destroy_mutex(&player->lock);
        enif_release_resource(player);
        return make_error(env, priv, priv->out_of_memory);
    }
    player->buffer_ref.sampleRate = clip->sample_rate;
    player->ref_inited = 1;
    result = ma_sound_init_from_data_source(&priv->engine, &player->buffer_ref, sound_flags(), NULL, &player->sound);
    if (result != MA_SUCCESS) {
        ma_audio_buffer_ref_uninit(&player->buffer_ref);
        destroy_mutex(&player->lock);
        enif_release_resource(player);
        return make_error(env, priv, priv->device_failed);
    }
    player->sound_inited = 1;
    player->sound_ptr = &player->sound;
    player->kind = PLAYER_CLIP;
    player->state = AUDIO_LIVE;
    player->volume = 1.0f;
    player->pan = 0.0f;
    player->pitch = 1.0f;
    player->looping = 0;
    player->sample_rate = clip->sample_rate;
    player->frame_count = clip->frame_count;
    enif_keep_resource(clip);
    player->source_resource = clip;
    enif_mutex_lock(clip->lock);
    clip->player_keep_count++;
    enif_mutex_unlock(clip->lock);
    {
        ERL_NIF_TERM term = enif_make_resource(env, player);
        enif_release_resource(player);
        return make_ok_term(env, priv, term);
    }
}

static ERL_NIF_TERM nif_player_with_stream(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    stream_t *stream;
    player_t *player;
    ma_result result;
    (void)argc;

    if (!enif_get_resource(env, argv[0], priv->stream_type, (void **)&stream)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    enif_mutex_lock(priv->lock);
    if (!priv->started) {
        enif_mutex_unlock(priv->lock);
        return make_error(env, priv, priv->not_initialized);
    }
    enif_mutex_unlock(priv->lock);
    if (!begin_live(stream->lock, &stream->state)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    if (stream->player_keep_count > 0) {
        enif_mutex_unlock(stream->lock);
        return make_error(env, priv, priv->in_use);
    }
    enif_mutex_unlock(stream->lock);

    player = enif_alloc_resource(priv->player_type, sizeof(player_t));
    if (player == NULL) {
        return make_error(env, priv, priv->out_of_memory);
    }
    memset(player, 0, sizeof(*player));
    player->lock = enif_mutex_create("audio_player");
    if (player->lock == NULL) {
        enif_release_resource(player);
        return make_error(env, priv, priv->out_of_memory);
    }
    if (stream->kind == STREAM_FILE) {
        player->kind = PLAYER_FILE_STREAM;
        player->sound_ptr = &stream->sound;
        player->sound_inited = 0;
    } else {
        player->kind = PLAYER_QUEUE_STREAM;
        result = ma_sound_init_from_data_source(&priv->engine, &stream->qds, sound_flags(), NULL, &player->sound);
        if (result != MA_SUCCESS) {
            destroy_mutex(&player->lock);
            enif_release_resource(player);
            return make_error(env, priv, priv->device_failed);
        }
        player->sound_inited = 1;
        player->sound_ptr = &player->sound;
    }
    player->state = AUDIO_LIVE;
    player->volume = 1.0f;
    player->pan = 0.0f;
    player->pitch = 1.0f;
    player->looping = 0;
    player->sample_rate = stream->sample_rate;
    player->frame_count = 0;
    enif_keep_resource(stream);
    player->source_resource = stream;
    enif_mutex_lock(stream->lock);
    stream->player_keep_count++;
    enif_mutex_unlock(stream->lock);
    {
        ERL_NIF_TERM term = enif_make_resource(env, player);
        enif_release_resource(player);
        return make_ok_term(env, priv, term);
    }
}

static player_t *get_player(ErlNifEnv *env, priv_t *priv, ERL_NIF_TERM term)
{
    player_t *player;
    if (!enif_get_resource(env, term, priv->player_type, (void **)&player)) {
        return NULL;
    }
    return player;
}

static ERL_NIF_TERM nif_player_destroy(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    player_t *player;
    (void)argc;

    player = get_player(env, priv, argv[0]);
    if (player == NULL) {
        return make_error(env, priv, priv->invalid_resource);
    }
    if (!begin_uninit(player->lock, &player->state)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    player_native_uninit(player);
    mark_dead(player->lock, &player->state);
    return priv->ok;
}

static ERL_NIF_TERM nif_player_play(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    player_t *player;
    (void)argc;

    player = get_player(env, priv, argv[0]);
    if (player == NULL) {
        return make_error(env, priv, priv->invalid_resource);
    }
    if (!begin_player(player)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    ma_sound_seek_to_pcm_frame(player->sound_ptr, 0);
    ma_sound_start(player->sound_ptr);
    enif_mutex_unlock(player->lock);
    return priv->ok;
}

static ERL_NIF_TERM nif_player_stop(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    player_t *player;
    (void)argc;

    player = get_player(env, priv, argv[0]);
    if (player == NULL) {
        return make_error(env, priv, priv->invalid_resource);
    }
    if (!begin_player(player)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    ma_sound_stop(player->sound_ptr);
    ma_sound_seek_to_pcm_frame(player->sound_ptr, 0);
    enif_mutex_unlock(player->lock);
    return priv->ok;
}

static ERL_NIF_TERM nif_player_pause(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    player_t *player;
    (void)argc;

    player = get_player(env, priv, argv[0]);
    if (player == NULL) {
        return make_error(env, priv, priv->invalid_resource);
    }
    if (!begin_player(player)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    ma_sound_stop(player->sound_ptr);
    enif_mutex_unlock(player->lock);
    return priv->ok;
}

static ERL_NIF_TERM nif_player_resume(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    player_t *player;
    (void)argc;

    player = get_player(env, priv, argv[0]);
    if (player == NULL) {
        return make_error(env, priv, priv->invalid_resource);
    }
    if (!begin_player(player)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    ma_sound_start(player->sound_ptr);
    enif_mutex_unlock(player->lock);
    return priv->ok;
}

static ERL_NIF_TERM nif_player_playing(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    player_t *player;
    int playing;
    (void)argc;

    player = get_player(env, priv, argv[0]);
    if (player == NULL) {
        return make_error(env, priv, priv->invalid_resource);
    }
    if (!begin_player(player)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    playing = ma_sound_is_playing(player->sound_ptr) ? 1 : 0;
    enif_mutex_unlock(player->lock);
    return playing ? priv->atom_true : priv->atom_false;
}

static int get_float_arg(ErlNifEnv *env, ERL_NIF_TERM term, double *out)
{
    int iv;
    if (enif_get_double(env, term, out)) {
        return 1;
    }
    if (enif_get_int(env, term, &iv)) {
        *out = (double)iv;
        return 1;
    }
    return 0;
}

static ERL_NIF_TERM nif_player_set_volume(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    player_t *player;
    double volume;
    (void)argc;

    player = get_player(env, priv, argv[0]);
    if (player == NULL) {
        return make_error(env, priv, priv->invalid_resource);
    }
    if (!get_float_arg(env, argv[1], &volume) || is_bad_double(volume) || volume < 0.0) {
        return make_error(env, priv, priv->badarg);
    }
    if (!begin_player(player)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    player->volume = (float)volume;
    ma_sound_set_volume(player->sound_ptr, (float)volume);
    enif_mutex_unlock(player->lock);
    return priv->ok;
}

static ERL_NIF_TERM nif_player_volume(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    player_t *player;
    float volume;
    (void)argc;

    player = get_player(env, priv, argv[0]);
    if (player == NULL) {
        return make_error(env, priv, priv->invalid_resource);
    }
    if (!begin_live(player->lock, &player->state)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    volume = player->volume;
    enif_mutex_unlock(player->lock);
    return enif_make_double(env, (double)volume);
}

static ERL_NIF_TERM nif_player_set_pan(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    player_t *player;
    double pan;
    (void)argc;

    player = get_player(env, priv, argv[0]);
    if (player == NULL) {
        return make_error(env, priv, priv->invalid_resource);
    }
    if (!get_float_arg(env, argv[1], &pan) || is_bad_double(pan) || pan < -1.0 || pan > 1.0) {
        return make_error(env, priv, priv->badarg);
    }
    if (!begin_player(player)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    player->pan = (float)pan;
    ma_sound_set_pan(player->sound_ptr, (float)pan);
    enif_mutex_unlock(player->lock);
    return priv->ok;
}

static ERL_NIF_TERM nif_player_pan(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    player_t *player;
    float pan;
    (void)argc;

    player = get_player(env, priv, argv[0]);
    if (player == NULL) {
        return make_error(env, priv, priv->invalid_resource);
    }
    if (!begin_live(player->lock, &player->state)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    pan = player->pan;
    enif_mutex_unlock(player->lock);
    return enif_make_double(env, (double)pan);
}

static ERL_NIF_TERM nif_player_set_pitch(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    player_t *player;
    double pitch;
    (void)argc;

    player = get_player(env, priv, argv[0]);
    if (player == NULL) {
        return make_error(env, priv, priv->invalid_resource);
    }
    if (!get_float_arg(env, argv[1], &pitch) || is_bad_double(pitch) || pitch <= 0.0) {
        return make_error(env, priv, priv->badarg);
    }
    if (!begin_player(player)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    player->pitch = (float)pitch;
    ma_sound_set_pitch(player->sound_ptr, (float)pitch);
    enif_mutex_unlock(player->lock);
    return priv->ok;
}

static ERL_NIF_TERM nif_player_pitch(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    player_t *player;
    float pitch;
    (void)argc;

    player = get_player(env, priv, argv[0]);
    if (player == NULL) {
        return make_error(env, priv, priv->invalid_resource);
    }
    if (!begin_live(player->lock, &player->state)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    pitch = player->pitch;
    enif_mutex_unlock(player->lock);
    return enif_make_double(env, (double)pitch);
}

static ERL_NIF_TERM nif_player_set_loop(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    player_t *player;
    int looping;
    (void)argc;

    player = get_player(env, priv, argv[0]);
    if (player == NULL) {
        return make_error(env, priv, priv->invalid_resource);
    }
    if (atom_eq(env, argv[1], priv->atom_true)) {
        looping = 1;
    } else if (atom_eq(env, argv[1], priv->atom_false)) {
        looping = 0;
    } else {
        return make_error(env, priv, priv->badarg);
    }
    if (!begin_player(player)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    player->looping = looping;
    ma_sound_set_looping(player->sound_ptr, looping ? MA_TRUE : MA_FALSE);
    enif_mutex_unlock(player->lock);
    return priv->ok;
}

static ERL_NIF_TERM nif_player_looping(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    player_t *player;
    int looping;
    (void)argc;

    player = get_player(env, priv, argv[0]);
    if (player == NULL) {
        return make_error(env, priv, priv->invalid_resource);
    }
    if (!begin_live(player->lock, &player->state)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    looping = player->looping;
    enif_mutex_unlock(player->lock);
    return looping ? priv->atom_true : priv->atom_false;
}

static ERL_NIF_TERM nif_player_seek(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    player_t *player;
    double seconds;
    ma_uint64 frame;
    (void)argc;

    player = get_player(env, priv, argv[0]);
    if (player == NULL) {
        return make_error(env, priv, priv->invalid_resource);
    }
    if (!get_float_arg(env, argv[1], &seconds) || is_bad_double(seconds) || seconds < 0.0) {
        return make_error(env, priv, priv->badarg);
    }
    if (!begin_player(player)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    frame = (ma_uint64)(seconds * (double)player->sample_rate);
    if (player->frame_count > 0 && frame > player->frame_count) {
        frame = player->frame_count;
    }
    ma_sound_seek_to_pcm_frame(player->sound_ptr, frame);
    enif_mutex_unlock(player->lock);
    return priv->ok;
}

static ERL_NIF_TERM nif_player_cursor(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    player_t *player;
    float cursor = 0.0f;
    (void)argc;

    player = get_player(env, priv, argv[0]);
    if (player == NULL) {
        return make_error(env, priv, priv->invalid_resource);
    }
    if (!begin_player(player)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    ma_sound_get_cursor_in_seconds(player->sound_ptr, &cursor);
    enif_mutex_unlock(player->lock);
    return enif_make_double(env, (double)cursor);
}

static int wav_is_supported_payload(const unsigned char *data, size_t size)
{
    size_t offset;
    if (size < 12) {
        return 0;
    }
    if (memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WAVE", 4) != 0) {
        return 0;
    }
    offset = 12;
    while (offset + 8 <= size) {
        unsigned int chunk_size;
        if (memcmp(data + offset, "fmt ", 4) == 0) {
            uint16_t audio_format = 0;
            uint16_t bits = 0;
            if (offset + 24 > size) {
                return 0;
            }
            memcpy(&chunk_size, data + offset + 4, 4);
            memcpy(&audio_format, data + offset + 8, 2);
            memcpy(&bits, data + offset + 22, 2);
            if (audio_format == 1 && bits == 16) {
                return 1;
            }
            if (audio_format == 3 && bits == 32) {
                return 1;
            }
            return 0;
        }
        memcpy(&chunk_size, data + offset + 4, 4);
        offset += 8 + chunk_size;
        if (chunk_size & 1) {
            offset++;
        }
    }
    return 0;
}

static ERL_NIF_TERM nif_wav_decode(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    ErlNifBinary bin;
    ma_decoder decoder;
    ma_decoder_config config;
    ma_result result;
    ma_uint64 frames;
    ma_format format;
    ma_uint32 channels;
    ma_uint32 sample_rate;
    uint64_t engine_bytes;
    void *pcm;
    ERL_NIF_TERM data_term;
    unsigned char *out;
    size_t out_size;
    (void)argc;

    if (!enif_inspect_binary(env, argv[0], &bin)) {
        return make_error(env, priv, priv->badarg);
    }
    if (bin.size < 12 || memcmp(bin.data, "RIFF", 4) != 0 || memcmp(bin.data + 8, "WAVE", 4) != 0) {
        return make_error(env, priv, priv->unsupported_format);
    }
    if (!wav_is_supported_payload(bin.data, bin.size)) {
        return make_error(env, priv, priv->unsupported_format);
    }
    config = ma_decoder_config_init(ma_format_unknown, 0, 0);
    result = ma_decoder_init_memory(bin.data, bin.size, &config, &decoder);
    if (result != MA_SUCCESS) {
        return make_error(env, priv, priv->decode_failed);
    }
    format = decoder.outputFormat;
    channels = decoder.outputChannels;
    sample_rate = decoder.outputSampleRate;
    if ((format != ma_format_f32 && format != ma_format_s16) || (channels != 1 && channels != 2)) {
        ma_decoder_uninit(&decoder);
        return make_error(env, priv, priv->unsupported_format);
    }
    result = ma_decoder_get_length_in_pcm_frames(&decoder, &frames);
    if (result != MA_SUCCESS || frames == 0) {
        ma_decoder_uninit(&decoder);
        return make_error(env, priv, priv->decode_failed);
    }
    engine_bytes = (uint64_t)frames * (uint64_t)channels * 4u;
    if (engine_bytes > AUDIO_MAX_PCM_BYTES) {
        ma_decoder_uninit(&decoder);
        return make_error(env, priv, priv->too_large);
    }
    out_size = (size_t)frames * channels * bytes_per_sample(format);
    pcm = malloc(out_size);
    if (pcm == NULL) {
        ma_decoder_uninit(&decoder);
        return make_error(env, priv, priv->out_of_memory);
    }
    {
        ma_uint64 read = 0;
        result = ma_decoder_read_pcm_frames(&decoder, pcm, frames, &read);
        ma_decoder_uninit(&decoder);
        if (result != MA_SUCCESS && result != MA_AT_END) {
            free(pcm);
            return make_error(env, priv, priv->decode_failed);
        }
        frames = read;
        out_size = (size_t)frames * channels * bytes_per_sample(format);
    }
    out = enif_make_new_binary(env, out_size, &data_term);
    memcpy(out, pcm, out_size);
    free(pcm);
    return make_ok_term(env, priv, enif_make_tuple5(env,
        format_atom(priv, format),
        enif_make_uint(env, sample_rate),
        enif_make_uint(env, channels),
        enif_make_uint64(env, frames),
        data_term));
}

static ERL_NIF_TERM nif_wav_encode(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    int format_ok = 0;
    ma_format format;
    int sample_rate;
    int channels;
    ErlNifUInt64 frame_count;
    ErlNifBinary data;
    uint64_t engine_bytes;
    ma_encoder encoder;
    ma_encoder_config config;
    mem_buf buf;
    ma_result result;
    ERL_NIF_TERM out_term;
    unsigned char *out;
    (void)argc;

    format = parse_format(priv, env, argv[0], &format_ok);
    if (!format_ok) {
        return make_error(env, priv, priv->badarg);
    }
    if (!enif_get_int(env, argv[1], &sample_rate) || sample_rate < 1) {
        return make_error(env, priv, priv->badarg);
    }
    if (!enif_get_int(env, argv[2], &channels) || (channels != 1 && channels != 2)) {
        return make_error(env, priv, priv->unsupported_channels);
    }
    if (!enif_get_uint64(env, argv[3], &frame_count) || frame_count == 0) {
        return make_error(env, priv, priv->badarg);
    }
    if (!enif_inspect_binary(env, argv[4], &data)) {
        return make_error(env, priv, priv->badarg);
    }
    engine_bytes = (uint64_t)frame_count * (uint64_t)channels * 4u;
    if (engine_bytes > AUDIO_MAX_PCM_BYTES) {
        return make_error(env, priv, priv->too_large);
    }
    if ((uint64_t)data.size != (uint64_t)frame_count * (uint64_t)channels * bytes_per_sample(format)) {
        return make_error(env, priv, priv->badarg);
    }
    memset(&buf, 0, sizeof(buf));
    config = ma_encoder_config_init(ma_encoding_format_wav, format, (ma_uint32)channels, (ma_uint32)sample_rate);
    result = ma_encoder_init(enc_write, enc_seek, &buf, &config, &encoder);
    if (result != MA_SUCCESS) {
        return make_error(env, priv, priv->encode_failed);
    }
    result = ma_encoder_write_pcm_frames(&encoder, data.data, (ma_uint64)frame_count, NULL);
    ma_encoder_uninit(&encoder);
    if (result != MA_SUCCESS) {
        free(buf.data);
        return make_error(env, priv, priv->encode_failed);
    }
    out = enif_make_new_binary(env, buf.size, &out_term);
    if (buf.size > 0) {
        memcpy(out, buf.data, buf.size);
    }
    free(buf.data);
    return make_ok_term(env, priv, out_term);
}

static ERL_NIF_TERM nif_stream_from_file(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    char path[4096];
    stream_t *stream;
    ma_result result;
    (void)argc;

    if (enif_get_string(env, argv[0], path, sizeof(path), ERL_NIF_LATIN1) <= 0) {
        ErlNifBinary bin;
        if (!enif_inspect_binary(env, argv[0], &bin) || bin.size >= sizeof(path)) {
            return make_error(env, priv, priv->badarg);
        }
        memcpy(path, bin.data, bin.size);
        path[bin.size] = 0;
    }
    enif_mutex_lock(priv->lock);
    if (!priv->started) {
        enif_mutex_unlock(priv->lock);
        return make_error(env, priv, priv->not_initialized);
    }
    enif_mutex_unlock(priv->lock);

    stream = enif_alloc_resource(priv->stream_type, sizeof(stream_t));
    if (stream == NULL) {
        return make_error(env, priv, priv->out_of_memory);
    }
    memset(stream, 0, sizeof(*stream));
    stream->lock = enif_mutex_create("audio_stream");
    if (stream->lock == NULL) {
        enif_release_resource(stream);
        return make_error(env, priv, priv->out_of_memory);
    }
    result = ma_sound_init_from_file(&priv->engine, path,
        MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION, NULL, NULL, &stream->sound);
    if (result != MA_SUCCESS) {
        destroy_mutex(&stream->lock);
        enif_release_resource(stream);
        return make_error(env, priv, priv->decode_failed);
    }
    stream->sound_inited = 1;
    stream->kind = STREAM_FILE;
    stream->state = AUDIO_LIVE;
    stream->format = ma_format_f32;
    stream->channels = priv->channels;
    stream->sample_rate = priv->sample_rate;
    {
        ERL_NIF_TERM term = enif_make_resource(env, stream);
        enif_release_resource(stream);
        return make_ok_term(env, priv, term);
    }
}

static ERL_NIF_TERM nif_stream_with_queue(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    int format_ok = 0;
    ma_format format;
    int sample_rate;
    int channels;
    int capacity;
    stream_t *stream;
    ma_result result;
    ma_data_source_config ds_config;
    (void)argc;

    format = parse_format(priv, env, argv[0], &format_ok);
    if (!format_ok) {
        return make_error(env, priv, priv->badarg);
    }
    if (!enif_get_int(env, argv[1], &sample_rate) || sample_rate < 1) {
        return make_error(env, priv, priv->badarg);
    }
    if (!enif_get_int(env, argv[2], &channels) || (channels != 1 && channels != 2)) {
        return make_error(env, priv, priv->unsupported_channels);
    }
    if (!enif_get_int(env, argv[3], &capacity) || capacity < 1) {
        return make_error(env, priv, priv->badarg);
    }
    enif_mutex_lock(priv->lock);
    if (!priv->started) {
        enif_mutex_unlock(priv->lock);
        return make_error(env, priv, priv->not_initialized);
    }
    enif_mutex_unlock(priv->lock);

    stream = enif_alloc_resource(priv->stream_type, sizeof(stream_t));
    if (stream == NULL) {
        return make_error(env, priv, priv->out_of_memory);
    }
    memset(stream, 0, sizeof(*stream));
    stream->lock = enif_mutex_create("audio_stream");
    if (stream->lock == NULL) {
        enif_release_resource(stream);
        return make_error(env, priv, priv->out_of_memory);
    }
    result = ma_pcm_rb_init(format, (ma_uint32)channels, (ma_uint32)capacity, NULL, NULL, &stream->ring);
    if (result != MA_SUCCESS) {
        destroy_mutex(&stream->lock);
        enif_release_resource(stream);
        return make_error(env, priv, priv->out_of_memory);
    }
    stream->ring.sampleRate = (ma_uint32)sample_rate;
    stream->ring_inited = 1;
    stream->qds.rb = &stream->ring;
    stream->qds.underrun = &stream->underrun;
    ds_config = ma_data_source_config_init();
    ds_config.vtable = &g_queue_vtable;
    result = ma_data_source_init(&ds_config, &stream->qds.base);
    if (result != MA_SUCCESS) {
        ma_pcm_rb_uninit(&stream->ring);
        destroy_mutex(&stream->lock);
        enif_release_resource(stream);
        return make_error(env, priv, priv->out_of_memory);
    }
    stream->qds_inited = 1;
    stream->kind = STREAM_QUEUE;
    stream->state = AUDIO_LIVE;
    stream->format = format;
    stream->channels = (uint32_t)channels;
    stream->sample_rate = (uint32_t)sample_rate;
    stream->capacity_frames = (uint32_t)capacity;
    {
        ERL_NIF_TERM term = enif_make_resource(env, stream);
        enif_release_resource(stream);
        return make_ok_term(env, priv, term);
    }
}

static ERL_NIF_TERM nif_stream_destroy(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    stream_t *stream;
    unsigned keep;
    (void)argc;

    if (!enif_get_resource(env, argv[0], priv->stream_type, (void **)&stream)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    enif_mutex_lock(stream->lock);
    if (stream->state != AUDIO_LIVE) {
        enif_mutex_unlock(stream->lock);
        return make_error(env, priv, priv->invalid_resource);
    }
    keep = stream->player_keep_count;
    if (keep > 0) {
        stream->owner_dead = 1;
        enif_mutex_unlock(stream->lock);
        return make_error(env, priv, priv->in_use);
    }
    stream->state = AUDIO_UNINITING;
    enif_mutex_unlock(stream->lock);
    stream_native_uninit(stream);
    mark_dead(stream->lock, &stream->state);
    return priv->ok;
}

static ERL_NIF_TERM nif_stream_force_uninit(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    stream_t *stream;
    (void)argc;

    if (!enif_get_resource(env, argv[0], priv->stream_type, (void **)&stream)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    if (!begin_uninit(stream->lock, &stream->state)) {
        return priv->ok;
    }
    stream_native_uninit(stream);
    mark_dead(stream->lock, &stream->state);
    return priv->ok;
}

static ERL_NIF_TERM nif_stream_write(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    stream_t *stream;
    int format_ok = 0;
    ma_format format;
    int sample_rate;
    int channels;
    ErlNifUInt64 frame_count;
    ErlNifBinary data;
    ma_uint32 bpf;
    ma_uint32 remaining;
    const unsigned char *src;
    (void)argc;

    if (!enif_get_resource(env, argv[0], priv->stream_type, (void **)&stream)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    format = parse_format(priv, env, argv[1], &format_ok);
    if (!format_ok || !enif_get_int(env, argv[2], &sample_rate) ||
        !enif_get_int(env, argv[3], &channels) || !enif_get_uint64(env, argv[4], &frame_count) ||
        !enif_inspect_binary(env, argv[5], &data)) {
        return make_error(env, priv, priv->badarg);
    }
    if (!begin_live(stream->lock, &stream->state)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    if (stream->kind != STREAM_QUEUE) {
        enif_mutex_unlock(stream->lock);
        return make_error(env, priv, priv->badarg);
    }
    if (format != stream->format || (uint32_t)sample_rate != stream->sample_rate ||
        (uint32_t)channels != stream->channels) {
        enif_mutex_unlock(stream->lock);
        return make_error(env, priv, priv->format_mismatch);
    }
    if (frame_count == 0) {
        enif_mutex_unlock(stream->lock);
        return priv->ok;
    }
    if (ma_pcm_rb_available_write(&stream->ring) < (ma_uint32)frame_count) {
        enif_mutex_unlock(stream->lock);
        return make_error(env, priv, priv->full);
    }
    bpf = ma_get_bytes_per_frame(stream->format, stream->channels);
    remaining = (ma_uint32)frame_count;
    src = data.data;
    while (remaining > 0) {
        void *dst = NULL;
        ma_uint32 n = remaining;
        if (ma_pcm_rb_acquire_write(&stream->ring, &n, &dst) != MA_SUCCESS || n == 0) {
            enif_mutex_unlock(stream->lock);
            return make_error(env, priv, priv->full);
        }
        memcpy(dst, src, (size_t)n * bpf);
        ma_pcm_rb_commit_write(&stream->ring, n);
        src += n * bpf;
        remaining -= n;
    }
    enif_mutex_unlock(stream->lock);
    return priv->ok;
}

static ERL_NIF_TERM nif_stream_available(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    stream_t *stream;
    unsigned avail;
    (void)argc;

    if (!enif_get_resource(env, argv[0], priv->stream_type, (void **)&stream)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    if (!begin_live(stream->lock, &stream->state)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    if (stream->kind != STREAM_QUEUE) {
        enif_mutex_unlock(stream->lock);
        return make_error(env, priv, priv->badarg);
    }
    avail = ma_pcm_rb_available_write(&stream->ring);
    enif_mutex_unlock(stream->lock);
    return enif_make_uint(env, avail);
}

static ERL_NIF_TERM nif_stream_queued(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    stream_t *stream;
    unsigned queued;
    (void)argc;

    if (!enif_get_resource(env, argv[0], priv->stream_type, (void **)&stream)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    if (!begin_live(stream->lock, &stream->state)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    if (stream->kind != STREAM_QUEUE) {
        enif_mutex_unlock(stream->lock);
        return make_error(env, priv, priv->badarg);
    }
    queued = ma_pcm_rb_available_read(&stream->ring);
    enif_mutex_unlock(stream->lock);
    return enif_make_uint(env, queued);
}

static ERL_NIF_TERM nif_stream_underrun(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    stream_t *stream;
    int flag;
    (void)argc;

    if (!enif_get_resource(env, argv[0], priv->stream_type, (void **)&stream)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    if (!begin_live(stream->lock, &stream->state)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    flag = stream->underrun;
    enif_mutex_unlock(stream->lock);
    return flag ? priv->atom_true : priv->atom_false;
}

static ERL_NIF_TERM nif_stream_duration(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    stream_t *stream;
    float length = 0.0f;
    (void)argc;

    if (!enif_get_resource(env, argv[0], priv->stream_type, (void **)&stream)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    if (!begin_live(stream->lock, &stream->state)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    if (stream->kind != STREAM_FILE || !stream->sound_inited) {
        enif_mutex_unlock(stream->lock);
        return priv->atom_undefined;
    }
    if (ma_sound_get_length_in_seconds(&stream->sound, &length) != MA_SUCCESS) {
        enif_mutex_unlock(stream->lock);
        return priv->atom_undefined;
    }
    enif_mutex_unlock(stream->lock);
    return enif_make_double(env, (double)length);
}

static ERL_NIF_TERM nif_recorder_open(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    int format_ok = 0;
    ma_format format;
    int sample_rate;
    int channels;
    int buffer_frames;
    int has_device = 0;
    ma_device_id device_id;
    recorder_t *rec;
    ma_device_config config;
    ma_result result;
    (void)argc;

    memset(&device_id, 0, sizeof(device_id));
    format = parse_format(priv, env, argv[0], &format_ok);
    if (!format_ok) {
        return make_error(env, priv, priv->badarg);
    }
    if (!enif_get_int(env, argv[1], &sample_rate) || sample_rate < 1) {
        return make_error(env, priv, priv->badarg);
    }
    if (!enif_get_int(env, argv[2], &channels) || (channels != 1 && channels != 2)) {
        return make_error(env, priv, priv->unsupported_channels);
    }
    if (!enif_get_int(env, argv[3], &buffer_frames) || buffer_frames < 1) {
        return make_error(env, priv, priv->badarg);
    }
    if (atom_eq(env, argv[4], priv->atom_default)) {
        has_device = 0;
    } else if (enif_is_binary(env, argv[4])) {
        ErlNifBinary bin;
        if (!enif_inspect_binary(env, argv[4], &bin) || bin.size != sizeof(ma_device_id)) {
            return make_error(env, priv, priv->badarg);
        }
        memcpy(&device_id, bin.data, sizeof(ma_device_id));
        has_device = 1;
    } else {
        return make_error(env, priv, priv->badarg);
    }
    enif_mutex_lock(priv->lock);
    if (!priv->started || !priv->context_inited) {
        enif_mutex_unlock(priv->lock);
        return make_error(env, priv, priv->not_initialized);
    }
    enif_mutex_unlock(priv->lock);

    rec = enif_alloc_resource(priv->recorder_type, sizeof(recorder_t));
    if (rec == NULL) {
        return make_error(env, priv, priv->out_of_memory);
    }
    memset(rec, 0, sizeof(*rec));
    rec->lock = enif_mutex_create("audio_recorder");
    if (rec->lock == NULL) {
        enif_release_resource(rec);
        return make_error(env, priv, priv->out_of_memory);
    }
    result = ma_pcm_rb_init(format, (ma_uint32)channels, (ma_uint32)buffer_frames, NULL, NULL, &rec->ring);
    if (result != MA_SUCCESS) {
        destroy_mutex(&rec->lock);
        enif_release_resource(rec);
        return make_error(env, priv, priv->out_of_memory);
    }
    rec->ring.sampleRate = (ma_uint32)sample_rate;
    rec->ring_inited = 1;
    rec->format = format;
    rec->channels = (uint32_t)channels;
    rec->sample_rate = (uint32_t)sample_rate;
    rec->buffer_frames = (uint32_t)buffer_frames;
    rec->state = AUDIO_LIVE;
    config = ma_device_config_init(ma_device_type_capture);
    config.capture.format = format;
    config.capture.channels = (ma_uint32)channels;
    config.sampleRate = (ma_uint32)sample_rate;
    config.dataCallback = recorder_callback;
    config.pUserData = rec;
    if (has_device) {
        config.capture.pDeviceID = &device_id;
    }
    result = ma_device_init(&priv->context, &config, &rec->device);
    if (result != MA_SUCCESS) {
        ma_pcm_rb_uninit(&rec->ring);
        destroy_mutex(&rec->lock);
        enif_release_resource(rec);
        if (result == MA_NO_DEVICE) {
            return make_error(env, priv, priv->no_device);
        }
        return make_error(env, priv, priv->device_failed);
    }
    rec->device_inited = 1;
    {
        ERL_NIF_TERM term = enif_make_resource(env, rec);
        enif_release_resource(rec);
        return make_ok_term(env, priv, term);
    }
}

static ERL_NIF_TERM nif_recorder_destroy(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    recorder_t *rec;
    (void)argc;

    if (!enif_get_resource(env, argv[0], priv->recorder_type, (void **)&rec)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    if (!begin_uninit(rec->lock, &rec->state)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    recorder_native_uninit(rec);
    mark_dead(rec->lock, &rec->state);
    return priv->ok;
}

static ERL_NIF_TERM nif_recorder_start(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    recorder_t *rec;
    ma_result result;
    (void)argc;

    if (!enif_get_resource(env, argv[0], priv->recorder_type, (void **)&rec)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    if (!begin_live(rec->lock, &rec->state)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    result = ma_device_start(&rec->device);
    if (result != MA_SUCCESS) {
        enif_mutex_unlock(rec->lock);
        return make_error(env, priv, priv->device_failed);
    }
    rec->running = 1;
    enif_mutex_unlock(rec->lock);
    return priv->ok;
}

static ERL_NIF_TERM nif_recorder_stop(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    recorder_t *rec;
    (void)argc;

    if (!enif_get_resource(env, argv[0], priv->recorder_type, (void **)&rec)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    if (!begin_live(rec->lock, &rec->state)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    ma_device_stop(&rec->device);
    rec->running = 0;
    enif_mutex_unlock(rec->lock);
    return priv->ok;
}

static ERL_NIF_TERM nif_recorder_running(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    recorder_t *rec;
    int running;
    (void)argc;

    if (!enif_get_resource(env, argv[0], priv->recorder_type, (void **)&rec)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    if (!begin_live(rec->lock, &rec->state)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    running = rec->running;
    enif_mutex_unlock(rec->lock);
    return running ? priv->atom_true : priv->atom_false;
}

static ERL_NIF_TERM nif_recorder_read(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    recorder_t *rec;
    int max_frames;
    ma_uint32 avail;
    ma_uint32 to_read;
    ma_uint32 bpf;
    ERL_NIF_TERM data_term;
    unsigned char *out;
    unsigned char *dst;
    int had_overrun;
    (void)argc;

    if (!enif_get_resource(env, argv[0], priv->recorder_type, (void **)&rec)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    if (!enif_get_int(env, argv[1], &max_frames) || max_frames < 0) {
        return make_error(env, priv, priv->badarg);
    }
    if (!begin_live(rec->lock, &rec->state)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    avail = ma_pcm_rb_available_read(&rec->ring);
    if (avail == 0) {
        had_overrun = rec->overrun;
        if (had_overrun) {
            rec->overrun = 0;
        }
        enif_mutex_unlock(rec->lock);
        return priv->empty;
    }
    to_read = avail;
    if (max_frames > 0 && (ma_uint32)max_frames < to_read) {
        to_read = (ma_uint32)max_frames;
    }
    bpf = ma_get_bytes_per_frame(rec->format, rec->channels);
    out = enif_make_new_binary(env, (size_t)to_read * bpf, &data_term);
    dst = out;
    {
        ma_uint32 remaining = to_read;
        while (remaining > 0) {
            void *src = NULL;
            ma_uint32 n = remaining;
            if (ma_pcm_rb_acquire_read(&rec->ring, &n, &src) != MA_SUCCESS || n == 0) {
                break;
            }
            memcpy(dst, src, (size_t)n * bpf);
            ma_pcm_rb_commit_read(&rec->ring, n);
            dst += n * bpf;
            remaining -= n;
        }
        to_read -= remaining;
    }
    had_overrun = rec->overrun;
    rec->overrun = 0;
    enif_mutex_unlock(rec->lock);
    (void)had_overrun;
    if (to_read == 0) {
        return priv->empty;
    }
    return make_ok_term(env, priv, enif_make_tuple5(env,
        format_atom(priv, rec->format),
        enif_make_uint(env, rec->sample_rate),
        enif_make_uint(env, rec->channels),
        enif_make_uint(env, to_read),
        data_term));
}

static ERL_NIF_TERM nif_recorder_overrun(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    recorder_t *rec;
    int flag;
    (void)argc;

    if (!enif_get_resource(env, argv[0], priv->recorder_type, (void **)&rec)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    if (!begin_live(rec->lock, &rec->state)) {
        return make_error(env, priv, priv->invalid_resource);
    }
    flag = rec->overrun;
    enif_mutex_unlock(rec->lock);
    return flag ? priv->atom_true : priv->atom_false;
}

static ERL_NIF_TERM nif_owner_down(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    priv_t *priv = get_priv(env);
    char kind[32];
    (void)argc;

    if (!enif_get_atom(env, argv[0], kind, sizeof(kind), ERL_NIF_LATIN1)) {
        return make_error(env, priv, priv->badarg);
    }
    if (strcmp(kind, "clip") == 0) {
        clip_t *clip;
        if (!enif_get_resource(env, argv[1], priv->clip_type, (void **)&clip)) {
            return priv->ok;
        }
        enif_mutex_lock(clip->lock);
        if (clip->state != AUDIO_LIVE) {
            enif_mutex_unlock(clip->lock);
            return priv->ok;
        }
        clip->owner_dead = 1;
        if (clip->player_keep_count > 0) {
            enif_mutex_unlock(clip->lock);
            return priv->ok;
        }
        clip->state = AUDIO_UNINITING;
        enif_mutex_unlock(clip->lock);
        clip_native_uninit(clip);
        mark_dead(clip->lock, &clip->state);
        return priv->ok;
    }
    if (strcmp(kind, "player") == 0) {
        player_t *player = get_player(env, priv, argv[1]);
        if (player == NULL) {
            return priv->ok;
        }
        if (!begin_uninit(player->lock, &player->state)) {
            return priv->ok;
        }
        player_native_uninit(player);
        mark_dead(player->lock, &player->state);
        return priv->ok;
    }
    if (strcmp(kind, "stream") == 0) {
        stream_t *stream;
        if (!enif_get_resource(env, argv[1], priv->stream_type, (void **)&stream)) {
            return priv->ok;
        }
        enif_mutex_lock(stream->lock);
        if (stream->state != AUDIO_LIVE) {
            enif_mutex_unlock(stream->lock);
            return priv->ok;
        }
        stream->owner_dead = 1;
        if (stream->player_keep_count > 0) {
            enif_mutex_unlock(stream->lock);
            return priv->ok;
        }
        stream->state = AUDIO_UNINITING;
        enif_mutex_unlock(stream->lock);
        stream_native_uninit(stream);
        mark_dead(stream->lock, &stream->state);
        return priv->ok;
    }
    if (strcmp(kind, "recorder") == 0) {
        recorder_t *rec;
        if (!enif_get_resource(env, argv[1], priv->recorder_type, (void **)&rec)) {
            return priv->ok;
        }
        if (!begin_uninit(rec->lock, &rec->state)) {
            return priv->ok;
        }
        recorder_native_uninit(rec);
        mark_dead(rec->lock, &rec->state);
        return priv->ok;
    }
    return make_error(env, priv, priv->badarg);
}

static ERL_NIF_TERM nif_force_teardown(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    return nif_owner_down(env, argc, argv);
}

static int load(ErlNifEnv *env, void **priv_data, ERL_NIF_TERM load_info)
{
    priv_t *priv;
    ErlNifResourceFlags flags = (ErlNifResourceFlags)(ERL_NIF_RT_CREATE | ERL_NIF_RT_TAKEOVER);
    (void)load_info;

    priv = enif_alloc(sizeof(priv_t));
    if (priv == NULL) {
        return 1;
    }
    memset(priv, 0, sizeof(*priv));
    priv->ok = enif_make_atom(env, "ok");
    priv->error = enif_make_atom(env, "error");
    priv->badarg = enif_make_atom(env, "badarg");
    priv->not_initialized = enif_make_atom(env, "not_initialized");
    priv->device_failed = enif_make_atom(env, "device_failed");
    priv->no_device = enif_make_atom(env, "no_device");
    priv->out_of_memory = enif_make_atom(env, "out_of_memory");
    priv->invalid_resource = enif_make_atom(env, "invalid_resource");
    priv->unsupported_format = enif_make_atom(env, "unsupported_format");
    priv->unsupported_channels = enif_make_atom(env, "unsupported_channels");
    priv->decode_failed = enif_make_atom(env, "decode_failed");
    priv->encode_failed = enif_make_atom(env, "encode_failed");
    priv->format_mismatch = enif_make_atom(env, "format_mismatch");
    priv->full = enif_make_atom(env, "full");
    priv->too_large = enif_make_atom(env, "too_large");
    priv->in_use = enif_make_atom(env, "in_use");
    priv->empty = enif_make_atom(env, "empty");
    priv->atom_true = enif_make_atom(env, "true");
    priv->atom_false = enif_make_atom(env, "false");
    priv->atom_undefined = enif_make_atom(env, "undefined");
    priv->atom_null = enif_make_atom(env, "null");
    priv->atom_default = enif_make_atom(env, "default");
    priv->atom_f32 = enif_make_atom(env, "f32");
    priv->atom_s16 = enif_make_atom(env, "s16");
    priv->atom_playback = enif_make_atom(env, "playback");
    priv->atom_capture = enif_make_atom(env, "capture");
    priv->atom_id = enif_make_atom(env, "id");
    priv->atom_name = enif_make_atom(env, "name");
    priv->atom_kind = enif_make_atom(env, "kind");
    priv->atom_wasapi = enif_make_atom(env, "wasapi");
    priv->atom_coreaudio = enif_make_atom(env, "coreaudio");
    priv->atom_pulseaudio = enif_make_atom(env, "pulseaudio");
    priv->atom_alsa = enif_make_atom(env, "alsa");
    priv->atom_jack = enif_make_atom(env, "jack");
    priv->atom_dsound = enif_make_atom(env, "dsound");
    priv->atom_winmm = enif_make_atom(env, "winmm");
    priv->atom_aaudio = enif_make_atom(env, "aaudio");
    priv->atom_opensl = enif_make_atom(env, "opensl");
    priv->atom_unknown = enif_make_atom(env, "unknown");
    priv->clip_type = enif_open_resource_type(env, NULL, "audio_clip", clip_dtor, flags, NULL);
    priv->player_type = enif_open_resource_type(env, NULL, "audio_player", player_dtor, flags, NULL);
    priv->stream_type = enif_open_resource_type(env, NULL, "audio_stream", stream_dtor, flags, NULL);
    priv->recorder_type = enif_open_resource_type(env, NULL, "audio_recorder", recorder_dtor, flags, NULL);
    if (priv->clip_type == NULL || priv->player_type == NULL || priv->stream_type == NULL || priv->recorder_type == NULL) {
        enif_free(priv);
        return 1;
    }
    priv->lock = enif_mutex_create("audio_engine");
    if (priv->lock == NULL) {
        enif_free(priv);
        return 1;
    }
    g_priv = priv;
    *priv_data = priv;
    return 0;
}

static void unload(ErlNifEnv *env, void *priv_data)
{
    priv_t *priv = (priv_t *)priv_data;
    (void)env;
    if (priv == NULL) {
        return;
    }
    engine_teardown(priv);
    if (priv->lock != NULL) {
        enif_mutex_destroy(priv->lock);
    }
    g_priv = NULL;
    enif_free(priv);
}

static ErlNifFunc nif_funcs[] = {
    {"engine_start", 5, nif_engine_start, DIRTY_CPU_NIF},
    {"engine_stop", 0, nif_engine_stop, DIRTY_CPU_NIF},
    {"engine_info", 0, nif_engine_info, 0},
    {"set_master_volume", 1, nif_set_master_volume, 0},
    {"device_list", 1, nif_device_list, DIRTY_CPU_NIF},
    {"clip_with_samples", 5, nif_clip_with_samples, DIRTY_CPU_NIF},
    {"clip_destroy", 1, nif_clip_destroy, DIRTY_CPU_NIF},
    {"clip_info", 1, nif_clip_info, 0},
    {"clip_force_uninit", 1, nif_clip_force_uninit, DIRTY_CPU_NIF},
    {"player_with_clip", 1, nif_player_with_clip, DIRTY_CPU_NIF},
    {"player_with_stream", 1, nif_player_with_stream, DIRTY_CPU_NIF},
    {"player_destroy", 1, nif_player_destroy, DIRTY_CPU_NIF},
    {"player_play", 1, nif_player_play, 0},
    {"player_stop", 1, nif_player_stop, 0},
    {"player_pause", 1, nif_player_pause, 0},
    {"player_resume", 1, nif_player_resume, 0},
    {"player_playing", 1, nif_player_playing, 0},
    {"player_set_volume", 2, nif_player_set_volume, 0},
    {"player_volume", 1, nif_player_volume, 0},
    {"player_set_pan", 2, nif_player_set_pan, 0},
    {"player_pan", 1, nif_player_pan, 0},
    {"player_set_pitch", 2, nif_player_set_pitch, 0},
    {"player_pitch", 1, nif_player_pitch, 0},
    {"player_set_loop", 2, nif_player_set_loop, 0},
    {"player_looping", 1, nif_player_looping, 0},
    {"player_seek", 2, nif_player_seek, 0},
    {"player_cursor", 1, nif_player_cursor, 0},
    {"wav_decode", 1, nif_wav_decode, DIRTY_CPU_NIF},
    {"wav_encode", 5, nif_wav_encode, DIRTY_CPU_NIF},
    {"stream_from_file", 1, nif_stream_from_file, DIRTY_CPU_NIF},
    {"stream_with_queue", 4, nif_stream_with_queue, DIRTY_CPU_NIF},
    {"stream_destroy", 1, nif_stream_destroy, DIRTY_CPU_NIF},
    {"stream_force_uninit", 1, nif_stream_force_uninit, DIRTY_CPU_NIF},
    {"stream_write", 6, nif_stream_write, 0},
    {"stream_available", 1, nif_stream_available, 0},
    {"stream_queued", 1, nif_stream_queued, 0},
    {"stream_underrun", 1, nif_stream_underrun, 0},
    {"stream_duration", 1, nif_stream_duration, 0},
    {"recorder_open", 5, nif_recorder_open, DIRTY_CPU_NIF},
    {"recorder_destroy", 1, nif_recorder_destroy, DIRTY_CPU_NIF},
    {"recorder_start", 1, nif_recorder_start, DIRTY_CPU_NIF},
    {"recorder_stop", 1, nif_recorder_stop, DIRTY_CPU_NIF},
    {"recorder_running", 1, nif_recorder_running, 0},
    {"recorder_read", 2, nif_recorder_read, DIRTY_CPU_NIF},
    {"recorder_overrun", 1, nif_recorder_overrun, 0},
    {"owner_down", 2, nif_owner_down, DIRTY_CPU_NIF},
    {"force_teardown", 2, nif_force_teardown, DIRTY_CPU_NIF}
};

ERL_NIF_INIT(audio_nif, nif_funcs, load, NULL, NULL, unload)
