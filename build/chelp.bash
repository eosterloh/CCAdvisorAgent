set -euo pipefail

cmake .. -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
ln -sf "$(pwd)/compile_commands.json" ../compile_commands.json

# Compile the code
cmake --build .

# Run the resulting executable
./AdvisorAgBuild