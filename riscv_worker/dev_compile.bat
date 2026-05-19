rmdir /s /q build
cmake -G Ninja -S . -B build -DBCDEMO_PROTECTION_LEVEL=2 -DBCDEMO_TRACE_DECODED_INSTRUCTIONS=on
cmake --build build