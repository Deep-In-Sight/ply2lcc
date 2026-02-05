#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build_release"
APPDIR="$PROJECT_DIR/AppDir"
TOOLS_DIR="$PROJECT_DIR/tools"

echo "=== Building ply2lcc AppImage ==="

# Clean previous builds
rm -rf "$BUILD_DIR" "$APPDIR" "$PROJECT_DIR"/*.AppImage

# Build release
echo "Building release..."
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake "$PROJECT_DIR" -DCMAKE_BUILD_TYPE=Release -DBUILD_GUI=ON -DBUILD_TESTS=OFF
make -j$(nproc)

# Create AppDir structure
echo "Creating AppDir..."
mkdir -p "$APPDIR/usr/bin"
mkdir -p "$APPDIR/usr/share/applications"
mkdir -p "$APPDIR/usr/share/icons/hicolor/256x256/apps"

# Copy executables
cp "$BUILD_DIR/ply2lcc" "$APPDIR/usr/bin/"
cp "$BUILD_DIR/gui/ply2lcc-gui" "$APPDIR/usr/bin/"

# Create desktop file
cat > "$APPDIR/usr/share/applications/ply2lcc.desktop" << 'EOF'
[Desktop Entry]
Type=Application
Name=ply2lcc Converter
Comment=Convert 3DGS PLY files to LCC format
Exec=ply2lcc-gui
Icon=ply2lcc
Categories=Graphics;3DGraphics;
Terminal=false
EOF

# Create icon
if command -v convert &> /dev/null; then
    convert -size 256x256 xc:'#4a90d9' -fill white -font DejaVu-Sans-Bold -pointsize 36 \
        -gravity center -annotate 0 'PLY2LCC' "$APPDIR/usr/share/icons/hicolor/256x256/apps/ply2lcc.png"
else
    # Fallback: create minimal valid PNG
    echo "Warning: ImageMagick not found, using placeholder icon"
    # Create a simple 1x1 blue PNG and scale it
    printf '\x89PNG\r\n\x1a\n' > "$APPDIR/usr/share/icons/hicolor/256x256/apps/ply2lcc.png"
fi

# Download tools if needed
mkdir -p "$TOOLS_DIR"
cd "$TOOLS_DIR"

if [ ! -f linuxdeploy ]; then
    echo "Downloading linuxdeploy..."
    wget -q "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" -O linuxdeploy
    chmod +x linuxdeploy
fi

if [ ! -f linuxdeploy-plugin-qt ]; then
    echo "Downloading linuxdeploy-plugin-qt..."
    wget -q "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage" -O linuxdeploy-plugin-qt
    chmod +x linuxdeploy-plugin-qt
fi

if [ ! -f appimagetool ]; then
    echo "Downloading appimagetool..."
    wget -q "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage" -O appimagetool
    chmod +x appimagetool
fi

# Deploy Qt libraries
echo "Deploying Qt libraries..."
cd "$PROJECT_DIR"
export QMAKE=$(which qmake 2>/dev/null || echo "/usr/bin/qmake")
"$TOOLS_DIR/linuxdeploy" --appdir "$APPDIR" --plugin qt 2>&1 | tail -5 || true

# Create AppRun
cat > "$APPDIR/AppRun" << 'EOF'
#!/bin/bash
SELF=$(readlink -f "$0")
HERE=${SELF%/*}
export PATH="${HERE}/usr/bin:${PATH}"
export LD_LIBRARY_PATH="${HERE}/usr/lib:${LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="${HERE}/usr/plugins"
exec "${HERE}/usr/bin/ply2lcc-gui" "$@"
EOF
chmod +x "$APPDIR/AppRun"

# Create symlinks for AppImage
ln -sf usr/share/icons/hicolor/256x256/apps/ply2lcc.png "$APPDIR/.DirIcon"
ln -sf usr/share/applications/ply2lcc.desktop "$APPDIR/ply2lcc.desktop"

# Fix line endings in desktop file
sed -i 's/\r$//' "$APPDIR/usr/share/applications/ply2lcc.desktop"

# Build AppImage
echo "Creating AppImage..."
cd "$PROJECT_DIR"
ARCH=x86_64 "$TOOLS_DIR/appimagetool" "$APPDIR" "ply2lcc-x86_64.AppImage"

# Create release folder
mkdir -p "$PROJECT_DIR/release"
mv "ply2lcc-x86_64.AppImage" "$PROJECT_DIR/release/"
cp "$BUILD_DIR/ply2lcc" "$PROJECT_DIR/release/"

# Cleanup
rm -rf "$APPDIR"

echo ""
echo "=== Build complete ==="
echo "Release files:"
ls -lh "$PROJECT_DIR/release/"
