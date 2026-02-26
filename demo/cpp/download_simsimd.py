# demo/download_simsimd.py
import urllib.request
import os

# List of all required headers
FILES = [
    "simsimd.h",
    "binary.h",
    "spatial.h",
    "geospatial.h",
    "sparse.h",
    "probability.h",
    "dot.h",
    "curved.h",
    "elementwise.h",
    "types.h"
]

BASE_URL = "https://raw.githubusercontent.com/ashvardanian/SimSIMD/main/include/simsimd/"
TARGET_DIR = "./"

if not os.path.exists(TARGET_DIR):
    os.makedirs(TARGET_DIR)

print(f"⬇️ Downloading SimSIMD headers into '{TARGET_DIR}/'...")

for file in FILES:
    url = BASE_URL + file
    path = os.path.join(TARGET_DIR, file)
    try:
        urllib.request.urlretrieve(url, path)
    except Exception as e:
        print(f"Failed to download {file}: {e}")

print("Ready to compile!")