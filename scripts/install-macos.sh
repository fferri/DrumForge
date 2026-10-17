#!/bin/sh
#
# Install InfinityDrum into your VST3 folder.
#
#   ./install-macos.sh                    install to ~/Library/Audio/Plug-Ins/VST3
#   ./install-macos.sh /path/to/VST3      install somewhere else
#
# This does two things a drag-and-drop does not. It puts the plugin where DAWs look for
# it, and it clears the quarantine flag that a browser download leaves behind - without
# that, Gatekeeper refuses to load the plugin and your DAW simply will not see it.
#
# Nothing here needs sudo, and nothing outside the destination folder is touched.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
source_plugin="$here/InfinityDrum.vst3"
destination="${1:-$HOME/Library/Audio/Plug-Ins/VST3}"
installed="$destination/InfinityDrum.vst3"

if [ ! -d "$source_plugin" ]; then
    echo "error: InfinityDrum.vst3 is not next to this script." >&2
    echo "       Unzip the download first, then run the script from inside it." >&2
    exit 1
fi

# Refuse anything that could delete something other than a plugin bundle.
case "$installed" in
    *.vst3) ;;
    *) echo "error: refusing to install to $installed" >&2; exit 1 ;;
esac

if [ -z "$destination" ] || [ "$destination" = "/" ]; then
    echo "error: refusing to install into '$destination'" >&2
    exit 1
fi

echo "InfinityDrum installer"
echo "  from $source_plugin"
echo "  to   $installed"
echo

mkdir -p "$destination"

# Replaced rather than merged, so a previous version cannot leave stale files behind.
if [ -e "$installed" ]; then
    echo "removing the previous install"
    rm -rf "$installed"
fi

cp -R "$source_plugin" "$installed"

# A browser download sets this attribute, and Gatekeeper will refuse to load quarantined
# code. A terminal download sets nothing, so this is a no-op in that case.
xattr -dr com.apple.quarantine "$installed" 2>/dev/null || true

echo "Installed."
echo
echo "Rescan plugins in your DAW and InfinityDrum will appear as an instrument."
echo "If it does not, check that the DAW is looking at $destination"
