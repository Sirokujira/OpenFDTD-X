// WavReader.h — RIFF/WAVE 読み込み。Qt 非依存 / C++14。
//
// 対応: PCM 16/24/32 bit, IEEE float 32 bit, 任意チャンネル数 / 任意 fs、
// WAVE_FORMAT_EXTENSIBLE、odd サイズチャンクのパディング。
// 自動正規化はしない (整数 PCM はフルスケール ±1.0 への変換のみ、
// float はそのままの値を保持する)。
#pragma once
#include <cstddef>
#include <string>

#include "../core/AcousticError.h"
#include "../core/AudioBuffer.h"

namespace ofd {
namespace acoustics {

// 逐次読みのバイト供給源 (負債 #6: ファイル全体を読まずに WAV を解析する)。
// ファイル / メモリ / QFile (Qt 層) が同じパーサ readWavFromSource() を共有し、
// 経路ごとの挙動差をなくす。シーク可能であること (チャンク走査は 2 パス:
// ヘッダ走査 → data チャンクへ戻ってブロック単位でデコード)。
class WavByteSource {
public:
    virtual ~WavByteSource() {}
    // 現在位置から最大 maxLen バイト読み、読めたバイト数を返す
    // (0 = EOF または I/O エラー)。
    virtual std::size_t read(unsigned char *dst, std::size_t maxLen) = 0;
    // 先頭からの絶対位置へシークする。
    virtual bool seek(unsigned long long absPos) = 0;
    // 全体のバイト数 (チャンクの切り詰め検査に使う)。
    virtual unsigned long long size() = 0;
    // エラーメッセージ用の名前 (ファイルパス等。メモリ供給源は空文字)。
    virtual std::string name() const { return std::string(); }
};

AcousticResult<AudioBuffer> readWavFromSource(WavByteSource &src);
AcousticResult<AudioBuffer> readWavFile(const std::string &path);
AcousticResult<AudioBuffer> readWavFromMemory(const unsigned char *data,
                                              std::size_t size);

} // namespace acoustics
} // namespace ofd
