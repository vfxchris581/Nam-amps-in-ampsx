#!/bin/bash
# Removes the Nam Knobs app and plug-ins. Your amps, blends and presets are kept unless you choose otherwise.
echo "This removes Nam Knobs (app, VST3 and Audio Unit) from this Mac."
echo "Your password is needed because the plug-ins are installed for all users."
echo
sudo rm -rf "/Applications/Nam Knobs.app" \
            "/Library/Audio/Plug-Ins/VST3/Nam Knobs.vst3" \
            "/Library/Audio/Plug-Ins/Components/Nam Knobs.component" \
            "$HOME/Library/Audio/Plug-Ins/VST3/Nam Knobs.vst3" \
            "$HOME/Library/Audio/Plug-Ins/Components/Nam Knobs.component"
for id in vst3 au app; do sudo pkgutil --forget "com.vfxchris.namknobs.$id" >/dev/null 2>&1; done
killall -9 AudioComponentRegistrar 2>/dev/null
echo "Nam Knobs has been removed."
echo
read -r -p "Also delete your Nam Knobs library (amps, blends, captures, presets)? [y/N] " a
if [[ "$a" =~ ^[Yy]$ ]]; then
  rm -rf "$HOME/Library/Application Support/Nam Knobs"
  echo "Library deleted."
else
  echo "Library kept in ~/Library/Application Support/Nam Knobs"
fi
