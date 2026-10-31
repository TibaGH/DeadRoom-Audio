#!/bin/bash
# Builds the macOS installer from the collected plugins in dist/ (VST3, AU, AAX folders):
#
#   DeadRoom-Audio-<version>.pkg
#
# One installer with a choice per plugin. Each plugin goes to the standard system folders:
#   VST3  /Library/Audio/Plug-Ins/VST3
#   AU    /Library/Audio/Plug-Ins/Components
#   AAX   /Library/Application Support/Avid/Audio/Plug-Ins
#
# Files installed by the macOS Installer are not quarantined, so no Terminal steps are needed
# afterwards. Uses only tools that ship with macOS (pkgbuild, productbuild).
#
# Usage: packaging/package-macos.sh <dist folder> <version> <output folder>

set -euo pipefail

# Report the failing line as a GitHub annotation (readable without the full log)
trap 'echo "::error title=package-macos.sh::line $LINENO failed: $BASH_COMMAND"' ERR

DIST="$1"
VERSION="$2"
OUT="$3"

PLUGINS=(Roomless Duotone Lantern Farfield)
DESCRIPTIONS=(
    "Removes room reverb from recordings"
    "Baxandall mastering EQ, L/R or M/S"
    "Passive mastering EQ with tube make-up amplifier"
    "Puts dialogue at a distance in a room or outdoors"
)

WORK="$(mktemp -d)"
mkdir -p "$OUT" "$WORK/pkgs" "$WORK/resources" "$WORK/scripts"

# Remove copies of these plugins that were installed by hand in the user's own Library
# (older test builds), so the host does not see each plugin twice.
cat > "$WORK/scripts/postinstall" <<'EOF'
#!/bin/bash
CONSOLE_USER=$(stat -f%Su /dev/console 2>/dev/null || true)
if [ -n "$CONSOLE_USER" ] && [ "$CONSOLE_USER" != "root" ]; then
    HOME_DIR=$(dscl . -read "/Users/$CONSOLE_USER" NFSHomeDirectory 2>/dev/null | awk '{print $2}')
    for dir in "Library/Audio/Plug-Ins/VST3" "Library/Audio/Plug-Ins/VST" "Library/Audio/Plug-Ins/Components"; do
        for name in Roomless Duotone Lantern Farfield; do
            rm -rf "$HOME_DIR/$dir/$name.vst3" "$HOME_DIR/$dir/$name.component"
        done
    done
fi
# let Logic / GarageBand see new Audio Units straight away
killall -9 AudioComponentRegistrar 2>/dev/null || true
exit 0
EOF
chmod +x "$WORK/scripts/postinstall"

CHOICES=""
OUTLINE=""
REFS=""

for i in "${!PLUGINS[@]}"; do
    name="${PLUGINS[$i]}"
    lower=$(echo "$name" | tr '[:upper:]' '[:lower:]')
    id="com.deadroomaudio.$lower.pkg"
    root="$WORK/root-$name"

    found=0
    if [ -d "$DIST/VST3/$name.vst3" ]; then
        mkdir -p "$root/Library/Audio/Plug-Ins/VST3"
        ditto "$DIST/VST3/$name.vst3" "$root/Library/Audio/Plug-Ins/VST3/$name.vst3"
        found=1
    fi
    if [ -d "$DIST/AU/$name.component" ]; then
        mkdir -p "$root/Library/Audio/Plug-Ins/Components"
        ditto "$DIST/AU/$name.component" "$root/Library/Audio/Plug-Ins/Components/$name.component"
        found=1
    fi
    if [ -d "$DIST/AAX/$name.aaxplugin" ]; then
        mkdir -p "$root/Library/Application Support/Avid/Audio/Plug-Ins"
        ditto "$DIST/AAX/$name.aaxplugin" "$root/Library/Application Support/Avid/Audio/Plug-Ins/$name.aaxplugin"
        found=1
    fi

    if [ "$found" -eq 0 ]; then
        echo "warning: no builds of $name found in $DIST, skipping"
        continue
    fi

    # never "relocate" a bundle to some other copy the user may have elsewhere
    pkgbuild --analyze --root "$root" "$WORK/$name.plist"
    count=$(/usr/libexec/PlistBuddy -c "Print" "$WORK/$name.plist" | grep -c "RootRelativeBundlePath" || true)
    for ((k = 0; k < count; k++)); do
        /usr/libexec/PlistBuddy -c "Set :$k:BundleIsRelocatable false" "$WORK/$name.plist" 2>/dev/null || true
    done

    pkgbuild --root "$root" \
             --component-plist "$WORK/$name.plist" \
             --scripts "$WORK/scripts" \
             --identifier "$id" \
             --version "$VERSION" \
             --install-location / \
             "$WORK/pkgs/$name.pkg"

    OUTLINE="$OUTLINE<line choice=\"$lower\"/>"
    CHOICES="$CHOICES<choice id=\"$lower\" title=\"$name\" description=\"${DESCRIPTIONS[$i]}\" start_selected=\"true\"><pkg-ref id=\"$id\"/></choice>"
    REFS="$REFS<pkg-ref id=\"$id\" version=\"$VERSION\" onConclusion=\"none\">$name.pkg</pkg-ref>"
done

cat > "$WORK/resources/welcome.html" <<EOF
<html><body style="font-family: -apple-system, Helvetica; font-size: 13px;">
<h2>DeadRoom Audio $VERSION</h2>
<p>This installs the DeadRoom plugins as VST3, Audio Unit and AAX:</p>
<ul>
<li><b>Roomless</b>: removes room reverb from recordings</li>
<li><b>Duotone</b>: Baxandall mastering EQ, L/R or M/S</li>
<li><b>Lantern</b>: passive mastering EQ with tube make-up amplifier</li>
<li><b>Farfield</b>: puts dialogue at a distance in a room or outdoors</li>
</ul>
<p>Click <b>Customise</b> on the next screen to leave plugins out.</p>
<p>The AAX versions load in Pro Tools Developer until they are signed with PACE.</p>
</body></html>
EOF

cat > "$WORK/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>DeadRoom Audio $VERSION</title>
    <welcome file="welcome.html" mime-type="text/html"/>
    <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_localSystem="true"/>
    <os-version min="11.0"/>
    <choices-outline>$OUTLINE</choices-outline>
    $CHOICES
    $REFS
</installer-gui-script>
EOF

productbuild --distribution "$WORK/distribution.xml" \
             --resources "$WORK/resources" \
             --package-path "$WORK/pkgs" \
             "$OUT/DeadRoom-Audio-$VERSION.pkg"

pkgutil --check-signature "$OUT/DeadRoom-Audio-$VERSION.pkg" || true
ls -lh "$OUT"
rm -rf "$WORK"
