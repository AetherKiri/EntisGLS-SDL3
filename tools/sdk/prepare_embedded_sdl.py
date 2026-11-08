"""Adapt private SDL for an Android host that owns Java and the event loop."""

import argparse
import re
from pathlib import Path


SOURCES = (
    "events/SDL_events.c",
    "thread/pthread/SDL_systhread.c",
    "stdlib/SDL_getenv.c",
    "io/SDL_iostream.c",
    "video/SDL_video.c",
    "audio/openslES/SDL_openslES.c",
    "filesystem/android/SDL_sysfilesystem.c",
    "filesystem/posix/SDL_sysfsops.c",
)


def prepare(source: Path, output: Path) -> None:
    for relative in SOURCES:
        original = source / "src" / relative
        text = original.read_text()
        # The provider supplies input/lifecycle events and filesystem paths.
        # Keep SDL's portable event pump, POSIX I/O and pthread implementations;
        # SDLActivity's Java bindings are absent in the embedding application.
        text = text.replace("#ifdef SDL_PLATFORM_ANDROID",
                            "#if defined(SDL_PLATFORM_ANDROID) && !defined(AETHER_ENTISGLS_EMBEDDED)")
        text = text.replace("#ifndef SDL_PLATFORM_ANDROID",
                            "#if !defined(SDL_PLATFORM_ANDROID) || defined(AETHER_ENTISGLS_EMBEDDED)")
        text = text.replace("#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)",
                            "#if (defined(SDL_PLATFORM_ANDROID) && !defined(AETHER_ENTISGLS_EMBEDDED)) || defined(SDL_PLATFORM_IOS)")
        if relative == "audio/openslES/SDL_openslES.c":
            old = "    impl->ThreadInit = Android_AudioThreadInit;"
            if text.count(old) != 1:
                raise ValueError("SDL OpenSL ES thread initialization changed")
            text = text.replace(old, "    // The native OpenSL ES backend needs no SDLActivity thread callback.")
        if relative == "filesystem/android/SDL_sysfilesystem.c":
            for old in ("    return SDL_GetAndroidPackageName();",
                        "    const char *path = SDL_GetAndroidInternalStoragePath();"):
                if text.count(old) != 1:
                    raise ValueError("SDL Android filesystem entry points changed")
            text = text.replace("    return SDL_GetAndroidPackageName();",
                                "    SDL_Unsupported();\n    return NULL;")
            text = text.replace("    const char *path = SDL_GetAndroidInternalStoragePath();",
                                "    SDL_Unsupported();\n    const char *path = NULL;")

        def include(match):
            header = original.parent / match[1]
            return '#include "' + str(header.resolve()) + '"' if header.is_file() else match[0]

        text = re.sub(r'#include "([^"]+)"', include, text)
        target = output / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        text = "// Altered SDL source for the native Aether host; see prepare_embedded_sdl.py.\n" + text
        if not target.exists() or target.read_text() != text:
            target.write_text(text)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    prepare(args.source.resolve(), args.output.resolve())
