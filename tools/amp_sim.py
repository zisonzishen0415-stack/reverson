# tools/amp_sim.py - lightweight amp/cabinet preprocessing for demo takes.
# Makes a DI guitar take sound like it went through a clean-ish amp + 1x12 cab:
#   soft clip (gentle drive) -> cab EQ (high-pass, low-mid body, presence,
#   high-frequency rolloff) -> output.
# Usage: python amp_sim.py <in.wav> <out.wav> [drive] [cab]
#   drive  0.0..1.0  soft-clip amount (0.4 = clean-ish, 0.8 = pushed)
#   cab    0.0..1.0  cabinet tone amount (0 = flat, 1 = full cab voicing)
import numpy as np, wave, sys
from scipy import signal

def read_wav(path):
    with wave.open(path, 'rb') as w:
        n, sr, ch, sw = w.getnframes(), w.getframerate(), w.getnchannels(), w.getsampwidth()
        d = np.frombuffer(w.readframes(n), dtype=np.int16).reshape(-1, ch).astype(np.float32) / 32768.0
    return d, sr

def write_wav(path, d, sr):
    d = np.clip(d, -1.0, 1.0)
    with wave.open(path, 'wb') as w:
        w.setnchannels(d.shape[1] if d.ndim > 1 else 1)
        w.setsampwidth(2); w.setframerate(sr)
        w.writeframes((d * 32767.0).astype(np.int16).tobytes())

def soft_clip(x, drive):
    # tanh-style smooth clipping; drive 0 = passthrough
    if drive <= 0.0:
        return x
    g = 1.0 + 3.0 * drive          # pre-gain into the clipper
    k = 0.5 + 0.5 * drive          # blend dry -> clipped
    y = np.tanh(x * g) / np.tanh(g) * (1.0 + 0.15 * drive)  # compensate level
    return x * (1.0 - k) + y * k

def peaking_sos(sr, f0, q, gain_db):
    # RBJ audio EQ cookbook peaking filter -> SOS row [b0,b1,b2,a0,a1,a2] (a0=1)
    A = 10.0 ** (gain_db / 40.0)
    w0 = 2.0 * np.pi * f0 / sr
    alpha = np.sin(w0) / (2.0 * q)
    b0 = 1.0 + alpha * A
    b1 = -2.0 * np.cos(w0)
    b2 = 1.0 - alpha * A
    a0 = 1.0 + alpha / A
    a1 = -2.0 * np.cos(w0)
    a2 = 1.0 - alpha / A
    return np.array([[b0 / a0, b1 / a0, b2 / a0, 1.0, a1 / a0, a2 / a0]])

def cab_eq(x, sr, cab):
    # fixed filter bank (clean amp + 1x12)
    sos_hp = signal.butter(2, 90.0, 'highpass', fs=sr, output='sos')   # kill DC/rumble
    sos_body = peaking_sos(sr, 180.0, 1.0, 3.0)                        # low-mid body +3dB
    sos_pres = peaking_sos(sr, 3200.0, 1.2, 3.5)                       # presence bump
    sos_lp = signal.butter(4, 7000.0, 'lowpass', fs=sr, output='sos')  # cab rolloff
    y = signal.sosfilt(sos_hp, x, axis=0)
    y = signal.sosfilt(sos_body, y, axis=0)
    y = signal.sosfilt(sos_pres, y, axis=0)
    y = signal.sosfilt(sos_lp, y, axis=0)
    pk = np.abs(y).max() + 1e-9
    pk_in = np.abs(x).max() + 1e-9
    y *= pk_in / pk
    return x * (1.0 - cab) + y * cab

def main():
    if len(sys.argv) < 3:
        print('usage: python amp_sim.py <in.wav> <out.wav> [drive=0.5] [cab=0.85]')
        return 1
    drive = float(sys.argv[3]) if len(sys.argv) > 3 else 0.5
    cab = float(sys.argv[4]) if len(sys.argv) > 4 else 0.85
    d, sr = read_wav(sys.argv[1])
    out = soft_clip(d, drive)
    out = cab_eq(out, sr, cab)
    pk = np.abs(out).max()
    if pk > 0.95:
        out *= 0.95 / pk
    write_wav(sys.argv[2], out, sr)
    print(f'{sys.argv[2]}: peak={np.abs(out).max():.3f} drive={drive} cab={cab}')
    return 0

if __name__ == '__main__':
    sys.exit(main())
