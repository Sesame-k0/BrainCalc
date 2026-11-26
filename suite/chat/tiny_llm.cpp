#include "tiny_llm.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

namespace LiliumLLM {

namespace {

static constexpr uint32_t kTtq8Magic = 0x38515454u; // "TTQ8" LE

static constexpr const wchar_t * kEmotionNames[kNumEmotions] = {
  L"ふつう",
  L"うれしい",
  L"ドヤ",
  L"びっくり",
  L"かなしい",
  L"ぷんぷん",
  L"こまった",
  L"ねむい"
};

static float g_expLut[1025];
static bool g_expLutReady = false;

static void ensureExpLut() {
  if (g_expLutReady) return;
  for (int i = 0; i <= 1024; ++i) {
    float x = -12.0f + (12.0f * static_cast<float>(i)) / 1024.0f;
    g_expLut[i] = expf(x);
  }
  g_expLutReady = true;
}

static inline float fastExpNeg(float x) {
  if (x <= -12.0f) return 0.0f;
  if (x >= 0.0f) return 1.0f;
  float pos = (x + 12.0f) * (1024.0f / 12.0f);
  int idx = static_cast<int>(pos);
  if (idx >= 1024) return 1.0f;
  float frac = pos - static_cast<float>(idx);
  return g_expLut[idx] + frac * (g_expLut[idx + 1] - g_expLut[idx]);
}

static inline float fastSilu(float x) {
  if (x >= 0.0f) {
    float e = fastExpNeg(-x);
    return x / (1.0f + e);
  }
  float e = fastExpNeg(x);
  return (x * e) / (1.0f + e);
}

static uint32_t readU32LE(const uint8_t * p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

static int32_t readI32LE(const uint8_t * p) {
  return static_cast<int32_t>(readU32LE(p));
}

static float readF32LE(const uint8_t * p) {
  float v = 0.0f;
  memcpy(&v, p, sizeof(float));
  return v;
}

static int compareVocabStr(const void * a, const void * b) {
  const auto * va = static_cast<const uint8_t *>(a);
  const auto * vb = static_cast<const uint8_t *>(b);
  const char * sa = *reinterpret_cast<const char * const *>(va);
  const char * sb = *reinterpret_cast<const char * const *>(vb);
  return strcmp(sa, sb);
}

static int compareProbDesc(const void * a, const void * b) {
  float pa = *static_cast<const float *>(a);
  float pb = *static_cast<const float *>(b);
  if (pa > pb) return -1;
  if (pa < pb) return 1;
  return 0;
}

static void rmsnorm(float * out, const float * x, const float * w, int size, float eps) {
  float ss = 0.0f;
  for (int j = 0; j < size; ++j) {
    ss += x[j] * x[j];
  }
  ss /= static_cast<float>(size);
  ss += eps;
  float inv = 1.0f / sqrtf(ss);
  for (int j = 0; j < size; ++j) {
    out[j] = w[j] * (inv * x[j]);
  }
}

static void dequantizeRowQ15(float * out, const Q8TensorView & t, int row, int rowLen, int gs) {
  const int8_t * q = t.q + static_cast<size_t>(row) * static_cast<size_t>(rowLen);
  const int groups = rowLen / gs;
  const uint16_t * sQ15 = t.sQ15 + static_cast<size_t>(row) * static_cast<size_t>(groups);
  const float rScale = t.rowScale[row] * (1.0f / 32767.0f);
  for (int g = 0; g < groups; ++g) {
    float sc = rScale * static_cast<float>(sQ15[g]);
    const int8_t * qp = q + g * gs;
    float * op = out + g * gs;
    for (int k = 0; k < gs; ++k) {
      op[k] = static_cast<float>(qp[k]) * sc;
    }
  }
}

static inline int8_t quantClampI8(float val, float invScale) {
  float scaled = val * invScale;
  int qi = (scaled >= 0.0f) ? static_cast<int>(scaled + 0.5f) : static_cast<int>(scaled - 0.5f);
  if (qi > 127) qi = 127;
  if (qi < -127) qi = -127;
  return static_cast<int8_t>(qi);
}

static void quantizeVecQ15(const float * x, int8_t * xq, uint16_t * xsQ15,
                           float & outXFactor, int n, int gs) {
  const int groups = n / gs;
  float tmpSc[128];
  float xmaxS = 0.0f;
  for (int g = 0; g < groups; ++g) {
    const float * xp = x + g * gs;
    float wmax = 0.0f;
    for (int k = 0; k < gs; ++k) {
      float v = xp[k];
      if (v < 0.0f) v = -v;
      if (v > wmax) wmax = v;
    }
    float d = wmax / 127.0f;
    float id = (wmax > 1e-12f) ? (127.0f / wmax) : 0.0f;
    tmpSc[g] = d;
    if (d > xmaxS) xmaxS = d;
    int8_t * qp = xq + g * gs;
    for (int k = 0; k < gs; ++k) {
      qp[k] = quantClampI8(xp[k], id);
    }
  }
  outXFactor = xmaxS * (1.0f / 32767.0f);
  float rinv = (xmaxS > 1e-12f) ? (32767.0f / xmaxS) : 0.0f;
  for (int g = 0; g < groups; ++g) {
    int q = static_cast<int>(tmpSc[g] * rinv + 0.5f);
    if (q < 0) q = 0;
    if (q > 32767) q = 32767;
    xsQ15[g] = static_cast<uint16_t>(q);
  }
}

// Pure-integer Q15 group-scale MAC with 2-row unrolling (minimizes soft-float on ARM926EJ-S)
static void matmulQ8Int(float * out, const int8_t * xq, const uint16_t * xsQ15,
                        float xFactor, const Q8TensorView & w, int nIn, int nOut, int gs) {
  const int groups = nIn / gs;
  int i = 0;
  for (; i + 1 < nOut; i += 2) {
    const int8_t * wq0 = w.q + static_cast<size_t>(i) * static_cast<size_t>(nIn);
    const int8_t * wq1 = wq0 + nIn;
    const uint16_t * ws0 = w.sQ15 + static_cast<size_t>(i) * static_cast<size_t>(groups);
    const uint16_t * ws1 = ws0 + groups;
    int64_t acc0 = 0;
    int64_t acc1 = 0;
    for (int g = 0; g < groups; ++g) {
      const int8_t * xgp = xq + g * gs;
      const int8_t * w0p = wq0 + g * gs;
      const int8_t * w1p = wq1 + g * gs;
      int32_t v0 = 0;
      int32_t v1 = 0;
      for (int k = 0; k < gs; k += 4) {
        int32_t x0 = xgp[k];
        int32_t x1 = xgp[k + 1];
        int32_t x2 = xgp[k + 2];
        int32_t x3 = xgp[k + 3];
        v0 += x0 * w0p[k] + x1 * w0p[k + 1] + x2 * w0p[k + 2] + x3 * w0p[k + 3];
        v1 += x0 * w1p[k] + x1 * w1p[k + 1] + x2 * w1p[k + 2] + x3 * w1p[k + 3];
      }
      uint32_t xg = xsQ15[g];
      int32_t s0 = static_cast<int32_t>((xg * static_cast<uint32_t>(ws0[g])) >> 15);
      int32_t s1 = static_cast<int32_t>((xg * static_cast<uint32_t>(ws1[g])) >> 15);
      acc0 += static_cast<int64_t>(v0) * s0;
      acc1 += static_cast<int64_t>(v1) * s1;
    }
    out[i]     = static_cast<float>(acc0) * (xFactor * w.rowScale[i]);
    out[i + 1] = static_cast<float>(acc1) * (xFactor * w.rowScale[i + 1]);
  }
  for (; i < nOut; ++i) {
    const int8_t * wq0 = w.q + static_cast<size_t>(i) * static_cast<size_t>(nIn);
    const uint16_t * ws0 = w.sQ15 + static_cast<size_t>(i) * static_cast<size_t>(groups);
    int64_t acc0 = 0;
    for (int g = 0; g < groups; ++g) {
      const int8_t * xgp = xq + g * gs;
      const int8_t * w0p = wq0 + g * gs;
      int32_t v0 = 0;
      for (int k = 0; k < gs; k += 4) {
        v0 += static_cast<int32_t>(xgp[k])     * static_cast<int32_t>(w0p[k])
            + static_cast<int32_t>(xgp[k + 1]) * static_cast<int32_t>(w0p[k + 1])
            + static_cast<int32_t>(xgp[k + 2]) * static_cast<int32_t>(w0p[k + 2])
            + static_cast<int32_t>(xgp[k + 3]) * static_cast<int32_t>(w0p[k + 3]);
      }
      int32_t s0 = static_cast<int32_t>((static_cast<uint32_t>(xsQ15[g]) * static_cast<uint32_t>(ws0[g])) >> 15);
      acc0 += static_cast<int64_t>(v0) * s0;
    }
    out[i] = static_cast<float>(acc0) * (xFactor * w.rowScale[i]);
  }
}

static void softmaxFast(float * x, int size) {
  if (size <= 0) return;
  float mx = x[0];
  for (int i = 1; i < size; ++i) {
    if (x[i] > mx) mx = x[i];
  }
  float sum = 0.0f;
  for (int i = 0; i < size; ++i) {
    x[i] = fastExpNeg(x[i] - mx);
    sum += x[i];
  }
  float inv = (sum > 0.0f) ? (1.0f / sum) : 1.0f;
  for (int i = 0; i < size; ++i) {
    x[i] *= inv;
  }
}

static int utf8CharByteLen(uint8_t b) {
  if ((b & 0x80u) == 0) return 1;
  if ((b & 0xE0u) == 0xC0u) return 2;
  if ((b & 0xF0u) == 0xE0u) return 3;
  if ((b & 0xF8u) == 0xF0u) return 4;
  return 1;
}

static size_t v1ScalePoolNeed(size_t rows, size_t cols, int gs) {
  size_t ng = cols / static_cast<size_t>(gs);
  size_t sCountAligned = ((rows * ng) + 1u) & ~static_cast<size_t>(1u);
  return sCountAligned * sizeof(uint16_t) + rows * sizeof(float);
}

static bool sliceV1Convert(const uint8_t * base, size_t totalBytes, size_t & ioOff,
                           size_t rows, size_t cols, int gs,
                           Q8TensorView & out, uint8_t * & ioPool) {
  size_t elemCount = rows * cols;
  if (cols % static_cast<size_t>(gs) != 0) return false;
  size_t ng = cols / static_cast<size_t>(gs);
  size_t sCount = rows * ng;
  size_t need = elemCount + sCount * sizeof(float);
  if (ioOff + need > totalBytes) return false;

  out.q = reinterpret_cast<const int8_t *>(base + ioOff);
  ioOff += elemCount;
  const float * sRaw = reinterpret_cast<const float *>(base + ioOff);
  ioOff += sCount * sizeof(float);

  size_t sCountAligned = (sCount + 1u) & ~static_cast<size_t>(1u);
  uint16_t * dstQ15 = reinterpret_cast<uint16_t *>(ioPool);
  ioPool += sCountAligned * sizeof(uint16_t);
  float * dstRow = reinterpret_cast<float *>(ioPool);
  ioPool += rows * sizeof(float);

  out.sQ15 = dstQ15;
  out.rowScale = dstRow;

  for (size_t r = 0; r < rows; ++r) {
    const float * rRaw = sRaw + r * ng;
    float rmax = 0.0f;
    for (size_t g = 0; g < ng; ++g) {
      if (rRaw[g] > rmax) rmax = rRaw[g];
    }
    dstRow[r] = rmax;
    float rinv = (rmax > 1e-12f) ? (32767.0f / rmax) : 0.0f;
    for (size_t g = 0; g < ng; ++g) {
      int q = static_cast<int>(rRaw[g] * rinv + 0.5f);
      if (q < 0) q = 0;
      if (q > 32767) q = 32767;
      dstQ15[r * ng + g] = static_cast<uint16_t>(q);
    }
  }
  return true;
}

static bool sliceV2Q8(const uint8_t * base, size_t totalBytes, size_t & ioOff,
                      size_t rows, size_t cols, int gs, Q8TensorView & out) {
  if (cols % static_cast<size_t>(gs) != 0) return false;
  size_t elemCount = rows * cols;
  size_t ng = cols / static_cast<size_t>(gs);
  size_t sCount = rows * ng;
  size_t sCountAligned = (sCount + 1u) & ~static_cast<size_t>(1u);
  size_t need = elemCount + sCountAligned * sizeof(uint16_t) + rows * sizeof(float);
  if (ioOff + need > totalBytes) return false;

  out.q = reinterpret_cast<const int8_t *>(base + ioOff);
  ioOff += elemCount;
  out.sQ15 = reinterpret_cast<const uint16_t *>(base + ioOff);
  ioOff += sCountAligned * sizeof(uint16_t);
  out.rowScale = reinterpret_cast<const float *>(base + ioOff);
  ioOff += rows * sizeof(float);
  return true;
}

} // namespace

const wchar_t * emotionLabel(int emoIdx) {
  if (emoIdx < 0 || emoIdx >= kNumEmotions) return kEmotionNames[0];
  return kEmotionNames[emoIdx];
}

int inferFallbackEmotion(const wchar_t * replyW) {
  if (!replyW || !replyW[0]) return EmoNormal;
  if (wcsstr(replyW, L"おやす") || wcsstr(replyW, L"ねむ") || wcsstr(replyW, L"眠")) return EmoSleepy;
  if (wcsstr(replyW, L"好き") || wcsstr(replyW, L"うれし") || wcsstr(replyW, L"嬉") ||
      wcsstr(replyW, L"楽し") || wcsstr(replyW, L"ありがと") || wcsstr(replyW, L"元気") ||
      wcsstr(replyW, L"おはよう")) {
    return EmoHappy;
  }
  if (wcsstr(replyW, L"ごめん") || wcsstr(replyW, L"悲し") || wcsstr(replyW, L"寂し") ||
      wcsstr(replyW, L"つかれ")) {
    return EmoSad;
  }
  if (wcsstr(replyW, L"すごい") || wcsstr(replyW, L"さすが") || wcsstr(replyW, L"やった")) {
    return EmoSmug;
  }
  if (wcsstr(replyW, L"びっくり") || wcsstr(replyW, L"驚") || wcsstr(replyW, L"！")) {
    return EmoSurprised;
  }
  if (wcsstr(replyW, L"困") || wcsstr(replyW, L"わから") || wcsstr(replyW, L"？")) {
    return EmoTroubled;
  }
  return EmoNormal;
}

TinyLlmEngine::TinyLlmEngine()
  : m_info{},
    m_modelBuf(nullptr),
    m_v1ScalePool(nullptr),
    m_rmsAtt(nullptr),
    m_rmsFfn(nullptr),
    m_rmsFinal(nullptr),
    m_qTokens{},
    m_wq(nullptr),
    m_wk(nullptr),
    m_wv(nullptr),
    m_wo(nullptr),
    m_w1(nullptr),
    m_w2(nullptr),
    m_w3(nullptr),
    m_wcls{},
    m_sharedWeights(true),
    m_tokBuf(nullptr),
    m_tokCount(0),
    m_vocabStr(nullptr),
    m_vocabScore(nullptr),
    m_sortedLookup(nullptr),
    m_tokPromptA(3),
    m_tokPromptB(4),
    m_bosId(1),
    m_eosId(2),
    m_endId(-1),
    m_systemId(-1),
    m_userId(-1),
    m_assistantId(-1),
    m_newlineId(-1),
    m_minNormalTokId(13),
    m_rmsEps(1e-5f),
    m_ropeTheta(10000.0f),
    m_sysPrompt{},
    m_ropeCos(nullptr),
    m_ropeSin(nullptr),
    m_x(nullptr),
    m_xb(nullptr),
    m_xb2(nullptr),
    m_hb(nullptr),
    m_hb2(nullptr),
    m_q(nullptr),
    m_k(nullptr),
    m_v(nullptr),
    m_att(nullptr),
    m_logits(nullptr),
    m_useInt8Kv(false),
    m_kCacheF(nullptr),
    m_vCacheF(nullptr),
    m_kCacheQ(nullptr),
    m_vCacheQ(nullptr),
    m_kCacheS(nullptr),
    m_vCacheS(nullptr),
    m_xq(nullptr),
    m_xsQ15(nullptr),
    m_hq(nullptr),
    m_hsQ15(nullptr),
    m_qHeadQ(nullptr),
    m_attQ(nullptr),
    m_oAcc(nullptr),
    m_probBuf(nullptr),
    m_promptLen(0),
    m_prefillIdx(0),
    m_pos(0),
    m_kvPos(0),
    m_curTok(1),
    m_genCount(0),
    m_maxNewTokens(64),
    m_minNewTokens(0),
    m_temperature(0.7f),
    m_repPenalty(0.0f),
    m_active(false),
    m_hasSystemPrefilled(false),
    m_needCloseTurn(false),
    m_pendingLastTok(-1),
    m_turnCount(0),
    m_contextAutoReset(false),
    m_turnStartKvPos(0),
    m_turnStartNeedClose(false),
    m_turnStartPendingTok(-1),
    m_rng(0x9E3779B97F4A7C15ULL),
    m_recentCount(0),
    m_utf8PendLen(0),
    m_utf8Need(0) {
  for (int i = 0; i < 256; ++i) m_byteTokenIds[i] = -1;
}

TinyLlmEngine::~TinyLlmEngine() {
  unload();
}

void TinyLlmEngine::resetConversation() {
  m_active = false;
  m_promptLen = 0;
  m_prefillIdx = 0;
  m_pos = 0;
  m_kvPos = 0;
  m_genCount = 0;
  m_hasSystemPrefilled = false;
  m_needCloseTurn = false;
  m_pendingLastTok = -1;
  m_turnCount = 0;
  m_recentCount = 0;
  m_utf8PendLen = 0;
  m_utf8Need = 0;
}

void TinyLlmEngine::unload() {
  free(m_modelBuf);     m_modelBuf = nullptr;
  free(m_v1ScalePool);  m_v1ScalePool = nullptr;
  free(m_wq);           m_wq = nullptr;
  free(m_wk);           m_wk = nullptr;
  free(m_wv);           m_wv = nullptr;
  free(m_wo);           m_wo = nullptr;
  free(m_w1);           m_w1 = nullptr;
  free(m_w2);           m_w2 = nullptr;
  free(m_w3);           m_w3 = nullptr;
  free(m_tokBuf);       m_tokBuf = nullptr;
  free(m_vocabStr);     m_vocabStr = nullptr;
  free(m_vocabScore);   m_vocabScore = nullptr;
  free(m_sortedLookup); m_sortedLookup = nullptr;
  free(m_ropeCos);      m_ropeCos = nullptr;
  free(m_ropeSin);      m_ropeSin = nullptr;
  free(m_x);            m_x = nullptr;
  free(m_xb);           m_xb = nullptr;
  free(m_xb2);          m_xb2 = nullptr;
  free(m_hb);           m_hb = nullptr;
  free(m_hb2);          m_hb2 = nullptr;
  free(m_q);            m_q = nullptr;
  free(m_k);            m_k = nullptr;
  free(m_v);            m_v = nullptr;
  free(m_att);          m_att = nullptr;
  free(m_logits);       m_logits = nullptr;
  free(m_kCacheF);      m_kCacheF = nullptr;
  free(m_vCacheF);      m_vCacheF = nullptr;
  free(m_kCacheQ);      m_kCacheQ = nullptr;
  free(m_vCacheQ);      m_vCacheQ = nullptr;
  free(m_kCacheS);      m_kCacheS = nullptr;
  free(m_vCacheS);      m_vCacheS = nullptr;
  free(m_xq);           m_xq = nullptr;
  free(m_xsQ15);        m_xsQ15 = nullptr;
  free(m_hq);           m_hq = nullptr;
  free(m_hsQ15);        m_hsQ15 = nullptr;
  free(m_qHeadQ);       m_qHeadQ = nullptr;
  free(m_attQ);         m_attQ = nullptr;
  free(m_oAcc);         m_oAcc = nullptr;
  free(m_probBuf);      m_probBuf = nullptr;
  memset(&m_info, 0, sizeof(m_info));
  m_useInt8Kv = false;
  m_sysPrompt[0] = '\0';
  resetConversation();
}

bool TinyLlmEngine::load(const wchar_t * modelPath, const wchar_t * tokPath,
                         wchar_t * outErr, size_t maxErrLen) {
  unload();
  ensureExpLut();

  auto setErr = [&](const wchar_t * msg) {
    if (outErr && maxErrLen > 0) {
      wcsncpy(outErr, msg, maxErrLen - 1);
      outErr[maxErrLen - 1] = L'\0';
    }
  };

  FILE * fm = _wfopen(modelPath, L"rb");
  if (!fm) {
    setErr(L"モデルファイルを開けません");
    return false;
  }
  fseek(fm, 0, SEEK_END);
  long mSz = ftell(fm);
  fseek(fm, 0, SEEK_SET);
  if (mSz < 40 || mSz > 32L * 1024L * 1024L) {
    fclose(fm);
    setErr(L"モデルサイズ不正");
    return false;
  }
  m_modelBuf = static_cast<uint8_t *>(malloc(static_cast<size_t>(mSz)));
  if (!m_modelBuf || fread(m_modelBuf, 1, static_cast<size_t>(mSz), fm) != static_cast<size_t>(mSz)) {
    fclose(fm);
    unload();
    setErr(L"モデル読込/RAM不足");
    return false;
  }
  fclose(fm);

  const uint32_t magic = readU32LE(m_modelBuf + 0);
  const int32_t ver = readI32LE(m_modelBuf + 4);
  const int32_t dim = readI32LE(m_modelBuf + 8);
  const int32_t hdim = readI32LE(m_modelBuf + 12);
  const int32_t nLayers = readI32LE(m_modelBuf + 16);
  const int32_t nHeads = readI32LE(m_modelBuf + 20);
  const int32_t nKvHeads = readI32LE(m_modelBuf + 24);
  const int32_t vocabRaw = readI32LE(m_modelBuf + 28);
  const int32_t seqLen = readI32LE(m_modelBuf + 32);
  const int32_t gs = readI32LE(m_modelBuf + 36);

  m_sharedWeights = (vocabRaw > 0);
  const int32_t vocab = (vocabRaw < 0) ? -vocabRaw : vocabRaw;

  if (magic != kTtq8Magic || (ver != 1 && ver != 2) ||
      gs <= 0 || (gs % 4) != 0 ||
      dim <= 0 || (dim % gs) != 0 || (dim / gs) > 128 ||
      hdim <= 0 || (hdim % gs) != 0 || (hdim / gs) > 128 ||
      nLayers <= 0 || nHeads <= 0 || nKvHeads <= 0 ||
      (nHeads % nKvHeads) != 0 || (dim % nHeads) != 0 ||
      vocab <= 0 || seqLen <= 0 || seqLen > 2048) {
    unload();
    setErr(L"非対応のTTQ8ヘッダです");
    return false;
  }

  size_t off = 40;
  if (ver == 1) {
    m_info.formatVersion = 1;
    m_info.promptStyle = 0;
    m_info.flags = kFlagHasEmotionTags;
    wcsncpy(m_info.displayName, L"ずんだ 1.7M", 31);
    m_info.displayName[31] = L'\0';
    m_bosId = 1;
    m_eosId = 2;
    m_endId = -1;
    m_systemId = -1;
    m_userId = -1;
    m_assistantId = -1;
    m_newlineId = -1;
    m_minNormalTokId = 13;
    m_rmsEps = 1e-5f;
    m_ropeTheta = 10000.0f;
    m_sysPrompt[0] = '\0';
  } else {
    if (mSz < 128) {
      unload();
      setErr(L"v2ヘッダ不足");
      return false;
    }
    const uint32_t quantType = readU32LE(m_modelBuf + 40);
    const uint32_t promptStyle = readU32LE(m_modelBuf + 44);
    const uint32_t flags = readU32LE(m_modelBuf + 48);
    m_bosId = readI32LE(m_modelBuf + 52);
    m_eosId = readI32LE(m_modelBuf + 56);
    m_endId = readI32LE(m_modelBuf + 60);
    m_systemId = readI32LE(m_modelBuf + 64);
    m_userId = readI32LE(m_modelBuf + 68);
    m_assistantId = readI32LE(m_modelBuf + 72);
    m_newlineId = readI32LE(m_modelBuf + 76);
    m_rmsEps = readF32LE(m_modelBuf + 80);
    m_ropeTheta = readF32LE(m_modelBuf + 84);
    const uint32_t sysLen = readU32LE(m_modelBuf + 88);
    const uint32_t sysPaddedLen = readU32LE(m_modelBuf + 92);

    if (quantType != 0 || promptStyle > 1 ||
        sysLen > sysPaddedLen || sysPaddedLen > 255 ||
        128u + sysPaddedLen > static_cast<size_t>(mSz)) {
      unload();
      setErr(L"非対応のTTQ8 v2ヘッダです");
      return false;
    }
    if (m_rmsEps <= 0.0f) m_rmsEps = 1e-5f;
    if (m_ropeTheta <= 0.0f) m_ropeTheta = 10000.0f;

    m_info.formatVersion = 2;
    m_info.promptStyle = promptStyle;
    m_info.flags = flags;
    m_minNormalTokId = (promptStyle == 1) ? 9 : 13;

    char dispUtf8[33] = {};
    memcpy(dispUtf8, m_modelBuf + 96, 32);
    dispUtf8[32] = '\0';
    MultiByteToWideChar(CP_UTF8, 0, dispUtf8, -1, m_info.displayName, 31);
    m_info.displayName[31] = L'\0';
    if (!m_info.displayName[0]) {
      wcsncpy(m_info.displayName, L"TTQ8 v2", 31);
      m_info.displayName[31] = L'\0';
    }

    if (sysLen > 0) {
      memcpy(m_sysPrompt, m_modelBuf + 128, sysLen);
    }
    m_sysPrompt[sysLen] = '\0';
    off = 128u + static_cast<size_t>(sysPaddedLen);
  }

  m_info.dim = dim;
  m_info.hiddenDim = hdim;
  m_info.nLayers = nLayers;
  m_info.nHeads = nHeads;
  m_info.nKvHeads = nKvHeads;
  m_info.vocabSize = vocab;
  m_info.seqLen = seqLen;
  m_info.groupSize = gs;
  m_info.fileBytes = static_cast<size_t>(mSz);

  const size_t kvDim = static_cast<size_t>((dim * nKvHeads) / nHeads);
  const uint64_t layerParams =
      static_cast<uint64_t>(dim) * dim +
      static_cast<uint64_t>(kvDim) * dim * 2ULL +
      static_cast<uint64_t>(dim) * dim +
      static_cast<uint64_t>(hdim) * dim * 3ULL +
      static_cast<uint64_t>(dim) * 2ULL;
  m_info.paramCount =
      static_cast<uint64_t>(vocab) * dim +
      static_cast<uint64_t>(nLayers) * layerParams +
      static_cast<uint64_t>(dim) +
      (m_sharedWeights ? 0ULL : static_cast<uint64_t>(vocab) * dim);

  size_t rmsAttBytes = static_cast<size_t>(nLayers) * dim * sizeof(float);
  size_t rmsFinBytes = static_cast<size_t>(dim) * sizeof(float);
  if (off + rmsAttBytes * 2 + rmsFinBytes > static_cast<size_t>(mSz)) {
    unload();
    setErr(L"モデル破損 (RMS)");
    return false;
  }
  m_rmsAtt = reinterpret_cast<const float *>(m_modelBuf + off);
  off += rmsAttBytes;
  m_rmsFfn = reinterpret_cast<const float *>(m_modelBuf + off);
  off += rmsAttBytes;
  m_rmsFinal = reinterpret_cast<const float *>(m_modelBuf + off);
  off += rmsFinBytes;

  m_wq = static_cast<Q8TensorView *>(calloc(static_cast<size_t>(nLayers), sizeof(Q8TensorView)));
  m_wk = static_cast<Q8TensorView *>(calloc(static_cast<size_t>(nLayers), sizeof(Q8TensorView)));
  m_wv = static_cast<Q8TensorView *>(calloc(static_cast<size_t>(nLayers), sizeof(Q8TensorView)));
  m_wo = static_cast<Q8TensorView *>(calloc(static_cast<size_t>(nLayers), sizeof(Q8TensorView)));
  m_w1 = static_cast<Q8TensorView *>(calloc(static_cast<size_t>(nLayers), sizeof(Q8TensorView)));
  m_w2 = static_cast<Q8TensorView *>(calloc(static_cast<size_t>(nLayers), sizeof(Q8TensorView)));
  m_w3 = static_cast<Q8TensorView *>(calloc(static_cast<size_t>(nLayers), sizeof(Q8TensorView)));
  if (!m_wq || !m_wk || !m_wv || !m_wo || !m_w1 || !m_w2 || !m_w3) {
    unload();
    setErr(L"メモリ確保失敗");
    return false;
  }

  const size_t tot = static_cast<size_t>(mSz);
  bool layoutOk = true;

  if (ver == 1) {
    size_t poolBytes =
        v1ScalePoolNeed(static_cast<size_t>(vocab), static_cast<size_t>(dim), gs) +
        static_cast<size_t>(nLayers) * (
            v1ScalePoolNeed(static_cast<size_t>(dim), static_cast<size_t>(dim), gs) * 2u +
            v1ScalePoolNeed(kvDim, static_cast<size_t>(dim), gs) * 2u +
            v1ScalePoolNeed(static_cast<size_t>(hdim), static_cast<size_t>(dim), gs) * 2u +
            v1ScalePoolNeed(static_cast<size_t>(dim), static_cast<size_t>(hdim), gs));
    if (!m_sharedWeights) {
      poolBytes += v1ScalePoolNeed(static_cast<size_t>(vocab), static_cast<size_t>(dim), gs);
    }
    m_v1ScalePool = static_cast<uint8_t *>(malloc(poolBytes));
    if (!m_v1ScalePool) {
      unload();
      setErr(L"スケール変換RAM不足");
      return false;
    }
    uint8_t * poolCur = m_v1ScalePool;
    layoutOk = sliceV1Convert(m_modelBuf, tot, off, static_cast<size_t>(vocab), static_cast<size_t>(dim), gs, m_qTokens, poolCur);
    for (int l = 0; l < nLayers && layoutOk; ++l) layoutOk = sliceV1Convert(m_modelBuf, tot, off, static_cast<size_t>(dim), static_cast<size_t>(dim), gs, m_wq[l], poolCur);
    for (int l = 0; l < nLayers && layoutOk; ++l) layoutOk = sliceV1Convert(m_modelBuf, tot, off, kvDim, static_cast<size_t>(dim), gs, m_wk[l], poolCur);
    for (int l = 0; l < nLayers && layoutOk; ++l) layoutOk = sliceV1Convert(m_modelBuf, tot, off, kvDim, static_cast<size_t>(dim), gs, m_wv[l], poolCur);
    for (int l = 0; l < nLayers && layoutOk; ++l) layoutOk = sliceV1Convert(m_modelBuf, tot, off, static_cast<size_t>(dim), static_cast<size_t>(dim), gs, m_wo[l], poolCur);
    for (int l = 0; l < nLayers && layoutOk; ++l) layoutOk = sliceV1Convert(m_modelBuf, tot, off, static_cast<size_t>(hdim), static_cast<size_t>(dim), gs, m_w1[l], poolCur);
    for (int l = 0; l < nLayers && layoutOk; ++l) layoutOk = sliceV1Convert(m_modelBuf, tot, off, static_cast<size_t>(dim), static_cast<size_t>(hdim), gs, m_w2[l], poolCur);
    for (int l = 0; l < nLayers && layoutOk; ++l) layoutOk = sliceV1Convert(m_modelBuf, tot, off, static_cast<size_t>(hdim), static_cast<size_t>(dim), gs, m_w3[l], poolCur);
    if (layoutOk) {
      if (m_sharedWeights) m_wcls = m_qTokens;
      else layoutOk = sliceV1Convert(m_modelBuf, tot, off, static_cast<size_t>(vocab), static_cast<size_t>(dim), gs, m_wcls, poolCur);
    }
  } else {
    layoutOk = sliceV2Q8(m_modelBuf, tot, off, static_cast<size_t>(vocab), static_cast<size_t>(dim), gs, m_qTokens);
    for (int l = 0; l < nLayers && layoutOk; ++l) layoutOk = sliceV2Q8(m_modelBuf, tot, off, static_cast<size_t>(dim), static_cast<size_t>(dim), gs, m_wq[l]);
    for (int l = 0; l < nLayers && layoutOk; ++l) layoutOk = sliceV2Q8(m_modelBuf, tot, off, kvDim, static_cast<size_t>(dim), gs, m_wk[l]);
    for (int l = 0; l < nLayers && layoutOk; ++l) layoutOk = sliceV2Q8(m_modelBuf, tot, off, kvDim, static_cast<size_t>(dim), gs, m_wv[l]);
    for (int l = 0; l < nLayers && layoutOk; ++l) layoutOk = sliceV2Q8(m_modelBuf, tot, off, static_cast<size_t>(dim), static_cast<size_t>(dim), gs, m_wo[l]);
    for (int l = 0; l < nLayers && layoutOk; ++l) layoutOk = sliceV2Q8(m_modelBuf, tot, off, static_cast<size_t>(hdim), static_cast<size_t>(dim), gs, m_w1[l]);
    for (int l = 0; l < nLayers && layoutOk; ++l) layoutOk = sliceV2Q8(m_modelBuf, tot, off, static_cast<size_t>(dim), static_cast<size_t>(hdim), gs, m_w2[l]);
    for (int l = 0; l < nLayers && layoutOk; ++l) layoutOk = sliceV2Q8(m_modelBuf, tot, off, static_cast<size_t>(hdim), static_cast<size_t>(dim), gs, m_w3[l]);
    if (layoutOk) {
      if (m_sharedWeights) m_wcls = m_qTokens;
      else layoutOk = sliceV2Q8(m_modelBuf, tot, off, static_cast<size_t>(vocab), static_cast<size_t>(dim), gs, m_wcls);
    }
  }

  if (!layoutOk) {
    unload();
    setErr(L"Q8テンソル配置が不正です");
    return false;
  }

  // Load tokenizer binary (.tok / tok.bin)
  FILE * ft = _wfopen(tokPath, L"rb");
  if (!ft) {
    unload();
    setErr(L"トークナイザが見つかりません");
    return false;
  }
  fseek(ft, 0, SEEK_END);
  long tSz = ftell(ft);
  fseek(ft, 0, SEEK_SET);
  if (tSz < 16 || tSz > 4L * 1024L * 1024L) {
    fclose(ft);
    unload();
    setErr(L"トークナイザサイズ不正");
    return false;
  }

  uint8_t * rawTok = static_cast<uint8_t *>(malloc(static_cast<size_t>(tSz)));
  if (!rawTok || fread(rawTok, 1, static_cast<size_t>(tSz), ft) != static_cast<size_t>(tSz)) {
    free(rawTok);
    fclose(ft);
    unload();
    setErr(L"トークナイザ読込失敗");
    return false;
  }
  fclose(ft);

  m_tokCount = vocab;
  m_vocabStr = static_cast<const char **>(calloc(static_cast<size_t>(vocab), sizeof(const char *)));
  m_vocabScore = static_cast<float *>(calloc(static_cast<size_t>(vocab), sizeof(float)));
  m_sortedLookup = static_cast<VocabLookup *>(calloc(static_cast<size_t>(vocab), sizeof(VocabLookup)));
  m_tokBuf = static_cast<uint8_t *>(malloc(static_cast<size_t>(tSz) + static_cast<size_t>(vocab) + 16));
  if (!m_vocabStr || !m_vocabScore || !m_sortedLookup || !m_tokBuf) {
    free(rawTok);
    unload();
    setErr(L"トークナイザRAM不足");
    return false;
  }

  size_t rOff = 4; // skip max_token_length
  size_t poolOff = 0;
  bool tokOk = true;
  for (int i = 0; i < vocab; ++i) {
    if (rOff + 8 > static_cast<size_t>(tSz)) { tokOk = false; break; }
    m_vocabScore[i] = readF32LE(rawTok + rOff);
    int32_t slen = readI32LE(rawTok + rOff + 4);
    rOff += 8;
    if (slen < 0 || rOff + static_cast<size_t>(slen) > static_cast<size_t>(tSz)) { tokOk = false; break; }
    char * dst = reinterpret_cast<char *>(m_tokBuf + poolOff);
    memcpy(dst, rawTok + rOff, static_cast<size_t>(slen));
    dst[slen] = '\0';
    m_vocabStr[i] = dst;
    m_sortedLookup[i].str = dst;
    m_sortedLookup[i].id = i;
    rOff += static_cast<size_t>(slen);
    poolOff += static_cast<size_t>(slen) + 1u;
  }
  free(rawTok);
  if (!tokOk) {
    unload();
    setErr(L"トークナイザ解析失敗");
    return false;
  }

  qsort(m_sortedLookup, static_cast<size_t>(vocab), sizeof(VocabLookup), compareVocabStr);

  for (int b = 0; b < 256; ++b) {
    char key[8];
    snprintf(key, sizeof(key), "<0x%02X>", b);
    m_byteTokenIds[b] = findTokenId(key);
  }
  if (m_info.promptStyle == 0) {
    int idA = findTokenId("\xEF\xBC\xA1\xEF\xBC\x9A"); // UTF-8 "Ａ："
    int idB = findTokenId("\xEF\xBC\xA2\xEF\xBC\x9A"); // UTF-8 "Ｂ："
    m_tokPromptA = (idA >= 0) ? idA : 3;
    m_tokPromptB = (idB >= 0) ? idB : 4;
  } else {
    m_tokPromptA = -1;
    m_tokPromptB = -1;
  }

  // Allocate RunState & KV cache (INT8 KV for larger models like 10M/512ctx saves ~6 MB RAM)
  const int headDim = dim / nHeads;
  const int halfHead = headDim / 2;
  const size_t kvElems = static_cast<size_t>(nLayers) * static_cast<size_t>(seqLen) * kvDim;
  m_useInt8Kv = (kvElems > 262144u);

  m_ropeCos = static_cast<float *>(malloc(static_cast<size_t>(seqLen) * halfHead * sizeof(float)));
  m_ropeSin = static_cast<float *>(malloc(static_cast<size_t>(seqLen) * halfHead * sizeof(float)));
  m_x      = static_cast<float *>(calloc(static_cast<size_t>(dim), sizeof(float)));
  m_xb     = static_cast<float *>(calloc(static_cast<size_t>(dim), sizeof(float)));
  m_xb2    = static_cast<float *>(calloc(static_cast<size_t>(dim), sizeof(float)));
  m_hb     = static_cast<float *>(calloc(static_cast<size_t>(hdim), sizeof(float)));
  m_hb2    = static_cast<float *>(calloc(static_cast<size_t>(hdim), sizeof(float)));
  m_q      = static_cast<float *>(calloc(static_cast<size_t>(dim), sizeof(float)));
  m_k      = static_cast<float *>(calloc(kvDim, sizeof(float)));
  m_v      = static_cast<float *>(calloc(kvDim, sizeof(float)));
  m_att    = static_cast<float *>(calloc(static_cast<size_t>(nHeads) * seqLen, sizeof(float)));
  m_logits = static_cast<float *>(calloc(static_cast<size_t>(vocab), sizeof(float)));
  m_xq     = static_cast<int8_t *>(calloc(static_cast<size_t>(dim), sizeof(int8_t)));
  m_xsQ15  = static_cast<uint16_t *>(calloc(static_cast<size_t>(dim / gs), sizeof(uint16_t)));
  m_hq     = static_cast<int8_t *>(calloc(static_cast<size_t>(hdim), sizeof(int8_t)));
  m_hsQ15  = static_cast<uint16_t *>(calloc(static_cast<size_t>(hdim / gs), sizeof(uint16_t)));
  m_probBuf = static_cast<ProbItem *>(calloc(static_cast<size_t>(vocab), sizeof(ProbItem)));

  bool kvOk = false;
  if (m_useInt8Kv) {
    const size_t kvHeadsTotal = static_cast<size_t>(nLayers) * static_cast<size_t>(seqLen) * static_cast<size_t>(nKvHeads);
    m_kCacheQ = static_cast<int8_t *>(calloc(kvElems, sizeof(int8_t)));
    m_vCacheQ = static_cast<int8_t *>(calloc(kvElems, sizeof(int8_t)));
    m_kCacheS = static_cast<float *>(calloc(kvHeadsTotal, sizeof(float)));
    m_vCacheS = static_cast<float *>(calloc(kvHeadsTotal, sizeof(float)));
    m_qHeadQ  = static_cast<int8_t *>(calloc(static_cast<size_t>(headDim), sizeof(int8_t)));
    m_attQ    = static_cast<int16_t *>(calloc(static_cast<size_t>(seqLen), sizeof(int16_t)));
    m_oAcc    = static_cast<int32_t *>(calloc(static_cast<size_t>(headDim), sizeof(int32_t)));
    kvOk = (m_kCacheQ && m_vCacheQ && m_kCacheS && m_vCacheS && m_qHeadQ && m_attQ && m_oAcc);
  } else {
    m_kCacheF = static_cast<float *>(calloc(kvElems, sizeof(float)));
    m_vCacheF = static_cast<float *>(calloc(kvElems, sizeof(float)));
    kvOk = (m_kCacheF && m_vCacheF);
  }

  if (!m_ropeCos || !m_ropeSin || !m_x || !m_xb || !m_xb2 || !m_hb || !m_hb2 ||
      !m_q || !m_k || !m_v || !m_att || !m_logits || !kvOk ||
      !m_xq || !m_xsQ15 || !m_hq || !m_hsQ15 || !m_probBuf) {
    unload();
    setErr(L"KVキャッシュ用RAM不足");
    return false;
  }

  const double theta = static_cast<double>(m_ropeTheta);
  for (int p = 0; p < seqLen; ++p) {
    for (int i = 0; i < halfHead; ++i) {
      double freq = 1.0 / pow(theta, static_cast<double>(i * 2) / static_cast<double>(headDim));
      double ang = static_cast<double>(p) * freq;
      m_ropeCos[p * halfHead + i] = static_cast<float>(cos(ang));
      m_ropeSin[p * halfHead + i] = static_cast<float>(sin(ang));
    }
  }

  m_info.loaded = true;
  resetConversation();
  return true;
}

int TinyLlmEngine::findTokenId(const char * utf8Str) const {
  if (!m_sortedLookup || !utf8Str) return -1;
  int lo = 0;
  int hi = m_tokCount - 1;
  while (lo <= hi) {
    int mid = lo + (hi - lo) / 2;
    int cmp = strcmp(m_sortedLookup[mid].str, utf8Str);
    if (cmp == 0) return m_sortedLookup[mid].id;
    if (cmp < 0) lo = mid + 1;
    else hi = mid - 1;
  }
  return -1;
}

int TinyLlmEngine::encodeLineUtf8(const char * lineUtf8, int * outTokens, int maxTokens) const {
  if (!m_info.loaded || !lineUtf8 || !outTokens || maxTokens < 1) return 0;

  const bool escapeSpaces = (m_info.flags & kFlagEscapeSpaces) != 0;
  const size_t len = strlen(lineUtf8);
  int n = 0;
  size_t i = 0;
  char chBuf[8];

  while (i < len && n < maxTokens) {
    int clen = 1;
    if (escapeSpaces && lineUtf8[i] == ' ') {
      chBuf[0] = '\xE2';
      chBuf[1] = '\x96';
      chBuf[2] = '\x81';
      chBuf[3] = '\0';
    } else {
      clen = utf8CharByteLen(static_cast<uint8_t>(lineUtf8[i]));
      if (i + static_cast<size_t>(clen) > len) clen = 1;
      memcpy(chBuf, lineUtf8 + i, static_cast<size_t>(clen));
      chBuf[clen] = '\0';
    }

    int tid = findTokenId(chBuf);
    if (tid >= 0) {
      outTokens[n++] = tid;
    } else {
      int byteLen = static_cast<int>(strlen(chBuf));
      for (int b = 0; b < byteLen && n < maxTokens; ++b) {
        int bid = m_byteTokenIds[static_cast<uint8_t>(chBuf[b])];
        if (bid >= 0) outTokens[n++] = bid;
      }
    }
    i += static_cast<size_t>(clen);
  }

  // BPE merges on the encoded piece sequence
  char merged[128];
  for (;;) {
    float bestScore = -1e30f;
    int bestId = -1;
    int bestIdx = -1;
    for (int k = 0; k + 1 < n; ++k) {
      if (outTokens[k] < m_minNormalTokId || outTokens[k + 1] < m_minNormalTokId) continue;
      const char * s0 = m_vocabStr[outTokens[k]];
      const char * s1 = m_vocabStr[outTokens[k + 1]];
      size_t l0 = strlen(s0), l1 = strlen(s1);
      if (l0 + l1 >= sizeof(merged)) continue;
      memcpy(merged, s0, l0);
      memcpy(merged + l0, s1, l1 + 1);
      int tid = findTokenId(merged);
      if (tid >= 0 && m_vocabScore[tid] > bestScore) {
        bestScore = m_vocabScore[tid];
        bestId = tid;
        bestIdx = k;
      }
    }
    if (bestIdx < 0) break;
    outTokens[bestIdx] = bestId;
    memmove(outTokens + bestIdx + 1, outTokens + bestIdx + 2,
            static_cast<size_t>(n - bestIdx - 2) * sizeof(int));
    --n;
  }

  return n;
}

int TinyLlmEngine::encodeTextUtf8(const char * textUtf8, int * outTokens, int maxTokens) const {
  if (!m_info.loaded || !textUtf8 || !outTokens || maxTokens < 1) return 0;

  if (m_info.promptStyle == 0 || m_newlineId < 0) {
    return encodeLineUtf8(textUtf8, outTokens, maxTokens);
  }

  int n = 0;
  const char * cur = textUtf8;
  bool first = true;
  while (n < maxTokens) {
    if (!first) {
      outTokens[n++] = m_newlineId;
      if (n >= maxTokens) break;
    }
    first = false;

    const char * nl = strchr(cur, '\n');
    if (!nl) {
      n += encodeLineUtf8(cur, outTokens + n, maxTokens - n);
      break;
    }

    size_t segLen = static_cast<size_t>(nl - cur);
    char lineBuf[256];
    if (segLen >= sizeof(lineBuf)) segLen = sizeof(lineBuf) - 1;
    memcpy(lineBuf, cur, segLen);
    lineBuf[segLen] = '\0';
    n += encodeLineUtf8(lineBuf, outTokens + n, maxTokens - n);
    cur = nl + 1;
  }
  return n;
}

void TinyLlmEngine::decodeTokenBytes(int tok, wchar_t * outW, size_t maxW) {
  if (!outW || maxW == 0) return;
  outW[0] = L'\0';
  if (m_info.promptStyle == 1 && tok == m_newlineId) {
    m_utf8PendLen = 0;
    m_utf8Need = 0;
    wcsncpy(outW, L"\n", maxW - 1);
    outW[maxW - 1] = L'\0';
    return;
  }
  if (tok < m_minNormalTokId || tok >= m_tokCount) return;

  const char * piece = m_vocabStr[tok];
  // Check SentencePiece <0xXX> byte token
  if (piece[0] == '<' && piece[1] == '0' && piece[2] == 'x' &&
      piece[5] == '>' && piece[6] == '\0') {
    auto hexVal = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      return -1;
    };
    int hi = hexVal(piece[3]);
    int lo = hexVal(piece[4]);
    if (hi >= 0 && lo >= 0) {
      uint8_t byteVal = static_cast<uint8_t>((hi << 4) | lo);
      if (m_utf8PendLen == 0) {
        m_utf8Need = utf8CharByteLen(byteVal);
      }
      if (m_utf8PendLen < 4) {
        m_utf8Pend[m_utf8PendLen++] = byteVal;
      }
      if (m_utf8PendLen >= m_utf8Need) {
        char tmp[8] = {};
        memcpy(tmp, m_utf8Pend, static_cast<size_t>(m_utf8PendLen));
        m_utf8PendLen = 0;
        m_utf8Need = 0;
        MultiByteToWideChar(CP_UTF8, 0, tmp, -1, outW, static_cast<int>(maxW));
        outW[maxW - 1] = L'\0';
      }
      return;
    }
  }

  m_utf8PendLen = 0;
  m_utf8Need = 0;

  if (m_info.flags & kFlagEscapeSpaces) {
    char clean[128];
    size_t dst = 0;
    for (size_t i = 0; piece[i] != '\0' && dst + 1 < sizeof(clean); ) {
      if (static_cast<uint8_t>(piece[i]) == 0xE2 &&
          static_cast<uint8_t>(piece[i + 1]) == 0x96 &&
          static_cast<uint8_t>(piece[i + 2]) == 0x81) {
        clean[dst++] = ' ';
        i += 3;
      } else {
        clean[dst++] = piece[i++];
      }
    }
    clean[dst] = '\0';
    MultiByteToWideChar(CP_UTF8, 0, clean, -1, outW, static_cast<int>(maxW));
  } else {
    MultiByteToWideChar(CP_UTF8, 0, piece, -1, outW, static_cast<int>(maxW));
  }
  outW[maxW - 1] = L'\0';
}

void TinyLlmEngine::forwardToken(int token, int pos, bool needLogits) {
  const int dim = m_info.dim;
  const int hdim = m_info.hiddenDim;
  const int gs = m_info.groupSize;
  const int headDim = dim / m_info.nHeads;
  const int halfHead = headDim / 2;
  const int kvDim = (dim * m_info.nKvHeads) / m_info.nHeads;
  const int kvMul = m_info.nHeads / m_info.nKvHeads;

  dequantizeRowQ15(m_x, m_qTokens, token, dim, gs);

  const float * ropeC = m_ropeCos + pos * halfHead;
  const float * ropeS = m_ropeSin + pos * halfHead;
  const float invSqrtHead = 1.0f / sqrtf(static_cast<float>(headDim));

  for (int l = 0; l < m_info.nLayers; ++l) {
    rmsnorm(m_xb, m_x, m_rmsAtt + l * dim, dim, m_rmsEps);
    float xFactor = 0.0f;
    quantizeVecQ15(m_xb, m_xq, m_xsQ15, xFactor, dim, gs);

    matmulQ8Int(m_q, m_xq, m_xsQ15, xFactor, m_wq[l], dim, dim, gs);
    matmulQ8Int(m_k, m_xq, m_xsQ15, xFactor, m_wk[l], dim, kvDim, gs);
    matmulQ8Int(m_v, m_xq, m_xsQ15, xFactor, m_wv[l], dim, kvDim, gs);

    // Adjacent-pair RoPE (matching both llama2.c and JapaneseLM half_split/pair_rot)
    for (int i = 0; i < dim; i += 2) {
      int pairIdx = (i % headDim) >> 1;
      float fc = ropeC[pairIdx];
      float fs = ropeS[pairIdx];
      float q0 = m_q[i];
      float q1 = m_q[i + 1];
      m_q[i]     = q0 * fc - q1 * fs;
      m_q[i + 1] = q0 * fs + q1 * fc;
      if (i < kvDim) {
        float k0 = m_k[i];
        float k1 = m_k[i + 1];
        m_k[i]     = k0 * fc - k1 * fs;
        m_k[i + 1] = k0 * fs + k1 * fc;
      }
    }

    if (m_useInt8Kv) {
      const size_t cacheVecOff = static_cast<size_t>(l) * m_info.seqLen * kvDim + static_cast<size_t>(pos) * kvDim;
      const size_t cacheHeadOff = static_cast<size_t>(l) * m_info.seqLen * m_info.nKvHeads + static_cast<size_t>(pos) * m_info.nKvHeads;
      for (int kh = 0; kh < m_info.nKvHeads; ++kh) {
        const float * kp = m_k + kh * headDim;
        const float * vp = m_v + kh * headDim;
        float km = 0.0f, vm = 0.0f;
        for (int j = 0; j < headDim; ++j) {
          float ak = kp[j]; if (ak < 0.0f) ak = -ak; if (ak > km) km = ak;
          float av = vp[j]; if (av < 0.0f) av = -av; if (av > vm) vm = av;
        }
        float ks = km / 127.0f;
        float vs = vm / 127.0f;
        m_kCacheS[cacheHeadOff + kh] = ks;
        m_vCacheS[cacheHeadOff + kh] = vs;
        float ki = (km > 1e-12f) ? (127.0f / km) : 0.0f;
        float vi = (vm > 1e-12f) ? (127.0f / vm) : 0.0f;
        int8_t * kd = m_kCacheQ + cacheVecOff + kh * headDim;
        int8_t * vd = m_vCacheQ + cacheVecOff + kh * headDim;
        for (int j = 0; j < headDim; ++j) {
          kd[j] = quantClampI8(kp[j], ki);
          vd[j] = quantClampI8(vp[j], vi);
        }
      }

      const int8_t * kLayerQ = m_kCacheQ + static_cast<size_t>(l) * m_info.seqLen * kvDim;
      const int8_t * vLayerQ = m_vCacheQ + static_cast<size_t>(l) * m_info.seqLen * kvDim;
      const float * kLayerS = m_kCacheS + static_cast<size_t>(l) * m_info.seqLen * m_info.nKvHeads;
      const float * vLayerS = m_vCacheS + static_cast<size_t>(l) * m_info.seqLen * m_info.nKvHeads;

      for (int h = 0; h < m_info.nHeads; ++h) {
        const float * qh = m_q + h * headDim;
        float qm = 0.0f;
        for (int j = 0; j < headDim; ++j) {
          float aq = qh[j];
          if (aq < 0.0f) aq = -aq;
          if (aq > qm) qm = aq;
        }
        float qScale = qm / 127.0f;
        float qi = (qm > 1e-12f) ? (127.0f / qm) : 0.0f;
        for (int j = 0; j < headDim; ++j) {
          m_qHeadQ[j] = quantClampI8(qh[j], qi);
        }

        const int kvHead = h / kvMul;
        const int kvOff = kvHead * headDim;
        const float qInv = qScale * invSqrtHead;
        float * atth = m_att + h * m_info.seqLen;
        for (int t = 0; t <= pos; ++t) {
          const int8_t * kh = kLayerQ + static_cast<size_t>(t) * kvDim + kvOff;
          int32_t dot = 0;
          for (int j = 0; j < headDim; j += 4) {
            dot += static_cast<int32_t>(m_qHeadQ[j])     * static_cast<int32_t>(kh[j])
                 + static_cast<int32_t>(m_qHeadQ[j + 1]) * static_cast<int32_t>(kh[j + 1])
                 + static_cast<int32_t>(m_qHeadQ[j + 2]) * static_cast<int32_t>(kh[j + 2])
                 + static_cast<int32_t>(m_qHeadQ[j + 3]) * static_cast<int32_t>(kh[j + 3]);
          }
          atth[t] = static_cast<float>(dot) * (qInv * kLayerS[t * m_info.nKvHeads + kvHead]);
        }
        softmaxFast(atth, pos + 1);

        // Quantize weighted softmax * vScale into Q13 integer weights for integer V accumulation
        float aMax = 0.0f;
        for (int t = 0; t <= pos; ++t) {
          float aw = atth[t] * vLayerS[t * m_info.nKvHeads + kvHead];
          atth[t] = aw;
          if (aw > aMax) aMax = aw;
        }
        float aInv = (aMax > 1e-12f) ? (8191.0f / aMax) : 0.0f;
        for (int t = 0; t <= pos; ++t) {
          int qw = static_cast<int>(atth[t] * aInv + 0.5f);
          if (qw < 0) qw = 0;
          if (qw > 8191) qw = 8191;
          m_attQ[t] = static_cast<int16_t>(qw);
        }

        memset(m_oAcc, 0, static_cast<size_t>(headDim) * sizeof(int32_t));
        for (int t = 0; t <= pos; ++t) {
          int32_t aw = m_attQ[t];
          if (aw == 0) continue;
          const int8_t * vh = vLayerQ + static_cast<size_t>(t) * kvDim + kvOff;
          for (int j = 0; j < headDim; j += 4) {
            m_oAcc[j]     += aw * static_cast<int32_t>(vh[j]);
            m_oAcc[j + 1] += aw * static_cast<int32_t>(vh[j + 1]);
            m_oAcc[j + 2] += aw * static_cast<int32_t>(vh[j + 2]);
            m_oAcc[j + 3] += aw * static_cast<int32_t>(vh[j + 3]);
          }
        }
        float * xbh = m_xb2 + h * headDim;
        const float oScale = aMax * (1.0f / 8191.0f);
        for (int j = 0; j < headDim; ++j) {
          xbh[j] = static_cast<float>(m_oAcc[j]) * oScale;
        }
      }
    } else {
      const size_t cacheOff = static_cast<size_t>(l) * m_info.seqLen * kvDim + static_cast<size_t>(pos) * kvDim;
      memcpy(m_kCacheF + cacheOff, m_k, static_cast<size_t>(kvDim) * sizeof(float));
      memcpy(m_vCacheF + cacheOff, m_v, static_cast<size_t>(kvDim) * sizeof(float));

      const float * kLayer = m_kCacheF + static_cast<size_t>(l) * m_info.seqLen * kvDim;
      const float * vLayer = m_vCacheF + static_cast<size_t>(l) * m_info.seqLen * kvDim;

      for (int h = 0; h < m_info.nHeads; ++h) {
        const float * qh = m_q + h * headDim;
        float * atth = m_att + h * m_info.seqLen;
        int kvOff = (h / kvMul) * headDim;
        for (int t = 0; t <= pos; ++t) {
          const float * kh = kLayer + t * kvDim + kvOff;
          float score = 0.0f;
          for (int j = 0; j < headDim; ++j) {
            score += qh[j] * kh[j];
          }
          atth[t] = score * invSqrtHead;
        }
        softmaxFast(atth, pos + 1);

        float * xbh = m_xb2 + h * headDim;
        memset(xbh, 0, static_cast<size_t>(headDim) * sizeof(float));
        for (int t = 0; t <= pos; ++t) {
          const float * vh = vLayer + t * kvDim + kvOff;
          float a = atth[t];
          for (int j = 0; j < headDim; ++j) {
            xbh[j] += a * vh[j];
          }
        }
      }
    }

    quantizeVecQ15(m_xb2, m_xq, m_xsQ15, xFactor, dim, gs);
    matmulQ8Int(m_xb, m_xq, m_xsQ15, xFactor, m_wo[l], dim, dim, gs);
    for (int i = 0; i < dim; ++i) m_x[i] += m_xb[i];

    // SwiGLU FFN with LUT silu
    rmsnorm(m_xb, m_x, m_rmsFfn + l * dim, dim, m_rmsEps);
    quantizeVecQ15(m_xb, m_xq, m_xsQ15, xFactor, dim, gs);
    matmulQ8Int(m_hb, m_xq, m_xsQ15, xFactor, m_w1[l], dim, hdim, gs);
    matmulQ8Int(m_hb2, m_xq, m_xsQ15, xFactor, m_w3[l], dim, hdim, gs);
    for (int i = 0; i < hdim; ++i) {
      m_hb[i] = fastSilu(m_hb[i]) * m_hb2[i];
    }

    float hFactor = 0.0f;
    quantizeVecQ15(m_hb, m_hq, m_hsQ15, hFactor, hdim, gs);
    matmulQ8Int(m_xb, m_hq, m_hsQ15, hFactor, m_w2[l], hdim, dim, gs);
    for (int i = 0; i < dim; ++i) m_x[i] += m_xb[i];
  }

  if (needLogits) {
    rmsnorm(m_x, m_x, m_rmsFinal, dim, m_rmsEps);
    float xFactor = 0.0f;
    quantizeVecQ15(m_x, m_xq, m_xsQ15, xFactor, dim, gs);
    matmulQ8Int(m_logits, m_xq, m_xsQ15, xFactor, m_wcls, dim, m_info.vocabSize, gs);
  }
}

int TinyLlmEngine::sampleLogit(float temperature) {
  const int V = m_info.vocabSize;
  const float rp = (m_repPenalty > 1.0f)
                       ? m_repPenalty
                       : ((m_info.flags & kFlagSuppressCtrlTokens) ? 1.15f : 1.12f);

  if (m_genCount < m_minNewTokens) {
    if (m_info.promptStyle == 0) {
      for (int s = 2; s <= 12 && s < V; ++s) m_logits[s] = -1e9f;
    } else {
      if (m_eosId >= 0 && m_eosId < V) m_logits[m_eosId] = -1e9f;
      if (m_endId >= 0 && m_endId < V) m_logits[m_endId] = -1e9f;
    }
  }

  if (m_info.flags & kFlagSuppressCtrlTokens) {
    if (V > 7) {
      m_logits[0] = -1e9f;
      m_logits[1] = -1e9f;
      m_logits[2] = -1e9f;
      m_logits[4] = -1e9f;
      m_logits[5] = -1e9f;
      m_logits[6] = -1e9f;
    }
    // Apply repetition penalty before both greedy and sampling on Akebono
    for (int r = 0; r < m_recentCount; ++r) {
      int rt = m_recentToks[r];
      if (rt >= m_minNormalTokId && rt < V) {
        m_logits[rt] = (m_logits[rt] < 0.0f) ? (m_logits[rt] * rp) : (m_logits[rt] / rp);
      }
    }
  } else {
    // Style 0 (Zundamon): suppress <unk>, BOS
    m_logits[0] = -1e9f;
    m_logits[1] = -1e9f;
  }

  if (temperature <= 0.01f) {
    int best = (m_eosId >= 0 && m_eosId < V) ? m_eosId : 2;
    for (int i = 0; i < V; ++i) {
      if (m_logits[i] > m_logits[best]) best = i;
    }
    return best;
  }

  if (!(m_info.flags & kFlagSuppressCtrlTokens)) {
    for (int r = 0; r < m_recentCount; ++r) {
      int rt = m_recentToks[r];
      if (rt >= m_minNormalTokId && rt < V) {
        m_logits[rt] = (m_logits[rt] < 0.0f) ? (m_logits[rt] * rp) : (m_logits[rt] / rp);
      }
    }
  }

  for (int i = 0; i < V; ++i) {
    m_logits[i] /= temperature;
  }
  softmaxFast(m_logits, V);

  // Top-P (0.90) nucleus sampling
  int nCand = 0;
  const float cutoff = (1.0f - 0.90f) / static_cast<float>(V);
  const int startIdx = (m_info.promptStyle == 1) ? 3 : 2;
  for (int i = startIdx; i < V; ++i) {
    if (m_logits[i] >= cutoff) {
      m_probBuf[nCand].p = m_logits[i];
      m_probBuf[nCand].idx = i;
      ++nCand;
    }
  }
  if (nCand == 0) {
    int best = startIdx;
    for (int i = startIdx + 1; i < V; ++i) {
      if (m_logits[i] > m_logits[best]) best = i;
    }
    return best;
  }

  qsort(m_probBuf, static_cast<size_t>(nCand), sizeof(ProbItem), compareProbDesc);

  float cum = 0.0f;
  int lastIdx = nCand - 1;
  for (int i = 0; i < nCand; ++i) {
    cum += m_probBuf[i].p;
    if (cum > 0.90f) {
      lastIdx = i;
      break;
    }
  }

  m_rng ^= m_rng >> 12;
  m_rng ^= m_rng << 25;
  m_rng ^= m_rng >> 27;
  uint32_t rnd = static_cast<uint32_t>((m_rng * 0x2545F4914F6CDD1DULL) >> 40);
  float coin = (static_cast<float>(rnd) / 16777216.0f) * cum;

  float cdf = 0.0f;
  for (int i = 0; i <= lastIdx; ++i) {
    cdf += m_probBuf[i].p;
    if (coin < cdf) return m_probBuf[i].idx;
  }
  return m_probBuf[lastIdx].idx;
}

bool TinyLlmEngine::beginPrompt(const wchar_t * userTextW, float temperature, int maxNewTokens,
                                int minNewTokens, float repPenalty) {
  if (!m_info.loaded || !userTextW || !userTextW[0]) return false;

  char utf8[512] = {};
  WideCharToMultiByte(CP_UTF8, 0, userTextW, -1, utf8, sizeof(utf8) - 1, NULL, NULL);
  utf8[sizeof(utf8) - 1] = '\0';

  m_contextAutoReset = false;

  if (m_info.promptStyle == 0) {
    int n = 0;
    m_promptTokens[n++] = m_bosId;
    m_promptTokens[n++] = m_tokPromptA;
    n += encodeTextUtf8(utf8, m_promptTokens + n, kMaxPromptTokens - 4);
    m_promptTokens[n++] = m_tokPromptB;
    m_promptLen = n;
    m_kvPos = 0;
    m_pos = 0;
    m_prefillIdx = 0;
    m_curTok = m_promptTokens[0];
  } else {
    int userToks[160];
    const int uLen = encodeTextUtf8(utf8, userToks, 152);
    if (uLen <= 0) return false;

    int sysToks[96];
    int sLen = 0;
    if (!m_hasSystemPrefilled && m_sysPrompt[0]) {
      sLen = encodeTextUtf8(m_sysPrompt, sysToks, 92);
    }
    int overhead = m_hasSystemPrefilled
                       ? ((m_pendingLastTok >= 0 ? 1 : 0) + (m_needCloseTurn ? 1 : 0) + 3)
                       : (5 + sLen);
    if (m_kvPos + overhead + uLen + 24 >= m_info.seqLen) {
      resetConversation();
      m_contextAutoReset = true;
      sLen = m_sysPrompt[0] ? encodeTextUtf8(m_sysPrompt, sysToks, 92) : 0;
      overhead = 5 + sLen;
    }

    m_turnStartKvPos = m_kvPos;
    m_turnStartNeedClose = m_needCloseTurn;
    m_turnStartPendingTok = m_pendingLastTok;

    int n = 0;
    if (!m_hasSystemPrefilled) {
      m_promptTokens[n++] = m_bosId;
      m_promptTokens[n++] = m_systemId;
      for (int i = 0; i < sLen && n + 8 < kMaxPromptTokens; ++i) {
        m_promptTokens[n++] = sysToks[i];
      }
      m_promptTokens[n++] = m_endId;
      m_hasSystemPrefilled = true;
    } else {
      if (m_pendingLastTok >= 0) {
        m_promptTokens[n++] = m_pendingLastTok;
        m_pendingLastTok = -1;
      }
      if (m_needCloseTurn) {
        m_promptTokens[n++] = m_endId;
        m_needCloseTurn = false;
      }
    }

    m_promptTokens[n++] = m_userId;
    int maxUserCopy = kMaxPromptTokens - n - 3;
    int availCtx = m_info.seqLen - m_kvPos - n - 12;
    if (availCtx < maxUserCopy) maxUserCopy = availCtx;
    if (maxUserCopy < 1) return false;
    int copyU = (uLen < maxUserCopy) ? uLen : maxUserCopy;
    for (int i = 0; i < copyU; ++i) {
      m_promptTokens[n++] = userToks[i];
    }
    m_promptTokens[n++] = m_endId;
    m_promptTokens[n++] = m_assistantId;

    m_promptLen = n;
    m_prefillIdx = 0;
    m_pos = m_kvPos;
    m_curTok = m_promptTokens[0];
  }

  m_genCount = 0;
  int room = m_info.seqLen - m_kvPos - m_promptLen - 2;
  m_maxNewTokens = (maxNewTokens > 1 && maxNewTokens < room) ? maxNewTokens : room;
  if (m_maxNewTokens < 1) m_maxNewTokens = 1;
  m_minNewTokens = (m_maxNewTokens > 1 && minNewTokens > 0)
                       ? ((minNewTokens < m_maxNewTokens) ? minNewTokens : (m_maxNewTokens - 1))
                       : 0;
  m_temperature = temperature;
  m_repPenalty = repPenalty;
  m_recentCount = 0;
  m_utf8PendLen = 0;
  m_utf8Need = 0;
  m_rng = (static_cast<uint64_t>(GetTickCount()) << 24) ^ 0xA5A5F0F012345679ULL;
  if (m_rng == 0) m_rng = 0x9E3779B97F4A7C15ULL;
  m_active = true;
  return true;
}

bool TinyLlmEngine::stepToken(wchar_t * outDeltaW, size_t maxDeltaW,
                              bool & outGeneratingText, int & outEmotionIdx, bool & outDone) {
  if (outDeltaW && maxDeltaW > 0) outDeltaW[0] = L'\0';
  outGeneratingText = false;
  outEmotionIdx = -1;
  outDone = false;

  if (!m_active || !m_info.loaded) {
    outDone = true;
    return false;
  }
  if (m_pos >= m_info.seqLen - 2 || m_genCount >= m_maxNewTokens) {
    m_active = false;
    if (isMultiTurn()) {
      m_needCloseTurn = true;
      ++m_turnCount;
    }
    outDone = true;
    return true;
  }

  const bool isPrefillStep = (m_prefillIdx + 1 < m_promptLen);
  forwardToken(m_curTok, m_pos, !isPrefillStep);
  ++m_pos;
  m_kvPos = m_pos;

  if (isPrefillStep) {
    ++m_prefillIdx;
    m_curTok = m_promptTokens[m_prefillIdx];
    return false;
  }

  m_prefillIdx = m_promptLen;
  m_pendingLastTok = -1;

  outGeneratingText = true;
  int nextTok = sampleLogit(m_temperature);

  if (m_info.promptStyle == 0) {
    m_curTok = nextTok;
    ++m_genCount;
    if (nextTok == m_eosId || nextTok == m_tokPromptA || nextTok == m_tokPromptB) {
      m_active = false;
      outDone = true;
      return true;
    }
    if ((m_info.flags & kFlagHasEmotionTags) && nextTok >= 5 && nextTok <= 12) {
      outEmotionIdx = nextTok - 5;
      m_active = false;
      outDone = true;
      return true;
    }
  } else {
    if (nextTok == m_endId || nextTok == m_eosId) {
      m_needCloseTurn = true;
      ++m_turnCount;
      m_active = false;
      outDone = true;
      return true;
    }
    m_curTok = nextTok;
    m_pendingLastTok = nextTok;
    ++m_genCount;
  }

  if (m_recentCount < kRepeatWin) {
    m_recentToks[m_recentCount++] = nextTok;
  } else {
    memmove(m_recentToks, m_recentToks + 1, sizeof(int) * (kRepeatWin - 1));
    m_recentToks[kRepeatWin - 1] = nextTok;
  }

  decodeTokenBytes(nextTok, outDeltaW, maxDeltaW);

  if (m_pos >= m_info.seqLen - 2 || m_genCount >= m_maxNewTokens) {
    m_active = false;
    if (isMultiTurn()) {
      m_needCloseTurn = true;
      ++m_turnCount;
    }
    outDone = true;
  }
  return true;
}

void TinyLlmEngine::abortGeneration() {
  if (!m_active) return;
  m_active = false;
  if (isMultiTurn()) {
    if (m_prefillIdx < m_promptLen) {
      // Aborted before generation started: rewind KV cursor cleanly
      m_kvPos = m_turnStartKvPos;
      m_pos = m_turnStartKvPos;
      m_needCloseTurn = m_turnStartNeedClose;
      m_pendingLastTok = m_turnStartPendingTok;
      if (m_turnStartKvPos == 0) m_hasSystemPrefilled = false;
    } else {
      m_needCloseTurn = true;
      ++m_turnCount;
    }
  }
}

} // namespace LiliumLLM
