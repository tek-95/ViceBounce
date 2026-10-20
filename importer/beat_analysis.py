"""Local onset/tempo analysis. No network or artist-specific constants."""
import numpy as np

ANALYSIS_VERSION = 2

def onset_envelope(mono, rate):
    size, hop = 1024, 64
    if len(mono) < size * 4:
        raise ValueError('Track is too short for beat analysis (minimum about 1 second).')
    frames = np.lib.stride_tricks.sliding_window_view(mono, size)[::hop]
    frequencies = np.fft.rfftfreq(size, 1 / rate)
    bass = (frequencies >= 35) & (frequencies <= 200)
    mid = (frequencies >= 200) & (frequencies <= 3000)
    bands = []
    for start in range(0, len(frames), 512):
        spectrum = np.abs(np.fft.rfft(frames[start:start + 512] * np.hanning(size), axis=1))
        bands.append(np.column_stack(((spectrum[:, bass] ** 2).sum(axis=1),
                                      (spectrum[:, mid] ** 2).sum(axis=1))))
    energy = np.concatenate(bands)
    rises = np.maximum(0, np.diff(np.log1p(energy), axis=0, prepend=np.zeros((1, 2))))
    envelope = np.convolve(rises[:, 0] + .15 * rises[:, 1], [.25, .5, .25], mode='same')
    times = (np.arange(len(frames)) * hop + size / 2) / rate
    if float(np.max(envelope)) < 1e-5:
        raise ValueError('No rhythmic signal found. Silent tracks are not imported automatically.')
    return envelope, times, hop / rate

def fit_tempo(envelope, times, candidates):
    # Bound work even for long files. Sampling starts at the first actual sound,
    # not at a hardcoded offset, and never stretches or trims the recording.
    audible = np.flatnonzero(envelope > np.max(envelope) * .03)
    start = times[audible[0]] if len(audible) else times[0]
    mask = (times >= start) & (times < start + 120)
    t, weights = times[mask], envelope[mask]
    step = max(1, len(t) // 20000)
    t, weights = t[::step], weights[::step]
    scores = [abs(np.sum(weights * np.exp(2j * np.pi * t * bpm / 60))) / max(weights.sum(), 1e-12)
              for bpm in candidates]
    index = int(np.argmax(scores))
    return float(candidates[index]), float(scores[index])

def analyze(mono, rate, duration, manual_bpm=None, manual_offset_ms=None):
    envelope, times, dt = onset_envelope(mono, rate)
    if manual_bpm is None:
        coarse, _ = fit_tempo(envelope, times, np.arange(60, 200.01, .25))
        bpm, coherence = fit_tempo(envelope, times, np.arange(coarse - .3, coarse + .301, .01))
    else:
        bpm, coherence = fit_tempo(envelope, times, [manual_bpm])
    if manual_offset_ms is not None or manual_bpm is not None or coherence >= .45:
        z = np.sum(envelope * np.exp(2j * np.pi * times * bpm / 60))
        first = float((np.angle(z) % (2 * np.pi)) * 60 / bpm / (2 * np.pi))
        if manual_offset_ms is not None:
            first = manual_offset_ms / 1000
        beats = np.arange(first, duration, 60 / bpm)
        beats = beats[beats >= 0]
        if len(beats) < 2:
            raise ValueError('At least two beats must fit in the track.')
        mode = 'manual-grid' if manual_bpm is not None or manual_offset_ms is not None else 'measured-grid'
        return dict(bpm=round(bpm, 4), confidence=coherence, mode=mode, beats_ms=(beats * 1000).tolist())

    # Estimate local tempo around the global hypothesis. A changing tempo is
    # represented by actual beat timestamps, not forced into one fixed BPM.
    centers, local = [], []
    for center in np.arange(times[0], times[-1] + 6, 6):
        mask = (times >= center - 6) & (times <= center + 6)
        if envelope[mask].sum() < 1e-6:
            tempo = bpm
        else:
            candidates = np.arange(max(45, bpm * .72), min(240, bpm * 1.3) + .25, .25)
            tempo, _ = fit_tempo(envelope[mask], times[mask], candidates)
        centers.append(center); local.append(tempo)
    periods = 60 / np.interp(times, centers, local) / dt
    normalized = np.clip(envelope / max(float(np.percentile(envelope, 95)), 1e-7), 0, 3)
    score = np.zeros(len(times)); previous = np.full(len(times), -1, dtype=np.int32)
    for i, period in enumerate(periods):
        gaps = np.arange(max(1, int(period * .8)), int(period * 1.2) + 1)
        gaps = gaps[gaps <= i]
        emission = normalized[i] - .08
        if len(gaps):
            values = score[i - gaps] - 24 * np.log(gaps / period) ** 2
            best = int(np.argmax(values))
            if values[best] > 0:
                score[i] = emission + values[best]; previous[i] = i - gaps[best]
                continue
        score[i] = emission
    index = int(np.argmax(score)); indices = []
    while index >= 0:
        indices.append(index); index = int(previous[index])
    indices = np.array(indices[::-1], dtype=int)
    if len(indices) < 3:
        raise ValueError('Could not find a stable sequence of beats. Use --bpm and --offset-ms.')
    beats = times[indices]
    intervals = np.diff(beats)
    measured = float(60 / np.median(intervals))
    confidence = float(np.clip(np.mean(normalized[indices]) / 1.5, 0, 1))
    return dict(bpm=round(bpm, 4), measured_median_bpm=measured, confidence=confidence,
                tempo_coherence=coherence, mode='tracked-beats',
                local_bpm_min=float(np.min(60 / intervals)), local_bpm_max=float(np.max(60 / intervals)),
                beats_ms=np.round(beats * 1000, 4).tolist())
