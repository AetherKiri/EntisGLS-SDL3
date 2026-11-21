#!/usr/bin/env python3
"""Adapt NCB's engine include boundary to the native host without altering NCB."""

# Support direct execution from any working directory.
import sys as _sys
from pathlib import Path as _BootstrapPath
_sys.path.insert(0, str(next(parent / "tools" for parent in _BootstrapPath(__file__).resolve().parents
                             if (parent / "tools" / "_bootstrap.py").is_file())))
from _bootstrap import ROOT, GENERATED_ROOT

from pathlib import Path
import json
import re
import html
SOURCE = ROOT / 'vendor/kirikiroid2/ncbind'
OUT = GENERATED_ROOT / 'motion-ncbind'
TJS_SOURCE = ROOT / 'vendor/kirikiroid2/tjs2'
TJS_OUT = GENERATED_ROOT / 'motion-tjs-source'


def write_changed(path, text):
    if not path.exists() or path.read_text() != text:
        path.write_text(text)

def use_utf_conversion_header(text):
    # The vendored UTF conversion headers are self-contained. The umbrella
    # header also pulls in Boost.Iterator and other non-vendored modules.
    old = '#include <boost/locale.hpp>'
    assert text.count(old) == 1
    return text.replace(old, '#include <boost/locale/encoding_utf.hpp>')


def prepare_tjs_sources():
    TJS_OUT.mkdir(parents=True, exist_ok=True)
    for path in sorted([*TJS_SOURCE.glob('*.h'), *TJS_SOURCE.glob('*.cpp')]):
        if path.name == 'tjsConfig.cpp':
            continue
        text = path.read_text()
        if path.name == 'tjsInterCodeGen.h':
            old = 'return *(tTJSVariant *)nullptr;'
            assert text.count(old) == 1
            text = text.replace(old, 'TJS_eTJSError(TJSInternalError);')
        elif path.name == 'tjsString.h':
            text = use_utf_conversion_header(text)
            old = 'operator"" _tss'
            assert text.count(old) == 1
            text = text.replace(old, 'operator""_tss')
        elif path.name == 'tjsBinarySerializer.h':
            pattern = (r'} else if\((len|count) <= ULONG_MAX\) {'
                       r'(\n                tjs_uint32 v = \1;.*?stream->Write\(tmp, sizeof\(tmp\)\);)'
                       r'\n            } else {\n                TJS_eTJSError\(TJSWriteError\);')
            text, count = re.subn(pattern, r'} else {\2', text, flags=re.S)
            assert count == 4
        elif path.name in ('tjsCompileControl.cpp', 'tjsDateParser.cpp',
                           'tjsInterCodeGen.cpp', 'tjsInterCodeExec.cpp'):
            pattern = r'^( +)spdlog::get\("tjs2"\)->(.+);$'
            text, count = re.subn(pattern,
                r'\1if(auto logger = spdlog::get("tjs2"))\n\1    logger->\2;', text, flags=re.M)
            assert count == (3 if path.name == 'tjsInterCodeExec.cpp' else 1)
        write_changed(TJS_OUT / path.name, text)


def main():
    prepare_tjs_sources()
    OUT.mkdir(parents=True, exist_ok=True)
    config = use_utf_conversion_header((TJS_SOURCE / 'tjsConfig.cpp').read_text())
    # A bounded, non-NUL-terminated source (including tTJSString(tjs_char))
    # must not read source[len]. ASAN caught the original order doing so.
    old = '        len++;\n        while((ch = *s) != 0 && --len)\n            *(d++) = ch, s++;'
    assert config.count(old) == 1
    config = config.replace(old, '        while(len != 0 && (ch = *s) != 0) {\n            *(d++) = ch; ++s; --len;\n        }')
    # TJS uses char16_t and always encodes its code units in at most three bytes.
    old = '    else if(wc < 0x10000)\n        count = 3;'
    assert config.count(old) == 1
    start = config.index(old)
    end = config.index('    if(n < count)', start)
    config = config[:start] + '    else\n        count = 3;\n' + config[end:]
    old = '#define INTMAX_MAX 0x7fffffffffffffff\n'
    assert config.count(old) == 1
    config = config.replace(old, '')
    write_changed(OUT / 'tjsConfig.cpp', '// SDL portability overlay; vendor source is unchanged.\n' + config)
    for name in ['ncbind.hpp','ncbind.cpp','ncb_invoke.hpp','ncb_foreach.inc']:
        text = (SOURCE / name).read_text()
        if name == 'ncbind.hpp':
            # NCB uses only script-global access, expression evaluation,
            # exception reporting and logging from these engine headers.
            # The adapter implements all of them using the real TJS instance.
            for header in ['StorageImpl.h','ScriptMgnIntf.h','DebugIntf.h','MsgIntf.h','CharacterSet.h']:
                text = text.replace('#include "' + header + '"', '#include "tjs_host.h"')
            text = text.replace('#include <map>', '#include <set>\n#include <map>')
            # These probes are only referenced by sizeof and need no bodies.
            for probe in ('static OK check(TO)', 'static NG check(...)', 'static FROM wrap()'):
                old = probe + ' {};'
                assert text.count(old) == 1
                text = text.replace(old, probe + ';')
        write_changed(OUT / name, '// Generated from imported NCB; see vendor/motion-deps/provenance.json.\n' + text)
    pairs = [(k, html.unescape(v)) for k, v in re.findall(r'<Item\s+id="([^"]+)"\s+text="([^"]*)"', (ROOT / 'vendor/kirikiroid2/tjs_support/en_us.xml').read_text())]
    text = '#include \"tjs.h\"\n#include <unordered_map>\nttstr TVPGetMessageByLocale(const std::string &key) {\n    static const std::unordered_map<std::string,std::string> messages = {\n'
    text += ''.join('        {' + json.dumps(k, ensure_ascii=False) + ',' + json.dumps(v, ensure_ascii=False) + '},\n' for k, v in pairs)
    text += '    };\n    const auto item = messages.find(key);\n    return ttstr(item == messages.end() ? key : item->second);\n}\n'
    write_changed(OUT / 'locale.cpp', text)

if __name__ == '__main__': main()
