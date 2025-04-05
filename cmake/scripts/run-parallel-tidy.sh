#!/bin/bash

# Script to run clang-tidy in parallel on multiple files
# Usage: ./run-parallel-tidy.sh [compile_commands_dir] [stl_include_path]

set -e

COMPILE_COMMANDS_DIR="${1:-${CMAKE_BINARY_DIR}}"
STL_INCLUDE_PATH="${2:-/usr/include/c++/14}"
OUTPUT_FILE="clang-tidy-output.txt"

# Get number of CPU cores
if [ -f /proc/cpuinfo ]; then
    # Linux
    NUM_CORES=$(grep -c ^processor /proc/cpuinfo)
elif [ "$(uname)" == "Darwin" ]; then
    # macOS
    NUM_CORES=$(sysctl -n hw.ncpu)
else
    # Default to 4 cores if we can't determine
    NUM_CORES=4
fi

echo "Running clang-tidy with $NUM_CORES parallel processes"

# Remove any previous output file
rm -f "$OUTPUT_FILE"

# Get list of all source files to check
FILES=$(find . -type f \( -name "*.cpp" -o -name "*.c" -o -name "*.h" -o -name "*.hpp" \) | grep -v "build/")

# Count total files
TOTAL_FILES=$(echo "$FILES" | wc -l)
echo "Found $TOTAL_FILES files to check"

# Split files into batches for parallel processing
split_files() {
    local files="$1"
    local num_cores="$2"
    local files_per_core=$((TOTAL_FILES / num_cores + 1))
    
    # Create temporary directory for file lists
    mkdir -p .tidy_tmp
    rm -f .tidy_tmp/files_*.txt
    
    echo "$files" | split -l "$files_per_core" - .tidy_tmp/files_
}

# Process a batch of files
process_batch() {
    local batch_file="$1"
    local output_file="${batch_file}.out"
    
    # Extract the batch number for logging
    local batch_num=$(basename "$batch_file" | sed 's/files_//')
    
    echo "Processing batch $batch_num..."
    
    # Run clang-tidy on each file in this batch
    while read -r file; do
        clang-tidy -p "$COMPILE_COMMANDS_DIR" --extra-arg="-I$STL_INCLUDE_PATH" "$file" 2>&1
    done < "$batch_file" > "$output_file"
    
    echo "Completed batch $batch_num"
}

# Split files into batches
split_files "$FILES" "$NUM_CORES"

# Run clang-tidy in parallel on each batch
BATCH_FILES=$(ls .tidy_tmp/files_*)
for batch_file in $BATCH_FILES; do
    process_batch "$batch_file" &
done

# Wait for all background processes to finish
wait

# Combine results
echo "Combining results..."
cat .tidy_tmp/files_*.out > "$OUTPUT_FILE"

# Clean up temporary files
rm -rf .tidy_tmp

echo "Clang-tidy completed. Results saved to $OUTPUT_FILE"

# Check if there were any warnings or errors
if grep -q "warning:" "$OUTPUT_FILE" || grep -q "error:" "$OUTPUT_FILE"; then
    echo "Clang-tidy found issues. Please review $OUTPUT_FILE for details."
    exit 1
else
    echo "No issues found by clang-tidy."
    exit 0
fi