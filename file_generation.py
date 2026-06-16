with open("file.txt", "w") as f:
    # Highly compressible section
    f.write("A" * 50_000_000)

    # Medium compressible section
    for _ in range(5_000_000):
        f.write("ABCDABCDABCD")

    # Natural language section
    for _ in range(1_000_000):
        f.write("The quick brown fox jumps over the lazy dog.\n")