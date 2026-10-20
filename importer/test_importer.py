from pathlib import Path
import sys, tempfile, unittest, json
import numpy as np
import soundfile as sf
from beat_analysis import analyze
from import_music import import_track, read_ini, update_ini
import lang, os

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
        (self.scripts/'SABounce.ini').write_text('[SABounce]\n; Keep me\nAmplitude=0.275\nAllPeople=1\n[Other]\nBPM=999\n')
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
        update_ini(self.scripts/'SABounce.ini',dict(MusicSyncOffsetMs='27.5'))
        again=import_track(flac,self.scripts)
        self.assertTrue(again['cached']);self.assertTrue(again['unchanged'])
        text=read_ini(self.scripts/'SABounce.ini')
        self.assertIn('Amplitude=0.275',text);self.assertIn('; Keep me',text)
        self.assertIn('MusicSyncOffsetMs=27.5',text);self.assertIn('[Other]\nBPM=999',text.replace('\r\n','\n'))
        alias=self.root/'renamed.flac';alias.write_bytes(flac.read_bytes())
        renamed=import_track(alias,self.scripts)
        self.assertTrue(renamed['cached']);self.assertIn('MusicSyncOffsetMs=27.5',read_ini(self.scripts/'SABounce.ini'))
        sf.write(flac,signal(np.arange(.8,14,60/140),16),RATE,subtype='PCM_16')
        replaced=import_track(flac,self.scripts)
        self.assertFalse(replaced['cached']);self.assertNotEqual(two['source_sha256'],replaced['source_sha256'])
        self.assertLess(abs(replaced['bpm']-140),.2)
    def test_bad_source_keeps_previous_configuration(self):
        bad=self.root/'broken.mp3';bad.write_bytes(b'not audio')
        original=(self.scripts/'SABounce.ini').read_bytes()
        with self.assertRaises(Exception): import_track(bad,self.scripts)
        self.assertEqual(original,(self.scripts/'SABounce.ini').read_bytes())
    def test_manual_override(self):
        wav=self.root/'beat.wav';sf.write(wav,self.audio,RATE)
        result=import_track(wav,self.scripts,bpm=120,offset_ms=370)
        self.assertEqual(result['mode'],'manual-grid');self.assertAlmostEqual(result['first_beat_ms'],370)

class LanguageTests(unittest.TestCase):
    def setUp(self): self.saved=os.environ.get('SABOUNCE_LANG')
    def tearDown(self):
        if self.saved is None: os.environ.pop('SABOUNCE_LANG',None)
        else: os.environ['SABOUNCE_LANG']=self.saved
    def test_english_and_russian(self):
        os.environ['SABOUNCE_LANG']='en'
        self.assertIn('Beats found: 291',lang.done_message(135.0,291,True))
        self.assertEqual(lang.localize_error('No source file was selected.'),'No source file was selected.')
        self.assertTrue(lang.picker_filter().startswith('Music (MP3'))
        os.environ['SABOUNCE_LANG']='ru'
        self.assertIn('Найдено ударов: 291',lang.done_message(135.0,291,True))
        self.assertIn('Ритм определён неуверенно',lang.done_message(135.0,291,False))
        self.assertEqual(lang.localize_error('No source file was selected.'),'Файл не выбран.')
        self.assertEqual(lang.localize_error('Music file does not exist: C:/x.mp3'),'Файл музыки не найден: C:/x.mp3')
        self.assertEqual(lang.localize_error('something unexpected'),'something unexpected')
        self.assertTrue(lang.picker_filter().startswith('Музыка (MP3'))
    def test_every_known_error_is_translated(self):
        import re
        sources=''.join(Path(f).read_text(encoding='utf-8') for f in ('import_music.py','beat_analysis.py'))
        raised=set(re.findall(r"raise \w+\('([^']+?)'\)",sources))
        for message in raised: self.assertIn(message,lang.ERRORS_RU,'no Russian text for: '+message)
    def test_default_follows_windows(self):
        os.environ.pop('SABOUNCE_LANG',None)
        self.assertIn(lang.is_russian(),(True,False))

if __name__=='__main__': unittest.main(verbosity=2)
