#!/usr/bin/env python3
"""
Fuego XFG Image Generator
Uses Google Gemini to generate marketing images.
Uses urllib (built-in) instead of requests.
"""

import os
import sys
import json
import base64
import argparse
import urllib.request
import urllib.error
from pathlib import Path


def generate_image(prompt: str, output_path: str = None, size: int = 1024) -> dict:
    """Generate image using Gemini API."""
    
    api_key = os.environ.get("GEMINI_API_KEY")
    if not api_key:
        return {"error": "GEMINI_API_KEY not set. Run: export GEMINI_API_KEY='your-key'"}
    
    url = f"https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash-exp:imageGeneration:001?key={api_key}"
    
    data = {
        "contents": [{
            "parts": [{"text": prompt}]
        }],
        "generationConfig": {
            "responseModalities": ["IMAGE", "TEXT"],
            "temperature": 1,
            "topP": 0.95,
            "topK": 40,
            "imageCount": 1
        }
    }
    
    print(f"Generating image...")
    print(f"Prompt: {prompt[:100]}...")
    
    try:
        req = urllib.request.Request(
            url,
            data=json.dumps(data).encode('utf-8'),
            headers={'Content-Type': 'application/json'},
            method='POST'
        )
        
        with urllib.request.urlopen(req, timeout=120) as response:
            result = json.loads(response.read().decode('utf-8'))
        
        if "candidates" in result:
            for candidate in result.get("candidates", []):
                for part in candidate.get("content", {}).get("parts", []):
                    if "image" in part:
                        image_data = base64.b64decode(part["image"])
                        
                        if output_path is None:
                            output_path = "fuego_marketing.png"
                        
                        Path(output_path).parent.mkdir(parents=True, exist_ok=True)
                        with open(output_path, "wb") as f:
                            f.write(image_data)
                        
                        print(f"✓ Image saved: {output_path}")
                        return {"success": True, "path": output_path}
        
        return {"error": "No image in response", "response": result}
        
    except urllib.error.HTTPError as e:
        error_body = e.read().decode('utf-8') if e.fp else ""
        return {"error": f"HTTP {e.code}: {error_body[:500]}"}
    except Exception as e:
        return {"error": f"Request failed: {e}"}


# Fuego-specific prompts for quick use
FUEGO_PROMPTS = {
    "coin": """Digital cryptocurrency logo design, stylized flame symbol for XFG coin, 
    dark obsidian black background, orange-red fire glow with golden highlights, 
    premium fintech aesthetic, clean minimalist design, 4K resolution""",
    
    "hero": """Dramatic cryptocurrency XFG coin emerging from darkness with flame aura, 
    dark background with subtle fire particles floating, neon orange glow effects, 
    cinematic lighting, futuristic fintech concept, digital art, 4K""",
    
    "privacy": """Cinematic digital art, mysterious hooded figure silhouette with glowing 
    orange flame eyes, dark cyberpunk atmosphere, privacy and freedom symbolism, 
    deep blue-black background with dramatic backlighting, 4K""",
    
    "network": """Abstract visualization of blockchain network as glowing orange circuits 
    against black void, nodes pulsing with fire energy, data streams flowing like flames, 
    futuristic technology concept, dark moody atmosphere, 4K""",
    
    "yield": """Abstract gold coins melting into flame with growth chart made of fire rising, 
    dark elegant background, wealth accumulation and CD interest symbolism, 
    premium financial aesthetic, digital art, 4K""",
    
    "swap": """Two cryptocurrency coins (XFG flame coin and alt-coin) connected by a bridge 
    of flames, atomic swap exchange concept, dark futuristic background, 
    digital art, 4K""",
    
    "banner": """Wide cinematic banner for cryptocurrency website, XFG flame logo on left side, 
    dark atmospheric background with subtle fire glow effects, text space on right side,
    professional fintech marketing material, horizontal 16:9 format, 4K""",
    
    "twitter": """Social media post graphic for cryptocurrency, centered XFG flame coin logo, 
    dark background, dramatic orange-gold gradient fire effects, clean minimalist design,
    Twitter/X post format, square 1:1 ratio, 4K""",
}


def main():
    parser = argparse.ArgumentParser(description="Fuego XFG Marketing Image Generator")
    parser.add_argument("prompt", nargs="?", help="Image prompt or preset name")
    parser.add_argument("--output", "-o", help="Output file path")
    parser.add_argument("--size", "-s", type=int, default=1024, help="Image size")
    parser.add_argument("--list", "-l", action="store_true", help="List available presets")
    
    args = parser.parse_args()
    
    if args.list or not args.prompt:
        print("\n📋 Available Preset Prompts:\n")
        for name, desc in FUEGO_PROMPTS.items():
            preview = desc[:60] + "..."
            print(f"  {name:12} - {preview}")
        print("\nUsage: python generate_marketing.py [preset|prompt]")
        print("       python generate_marketing.py coin --output ./coin.png")
        return
    
    # Check if it's a preset
    prompt = FUEGO_PROMPTS.get(args.prompt.lower(), args.prompt)
    
    result = generate_image(prompt, args.output, args.size)
    
    if "error" in result:
        print(f"❌ Error: {result['error']}")
        if "GEMINI_API_KEY" in result["error"]:
            print("\n💡 Set your API key:")
            print("   export GEMINI_API_KEY='your-key-here'")
    else:
        print(f"✅ Success: {result['path']}")


if __name__ == "__main__":
    main()