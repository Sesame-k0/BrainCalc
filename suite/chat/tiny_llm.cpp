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

static void rmsnorm(float * out, const float * x, const float * w, int size) {
  float ss = 0.0f;
  for (int j = 0; j < size; ++j) {
    ss += x[j] * x[j];
  }
  ss /= static_cast<float>(size);
  ss += 1e-5f;
  float inv = 1.0f / sqrtf(ss);
  for (int j = 0; j < size; ++j) {
    out[j] = w[j] * (inv * x[j]);
  }
}

static void dequantizeRow(float * out, const Q8TensorView & t, int row, int rowLen, int gs) {
  const int8_t * q = t.q + static_cast<size_t>(row) * static_cast<size_t>(rowLen);
  const float * s = t.s + (static_cast<size_t>(row) * static_cast<size_t>(rowLen)) / static_cast<size_t>(gs);
  const int groups = rowLen / gs;
  for (int g = 0; g < groups; ++g) {
    float sc = s[g];
    const int8_t * qp = q + g * gs;
    float * op = out + g * gs;
    for (int k = 0; k < gs; ++k) {
      op[k] = static_cast<float>(qp[k]) * sc;
    }
  }
}

static void quantizeVec(const float * x, int8_t * xq, float * xs, int n, int gs) {
  const int groups = n / gs;
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
    xs[g] = d;
    int8_t * qp = xq + g * gs;
    for (int k = 0; k < gs; ++k) {
      float scaled = xp[k] * id;
      int qi = (scaled >= 0.0f) ? static_cast<int>(scaled + 0.5f) : static_cast<int>(scaled - 0.5f);
      if (qi > 127) qi = 127;
      if (qi < -127) qi = -127;
      qp[k] = static_cast<int8_t>(qi);
    }
  }
}

// Pure 32-bit integer MAC inner loop across each 32-element group (fast without hardware FPU)
static void matmulQ8(float * out, const int8_t * xq, const float * xs,
                     const Q8TensorView & w, int nIn, int nOut, int gs) {
  const int groups = nIn / gs;
  for (int i = 0; i < nOut; ++i) {
    const int8_t * wq = w.q + static_cast<size_t>(i) * static_cast<size_t>(nIn);
    const float * ws = w.s + (static_cast<size_t>(i) * static_cast<size_t>(nIn)) / static_cast<size_t>(gs);
    float acc = 0.0f;
    for (int g = 0; g < groups; ++g) {
      const int8_t * xgp = xq + g * gs;
      const int8_t * wgp = wq + g * gs;
      int32_t ival = 0;
      for (int k = 0; k < gs; k += 4) {
        ival += static_cast<int32_t>(xgp[k])     * static_cast<int32_t>(wgp[k])
              + static_cast<int32_t>(xgp[k + 1]) * static_cast<int32_t>(wgp[k + 1])
              + static_cast<int32_t>(xgp[k + 2]) * static_cast<int32_t>(wgp[k + 2])
              + static_cast<int32_t>(xgp[k + 3]) * static_cast<int32_t>(wgp[k + 3]);
      }
      acc += static_cast<float>(ival) * (xs[g] * ws[g]);
    }
    out[i] = acc;
  }
}

static void softmax(float * x, int size) {
  if (size <= 0) return;
  float mx = x[0];
  for (int i = 1; i < size; ++i) {
    if (x[i] > mx) mx = x[i];
  }
  float sum = 0.0f;
  for (int i = 0; i < size; ++i) {
    x[i] = expf(x[i] - mx);
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

static bool sliceQ8(const uint8_t * base, size_t totalBytes, size_t & ioOff,
                    size_t elemCount, int gs, Q8TensorView & out) {
  if (elemCount % static_cast<size_t>(gs) != 0) return false;
  size_t sCount = elemCount / static_cast<size_t>(gs);
  size_t need = elemCount + sCount * sizeof(float);
  if (ioOff + need > totalBytes) return false;
  out.q = reinterpret_cast<const int8_t *>(base + ioOff);
  ioOff += elemCount;
  out.s = reinterpret_cast<const float *>(base + ioOff);
  ioOff += sCount * sizeof(float);
  return true;
}

} // namespace

const wchar_t * emotionLabel(int emoIdx) {
  if (emoIdx < 0 || emoIdx >= kNumEmotions) return kEmotionNames[0];
  return kEmotionNames[emoIdx];
}

TinyLlmEngine::TinyLlmEngine()
  : m_info{},
    m_modelBuf(nullptr),
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
    m_kCache(nullptr),
    m_vCache(nullptr),
    m_xq(nullptr),
    m_xs(nullptr),
    m_hq(nullptr),
    m_hs(nullptr),
    m_probBuf(nullptr),
    m_promptLen(0),
    m_pos(0),
    m_curTok(1),
    m_genCount(0),
    m_maxNewTokens(48),
    m_temperature(0.7f),
    m_active(false),
    m_rng(0x9E3779B97F4A7C15ULL),
    m_recentCount(0),
    m_utf8PendLen(0),
    m_utf8Need(0) {
  for (int i = 0; i < 256; ++i) m_byteTokenIds[i] = -1;
}

TinyLlmEngine::~TinyLlmEngine() {
  unload();
}

void TinyLlmEngine::unload() {
  free(m_modelBuf);     m_modelBuf = nullptr;
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
  free(m_kCache);       m_kCache = nullptr;
  free(m_vCache);       m_vCache = nullptr;
  free(m_xq);           m_xq = nullptr;
  free(m_xs);           m_xs = nullptr;
  free(m_hq);           m_hq = nullptr;
  free(m_hs);           m_hs = nullptr;
  free(m_probBuf);      m_probBuf = nullptr;
  memset(&m_info, 0, sizeof(m_info));
  m_active = false;
}

bool TinyLlmEngine::load(const wchar_t * modelPath, const wchar_t * tokPath,
                         wchar_t * outErr, size_t maxErrLen) {
  unload();
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

  uint32_t magic = readU32LE(m_modelBuf + 0);
  int32_t ver = readI32LE(m_modelBuf + 4);
  int32_t dim = readI32LE(m_modelBuf + 8);
  int32_t hdim = readI32LE(m_modelBuf + 12);
  int32_t nLayers = readI32LE(m_modelBuf + 16);
  int32_t nHeads = readI32LE(m_modelBuf + 20);
  int32_t nKvHeads = readI32LE(m_modelBuf + 24);
  int32_t vocabRaw = readI32LE(m_modelBuf + 28);
  int32_t seqLen = readI32LE(m_modelBuf + 32);
  int32_t gs = readI32LE(m_modelBuf + 36);

  m_sharedWeights = (vocabRaw > 0);
  int32_t vocab = (vocabRaw < 0) ? -vocabRaw : vocabRaw;

  if (magic != kTtq8Magic || ver != 1 || gs <= 0 || (gs % 4) != 0 ||
      dim <= 0 || (dim % gs) != 0 || hdim <= 0 || (hdim % gs) != 0 ||
      nLayers <= 0 || nHeads <= 0 || nKvHeads <= 0 ||
      (nHeads % nKvHeads) != 0 || (dim % nHeads) != 0 ||
      vocab <= 0 || seqLen <= 0 || seqLen > 2048) {
    unload();
    setErr(L"非対応のTTQ8ヘッダです");
    return false;
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

  size_t off = 40;
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

  const size_t kvDim = static_cast<size_t>((dim * nKvHeads) / nHeads);
  const size_t tot = static_cast<size_t>(mSz);
  bool layoutOk = sliceQ8(m_modelBuf, tot, off, static_cast<size_t>(vocab) * dim, gs, m_qTokens);
  for (int l = 0; l < nLayers && layoutOk; ++l) layoutOk = sliceQ8(m_modelBuf, tot, off, static_cast<size_t>(dim) * dim, gs, m_wq[l]);
  for (int l = 0; l < nLayers && layoutOk; ++l) layoutOk = sliceQ8(m_modelBuf, tot, off, kvDim * dim, gs, m_wk[l]);
  for (int l = 0; l < nLayers && layoutOk; ++l) layoutOk = sliceQ8(m_modelBuf, tot, off, kvDim * dim, gs, m_wv[l]);
  for (int l = 0; l < nLayers && layoutOk; ++l) layoutOk = sliceQ8(m_modelBuf, tot, off, static_cast<size_t>(dim) * dim, gs, m_wo[l]);
  for (int l = 0; l < nLayers && layoutOk; ++l) layoutOk = sliceQ8(m_modelBuf, tot, off, static_cast<size_t>(hdim) * dim, gs, m_w1[l]);
  for (int l = 0; l < nLayers && layoutOk; ++l) layoutOk = sliceQ8(m_modelBuf, tot, off, static_cast<size_t>(dim) * hdim, gs, m_w2[l]);
  for (int l = 0; l < nLayers && layoutOk; ++l) layoutOk = sliceQ8(m_modelBuf, tot, off, static_cast<size_t>(hdim) * dim, gs, m_w3[l]);
  if (layoutOk) {
    if (m_sharedWeights) {
      m_wcls = m_qTokens;
    } else {
      layoutOk = sliceQ8(m_modelBuf, tot, off, static_cast<size_t>(vocab) * dim, gs, m_wcls);
    }
  }
  if (!layoutOk) {
    unload();
    setErr(L"Q8テンソル配置が不正です");
    return false;
  }

  // Load tokenizer binary (tok.bin)
  FILE * ft = _wfopen(tokPath, L"rb");
  if (!ft) {
    unload();
    setErr(L"tok.bin が見つかりません");
    return false;
  }
  fseek(ft, 0, SEEK_END);
  long tSz = ftell(ft);
  fseek(ft, 0, SEEK_SET);
  if (tSz < 16 || tSz > 4L * 1024L * 1024L) {
    fclose(ft);
    unload();
    setErr(L"tok.bin サイズ不正");
    return false;
  }

  // Copy raw tok.bin into buffer + space for null-terminated string pool
  uint8_t * rawTok = static_cast<uint8_t *>(malloc(static_cast<size_t>(tSz)));
  if (!rawTok || fread(rawTok, 1, static_cast<size_t>(tSz), ft) != static_cast<size_t>(tSz)) {
    free(rawTok);
    fclose(ft);
    unload();
    setErr(L"tok.bin 読込失敗");
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
    setErr(L"tok.bin 解析失敗");
    return false;
  }

  qsort(m_sortedLookup, static_cast<size_t>(vocab), sizeof(VocabLookup), compareVocabStr);

  for (int b = 0; b < 256; ++b) {
    char key[8];
    snprintf(key, sizeof(key), "<0x%02X>", b);
    m_byteTokenIds[b] = findTokenId(key);
  }
  int idA = findTokenId("\xEF\xBC\xA1\xEF\xBC\x9A"); // UTF-8 "Ａ："
  int idB = findTokenId("\xEF\xBC\xA2\xEF\xBC\x9A"); // UTF-8 "Ｂ："
  m_tokPromptA = (idA >= 0) ? idA : 3;
  m_tokPromptB = (idB >= 0) ? idB : 4;

  // Allocate RunState and precompute RoPE cos/sin table
  const int headDim = dim / nHeads;
  const int halfHead = headDim / 2;
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
  m_kCache = static_cast<float *>(calloc(static_cast<size_t>(nLayers) * seqLen * kvDim, sizeof(float)));
  m_vCache = static_cast<float *>(calloc(static_cast<size_t>(nLayers) * seqLen * kvDim, sizeof(float)));
  m_xq     = static_cast<int8_t *>(calloc(static_cast<size_t>(dim), sizeof(int8_t)));
  m_xs     = static_cast<float *>(calloc(static_cast<size_t>(dim / gs), sizeof(float)));
  m_hq     = static_cast<int8_t *>(calloc(static_cast<size_t>(hdim), sizeof(int8_t)));
  m_hs     = static_cast<float *>(calloc(static_cast<size_t>(hdim / gs), sizeof(float)));
  m_probBuf = static_cast<ProbItem *>(calloc(static_cast<size_t>(vocab), sizeof(ProbItem)));

  if (!m_ropeCos || !m_ropeSin || !m_x || !m_xb || !m_xb2 || !m_hb || !m_hb2 ||
      !m_q || !m_k || !m_v || !m_att || !m_logits || !m_kCache || !m_vCache ||
      !m_xq || !m_xs || !m_hq || !m_hs || !m_probBuf) {
    unload();
    setErr(L"KVキャッシュ用RAM不足");
    return false;
  }

  for (int p = 0; p < seqLen; ++p) {
    for (int i = 0; i < halfHead; ++i) {
      double freq = 1.0 / pow(10000.0, static_cast<double>(i * 2) / static_cast<double>(headDim));
      double ang = static_cast<double>(p) * freq;
      m_ropeCos[p * halfHead + i] = static_cast<float>(cos(ang));
      m_ropeSin[p * halfHead + i] = static_cast<float>(sin(ang));
    }
  }

  m_info.loaded = true;
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

int TinyLlmEngine::encodePromptUtf8(const char * textUtf8, int * outTokens, int maxTokens) const {
  if (!m_info.loaded || !textUtf8 || !outTokens || maxTokens < 8) return 0;

  int n = 0;
  outTokens[n++] = 1;            // <s>
  outTokens[n++] = m_tokPromptA; // Ａ：

  size_t len = strlen(textUtf8);
  size_t i = 0;
  char chBuf[8];
  while (i < len && n + 8 < maxTokens) {
    int clen = utf8CharByteLen(static_cast<uint8_t>(textUtf8[i]));
    if (i + static_cast<size_t>(clen) > len) clen = 1;
    memcpy(chBuf, textUtf8 + i, static_cast<size_t>(clen));
    chBuf[clen] = '\0';
    int tid = findTokenId(chBuf);
    if (tid >= 0) {
      outTokens[n++] = tid;
    } else {
      for (int b = 0; b < clen && n + 8 < maxTokens; ++b) {
        int bid = m_byteTokenIds[static_cast<uint8_t>(chBuf[b])];
        if (bid >= 0) outTokens[n++] = bid;
      }
    }
    i += static_cast<size_t>(clen);
  }

  // BPE merges on user content tokens (indices [2 .. n-1])
  char merged[128];
  for (;;) {
    float bestScore = -1e30f;
    int bestId = -1;
    int bestIdx = -1;
    for (int k = 2; k + 1 < n; ++k) {
      if (outTokens[k] <= 12 || outTokens[k + 1] <= 12) continue;
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

  outTokens[n++] = m_tokPromptB; // Ｂ：
  return n;
}

void TinyLlmEngine::decodeTokenBytes(int tok, wchar_t * outW, size_t maxW) {
  if (!outW || maxW == 0) return;
  outW[0] = L'\0';
  if (tok <= 12 || tok >= m_tokCount) return;

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
  MultiByteToWideChar(CP_UTF8, 0, piece, -1, outW, static_cast<int>(maxW));
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

  dequantizeRow(m_x, m_qTokens, token, dim, gs);

  const float * ropeC = m_ropeCos + pos * halfHead;
  const float * ropeS = m_ropeSin + pos * halfHead;

  for (int l = 0; l < m_info.nLayers; ++l) {
    rmsnorm(m_xb, m_x, m_rmsAtt + l * dim, dim);
    quantizeVec(m_xb, m_xq, m_xs, dim, gs);

    matmulQ8(m_q, m_xq, m_xs, m_wq[l], dim, dim, gs);
    matmulQ8(m_k, m_xq, m_xs, m_wk[l], dim, kvDim, gs);
    matmulQ8(m_v, m_xq, m_xs, m_wv[l], dim, kvDim, gs);

    // Adjacent-pair RoPE (matching llama2.c / TTQ8 reference)
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

    size_t cacheOff = static_cast<size_t>(l) * m_info.seqLen * kvDim + static_cast<size_t>(pos) * kvDim;
    memcpy(m_kCache + cacheOff, m_k, static_cast<size_t>(kvDim) * sizeof(float));
    memcpy(m_vCache + cacheOff, m_v, static_cast<size_t>(kvDim) * sizeof(float));

    const float invSqrtHead = 1.0f / sqrtf(static_cast<float>(headDim));
    const float * kLayer = m_kCache + static_cast<size_t>(l) * m_info.seqLen * kvDim;
    const float * vLayer = m_vCache + static_cast<size_t>(l) * m_info.seqLen * kvDim;

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
      softmax(atth, pos + 1);

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

    quantizeVec(m_xb2, m_xq, m_xs, dim, gs);
    matmulQ8(m_xb, m_xq, m_xs, m_wo[l], dim, dim, gs);
    for (int i = 0; i < dim; ++i) m_x[i] += m_xb[i];

    // SwiGLU FFN
    rmsnorm(m_xb, m_x, m_rmsFfn + l * dim, dim);
    quantizeVec(m_xb, m_xq, m_xs, dim, gs);
    matmulQ8(m_hb, m_xq, m_xs, m_w1[l], dim, hdim, gs);
    matmulQ8(m_hb2, m_xq, m_xs, m_w3[l], dim, hdim, gs);
    for (int i = 0; i < hdim; ++i) {
      float v = m_hb[i];
      float sig = 1.0f / (1.0f + expf(-v));
      m_hb[i] = (v * sig) * m_hb2[i];
    }

    quantizeVec(m_hb, m_hq, m_hs, hdim, gs);
    matmulQ8(m_xb, m_hq, m_hs, m_w2[l], hdim, dim, gs);
    for (int i = 0; i < dim; ++i) m_x[i] += m_xb[i];
  }

  if (needLogits) {
    rmsnorm(m_x, m_x, m_rmsFinal, dim);
    quantizeVec(m_x, m_xq, m_xs, dim, gs);
    matmulQ8(m_logits, m_xq, m_xs, m_wcls, dim, m_info.vocabSize, gs);
  }
}

int TinyLlmEngine::sampleLogit(float temperature) {
  const int V = m_info.vocabSize;
  // Never repeat special prompt markers <unk>, BOS, Ａ：, Ｂ： when generating reply
  m_logits[0] = -1e9f;
  m_logits[1] = -1e9f;

  if (temperature <= 0.01f) {
    int best = 2;
    for (int i = 3; i < V; ++i) {
      if (m_logits[i] > m_logits[best]) best = i;
    }
    return best;
  }

  // Mild repetition penalty on recent content tokens (skipping special emotion/stop ids 2..12)
  for (int r = 0; r < m_recentCount; ++r) {
    int rt = m_recentToks[r];
    if (rt > 12 && rt < V) {
      m_logits[rt] = (m_logits[rt] < 0.0f) ? (m_logits[rt] * 1.12f) : (m_logits[rt] / 1.12f);
    }
  }

  for (int i = 0; i < V; ++i) {
    m_logits[i] /= temperature;
  }
  softmax(m_logits, V);

  // Top-P (0.90) nucleus sampling
  int nCand = 0;
  const float cutoff = (1.0f - 0.90f) / static_cast<float>(V);
  for (int i = 2; i < V; ++i) {
    if (m_logits[i] >= cutoff) {
      m_probBuf[nCand].p = m_logits[i];
      m_probBuf[nCand].idx = i;
      ++nCand;
    }
  }
  if (nCand == 0) {
    int best = 2;
    for (int i = 3; i < V; ++i) {
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

bool TinyLlmEngine::beginPrompt(const wchar_t * userTextW, float temperature, int maxNewTokens) {
  if (!m_info.loaded || !userTextW || !userTextW[0]) return false;

  char utf8[512] = {};
  WideCharToMultiByte(CP_UTF8, 0, userTextW, -1, utf8, sizeof(utf8) - 1, NULL, NULL);
  utf8[sizeof(utf8) - 1] = '\0';

  m_promptLen = encodePromptUtf8(utf8, m_promptTokens, kMaxPromptTokens);
  if (m_promptLen < 3) return false;

  m_pos = 0;
  m_curTok = m_promptTokens[0];
  m_genCount = 0;
  m_maxNewTokens = (maxNewTokens > 1 && maxNewTokens < m_info.seqLen - m_promptLen)
                       ? maxNewTokens
                       : (m_info.seqLen - m_promptLen - 1);
  if (m_maxNewTokens < 1) m_maxNewTokens = 1;
  m_temperature = temperature;
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
  if (m_pos >= m_info.seqLen - 1 || m_genCount >= m_maxNewTokens) {
    m_active = false;
    outDone = true;
    return true;
  }

  const bool isPrefillStep = (m_pos + 1 < m_promptLen);
  forwardToken(m_curTok, m_pos, !isPrefillStep);
  ++m_pos;

  if (isPrefillStep) {
    m_curTok = m_promptTokens[m_pos];
    return false;
  }

  outGeneratingText = true;
  int nextTok = sampleLogit(m_temperature);
  m_curTok = nextTok;
  ++m_genCount;

  if (nextTok == 2 || nextTok == m_tokPromptA || nextTok == m_tokPromptB) {
    m_active = false;
    outDone = true;
    return true;
  }
  if (nextTok >= 5 && nextTok <= 12) {
    outEmotionIdx = nextTok - 5;
    m_active = false;
    outDone = true;
    return true;
  }

  if (m_recentCount < kRepeatWin) {
    m_recentToks[m_recentCount++] = nextTok;
  } else {
    memmove(m_recentToks, m_recentToks + 1, sizeof(int) * (kRepeatWin - 1));
    m_recentToks[kRepeatWin - 1] = nextTok;
  }

  decodeTokenBytes(nextTok, outDeltaW, maxDeltaW);
  return true;
}

void TinyLlmEngine::abortGeneration() {
  m_active = false;
}

} // namespace LiliumLLM
