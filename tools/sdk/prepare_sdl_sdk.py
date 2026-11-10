#!/usr/bin/env python3
"""Generate explicit SDL portability overlays, preserving supplied SDK sources."""

# Support direct execution from any working directory.
import sys as _sys
from pathlib import Path as _BootstrapPath
_sys.path.insert(0, str(next(parent / "tools" for parent in _BootstrapPath(__file__).resolve().parents
                             if (parent / "tools" / "_bootstrap.py").is_file())))
from _bootstrap import ROOT

import argparse
import hashlib
import json
import re
from pathlib import Path
import subprocess
import sys
from entis_sdk import COTOPHA, ROOT
from prepare_sdl_window import prepare as prepare_window, write
from prepare_sdl_system import replace_body
from prepare_sdl_graphics import transform_context


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    out = parser.parse_args().output.resolve()
    prepare_window(out)
    subprocess.run([sys.executable, str(ROOT/'tools/sdk/prepare_sdl_sync.py'),
        '--source', str(COTOPHA/'Source/common/sakura/ssys_synchronism.cpp'),
        '--output', str(out/'source/ssys_synchronism.cpp')], check=True)
    subprocess.run([sys.executable, str(ROOT/'tools/sdk/prepare_sdl_system.py'),
        '--sdk', str(COTOPHA), '--output', str(out/'system')], check=True)
    records = []
    generated = {}
    def source_copy(relative, transform):
        src = COTOPHA/relative
        text = transform(src.read_text(encoding='utf-8-sig'))
        target = out/relative
        digest = hashlib.sha256(src.read_bytes()).hexdigest()
        generated[target] = '// SDL overlay input SHA-256 '+digest+'\n'+text
        records.append({'source': str(src.relative_to(ROOT)), 'sha256': digest,
                        'output': str(target.relative_to(out))})

    # C++17 libc++ must not see C stdatomic macros. SDK uses compiler atomic
    # builtins; only the memory-order constant is needed by its common sources.
    source_copy('Include/unix/esl/esl_stddefs.h', lambda s:
        s.replace('#include <stdatomic.h>', '#include <atomic>\nusing std::memory_order_seq_cst;'))
    source_copy('Include/common/sakura/sakura_cpp_presets.h', lambda s:
        '#include <cstdarg>\n'+s.replace('defined(__arm__)',
            '(defined(__arm__) || defined(__aarch64__) || defined(__arm64__))'))

    source_copy('Source/common/sakura/ssys_thread.cpp', lambda s:
        '#include "platform/sdl/synchronization.h"\n'+s.replace('gettid()', 'study::platform::sdl::CurrentThreadId()'))
    source_copy('Source/common/sakura/ssys_heap_memory.cpp', lambda s:
        s.replace('#include <malloc.h>', '#include <cstdlib>')
         .replace('sizePageUnit = PAGE_SIZE', 'sizePageUnit = sizeMinPage'))
    source_copy('Source/common/sakuragl/media/sgl_sound_player.cpp', lambda s:
        s.replace('#error no implement SGLSoundPlayer::Open',
                  '// SDL startup registers a real SGLSoundPlayerInterface creator.'))
    source_copy('Source/common/sakuragl/media/sgl_sound_recorder.cpp', lambda s:
        s.replace('#error no implement SGLSoundRecorder::EnumerateDevices',
                  'if (pwNameBuf && nNameBufLength) pwNameBuf[0] = 0;')
         .replace('#error no implement SGLSoundRecorder::Open',
                  'return sglErrNotSupported; // Recording is not part of this backend.'))
    def font(s):
        style = 'SGLError SGLFont::SetStyle( const SGLFontStyle& style )\n{'
        if s.count(style) != 1: raise ValueError('SDK font style entry changed')
        s = '#include "platform/sdl/system.h"\n' + s.replace(style, '''SGLError SGLFont::SetStyle( const SGLFontStyle& requested )
{
    SGLFontStyle style(requested);
    const auto selected = study::platform::sdl::FontOverride();
    const auto fallback = study::platform::sdl::FontFallback();
    if (!selected.empty()) style.pszFace = selected.c_str();''')
        lookup = 'SGLFontObject *\tpFont = m_pFontStock->GetAs( style.pszFace ) ;'
        if s.count(lookup) != 1: raise ValueError('SDK font stock lookup changed')
        s = s.replace(lookup, lookup + '''
        if (!pFont && !fallback.empty()) {
            pFont = m_pFontStock->GetAs(fallback.c_str());
            if (pFont) style.pszFace = fallback.c_str();
        }''')
        constructor = 'SGLFont::SGLFont( void )\n{\n\tm_pFont = NULL ;\n}'
        if s.count(constructor) != 1: raise ValueError('SDK default font constructor changed')
        s = s.replace(constructor, 'SGLFont::SGLFont( void )\n{\n\tm_pFont = NULL ;\n\tm_flagOwner = false ;\n}')
        old = '''\t\t\tm_pFont = pFont->NewFont( style ) ;
\t\t\tif ( m_pFont != NULL )
\t\t\t{
\t\t\t\tm_flagOwner = true ;
\t\t\t\treturn\tsglErrSuccess ;
\t\t\t}'''
        if s.count(old) != 1: raise ValueError('SDK stock font ownership block changed')
        # This branch already released QuickLock. Failed stock creation must
        # not fall through into the second unlock and a missing system backend.
        return s.replace(old, old + '\n\t\t\treturn sglErrFailed;')
    source_copy('Source/common/sakuragl/sgl2d/sgl_font.cpp', font)
    def fixed_pitch_lettering(s):
        # Fixed character advance changes width, not the glyph cell height.
        # The SDK skipped that height calculation and pushed narrow name text
        # below its canvas when pitchChar was nonzero.
        old = '''\telse if ( !flagVertical )
\t{
\t\tfor ( i = 0; i < nCount; i ++ )'''
        if s.count(old) != 1:
            raise ValueError('SDK fixed-pitch lettering branch changed')
        s = s.replace(old, old.replace('else if', 'if'))
        for dimension in ('w', 'h'):
            old = f'\t\t\twidthChars += pChar->sizeChar.{dimension} ;'
            if s.count(old) != 1:
                raise ValueError('SDK lettering width calculation changed')
            s = s.replace(old, '\t\t\tif ( context.pitchChar == 0 )\n' + old)
        # Without a border pBufLetter is still empty here. Seed the shadow
        # mask from the glyph, otherwise enabling shadow alone paints nothing.
        old = '\t\tsglMultiplierBlendImageBuffer( *(imgShadow.GetImage()), *pBufLetter ) ;'
        if s.count(old) != 1:
            raise ValueError('SDK lettering shadow mask changed')
        s = s.replace(old, '''        if (deco.nFlags & flagBorder) {
            sglMultiplierBlendImageBuffer(*(imgShadow.GetImage()), *pBufLetter);
        } else {
            SGLSmartImage shadowMask;
            shadowMask.CreateBuffer(infLetter);
            sglAdditionalBlendImageBuffer(*shadowMask.GetImage(), *pCharShape,
                -rectExLetter.left, -rectExLetter.top);
            sglMultiplierBlendImageBuffer(*imgShadow.GetImage(), *shadowMask.GetImage());
        }''')
        return s
    source_copy('Source/common/sakuragl/sgl2d/sgl_lettering.cpp', fixed_pitch_lettering)
    def legacy_archive(s):
        start = s.index('SSystem::SError SGLArchiveFile::PrepareDecoder')
        entry = '\tif ( pfeEntry->nEncodeType == encodeERISA )'
        at = s.index(entry, start)
        s = s[:at] + '''    if (pfeEntry->nEncodeType == 0x40000000) {
        m_pInCRC32 = study::platform::sdl::CreateLegacyBshfDecoder(
            pStream, pfeEntry->nBytes, pwszPassword);
        return m_pInCRC32 != nullptr ? errSuccess : errFailed;
    }
''' + s[at:]
        return '#include "platform/sdl/legacy_bshf.h"\n' + s
    source_copy('Source/common/sakuragl/erisa/sgl_erisa_archive_file.cpp', legacy_archive)
    def button_notifications(s):
        old = '\t\t\t\t\t\tm_statusButton, commandNormal, true ) ;'
        if s.count(old) != 1:
            raise ValueError('SDK button status notification changed')
        # Translate only native button status events delivered to a GLS3 tree.
        # The SDK listener, view state and unrelated commands keep GLS4 values.
        return ('#include "runtime/cotopha_port/legacy_button_notification.h"\n' +
                s.replace(old, '\t\t\t\t\t\tTranslateLegacyButtonStatusNotification( pParent, m_statusButton ),\n'
                               '\t\t\t\t\t\tcommandNormal, true ) ;'))
    source_copy('Source/common/sakuraglx/sprite/sglx_sprite_button.cpp', button_notifications)
    def edit_options(s):
        old = 'for ( size_t i = 0; pwszAcceptIDs[i]; i ++ )'
        if s.count(old) != 1:
            raise ValueError('SDK edit acceptance option table changed')
        # Unlike the deny table, the accept table has no null terminator.
        # Stay within both parallel arrays even for unrecognized keywords.
        return s.replace(old, 'for ( size_t i = 0; i < sizeof(pwszAcceptIDs) / sizeof(pwszAcceptIDs[0]); i ++ )')
    source_copy('Source/common/sakuraglx/sprite/sglx_sprite_edit.cpp', edit_options)
    def message_link_input(s):
        old = '\tif ( m_pFocusLink == pHitLink )'
        if s.count(old) != 2:
            raise ValueError('SDK message link mouse handlers changed')
        # Empty/ordinary text has neither a focused nor a hit link. Equality
        # of those two null pointers must not enter the link-click handler.
        return s.replace(old, '\tif ( pHitLink != NULL && m_pFocusLink == pHitLink )')
    source_copy('Source/common/sakuraglx/sprite/sglx_sprite_message.cpp', message_link_input)
    source_copy('Source/common/glscs/glscs_sakura2_jit_native_compiler.cpp', lambda s:
        s.replace('sizePageUnit = PAGE_SIZE', 'return nullptr'))
    def std_application(s):
        s = '#include <SDL3/SDL.h>\n#include <string>\n' + s
        s = s.replace('#error\tno implement SGLStdApplication::GetUserUniqueId',
            'Trace("SDL: machine identity is unavailable\\n");\n\treturn SString();')
        return replace_body(s, 'SGLStdApplication::EnumerateEnvironmentVariableNames', '''
lstVarNames.RemoveAll();
char** variables = SDL_GetEnvironmentVariables(SDL_GetEnvironment());
if (!variables) return;
for (size_t i = 0; variables[i]; ++i) {
    const char* end = strchr(variables[i], '=');
    if (!end) continue;
    std::string name(variables[i], size_t(end - variables[i]));
    auto* value = new SString;
    value->FromUTF8(reinterpret_cast<const uint8_t*>(name.c_str()));
    lstVarNames.Add(value);
}
SDL_free(variables);
''')
    source_copy('Source/common/sakuraglx/sglx_std_app.cpp', std_application)
    loquaty_file = ROOT/'vendor/official-loquaty/Loquaty/source/loquaty_file.cpp'
    loquaty_text = loquaty_file.read_text(encoding='utf-8-sig')
    loquaty_text = loquaty_text.replace('lseek64', 'lseek').replace('off64_t', 'off_t')
    write(out/'loquaty/loquaty_file.cpp', loquaty_text)
    records.append({'source': str(loquaty_file.relative_to(ROOT)),
        'sha256': hashlib.sha256(loquaty_file.read_bytes()).hexdigest(),
        'output': 'loquaty/loquaty_file.cpp'})

    for name, count in (('loquaty_cls_clock.cpp', 3), ('loquaty_date_time.cpp', 2)):
        clock_source = ROOT/'vendor/official-loquaty/Loquaty/source'/name
        clock_text = clock_source.read_text(encoding='utf-8-sig')
        clock_call = 'std::timespec_get( &ts, TIME_UTC )'
        if clock_text.count(clock_call) != count:
            raise ValueError('Loquaty clock entry points changed: ' + name)
        clock_text = clock_text.replace(clock_call, 'EntisTimespecGet( &ts )')
        clock_text = clock_text.replace('#include <loquaty.h>', '''#include <loquaty.h>
static int EntisTimespecGet(std::timespec* value) {
#if defined(__ANDROID__) && __ANDROID_API__ < 29
    return clock_gettime(CLOCK_REALTIME, value) == 0 ? TIME_UTC : 0;
#else
    return std::timespec_get(value, TIME_UTC);
#endif
}''')
        write(out/'loquaty'/name, clock_text)
        records.append({'source': str(clock_source.relative_to(ROOT)),
            'sha256': hashlib.sha256(clock_source.read_bytes()).hexdigest(),
            'output': 'loquaty/' + name})

    # Android's EGL provides a default display and pbuffers, but commonly not
    # desktop device-enumeration extensions. Godot owns the display connection.
    egl_source = ROOT/'vendor/sdl3/src/video/SDL_egl.c'
    egl_text = egl_source.read_text()
    anchor = '    // Check for all extensions that are optional until used and fail if any is missing\n'
    if egl_text.count(anchor) != 1:
        raise ValueError('SDL offscreen EGL initialization changed')
    egl_text = egl_text.replace(anchor, '''#if defined(SDL_PLATFORM_ANDROID)
    _this->egl_data->egl_display = _this->egl_data->eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (_this->egl_data->egl_display == EGL_NO_DISPLAY ||
        !_this->egl_data->eglInitialize(_this->egl_data->egl_display, NULL, NULL)) {
        return SDL_SetError("Could not initialize the Android EGL display");
    }
    SDL_EGL_GetVersion(_this);
    _this->egl_data->is_offscreen = true;
    return true;
#endif

''' + anchor)
    terminate = '            _this->egl_data->eglTerminate(_this->egl_data->egl_display);'
    if egl_text.count(terminate) != 1:
        raise ValueError('SDL EGL display teardown changed')
    egl_text = egl_text.replace(terminate, '''            // Private contexts/surfaces are destroyed by SDL; terminating
            // this process-wide display would also destroy Godot's contexts.''')
    def egl_include(match):
        header = egl_source.parent/match[1]
        return '#include "' + str(header.resolve()) + '"' if header.is_file() else match[0]
    egl_text = re.sub(r'#include "([^"]+)"', egl_include, egl_text)
    write(out/'source/SDL_egl.c', egl_text)
    records.append({'source': str(egl_source.relative_to(ROOT)),
        'sha256': hashlib.sha256(egl_source.read_bytes()).hexdigest(),
        'output': 'source/SDL_egl.c'})

    # The original backend uses operating-system macros to select graphics APIs
    # and WGL ownership. SDL owns contexts on every OS; GLES is a separate choice.
    def graphics(s):
        return (s.replace('__PLATFORM_ANDROID__', 'STUDYSTEADY_GL_ES')
                 .replace('ANDROID_API_LEVEL', 'STUDYSTEADY_GL_API_LEVEL')
                 .replace('__PLATFORM_WINDOWS__', 'STUDYSTEADY_SDK_WGL'))
    for base in ('Include/opengl', 'Source/opengl'):
        for src in sorted((COTOPHA/base).rglob('*')):
            if src.suffix not in ('.h', '.hpp', '.cpp'): continue
            relative = src.relative_to(COTOPHA)
            source_copy(relative, graphics)

    producer = out/'Source/opengl/sakuragl/sgl_opengl_window_producer.cpp'
    text = generated[producer]
    old = '#if\tdefined(STUDYSTEADY_GL_ES)\n\tGLInitializeProcedure'
    if text.count(old) != 1: raise ValueError('SDK context initialization branch changed')
    # A host-supplied context applies equally to desktop GL and mobile GLES.
    text = text.replace(old, '#if !defined(STUDYSTEADY_SDK_WGL)\n\tGLInitializeProcedure')
    generated[producer] = text

    # Texture storage is GLES 3.0 core; compute is GLES 3.1. The SDK groups
    # them under one Android API guard, duplicating GLES3 core declarations.
    ext_header = out/'Include/opengl/sakuragl/sgl_opengl_extension.h'
    text = generated[ext_header]
    # The same GLES 3.0 API is shipped in OpenGLES.framework on iOS. Keep the
    # Khronos include layout unchanged for Android and other GLES platforms.
    es_start = text.index('#define\tGL_GLEXT_PROTOTYPES')
    es_end = text.index('\n#else\n', es_start)
    es_headers = text[es_start:es_end]
    if '#include <GLES3/gl3.h>' not in es_headers:
        raise ValueError('SDK GLES header block changed')
    text = text[:es_start] + '''#if defined(__APPLE__)
#include <OpenGLES/ES1/gl.h>
#include <OpenGLES/ES1/glext.h>
#include <OpenGLES/ES3/gl.h>
#include <OpenGLES/ES3/glext.h>
#include <OpenGLES/ES2/glext.h>
#else
''' + es_headers + '\n#endif\n' + text[es_end:]
    old_pointer_types = '''\t#if\tdefined(__PROCESSOR_INTEL_X86_64__)
//\t\ttypedef int64_t\t\tptrdiff_t ;
\t#else
\t\ttypedef int\t\t\tptrdiff_t ;
\t#endif
\ttypedef ptrdiff_t GLintptr;
\ttypedef ptrdiff_t GLsizeiptr;'''
    if text.count(old_pointer_types) != 1: raise ValueError('SDK GL pointer-sized types changed')
    text = '#include <cstddef>\n' + text.replace(old_pointer_types,
        '\ttypedef std::ptrdiff_t GLintptr;\n\ttypedef std::ptrdiff_t GLsizeiptr;')
    for name in ('glTexStorage2D', 'glTexStorage3D'):
        line = next(line for line in text.splitlines() if 'extern' in line and line.rstrip().endswith(name+' ;'))
        text = text.replace(line, '#if !defined(STUDYSTEADY_GL_ES) || (STUDYSTEADY_GL_API_LEVEL < 18)\n'+line+'\n#endif')
    old_3d_framebuffer = '\t#define\tglFramebufferTexture3D\t\tglFramebufferTexture3DOES'
    if text.count(old_3d_framebuffer) != 1: raise ValueError('SDK GLES 3D attachment alias changed')
    text = text.replace(old_3d_framebuffer, '''#if defined(__APPLE__)
    // iOS provides 3D texture attachment through GLES 3.0 core, not OES_texture_3D.
    #define glFramebufferTexture3D(target, attachment, textarget, texture, level, layer) \\
        glFramebufferTextureLayer(target, attachment, texture, level, layer)
#else
''' + old_3d_framebuffer + '\n#endif')
    generated[ext_header] = text

    # SDK framebuffer zero means the window drawable. UIKit's drawable is an
    # SDL-owned EAGL FBO; retain the meaning when attaching and blitting it.
    for filename in ('sgl_opengl_context.cpp', 'sgl_opengl_render_context.cpp'):
        target = out/'Source/opengl/sakuragl'/filename
        text = generated[target]
        text = '#include "platform/sdl/gl_drawable.h"\n' + text
        calls = list(re.finditer(r'^\s*glBindFramebuffer\s*\(', text, re.MULTILINE))
        expected = 7 if filename == 'sgl_opengl_context.cpp' else 6
        if len(calls) != expected: raise ValueError(f'SDK framebuffer calls changed: {filename}')
        for call in reversed(calls):
            start = call.end()
            depth = 1
            comma = None
            end = start
            while depth:
                character = text[end]
                if character == '(': depth += 1
                elif character == ')': depth -= 1
                elif character == ',' and depth == 1: comma = end
                end += 1
            if comma is None: raise ValueError('SDK framebuffer argument list changed')
            argument = text[comma + 1:end - 1].strip()
            text = text[:comma + 1] + ' study::platform::sdl::ResolveWindowFramebuffer(' + argument + ') ' + text[end - 1:]
        if filename == 'sgl_opengl_render_context.cpp':
            if text.count('glDrawBuffer( GL_BACK )') != 3:
                raise ValueError('SDK window draw-buffer calls changed')
            text = text.replace('glDrawBuffer( GL_BACK )',
                'glDrawBuffer( study::platform::sdl::WindowBackBuffer() )')
        generated[target] = text

    original_header = (COTOPHA/'Include/opengl/sakuragl/sgl_opengl_extension.h').read_text(encoding='utf-8-sig')
    names = sorted(set(re.findall(r'API_(gl\w+)', original_header)))
    # SDL's desktop header declares more than OpenGL 1.1. Hide declarations
    # which the SDK intentionally loads into its own OpenGLExtension namespace.
    desktop_header = '#pragma once\n' + ''.join(f'#define {n} StudySDLUnusedDeclaration_{n}\n' for n in names)
    desktop_header += '#include <SDL3/SDL_opengl.h>\n'
    desktop_header += ''.join(f'#undef {n}\n' for n in names)
    write(out/'include/GL/gl.h', desktop_header)
    write(out/'include/GL/glu.h', '''#pragma once
#include <GL/gl.h>
extern "C" {
const GLubyte* gluErrorString(GLenum error);
GLint gluBuild2DMipmaps(GLenum target, GLint internalFormat, GLsizei width,
                      GLsizei height, GLenum format, GLenum type, const void* pixels);
}
''')
    extension = out/'Source/opengl/sakuragl/sgl_opengl_extension.cpp'
    text = generated[extension]
    old = '#define\tGLEX_LOAD_GLAPI(x)\t\tapi_supported = false'
    if text.count(old) != 1: raise ValueError('SDK GL loader fallback changed')
    text = '#include <SDL3/SDL.h>\n'+text.replace(old, '''#define GLEX_LOAD_GLAPI(x) \\
    OpenGLExtension::x = reinterpret_cast<OpenGLExtension::API_##x>(SDL_GL_GetProcAddress(#x)); \\
    if (!OpenGLExtension::x) api_supported = false''')
    for name in ('glTexStorage2D', 'glTexStorage3D'):
        declaration = 'GLEX_DEFINE_GLAPI('+name+') ;'
        text = text.replace(declaration, '#if !defined(STUDYSTEADY_GL_ES) || (STUDYSTEADY_GL_API_LEVEL < 18)\n'+declaration+'\n#endif')
    generated[extension] = text
    for target, content in generated.items():
        if target == out/'Source/opengl/sakuragl/sgl_opengl_context.cpp':
            content = transform_context(content)
        write(target, content)
    write(out/'source-manifest.json', json.dumps(records, indent=2)+'\n')


if __name__ == '__main__':
    main()
