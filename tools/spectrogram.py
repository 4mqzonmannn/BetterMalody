#!/usr/bin/env python3
"""譜面の音源から Malody 向けスペクトログラム(縦=時間(上へ進む) / 横=周波数(左=低音))を生成する試作。
使い方: spectrogram.py <音源.ogg> <譜面.mc> <出力.png> [--beat0 B --beats N] [--rpb 行/拍] [--bands 96]
  --beat0/--beats を省略すると曲全体(概観)。 .mc の BPM リストと sound の offset で 拍→時間 を対応づける。
依存: numpy, PIL, ffmpeg"""
import argparse, json, subprocess, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont

SR = 44100

def decode(path):
    p = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-f", "f32le", "-ac", "1", "-ar", str(SR), "-"], capture_output=True, check=True)
    return np.frombuffer(p.stdout, dtype=np.float32)

def stft_db(x, n_fft=2048, hop=256):
    win = np.hanning(n_fft).astype(np.float32)
    n = 1 + max(0, (len(x) - n_fft)) // hop
    idx = np.arange(n_fft)[None, :] + hop * np.arange(n)[:, None]
    # 分割して処理(メモリ節約)
    out = np.empty((n, n_fft // 2 + 1), dtype=np.float32)
    B = 2048
    for i in range(0, n, B):
        fr = x[idx[i:i + B]] * win
        out[i:i + B] = np.abs(np.fft.rfft(fr, axis=1))
    return out, hop

def log_bands(mag, bands=96, fmin=50.0, fmax=14000.0, n_fft=2048):
    """線形ビン → 対数周波数の帯域。帯域内に複数ビンがあれば最大値、無ければ隣接ビン間を補間(低域の欠けを防ぐ)。
    mag: (frames, bins) 振幅。戻り: (frames, bands) の振幅"""
    freqs = np.fft.rfftfreq(n_fft, 1.0 / SR)
    edges = np.geomspace(fmin, fmax, bands + 1)
    out = np.empty((mag.shape[0], bands), dtype=np.float32)
    for b in range(bands):
        lo, hi = edges[b], edges[b + 1]
        idx = np.nonzero((freqs >= lo) & (freqs < hi))[0]
        if len(idx) >= 1:
            out[:, b] = mag[:, idx].max(axis=1)
        else:                                   # 帯域がビン間に入る: 中心周波数で線形補間
            c = np.sqrt(lo * hi)
            j = int(np.clip(np.searchsorted(freqs, c) - 1, 0, len(freqs) - 2))
            w = (c - freqs[j]) / (freqs[j + 1] - freqs[j])
            out[:, b] = (1 - w) * mag[:, j] + w * mag[:, j + 1]
    return out

CMAP = [(0.00, (0, 0, 0)), (0.20, (20, 0, 70)), (0.40, (140, 10, 90)), (0.60, (230, 60, 20)), (0.80, (255, 160, 20)), (1.00, (255, 255, 200))]
def colorize(v):               # v: 0..1
    xs = np.array([c[0] for c in CMAP]); out = np.zeros(v.shape + (3,), dtype=np.uint8)
    for ch in range(3):
        out[..., ch] = np.interp(v, xs, [c[1][ch] for c in CMAP]).astype(np.uint8)
    return out

class Timing:
    def __init__(self, mc):
        d = json.load(open(mc))
        self.offset = 0
        for n in d["note"]:
            if n.get("type") == 1: self.offset = n.get("offset", 0)
        pts = []
        for t in d["time"]:
            b = t["beat"]; pts.append((b[0] + b[1] / b[2], 60000.0 / t["bpm"]))
        pts.sort()
        self.b = [p[0] for p in pts]; self.len = [p[1] for p in pts]
        self.t = [0.0]
        for i in range(1, len(pts)):
            self.t.append(self.t[-1] + (self.b[i] - self.b[i - 1]) * self.len[i - 1])
        self.meta = d["meta"]
    def time_ms(self, beat):   # 拍 → 音源上の時刻(ms)。 Malody: 譜面0拍 = 音源の offset ms 目
        i = max(0, np.searchsorted(self.b, beat, side="right") - 1)
        return self.offset + self.t[i] + (beat - self.b[i]) * self.len[i]
    def beat_at(self, ms):
        ms -= self.offset
        i = max(0, np.searchsorted(self.t, ms, side="right") - 1)
        return self.b[i] + (ms - self.t[i]) / self.len[i]

def render(spec_db, hop, tm, beat0, beats, rpb, bw, lo_db, hi_db):
    H = int(beats * rpb)
    rows = np.arange(H)
    beat_of_row = beat0 + (rows + 0.5) / rpb
    ms = np.array([tm.time_ms(b) for b in beat_of_row])
    fr = ms / 1000.0 * SR / hop
    f0 = np.clip(np.floor(fr).astype(int), 0, spec_db.shape[0] - 1)
    f1 = np.clip(f0 + 1, 0, spec_db.shape[0] - 1)
    w = (fr - np.floor(fr))[:, None]
    # 1行が複数フレームにまたがるとき(rpb が小さい)は最大値で畳み込む
    ms_next = np.array([tm.time_ms(b + 1.0 / rpb) for b in beat_of_row])
    span = np.maximum(1, ((ms_next - ms) / 1000.0 * SR / hop).astype(int))
    img = np.empty((H, spec_db.shape[1]), dtype=np.float32)
    for i in range(H):
        a = f0[i]; b = min(spec_db.shape[0], a + span[i])
        img[i] = spec_db[a:b].max(axis=0) if b - a > 1 else (1 - w[i]) * spec_db[f0[i]] + w[i] * spec_db[f1[i]]
    v = np.clip((img - lo_db) / (hi_db - lo_db), 0, 1)
    rgb = colorize(v)
    rgb = np.repeat(rgb, bw, axis=1)            # 周波数方向を bw 倍に
    im = Image.fromarray(rgb[::-1])              # 時間が上へ進む(Malody と同じ向き)
    return im

def offset_check(db, hop, tm, beat0, beat1, grid=4):
    """強いアタックが拍グリッド(1/grid 拍)からどれだけズレているかを集計(ms)。 +は譜面グリッドより音が遅い。"""
    flux = np.maximum(0, np.diff(db, axis=0)).sum(axis=1)
    flux = flux / (flux.max() + 1e-9)
    thr = np.percentile(flux, 93)
    pk = [i for i in range(1, len(flux) - 1) if flux[i] >= thr and flux[i] >= flux[i - 1] and flux[i] > flux[i + 1]]
    devs = []
    for i in pk:
        ms = (i + 1) * hop / SR * 1000
        b = tm.beat_at(ms)
        if not (beat0 <= b < beat1): continue
        g = round(b * grid) / grid
        devs.append((tm.time_ms(b) - tm.time_ms(g)) * 1.0)    # 音の時刻 - 最寄りグリッドの時刻(ms)
    devs = np.array(devs)
    if len(devs) < 8: return None
    hist, edges = np.histogram(devs, bins=np.arange(-60, 61, 10))
    return dict(n=len(devs), median=float(np.median(devs)), mean=float(devs.mean()), std=float(devs.std()), hist=list(zip(edges[:-1].astype(int), hist)))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ogg"); ap.add_argument("mc"); ap.add_argument("out")
    ap.add_argument("--beat0", type=float, default=None); ap.add_argument("--beats", type=float, default=None)
    ap.add_argument("--rpb", type=int, default=None); ap.add_argument("--bands", type=int, default=96)
    ap.add_argument("--bw", type=int, default=4); ap.add_argument("--range", type=float, default=70.0)
    a = ap.parse_args()
    x = decode(a.ogg)
    tm = Timing(a.mc)
    mag, hop = stft_db(x)
    bands = log_bands(mag, a.bands)
    db = 20 * np.log10(bands + 1e-6)
    hi = np.percentile(db, 99.5); lo = hi - a.range
    dur_ms = len(x) / SR * 1000
    last_beat = tm.beat_at(dur_ms)
    if a.beat0 is None:
        beat0, beats, rpb = 0.0, float(last_beat), a.rpb or 6
    else:
        beat0, beats, rpb = a.beat0, a.beats or 16, a.rpb or 48
    oc = offset_check(db, hop, tm, beat0, beat0 + beats)
    if oc:
        print(f"offset check: n={oc['n']} median={oc['median']:+.1f}ms mean={oc['mean']:+.1f}ms std={oc['std']:.1f}ms")
        print("  ズレ分布(ms: 件数):", " ".join(f"{e}:{h}" for e, h in oc["hist"]))
    im = render(db, hop, tm, beat0, beats, rpb, a.bw, lo, hi)
    d = ImageDraw.Draw(im)
    H = im.height
    for k in range(int(np.floor(beat0)), int(np.ceil(beat0 + beats)) + 1):
        y = H - (k - beat0) * rpb
        if 0 <= y < H:
            d.line([(0, y), (im.width, y)], fill=(255, 255, 255, 90), width=1)
            if rpb >= 24 or k % 8 == 0: d.text((2, y - 11), str(k), fill=(255, 255, 255))
    im.save(a.out)
    print(f"{a.out}: {im.size[0]}x{im.size[1]} beats {beat0:.1f}..{beat0+beats:.1f} (曲長 {dur_ms/1000:.1f}s, 最終拍≈{last_beat:.1f}) range {lo:.0f}..{hi:.0f}dB")

if __name__ == "__main__":
    main()
