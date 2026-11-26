# AutoEQ

AutoEQ turns down the frequencies that ring, only while they ring. Switch to EQ for an 8-band EQ where any band can be dynamic, or to SPACE to carve room for a vocal or kick fed in on the sidechain.

Product page: https://99hertz.com/autoeq

AutoEQ is an audio plugin (VST3 and AU on macOS, VST3 on Windows) built with [JUCE](https://juce.com) 8.

Copyright (c) 2026 99 Hertz Limited

## Licence

AutoEQ is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License version 3 (AGPLv3). See [LICENSE](LICENSE).

The EQ (`Source/ParametricEQ.*`) is ported from 99Hertz MusicStation's channel EQ and is published here under the same AGPLv3 licence.

Third-party parts:

- **JUCE** 8.0.13, used under its AGPLv3 open-source licence. Not included in this repo; CMake downloads it at build time.
- **TweetNaCl** (`Source/Licence/tweetnacl.c`, `tweetnacl.h`), public domain.
- **Barlow Condensed** and **Share Tech Mono** fonts (`Resources/Fonts/`), SIL Open Font License 1.1. The licence texts are included next to the fonts (`OFL-*.txt`).

## Building

You need CMake 3.22 or newer. JUCE 8.0.13 is fetched automatically on the first configure (or put a JUCE checkout at `./JUCE` to use that instead).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DAUTOEQ_COPY_PLUGIN_AFTER_BUILD=OFF
cmake --build build --config Release --target AutoEQ_VST3 AutoEQ_AU
```

The plugins end up in `build/AutoEQ_artefacts/Release/`.

Offline tests (latency, modes, sidechain detection, clicks, save/restore, free-version gate, UI snapshots):

```sh
cmake -S . -B build-test -DCMAKE_BUILD_TYPE=Release -DAUTOEQ_BUILD_TEST=ON -DAUTOEQ_COPY_PLUGIN_AFTER_BUILD=OFF
cmake --build build-test --target AutoEQTest
build-test/AutoEQTest_artefacts/Release/AutoEQTest out/
```
