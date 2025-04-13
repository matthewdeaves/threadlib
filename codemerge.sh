#!/bin/bash

# Define the output file name
OUTPUT_FILE="code_review_bundle.txt"
# Define the directory to search (current directory)
PROJECT_DIR="."
# Define directories to exclude
EXCLUDE_DIR="./build"

# Remove the output file if it already exists to start fresh
rm -f "$OUTPUT_FILE"

# Find all .c, .h, and Makefile files, excluding the specified directory
# Use -print0 and read -d $'\0' to handle filenames with spaces or special characters
find "$PROJECT_DIR" \( -name '*.c' -o -name '*.h' -o -name 'Makefile' \) -not -path "$EXCLUDE_DIR/*" -print0 | while IFS= read -r -d $'\0' file; do
  # Print a header indicating the file name to the output file
  echo "=======================================" >> "$OUTPUT_FILE"
  echo "=== File: ${file#./} ===" >> "$OUTPUT_FILE" # Remove leading ./ for cleaner path
  echo "=======================================" >> "$OUTPUT_FILE"
  echo "" >> "$OUTPUT_FILE"

  # Append the content of the current file to the output file
  cat "$file" >> "$OUTPUT_FILE"

  # Add a couple of newlines for separation between files
  echo "" >> "$OUTPUT_FILE"
  echo "" >> "$OUTPUT_FILE"
done

echo "All .c, .h, and Makefile files (excluding $EXCLUDE_DIR) have been concatenated into $OUTPUT_FILE"

exit 0