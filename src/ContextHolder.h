#pragma once

#include "GenieDialog.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

typedef std::unordered_map<std::string, float> LoraStrengthMap;

// Defaults mirror qualla's basic sampler when the config leaves a key unset.
struct SamplerParams {
  float temperature = 0.1f;
  int32_t top_k = 0;
  float top_p = 0.8f;
  int32_t seed = -1;
  bool operator==(const SamplerParams &o) const {
    return temperature == o.temperature && top_k == o.top_k &&
           top_p == o.top_p && seed == o.seed;
  }
};

// Empty kind means no grammar. Either `content` (written to a temp file) or
// `file` (an existing path, e.g. from the dialog config) is used.
struct GrammarParams {
  std::string kind;
  std::string content;
  std::string file;
  bool operator==(const GrammarParams &o) const {
    return kind == o.kind && content == o.content && file == o.file;
  }
};

struct QueryParams {
  std::string prompt;
  std::string stop_json = "{}";
  uint32_t max_tokens = UINT32_MAX;
  SamplerParams sampler;
  GrammarParams grammar;
};

struct QueryResult {
  std::string profile_json;
  bool interrupted = false;
  int64_t num_generated = -1;
};

class ContextHolder {
  using CompletionCallback =
      std::function<void(const char *, const GenieDialog_SentenceCode_t)>;

public:
  // `defaults` holds the per-query values the dialog config was created with
  ContextHolder(std::string config_json, QueryParams defaults,
                std::string dialog_type);
  ~ContextHolder();
  void release();
  void process(std::string prompt);
  QueryResult query(const QueryParams &params, const CompletionCallback &callback);
  void abort();
  void save(std::string filename);
  void restore(std::string filename);
  void apply_lora(const std::string &engine, const std::string &lora_adapter_name);
  void set_lora_strength(const std::string &engine, const LoraStrengthMap &lora_strength_map);
  void release_lora_memory(const std::string &engine, const std::string &lora_adapter_name);
  uint32_t get_context_occupancy();
  std::string get_applied_lora_adapter();
  void reset();
  const QueryParams &defaults() const { return defaults_; }

protected:
  static void process_callback(const char *response,
                               const GenieDialog_SentenceCode_t sentenceCode,
                               const void *userData);
  static void on_response(const char *response,
                          const GenieDialog_SentenceCode_t sentenceCode,
                          const void *userData);

private:
  void apply_stop_words(const std::string &stop_json);
  void apply_max_tokens(uint32_t max_tokens);
  void apply_sampler(const SamplerParams &sampler);
  void apply_grammar(const GrammarParams &grammar);

  std::string full_context = "";
  QueryParams defaults_;
  std::string dialog_type;
  std::string applied_stop_json;
  uint32_t applied_max_tokens;
  SamplerParams applied_sampler;
  GrammarParams applied_grammar;
  std::atomic<bool> busying = false;
  GenieDialog_Handle_t dialog = NULL;
  GenieDialogConfig_Handle_t config = NULL;
  GenieProfile_Handle_t profile = NULL;
  CompletionCallback callback = nullptr;
};