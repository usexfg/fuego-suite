#!/bin/bash
# Fuego XFG Image Generator using curl

API_KEY="AIzaSyBzeSC3-lbQhuVS6b1di4o1AB6e78Rdp74"
OUTPUT_DIR="/Users/aejt/fuego_WS/docs/marketing/images"

mkdir -p "$OUTPUT_DIR"

generate_image() {
    local name="$1"
    local prompt="$2"
    local output="$OUTPUT_DIR/${name}.png"
    
    echo "🎨 Generating: $name..."
    
    # Create temp file for response
    local temp_json=$(mktemp)
    
    # Call Gemini API
    curl -s -X POST \
        "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash-exp:imageGeneration:001?key=${API_KEY}" \
        -H "Content-Type: application/json" \
        -d "{\"contents\":[{\"parts\":[{\"text\":\"$prompt\"}]}],\"generationConfig\":{\"responseModalities\":[\"IMAGE\",\"TEXT\"],\"temperature\":1}}" \
        -o "$temp_json"
    
    # Extract base64 image and decode
    if grep -q "image" "$temp_json" 2>/dev/null; then
        # Extract the base64 image data (simplified parsing)
        python3 << PYTHON
import json
import base64
import sys

with open("$temp_json", 'r') as f:
    data = json.load(f)

try:
    image_data = data["candidates"][0]["content"]["parts"][0]["image"]
    with open("$output", "wb") as f:
        f.write(base64.b64decode(image_data))
    print("✓ Saved: $output")
except Exception as e:
    print(f"❌ Error: {e}")
    with open("$OUTPUT_DIR/error-$name.json", "w") as f:
        json.dump(data, f, indent=2)
PYTHON
    else
        echo "❌ Failed: $name"
        cat "$temp_json" | head -5
    fi
    
    rm -f "$temp_json"
}

# Generate all presets
generate_image "01-coin" "Digital cryptocurrency logo design, stylized flame symbol for XFG coin, dark obsidian black background, orange-red fire glow with golden highlights, premium fintech aesthetic, clean minimalist design"

generate_image "02-hero" "Dramatic cryptocurrency XFG coin emerging from darkness with flame aura, dark background with subtle fire particles floating, neon orange glow effects, cinematic lighting, futuristic fintech concept, digital art"

generate_image "03-privacy" "Cinematic digital art, mysterious hooded figure silhouette with glowing orange flame eyes, dark cyberpunk atmosphere, privacy and freedom symbolism, deep blue-black background with dramatic backlighting"

generate_image "04-network" "Abstract visualization of blockchain network as glowing orange circuits against black void, nodes pulsing with fire energy, data streams flowing like flames, futuristic technology concept, dark moody atmosphere"

generate_image "05-yield" "Abstract gold coins melting into flame with growth chart made of fire rising, dark elegant background, wealth accumulation and CD interest symbolism, premium financial aesthetic, digital art"

generate_image "06-swap" "Two cryptocurrency coins (XFG flame coin and alt-coin) connected by a bridge of flames, atomic swap exchange concept, dark futuristic background, digital art"

generate_image "07-banner" "Wide cinematic banner for cryptocurrency website, XFG flame logo on left side, dark atmospheric background with subtle fire glow effects, text space on right side, professional fintech marketing material, horizontal format"

generate_image "08-twitter" "Social media post graphic for cryptocurrency, centered XFG flame coin logo, dark background, dramatic orange-gold gradient fire effects, clean minimalist design, square format"

echo ""
echo "✅ All images generated in $OUTPUT_DIR"
ls -la "$OUTPUT_DIR"