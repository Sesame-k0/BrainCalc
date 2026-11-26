#include "../common/lilium_ui.h"
#include "../common/lilium_ime_client.h"
#include "../common/lilium_settings.h"

#include <mmsystem.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifndef VK_OEM_PLUS
#define VK_OEM_PLUS 0xBB
#endif
#ifndef VK_OEM_MINUS
#define VK_OEM_MINUS 0xBD
#endif

namespace {

static constexpr int kMaxWavFiles = 128;
static constexpr int kVisRows = 5;
static constexpr int kWaveBars = 64;
static constexpr int kNumAudioBufs = 4;
static constexpr int kChunkMaxFrames = 4096;

enum class AudioMode {
  Idle = 0,
  Recording = 1,
  RecPaused = 2,
  Playing = 3,
  PlayPaused = 4
};

enum class ModalKind {
  None = 0,
  Rename = 1,
  DeleteConfirm = 2
};

struct WavEntry {
  wchar_t name[96];
  uint32_t sizeBytes;
  uint32_t sampleRate;
  uint16_t channels;
  uint16_t bitsPerSample;
  uint32_t dataOffset;
  uint32_t dataBytes;
  uint32_t durationMs;
  FILETIME writeTime;
};

struct RecorderState {
  LiliumUI::AppWindow window;
  bool manageTaskBar = false;

  wchar_t voiceDir[MAX_PATH] = L"";
  WavEntry files[kMaxWavFiles] = {};
  int fileCount = 0;
  int sel = 0;
  int scroll = 0;

  AudioMode mode = AudioMode::Idle;
  ModalKind modal = ModalKind::None;
  int deleteFocus = 1; // Safe default: Cancel
  LiliumIME::TextInputModal inputModal;

  int rateIdx = 1;  // 0=8000, 1=16000, 2=22050
  int speedIdx = 1; // 0=0.75x, 1=1.0x, 2=1.25x, 3=1.5x
  int volumePct = 80;

  // A-B Repeat (in frames of active file)
  int abState = 0; // 0=Off, 1=A set, 2=A-B active
  uint32_t abStartFrame = 0;
  uint32_t abEndFrame = 0;

  // Audio worker synchronization
  CRITICAL_SECTION audioCs;
  bool csInit = false;
  HANDLE hWaveEvent = NULL;
  HANDLE hWorkerThread = NULL;
  volatile bool stopRequested = false;
  volatile bool workerRunning = false;
  volatile bool pausedFlag = false;
  volatile int seekReqSec = 0;
  volatile bool seekAbsReq = false;
  volatile uint32_t seekAbsFrame = 0;
  volatile bool eofReached = false;
  volatile int deviceErrCode = 0;

  HWAVEIN hWaveIn = NULL;
  HWAVEOUT hWaveOut = NULL;
  FILE * recFp = nullptr;
  wchar_t activeRecName[96] = L"";
  wchar_t activeRecPath[MAX_PATH] = L"";
  uint32_t activeSampleRate = 16000;
  uint16_t activeChannels = 1;
  uint16_t activeBits = 16;
  uint32_t activeDataOffset = 44;
  uint32_t activeTotalFrames = 0;
  volatile uint32_t curFrames = 0;
  volatile uint32_t recDataBytes = 0;

  // Visualization (0..100 scale)
  uint8_t waveBars[kWaveBars] = {};
  int peakLevel = 0;

  wchar_t toast[64] = L"";
  DWORD toastExpireTick = 0;
  int lastMin = -1;
  int lastBat = -1;
};

static constexpr int kRateValues[3] = { 8000, 16000, 22050 };
static constexpr const wchar_t * kRateLabels[3] = { L"8kHz", L"16kHz", L"22kHz" };
static constexpr int kSpeedPct[4] = { 75, 100, 125, 150 };
static constexpr const wchar_t * kSpeedLabels[4] = { L"0.75x", L"1.0x", L"1.25x", L"1.5x" };

static void setToast(RecorderState * st, const wchar_t * msg) {
  if (!st) return;
  wcsncpy(st->toast, msg ? msg : L"", 63);
  st->toast[63] = L'\0';
  st->toastExpireTick = GetTickCount() + 3000;
}

static const wchar_t * activeStatusText(RecorderState * st) {
  if (!st) return L"録音待機";
  if (st->toast[0]) {
    if (static_cast<int32_t>(GetTickCount() - st->toastExpireTick) < 0) {
      return st->toast;
    }
    st->toast[0] = L'\0';
  }
  switch (st->mode) {
    case AudioMode::Recording: return L"録音中 (WAV保存)";
    case AudioMode::RecPaused: return L"録音一時停止";
    case AudioMode::Playing: return L"再生中";
    case AudioMode::PlayPaused: return L"再生一時停止";
    default: return L"録音・再生待機";
  }
}

static uint16_t readU16LE(const uint8_t * p) {
  return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

static uint32_t readU32LE(const uint8_t * p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

static void writeU16LE(uint8_t * p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

static void writeU32LE(uint8_t * p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
  p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
  p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

static void writeWavHeader(FILE * fp, uint32_t sampleRate, uint16_t channels,
                           uint16_t bitsPerSample, uint32_t dataBytes) {
  if (!fp) return;
  uint8_t hdr[44] = {};
  uint16_t blockAlign = static_cast<uint16_t>(channels * (bitsPerSample / 8));
  uint32_t byteRate = sampleRate * blockAlign;
  memcpy(hdr + 0, "RIFF", 4);
  writeU32LE(hdr + 4, 36u + dataBytes);
  memcpy(hdr + 8, "WAVE", 4);
  memcpy(hdr + 12, "fmt ", 4);
  writeU32LE(hdr + 16, 16u);
  writeU16LE(hdr + 20, 1u); // PCM
  writeU16LE(hdr + 22, channels);
  writeU32LE(hdr + 24, sampleRate);
  writeU32LE(hdr + 28, byteRate);
  writeU16LE(hdr + 32, blockAlign);
  writeU16LE(hdr + 34, bitsPerSample);
  memcpy(hdr + 36, "data", 4);
  writeU32LE(hdr + 40, dataBytes);
  fwrite(hdr, 1, 44, fp);
}

static bool parseWavFileInfo(const wchar_t * path, WavEntry & out) {
  FILE * fp = _wfopen(path, L"rb");
  if (!fp) return false;
  uint8_t top[12] = {};
  if (fread(top, 1, 12, fp) != 12 || memcmp(top, "RIFF", 4) != 0 || memcmp(top + 8, "WAVE", 4) != 0) {
    fclose(fp);
    return false;
  }

  bool gotFmt = false, gotData = false;
  uint16_t fmtTag = 0, ch = 0, bits = 0;
  uint32_t rate = 0, dataOff = 0, dataSz = 0;

  uint8_t chHdr[8] = {};
  while (fread(chHdr, 1, 8, fp) == 8) {
    uint32_t sz = readU32LE(chHdr + 4);
    long payloadOff = ftell(fp);
    if (payloadOff < 0) break;

    if (memcmp(chHdr, "fmt ", 4) == 0 && sz >= 16) {
      uint8_t fmt[16] = {};
      if (fread(fmt, 1, 16, fp) == 16) {
        fmtTag = readU16LE(fmt + 0);
        ch = readU16LE(fmt + 2);
        rate = readU32LE(fmt + 4);
        bits = readU16LE(fmt + 14);
        gotFmt = true;
      }
    } else if (memcmp(chHdr, "data", 4) == 0) {
      dataOff = static_cast<uint32_t>(payloadOff);
      dataSz = sz;
      gotData = true;
      break;
    }
    uint32_t step = sz + (sz & 1u);
    if (fseek(fp, payloadOff + static_cast<long>(step), SEEK_SET) != 0) break;
  }
  fclose(fp);

  if (!gotFmt || !gotData || fmtTag != 1 || rate == 0 ||
      (ch != 1 && ch != 2) || (bits != 8 && bits != 16)) {
    return false;
  }
  out.sampleRate = rate;
  out.channels = ch;
  out.bitsPerSample = bits;
  out.dataOffset = dataOff;
  out.dataBytes = dataSz;
  uint32_t bps = rate * ch * (bits / 8);
  out.durationMs = (bps > 0) ? static_cast<uint32_t>((static_cast<uint64_t>(dataSz) * 1000ULL) / bps) : 0;
  return true;
}

static void buildIdleWaveformPreview(RecorderState * st) {
  if (!st || st->mode != AudioMode::Idle) return;
  memset(st->waveBars, 0, sizeof(st->waveBars));
  st->peakLevel = 0;
  st->curFrames = 0;
  st->activeTotalFrames = 0;
  if (st->fileCount <= 0 || st->sel < 0 || st->sel >= st->fileCount) return;

  const WavEntry & w = st->files[st->sel];
  uint32_t blockAlign = static_cast<uint32_t>(w.channels) * (w.bitsPerSample / 8u);
  if (blockAlign == 0) return;
  st->activeTotalFrames = w.dataBytes / blockAlign;
  st->activeSampleRate = w.sampleRate;
  st->activeChannels = w.channels;
  st->activeBits = w.bitsPerSample;
  if (st->activeTotalFrames == 0) return;

  wchar_t fullPath[MAX_PATH] = L"";
  LiliumUI::joinPath(st->voiceDir, w.name, fullPath, MAX_PATH);
  FILE * fp = _wfopen(fullPath, L"rb");
  if (!fp) return;

  uint8_t buf[256] = {};
  for (int b = 0; b < kWaveBars; ++b) {
    uint32_t frame = static_cast<uint32_t>((static_cast<uint64_t>(b) * st->activeTotalFrames) / kWaveBars);
    long off = static_cast<long>(w.dataOffset + frame * blockAlign);
    if (fseek(fp, off, SEEK_SET) != 0) break;
    size_t wantBytes = 64u * blockAlign;
    if (wantBytes > sizeof(buf)) wantBytes = sizeof(buf);
    size_t got = fread(buf, 1, wantBytes, fp);
    int frames = static_cast<int>(got / blockAlign);
    int maxAbs = 0;
    for (int f = 0; f < frames; ++f) {
      const uint8_t * p = buf + f * blockAlign;
      int s = (w.bitsPerSample == 16)
                  ? static_cast<int>(static_cast<int16_t>(readU16LE(p)))
                  : (static_cast<int>(p[0]) - 128) * 256;
      if (s < 0) s = -s;
      if (s > maxAbs) maxAbs = s;
    }
    int level = (maxAbs * 100) / 32768;
    if (level > 100) level = 100;
    st->waveBars[b] = static_cast<uint8_t>(level);
  }
  fclose(fp);
}

static void scanWavFiles(RecorderState * st, bool preserveSel = true) {
  if (!st) return;
  wchar_t prevName[96] = L"";
  if (preserveSel && st->sel >= 0 && st->sel < st->fileCount) {
    wcsncpy(prevName, st->files[st->sel].name, 95);
    prevName[95] = L'\0';
  }
  st->fileCount = 0;

  wchar_t pat[MAX_PATH] = L"";
  LiliumUI::joinPath(st->voiceDir, L"*.wav", pat, MAX_PATH);
  WIN32_FIND_DATAW fd = {};
  HANDLE hFind = FindFirstFileW(pat, &fd);
  if (hFind != INVALID_HANDLE_VALUE) {
    do {
      if (st->fileCount >= kMaxWavFiles) break;
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
      wchar_t full[MAX_PATH] = L"";
      LiliumUI::joinPath(st->voiceDir, fd.cFileName, full, MAX_PATH);
      WavEntry entry = {};
      wcsncpy(entry.name, fd.cFileName, 95);
      entry.name[95] = L'\0';
      entry.sizeBytes = fd.nFileSizeLow;
      entry.writeTime = fd.ftLastWriteTime;
      if (parseWavFileInfo(full, entry)) {
        st->files[st->fileCount++] = entry;
      }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
  }

  // Sort newest first
  for (int i = 1; i < st->fileCount; ++i) {
    WavEntry mv = st->files[i];
    int j = i - 1;
    while (j >= 0 && CompareFileTime(&st->files[j].writeTime, &mv.writeTime) < 0) {
      st->files[j + 1] = st->files[j];
      --j;
    }
    st->files[j + 1] = mv;
  }

  st->sel = 0;
  if (preserveSel && prevName[0]) {
    for (int i = 0; i < st->fileCount; ++i) {
      if (_wcsicmp(st->files[i].name, prevName) == 0) {
        st->sel = i;
        break;
      }
    }
  }
  if (st->sel >= st->fileCount && st->fileCount > 0) st->sel = st->fileCount - 1;
  if (st->scroll > st->sel) st->scroll = st->sel;
  if (st->sel >= st->scroll + kVisRows) st->scroll = st->sel - kVisRows + 1;
  if (st->scroll < 0) st->scroll = 0;
  buildIdleWaveformPreview(st);
}

static void pushVisLevel(RecorderState * st, int peakAbs16) {
  int lvl = (peakAbs16 * 100) / 32768;
  // Slightly boost quiet voice peaks for clear Brain LCD visual feedback
  int vis = lvl * 2;
  if (vis > 100) vis = 100;
  if (st->csInit) EnterCriticalSection(&st->audioCs);
  memmove(st->waveBars, st->waveBars + 1, kWaveBars - 1);
  st->waveBars[kWaveBars - 1] = static_cast<uint8_t>(vis);
  st->peakLevel = vis;
  if (st->csInit) LeaveCriticalSection(&st->audioCs);
}

static void applyWaveOutVolume(RecorderState * st) {
  if (!st || !st->hWaveOut) return;
  uint32_t chVol = static_cast<uint32_t>((st->volumePct * 0xFFFF) / 100);
  waveOutSetVolume(st->hWaveOut, (chVol << 16) | chVol);
}

static DWORD WINAPI recordWorkerProc(LPVOID param) {
  RecorderState * st = static_cast<RecorderState *>(param);
  const int framesPerBuf = static_cast<int>(st->activeSampleRate / 10); // 100 ms
  const size_t bufBytes = static_cast<size_t>(framesPerBuf) * sizeof(int16_t);

  int16_t * pcmBufs[kNumAudioBufs] = {};
  WAVEHDR hdrs[kNumAudioBufs] = {};
  bool allocOk = true;
  for (int i = 0; i < kNumAudioBufs; ++i) {
    pcmBufs[i] = static_cast<int16_t *>(calloc(static_cast<size_t>(framesPerBuf), sizeof(int16_t)));
    if (!pcmBufs[i]) { allocOk = false; break; }
    hdrs[i].lpData = reinterpret_cast<LPSTR>(pcmBufs[i]);
    hdrs[i].dwBufferLength = static_cast<DWORD>(bufBytes);
  }

  int preparedCount = 0;
  if (allocOk) {
    for (int i = 0; i < kNumAudioBufs; ++i) {
      if (waveInPrepareHeader(st->hWaveIn, &hdrs[i], sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
        allocOk = false;
        break;
      }
      ++preparedCount;
      if (waveInAddBuffer(st->hWaveIn, &hdrs[i], sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
        allocOk = false;
        break;
      }
    }
  }

  if (allocOk) {
    waveInStart(st->hWaveIn);
    int syncCounter = 0;
    while (!st->stopRequested) {
      WaitForSingleObject(st->hWaveEvent, 120);
      for (int i = 0; i < kNumAudioBufs; ++i) {
        if (!(hdrs[i].dwFlags & WHDR_DONE)) continue;
        int nSamples = static_cast<int>(hdrs[i].dwBytesRecorded / sizeof(int16_t));
        int maxAbs = 0;
        for (int k = 0; k < nSamples; ++k) {
          int v = pcmBufs[i][k];
          if (v < 0) v = -v;
          if (v > maxAbs) maxAbs = v;
        }
        pushVisLevel(st, maxAbs);

        if (!st->stopRequested && !st->pausedFlag && nSamples > 0 && st->recFp) {
          fwrite(pcmBufs[i], sizeof(int16_t), static_cast<size_t>(nSamples), st->recFp);
          st->recDataBytes += static_cast<uint32_t>(nSamples) * sizeof(int16_t);
          st->curFrames += static_cast<uint32_t>(nSamples);
          if (++syncCounter >= 20) {
            syncCounter = 0;
            uint32_t curSize = st->recDataBytes;
            if (fseek(st->recFp, 0, SEEK_SET) == 0) {
              writeWavHeader(st->recFp, st->activeSampleRate, 1, 16, curSize);
              fseek(st->recFp, 0, SEEK_END);
              fflush(st->recFp);
            }
          }
        }

        if (!st->stopRequested) {
          hdrs[i].dwBytesRecorded = 0;
          hdrs[i].dwFlags &= ~WHDR_DONE;
          waveInAddBuffer(st->hWaveIn, &hdrs[i], sizeof(WAVEHDR));
        }
      }
    }
  }

  waveInStop(st->hWaveIn);
  waveInReset(st->hWaveIn);
  for (int i = 0; i < preparedCount; ++i) {
    waveInUnprepareHeader(st->hWaveIn, &hdrs[i], sizeof(WAVEHDR));
  }
  for (int i = 0; i < kNumAudioBufs; ++i) {
    free(pcmBufs[i]);
  }
  st->workerRunning = false;
  return 0;
}

static bool readResampledChunk(FILE * fp, const RecorderState * st, uint32_t & ioFramePos,
                               double speedRatio, int16_t * outMono, int maxOutSamples) {
  if (!fp || !st || st->activeTotalFrames == 0) return false;
  const uint32_t blockAlign = static_cast<uint32_t>(st->activeChannels) * (st->activeBits / 8u);
  if (blockAlign == 0) return false;

  // Read enough raw frames to cover speedRatio * maxOutSamples (max 1.5x -> <= 12288)
  uint8_t * raw = static_cast<uint8_t *>(malloc(static_cast<size_t>(kChunkMaxFrames) * blockAlign));
  if (!raw) return false;

  uint32_t limitFrame = (st->abState == 2 && st->abEndFrame > st->abStartFrame)
                            ? st->abEndFrame
                            : st->activeTotalFrames;
  if (ioFramePos >= limitFrame) {
    if (st->abState == 2 && st->abEndFrame > st->abStartFrame) {
      ioFramePos = st->abStartFrame;
    } else {
      free(raw);
      return false;
    }
  }

  uint32_t maxReadFrames = static_cast<uint32_t>(maxOutSamples * speedRatio) + 2u;
  if (maxReadFrames > kChunkMaxFrames) maxReadFrames = kChunkMaxFrames;
  uint32_t availFrames = limitFrame - ioFramePos;
  if (maxReadFrames > availFrames) maxReadFrames = availFrames;
  if (maxReadFrames == 0) {
    free(raw);
    return false;
  }

  long fileOff = static_cast<long>(st->activeDataOffset + ioFramePos * blockAlign);
  if (fseek(fp, fileOff, SEEK_SET) != 0) {
    free(raw);
    return false;
  }
  size_t gotBytes = fread(raw, 1, static_cast<size_t>(maxReadFrames) * blockAlign, fp);
  uint32_t gotFrames = static_cast<uint32_t>(gotBytes / blockAlign);
  if (gotFrames == 0) {
    free(raw);
    return false;
  }

  int produced = 0;
  double pos = 0.0;
  while (produced < maxOutSamples && static_cast<uint32_t>(pos) < gotFrames) {
    uint32_t f0 = static_cast<uint32_t>(pos);
    uint32_t f1 = (f0 + 1 < gotFrames) ? (f0 + 1) : f0;
    auto sampleAt = [&](uint32_t fi) -> int32_t {
      const uint8_t * p = raw + fi * blockAlign;
      int32_t s = 0;
      for (int c = 0; c < static_cast<int>(st->activeChannels); ++c) {
        if (st->activeBits == 16) {
          s += static_cast<int16_t>(readU16LE(p + c * 2));
        } else {
          s += (static_cast<int32_t>(p[c]) - 128) * 256;
        }
      }
      return s / static_cast<int>(st->activeChannels);
    };
    double frac = pos - static_cast<double>(f0);
    int32_t v0 = sampleAt(f0);
    int32_t v1 = sampleAt(f1);
    int32_t interp = static_cast<int32_t>(v0 + (v1 - v0) * frac);
    if (interp > 32767) interp = 32767;
    if (interp < -32768) interp = -32768;
    outMono[produced++] = static_cast<int16_t>(interp);
    pos += speedRatio;
  }

  for (int k = produced; k < maxOutSamples; ++k) outMono[k] = 0;
  uint32_t advance = static_cast<uint32_t>(pos);
  if (advance == 0) advance = gotFrames;
  ioFramePos += advance;
  free(raw);
  return produced > 0;
}

static DWORD WINAPI playWorkerProc(LPVOID param) {
  RecorderState * st = static_cast<RecorderState *>(param);
  const int outFramesPerBuf = static_cast<int>(st->activeSampleRate / 12); // ~83 ms
  const size_t bufBytes = static_cast<size_t>(outFramesPerBuf) * sizeof(int16_t);

  int16_t * pcmBufs[kNumAudioBufs] = {};
  WAVEHDR hdrs[kNumAudioBufs] = {};
  bool allocOk = true;
  for (int i = 0; i < kNumAudioBufs; ++i) {
    pcmBufs[i] = static_cast<int16_t *>(calloc(static_cast<size_t>(outFramesPerBuf), sizeof(int16_t)));
    if (!pcmBufs[i]) { allocOk = false; break; }
    hdrs[i].lpData = reinterpret_cast<LPSTR>(pcmBufs[i]);
    hdrs[i].dwBufferLength = static_cast<DWORD>(bufBytes);
    hdrs[i].dwFlags = WHDR_DONE;
  }

  int preparedCount = 0;
  if (allocOk) {
    for (int i = 0; i < kNumAudioBufs; ++i) {
      if (waveOutPrepareHeader(st->hWaveOut, &hdrs[i], sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
        allocOk = false;
        break;
      }
      ++preparedCount;
      hdrs[i].dwFlags |= WHDR_DONE;
    }
  }

  FILE * fp = _wfopen(st->activeRecPath, L"rb");
  uint32_t framePos = 0;
  bool reachedEnd = false;

  if (allocOk && fp) {
    while (!st->stopRequested) {
      if (st->seekAbsReq) {
        st->seekAbsReq = false;
        uint32_t tgt = st->seekAbsFrame;
        if (tgt >= st->activeTotalFrames) tgt = (st->activeTotalFrames > 0) ? st->activeTotalFrames - 1 : 0;
        framePos = tgt;
        reachedEnd = false;
        waveOutReset(st->hWaveOut);
      } else if (st->seekReqSec != 0) {
        int deltaSec = st->seekReqSec;
        st->seekReqSec = 0;
        int64_t nextF = static_cast<int64_t>(framePos) +
                        static_cast<int64_t>(deltaSec) * static_cast<int64_t>(st->activeSampleRate);
        if (nextF < 0) nextF = 0;
        if (nextF >= static_cast<int64_t>(st->activeTotalFrames)) {
          nextF = (st->activeTotalFrames > 0) ? static_cast<int64_t>(st->activeTotalFrames - 1) : 0;
        }
        framePos = static_cast<uint32_t>(nextF);
        reachedEnd = false;
        waveOutReset(st->hWaveOut);
      }

      if (st->pausedFlag) {
        WaitForSingleObject(st->hWaveEvent, 80);
        continue;
      }

      int inFlight = 0;
      for (int i = 0; i < kNumAudioBufs; ++i) {
        if (st->stopRequested) break;
        if (!(hdrs[i].dwFlags & WHDR_DONE)) {
          ++inFlight;
          continue;
        }
        if (reachedEnd) continue;

        double speed = static_cast<double>(kSpeedPct[st->speedIdx]) / 100.0;
        if (readResampledChunk(fp, st, framePos, speed, pcmBufs[i], outFramesPerBuf)) {
          int maxAbs = 0;
          for (int k = 0; k < outFramesPerBuf; ++k) {
            int v = pcmBufs[i][k];
            if (v < 0) v = -v;
            if (v > maxAbs) maxAbs = v;
          }
          pushVisLevel(st, maxAbs);
          st->curFrames = framePos;
          hdrs[i].dwFlags &= ~WHDR_DONE;
          waveOutWrite(st->hWaveOut, &hdrs[i], sizeof(WAVEHDR));
          ++inFlight;
        } else {
          reachedEnd = true;
        }
      }

      if (reachedEnd && inFlight == 0) {
        st->curFrames = st->activeTotalFrames;
        st->eofReached = true;
        break;
      }
      WaitForSingleObject(st->hWaveEvent, 70);
    }
  }

  if (fp) fclose(fp);
  waveOutReset(st->hWaveOut);
  for (int i = 0; i < preparedCount; ++i) {
    waveOutUnprepareHeader(st->hWaveOut, &hdrs[i], sizeof(WAVEHDR));
  }
  for (int i = 0; i < kNumAudioBufs; ++i) {
    free(pcmBufs[i]);
  }
  st->workerRunning = false;
  return 0;
}

static void stopAudioSession(RecorderState * st, bool rescan) {
  if (!st) return;
  AudioMode prev = st->mode;
  if (prev == AudioMode::Idle) return;

  st->stopRequested = true;
  st->pausedFlag = false;
  if (st->hWaveEvent) SetEvent(st->hWaveEvent);

  if (st->hWaveIn) waveInReset(st->hWaveIn);
  if (st->hWaveOut) waveOutReset(st->hWaveOut);

  if (st->hWorkerThread) {
    WaitForSingleObject(st->hWorkerThread, 4000);
    CloseHandle(st->hWorkerThread);
    st->hWorkerThread = NULL;
  }
  if (st->hWaveIn) {
    waveInClose(st->hWaveIn);
    st->hWaveIn = NULL;
  }
  if (st->hWaveOut) {
    waveOutClose(st->hWaveOut);
    st->hWaveOut = NULL;
  }
  if (st->hWaveEvent) {
    CloseHandle(st->hWaveEvent);
    st->hWaveEvent = NULL;
  }

  if (st->recFp) {
    uint32_t dataSz = st->recDataBytes;
    if (fseek(st->recFp, 0, SEEK_SET) == 0) {
      writeWavHeader(st->recFp, st->activeSampleRate, 1, 16, dataSz);
    }
    fclose(st->recFp);
    st->recFp = nullptr;
    if (dataSz == 0 && st->activeRecPath[0]) {
      DeleteFileW(st->activeRecPath);
    }
  }

  st->mode = AudioMode::Idle;
  st->eofReached = false;
  st->peakLevel = 0;
  st->window.setTimerInterval(500);

  if (prev == AudioMode::Recording || prev == AudioMode::RecPaused) {
    LiliumSettings::suiteLog(L"Recorder", L"Stopped recording %ls (%lu B)",
                             st->activeRecName, static_cast<unsigned long>(st->recDataBytes));
    if (st->recDataBytes > 0) setToast(st, L"録音を保存しました");
  }
  if (rescan) {
    scanWavFiles(st, prev == AudioMode::Playing || prev == AudioMode::PlayPaused);
    if ((prev == AudioMode::Recording || prev == AudioMode::RecPaused) && st->fileCount > 0) {
      st->sel = 0;
      st->scroll = 0;
      buildIdleWaveformPreview(st);
    }
  }
}

static bool startRecording(RecorderState * st) {
  if (!st) return false;
  stopAudioSession(st, false);

  if (waveInGetNumDevs() == 0) {
    setToast(st, L"マイクデバイスが見つかりません");
    return false;
  }

  SYSTEMTIME tm;
  GetLocalTime(&tm);
  _snwprintf(st->activeRecName, 96, L"REC_%04u%02u%02u_%02u%02u%02u.wav",
             static_cast<unsigned>(tm.wYear), static_cast<unsigned>(tm.wMonth),
             static_cast<unsigned>(tm.wDay), static_cast<unsigned>(tm.wHour),
             static_cast<unsigned>(tm.wMinute), static_cast<unsigned>(tm.wSecond));
  st->activeRecName[95] = L'\0';
  LiliumUI::joinPath(st->voiceDir, st->activeRecName, st->activeRecPath, MAX_PATH);
  if (GetFileAttributesW(st->activeRecPath) != INVALID_FILE_ATTRIBUTES) {
    _snwprintf(st->activeRecName, 96, L"REC_%04u%02u%02u_%02u%02u%02u_%03u.wav",
               static_cast<unsigned>(tm.wYear), static_cast<unsigned>(tm.wMonth),
               static_cast<unsigned>(tm.wDay), static_cast<unsigned>(tm.wHour),
               static_cast<unsigned>(tm.wMinute), static_cast<unsigned>(tm.wSecond),
               static_cast<unsigned>(tm.wMilliseconds));
    st->activeRecName[95] = L'\0';
    LiliumUI::joinPath(st->voiceDir, st->activeRecName, st->activeRecPath, MAX_PATH);
  }

  uint32_t rate = static_cast<uint32_t>(kRateValues[st->rateIdx]);
  st->activeSampleRate = rate;
  st->activeChannels = 1;
  st->activeBits = 16;
  st->activeDataOffset = 44;
  st->activeTotalFrames = 0;
  st->curFrames = 0;
  st->recDataBytes = 0;
  st->abState = 0;
  memset(st->waveBars, 0, sizeof(st->waveBars));
  st->peakLevel = 0;

  st->recFp = _wfopen(st->activeRecPath, L"wb");
  if (!st->recFp) {
    setToast(st, L"WAVファイルを作成できません");
    return false;
  }
  writeWavHeader(st->recFp, rate, 1, 16, 0);
  fflush(st->recFp);

  st->hWaveEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
  if (!st->hWaveEvent) {
    fclose(st->recFp);
    st->recFp = nullptr;
    DeleteFileW(st->activeRecPath);
    return false;
  }

  WAVEFORMATEX wfx = {};
  wfx.wFormatTag = WAVE_FORMAT_PCM;
  wfx.nChannels = 1;
  wfx.nSamplesPerSec = rate;
  wfx.wBitsPerSample = 16;
  wfx.nBlockAlign = 2;
  wfx.nAvgBytesPerSec = rate * 2;

  MMRESULT mr = waveInOpen(&st->hWaveIn, WAVE_MAPPER, &wfx,
                           reinterpret_cast<DWORD_PTR>(st->hWaveEvent), 0, CALLBACK_EVENT);
  if (mr != MMSYSERR_NOERROR || !st->hWaveIn) {
    CloseHandle(st->hWaveEvent);
    st->hWaveEvent = NULL;
    fclose(st->recFp);
    st->recFp = nullptr;
    DeleteFileW(st->activeRecPath);
    if (mr == MMSYSERR_ALLOCATED) {
      setToast(st, L"音声デバイス使用中 (辞書解放で解消)");
    } else {
      wchar_t errMsg[48];
      _snwprintf(errMsg, 48, L"録音開始エラー (Code:%u)", static_cast<unsigned>(mr));
      errMsg[47] = L'\0';
      setToast(st, errMsg);
    }
    return false;
  }

  st->stopRequested = false;
  st->pausedFlag = false;
  st->eofReached = false;
  st->workerRunning = true;
  st->mode = AudioMode::Recording;
  st->hWorkerThread = CreateThread(NULL, 0, recordWorkerProc, st, 0, NULL);
  if (!st->hWorkerThread) {
    stopAudioSession(st, false);
    setToast(st, L"録音スレッド起動失敗");
    return false;
  }
  st->window.setTimerInterval(80);
  LiliumSettings::suiteLog(L"Recorder", L"Start recording %ls (%luHz)",
                           st->activeRecName, static_cast<unsigned long>(rate));
  return true;
}

static bool startPlayback(RecorderState * st) {
  if (!st || st->fileCount <= 0 || st->sel < 0 || st->sel >= st->fileCount) {
    setToast(st, L"再生できるファイルがありません");
    return false;
  }
  stopAudioSession(st, false);

  if (waveOutGetNumDevs() == 0) {
    setToast(st, L"再生デバイスが見つかりません");
    return false;
  }

  const WavEntry & w = st->files[st->sel];
  wcsncpy(st->activeRecName, w.name, 95);
  st->activeRecName[95] = L'\0';
  LiliumUI::joinPath(st->voiceDir, w.name, st->activeRecPath, MAX_PATH);
  st->activeSampleRate = w.sampleRate;
  st->activeChannels = w.channels;
  st->activeBits = w.bitsPerSample;
  st->activeDataOffset = w.dataOffset;
  uint32_t blockAlign = static_cast<uint32_t>(w.channels) * (w.bitsPerSample / 8u);
  st->activeTotalFrames = (blockAlign > 0) ? (w.dataBytes / blockAlign) : 0;
  st->curFrames = 0;
  st->abState = 0;
  st->abStartFrame = 0;
  st->abEndFrame = st->activeTotalFrames;
  memset(st->waveBars, 0, sizeof(st->waveBars));
  st->peakLevel = 0;

  if (st->activeTotalFrames == 0) {
    setToast(st, L"音声データが空です");
    return false;
  }

  st->hWaveEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
  if (!st->hWaveEvent) return false;

  WAVEFORMATEX wfx = {};
  wfx.wFormatTag = WAVE_FORMAT_PCM;
  wfx.nChannels = 1;
  wfx.nSamplesPerSec = w.sampleRate;
  wfx.wBitsPerSample = 16;
  wfx.nBlockAlign = 2;
  wfx.nAvgBytesPerSec = w.sampleRate * 2;

  MMRESULT mr = waveOutOpen(&st->hWaveOut, WAVE_MAPPER, &wfx,
                            reinterpret_cast<DWORD_PTR>(st->hWaveEvent), 0, CALLBACK_EVENT);
  if (mr != MMSYSERR_NOERROR || !st->hWaveOut) {
    CloseHandle(st->hWaveEvent);
    st->hWaveEvent = NULL;
    setToast(st, (mr == MMSYSERR_ALLOCATED) ? L"再生デバイス使用中です" : L"再生開始エラー");
    return false;
  }
  applyWaveOutVolume(st);

  st->stopRequested = false;
  st->pausedFlag = false;
  st->eofReached = false;
  st->seekReqSec = 0;
  st->seekAbsReq = false;
  st->workerRunning = true;
  st->mode = AudioMode::Playing;
  st->hWorkerThread = CreateThread(NULL, 0, playWorkerProc, st, 0, NULL);
  if (!st->hWorkerThread) {
    stopAudioSession(st, false);
    return false;
  }
  st->window.setTimerInterval(80);
  return true;
}

static void formatTimeMs(uint32_t ms, bool tenths, wchar_t * out, size_t maxLen) {
  uint32_t totalSec = ms / 1000u;
  uint32_t mins = totalSec / 60u;
  uint32_t secs = totalSec % 60u;
  if (tenths) {
    uint32_t t = (ms % 1000u) / 100u;
    _snwprintf(out, maxLen, L"%02u:%02u.%u",
               static_cast<unsigned>(mins), static_cast<unsigned>(secs), static_cast<unsigned>(t));
  } else {
    _snwprintf(out, maxLen, L"%02u:%02u",
               static_cast<unsigned>(mins), static_cast<unsigned>(secs));
  }
  out[maxLen - 1] = L'\0';
}

static void renderDeck(LiliumUI::Canvas & c, RecorderState * st) {
  const int dx = 14, dy = 48, dw = LiliumUI::kMainW - 28, dh = 166;
  c.drawBox(dx, dy, dw, dh, LiliumUI::kColWhite, LiliumUI::kColBorder);

  const bool isRec = (st->mode == AudioMode::Recording || st->mode == AudioMode::RecPaused);
  const bool isPlay = (st->mode == AudioMode::Playing || st->mode == AudioMode::PlayPaused);

  // Active file title
  const wchar_t * label = L"新規録音待機中 ([F1] で録音開始)";
  if (isRec || isPlay) {
    label = st->activeRecName;
  } else if (st->fileCount > 0 && st->sel >= 0 && st->sel < st->fileCount) {
    label = st->files[st->sel].name;
  }
  c.drawText(dx + 12, dy + 6, 290, 24,
             isRec ? LiliumUI::kColDangerText : LiliumUI::kColTealDeep,
             label, 2, LiliumUI::Align::Left, true);

  // Elapsed / Total clock + status pills
  uint32_t curMs = (st->activeSampleRate > 0)
                       ? static_cast<uint32_t>((static_cast<uint64_t>(st->curFrames) * 1000ULL) / st->activeSampleRate)
                       : 0u;
  uint32_t totMs = (st->activeSampleRate > 0)
                       ? static_cast<uint32_t>((static_cast<uint64_t>(st->activeTotalFrames) * 1000ULL) / st->activeSampleRate)
                       : 0u;
  wchar_t tCur[16], tTot[16], clockStr[36];
  formatTimeMs(curMs, true, tCur, 16);
  if (isRec) {
    _snwprintf(clockStr, 36, L"%ls (%.1f KB)", tCur, static_cast<double>(st->recDataBytes) / 1024.0);
  } else {
    formatTimeMs(totMs, true, tTot, 16);
    _snwprintf(clockStr, 36, L"%ls / %ls", tCur, tTot);
  }
  clockStr[35] = L'\0';
  c.drawText(dx + 308, dy + 6, dw - 320, 24,
             isRec ? LiliumUI::kColDangerText : LiliumUI::kColText,
             clockStr, 2, LiliumUI::Align::Right, true);

  // Waveform box (left) & VU Meter (right)
  const int wx = dx + 12, wy = dy + 34, ww = 540, wh = 66;
  c.drawBox(wx, wy, ww, wh, 0x162028, LiliumUI::kColCharcoal);
  c.fillRect(wx + 2, wy + wh / 2, ww - 4, 1, 0x293945);

  uint8_t localBars[kWaveBars] = {};
  int localPeak = 0;
  if (st->csInit) EnterCriticalSection(&st->audioCs);
  memcpy(localBars, st->waveBars, kWaveBars);
  localPeak = st->peakLevel;
  if (st->csInit) LeaveCriticalSection(&st->audioCs);

  const int stepW = (ww - 16) / kWaveBars; // 8 px per bar
  const uint32_t barCol = isRec ? 0xFF6B5B : (isPlay ? 0x4FBFAF : 0x6AA39C);
  for (int i = 0; i < kWaveBars; ++i) {
    int lvl = localBars[i];
    int bh = 2 + (lvl * (wh - 12)) / 100;
    int bx = wx + 8 + i * stepW;
    int by = wy + (wh - bh) / 2;
    c.fillRect(bx, by, stepW - 2, bh, barCol);
  }

  // Vertical VU peak bar
  const int vx = wx + ww + 8, vw = dw - 24 - ww - 8;
  c.drawBox(vx, wy, vw, wh, 0x162028, LiliumUI::kColCharcoal);
  int vuSegments = (localPeak * 10 + 99) / 100;
  if (vuSegments > 10) vuSegments = 10;
  for (int s = 0; s < 10; ++s) {
    bool lit = (s < vuSegments);
    uint32_t col = 0x26323B;
    if (lit) {
      col = (s >= 8) ? LiliumUI::kColDangerFill : (s >= 6 ? 0xE6A23C : 0x3ECF8E);
    }
    int sy = wy + wh - 6 - (s + 1) * 5;
    c.fillRect(vx + 5, sy, vw - 10, 4, col);
  }

  // Seek / Progress Bar (dy + 106 .. dy + 120)
  const int px = dx + 12, py = dy + 106, pw = dw - 24, ph = 14;
  c.drawBox(px, py, pw, ph, LiliumUI::kColSidebar, LiliumUI::kColBorder);
  if (st->activeTotalFrames > 0) {
    if (st->abState >= 1) {
      int ax = px + static_cast<int>((static_cast<uint64_t>(st->abStartFrame) * pw) / st->activeTotalFrames);
      int bx = (st->abState == 2)
                   ? (px + static_cast<int>((static_cast<uint64_t>(st->abEndFrame) * pw) / st->activeTotalFrames))
                   : ax;
      if (bx > ax) c.fillRect(ax, py + 1, bx - ax, ph - 2, 0xFFE3A3);
      c.fillRect(ax, py, 2, ph, 0xCC7A00);
      if (st->abState == 2) c.fillRect(bx - 2, py, 2, ph, 0xCC7A00);
    }
    int fillW = static_cast<int>((static_cast<uint64_t>(st->curFrames) * (pw - 2)) / st->activeTotalFrames);
    if (fillW > pw - 2) fillW = pw - 2;
    if (fillW > 0) c.fillRect(px + 1, py + 3, fillW, ph - 6, LiliumUI::kColTeal);
  }

  // Action control strip (7 buttons across dw - 24 = 588 -> 84px pitch)
  static constexpr const wchar_t * kRecLabelIdle = L"[F1] 録音";
  static constexpr const wchar_t * kRecLabelAct = L"[F1] 停止";
  const wchar_t * spcLabel = isRec ? ((st->mode == AudioMode::RecPaused) ? L"[Spc] 再開" : L"[Spc] 休止")
                                   : (isPlay ? ((st->mode == AudioMode::PlayPaused) ? L"[Spc] 再開" : L"[Spc] 休止")
                                             : L"[Spc] 再生");
  wchar_t rateBtn[20], spdBtn[20], abBtn[20];
  _snwprintf(rateBtn, 20, L"[F2]%ls", kRateLabels[st->rateIdx]);
  _snwprintf(spdBtn, 20, L"[F3]%ls", kSpeedLabels[st->speedIdx]);
  _snwprintf(abBtn, 20, L"[F4]%ls", (st->abState == 0) ? L"A-B" : (st->abState == 1 ? L"A->?" : L"A-B中"));
  rateBtn[19] = spdBtn[19] = abBtn[19] = L'\0';

  const wchar_t * btnLabels[7] = {
    isRec ? kRecLabelAct : kRecLabelIdle,
    spcLabel,
    rateBtn,
    spdBtn,
    abBtn,
    L"[F8] 改名",
    L"[F9] 削除"
  };
  for (int b = 0; b < 7; ++b) {
    int bx = dx + 12 + b * 84;
    int bw = 80;
    int by = dy + 126;
    int bh = 32;
    uint32_t bg = LiliumUI::kColWhite;
    uint32_t bd = LiliumUI::kColBorder;
    uint32_t fg = LiliumUI::kColText;
    if (b == 0) {
      bg = isRec ? LiliumUI::kColDangerFill : LiliumUI::kColDangerBg;
      bd = LiliumUI::kColDangerBorder;
      fg = isRec ? LiliumUI::kColWhite : LiliumUI::kColDangerText;
    } else if (b == 1) {
      bg = isPlay ? LiliumUI::kColTeal : LiliumUI::kColTealSoft;
      bd = LiliumUI::kColTealDark;
      fg = isPlay ? LiliumUI::kColWhite : LiliumUI::kColTealDeep;
    } else if (b == 4 && st->abState > 0) {
      bg = LiliumUI::kColWarnBg;
      bd = LiliumUI::kColWarnBorder;
      fg = LiliumUI::kColWarnText;
    }
    c.drawBox(bx, by, bw, bh, bg, bd);
    c.drawText(bx + 1, by, bw - 2, bh, fg, btnLabels[b], 1, LiliumUI::Align::Center, true);
  }
}

static void renderFileList(LiliumUI::Canvas & c, RecorderState * st) {
  const int lx = 14, ly = 220, lw = LiliumUI::kMainW - 28, lh = 224;
  c.drawBox(lx, ly, lw, lh, LiliumUI::kColWhite, LiliumUI::kColBorder);
  c.drawBox(lx, ly, lw, 28, LiliumUI::kColTealHeader, LiliumUI::kColBorder);

  wchar_t hdr[64];
  _snwprintf(hdr, 64, L"録音ファイル一覧 (apps\\voice)  [%d 件]", st->fileCount);
  hdr[63] = L'\0';
  c.drawText(lx + 10, ly, 380, 28, LiliumUI::kColTealDeep, hdr, 2, LiliumUI::Align::Left, true);

  wchar_t volStr[32];
  _snwprintf(volStr, 32, L"音量: %d%% [+/-]", st->volumePct);
  volStr[31] = L'\0';
  c.drawText(lx + 400, ly, lw - 410, 28, LiliumUI::kColTealDark, volStr, 2, LiliumUI::Align::Right, true);

  if (st->fileCount == 0) {
    c.drawText(lx + 20, ly + 84, lw - 40, 28, LiliumUI::kColMuted,
               L"録音ファイルがありません。[F1] を押して録音を開始できます。",
               2, LiliumUI::Align::Center, true);
    return;
  }

  for (int r = 0; r < kVisRows; ++r) {
    int idx = st->scroll + r;
    if (idx >= st->fileCount) break;
    const WavEntry & w = st->files[idx];
    int ry = ly + 31 + r * 38;
    bool selected = (idx == st->sel);
    bool playingThis = (st->mode == AudioMode::Playing || st->mode == AudioMode::PlayPaused) &&
                       _wcsicmp(w.name, st->activeRecName) == 0;
    uint32_t bg = selected ? LiliumUI::kColTealSoft : ((r % 2 == 1) ? LiliumUI::kColRowAlt : LiliumUI::kColWhite);
    uint32_t bd = selected ? LiliumUI::kColTeal : LiliumUI::kColWhite;
    c.drawBox(lx + 4, ry, lw - 8, 35, bg, bd);
    if (selected) c.fillRect(lx + 4, ry, 4, 35, LiliumUI::kColTealDark);

    c.drawText(lx + 14, ry, 310, 35,
               playingThis ? LiliumUI::kColTealDark : (selected ? LiliumUI::kColTealDeep : LiliumUI::kColText),
               w.name, 2, LiliumUI::Align::Left, true);

    wchar_t durStr[16], meta[48];
    formatTimeMs(w.durationMs, false, durStr, 16);
    _snwprintf(meta, 48, L"%ls  %uHz  %uKB", durStr,
               static_cast<unsigned>(w.sampleRate),
               static_cast<unsigned>((w.sizeBytes + 1023u) / 1024u));
    meta[47] = L'\0';
    c.drawText(lx + 332, ry, lw - 344, 35, LiliumUI::kColMuted, meta, 2, LiliumUI::Align::Right, true);
  }
}

static void onRender(LiliumUI::Canvas & c, void * userData) {
  RecorderState * st = static_cast<RecorderState *>(userData);

  if (st->modal == ModalKind::Rename && st->inputModal.isOpen() &&
      !c.fullRedraw && st->inputModal.hasDrawnChrome()) {
    st->inputModal.render(c, true);
    return;
  }

  if (c.fullRedraw) {
    c.fillRect(0, 0, LiliumUI::kMainW, LiliumUI::kScreenH, LiliumUI::kColBgMain);
    LiliumUI::renderTopHeader(c, L"Lilium Recorder", activeStatusText(st));
    renderDeck(c, st);
    renderFileList(c, st);
    c.drawText(14, 448, LiliumUI::kMainW - 28, 24, LiliumUI::kColMuted,
               L"[F1]録音/停止 [Spc]再生/休止 [←→]シーク [↑↓]選択 [F8]改名 [F9]削除",
               1, LiliumUI::Align::Center, true);

    const wchar_t * cardBody =
        (st->mode == AudioMode::Recording)
            ? L"内蔵マイクからWAV録音中。[F1]で保存して停止します。"
            : L"Brain内蔵マイクでWAV録音・語学A-Bリピートや倍速再生に対応。";
    LiliumUI::renderSidebar(c, activeStatusText(st), L"ボイスレコーダー", cardBody,
                            L"[F1]録音 [Spc]再生", L"[F2]音質 [F3]速度",
                            (st->mode == AudioMode::Idle) ? L"終 了" : L"停止＆終了",
                            st->mode == AudioMode::Recording);
  } else {
    LiliumUI::renderTopHeader(c, L"Lilium Recorder", activeStatusText(st));
    renderDeck(c, st);
    LiliumUI::renderSidebarStatus(c, activeStatusText(st));
  }

  if (st->modal == ModalKind::Rename) {
    st->inputModal.render(c, true);
  } else if (st->modal == ModalKind::DeleteConfirm && st->fileCount > 0) {
    static constexpr LiliumUI::ModalButton kDelBtns[2] = {
      { L"削除する", true }, { L"キャンセル", false }
    };
    LiliumUI::renderConfirmModal(c, L"【 録音ファイル削除 】",
                                 st->files[st->sel].name,
                                 L"選択中のWAVファイルを削除しますか？",
                                 kDelBtns, 2, st->deleteFocus,
                                 L"[←→]選択 [Enter]実行 [Esc]中止");
  }
}

static void handleRenameConfirm(RecorderState * st) {
  if (!st || st->fileCount <= 0 || st->sel < 0 || st->sel >= st->fileCount) return;
  const wchar_t * raw = st->inputModal.text();
  if (!raw || !raw[0]) return;
  wchar_t base[80] = L"";
  wcsncpy(base, raw, 72);
  base[72] = L'\0';
  for (int i = 0; base[i]; ++i) {
    if (base[i] == L'\\' || base[i] == L'/' || base[i] == L':' ||
        base[i] == L'*' || base[i] == L'?' || base[i] == L'"') {
      base[i] = L'_';
    }
  }
  size_t len = wcslen(base);
  wchar_t newName[96] = L"";
  if (len >= 4 && _wcsicmp(base + len - 4, L".wav") == 0) {
    wcsncpy(newName, base, 95);
  } else {
    _snwprintf(newName, 96, L"%ls.wav", base);
  }
  newName[95] = L'\0';

  wchar_t oldP[MAX_PATH] = L"", newP[MAX_PATH] = L"";
  LiliumUI::joinPath(st->voiceDir, st->files[st->sel].name, oldP, MAX_PATH);
  LiliumUI::joinPath(st->voiceDir, newName, newP, MAX_PATH);
  if (_wcsicmp(oldP, newP) != 0 && MoveFileW(oldP, newP)) {
    scanWavFiles(st, false);
    for (int i = 0; i < st->fileCount; ++i) {
      if (_wcsicmp(st->files[i].name, newName) == 0) {
        st->sel = i;
        break;
      }
    }
    setToast(st, L"ファイル名を変更しました");
  }
}

static void onKeyDown(WPARAM vkCode, bool isShift, void * userData) {
  RecorderState * st = static_cast<RecorderState *>(userData);

  if (st->modal == ModalKind::Rename) {
    bool confirmed = false, closed = false;
    st->inputModal.handleKey(vkCode, isShift, confirmed, closed);
    if (confirmed) handleRenameConfirm(st);
    if (closed) st->modal = ModalKind::None;
    st->window.requestRepaint(confirmed || closed);
    return;
  }

  if (st->modal == ModalKind::DeleteConfirm) {
    if (vkCode == VK_LEFT || vkCode == VK_RIGHT) {
      st->deleteFocus = (st->deleteFocus == 0) ? 1 : 0;
    } else if (vkCode == VK_RETURN || vkCode == VK_F3) {
      if (st->deleteFocus == 0 && st->fileCount > 0) {
        wchar_t full[MAX_PATH] = L"";
        LiliumUI::joinPath(st->voiceDir, st->files[st->sel].name, full, MAX_PATH);
        DeleteFileW(full);
        scanWavFiles(st, false);
        setToast(st, L"ファイルを削除しました");
      }
      st->modal = ModalKind::None;
    } else if (vkCode == VK_ESCAPE || vkCode == VK_BACK) {
      st->modal = ModalKind::None;
    }
    st->window.requestRepaint(true);
    return;
  }

  if (vkCode == VK_HOME || vkCode == VK_F10) {
    stopAudioSession(st, false);
    st->window.requestQuit();
    return;
  }

  if (vkCode == VK_F1) {
    if (st->mode == AudioMode::Recording || st->mode == AudioMode::RecPaused) {
      stopAudioSession(st, true);
    } else {
      startRecording(st);
    }
    st->window.requestRepaint(true);
    return;
  }

  if (vkCode == VK_SPACE) {
    if (st->mode == AudioMode::Recording) {
      st->pausedFlag = true;
      st->mode = AudioMode::RecPaused;
    } else if (st->mode == AudioMode::RecPaused) {
      st->pausedFlag = false;
      st->mode = AudioMode::Recording;
    } else if (st->mode == AudioMode::Playing) {
      st->pausedFlag = true;
      if (st->hWaveOut) waveOutPause(st->hWaveOut);
      st->mode = AudioMode::PlayPaused;
    } else if (st->mode == AudioMode::PlayPaused) {
      st->pausedFlag = false;
      if (st->hWaveOut) waveOutRestart(st->hWaveOut);
      st->mode = AudioMode::Playing;
    } else {
      startPlayback(st);
    }
    st->window.requestRepaint(true);
    return;
  }

  if (vkCode == VK_RETURN) {
    if (st->mode == AudioMode::Idle) {
      startPlayback(st);
    } else if (st->mode == AudioMode::Playing || st->mode == AudioMode::PlayPaused) {
      stopAudioSession(st, false);
    } else if (st->mode == AudioMode::Recording || st->mode == AudioMode::RecPaused) {
      stopAudioSession(st, true);
    }
    st->window.requestRepaint(true);
    return;
  }

  if (vkCode == VK_ESCAPE || vkCode == VK_BACK) {
    if (st->mode != AudioMode::Idle) {
      stopAudioSession(st, true);
      st->window.requestRepaint(true);
    } else if (vkCode == VK_ESCAPE) {
      st->window.requestQuit();
    }
    return;
  }

  if (vkCode == VK_F2) {
    if (st->mode == AudioMode::Idle) {
      st->rateIdx = (st->rateIdx + 1) % 3;
      st->window.requestRepaint(false);
    }
    return;
  }
  if (vkCode == VK_F3) {
    st->speedIdx = (st->speedIdx + 1) % 4;
    st->window.requestRepaint(false);
    return;
  }
  if (vkCode == VK_F4) {
    if (st->mode == AudioMode::Playing || st->mode == AudioMode::PlayPaused) {
      if (st->abState == 0) {
        st->abStartFrame = st->curFrames;
        st->abState = 1;
      } else if (st->abState == 1) {
        if (st->curFrames > st->abStartFrame + (st->activeSampleRate / 5u)) {
          st->abEndFrame = st->curFrames;
          st->abState = 2;
        } else {
          st->abState = 0;
        }
      } else {
        st->abState = 0;
      }
      st->window.requestRepaint(false);
    }
    return;
  }

  if ((vkCode == VK_F8 || vkCode == VK_F5) && st->mode == AudioMode::Idle && st->fileCount > 0) {
    wchar_t stem[96] = L"";
    wcsncpy(stem, st->files[st->sel].name, 95);
    stem[95] = L'\0';
    size_t len = wcslen(stem);
    if (len >= 4 && _wcsicmp(stem + len - 4, L".wav") == 0) stem[len - 4] = L'\0';
    st->modal = ModalKind::Rename;
    st->inputModal.open(L"録音ファイル名変更", L"新しい名前を入力 (.wav 自動付与):", stem, 64);
    st->window.requestRepaint(true);
    return;
  }

  if ((vkCode == VK_F9 || vkCode == VK_F6) && st->mode == AudioMode::Idle && st->fileCount > 0) {
    st->modal = ModalKind::DeleteConfirm;
    st->deleteFocus = 1;
    st->window.requestRepaint(true);
    return;
  }

  if (vkCode == VK_UP && st->mode == AudioMode::Idle && st->fileCount > 0) {
    st->sel = (st->sel + st->fileCount - 1) % st->fileCount;
    if (st->sel < st->scroll) st->scroll = st->sel;
    if (st->sel >= st->scroll + kVisRows) st->scroll = st->sel - kVisRows + 1;
    buildIdleWaveformPreview(st);
    st->window.requestRepaint(true);
    return;
  }
  if (vkCode == VK_DOWN && st->mode == AudioMode::Idle && st->fileCount > 0) {
    st->sel = (st->sel + 1) % st->fileCount;
    if (st->sel < st->scroll) st->scroll = st->sel;
    if (st->sel >= st->scroll + kVisRows) st->scroll = st->sel - kVisRows + 1;
    buildIdleWaveformPreview(st);
    st->window.requestRepaint(true);
    return;
  }
  if (vkCode == VK_LEFT && (st->mode == AudioMode::Playing || st->mode == AudioMode::PlayPaused)) {
    st->seekReqSec = -5;
    return;
  }
  if (vkCode == VK_RIGHT && (st->mode == AudioMode::Playing || st->mode == AudioMode::PlayPaused)) {
    st->seekReqSec = 5;
    return;
  }
  if (vkCode == VK_OEM_PLUS || vkCode == VK_ADD || vkCode == VK_PRIOR) {
    st->volumePct += 10;
    if (st->volumePct > 100) st->volumePct = 100;
    applyWaveOutVolume(st);
    st->window.requestRepaint(true);
    return;
  }
  if (vkCode == VK_OEM_MINUS || vkCode == VK_SUBTRACT || vkCode == VK_NEXT) {
    st->volumePct -= 10;
    if (st->volumePct < 0) st->volumePct = 0;
    applyWaveOutVolume(st);
    st->window.requestRepaint(true);
    return;
  }
}

static void onClick(int x, int y, void * userData) {
  RecorderState * st = static_cast<RecorderState *>(userData);

  if (st->modal == ModalKind::Rename) {
    bool confirmed = false, closed = false;
    st->inputModal.handleClick(x, y, confirmed, closed);
    if (confirmed) handleRenameConfirm(st);
    if (closed) st->modal = ModalKind::None;
    st->window.requestRepaint(confirmed || closed);
    return;
  }
  if (st->modal == ModalKind::DeleteConfirm) {
    if (y >= 260 && y <= 316) {
      onKeyDown((x < 320) ? VK_LEFT : VK_RIGHT, false, userData);
      onKeyDown(VK_RETURN, false, userData);
    }
    return;
  }

  // Right sidebar exit
  if (x >= LiliumUI::kExitBtnX && x < LiliumUI::kExitBtnX + LiliumUI::kExitBtnW &&
      y >= LiliumUI::kExitBtnY && y < LiliumUI::kExitBtnY + LiliumUI::kExitBtnH) {
    onKeyDown(VK_F10, false, userData);
    return;
  }

  // Seek bar click
  if ((st->mode == AudioMode::Playing || st->mode == AudioMode::PlayPaused) &&
      st->activeTotalFrames > 0 && x >= 26 && x < 614 && y >= 150 && y <= 172) {
    uint32_t target = static_cast<uint32_t>((static_cast<uint64_t>(x - 26) * st->activeTotalFrames) / 588u);
    st->seekAbsFrame = target;
    st->seekAbsReq = true;
    return;
  }

  // Deck action strip (y = 174..206)
  if (y >= 174 && y <= 206 && x >= 26 && x < 614) {
    int b = (x - 26) / 84;
    static constexpr WPARAM kBtnVks[7] = {
      VK_F1, VK_SPACE, VK_F2, VK_F3, VK_F4, VK_F8, VK_F9
    };
    if (b >= 0 && b < 7) onKeyDown(kBtnVks[b], false, userData);
    return;
  }

  // File list click
  if (st->mode == AudioMode::Idle && x >= 18 && x < LiliumUI::kMainW - 18 && y >= 251 && y < 441) {
    int r = (y - 251) / 38;
    int idx = st->scroll + r;
    if (r >= 0 && r < kVisRows && idx < st->fileCount) {
      if (st->sel == idx) {
        startPlayback(st);
      } else {
        st->sel = idx;
        buildIdleWaveformPreview(st);
      }
      st->window.requestRepaint(true);
    }
  }
}

static void onTimer(LiliumUI::Canvas & c, void * userData) {
  RecorderState * st = static_cast<RecorderState *>(userData);
  if (st->modal != ModalKind::None) return;

  if (st->eofReached && (st->mode == AudioMode::Playing || st->mode == AudioMode::PlayPaused)) {
    stopAudioSession(st, false);
    st->window.requestRepaint(true);
    return;
  }

  if (st->mode == AudioMode::Idle) {
    SYSTEMTIME tm;
    GetLocalTime(&tm);
    int batPct = 100;
    bool chg = false;
    LiliumUI::queryBattery(batPct, chg);
    if (tm.wMinute != st->lastMin || batPct != st->lastBat) {
      st->lastMin = tm.wMinute;
      st->lastBat = batPct;
      LiliumUI::renderSidebarStatus(c, activeStatusText(st));
    }
    return;
  }

  renderDeck(c, st);
  LiliumUI::renderSidebarStatus(c, activeStatusText(st));
}

static bool onCloseRequest(void * userData) {
  RecorderState * st = static_cast<RecorderState *>(userData);
  stopAudioSession(st, false);
  return true;
}

} // namespace

int main(int /*argc*/, char * /*argv*/[]) {
  void * raw = calloc(1, sizeof(RecorderState));
  if (!raw) return 1;
  RecorderState * st = new (raw) RecorderState();
  InitializeCriticalSection(&st->audioCs);
  st->csInit = true;
  st->manageTaskBar = !LiliumUI::isLaunchedFromLauncherArg();

  wchar_t exeDir[MAX_PATH] = L"";
  LiliumUI::getExeDir(exeDir, MAX_PATH);
  LiliumUI::joinPath(exeDir, L"voice", st->voiceDir, MAX_PATH);
  CreateDirectoryW(st->voiceDir, NULL);
  scanWavFiles(st, false);

  LiliumUI::AppCallbacks cb = {};
  cb.onRender = onRender;
  cb.onKeyDown = onKeyDown;
  cb.onClick = onClick;
  cb.onTimer = onTimer;
  cb.onCloseRequest = onCloseRequest;
  cb.timerIntervalMs = 500;
  cb.timerAlwaysFire = true;

  int rc = 1;
  if (st->window.create(L"LiliumRecorderWinClass",
                        L"Lilium Recorder (Voice Memo / WAV1:)",
                        cb, st, st->manageTaskBar)) {
    rc = st->window.runLoop();
  }

  stopAudioSession(st, false);
  if (st->csInit) DeleteCriticalSection(&st->audioCs);
  st->~RecorderState();
  free(raw);
  return rc;
}
