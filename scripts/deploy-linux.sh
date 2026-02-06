#!/bin/bash
# deploy-linux.sh
# Creates a self-contained distribution of ply2lcc (CLI + GUI) for Linux
# Usage: ./scripts/deploy-linux.sh [--build-dir <path>] [--output-dir <path>] [--tar]

set -e

# Parse arguments
BUILD_DIR="build_release"
OUTPUT_DIR="dist"
CREATE_TAR=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        --output-dir)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        --tar)
            CREATE_TAR=true
            shift
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--build-dir <path>] [--output-dir <path>] [--tar]"
            exit 1
            ;;
    esac
done

# Resolve paths
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_PATH="$PROJECT_ROOT/$BUILD_DIR"
OUTPUT_PATH="$PROJECT_ROOT/$OUTPUT_DIR"

echo "=== ply2lcc Linux Deployment Script ==="
echo "Build directory: $BUILD_PATH"
echo "Output directory: $OUTPUT_PATH"

# Check build outputs exist
CLI_EXE="$BUILD_PATH/ply2lcc"
GUI_EXE="$BUILD_PATH/gui/ply2lcc-gui"

if [[ ! -f "$CLI_EXE" ]]; then
    echo "ERROR: CLI executable not found: $CLI_EXE"
    echo "Run: mkdir -p $BUILD_DIR && cd $BUILD_DIR && cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_GUI=ON && make -j\$(nproc)"
    exit 1
fi

HAS_GUI=false
if [[ -f "$GUI_EXE" ]]; then
    HAS_GUI=true
else
    echo "WARNING: GUI executable not found. Building CLI-only distribution."
fi

# Clean and create output directory
if [[ -d "$OUTPUT_PATH" ]]; then
    echo "Cleaning existing output directory..."
    rm -rf "$OUTPUT_PATH"
fi
mkdir -p "$OUTPUT_PATH/bin"
mkdir -p "$OUTPUT_PATH/lib"

# Copy CLI
echo "Copying CLI executable..."
cp "$CLI_EXE" "$OUTPUT_PATH/bin/"

# Function to copy library and its dependencies
copy_lib_deps() {
    local lib="$1"
    local dest="$2"

    if [[ -f "$lib" ]]; then
        local basename=$(basename "$lib")
        if [[ ! -f "$dest/$basename" ]]; then
            cp -L "$lib" "$dest/"
            echo "  Copied: $basename"
        fi
    fi
}

# Get non-system dependencies for a binary
get_deps() {
    local binary="$1"
    ldd "$binary" 2>/dev/null | grep "=>" | awk '{print $3}' | while read lib; do
        if [[ -f "$lib" ]]; then
            # Skip system libraries that are guaranteed to exist
            case "$lib" in
                /lib/x86_64-linux-gnu/libc.so*) ;;
                /lib/x86_64-linux-gnu/libm.so*) ;;
                /lib/x86_64-linux-gnu/libdl.so*) ;;
                /lib/x86_64-linux-gnu/libpthread.so*) ;;
                /lib/x86_64-linux-gnu/librt.so*) ;;
                /lib64/ld-linux-x86-64.so*) ;;
                # Skip GPU/graphics drivers - must use system versions
                */libGL.so*) ;;
                */libGLX.so*) ;;
                */libGLdispatch.so*) ;;
                */libEGL.so*) ;;
                */libdrm.so*) ;;
                */libX11.so*) ;;
                */libxcb.so*) ;;
                */libX*.so*) ;;
                # Include everything else
                *)
                    echo "$lib"
                    ;;
            esac
        fi
    done
}

# Copy CLI dependencies
echo "Copying CLI dependencies..."
for lib in $(get_deps "$CLI_EXE"); do
    copy_lib_deps "$lib" "$OUTPUT_PATH/lib"
done

# Copy GUI and Qt dependencies
if $HAS_GUI; then
    echo "Copying GUI executable..."
    cp "$GUI_EXE" "$OUTPUT_PATH/bin/"

    echo "Copying GUI dependencies..."
    for lib in $(get_deps "$GUI_EXE"); do
        copy_lib_deps "$lib" "$OUTPUT_PATH/lib"
    done

    # Copy Qt plugins
    echo "Copying Qt plugins..."
    mkdir -p "$OUTPUT_PATH/plugins/platforms"
    mkdir -p "$OUTPUT_PATH/plugins/imageformats"
    mkdir -p "$OUTPUT_PATH/plugins/xcbglintegrations"

    # Find Qt plugin directories
    QT_PLUGIN_PATHS=(
        "/usr/lib/x86_64-linux-gnu/qt5/plugins"
        "/lib/x86_64-linux-gnu/qt5/plugins"
        "/usr/lib/qt5/plugins"
        "/usr/lib64/qt5/plugins"
    )

    QT_PLUGINS=""
    for path in "${QT_PLUGIN_PATHS[@]}"; do
        if [[ -d "$path" ]]; then
            QT_PLUGINS="$path"
            break
        fi
    done

    if [[ -n "$QT_PLUGINS" ]]; then
        echo "  Found Qt plugins at: $QT_PLUGINS"

        # Platform plugin (required)
        if [[ -f "$QT_PLUGINS/platforms/libqxcb.so" ]]; then
            cp "$QT_PLUGINS/platforms/libqxcb.so" "$OUTPUT_PATH/plugins/platforms/"
            # Copy dependencies of the platform plugin
            for lib in $(get_deps "$QT_PLUGINS/platforms/libqxcb.so"); do
                copy_lib_deps "$lib" "$OUTPUT_PATH/lib"
            done
        fi

        # Image format plugins (optional but nice)
        for fmt in libqgif.so libqico.so libqjpeg.so libqsvg.so; do
            if [[ -f "$QT_PLUGINS/imageformats/$fmt" ]]; then
                cp "$QT_PLUGINS/imageformats/$fmt" "$OUTPUT_PATH/plugins/imageformats/"
            fi
        done

        # XCB GL integrations
        for gl in libqxcb-egl-integration.so libqxcb-glx-integration.so; do
            if [[ -f "$QT_PLUGINS/xcbglintegrations/$gl" ]]; then
                cp "$QT_PLUGINS/xcbglintegrations/$gl" "$OUTPUT_PATH/plugins/xcbglintegrations/"
            fi
        done
    else
        echo "  WARNING: Qt plugins not found"
    fi

    # Create qt.conf
    cat > "$OUTPUT_PATH/bin/qt.conf" << 'EOF'
[Paths]
Prefix = ..
Plugins = plugins
EOF
fi

# Copy dependencies of all libraries we've copied (recursive)
echo "Resolving transitive dependencies..."
for i in 1 2 3; do  # Up to 3 levels deep
    for lib in "$OUTPUT_PATH/lib"/*.so*; do
        if [[ -f "$lib" ]]; then
            for dep in $(get_deps "$lib"); do
                copy_lib_deps "$dep" "$OUTPUT_PATH/lib"
            done
        fi
    done
done

# Patch RPATH so executables find libraries without wrapper scripts
echo "Patching RPATH with patchelf..."
if command -v patchelf &> /dev/null; then
    patchelf --set-rpath '$ORIGIN/../lib' "$OUTPUT_PATH/bin/ply2lcc"
    echo "  Patched: ply2lcc"

    if $HAS_GUI; then
        patchelf --set-rpath '$ORIGIN/../lib' "$OUTPUT_PATH/bin/ply2lcc-gui"
        echo "  Patched: ply2lcc-gui"

        # Also patch Qt plugins to find libs
        for plugin in "$OUTPUT_PATH/plugins"/*/*.so; do
            if [[ -f "$plugin" ]]; then
                patchelf --set-rpath '$ORIGIN/../../lib' "$plugin" 2>/dev/null || true
            fi
        done
    fi

    # Patch all libraries to find each other
    for lib in "$OUTPUT_PATH/lib"/*.so*; do
        if [[ -f "$lib" ]]; then
            patchelf --set-rpath '$ORIGIN' "$lib" 2>/dev/null || true
        fi
    done
else
    echo "WARNING: patchelf not found. Install with: sudo apt install patchelf"
    echo "         Without patchelf, you'll need to set LD_LIBRARY_PATH manually."
fi

# Create convenience symlinks at root level
ln -sf bin/ply2lcc "$OUTPUT_PATH/ply2lcc"
if $HAS_GUI; then
    ln -sf bin/ply2lcc-gui "$OUTPUT_PATH/ply2lcc-gui"
fi

# Create README
cat > "$OUTPUT_PATH/README.txt" << 'EOF'
ply2lcc - 3DGS PLY to LCC Converter
===================================

Usage:
  CLI: ./ply2lcc -i <input.ply> -o <output_dir> [options]
  GUI: ./ply2lcc-gui

Requirements:
  - Linux x86_64
  - Graphics drivers (for GUI)

The distribution is self-contained and should work on most Linux systems.
If you encounter issues, ensure your system has basic X11 libraries installed.
EOF

# List deployed files
echo ""
echo "Deployed files:"
find "$OUTPUT_PATH" -type f -printf "  %P (%k KB)\n" | sort

# Calculate total size
TOTAL_SIZE=$(du -sh "$OUTPUT_PATH" | cut -f1)
echo ""
echo "Total size: $TOTAL_SIZE"

# Create tar.gz if requested
if $CREATE_TAR; then
    TAR_NAME="ply2lcc-linux-x86_64.tar.gz"
    TAR_PATH="$PROJECT_ROOT/$TAR_NAME"
    echo ""
    echo "Creating tar archive: $TAR_PATH"

    cd "$PROJECT_ROOT"
    tar -czf "$TAR_NAME" -C "$(dirname "$OUTPUT_PATH")" "$(basename "$OUTPUT_PATH")"

    TAR_SIZE=$(du -h "$TAR_PATH" | cut -f1)
    echo "Created: $TAR_NAME ($TAR_SIZE)"
fi

echo ""
echo "=== Deployment Complete ==="
echo "Output: $OUTPUT_PATH"
