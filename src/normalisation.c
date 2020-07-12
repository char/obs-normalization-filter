#include "normalisation.h"

#define do_log(level, format, ...)                          \
    blog(level, "[normalization filter: '%s'] " format, \
         obs_source_get_name(nf->context), ##__VA_ARGS__)

#define warn(format, ...) do_log(LOG_WARNING, format, ##__VA_ARGS__)
#define info(format, ...) do_log(LOG_INFO, format, ##__VA_ARGS__)

#define S_NORMALIZATION_DB "db"
#define S_NORMALIZATION_CAP "cap"
#define S_NORMALIZATION_WINDOW "window"
#define S_NORMALIZATION_LOOKAHEAD "lookahead"

#define TEXT_NORMALIZATION_DB "Target (dB)"
#define TEXT_NORMALIZATION_CAP "Gain cap (dB)"
#define TEXT_NORMALIZATION_WINDOW "Window (seconds)"
#define TEXT_NORMALIZATION_LOOKAHEAD "Lookahead (percent)"


struct normalization_data {
    obs_source_t *context;
    ebur128_state **sts;
    size_t channels;
    uint32_t sample_rate;
    uint32_t head;
    uint32_t count;
    uint32_t frame_cap;
    float **frames;
    double target;
    double cap;
};


static inline void del_frame_buf(struct normalization_data *nf) {
    if (nf->frames) {
        for (size_t c = 0; c < nf->channels; ++c)
            bfree(nf->frames[c]);
        bfree(nf->frames);
    }
}

static const char* get_name(void* data) {
    (void) data;
    return "Normalization";
}

static void update(void* data, obs_data_t* s) {
    struct normalization_data *nf = data;
    nf->target = obs_data_get_double(s, S_NORMALIZATION_DB);
    nf->cap = obs_data_get_double(s, S_NORMALIZATION_CAP);
    uint32_t window = obs_data_get_int(s, S_NORMALIZATION_WINDOW);
    uint32_t frame_cap =
        window * obs_data_get_int(s, S_NORMALIZATION_LOOKAHEAD);
    window *= 1000;
    frame_cap *= nf->sample_rate / 100;
    if (frame_cap != nf->frame_cap) {
        float **new = bzalloc(nf->channels * sizeof(float *));
        for (size_t c = 0; c < nf->channels; ++c)
            new[c] = bzalloc(frame_cap * sizeof(float));
        nf->head = 0;
        nf->count = 0;
        nf->frame_cap = frame_cap;
        del_frame_buf(nf);
        nf->frames = new;
        for (size_t c = 0; c < nf->channels; ++c)
            ebur128_set_max_history(nf->sts[c], window);
    }
}

static void* create(obs_data_t* settings, obs_source_t* filter) {
    struct normalization_data *nf = bzalloc(sizeof(*nf));
    nf->context = filter;
    nf->channels = audio_output_get_channels(obs_get_audio());
    nf->sample_rate = audio_output_get_sample_rate(obs_get_audio());
    nf->head = 0;
    nf->count = 0;
    nf->frame_cap = 0;
    nf->frames = NULL;
    nf->sts = bzalloc(nf->channels * sizeof(ebur128_state *));
    for (size_t c = 0; c < nf->channels; ++c)
        nf->sts[c] = ebur128_init(1, nf->sample_rate, EBUR128_MODE_I);
    update(nf, settings);
    return nf;
}

static void destroy(void* data) {
    struct normalization_data *nf = data;

    for (size_t c = 0; c < nf->channels; ++c)
        ebur128_destroy(nf->sts + c);

    del_frame_buf(nf);

    bfree(nf->sts);
    bfree(nf);
}


static inline bool push_multiply(struct normalization_data *nf, float **arr,
        uint32_t len, double mul) {
    /* if don't lookahead */
    if (!nf->frame_cap) {
        for (size_t c = 0; c < nf->channels; ++c) {
            for (uint32_t i = 0; i < len; ++i) arr[c][i] *= mul;
        }
        return true;
    }
    const uint32_t free = nf->frame_cap - nf->count;
    for (size_t c = 0; c < nf->channels; ++c) {
        float *start = nf->frames[c];
        float *end = start + nf->frame_cap;
        float *cur = start + nf->head;
        float *newp1 = arr[c];
        float *newp2 = newp1;
        float *newend = newp1 + len;
        for (uint32_t n = 0; n < free; ++n) {
            *cur = *newp1++;
            ++cur;
            if (cur == end)
                cur = start;
            if (newp1 == newend)
                break;
        }
        if (newp1 == newend)
            continue;
        for (uint32_t n = 0; n < len - free; ++n) {
            float t = *cur;
            *cur = *newp1++;
            *newp2 = t * mul;
            ++newp2;
            ++cur;
            if (cur == end)
                cur = start;
        }
        for (uint32_t n = 0; n < free; ++n) {
            *newp2 = *cur * mul;
            ++newp2;
            ++cur;
            if (cur == end)
                cur = start;
        }
    }
    nf->head += len;
    nf->head %= nf->frame_cap;
    if (len <= free) {
        nf->count += len;
        return false;
    } else {
        nf->count = nf->frame_cap - free;
        return true;
    }
}

static struct obs_audio_data* filter_audio(void *data, struct obs_audio_data *audio) {
    struct normalization_data *nf = data;
    const size_t channels = nf->channels;
    float **adata = (float **)audio->data;
    double gain;
    for (size_t c = 0; c < channels; ++c)
        if (!adata[c]) return audio;
    for (size_t c = 0; c < channels; ++c)
        ebur128_add_frames_float(nf->sts[c], adata[c], audio->frames);
    ebur128_loudness_global_multiple(nf->sts, channels, &gain);
    if (gain == gain && gain != -HUGE_VAL) {
        gain = nf->target - gain;
        gain = gain < nf->cap ? gain : nf->cap;
    } else {
        gain = 0;
    }
    if (push_multiply(nf, adata, audio->frames, db_to_mul(gain)))
        return audio;
    else
        return NULL;
}

static obs_properties_t* get_properties(void* data) {
    (void) data;

    obs_properties_t *ppts = obs_properties_create();
    obs_properties_add_float_slider(ppts, S_NORMALIZATION_DB,
                    TEXT_NORMALIZATION_DB, -60.0, 0.0, 0.1);
    obs_properties_add_float_slider(ppts, S_NORMALIZATION_CAP,
                    TEXT_NORMALIZATION_CAP, 0.0, 30.0, 0.1);
    obs_properties_add_int(ppts, S_NORMALIZATION_WINDOW,
                   TEXT_NORMALIZATION_WINDOW, 3, 30, 1);
    obs_properties_add_int(ppts, S_NORMALIZATION_LOOKAHEAD,
                   TEXT_NORMALIZATION_LOOKAHEAD, 0, 100, 1);

    return ppts;
}

static void get_defaults(obs_data_t* s) {
    obs_data_set_default_double(s, S_NORMALIZATION_DB, -23.0f);
    obs_data_set_default_double(s, S_NORMALIZATION_CAP, 20.0f);
    obs_data_set_default_int(s, S_NORMALIZATION_WINDOW, 10);
    obs_data_set_default_int(s, S_NORMALIZATION_LOOKAHEAD, 50);
}

struct obs_source_info normalisation_source = {
    .id = "obs_normalization_filter",
    .type = OBS_SOURCE_TYPE_FILTER,
    .output_flags = OBS_SOURCE_AUDIO,
    .get_name = get_name,
    .create = create,
    .destroy = destroy,
    .update = update,
    .filter_audio = filter_audio,
    .get_properties = get_properties,
    .get_defaults = get_defaults,
};
