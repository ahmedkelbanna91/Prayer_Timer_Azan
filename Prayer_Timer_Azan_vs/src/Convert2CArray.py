import gzip
import os

# Get the directory of the script
script_dir = os.path.dirname(os.path.abspath(__file__))

def compress_html(file_name):
    """Compress an HTML file and return its compressed size and C-style array representation."""
    file_path = os.path.join(script_dir, file_name)  # Ensure correct path
    with open(file_path, "rb") as f:
        html_content = f.read()

    compressed_data = gzip.compress(html_content)  # Compress using gzip
    c_array = ", ".join(f"0x{b:02x}" for b in compressed_data)  # Convert to C array
    return len(compressed_data), c_array  # Return size and array

# Get all HTML files in the same directory as the script
html_files = [file for file in os.listdir(script_dir) if file.endswith(".html")]
data_dict = {}

# Process each HTML file in the same folder
for file in html_files:
    size, c_array = compress_html(file)
    key_name = file.replace(" ", "_").replace("-", "_").replace(".", "_")
    data_dict[key_name] = (size, c_array)

# Generate C header file
c_code = """#ifndef WEBPAGE_H
#define WEBPAGE_H

#include <cstddef>
#include <cstdint>

"""

for key, (size, array) in data_dict.items():
    c_code += f"""
const size_t {key}_len = {size};  // Store the size
const uint8_t {key}[] = {{
    {array}
}};"""

c_code += "\n#endif // WEBPAGE_H"

# Save output in the same folder as the script
header_file_path = os.path.join(script_dir, "webPage.h")
with open(header_file_path, "w") as f:
    f.write(c_code)

print(f"✅ C header file 'webPage.h' generated successfully at: {header_file_path}")
