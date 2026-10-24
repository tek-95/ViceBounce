"""ViceBounce music importer. Packaged as a standalone Windows executable."""
from pathlib import Path
import argparse, ctypes, hashlib, json, os, re, sys, tempfile, time, traceback
if not getattr(sys, 'frozen', False):
    sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'work' / 'deps' / 'audio'))
import numpy as np
import soundfile as sf
from beat_analysis import analyze, ANALYSIS_VERSION

def sha256(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()

def atomic_bytes(path, data):
    path = Path(path); path.parent.mkdir(parents=True, exist_ok=True)
    handle, temporary = tempfile.mkstemp(prefix='.vicebounce-', dir=path.parent)
    try:
        with os.fdopen(handle, 'wb') as stream:
            stream.write(data); stream.flush(); os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary): os.unlink(temporary)

def read_ini(path):
    data = path.read_bytes() if path.exists() else b'[ViceBounce]\r\n'
    if data.startswith((b'\xff\xfe', b'\xfe\xff')): return data.decode('utf-16')
    try: return data.decode('utf-8-sig')
    except UnicodeDecodeError: return data.decode('mbcs' if os.name == 'nt' else 'latin1')

def update_ini(path, values):
    text = read_ini(path)
    section = re.search(r'(?im)^\[ViceBounce\][ \t]*\r?$', text)
    if not section:
        text += '\r\n[ViceBounce]\r\n'; section = re.search(r'(?im)^\[ViceBounce\][ \t]*\r?$', text)
    start = section.end(); next_section = re.search(r'(?m)^\[', text[start:])
    end = start + next_section.start() if next_section else len(text)
    body = text[start:end]
    for key, value in values.items():
        line = f'{key}={value}'
        pattern = rf'(?im)^{re.escape(key)}[ \t]*=.*\r?$'
        if re.search(pattern, body): body = re.sub(pattern, lambda _: line, body)
        else: body = body.rstrip() + '\r\n' + line + '\r\n'
    new = text[:start] + body + text[end:]
    if new == text: return False
    if path.exists(): atomic_bytes(path.with_suffix('.ini.before-music'), path.read_bytes())
    atomic_bytes(path, new.encode('utf-16'))
    return True

def decode(source, destination):
    chunks = []; count = 0
    with sf.SoundFile(source) as reader:
        if reader.frames / reader.samplerate > 1800:
            raise ValueError('Tracks longer than 30 minutes are not supported by this importer.')
        if reader.samplerate < 8000 or reader.samplerate > 192000:
            raise ValueError('Supported sample rates: 8000 to 192000 Hz.')
        rate = reader.samplerate; channels = min(reader.channels, 2)
        stride = max(1, round(rate / 11025))
        with sf.SoundFile(destination, 'w', samplerate=rate, channels=channels, subtype='PCM_16', format='WAV') as writer:
            while True:
                block = reader.read(32768, dtype='float32', always_2d=True)
                if not len(block): break
                if not np.isfinite(block).all(): raise ValueError('Audio contains invalid sample values.')
                writer.write(block[:, :channels])
                mono = block[:, :channels].mean(axis=1)
                chunks.append(mono[(-count) % stride::stride]); count += len(block)
    if count < rate: raise ValueError('Track must be at least one second long.')
    return np.concatenate(chunks), rate / stride, count / rate, rate, channels

def import_track(source, scripts, bpm=None, offset_ms=None, force=False):
    source = Path(source).resolve(); scripts = Path(scripts).resolve()
    if not source.is_file(): raise ValueError('Music file does not exist: ' + str(source))
    digest = sha256(source)
    # Manual pulse choices belong to a file's content, not only the currently
    # selected INI entry. Keep them across track switches, renames and analyses.
    overrides_path = scripts / 'ViceBounceMusic' / 'overrides.json'
    explicit_override = bpm is not None or offset_ms is not None
    overrides = {}
    if overrides_path.exists():
        overrides = json.loads(overrides_path.read_text('utf-8'))
        if not isinstance(overrides, dict): raise ValueError('Invalid music overrides file.')
    saved = overrides.get(digest, {})
    if not isinstance(saved, dict): raise ValueError('Invalid saved track override.')
    if bpm is None: bpm = saved.get('bpm')
    if offset_ms is None: offset_ms = saved.get('offset_ms')
    if bpm is not None and (not np.isfinite(bpm) or not 30 <= bpm <= 240): raise ValueError('Invalid saved track BPM.')
    if offset_ms is not None and (not np.isfinite(offset_ms) or abs(offset_ms) > 3600000): raise ValueError('Invalid saved track offset.')
    settings = json.dumps([ANALYSIS_VERSION, bpm, offset_ms]).encode()
    variant = hashlib.sha256(settings).hexdigest()[:8]
    folder = scripts / 'ViceBounceMusic' / (digest[:24] + '-' + variant)
    if force: folder = folder.with_name(folder.name + '-' + str(time.time_ns())[-8:])
    folder.mkdir(parents=True, exist_ok=True)
    wav, beats, metadata = folder / 'audio.wav', folder / 'beats.txt', folder / 'profile.json'
    cached = False
    try:
        report = json.loads(metadata.read_text('utf-8'))
        cached = (not force and report['source_sha256'] == digest and
                  report['analysis_version'] == ANALYSIS_VERSION and
                  sha256(wav) == report['wav_sha256'] and sha256(beats) == report['beats_sha256'])
    except (OSError, ValueError, KeyError): pass
    if not cached:
        temporary = folder / ('audio-' + str(os.getpid()) + '.wav')
        try:
            mono, rate, duration, sample_rate, channels = decode(source, temporary)
            analysis = analyze(mono, rate, duration, bpm, offset_ms)
            if sha256(source) != digest: raise ValueError('Source changed during import; retry when the file has finished copying.')
            beat_times = analysis.pop('beats_ms')
            beat_text = 'ViceBounceBeats 1\n' + '\n'.join(f'{beat:.4f}' for beat in beat_times) + '\n'
            os.replace(temporary, wav)
            atomic_bytes(beats, beat_text.encode('ascii'))
            report = dict(analysis_version=ANALYSIS_VERSION, source_sha256=digest, source_name=source.name,
                          duration_seconds=duration, sample_rate=sample_rate, channels=channels,
                          manual_bpm=bpm, manual_offset_ms=offset_ms,
                          first_beat_ms=beat_times[0], beat_count=len(beat_times),
                          wav_sha256=sha256(wav), beats_sha256=sha256(beats), **analysis)
            atomic_bytes(metadata, json.dumps(report, indent=2).encode('utf-8'))
        finally:
            temporary.unlink(missing_ok=True)
    use_map = report['confidence'] >= .2 or report['mode'] == 'manual-grid'
    values = dict(MusicEnabled='1', MusicFile=str(wav.relative_to(scripts)),
                  MusicBeatMapFile=str(beats.relative_to(scripts)), MusicUseBeatMap='1' if use_map else '0',
                  MusicSourceFile=str(source), MusicSourceSHA256=digest, MusicAutoImport='1',
                  MusicImportBPM='' if bpm is None else str(bpm),
                  MusicImportOffsetMs='' if offset_ms is None else str(offset_ms),
                  MusicFirstBeatMs=f"{report['first_beat_ms']:.4f}", MusicSyncOffsetMs='0', BPM=f"{report['bpm']:.4f}")
    # Preserve a user's calibrated offset and manual overrides when a startup
    # check finds the same selected file and an intact cache.
    current = read_ini(scripts / 'ViceBounce.ini')
    current_hash = re.search(r'(?im)^MusicSourceSHA256\s*=\s*([0-9a-f]+)', current)
    current_file = re.search(r'(?im)^MusicFile\s*=\s*(.+)', current)
    current_source = re.search(r'(?im)^MusicSourceFile\s*=\s*(.+)', current)
    same_profile = (cached and current_hash and current_hash[1] == digest and current_file and
                    current_file[1].strip() == str(wav.relative_to(scripts)))
    unchanged = same_profile and current_source and current_source[1].strip() == str(source)
    if same_profile and not unchanged:
        update_ini(scripts / 'ViceBounce.ini', dict(MusicSourceFile=str(source)))
    elif not unchanged:
        update_ini(scripts / 'ViceBounce.ini', values)
    if explicit_override:
        overrides[digest] = dict(bpm=bpm, offset_ms=offset_ms)
        atomic_bytes(overrides_path, json.dumps(overrides, indent=2).encode('utf-8'))
    return dict(**report, cached=cached, unchanged=bool(unchanged), music_file=str(wav), use_beat_map=use_map)

def game_window(handle, process_id):
    """Accept only the live game window explicitly passed by the ASI."""
    from ctypes import wintypes as w
    user = ctypes.WinDLL('user32', use_last_error=True)
    user.IsWindow.argtypes = [w.HWND]
    user.GetWindowThreadProcessId.argtypes = [w.HWND, ctypes.POINTER(w.DWORD)]
    user.ShowWindowAsync.argtypes = [w.HWND, ctypes.c_int]
    user.SetForegroundWindow.argtypes = [w.HWND]
    owner = w.DWORD()
    if not handle or not process_id or not user.IsWindow(handle): return None
    user.GetWindowThreadProcessId(handle, ctypes.byref(owner))
    return (user, handle) if owner.value == process_id else None


def pick_file():
    from ctypes import wintypes as w
    class OFN(ctypes.Structure):
        _fields_ = [('size', w.DWORD), ('owner', w.HWND), ('instance', w.HINSTANCE), ('filter', w.LPCWSTR),
                    ('custom', w.LPWSTR), ('maxCustom', w.DWORD), ('filterIndex', w.DWORD), ('file', w.LPWSTR),
                    ('maxFile', w.DWORD), ('titleFile', w.LPWSTR), ('maxTitle', w.DWORD), ('initialDir', w.LPCWSTR),
                    ('title', w.LPCWSTR), ('flags', w.DWORD), ('fileOffset', w.WORD), ('fileExtension', w.WORD),
                    ('extension', w.LPCWSTR), ('data', ctypes.c_ssize_t), ('hook', ctypes.c_void_p),
                    ('template', w.LPCWSTR), ('reserved', ctypes.c_void_p), ('reserved2', w.DWORD), ('flagsEx', w.DWORD)]
    filename = ctypes.create_unicode_buffer(32768); request = OFN()
    request.size = ctypes.sizeof(OFN); request.file = ctypes.cast(filename, w.LPWSTR); request.maxFile = len(filename)
    # An unowned dialog can stay behind an exclusive-fullscreen game. The ASI
    # grants foreground permission; the init hook raises the Explorer dialog.
    user = ctypes.WinDLL('user32', use_last_error=True)
    user.GetParent.argtypes = [w.HWND]; user.GetParent.restype = w.HWND
    user.SetWindowPos.argtypes = [w.HWND, w.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, w.UINT]
    user.SetForegroundWindow.argtypes = [w.HWND]
    hook_type = ctypes.WINFUNCTYPE(ctypes.c_size_t, w.HWND, w.UINT, w.WPARAM, w.LPARAM)
    @hook_type
    def on_init(window, message, wp, lp):
        if message == 0x110:  # WM_INITDIALOG, hook child belongs to Explorer dialog
            dialog = user.GetParent(window) or window
            user.SetWindowPos(dialog, w.HWND(-1), 0, 0, 0, 0, 0x3)
            user.SetForegroundWindow(dialog)
        return 0
    request.hook = ctypes.cast(on_init, ctypes.c_void_p)
    request.title = 'ViceBounce — choose your music'
    request.filter = 'Music (MP3, FLAC, WAV, OGG)\0*.mp3;*.flac;*.wav;*.ogg\0All files\0*.*\0\0'
    request.flags = 0x80000 | 0x1000 | 0x800 | 0x8 | 0x20  # Explorer, existing file/path, no chdir, hook
    if not ctypes.windll.comdlg32.GetOpenFileNameW(ctypes.byref(request)):
        error = ctypes.windll.comdlg32.CommDlgExtendedError()
        if error: raise OSError('File picker error ' + str(error))
        return None
    return filename.value

def main():
    parser = argparse.ArgumentParser(description='Import your local music into ViceBounce. No downloads or uploads.')
    parser.add_argument('file', nargs='?'); parser.add_argument('--source')
    parser.add_argument('--scripts-dir', type=Path)
    parser.add_argument('--bpm', type=float); parser.add_argument('--offset-ms', type=float)
    parser.add_argument('--force', action='store_true'); parser.add_argument('--quiet', action='store_true')
    parser.add_argument('--if-changed', action='store_true'); parser.add_argument('--status-file', type=Path)
    parser.add_argument('--game-window', type=int, default=0, help=argparse.SUPPRESS)
    parser.add_argument('--game-pid', type=int, default=0, help=argparse.SUPPRESS)
    args = parser.parse_args()
    home = Path(sys.executable).parent if getattr(sys, 'frozen', False) else Path(__file__).resolve().parents[1]
    scripts = args.scripts_dir or home
    if args.bpm is not None and (not np.isfinite(args.bpm) or not 30 <= args.bpm <= 240): parser.error('BPM must be 30..240')
    if args.offset_ms is not None and (not np.isfinite(args.offset_ms) or abs(args.offset_ms) > 3600000): parser.error('Invalid beat offset')
    status = args.status_file or scripts / 'ViceBounceImportStatus.json'
    # The ASI waits for this process to exit before loading the new profile.
    # A success MessageBox here leaves F11 busy and the old music playing.
    interactive = not args.quiet and not args.game_pid
    try:
        source = args.source or args.file
        if not source:
            if args.quiet: raise ValueError('No source file was selected.')
            game = game_window(args.game_window, args.game_pid)
            if game: game[0].ShowWindowAsync(game[1], 6)  # SW_MINIMIZE releases fullscreen
            try:
                source = pick_file()
            finally:
                # Restore immediately after the picker, including Cancel/error.
                # Analysis must not keep the game minimized or steal focus later.
                if game and game_window(args.game_window, args.game_pid):
                    game[0].ShowWindowAsync(game[1], 9)
                    game[0].SetForegroundWindow(game[1])
            if not source: return 2
        if args.if_changed and args.bpm is None and args.offset_ms is None:
            current = read_ini(scripts / 'ViceBounce.ini')
            previous_hash = re.search(r'(?im)^MusicSourceSHA256\s*=\s*([0-9a-f]+)', current)
            if previous_hash and previous_hash[1] == sha256(source):
                for key, attribute in [('MusicImportBPM','bpm'),('MusicImportOffsetMs','offset_ms')]:
                    match = re.search(rf'(?im)^{key}[ \t]*=[ \t]*([^\r\n]*)', current)
                    if match and match[1].strip():
                        value=float(match[1])
                        limit_ok=30<=value<=240 if attribute=='bpm' else abs(value)<=3600000
                        if not np.isfinite(value) or not limit_ok: raise ValueError('Invalid saved manual music setting: '+key)
                        setattr(args,attribute,value)
        report = import_track(source, scripts, args.bpm, args.offset_ms, args.force)
        atomic_bytes(status, json.dumps(dict(ok=True, **report), indent=2).encode('utf-8'))
        if sys.stdout: print(json.dumps(report, indent=2))
        if interactive:
            message = (f"Ready. Tempo: {report['bpm']:.2f} BPM. Beats detected: {report['beat_count']}.\n"
                       'Return to the game. If you started the importer separately, press F7.\n'
                       'Press F9 on a beat to adjust alignment.')
            if not report['use_beat_map']: message += '\nBeat detection is uncertain: check the BPM and adjust alignment manually.'
            ctypes.windll.user32.MessageBoxW(None, message, 'ViceBounce — music ready', 0x40 | 0x50000)
        return 0
    except Exception as error:
        atomic_bytes(status, json.dumps(dict(ok=False, error=str(error)), indent=2).encode('utf-8'))
        if sys.stderr: traceback.print_exc()
        if interactive: ctypes.windll.user32.MessageBoxW(None, str(error), 'ViceBounce — music import failed', 0x10 | 0x50000)
        return 1

if __name__ == '__main__': raise SystemExit(main())
