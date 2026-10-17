#ifndef SILLOK_H
#define SILLOK_H

#include <stddef.h>

#if defined(_WIN32) && defined(SILLOK_BUILDING_DLL)
#define SILLOK_API __declspec(dllexport)
#else
#define SILLOK_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sillok_engine sillok_engine;
typedef struct sillok_vad sillok_vad;
typedef struct sillok_speech_map sillok_speech_map;
typedef struct sillok_cancel sillok_cancel;
typedef struct sillok_buffer sillok_buffer;
typedef struct sillok_diarizer sillok_diarizer;

typedef void (*sillok_read_audio)(void *context, double start, double end, sillok_buffer *output);

SILLOK_API void sillok_free(char *string);
SILLOK_API void sillok_buffer_assign(sillok_buffer *buffer, const float *samples, size_t count);

SILLOK_API char *sillok_system_info(void);
SILLOK_API int sillok_vad_frame_size(void);

SILLOK_API sillok_cancel *sillok_cancel_new(void);
SILLOK_API void sillok_cancel_set(sillok_cancel *cancel);
SILLOK_API void sillok_cancel_free(sillok_cancel *cancel);

SILLOK_API sillok_engine *sillok_engine_new(void);
SILLOK_API void sillok_engine_free(sillok_engine *engine);
SILLOK_API char *sillok_engine_load(sillok_engine *engine, const char *ggml_path, const char *coreml_encoder_path, int use_gpu);
SILLOK_API void sillok_engine_unload(sillok_engine *engine);
SILLOK_API char *sillok_engine_detect_language(sillok_engine *engine, const float *samples, size_t count, const char *allowed_json);
SILLOK_API char *sillok_engine_transcribe(sillok_engine *engine, const float *samples, size_t count, const char *options_json, sillok_cancel *cancel);

SILLOK_API sillok_vad *sillok_vad_new(const char *model_path, char **error);
SILLOK_API void sillok_vad_free(sillok_vad *vad);

SILLOK_API sillok_speech_map *sillok_speech_map_new(sillok_vad *vad);
SILLOK_API void sillok_speech_map_free(sillok_speech_map *map);
SILLOK_API char *sillok_speech_map_append(sillok_speech_map *map, const float *samples, size_t count);
SILLOK_API char *sillok_speech_map_plan(sillok_speech_map *map, double duration, const char *planner_json);

SILLOK_API char *sillok_transcribe_step(sillok_engine *engine, const char *state_json, const char *chunks_json, const char *options_json,
                                        sillok_read_audio read, void *read_context, sillok_cancel *cancel);

SILLOK_API sillok_diarizer *sillok_diarizer_new(const char *model_path, int use_gpu, char **error);
SILLOK_API void sillok_diarizer_free(sillok_diarizer *diarizer);
SILLOK_API char *sillok_diarize(sillok_diarizer *diarizer, const char *segments_json, double duration,
                                sillok_read_audio read, void *read_context, sillok_cancel *cancel);

SILLOK_API char *sillok_export(const char *segments_json, const char *format);

#ifdef __cplusplus
}
#endif

#endif
