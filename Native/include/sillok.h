#ifndef SILLOK_H
#define SILLOK_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sillok_engine sillok_engine;
typedef struct sillok_vad sillok_vad;
typedef struct sillok_speech_map sillok_speech_map;
typedef struct sillok_cancel sillok_cancel;
typedef struct sillok_buffer sillok_buffer;

typedef void (*sillok_read_audio)(void *context, double start, double end, sillok_buffer *output);

void sillok_free(char *string);
void sillok_buffer_assign(sillok_buffer *buffer, const float *samples, size_t count);

char *sillok_system_info(void);
int sillok_vad_frame_size(void);

sillok_cancel *sillok_cancel_new(void);
void sillok_cancel_set(sillok_cancel *cancel);
void sillok_cancel_free(sillok_cancel *cancel);

sillok_engine *sillok_engine_new(void);
void sillok_engine_free(sillok_engine *engine);
char *sillok_engine_load(sillok_engine *engine, const char *ggml_path, const char *coreml_encoder_path, int use_gpu);
void sillok_engine_unload(sillok_engine *engine);
char *sillok_engine_detect_language(sillok_engine *engine, const float *samples, size_t count, const char *allowed_json);
char *sillok_engine_transcribe(sillok_engine *engine, const float *samples, size_t count, const char *options_json, sillok_cancel *cancel);

sillok_vad *sillok_vad_new(const char *model_path, char **error);
void sillok_vad_free(sillok_vad *vad);

sillok_speech_map *sillok_speech_map_new(sillok_vad *vad);
void sillok_speech_map_free(sillok_speech_map *map);
char *sillok_speech_map_append(sillok_speech_map *map, const float *samples, size_t count);
char *sillok_speech_map_plan(sillok_speech_map *map, double duration, const char *planner_json);

char *sillok_transcribe_step(sillok_engine *engine, const char *state_json, const char *chunks_json, const char *options_json,
                             sillok_read_audio read, void *read_context, sillok_cancel *cancel);

char *sillok_export(const char *segments_json, const char *format);

#ifdef __cplusplus
}
#endif

#endif
