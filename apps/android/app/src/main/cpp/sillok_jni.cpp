// JNI functions behind io.github.linkedlatch.sillok.engine.Native. Strings
// cross as UTF-8 byte arrays: JNI's own string functions use modified UTF-8,
// which the library does not accept and which cannot carry characters such as
// emoji unchanged. Failures throw SillokException with the library's status.

#include <jni.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <sillok/sillok.h>

namespace {

constexpr char kExceptionClass[] =
    "io/github/linkedlatch/sillok/engine/SillokException";
constexpr char kProgressClass[] =
    "io/github/linkedlatch/sillok/engine/Native$Progress";

jbyteArray NewBytes(JNIEnv* env, const char* text, std::size_t size) {
  jbyteArray bytes = env->NewByteArray(static_cast<jsize>(size));
  if (bytes != nullptr) {
    env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(size),
                            reinterpret_cast<const jbyte*>(text));
  }
  return bytes;
}

void Throw(JNIEnv* env, int status, const char* message) {
  jclass type = env->FindClass(kExceptionClass);
  if (type == nullptr) return;  // NoClassDefFoundError is pending
  jmethodID init = env->GetMethodID(type, "<init>", "(I[B)V");
  jbyteArray text = NewBytes(env, message, std::strlen(message));
  if (init == nullptr || text == nullptr) return;
  auto error = static_cast<jthrowable>(env->NewObject(type, init, status, text));
  if (error != nullptr) env->Throw(error);
}

// Throws for a failed status; returns whether the call succeeded.
bool Check(JNIEnv* env, sillok_status status) {
  if (status == SILLOK_OK) return true;
  Throw(env, status, sillok_last_error());
  return false;
}

void ThrowArgument(JNIEnv* env, const char* message) {
  Throw(env, SILLOK_INVALID_ARGUMENT, message);
}

std::string ToString(JNIEnv* env, jbyteArray bytes) {
  const jsize size = env->GetArrayLength(bytes);
  std::string text(static_cast<std::size_t>(size), '\0');
  env->GetByteArrayRegion(bytes, 0, size, reinterpret_cast<jbyte*>(text.data()));
  return text;
}

// Returns a string the library allocated as bytes, releasing it.
jbyteArray TakeString(JNIEnv* env, char* text) {
  jbyteArray bytes = NewBytes(env, text, std::strlen(text));
  sillok_free_string(text);
  return bytes;
}

// Reads `size` bytes of decoded PCM (16-bit integer or 32-bit float samples,
// as MediaCodec produces them) from a direct buffer as floats.
bool ReadPcm(JNIEnv* env, jobject buffer, jint offset, jint size,
             jboolean is_float, std::vector<float>& out) {
  auto* base = static_cast<const std::byte*>(env->GetDirectBufferAddress(buffer));
  const jlong capacity = env->GetDirectBufferCapacity(buffer);
  if (base == nullptr || offset < 0 || size < 0 ||
      static_cast<jlong>(offset) + size > capacity) {
    ThrowArgument(env, "PCM must lie within a direct buffer");
    return false;
  }
  const std::byte* data = base + offset;
  if (is_float) {
    out.resize(static_cast<std::size_t>(size) / sizeof(float));
    std::memcpy(out.data(), data, out.size() * sizeof(float));
  } else {
    std::vector<std::int16_t> samples(static_cast<std::size_t>(size) / sizeof(std::int16_t));
    std::memcpy(samples.data(), data, samples.size() * sizeof(std::int16_t));
    out.resize(samples.size());
    for (std::size_t i = 0; i < samples.size(); ++i) {
      out[i] = static_cast<float>(samples[i]) / 32768.0f;
    }
  }
  return true;
}

// The job's recording as 16-bit PCM for the platform encoder. The library
// keeps 16-bit samples, so scaling back is exact.
jshortArray ToPcm16(JNIEnv* env, const std::vector<float>& samples) {
  std::vector<jshort> pcm(samples.size());
  for (std::size_t i = 0; i < samples.size(); ++i) {
    const float scaled = std::round(samples[i] * 32768.0f);
    pcm[i] = static_cast<jshort>(std::clamp(scaled, -32768.0f, 32767.0f));
  }
  jshortArray array = env->NewShortArray(static_cast<jsize>(pcm.size()));
  if (array != nullptr) {
    env->SetShortArrayRegion(array, 0, static_cast<jsize>(pcm.size()), pcm.data());
  }
  return array;
}

template <typename T>
T* FromHandle(jlong handle) {
  return reinterpret_cast<T*>(static_cast<std::intptr_t>(handle));
}

template <typename T>
jlong ToHandle(T* pointer) {
  return static_cast<jlong>(reinterpret_cast<std::intptr_t>(pointer));
}

}  // namespace

#define SILLOK_JNI(type, name) \
  JNIEXPORT type JNICALL Java_io_github_linkedlatch_sillok_engine_Native_##name

extern "C" {

SILLOK_JNI(jbyteArray, version)(JNIEnv* env, jobject) {
  const char* version = sillok_version();
  return NewBytes(env, version, std::strlen(version));
}

SILLOK_JNI(jbyteArray, modelCatalog)(JNIEnv* env, jobject) {
  char* json = nullptr;
  if (!Check(env, sillok_model_catalog(&json))) return nullptr;
  return TakeString(env, json);
}

SILLOK_JNI(jbyteArray, modelCheck)(JNIEnv* env, jobject, jbyteArray dir,
                                  jboolean verify_hash) {
  char* json = nullptr;
  if (!Check(env, sillok_model_check(ToString(env, dir).c_str(),
                                     verify_hash ? 1 : 0, &json))) {
    return nullptr;
  }
  return TakeString(env, json);
}

SILLOK_JNI(jbyteArray, devices)(JNIEnv* env, jobject) {
  char* json = nullptr;
  if (!Check(env, sillok_devices(&json))) return nullptr;
  return TakeString(env, json);
}

SILLOK_JNI(jlong, engineCreate)(JNIEnv* env, jobject, jbyteArray config) {
  sillok_engine* engine = nullptr;
  if (!Check(env, sillok_engine_create(ToString(env, config).c_str(), &engine))) {
    return 0;
  }
  return ToHandle(engine);
}

SILLOK_JNI(void, engineDestroy)(JNIEnv*, jobject, jlong engine) {
  sillok_engine_destroy(FromHandle<sillok_engine>(engine));
}

SILLOK_JNI(jlong, jobCreate)(JNIEnv* env, jobject, jlong engine, jbyteArray options) {
  sillok_job* job = nullptr;
  if (!Check(env, sillok_job_create(FromHandle<sillok_engine>(engine),
                                    ToString(env, options).c_str(), &job))) {
    return 0;
  }
  return ToHandle(job);
}

SILLOK_JNI(void, jobPushAudio)(JNIEnv* env, jobject, jlong job, jobject buffer,
                              jint offset, jint size, jint sample_rate,
                              jint channels, jboolean is_float) {
  if (channels <= 0) {
    ThrowArgument(env, "channels must be positive");
    return;
  }
  std::vector<float> samples;
  if (!ReadPcm(env, buffer, offset, size, is_float, samples)) return;
  Check(env, sillok_job_push_audio(FromHandle<sillok_job>(job), samples.data(),
                                   samples.size() / static_cast<std::size_t>(channels),
                                   sample_rate, channels));
}

SILLOK_JNI(jbyteArray, jobRun)(JNIEnv* env, jobject, jlong job, jobject progress) {
  struct Listener {
    JNIEnv* env;
    jobject target;
    jmethodID method;
  };
  jclass type = env->FindClass(kProgressClass);
  if (type == nullptr) return nullptr;
  Listener listener{env, progress, env->GetMethodID(type, "onProgress", "(ID)V")};
  if (listener.method == nullptr) return nullptr;
  const auto report = [](void* user_data, sillok_stage stage, double fraction) {
    auto* self = static_cast<Listener*>(user_data);
    // A listener that threw has its exception pending; JNI forbids calling
    // back into Java until the run returns and Java sees it.
    if (self->env->ExceptionCheck()) return;
    self->env->CallVoidMethod(self->target, self->method,
                              static_cast<jint>(stage), fraction);
  };
  char* json = nullptr;
  const sillok_status status =
      sillok_job_run(FromHandle<sillok_job>(job), report, &listener, &json);
  if (env->ExceptionCheck()) {
    sillok_free_string(json);
    return nullptr;
  }
  if (!Check(env, status)) return nullptr;
  return TakeString(env, json);
}

SILLOK_JNI(void, jobCancel)(JNIEnv*, jobject, jlong job) {
  sillok_job_cancel(FromHandle<sillok_job>(job));
}

SILLOK_JNI(void, jobDestroy)(JNIEnv*, jobject, jlong job) {
  sillok_job_destroy(FromHandle<sillok_job>(job));
}

SILLOK_JNI(jbyteArray, export)(JNIEnv* env, jobject, jbyteArray transcript,
                              jbyteArray format, jbyteArray options) {
  const std::string options_text = options ? ToString(env, options) : std::string();
  char* text = nullptr;
  if (!Check(env, sillok_export(ToString(env, transcript).c_str(),
                                ToString(env, format).c_str(),
                                options ? options_text.c_str() : nullptr, &text))) {
    return nullptr;
  }
  return TakeString(env, text);
}

SILLOK_JNI(void, jobEndAudio)(JNIEnv* env, jobject, jlong job) {
  Check(env, sillok_job_end_audio(FromHandle<sillok_job>(job)));
}

SILLOK_JNI(jlong, jobAudioLength)(JNIEnv* env, jobject, jlong job) {
  std::uint64_t length = 0;
  if (!Check(env, sillok_job_audio_length(FromHandle<sillok_job>(job), &length))) return 0;
  return static_cast<jlong>(length);
}

SILLOK_JNI(jshortArray, jobReadAudio)(JNIEnv* env, jobject, jlong job, jlong start,
                                      jint count) {
  if (start < 0 || count < 0) {
    ThrowArgument(env, "start and count must not be negative");
    return nullptr;
  }
  std::vector<float> samples(static_cast<std::size_t>(count));
  std::size_t read = 0;
  if (!Check(env, sillok_job_read_audio(FromHandle<sillok_job>(job),
                                        static_cast<std::uint64_t>(start),
                                        samples.data(), samples.size(), &read))) {
    return nullptr;
  }
  samples.resize(read);
  return ToPcm16(env, samples);
}

}  // extern "C"
