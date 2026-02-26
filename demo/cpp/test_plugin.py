import array
import os
import shutil
import random
import time
from native_layer import NativeManager

def main():
    # 1. Load Plugin
    manager = NativeManager()
    
    # Ensure plugins dir exists
    if not os.path.exists("plugins"):
        os.makedirs("plugins")

    manager.watch_directory("./plugins")
    shutil.copy("plugin_simsimd.dll", "./plugins/plugin_simsimd.dll")


    # 2. Create Massive Vectors (1 Million Float32s)
    DIMS = 1_000_000
    print(f"Generatng {DIMS} dimension vectors...")
    
    # Python's array.array is the most efficient buffer type
    vec_a = array.array('f', [random.random() for _ in range(DIMS)])
    vec_b = array.array('f', [random.random() for _ in range(DIMS)])

    # 3. Benchmark
    print("Running Native SimSIMD...")
    start = time.time()
    
    # Zero-Copy Call
    result = manager.execute("plugin_simsimd", "cosine_similarity", [vec_a, vec_b])
    
    end = time.time()
    
    print(f"✅ Similarity: {result:.6f}")
    print(f"⏱️ Time: {(end - start) * 1000:.4f} ms")

if __name__ == "__main__":
    main()
    