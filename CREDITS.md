# Авторы и сторонний код

| Компонент | Откуда | Авторы | Лицензия |
|---|---|---|---|
| `src/psg/huc6280_psg_core.*` — эмуляция HuC6280 PSG | Mednafen (через Furnace, `src/engine/platform/sound/pce_psg.*`) | Mednafen Team; Charles MacDonald (2001, исходная структура PSG); правки tildearrow (Furnace) | GPL-2.0-or-later |
| `third_party/blip_buf` — band-limited ресемплер | blip_buf (копия из Furnace `extern/blip_buf`) | Shay Green (blargg); правка tildearrow (отключаемый high-pass) | LGPL-2.1-or-later |
| `third_party/font8x8` — пиксельный шрифт 8×8 | github.com/dhepper/font8x8 | Daniel Hepper; Marcel Sondaar; IBM VGA fonts | Public Domain |
| SDL2 (внешняя зависимость, не в репозитории) | libsdl.org | SDL contributors | zlib |

Изменения PCEtracker в ядре PSG: убрана зависимость от осциллографа Furnace и
отладочные accessors регистров, код помещён в `namespace pce::detail`.
**Логика эмуляции не менялась.**

Ориентиры по духу (код не используется): ProTracker / pt2-clone, SID-Wizard, LSDj.
