// WavReader.cpp — RIFF/WAVE パーサの実装 (エンディアン非依存のバイト合成)。
//
// 負債 #6 対応: パーサは WavByteSource からの逐次読みで動く。
// パス 1 でチャンクヘッダだけを走査して fmt と data の位置を確定し、
// パス 2 で data チャンクへシークしてブロック単位 (64 KiB) にデコードする。
// ファイル全体をメモリへ複製しないので、巨大 WAV のメモリピークは
// デコード結果 (double 配列) + 64 KiB に収まる。
// メモリ経路 (readWavFromMemory) も同じパーサを通すため挙動差は生じない。
#include "WavReader.h"

#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace ofd {
namespace acoustics {

namespace {
typedef AcousticResult<AudioBuffer> Result;

// リトルエンディアンのバイト列から整数を組み立てる (ホスト非依存)
unsigned readU16(const unsigned char *p) {
    return static_cast<unsigned>(p[0]) | (static_cast<unsigned>(p[1]) << 8);
}
unsigned long readU32(const unsigned char *p) {
    return static_cast<unsigned long>(p[0]) |
           (static_cast<unsigned long>(p[1]) << 8) |
           (static_cast<unsigned long>(p[2]) << 16) |
           (static_cast<unsigned long>(p[3]) << 24);
}

bool tagEquals(const unsigned char *p, const char *tag) {
    return std::memcmp(p, tag, 4) == 0;
}

// 符号つき PCM のデコード (フルスケール ±1.0 へ変換するのみ。正規化しない)
double decodePcm16(const unsigned char *p) {
    int v = static_cast<int>(readU16(p));
    if (v >= 32768) v -= 65536;
    return static_cast<double>(v) / 32768.0;
}
double decodePcm24(const unsigned char *p) {
    long v = static_cast<long>(p[0]) | (static_cast<long>(p[1]) << 8) |
             (static_cast<long>(p[2]) << 16);
    if (v >= 8388608L) v -= 16777216L;
    return static_cast<double>(v) / 8388608.0;
}
double decodePcm32(const unsigned char *p) {
    unsigned long u = readU32(p);
    long long v = static_cast<long long>(u);
    if (v >= 2147483648LL) v -= 4294967296LL;
    return static_cast<double>(v) / 2147483648.0;
}
double decodeFloat32(const unsigned char *p) {
    unsigned long u = readU32(p);
    unsigned int u32 = static_cast<unsigned int>(u);
    float f;
    std::memcpy(&f, &u32, sizeof(f));
    return static_cast<double>(f);
}

// src から exactly n バイト読む (short read = I/O エラー扱い)。
bool readExact(WavByteSource &src, unsigned char *dst, std::size_t n) {
    std::size_t got = 0;
    while (got < n) {
        const std::size_t r = src.read(dst + got, n - got);
        if (r == 0) return false;
        got += r;
    }
    return true;
}

Result readError(WavByteSource &src) {
    const std::string n = src.name();
    return Result::error(AcousticErrorCode::FileReadError,
                         n.empty() ? std::string("read error")
                                   : "read error: " + n);
}

// メモリ供給源 — readWavFromMemory が readWavFromSource を共有するための薄い皮
class MemoryByteSource : public WavByteSource {
public:
    MemoryByteSource(const unsigned char *data, std::size_t size)
        : m_data(data), m_size(size), m_pos(0) {}
    std::size_t read(unsigned char *dst, std::size_t maxLen) override {
        if (m_pos >= m_size) return 0;
        const std::size_t n =
            (maxLen < m_size - m_pos) ? maxLen : (m_size - m_pos);
        std::memcpy(dst, m_data + m_pos, n);
        m_pos += n;
        return n;
    }
    bool seek(unsigned long long absPos) override {
        if (absPos > m_size) return false;
        m_pos = static_cast<std::size_t>(absPos);
        return true;
    }
    unsigned long long size() override { return m_size; }

private:
    const unsigned char *m_data;
    std::size_t m_size;
    std::size_t m_pos;
};

// stdio 供給源 — readWavFile 用 (FILE* は呼び出し側が所有)
class StdioByteSource : public WavByteSource {
public:
    StdioByteSource(std::FILE *fp, unsigned long long size,
                    const std::string &path)
        : m_fp(fp), m_size(size), m_path(path) {}
    std::size_t read(unsigned char *dst, std::size_t maxLen) override {
        return std::fread(dst, 1, maxLen, m_fp);
    }
    bool seek(unsigned long long absPos) override {
        // long の範囲チェック (32bit long の環境では 2 GiB 超を弾く)
        if (absPos > static_cast<unsigned long long>(
                         (std::numeric_limits<long>::max)()))
            return false;
        return std::fseek(m_fp, static_cast<long>(absPos), SEEK_SET) == 0;
    }
    unsigned long long size() override { return m_size; }
    std::string name() const override { return m_path; }

private:
    std::FILE *m_fp;
    unsigned long long m_size;
    std::string m_path;
};

} // namespace

AcousticResult<AudioBuffer> readWavFromSource(WavByteSource &src) {
    const unsigned long long total = src.size();
    if (total == 0)
        return Result::error(AcousticErrorCode::EmptyInput,
                             "no data to parse");
    unsigned char hdr12[12];
    if (total < 12 || !src.seek(0) || !readExact(src, hdr12, 12))
        return Result::error(AcousticErrorCode::UnsupportedFormat,
                             "not a RIFF/WAVE file");
    if (!tagEquals(hdr12, "RIFF") || !tagEquals(hdr12 + 8, "WAVE"))
        return Result::error(AcousticErrorCode::UnsupportedFormat,
                             "not a RIFF/WAVE file");

    // パス 1: チャンク走査 (odd サイズは 1 バイトのパディングを飛ばす)。
    // ボディはシークで飛ばし、fmt の先頭 (最大 40 バイト) だけを読む。
    bool haveFmt = false;
    unsigned audioFormat = 0, channels = 0, bitsPerSample = 0;
    unsigned long sampleRate = 0;
    bool haveData = false;
    unsigned long long dataOffset = 0;
    unsigned long long dataSize = 0;

    unsigned long long pos = 12;
    while (pos + 8 <= total) {
        if (!src.seek(pos)) return readError(src);
        unsigned char ch[8];
        if (!readExact(src, ch, 8)) return readError(src);
        const unsigned long long chunkSize = readU32(ch + 4);
        const unsigned long long body = pos + 8;
        if (body + chunkSize > total)
            return Result::error(AcousticErrorCode::FileReadError,
                                 "truncated chunk");
        if (tagEquals(ch, "fmt ")) {
            if (chunkSize < 16)
                return Result::error(AcousticErrorCode::UnsupportedFormat,
                                     "fmt chunk too small");
            unsigned char f[40];
            const std::size_t want =
                (chunkSize < 40) ? static_cast<std::size_t>(chunkSize) : 40;
            if (!readExact(src, f, want)) return readError(src);
            audioFormat = readU16(f);
            channels = readU16(f + 2);
            sampleRate = readU32(f + 4);
            bitsPerSample = readU16(f + 14);
            // WAVE_FORMAT_EXTENSIBLE: SubFormat GUID の先頭 2 バイトが実形式
            if (audioFormat == 0xFFFE) {
                if (chunkSize < 40)
                    return Result::error(AcousticErrorCode::UnsupportedFormat,
                                         "extensible fmt chunk too small");
                audioFormat = readU16(f + 24);
            }
            haveFmt = true;
        } else if (tagEquals(ch, "data")) {
            haveData = true;         // 複数あれば後勝ち (従来実装と同じ)
            dataOffset = body;
            dataSize = chunkSize;
        }
        pos = body + chunkSize + (chunkSize & 1); // odd パディング対応
    }

    if (!haveFmt)
        return Result::error(AcousticErrorCode::UnsupportedFormat,
                             "missing fmt chunk");
    if (!haveData)
        return Result::error(AcousticErrorCode::UnsupportedFormat,
                             "missing data chunk");
    if (channels == 0)
        return Result::error(AcousticErrorCode::UnsupportedFormat,
                             "zero channels");
    if (sampleRate == 0)
        return Result::error(AcousticErrorCode::UnsupportedSampleRate,
                             "zero sample rate");

    // 形式チェック: PCM 16/24/32, IEEE float 32
    const bool isPcm = (audioFormat == 1);
    const bool isFloat = (audioFormat == 3);
    if (!isPcm && !isFloat)
        return Result::error(AcousticErrorCode::UnsupportedFormat,
                             "unsupported audio format tag");
    if (isPcm && bitsPerSample != 16 && bitsPerSample != 24 &&
        bitsPerSample != 32)
        return Result::error(AcousticErrorCode::UnsupportedFormat,
                             "unsupported PCM bit depth");
    if (isFloat && bitsPerSample != 32)
        return Result::error(AcousticErrorCode::UnsupportedFormat,
                             "unsupported float bit depth");

    const std::size_t bytesPerSample = bitsPerSample / 8;
    const std::size_t frameSize = bytesPerSample * channels;
    const unsigned long long frames64 = dataSize / frameSize;
    const std::size_t frames = static_cast<std::size_t>(frames64);

    AudioBuffer buf;
    buf.sampleRateHz = static_cast<double>(sampleRate);
    buf.channels.assign(channels, std::vector<double>(frames, 0.0));

    // パス 2: data チャンクへ戻り、フレーム整数個の 64 KiB ブロックで
    // 逐次デコードする (デコード式は従来と同一 — 結果はビット一致)。
    if (!src.seek(dataOffset)) return readError(src);
    const std::size_t blockFrames =
        (frameSize < 65536) ? (65536 / frameSize) : 1;
    std::vector<unsigned char> block(blockFrames * frameSize);
    std::size_t done = 0;
    while (done < frames) {
        const std::size_t n =
            ((frames - done) < blockFrames) ? (frames - done) : blockFrames;
        if (!readExact(src, block.data(), n * frameSize))
            return readError(src);
        for (std::size_t i = 0; i < n; ++i) {
            const unsigned char *p = block.data() + i * frameSize;
            for (unsigned c = 0; c < channels; ++c) {
                const unsigned char *sp = p + c * bytesPerSample;
                double v;
                if (isFloat) {
                    v = decodeFloat32(sp);
                } else if (bitsPerSample == 16) {
                    v = decodePcm16(sp);
                } else if (bitsPerSample == 24) {
                    v = decodePcm24(sp);
                } else {
                    v = decodePcm32(sp);
                }
                buf.channels[c][done + i] = v;
            }
        }
        done += n;
    }
    return Result::ok(buf);
}

AcousticResult<AudioBuffer> readWavFromMemory(const unsigned char *data,
                                              std::size_t size) {
    if (data == nullptr || size == 0)
        return Result::error(AcousticErrorCode::EmptyInput,
                             "no data to parse");
    MemoryByteSource src(data, size);
    return readWavFromSource(src);
}

AcousticResult<AudioBuffer> readWavFile(const std::string &path) {
    std::FILE *fp = std::fopen(path.c_str(), "rb");
    if (fp == nullptr)
        return Result::error(AcousticErrorCode::FileNotFound,
                             "cannot open: " + path);
    // サイズ確定 (チャンクの切り詰め検査に使う)
    unsigned long long total = 0;
    if (std::fseek(fp, 0, SEEK_END) == 0) {
        const long end = std::ftell(fp);
        if (end > 0) total = static_cast<unsigned long long>(end);
    }
    if (std::fseek(fp, 0, SEEK_SET) != 0) {
        std::fclose(fp);
        return Result::error(AcousticErrorCode::FileReadError,
                             "read error: " + path);
    }
    if (total == 0) {
        std::fclose(fp);
        return Result::error(AcousticErrorCode::EmptyInput,
                             "empty file: " + path);
    }
    StdioByteSource src(fp, total, path);
    const Result r = readWavFromSource(src);
    std::fclose(fp);
    return r;
}

} // namespace acoustics
} // namespace ofd
