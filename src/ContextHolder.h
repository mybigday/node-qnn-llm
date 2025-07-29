#pragma once

#include "GenieDialog.h"
#include <atomic>
#include <functional>
#include <string>

class ContextHolder {
  using CompletionCallback =
      std::function<void(const char *, const GenieDialog_SentenceCode_t)>;

public:
  ContextHolder(std::string config_json);
  ~ContextHolder();
  void release();
  void process(std::string prompt);
  std::string query(std::string prompt, const CompletionCallback &callback);
  void abort();
  void save(std::string filename);
  void restore(std::string filename);
  void set_stop_words(std::string stop_words_json);
  void apply_sampler_config(std::string config_json);

protected:
  static void process_callback(const char *response,
                               const GenieDialog_SentenceCode_t sentenceCode,
                               const void *userData);
  static void on_response(const char *response,
                          const GenieDialog_SentenceCode_t sentenceCode,
                          const void *userData);

private:
  std::string full_context = "";
  std::atomic<bool> busying = false;
  GenieDialog_Handle_t dialog = NULL;
  GenieDialogConfig_Handle_t config = NULL;
  GenieProfile_Handle_t profile = NULL;
  CompletionCallback callback = nullptr;
};