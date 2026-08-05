#!/usr/bin/env python3
"""Divide documentation.html in una pagina per capitolo + indice.

Il visualizzatore e' un QTextBrowser (mainwindow.cpp:1095): niente flexbox,
niente rem, niente border-left. La gerarchia visiva usa solo cio' che il
motore rich text di Qt disegna davvero (verificato a schermo):
bande di sfondo su <p>, <hr>, margini, font-size inline, tabelle con bgcolor.
"""
import re, os, html

SRC = 'documentation.html'
OUT_PREFIX = 'doc_'

# Ordine e titoli dei capitoli, dal TOC originale.
CHAPTERS = [
    ('intro',              'Introduction'),
    ('quickstart',         'Quick Start (First Launch)'),
    ('interface',          'User Interface'),
    ('interaction',        'Interaction &amp; Navigation'),
    ('projection',         'Projection Modes'),
    ('dimensions',         'The 4th Dimension'),
    ('equations',          'Parametric Equations'),
    ('geodesic',           'Geodesic Flow &amp; Non-Euclidean Geometries'),
    ('raymarching',        'Implicit Surfaces (Ray Marching)'),
    ('rendering',          'Rendering &amp; Textures'),
    ('audio',              'Sounds &amp; Audio'),
    ('animation',          'Animation &amp; Video'),
    ('scripting',          'Script Dock &amp; Advanced Scripting'),
    ('library-management', 'Library, Presets &amp; Saving Your Work'),
    ('modular-controls',   'Modular Activation &amp; Master Controls'),
    ('acknowledgments',    'Acknowledgments &amp; Third-Party Code'),
]
IDS = [c[0] for c in CHAPTERS]

# Ancore interne che NON sono capitoli: vanno risolte alla pagina che le contiene.
INNER_ANCHORS = {'rm-artifacts': 'raymarching'}

src = open(SRC, encoding='utf-8').read()

# ---------------------------------------------------------------- stile comune
# Solo proprieta' che Qt onora. I titoli sono <p> con stile inline perche'
# QTextBrowser impone le proprie dimensioni ai tag h2-h4.
STYLE = """
    <style>
        body { background-color: #1e1e1e; color: #d4d4d4;
               font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; }
        p, li, td, th { color: #d4d4d4; }
        strong { color: #007acc; }
        code { background-color: #101010; color: #ce9178;
               font-family: 'Consolas', 'Courier New', monospace; }
        pre  { background-color: #101010; color: #dcdcaa; }
        a { color: #569cd6; }
    </style>
"""

def chapter_banner(title):
    """Titolo di capitolo: banda a piena larghezza (tabella con bgcolor: Qt la
    disegna, mentre ignorerebbe un border-left)."""
    return (
        '<table width="100%" cellpadding="10" cellspacing="0" bgcolor="#0d3a5c">'
        '<tr><td><p style="font-size: 24px; color: #ffffff; margin: 0;">'
        f'<b>{title}</b></p></td></tr></table>\n'
    )

def nav_bar(prev_i, next_i, top=False):
    """Barra di navigazione: indice + capitolo prec./succ."""
    parts = []
    if prev_i is not None:
        pid, ptitle = CHAPTERS[prev_i]
        parts.append(f'<a href="{OUT_PREFIX}{pid}.html">&larr; {ptitle}</a>')
    parts.append('<a href="documentation.html">Contents</a>')
    if next_i is not None:
        nid, ntitle = CHAPTERS[next_i]
        parts.append(f'<a href="{OUT_PREFIX}{nid}.html">{ntitle} &rarr;</a>')
    sep = ' &nbsp;&middot;&nbsp; '
    align = 'margin-bottom: 20px;' if top else 'margin-top: 30px;'
    return (f'<hr>\n<p style="font-size: 15px; {align}">'
            + sep.join(parts) + '</p>\n' + ('<hr>\n' if top else ''))

def fix_links(body, current_id):
    """#ancora -> file della pagina che la contiene."""
    def repl(m):
        target = m.group(1)
        page = INNER_ANCHORS.get(target, target)
        if page == current_id:
            return 'href="#' + target + '"'      # link interno alla stessa pagina
        if page in IDS:
            return f'href="{OUT_PREFIX}{page}.html"'
        return m.group(0)
    body = re.sub(r'href="#([^"]+)"', repl, body)
    # qrc:/script_guide.html -> link relativo. Ora che la documentazione sta in
    # docs/, l'URL assoluto punta a una risorsa che non esiste piu' (il file e'
    # qrc:/docs/script_guide.html) e il link muore in SILENZIO: e' esattamente
    # il caso che la guardia in CMakeLists sorveglia. Relativo funziona sia su
    # disco sia da qrc, senza dipendere dal prefisso.
    body = body.replace('href="qrc:/script_guide.html"', 'href="script_guide.html"')
    return body

def promote_subheads(body):
    """h3t/h4t: aggiunge una banda scura leggera, cosi' i sottotitoli si
    staccano dal testo pur restando sotto il capitolo nella gerarchia."""
    def repl(m):
        cls, style, inner = m.group(1), m.group(2), m.group(3)
        size = '19px' if cls == 'h3t' else '17px'
        # Il <p> vuoto sopra crea il respiro: Qt ignora il margin sulle tabelle,
        # quindi senza di esso il sottotitolo resta incollato al testo precedente.
        return ('<p style="font-size: 6px; margin: 0;">&nbsp;</p>'
                '<table width="100%" cellpadding="6" cellspacing="0" bgcolor="#252526">'
                '<tr><td><p style="font-size: ' + size + '; color: #4ec9b0; margin: 0;">'
                + inner + '</p></td></tr></table>')
    return re.sub(r'<p class="(h3t|h4t)"([^>]*)>(.*?)</p>', repl, body, flags=re.S)

# ------------------------------------------------------------------ estrazione
pages = {}
for i, (sid, title) in enumerate(CHAPTERS):
    m = re.search(r'<section id="%s">(.*?)</section>' % re.escape(sid), src, re.S)
    if not m:
        raise SystemExit('sezione mancante: ' + sid)
    body = m.group(1)

    # via il titolo h2t originale: lo sostituisce la banda
    body = re.sub(r'<p class="h2t"[^>]*>.*?</p>\s*', '', body, count=1, flags=re.S)

    body = fix_links(body, sid)
    body = promote_subheads(body)

    prev_i = i - 1 if i > 0 else None
    next_i = i + 1 if i < len(CHAPTERS) - 1 else None

    page = (
        '<!DOCTYPE html>\n<html lang="en">\n<head>\n'
        '    <meta charset="UTF-8">\n'
        f'    <title>Surface Explorer &mdash; {title}</title>\n'
        + STYLE +
        '</head>\n<body>\n\n'
        + chapter_banner(title)
        + nav_bar(prev_i, next_i, top=True)
        + body.strip() + '\n\n'
        + nav_bar(prev_i, next_i)
        + '\n</body>\n</html>\n'
    )
    pages[f'{OUT_PREFIX}{sid}.html'] = page

# --------------------------------------------------------------------- indice
items = []
for i, (sid, title) in enumerate(CHAPTERS, 1):
    items.append(
        '<tr>'
        f'<td width="40" valign="top"><p style="font-size: 18px; color: #007acc; margin: 4px 0;"><b>{i}</b></p></td>'
        f'<td><p style="font-size: 18px; margin: 4px 0;"><a href="{OUT_PREFIX}{sid}.html">{title}</a></p></td>'
        '</tr>'
    )

index = (
    '<!DOCTYPE html>\n<html lang="en">\n<head>\n'
    '    <meta charset="UTF-8">\n'
    '    <title>Surface Explorer 4D - User Documentation</title>\n'
    + STYLE +
    '</head>\n<body>\n\n'
    '<p align="center" style="font-size: 30px; color: #33cc33;"><b>Surface Explorer</b></p>\n'
    '<p align="center"><img src="icon.png" width="110" height="110"></p>\n'
    '<p align="center" style="font-size: 20px; color: #33cc33;"><b>User Manual</b></p>\n'
    '<hr>\n'
    '<p align="center" style="font-size: 16px; color: #888;">'
    'Select a chapter. Use <b>Back</b> to return here at any time.</p>\n'
    '<hr>\n\n'
    '<table width="100%" cellpadding="6" cellspacing="0">\n'
    + '\n'.join(items) +
    '\n</table>\n\n'
    '<hr>\n'
    '<p style="font-size: 15px;">See also the '
    '<a href="script_guide.html">Script Guide</a> for the scripting language reference.</p>\n'
    '<p align="center" style="font-size: 13px; color: #888;">'
    'Surface Explorer 4D &mdash; Developed by Gaetano Moschetti &mdash; GNU GPL v3</p>\n'
    '<p align="center" style="font-size: 13px; color: #888;">'
    '&copy; 2026 Dioscorid. All rights reserved.</p>\n'
    '\n</body>\n</html>\n'
)
pages['documentation.html'] = index

for name, content in pages.items():
    open(name, 'w', encoding='utf-8').write(content)
    print(f'{name:<34} {len(content):>7} bytes')
print(f'\n{len(pages)} file scritti')
