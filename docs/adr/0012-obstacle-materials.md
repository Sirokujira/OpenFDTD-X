# ADR-0012: 障害物 (geometry) の材質の契約

## Status

Accepted (2026-10-19) — ソルバー側 (OpenAcoustics の `ofdx_acoustic_fdtd` /
`ofdx_acoustic_ga` 両バイナリ) は実装・検証済み (OpenAcoustics PR #9)。
GUI 側の設定 UI (AcousticSolverTab の「障害物の材質」表 →
`AcousticOpts::obstacleMaterials` → `.ofdx` `acoustic.obstacles`) も
実装済み (行が無ければキー自体を書かない — 絶対規則 2。selftest が
バイト一致往復を検証する)。**本 ADR に未実装の項目は無い。**

## Context

- 室内音響の `.ofd` の geometry (ステージ・客席段・柱・反射板など) は、
  外部ソルバーではこれまで**常に剛体**だった。幾何音響ソルバーだけは
  手書きキー `acoustic.ga.obstacles[]` で吸音率を与えられたが、FDTD は
  `acoustic.ga` を丸ごと読み飛ばす規約なので剛体のままだった。
- その結果、材質を与えると障害物が**低域 (剛体) と高域 (吸音) で別物**に
  なり、ハイブリッド合成 (ADR-0008) のクロスオーバーで整合が崩れる。
  ADR-0010 / ADR-0011 と同じく、両ソルバーが同じ境界を見ることが整合の
  前提である。

## Decision

1. **`.ofdx` に `acoustic.obstacles[]` を新設する** (`acoustic` 直下 —
   ADR-0010 Decision 1 / ADR-0011 Decision 2 と同じく両ソルバーが読む)。
   ```json
   "acoustic": {
     "obstacles": [ { "geometry": 2, "alpha": [0.10, 0.12, 0.16, 0.20, 0.24, 0.30] } ]
   }
   ```
   - `geometry` は `.ofd` の geometry 行の **1 起点**の番号 (= GUI の
     `geometries()` の並び、solver.log の `geometry #N`)。存在しない番号は
     ソルバーが**非零終了**する。同じ番号の行が複数あれば最初の行が有効。
   - `alpha` は 125 Hz〜4 kHz の 6 バンド (吸音表と同じ)。足りないバンドは
     最後の値で外挿、省略は剛体のまま。
   - 行の無い障害物は剛体。**キー自体を省略すれば従来動作と完全一致**。
   - 各行の他のキー (`name`、幾何音響の `scattering`、`enabled` …) は
     読む側が理解しなければ無視される (ADR-0003)。GUI はそれらを保持して
     書き戻す (GUI 経由の保存で幾何音響の散乱係数を落とさない)。
2. **両ソルバーの意味**:
   - FDTD — 材質を持つ障害物と空気の界面を、外壁と**同じ**半陰的
     インピーダンス境界にする (実効 α は有効帯域と重なるバンドの平均、
     角度依存吸音 (ADR-0011) なら同じ ζ)。界面と最寄りの圧力節点の間が
     外壁と同じ半セルなので、同じ位置の外壁とビット単位で同じ時間発展に
     なる。
   - 幾何音響 — 障害物の 6 面の鏡面反射・レイの反射に使う (バンド別)。
   - 透過は扱わない (障害物の向こうへ音は届かない)。
3. **幾何音響専用の `acoustic.ga.obstacles[]` は残す** (改名・削除しない —
   絶対規則 1)。同じ geometry の行が両方にあれば、幾何音響ソルバーでは
   `acoustic.ga` 側が優先する (幾何音響だけに材質を与えていた既存入力の
   後方互換。JSON の並び順に依らない)。FDTD は従来どおり `acoustic.ga` を
   読まない。GUI は `acoustic` 直下だけを書く。
4. **番号の追随**: 材質は geometry を番号で指すので、GUI で geometry 行を
   挿入・削除したら番号を追随させる (`AcousticOpts::geometryInserted` /
   `geometryRemoved` — 削除した障害物の材質は捨てる)。番号がずれたまま
   保存すると、材質が別の障害物へ**静かに移る**ため。
5. **metadata.json は追加キーのみ**: FDTD は `geometry_voxels` の各要素に
   `boundary_alpha` / `boundary_zeta` / `impedance_faces` (空気との界面数)。
   幾何音響は既存の `obstacle_materials`。ファイル契約 (ADR-0007) は不変。
6. **ソルバー側の番人** (OpenAcoustics `acoustic_check.sh`):
   - FDTD (o) — 管の端を α = 0.3 の障害物で塞ぐと、その面の反射係数が
     √0.7 と ±3%、rir.wav が同じ位置の外壁 (tube_a03) とバイト一致、角度
     依存吸音でも外壁版とバイト一致、キーなしは剛体 (R = 1)、
     `acoustic.ga.obstacles` だけなら剛体とバイト一致、不正な番号は非零終了。
   - 幾何音響 (J) — `acoustic` 直下に書いても `acoustic.ga` 側と rir.wav が
     バイト一致、両方あれば `acoustic.ga` 側が優先 (バイト一致)。

## Consequences

- (+) 既存フローは無変更 (opt-in、キー省略時は従来動作と完全一致)。
  ADR-0003 の未知キー無視により、旧版 GUI / 旧版ソルバーとも共存できる。
- (+) 表 1 つで両ソルバーに同時に効く (単発実行・ハイブリッド実行とも同じ
  `prepareRunInput` 経路でサイドカーを書く)。
- (−) 材質は吸音率のみ (帯域内で周波数依存のインピーダンス・透過・
  拡張反応は範囲外 — ADR-0011 Consequences と同じ)。
- (−) 幾何音響は障害物を外接箱 (AABB) で近似したまま (曲面は鏡像法の
  平面反射面集合に入らない)。FDTD は全 shape を形状どおりにボクセル化する
  (OpenAcoustics PR #9)。
