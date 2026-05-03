import json
import base64
import os
import requests
import time

API_KEY = "AIzaSyBzeSC3-lbQhuVS6b1di4o1AB6e78Rdp74"
MODEL = "gemini-2.0-flash-001"
OUTPUT_DIR = "docs/marketing/images"

os.makedirs(OUTPUT_DIR, exist_ok=True)

PROMPTS = {
    "01-coin": "Minimalist cryptocurrency logo, stylized flame XFG coin emerging from darkness, orange-gold gradient fire, matte black background, premium fintech feel, clean design, centered composition, 4k",
    "02-hero": "Digital art, dramatic cryptocurrency logo design, geometric flame symbol merging with blockchain blocks, dark obsidian background with orange-red fire glow, neon fire particles floating, futuristic fintech aesthetic, highly detailed, cinematic lighting, 4k, masterpiece",
    "03-privacy": "Cinematic digital art, mysterious hooded figure silhouette standing before city of lights, orange flame eyes glowing through darkness, dark blue-black background, freedom symbolism, cyberpunk aesthetic, privacy themes, dramatic backlighting, 8k, masterpiece",
    "04-network": "Abstract visualization of blockchain network as glowing orange circuits against black void, nodes pulsing with fire energy, data streams flowing like flames, digital art, futuristic technology concept, dark moody atmosphere, highly detailed, 4k resolution",
    "05-yield": "Abstract gold coins stacking with flame effects, growth graph made of fire, dark elegant background, wealth accumulation symbolism, premium financial aesthetic, 4k",
    "06-swap": "Visualization of two coins (XFG + SOL/ETH/XMR/BCH) connecting via flame bridge, swap exchange concept, dark futuristic background, digital art, 4k",
    "07-banner": "Twitter/X Banner for Fuego XFG, CryptoNote Fire theme, cinematic composition, orange fire accents on black/dark blue background, futuristic crypto aesthetic, clean minimal design, 4k horizontal banner format",
    "08-twitter": "Digital art of privacy shield made of orange flame patterns, protected digital assets glowing behind it, dark background, security concept, cyber protection, subtle blockchain patterns in smoke, cinematic, 4k"
}

def generate_image(name, prompt):
    print(f"🎨 Generating: {name}...")
    
    url = f"https://generativelanguage.googleapis.com/v1beta/models/{MODEL}:generateContent?key={API_KEY}"
    
    payload = {
        "contents": [
            {
                "parts": [
                    { "text": prompt }
                ]
            }
        ],
        "generationConfig": {
            "responseModalities": ["TEXT", "IMAGE"]
        }
    }
    
    headers = {
        "Content-Type": "application/json"
    }
    
    try:
        response = requests.post(url, json=payload, headers=headers)
        response.raise_for_status()
        data = response.json()
        
        # Check for image data in candidates
        found = False
        if "candidates" in data:
            for candidate in data["candidates"]:
                if "content" in candidate and "parts" in candidate["content"]:
                    for part in candidate["content"]["parts"]:
                        if "inlineData" in part:
                            image_data = part["inlineData"]["data"]
                            with open(f"{OUTPUT_DIR}/{name}.png", "wb") as f:
                                f.write(base64.b64decode(image_data))
                            print(f"✓ Saved: {OUTPUT_DIR}/{name}.png")
                            found = True
                            break
                if found: break
        
        if not found:
            print(f"❌ No image found in response for {name}")
            with open(f"{OUTPUT_DIR}/error-{name}.json", "w") as f:
                json.dump(data, f, indent=2)
                
    except Exception as e:
        print(f"❌ Error for {name}: {e}")
        if 'response' in locals():
            print(f"Response status: {response.status_code}")
            print(f"Response body: {response.text[:500]}")

for name, prompt in PROMPTS.items():
    generate_image(name, prompt)
    # Rate limiting sleep
    time.sleep(2)

print("\n✅ Generation complete")
