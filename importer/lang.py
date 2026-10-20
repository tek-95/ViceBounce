"""Language of the importer's windows: Russian on a Russian Windows, English everywhere else.

SABOUNCE_LANG=ru or SABOUNCE_LANG=en overrides the detection."""
import ctypes, os

RUSSIAN_LANGID = 0x19  # primary language id (low 10 bits of the Windows UI language)

def is_russian():
    forced = os.environ.get('SABOUNCE_LANG', '').strip().lower()
    if forced in ('ru', 'en'):
        return forced == 'ru'
    try:
        return (ctypes.windll.kernel32.GetUserDefaultUILanguage() & 0x3FF) == RUSSIAN_LANGID
    except Exception:
        return False

TEXT = {
    'title_done': ('SABounce — music is ready', 'SABounce — музыка готова'),
    'title_failed': ('SABounce — could not import the music', 'SABounce — не удалось импортировать музыку'),
    'title_picker': ('SABounce — choose your music', 'SABounce — выбери свою музыку'),
    'filter_music': ('Music (MP3, FLAC, WAV, OGG)', 'Музыка (MP3, FLAC, WAV, OGG)'),
    'filter_all': ('All files', 'Все файлы'),
    'done': ("Done. Tempo: {bpm:.2f} BPM. Beats found: {beats}.\n"
             "Go back to the game. If the importer was started on its own, press F7 in the game.\n"
             "Press F9 on an audible beat to align the squash.",
             "Готово. Темп: {bpm:.2f} BPM. Найдено ударов: {beats}.\n"
             "Вернись в игру. Если импортёр запущен отдельно, нажми F7.\n"
             "F9 — поправить совмещение по слышимому удару."),
    'unsure': ("\nThe rhythm was detected with low confidence: check the BPM and set the phase by hand.",
               "\nРитм определён неуверенно: проверь BPM и настрой фазу вручную."),
}

# Known error texts (raised by the importer itself) -> Russian. Anything else is shown as it is.
ERRORS_RU = {
    'Tracks longer than 30 minutes are not supported by this importer.': 'Треки длиннее 30 минут не поддерживаются.',
    'Supported sample rates: 8000 to 192000 Hz.': 'Поддерживаемая частота дискретизации: от 8000 до 192000 Гц.',
    'Audio contains invalid sample values.': 'В аудио есть некорректные значения.',
    'Track must be at least one second long.': 'Трек должен быть не короче одной секунды.',
    'Source changed during import; retry when the file has finished copying.':
        'Файл изменился во время импорта: повтори, когда он полностью скопируется.',
    'No source file was selected.': 'Файл не выбран.',
    'Track is too short for beat analysis (minimum about 1 second).':
        'Трек слишком короткий для анализа ритма (минимум около 1 секунды).',
    'No rhythmic signal found. Silent tracks are not imported automatically.':
        'Ритм не найден. Тихие треки автоматически не импортируются.',
    'At least two beats must fit in the track.': 'В треке должно уместиться хотя бы два удара.',
    'Could not find a stable sequence of beats. Use --bpm and --offset-ms.':
        'Не удалось найти устойчивый ритм. Задай --bpm и --offset-ms.',
}
ERROR_PREFIXES_RU = (
    ('Music file does not exist: ', 'Файл музыки не найден: '),
    ('File picker error ', 'Ошибка окна выбора файла '),
    ('Invalid saved manual music setting: ', 'Некорректная сохранённая настройка: '),
)

def tr(key, **values):
    english, russian = TEXT[key]
    return (russian if is_russian() else english).format(**values)

def localize_error(message):
    if not is_russian():
        return message
    if message in ERRORS_RU:
        return ERRORS_RU[message]
    for english, russian in ERROR_PREFIXES_RU:
        if message.startswith(english):
            return russian + message[len(english):]
    return message

def done_message(bpm, beats, confident):
    text = tr('done', bpm=bpm, beats=beats)
    return text if confident else text + tr('unsure')

def picker_filter():
    return f"{tr('filter_music')}\0*.mp3;*.flac;*.wav;*.ogg\0{tr('filter_all')}\0*.*\0\0"
