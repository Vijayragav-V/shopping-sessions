#!/bin/sh
set -eu
arrow_dir=$("${PYTHON:-python3}" -c 'import sysconfig; print(sysconfig.get_path("purelib") + "/pyarrow")')
case "$(uname)" in
  Darwin) arrow=libarrow.2100.dylib; parquet=libparquet.2100.dylib ;;
  Linux) arrow=libarrow.so.2100; parquet=libparquet.so.2100 ;;
esac
mkdir -p build
"${CXX:-c++}" -std=c++20 -O3 -DNDEBUG -pthread -Wall -Wextra -isystem "$arrow_dir/include" cpp/main.cpp "$arrow_dir/$parquet" "$arrow_dir/$arrow" -Wl,-rpath,"$arrow_dir" -o build/shopping
