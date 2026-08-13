# tools/amp_sim.py - lightweight amp/cabinet + Neve-ish coloration for demo takes.
# Makes a DI guitar take sound like it went through a clean-ish amp + 1x12 cab
# and a Neve-style preamp/console channel:
#   soft clip (gentle drive) -> cab EQ -> Neve coloration
#   (transformer even-harmonic saturation + 1073-style EQ) -> output.
# Usage: python amp_sim.py <in.wav> <out.wav> [drive] [cab] [neve]
#   drive  0.0..1.0  soft-clip amount (0.4 = clean-ish, 0.8 = pushed)
#   cab    0.0..1.0  cabinet tone amount (0 = flat, 1 = full cab voicing)
#   neve   0.0..1.0  Neve channel coloration (0 = none, 0.6 = console sheen)
#   pos    pre|post  neve before cab (drive-like) or after cab (console, default)
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

def shelf_sos(sr, f0, gain_db, high):
    # RBJ low/high shelf (S=1) -> SOS row, a0 normalized to 1
    A = 10.0 ** (gain_db / 40.0)
    w0 = 2.0 * np.pi * f0 / sr
    alpha = np.sin(w0) * np.sqrt(2.0) / 2.0
    c = np.cos(w0); sA = 2.0 * np.sqrt(A) * alpha
    if not high:  # low shelf
        b0 = A * ((A + 1.0) - (A - 1.0) * c + sA)
        b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * c)
        b2 = A * ((A + 1.0) - (A - 1.0) * c - sA)
        a0 = (A + 1.0) + (A - 1.0) * c + sA
        a1 = -2.0 * ((A - 1.0) + (A + 1.0) * c)
        a2 = (A + 1.0) + (A - 1.0) * c - sA
    else:  # high shelf
        b0 = A * ((A + 1.0) + (A - 1.0) * c + sA)
        b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * c)
        b2 = A * ((A + 1.0) + (A - 1.0) * c - sA)
        a0 = (A + 1.0) - (A - 1.0) * c + sA
        a1 = 2.0 * ((A - 1.0) - (A + 1.0) * c)
        a2 = (A + 1.0) - (A - 1.0) * c - sA
    return np.array([[b0 / a0, b1 / a0, b2 / a0, 1.0, a1 / a0, a2 / a0]])

def cab_eq(x, sr, cab):
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

def neve_coloration(x, sr, amount):
    # Transformer-style even-harmonic saturation + 1073-ish channel EQ.
    # amount 0 = bypass. The saturation runs on a normalized band so the
    # harmonic character stays consistent regardless of input level.
    if amount <= 0.0:
        return x
    # even-harmonic saturation: asymmetric tanh blend (x^2-ish warm density)
    pk = np.abs(x).max() + 1e-9
    n = x / pk
    g = 1.0 + 0.9 * amount                      # pre-gain into saturation
    sat = np.tanh(n * g + 0.25 * amount * np.tanh(n * g * 1.6))  # asymmetric
    sat = sat / np.tanh(g * 1.0 + 0.25 * amount * 1.0)           # normalize head
    # remove any DC the asymmetry introduced
    sat = sat - np.mean(sat, axis=0, keepdims=True)
    # 1073-style tone: LF shelf +2.5dB @110Hz, mid bell +1.5dB @700Hz Q~0.7,
    # HF shelf +1dB @12kHz (Neve highs are smooth, never harsh)
    sos_lf = shelf_sos(sr, 110.0, 2.5, False)
    sos_mid = peaking_sos(sr, 700.0, 0.7, 1.5)
    sos_hf = shelf_sos(sr, 12000.0, 1.0, True)
    tone = signal.sosfilt(sos_lf, sat, axis=0)
    tone = signal.sosfilt(sos_mid, tone, axis=0)
    tone = signal.sosfilt(sos_hf, tone, axis=0)
    # keep the processed level comparable to the input
    tone_pk = np.abs(tone).max() + 1e-9
    tone *= pk / tone_pk
    # blend: amount drives how much of the saturation+EQ replaces the dry
    return x * (1.0 - amount) + tone * amount

def main():
    if len(sys.argv) < 3:
        print('usage: python amp_sim.py <in.wav> <out.wav> [drive=0.5] [cab=0.85] [neve=0.5] [pos=post]')
        return 1
    drive = float(sys.argv[3]) if len(sys.argv) > 3 else 0.5
    cab = float(sys.argv[4]) if len(sys.argv) > 4 else 0.85
    neve = float(sys.argv[5]) if len(sys.argv) > 5 else 0.5
    pos = sys.argv[6] if len(sys.argv) > 6 else 'post'
    d, sr = read_wav(sys.argv[1])
    out = soft_clip(d, drive)
    if pos == 'pre':
        out = neve_coloration(out, sr, neve)   # amp -> neve -> cab
        out = cab_eq(out, sr, cab)
    else:
        out = cab_eq(out, sr, cab)             # amp -> cab -> neve (console)
        out = neve_coloration(out, sr, neve)
    pk = np.abs(out).max()
    if pk > 0.95:
        out *= 0.95 / pk
    write_wav(sys.argv[2], out, sr)
    print(f'{sys.argv[2]}: peak={np.abs(out).max():.3f} drive={drive} cab={cab} neve={neve}')
    return 0

if __name__ == '__main__':
    sys.exit(main())
