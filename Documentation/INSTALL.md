# Installing Nam VX

## macOS
1. Open `NamVX-macOS.dmg`.
2. Drag **Nam VX.app** to **Applications** (standalone).
3. Copy the plug-ins from the `Plug-Ins` folder:
   - `Nam VX.vst3` → `~/Library/Audio/Plug-Ins/VST3/`
   - `Nam VX.component` → `~/Library/Audio/Plug-Ins/Components/`
4. Restart your DAW and rescan plug-ins.

CI builds are ad-hoc signed, not notarised. If macOS blocks the app the first time, right-click it → **Open**, or run
`xattr -dr com.apple.quarantine "/Applications/Nam VX.app"` (and the same for the plug-in bundles).

## Windows
1. Unzip `NamVX-Windows.zip`.
2. Copy `Nam VX.vst3` to `C:\Program Files\Common Files\VST3\`.
3. Run `Nam VX.exe` for the standalone app (choose your audio interface under *Options → Audio/MIDI Settings*; ASIO is recommended).

## First steps
1. Drag a folder of `.nam` captures of **one** amplifier onto the window. Several amps at once is fine too — they are separated by their metadata.
2. Watch the status line: each capture is analysed, then the amp appears with its channels.
3. Open **Captures** to check the knob mapping. Values from file names are white, values estimated from the sound are amber with `~`, your own values are gold. Double-click to edit; type a channel name to move a capture; click **Apply mapping**.
4. Captures without manufacturer/model metadata appear at the bottom of the Captures page — name them to add them to an amp.
5. Load one or two cabinet IRs on **Cabinet** (or drop WAV files on the window).

## Naming captures so Nam VX reads every knob
`<Amp> <channel> G<gain> B<bass> M<mid> T<treble> P<presence> R<resonance> V<volume>/MV<master>`
e.g. `JCM800 ch2 G7 B5 M6 T7 P4.nam`, `Rect Modern gain=65 treble=7.nam` (0–100 values are scaled to 0–10).

## Where things are stored
- macOS: `~/Library/Application Support/Nam VX/Library`
- Windows: `%APPDATA%\Nam VX\Library`

`Amps/` (amp definitions), `Captures/` (library copies of your .nam files), `Presets/`, `CabPresets/`.
