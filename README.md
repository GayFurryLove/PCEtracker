# PCEtracker

Специализированный трекер **только для PC Engine / TurboGrafx-16** (HuC6280 PSG).
Клавиатурный процесс в духе ProTracker, ориентация на реальные возможности чипа,
экспорт в WAV и в родные форматы консоли (HES, ROM).

> Статус: **этап 1 из 8** — ядро звука HuC6280 и тесты. Интерфейса трекера пока нет.
> План и список вырезанного: [docs/ROADMAP.md](docs/ROADMAP.md).
> Устройство модулей: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Сборка на Linux (в т.ч. antiX i386)

```bash
sudo apt install build-essential cmake git
git clone https://github.com/<аккаунт>/PCEtracker.git
cd PCEtracker
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

SDL2 (`libsdl2-dev`) понадобится начиная с этапа 3 (интерфейс), сейчас не нужен.

Если `ctest --test-dir` не поддерживается (CMake < 3.20): `cd build && ctest --output-on-failure`.

## Сборка на Windows (x64)

Нужны CMake и Visual Studio Build Tools (C++).

```bash
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Послушать ядро

```bash
./build/psg_demo demo.wav                # HuC6280A, 44100 Гц, 16 бит
./build/psg_demo demo.wav --rev 6280     # ранняя ревизия чипа
./build/psg_demo demo.wav --rate 96000 --bits 24
```

Демо управляет чипом так же, как это делал бы код на консоли: записи в регистры раз в
кадр (60 Гц) и запись DDA-отсчётов по таймеру (~7 кГц). Каналы: пила с программным
затуханием, треугольный бас, DDA-«бочка», арпеджио на прямоугольнике, шумовой хэт.

## Лицензия

GPL-2.0-or-later (см. [LICENSE](LICENSE)). Ядро PSG взято из Mednafen в редакции Furnace,
ресемплер — blip_buf (LGPL-2.1+). Авторы и источники: [CREDITS.md](CREDITS.md).
