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

enum ModelFlags : uint32_t {
  kFlagEscapeSpaces       = 1u << 0,
  kFlagMultiTurn          = 1u << 1,
  kFlagHasEmotionTags     = 1u << 2,
  kFlagSuppressCtrlTokens = 1u << 3
};

const wchar_t * emotionLabel(int emoIdx);
int inferFallbackEmotion(const wchar_t * replyW);

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
  int formatVersion;
  uint32_t promptStyle;
  uint32_t flags;
  uint64_t paramCount;
  wchar_t displayName[32];
};

struct Q8TensorView {
  const int8_t * q;
  const uint16_t * sQ15;
  const float * rowScale;
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
  void resetConversation();

  bool isLoaded() const { return m_info.loaded; }
  const ModelInfo & info() const { return m_info; }

  int contextUsed() const { return m_kvPos; }
  int prefillProgress() const { return m_prefillIdx; }
  int prefillTotal() const { return m_promptLen; }
  bool isMultiTurn() const { return (m_info.flags & kFlagMultiTurn) != 0; }
  bool hasEmotionTags() const { return (m_info.flags & kFlagHasEmotionTags) != 0; }
  bool didContextAutoReset() const { return m_contextAutoReset; }

  // Begins async-friendly token-by-token generation for one turn.
  bool beginPrompt(const wchar_t * userTextW, float temperature, int maxNewTokens,
                   int minNewTokens = 0, float repPenalty = 0.0f);

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
  int encodeLineUtf8(const char * lineUtf8, int * outTokens, int maxTokens) const;
  int encodeTextUtf8(const char * textUtf8, int * outTokens, int maxTokens) const;
  void decodeTokenBytes(int tok, wchar_t * outW, size_t maxW);

  void forwardToken(int token, int pos, bool needLogits);
  int sampleLogit(float temperature);

  ModelInfo m_info;
  uint8_t * m_modelBuf;
  uint8_t * m_v1ScalePool;
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
  int m_bosId;
  int m_eosId;
  int m_endId;
  int m_systemId;
  int m_userId;
  int m_assistantId;
  int m_newlineId;
  int m_minNormalTokId;
  float m_rmsEps;
  float m_ropeTheta;
  char m_sysPrompt[256];

  // Precomputed RoPE cos/sin table [seqLen * (headDim / 2)]
  float * m_ropeCos;
  float * m_ropeSin;

  // RunState activations & KV cache (float32 for tiny <= 1MB KV; INT8 per-head for larger models)
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
  bool m_useInt8Kv;
  float * m_kCacheF;
  float * m_vCacheF;
  int8_t * m_kCacheQ;
  int8_t * m_vCacheQ;
  float * m_kCacheS;
  float * m_vCacheS;
  int8_t * m_xq;
  uint16_t * m_xsQ15;
  int8_t * m_hq;
  uint16_t * m_hsQ15;
  int8_t * m_qHeadQ;
  int16_t * m_attQ;
  int32_t * m_oAcc;

  struct ProbItem {
    float p;
    int idx;
  };
  ProbItem * m_probBuf;

  // Streaming conversation state
  static constexpr int kMaxPromptTokens = 256;
  static constexpr int kRepeatWin = 32;
  int m_promptTokens[kMaxPromptTokens];
  int m_promptLen;
  int m_prefillIdx;
  int m_pos;
  int m_kvPos;
  int m_curTok;
  int m_genCount;
  int m_maxNewTokens;
  int m_minNewTokens;
  float m_temperature;
  float m_repPenalty;
  bool m_active;
  bool m_hasSystemPrefilled;
  bool m_needCloseTurn;
  int m_pendingLastTok;
  int m_turnCount;
  bool m_contextAutoReset;
  int m_turnStartKvPos;
  bool m_turnStartNeedClose;
  int m_turnStartPendingTok;
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
