#!/usr/bin/env python3
import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

DEPS_STRING_OLD = "#include <limits.h>\n#include <stddef.h>\n#include <string.h>\n"
DEPS_STRING_NEW = "#include <limits.h>\n#include <stddef.h>\n"

DEPS_MALLOC_OLD = """#ifdef ZSTD_DEPS_NEED_MALLOC
#ifndef ZSTD_DEPS_MALLOC
#define ZSTD_DEPS_MALLOC

#include <stdlib.h>

#define ZSTD_malloc(s) malloc(s)
#define ZSTD_calloc(n,s) calloc((n), (s))
#define ZSTD_free(p) free((p))

#endif /* ZSTD_DEPS_MALLOC */
#endif /* ZSTD_DEPS_NEED_MALLOC */"""

DEPS_MALLOC_NEW = """#ifdef ZSTD_DEPS_NEED_MALLOC
#ifndef ZSTD_DEPS_MALLOC
#define ZSTD_DEPS_MALLOC

void* ZSTD_malloc(size_t size);
void* ZSTD_calloc(size_t num, size_t size);
void ZSTD_free(void* ptr);

#endif /* ZSTD_DEPS_MALLOC */
#endif /* ZSTD_DEPS_NEED_MALLOC */"""

XXHASH_OLD = (
    "#include <string.h>   /* memcmp, memcpy */\n#include <limits.h>   /* ULLONG_MAX */"
)
XXHASH_NEW = """#if defined(__GNUC__) && __GNUC__ >= 4
#  define memcpy(d,s,l)  __builtin_memcpy((d),(s),(l))
#  define memmove(d,s,l) __builtin_memmove((d),(s),(l))
#  define memset(p,v,l)  __builtin_memset((p),(v),(l))
#  define memcmp(a,b,l)  __builtin_memcmp((a),(b),(l))
#else
#  include <string.h>   /* memcmp, memcpy */
#endif
#include <limits.h>   /* ULLONG_MAX */"""

DECODER_IN_OLD = "#define ZSTD_TRACE 0\n"
DECODER_IN_NEW = "#define ZSTD_TRACE 0\n#define ZSTD_NO_INLINE\n#define HUF_FORCE_DECOMPRESS_X1\n#define XXH_NO_STDLIB\n"


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text()
    if text.count(old) != 1:
        print(
            f"gen-decoder: expected one match in {path}, found {text.count(old)}",
            file=sys.stderr,
        )
        raise SystemExit(1)
    path.write_text(text.replace(old, new))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("zstd_src", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    src = args.zstd_src.resolve()
    out = args.output.resolve()

    if not (src / "lib/common/zstd_deps.h").is_file():
        print(f"gen-decoder: {src} is not a zstd source tree", file=sys.stderr)
        raise SystemExit(1)

    with tempfile.TemporaryDirectory(prefix="zstd-gen-") as tmp:
        work = Path(tmp) / "zstd"
        shutil.copytree(src, work, ignore=shutil.ignore_patterns(".git"))

        replace_once(work / "lib/common/zstd_deps.h", DEPS_STRING_OLD, DEPS_STRING_NEW)
        replace_once(work / "lib/common/zstd_deps.h", DEPS_MALLOC_OLD, DEPS_MALLOC_NEW)
        replace_once(work / "lib/common/xxhash.h", XXHASH_OLD, XXHASH_NEW)
        replace_once(
            work / "build/single_file_libs/zstddeclib-in.c",
            DECODER_IN_OLD,
            DECODER_IN_NEW,
        )

        combined = Path(tmp) / "zstddeclib.c"
        subprocess.run(
            [
                sys.executable,
                "combine.py",
                "-r",
                "../../lib",
                "-x",
                "legacy/zstd_legacy.h",
                "-o",
                str(combined),
                "zstddeclib-in.c",
            ],
            cwd=work / "build/single_file_libs",
            check=True,
        )

        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text("// NOLINTBEGIN\n" + combined.read_text() + "\n// NOLINTEND\n")


if __name__ == "__main__":
    main()
