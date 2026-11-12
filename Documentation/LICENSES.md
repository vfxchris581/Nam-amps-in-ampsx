# Third-party components

| Component | Use | License |
|---|---|---|
| [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) (Steven Atkinson) | NAM inference | MIT |
| [nlohmann/json](https://github.com/nlohmann/json) (vendored in NAM core) | JSON | MIT |
| [Eigen 3.4](https://eigen.tuxfamily.org) | Linear algebra for NAM | MPL 2.0 |
| [JUCE 8](https://juce.com) | Audio framework, plug-in formats, GUI | AGPLv3 **or** JUCE commercial/personal license |
| Inter, Poppins fonts | UI typography | SIL Open Font License 1.1 (`Resources/Fonts/OFL.txt`) |
| VST3 SDK (via JUCE) | VST3 format | GPLv3 or Steinberg proprietary license (via JUCE) |

## JUCE — decide before you distribute binaries
JUCE 8 is dual-licensed. To distribute Nam VX builds you need **one** of:
- release Nam VX under the **AGPLv3** (source available to everyone who receives the app), or
- a **JUCE license** (JUCE offers a free Personal tier below a revenue threshold, and paid tiers above it — check juce.com for the current terms).

Building for your own use needs neither. `JUCE_DISPLAY_SPLASH_SCREEN` is set to 0, which JUCE only permits under a license that allows removing the splash screen.
