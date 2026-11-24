# Seeder

MIDI generator plugin by Dani Koenig. Play one note: it becomes the root and plants a seed.
Chords, melody, arp and bass are generated from it. Play another note and everything changes.

**Formats:** VST3, AU and CLAP, macOS Apple Silicon. Installer: `Seeder-Installer.pkg` (see INSTALL.txt).
**Variants:** `Seeder` (MIDI effect) and `Seeder Instrument` (instrument with MIDI out, for Ableton).

## How it works

- **Trigger note = root.** The pitch class sets the key, the full note sets the seed.
- **Note Mode**
  - *Seed per Note*: every key has its own reproducible pattern (the keyboard is a preset bank).
  - *Transpose*: same pattern, new root.
  - *Random*: new material at every trigger.
- **Change at**: the new root takes over at the next 16th, beat or bar.
- **Latch**: keep playing after the key is released.
- **Mood** (Happy, Melancholic, Sad, Dark, Soulful, Jazzy) selects scale, progressions, chord extensions and register.
- **Style** (House, Deep / Dub House, Techno, Classic House, Garage, Disco, Ambient) selects rhythm templates, bass patterns, arp rate and swing.
- **Complexity / Density / Swing / Variation / Seed** shape the result. Variation mutates the melody, arp and bass over loops; 0 = exact loop.
- **Mute per layer:** every layer (Chords, Melody, Arp, Bass) has an ON/OFF switch and its own MIDI channel (CH 1 to 4).
- **Drag and drop:** drag the DRAG MIDI box of a layer, or DRAG ALL (four tracks), into a track of your DAW as a .mid file.
- **Instance link:** several instances share key, seed and settings (shared memory, works across plug-in processes). Give each instance a **Role** (Chords, Melody, Arp, Bass): it plays only that layer, on MIDI channel 1. Groups 1 to 4 separate different sets of instances. Patterns are deterministic, so all instances stay identical.
- **NEW** rolls a new seed, **INIT** resets all controls.

Pattern length: 4 bars (64 sixteenth steps), one chord per bar.

## Build

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target Seeder_VST3 Seeder_AU Seeder_CLAP SeederInstrument_VST3 SeederInstrument_AU SeederInstrument_CLAP
```

Tests:

```
g++ -std=c++17 -ISource/Engine Tests/engine_test.cpp Source/Engine/Generator.cpp -o engine_test && ./engine_test
cmake -S . -B build -DSEEDER_BUILD_TESTS=ON && cmake --build build --target SeederProcessorTest
```

GitHub Actions builds and tests on every push; a tag `vX.Y.Z` creates a release with the zip.

## Layout

- `Source/Engine/`: pure C++ generator (Theory, Styles, Generator), no JUCE dependency
- `Source/PluginProcessor.*`: trigger logic, clock, scheduler, MIDI out
- `Source/Link.*`: instance link (memory-mapped file, heartbeat, shared parameters, trigger)
- `Source/MidiExport.h`: .mid export for drag and drop
- `Source/PluginEditor.*`, `Look.*`, `Knob.h`, `Widgets.h`: UI in the Repetitor style (1100x662, scales with the window)
