#!/bin/sh
# Compile GLSL shaders to SPIR-V and generate C headers with embedded bytecode.
# Requires glslangValidator (from the Vulkan SDK or system package).
#
# Usage: ./compile_shaders.sh
# Output: *_spv.h files in this directory, included by gpu_vulkan.c

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

GLSLC="glslangValidator"

# Check for glslangValidator
if ! command -v "$GLSLC" >/dev/null 2>&1; then
    # Try glslc as alternative (from Vulkan SDK)
    if command -v glslc >/dev/null 2>&1; then
        GLSLC="glslc"
    else
        echo "Error: glslangValidator or glslc not found. Install Vulkan SDK." >&2
        exit 1
    fi
fi

compile_shader() {
    local src="$1"
    local spv="${src}.spv"
    local header="$(echo "$src" | sed 's/\./_/g')_spv.h"
    local varname="$(echo "$src" | sed 's/\./_/g')_spv"

    echo "Compiling $src -> $header"

    if [ "$GLSLC" = "glslc" ]; then
        glslc -o "$spv" "$src"
    else
        glslangValidator -V -o "$spv" "$src"
    fi

    # Generate C header from SPIR-V binary
    echo "/* Auto-generated from $src - do not edit */" > "$header"
    echo "#include <stdint.h>" >> "$header"
    echo "#include <stddef.h>" >> "$header"
    echo "static const uint32_t ${varname}[] = {" >> "$header"

    # Convert binary to comma-separated uint32_t hex values
    xxd -i < "$spv" | \
        sed 's/unsigned char.*\[\] = {//;s/};//;s/unsigned int.*;//' | \
        tr -d '\n' | \
        # xxd -i gives bytes; we need to group into uint32_t (little-endian)
        # Actually, SPIR-V is already uint32_t aligned, so just use od
        true  # reset

    od -A n -t x4 -v < "$spv" | \
        sed 's/^ *//;s/ *$//;s/  */,/g;s/^/    /;s/$/,/' >> "$header"

    echo "};" >> "$header"

    # Clean up intermediate .spv file
    rm -f "$spv"
}

compile_shader rect.vert
compile_shader rect.frag
compile_shader image.vert
compile_shader image.frag
compile_shader glyph.vert
compile_shader glyph.frag

echo "All shaders compiled successfully."
