#!/bin/bash
# Removes Sam Spectral Control (REL-1): the VST3 plug-in, the Audio Unit, this folder and the
# installer receipts. Presets saved by the user in ~/Documents/Sam Spectral Control are kept.
name="Sam Spectral Control"
identifier="com.samaudio.samspectralcontrol"

echo "Removing $name. macOS asks for an administrator password."
sudo /bin/rm -rf "/Library/Audio/Plug-Ins/VST3/$name.vst3" \
    "/Library/Audio/Plug-Ins/Components/$name.component" \
    "/Library/Application Support/$name" || exit 1
for component in vst3 au uninstaller; do
    sudo /usr/sbin/pkgutil --forget "$identifier.$component" > /dev/null 2>&1 || true
done
echo "$name has been removed. Your presets in ~/Documents/$name were kept."
