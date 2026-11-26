#ifndef LILIUM_SUITE_CHAT_TINY_LLM_H
#define LILIUM_SUITE_CHAT_TINY_LLM_H

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <stddef.h>
#include <stdint.h>

namespace LiliumLLM {

static constexpr int kNumEmotions = 8;

enum EmotionId {
  EmoNormal = 0,     // 〈ふつう〉 (tok 5)
  EmoHappy = 1,      // 〈うれしい〉 (tok 6)
  EmoSmug = 2,       // 〈ドヤ〉 (tok 7)
  EmoSurprised = 3,  // 〈びっくり〉 (tok 8)
  EmoSad = 4,        // 〈かなしい〉 (tok 9)
  EmoAngry = 5,      // 〈ぷんぷん〉 (tok 10)
  EmoTroubled = 6,   // 〈こまった〉 (tok 11)
  EmoSleepy = 7      // 〈ねむい〉 (tok 12)
};

const wchar_t * emotionLabel(int emoIdx);

struct ModelInfo {
  int dim;
  int hiddenDim;
  int nLayers;
  int nHeads;
  int nKvHeads;
  int vocabSize;
  int seqLen;
  int groupSize;
  size_t fileBytes;
  bool loaded;
};

struct Q8TensorView {
  const int8_t * q;
  const float * s;
};

class TinyLlmEngine {
public:
  TinyLlmEngine();
  ~TinyLlmEngine();

  TinyLlmEngine(const TinyLlmEngine &) = delete;
  TinyLlmEngine & operator=(const TinyLlmEngine &) = delete;

  bool load(const wchar_t * modelPath, const wchar_t * tokPath,
            wchar_t * outErr, size_t maxErrLen);
  void unload();

  bool isLoaded() const { return m_info.loaded; }
  const ModelInfo & info() const { return m_info; }

  // Begins async-friendly token-by-token generation for one turn (`<s> A: <prompt> B:`).
  bool beginPrompt(const wchar_t * userTextW, float temperature, int maxNewTokens);

  // Advances 1 prefill or generation token per call.
  // Returns true if an output delta or state change occurred.
  bool stepToken(wchar_t * outDeltaW, size_t maxDeltaW,
                 bool & outGeneratingText, int & outEmotionIdx, bool & outDone);

  void abortGeneration();

  int promptTokenCount() const { return m_promptLen; }
  int currentPos() const { return m_pos; }
  int generatedTokenCount() const { return m_genCount; }

private:
  struct VocabLookup {
    const char * str;
    int id;
  };

  int findTokenId(const char * utf8Str) const;
  int encodePromptUtf8(const char * textUtf8, int * outTokens, int maxTokens) const;
  void decodeTokenBytes(int tok, wchar_t * outW, size_t maxW);

  void forwardToken(int token, int pos, bool needLogits);
  int sampleLogit(float temperature);

  ModelInfo m_info;
  uint8_t * m_modelBuf;
  const float * m_rmsAtt;
  const float * m_rmsFfn;
  const float * m_rmsFinal;
  Q8TensorView m_qTokens;
  Q8TensorView * m_wq;
  Q8TensorView * m_wk;
  Q8TensorView * m_wv;
  Q8TensorView * m_wo;
  Q8TensorView * m_w1;
  Q8TensorView * m_w2;
  Q8TensorView * m_w3;
  Q8TensorView m_wcls;
  bool m_sharedWeights;

  // Tokenizer state
  uint8_t * m_tokBuf;
  int m_tokCount;
  const char ** m_vocabStr;
  float * m_vocabScore;
  VocabLookup * m_sortedLookup;
  int m_byteTokenIds[256];
  int m_tokPromptA;
  int m_tokPromptB;

  // Precomputed RoPE cos/sin table [seqLen * (headDim / 2)]
  float * m_ropeCos;
  float * m_ropeSin;

  // RunState activations & KV cache
  float * m_x;
  float * m_xb;
  float * m_xb2;
  float * m_hb;
  float * m_hb2;
  float * m_q;
  float * m_k;
  float * m_v;
  float * m_att;
  float * m_logits;
  float * m_kCache;
  float * m_vCache;
  int8_t * m_xq;
  float * m_xs;
  int8_t * m_hq;
  float * m_hs;

  struct ProbItem {
    float p;
    int idx;
  };
  ProbItem * m_probBuf;

  // Streaming conversation state
  static constexpr int kMaxPromptTokens = 160;
  static constexpr int kRepeatWin = 32;
  int m_promptTokens[kMaxPromptTokens];
  int m_promptLen;
  int m_pos;
  int m_curTok;
  int m_genCount;
  int m_maxNewTokens;
  float m_temperature;
  bool m_active;
  uint64_t m_rng;
  int m_recentToks[kRepeatWin];
  int m_recentCount;

  // UTF-8 streaming byte decoder for <0xXX> fallback tokens
  uint8_t m_utf8Pend[4];
  int m_utf8PendLen;
  int m_utf8Need;
};

} // namespace LiliumLLM

#endif // LILIUM_SUITE_CHAT_TINY_LLM_H
