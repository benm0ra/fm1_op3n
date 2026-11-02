#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
"""Linux/macOS port of `tools/Build-Web.ps1` HTML assembly (browser preview only, no firmware).
Compile build/web/simulator.wasm first (see OP3N.md), then run from the repo root:
    python3 tools/build_web_preview.py [SOURCE_ARCHIVE_URL] [OUTPUT.html]
Without a URL the page is marked "local preview". For a published page pass the HTTPS URL of
the matching source archive (e.g. a tagged release tarball), as GPLv3 requires."""
import base64, hashlib, html, json, re, pathlib, sys
root = pathlib.Path(__file__).resolve().parent.parent
wasm = (root / 'build/web/simulator.wasm').read_bytes()
version = re.search(r'(?m)^#define FM1_RELEASE_VERSION "(V\d+)"', (root / 'app/version.h').read_text()).group(1)
sim = json.dumps({'schema': 1, 'release_version': version, 'sha256': hashlib.sha256(wasm).hexdigest(),
                  'source_sha256': hashlib.sha256(wasm).hexdigest(), 'sample_bank': 'synthesized-demo',
                  'data': base64.b64encode(wasm).decode()}, separators=(',', ':'))
web = (root / 'sdk/Update.html.in').read_text(encoding='utf-8')
source = sys.argv[1] if len(sys.argv) > 1 else ''
if source and not re.match(r'^https://[^\s@]+$', source):
    sys.exit('source URL must be HTTPS without credentials')
if not source:
    web = re.sub(r'<a\b[^>]*href="/\*FM1_SOURCE_URL\*/"[^>]*>(.*?)</a>', r'<span>\1 (local preview)</span>', web, flags=re.S)
notices = ['APP: GPLv3 with the FM1 SDK linking exception; no warranty. SDK and firmware resources have '
           'separate redistribution terms in THIRD_PARTY.md. Source: ' + (source or '(local preview)')]
for name in ('THIRD_PARTY.md', 'LICENSE.md'):
    notices.append('\n===== %s =====\n' % name + (root / name).read_text(encoding='utf-8'))
rep = {
    '/*FM1_SOURCE_URL*/': html.escape(source, quote=True),
    '/*FM1_PAYLOAD*/': 'const FM1_PACKAGE = null;',
    '/*FM1_SIMULATOR*/': 'const FM1_SIMULATOR = ' + sim + ';',
    '/*FM1_I18N*/': (root / 'sdk/I18n.js').read_text(encoding='utf-8'),
    '/*FM1_SCRIPT*/': (root / 'sdk/WebUpdate.js').read_text(encoding='utf-8'),
    '/*FM1_SIM_SCRIPT*/': (root / 'sdk/Simulator.js').read_text(encoding='utf-8'),
    '/*FM1_SAMPLE_SCRIPT*/': (root / 'sdk/Samples.js').read_text(encoding='utf-8'),
    '/*FM1_NOTICES*/': html.escape('\n'.join(notices), quote=True),
}
for k, v in rep.items():
    web = web.replace(k, v)
assert not re.search(r'/\*FM1_[A-Z_]+\*/', web), 'unexpanded placeholder'
out = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else root / 'build/preview/index.html'
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(web, encoding='utf-8')
print(out, len(web))
