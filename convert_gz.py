import os
import gzip
import shutil

SRC_DIR = "./data_re"
DST_DIR = "./data"

# Tạo thư mục đích nếu chưa có
os.makedirs(DST_DIR, exist_ok=True)

def compress_file(src_path, dst_path):
    with open(src_path, 'rb') as f_in:
        with gzip.open(dst_path, 'wb') as f_out:
            shutil.copyfileobj(f_in, f_out)
    print(f"Đã nén: {src_path} -> {dst_path}")

for root, dirs, files in os.walk(SRC_DIR):
    for file in files:
        if file.endswith((".html", ".css", ".js")):
            src_file = os.path.join(root, file)
            rel_path = os.path.relpath(src_file, SRC_DIR)
            dst_file = os.path.join(DST_DIR, rel_path) + ".gz"

            os.makedirs(os.path.dirname(dst_file), exist_ok=True)
            compress_file(src_file, dst_file)

print("Hoàn tất nén tất cả file.")
