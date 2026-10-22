#!/usr/bin/env python3
"""Download CC0 Poly Haven assets for the lobby room and record their credits.

Usage: fetch_polyhaven.py <downloads dir> <model|texture|hdri>:<id>[:<res>] ...
Models are fetched as glTF, textures as jpg maps (diffuse, roughness, GL normal), HDRIs as .hdr. Each asset's
name, authors and license are appended to <downloads dir>/credits.json.
"""
import json, os, sys, urllib.request

UA = {"User-Agent": "vpx-lobby-room/1.0"}

def get(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA)) as r:
        return r.read()

def save(url, path):
    if os.path.exists(path):
        return
    os.makedirs(os.path.dirname(path), exist_ok=True)
    data = get(url)
    with open(path, "wb") as f:
        f.write(data)

def main():
    out = sys.argv[1]
    credits_path = os.path.join(out, "credits.json")
    credits = json.load(open(credits_path)) if os.path.exists(credits_path) else {}
    for spec in sys.argv[2:]:
        kind, asset, *rest = spec.split(":")
        res = rest[0] if rest else ("1k" if kind == "model" else "2k")
        files = json.loads(get(f"https://api.polyhaven.com/files/{asset}"))
        info = json.loads(get(f"https://api.polyhaven.com/info/{asset}"))
        folder = os.path.join(out, asset)
        if kind == "hdri":
            url = files["hdri"][res]["hdr"]["url"]
            save(url, os.path.join(folder, os.path.basename(url)))
        elif kind == "model":
            entry = files["gltf"][res]["gltf"]
            save(entry["url"], os.path.join(folder, os.path.basename(entry["url"])))
            for rel, inc in entry["include"].items():
                save(inc["url"], os.path.join(folder, rel))
        else:
            # Some textures (leathers) come in color variants instead of one diffuse map: take the first
            for key in ("Diffuse" if "Diffuse" in files else "coll1", "Rough", "nor_gl"):
                if key in files:
                    url = files[key][res]["jpg"]["url"]
                    save(url, os.path.join(folder, os.path.basename(url)))
        credits[asset] = {
            "name": info.get("name", asset),
            "authors": list(info.get("authors", {}).keys()),
            "source": f"https://polyhaven.com/a/{asset}",
            "license": "CC0 1.0",
        }
        print("ok", asset, res)
    with open(credits_path, "w") as f:
        json.dump(credits, f, indent=2, sort_keys=True)

main()
