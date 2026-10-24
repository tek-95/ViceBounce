from pathlib import Path
import sys, tempfile, unittest, json
sys.path.insert(0,str(Path(__file__).resolve().parents[3]/'work'/'deps'/'audio'))
import numpy as np
import soundfile as sf
from beat_analysis import analyze, onset_envelope, fit_tempo
from import_music import import_track, read_ini, update_ini
import import_music
from unittest.mock import patch, Mock

RATE=22050
def signal(beats,duration):
    audio=np.zeros(int(duration*RATE),dtype=np.float32)
    for beat in beats:
        start=int(beat*RATE);t=np.arange(int(.12*RATE))/RATE
        click=.8*np.sin(2*np.pi*75*t)*np.exp(-t*45)
        count=min(len(click),len(audio)-start)
        if start>=0 and count>0: audio[start:start+count]+=click[:count]
    return audio
def analyze_signal(beats,duration):
    return analyze(signal(beats,duration)[::2],RATE/2,duration)
def beat_error(predicted,reference):
    predicted=np.array(predicted)/1000
    return np.array([np.min(np.abs(predicted-beat)) for beat in reference if predicted[0]+.1<beat<predicted[-1]-.1])

class AnalysisTests(unittest.TestCase):
    def test_percussion_grid_survives_irregular_bass(self):
        rate, duration, bpm, offset = 11025, 32, 134, .015
        audio = np.zeros(rate * duration, dtype=np.float32)
        # Irregular low attacks hide tempo from the bass-only hypotheses.
        for beat in np.random.default_rng(72).uniform(.2, duration - .2, 180):
            t = np.arange(int(.12 * rate)) / rate; start = int(beat * rate)
            audio[start:start + len(t)] += np.sin(2 * np.pi * 75 * t) * np.exp(-45 * t)
        expected = np.arange(offset, duration - .05, 60 / bpm)
        for beat in expected:
            t = np.arange(int(.04 * rate)) / rate; start = int(beat * rate)
            audio[start:start + len(t)] += .2 * np.sin(2 * np.pi * 3500 * t) * np.exp(-100 * t)
        result = analyze(audio, rate, duration)
        self.assertAlmostEqual(result['bpm'], bpm, delta=.05)
        self.assertEqual(result['mode'], 'measured-grid')
        self.assertLess(float(np.median(beat_error(result['beats_ms'], expected))), .025)
    def test_syncopated_sub_bass_does_not_replace_the_kick_grid(self):
        rate, duration = 11025, 30
        audio = np.zeros(rate * duration, dtype=np.float32)
        expected = np.arange(.37, duration - .2, .48)
        # Quiet sub-bass attacks at 3/2 the kick rate fool log compression.
        for frequency, amplitude, period in [(120, 1., .48), (50, .4, .32)]:
            for beat in np.arange(.37, duration - .2, period):
                t = np.arange(int(.12 * rate)) / rate
                start = int(beat * rate)
                audio[start:start + len(t)] += amplitude * np.sin(2 * np.pi * frequency * t) * np.exp(-45 * t)
        envelope, times, _ = onset_envelope(audio, rate)
        wrong, confidence = fit_tempo(envelope, times, np.arange(60, 200.01, .25))
        self.assertAlmostEqual(wrong, 187.5)
        self.assertLess(confidence, .45)
        result = analyze(audio, rate, duration)
        self.assertAlmostEqual(result['bpm'], 125, delta=.05)
        self.assertEqual(result['mode'], 'measured-grid')
        self.assertLess(float(np.median(beat_error(result['beats_ms'], expected))), .035)
    def test_shifted_and_padded_tracks(self):
        for bpm,offset in [(120,.37),(135,1.243),(148,.81)]:
            expected=np.arange(offset,25,60/bpm)
            result=analyze_signal(expected,28)
            self.assertLess(abs(result['bpm']-bpm),.15,result)
            self.assertLess(float(np.median(beat_error(result['beats_ms'],expected))),.035)
    def test_changing_tempo(self):
        beats=[];t=.37
        while t<40:
            beats.append(t);t+=60/(110+35*t/40)
        result=analyze_signal(beats,42)
        error=beat_error(result['beats_ms'],beats)
        self.assertLess(float(np.median(error)),.04,result)
        self.assertLess(float(np.percentile(error,95)),.1,result)
    def test_silence_is_rejected(self):
        with self.assertRaises(ValueError): analyze(np.zeros(44100),11025,4)

class ImportTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
        self.scripts=self.root/'scripts';self.scripts.mkdir()
        (self.scripts/'ViceBounce.ini').write_text('[ViceBounce]\n; Keep me\nAmplitude=0.275\nAllPeople=1\n[Other]\nBPM=999\n')
        self.beats=np.arange(.37,14,.5);self.audio=signal(self.beats,16)
    def tearDown(self): self.temp.cleanup()
    def test_formats_unicode_cache_and_replacement(self):
        wav=self.root/'песня с паузой.wav';flac=self.root/'другой формат.flac'
        pcm=(self.audio*32767).astype(np.int16)
        sf.write(wav,pcm,RATE,subtype='PCM_16');sf.write(flac,pcm,RATE,subtype='PCM_16')
        one=import_track(wav,self.scripts);two=import_track(flac,self.scripts)
        self.assertNotEqual(one['source_sha256'],two['source_sha256'])
        self.assertEqual(one['wav_sha256'],two['wav_sha256'])
        self.assertEqual(one['beats_sha256'],two['beats_sha256'])
        self.assertTrue(two['use_beat_map'])
        update_ini(self.scripts/'ViceBounce.ini',dict(MusicSyncOffsetMs='27.5'))
        again=import_track(flac,self.scripts)
        self.assertTrue(again['cached']);self.assertTrue(again['unchanged'])
        text=read_ini(self.scripts/'ViceBounce.ini')
        self.assertIn('Amplitude=0.275',text);self.assertIn('; Keep me',text)
        self.assertIn('MusicSyncOffsetMs=27.5',text);self.assertIn('[Other]\nBPM=999',text.replace('\r\n','\n'))
        alias=self.root/'renamed.flac';alias.write_bytes(flac.read_bytes())
        renamed=import_track(alias,self.scripts)
        self.assertTrue(renamed['cached']);self.assertIn('MusicSyncOffsetMs=27.5',read_ini(self.scripts/'ViceBounce.ini'))
        sf.write(flac,signal(np.arange(.8,14,60/140),16),RATE,subtype='PCM_16')
        replaced=import_track(flac,self.scripts)
        self.assertFalse(replaced['cached']);self.assertNotEqual(two['source_sha256'],replaced['source_sha256'])
        self.assertLess(abs(replaced['bpm']-140),.2)
    def test_bad_source_keeps_previous_configuration(self):
        bad=self.root/'broken.mp3';bad.write_bytes(b'not audio')
        original=(self.scripts/'ViceBounce.ini').read_bytes()
        with self.assertRaises(Exception): import_track(bad,self.scripts)
        self.assertEqual(original,(self.scripts/'ViceBounce.ini').read_bytes())
    def test_manual_override(self):
        wav=self.root/'beat.wav';sf.write(wav,self.audio,RATE)
        result=import_track(wav,self.scripts,bpm=120,offset_ms=370)
        self.assertEqual(result['mode'],'manual-grid');self.assertAlmostEqual(result['first_beat_ms'],370)
    def test_manual_pulse_persists_across_other_tracks_and_renames(self):
        wav=self.root/'half-time.wav';sf.write(wav,self.audio,RATE)
        chosen=import_track(wav,self.scripts,bpm=60,offset_ms=370)
        other=self.root/'other.wav';sf.write(other,signal(np.arange(.2,14,60/140),16),RATE)
        import_track(other,self.scripts)
        renamed=self.root/'same-content.wav';renamed.write_bytes(wav.read_bytes())
        restored=import_track(renamed,self.scripts)
        self.assertEqual(restored['bpm'],60)
        self.assertEqual(restored['first_beat_ms'],370)
        self.assertEqual(restored['beat_count'],chosen['beat_count'])
        self.assertEqual(restored['mode'],'manual-grid')
        changed=import_track(renamed,self.scripts,bpm=120)
        self.assertEqual(changed['bpm'],120)

class PickerLifecycleTests(unittest.TestCase):
    def run_picker(self, selection='song.mp3', error=None):
        with tempfile.TemporaryDirectory() as folder:
            user = Mock()
            events = []
            user.ShowWindowAsync.side_effect = lambda handle, mode: events.append(mode)
            def imported(*args):
                self.assertEqual(events, [6, 9])
                user.SetForegroundWindow.assert_called_once_with(123)
                return dict(bpm=35, beat_count=10, use_beat_map=True)
            with patch.object(sys, 'argv', ['import_music', '--scripts-dir', folder,
                                           '--game-window', '123', '--game-pid', '456']), \
                 patch.object(import_music, 'game_window', return_value=(user, 123)), \
                 patch.object(import_music, 'pick_file', return_value=selection, side_effect=error), \
                 patch.object(import_music, 'import_track', side_effect=imported) as importer, \
                 patch.object(import_music.ctypes.windll.user32, 'MessageBoxW') as message:
                result = import_music.main()
                message.assert_not_called()
                self.assertEqual(events, [6, 9])
                user.SetForegroundWindow.assert_called_once_with(123)
                self.assertEqual(importer.call_count, 1 if selection and not error else 0)
                return result

    def test_game_restored_before_analysis_and_no_completion_modal(self):
        self.assertEqual(self.run_picker(), 0)

    def test_cancel_restores_game_without_blocking_next_f11(self):
        self.assertEqual(self.run_picker(selection=None), 2)

    def test_picker_error_restores_game_and_exits_without_hidden_modal(self):
        self.assertEqual(self.run_picker(error=OSError('picker failed')), 1)

if __name__=='__main__': unittest.main(verbosity=2)
