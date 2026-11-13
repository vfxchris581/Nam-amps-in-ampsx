# Installing Nam Knobs

## macOS
1. Open `NamKnobs-macOS.dmg` and double-click **Install Nam Knobs.pkg**.
2. Click through the installer. By default it installs all three:
   - **VST3** → `/Library/Audio/Plug-Ins/VST3/Nam Knobs.vst3`
   - **Audio Unit** → `/Library/Audio/Plug-Ins/Components/Nam Knobs.component` (Logic, GarageBand, MainStage)
   - **Standalone app** → `/Applications/Nam Knobs.app`

   Click **Customize** on the *Installation Type* page to leave any of them out.
3. Open your DAW and rescan plug-ins if needed. Nam Knobs is listed under **VFX Chris**.

Universal build: runs natively on Apple Silicon and Intel, macOS 11 or later.

**If macOS says the installer "can't be opened"** (builds without a Developer ID signature):
open **System Settings → Privacy & Security**, scroll down and click **Open Anyway** next to
"Install Nam Knobs.pkg", then open it again. Or right-click the installer → **Open** → **Open**.

**Uninstall:** run **Uninstall Nam Knobs.command** from the disk image. It removes the app and
plug-ins and asks whether to keep your library (amps, blends, captures, presets).

## Windows
1. Unzip `NamKnobs-Windows.zip`.
2. Copy `Nam Knobs.vst3` to `C:\Program Files\Common Files\VST3\`.
3. Run `Nam Knobs.exe` for the standalone app (choose your audio interface under *Options → Audio/MIDI Settings*; ASIO is recommended).

## First steps
1. Drag a folder of `.nam` captures of **one** amplifier onto the window. Several amps at once is fine too — they are separated by their metadata.
2. Watch the status line: each capture is analysed, then the amp appears with its channels.
3. Open **Captures** to check the knob mapping. Values from file names are white, values estimated from the sound are amber with `~`, your own values are gold. Double-click to edit; type a channel name to move a capture; click **Apply mapping**.
4. Captures without manufacturer/model metadata appear at the bottom of the Captures page — name them to add them to an amp.
5. Load one or two cabinet IRs on **Cabinet** (or drop WAV files on the window).
6. Click **Tuner** at the top to tune up (it mutes the output while open; untick *Mute output while tuning* to hear yourself).
7. The **Noise Gate** on the Amp page is on by default. Raise *Threshold* until the hiss stops when you mute the strings; lower it if note tails get cut.
8. To start over, **Delete all** on the Tone Matrix empties the 18 loaders; **Delete all amps & captures...** on Library empties the whole library. Your original .nam files are never touched.

## Naming captures so Nam Knobs reads every knob
`<Amp> <channel> G<gain> B<bass> M<mid> T<treble> P<presence> R<resonance> V<volume>/MV<master>`
e.g. `JCM800 ch2 G7 B5 M6 T7 P4.nam`, `Rect Modern gain=65 treble=7.nam` (0–100 values are scaled to 0–10).

## Where things are stored
- macOS: `~/Library/Application Support/Nam Knobs/Library`
- Windows: `%APPDATA%\Nam Knobs\Library`

`Amps/` (amp definitions), `Captures/` (library copies of your .nam files), `Presets/`, `CabPresets/`.
