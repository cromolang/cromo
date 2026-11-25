"""The Cromo Guide: from the Markdown of book/<language>/ to the pages of the site.

    python tools/book.py [--out dist/site] [--check] [--pdf [--spine 21]] [--cromo build/cromo.exe]

Each language is a folder of book/ with SUMMARY.md, the list of its chapters:

    # The Cromo Guide
    - [Introduction](introduction.md)
    - [Getting started](getting-started.md)

The chapters are written in a subset of Markdown: headings, paragraphs, lists,
tables, block quotes, code blocks, images on a line of their own, and inline code,
bold, italic and links. The pages go to <out>/book/<language>/, with print.html,
the whole guide in one page for the PDF, and search.json for the search box.

The images are in book/images/, for every language: ![The text](images/app.png)
shows one with its text below it. --check puts the folder next to the programs
too, so that an example can embed() a file of it.

A code block says its language and some flags after the backticks:

    ```cromo                  a program, if it has main(): --check runs it
    ```cromo norun            compiled but not run (a server, a program that waits)
    ```cromo error            must not compile
    ```cromo nocheck          run, but its output is not compared
    ```cromo ignore           not compiled: a fragment that is not a program
    ```cromo file=shapes.cro  a file next to the programs of the chapter; its name is shown
    ```cromo args=notes.txt   the arguments of the program, separated by commas
    ```output                 what the program before prints, or the errors of an error block

A line of a cromo block that starts with `# ` (or is only `#`) is hidden: it is part
of the program that --check compiles and runs, but the page shows it only when the
reader asks for the whole program, and the PDF never. So an example can show only
the lines that matter, and stay a complete program.

--check compiles and runs every program of every language, and compares what it
prints with the output block that follows it. --pdf prints print.html of each
language to <out>/book/<file>.pdf with Microsoft Edge or Chrome, headless, as a
book of 165 x 240 mm: a cover, the contents with the pages, and a chapter on each
new page. It prints more than once: the pages of the chapters and of their sections
come from the PDF before, from the destinations of its links.

--pdf always makes the file that a printer asks for too, <file>-press.pdf: first
the cover open, with the back, the spine and the front on one sheet, then the inside
of the cover, white, then the pages one by one, starting from a title page. Every
sheet has 3 mm of bleed on its four sides, so a page is 171 x 246 mm, and texts and
marks stay at least 5 mm inside the cut. The spine is as wide as --spine says, the
measure that the printer gives for the pages and the paper chosen, or the pages
times --paper. It needs pypdf, which joins the sheets.
"""

import argparse
import datetime
import html
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.parse

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BOOK = os.path.join(ROOT, 'book')
REPOSITORY = 'https://github.com/cromolang/cromo'

LANGUAGES = {
    'en': {
        'name': 'English', 'pdf': 'cromo-guide-en.pdf', 'press_pdf': 'cromo-guide-en-press.pdf',
        'back_title': 'Native code, a familiar syntax, and no pointers.',
        'back_text': [
            'Cromo is a compiled language with the syntax of C and C++, no pointers and no garbage collector. Every value has '
            'one owner, and what it holds is released when the owner ends. The compiler checks ownership with a few rules that '
            'stay simple.',
            'This guide teaches the whole language, from the first program to a web service, with the library, the tools and '
            'what Cromo checks for you.'],
        'back_inside': ['Ownership, moves and references', 'Structs, enums and generics', 'Errors and exceptions',
                        'Threads, tasks and channels', 'Files, networking, HTTP and JSON', 'Databases and regular expressions',
                        'Windows and apps, also for Android', 'Calling C, and the unsafe dialect'],
        'back_comment': 'Every example in this book was compiled and run.',
        'previous': 'Previous', 'next': 'Next', 'search': 'Search the guide',
        'pdf_link': 'PDF', 'contents': 'Contents', 'theme': 'Light or dark theme',
        'site': 'cromolang.org', 'source': 'Source', 'no_results': 'No results', 'download': 'Download',
        'menu': 'Chapters', 'chapter': 'Chapter', 'appendix': 'Appendix',
        'subtitle': 'The Cromo programming language',
        'version': 'Version {} · {}',
        'months': 'January February March April May June July August September October November December',
        'license': 'The text and the examples are part of the sources of Cromo, under the MIT or the Apache 2.0 license.',
        'checked': 'Every example of this guide was compiled and run with cromo {}, and its output checked, when the guide was made.',
        'library': 'The library', 'core': 'The core', 'types': 'Types', 'functions': 'Functions',
        'builtin_functions': 'Built-in functions', 'builtin_types': 'Built-in types',
        'library_note': 'Made from the documentation in the sources of the library.',
    },
    'it': {
        'name': 'Italiano', 'pdf': 'cromo-guida-it.pdf', 'press_pdf': 'cromo-guida-it-stampa.pdf',
        'back_title': 'Codice nativo, una sintassi familiare, e niente puntatori.',
        'back_text': [
            'Cromo è un linguaggio compilato con la sintassi del C e del C++, senza puntatori e senza garbage collector. Ogni '
            'valore ha un solo proprietario, e ciò che detiene viene rilasciato quando il proprietario finisce. Il compilatore '
            'controlla il possesso con poche regole, che restano semplici.',
            'Questa guida insegna tutto il linguaggio, dal primo programma a un servizio web, con la libreria, gli strumenti e '
            'ciò che Cromo controlla per te.'],
        'back_inside': ['Possesso, spostamenti e riferimenti', 'Struct, enum e generici', 'Errori ed eccezioni',
                        'Thread, task e canali', 'File, rete, HTTP e JSON', 'Database ed espressioni regolari',
                        'Finestre e app, anche per Android', 'Chiamare il C, e il dialetto unsafe'],
        'back_comment': 'Ogni esempio di questo libro è stato compilato ed eseguito.',
        'previous': 'Precedente', 'next': 'Successivo', 'search': 'Cerca nella guida',
        'pdf_link': 'PDF', 'contents': 'Indice', 'theme': 'Tema chiaro o scuro',
        'site': 'cromolang.org', 'source': 'Sorgenti', 'no_results': 'Nessun risultato', 'download': 'Scarica',
        'menu': 'Capitoli', 'chapter': 'Capitolo', 'appendix': 'Appendice',
        'subtitle': 'Il linguaggio di programmazione Cromo',
        'version': 'Versione {} · {}',
        'months': 'gennaio febbraio marzo aprile maggio giugno luglio agosto settembre ottobre novembre dicembre',
        'license': 'Il testo e gli esempi fanno parte dei sorgenti di Cromo, con la licenza MIT o Apache 2.0.',
        'checked': 'Ogni esempio di questa guida è stato compilato ed eseguito con cromo {}, e il suo output verificato, '
                   'quando la guida è stata generata.',
        'print_contents': 'Sommario',
        'library': 'La libreria', 'core': 'Il nucleo', 'types': 'Tipi', 'functions': 'Funzioni',
        'builtin_functions': 'Funzioni predefinite', 'builtin_types': 'Tipi predefiniti',
        'library_note': 'Generata dalla documentazione nei sorgenti della libreria, che è in inglese.',
    },
}

KEYWORDS = set('''struct interface enum import extern unsafe const void if else for in break
continue ret switch case default try catch finally throw ref move public true false func'''.split())
TYPES = set('i8 i16 i32 i64 u8 u16 u32 u64 f32 f64 bool char string list map addr Self'.split())
BUILTINS = set('''print println eprint eprintln format panic assert wrapAdd wrapSub wrapMul embed countOnes trailingZeros
leadingZeros rotateLeft rotateRight fma'''.split())


# ---- Highlighting ----

TOKEN = re.compile(r'''
    (?P<comment>//[^\n]*|/\*.*?\*/)
  | (?P<string>"(?:\\.|[^"\\\n])*")
  | (?P<char>'(?:\\.|[^'\\\n])+')
  | (?P<attribute>\[\[[^\]\n]*\]\])
  | (?P<number>\b(?:0x[0-9A-Fa-f_]+|0b[01_]+|\d[\d_]*(?:\.\d[\d_]*)?(?:[eE][+-]?\d+)?)\b)
  | (?P<word>[A-Za-z_][A-Za-z0-9_]*)
  | (?P<other>.)
''', re.S | re.X)


# A line of code longer than the printed page is wide: the PDF breaks it, and its
# other lines start further in than its first, so that they read as its continuation.
LONG = 100


def lines_of(text, shown):
    """The lines of a block of code, each as `shown` writes it, the long ones marked for the PDF."""
    parts = []
    for line in text.split('\n'):
        if len(line) > LONG:
            indent = len(line) - len(line.lstrip(' '))
            parts.append(f'<span class="long" style="--indent: {indent}">{shown(line)}\n</span>')
        else:
            parts.append(shown(line) + '\n')
    code = ''.join(parts)
    return code[:-1] if code.endswith('\n') else code


def highlight(code):
    out = []
    for m in TOKEN.finditer(code):
        kind, text = m.lastgroup, m.group()
        escaped = html.escape(text, quote=False)
        if kind == 'comment':
            out.append(f'<span class="tk-c">{escaped}</span>')
        elif kind in ('string', 'char'):
            out.append(f'<span class="tk-s">{escaped}</span>')
        elif kind == 'number':
            out.append(f'<span class="tk-n">{escaped}</span>')
        elif kind == 'attribute':
            out.append(f'<span class="tk-a">{escaped}</span>')
        elif kind == 'word':
            rest = code[m.end():]
            if text in KEYWORDS:
                out.append(f'<span class="tk-k">{escaped}</span>')
            elif text in TYPES or (text[0].isupper() and not text.isupper()):
                out.append(f'<span class="tk-t">{escaped}</span>')
            elif text in BUILTINS or rest.startswith('(') or re.match(r'<[\w, <>]*>\(', rest):
                out.append(f'<span class="tk-f">{escaped}</span>')
            else:
                out.append(escaped)
        else:
            out.append(escaped)
    return ''.join(out)


def highlight_shell(code):
    lines = []
    for line in code.split('\n'):
        m = re.match(r'(.*?)(\s+#\s.*)?$', line)
        command, comment = m.group(1), m.group(2) or ''
        text = html.escape(command, quote=False)
        if comment:
            text += f'<span class="tk-c">{html.escape(comment, quote=False)}</span>'
        lines.append(text)
    return '\n'.join(lines)


# ---- Markdown ----

def slug(text):
    text = re.sub(r'<[^>]+>', '', text)
    text = html.unescape(text).lower()
    text = re.sub(r'[`*_\[\]()]', '', text)
    text = re.sub(r'[^\w\s-]', '', text, flags=re.U)
    return re.sub(r'[\s]+', '-', text.strip())


def inline(text, links):
    parts = re.split(r'(`[^`]+`)', text)
    out = []
    for part in parts:
        if part.startswith('`') and part.endswith('`') and len(part) > 1:
            out.append('<code>' + html.escape(part[1:-1], quote=False) + '</code>')
            continue
        part = html.escape(part, quote=False)
        part = re.sub(r'\*\*(.+?)\*\*', r'<strong>\1</strong>', part)
        part = re.sub(r'(?<![\w*])\*(?!\s)(.+?)(?<!\s)\*(?![\w*])', r'<em>\1</em>', part)

        def link(m):
            label, target = m.group(1), html.unescape(m.group(2))
            return f'<a href="{html.escape(links(target))}">{label}</a>'
        part = re.sub(r'\[([^\]]+)\]\(([^)\s]+)\)', link, part)
        out.append(part)
    return ''.join(out)


HIDDEN = re.compile(r'^#(?: |$)')


def program(text):
    """The whole program of a cromo block, with its hidden lines."""
    return '\n'.join(HIDDEN.sub('', line, count=1) for line in text.split('\n'))


class Block:
    def __init__(self, kind, **fields):
        self.kind = kind
        self.__dict__.update(fields)


def parse(text):
    """The blocks of a chapter: heading, paragraph, list, table, quote, code."""
    lines = text.replace('\r\n', '\n').split('\n')
    blocks = []
    i = 0
    while i < len(lines):
        line = lines[i]
        if not line.strip():
            i += 1
            continue
        fence = re.match(r'^(```+)\s*(.*)$', line)
        if fence:
            info = fence.group(2).split()
            body = []
            i += 1
            while i < len(lines) and not lines[i].startswith(fence.group(1)):
                body.append(lines[i])
                i += 1
            i += 1
            language = info[0] if info else ''
            flags = {}
            for item in info[1:]:
                key, _, value = item.partition('=')
                flags[key] = value or True
            blocks.append(Block('code', language=language, flags=flags, text='\n'.join(body)))
            continue
        heading = re.match(r'^(#{1,4})\s+(.*)$', line)
        if heading:
            blocks.append(Block('heading', level=len(heading.group(1)), text=heading.group(2).strip()))
            i += 1
            continue
        if line.startswith('>'):
            body = []
            while i < len(lines) and lines[i].startswith('>'):
                body.append(lines[i][1:].lstrip() if len(lines[i]) > 1 else '')
                i += 1
            blocks.append(Block('quote', blocks=parse('\n'.join(body))))
            continue
        image = re.match(r'^!\[([^\]]*)\]\(([^)\s]+)\)\s*$', line)
        if image:
            blocks.append(Block('image', text=image.group(1), source=image.group(2)))
            i += 1
            continue
        if line.startswith('|'):
            rows = []
            while i < len(lines) and lines[i].startswith('|'):
                cells = [c.strip().replace('\\|', '|') for c in re.split(r'(?<!\\)\|', lines[i].strip()[1:-1])]
                if not all(re.match(r'^:?-+:?$', c) for c in cells):
                    rows.append(cells)
                i += 1
            blocks.append(Block('table', rows=rows))
            continue
        item = re.match(r'^(\s*)([-*]|\d+\.)\s+(.*)$', line)
        if item:
            ordered = item.group(2)[0].isdigit()
            items = []
            while i < len(lines):
                m = re.match(r'^(\s*)([-*]|\d+\.)\s+(.*)$', lines[i])
                if m and m.group(1) == '':
                    items.append([m.group(3)])
                    i += 1
                elif items and lines[i].startswith('  ') and lines[i].strip():
                    items[-1].append(lines[i].strip())
                    i += 1
                elif items and not lines[i].strip() and i + 1 < len(lines) and re.match(r'^([-*]|\d+\.)\s', lines[i + 1]) \
                        and bool(re.match(r'^\d', lines[i + 1])) == ordered:
                    i += 1
                else:
                    break
            blocks.append(Block('list', ordered=ordered, items=[' '.join(it) for it in items]))
            continue
        body = []
        while i < len(lines) and lines[i].strip() and not re.match(r'^(```|#{1,4}\s|>|\||!\[|\s*([-*]|\d+\.)\s)', lines[i]):
            body.append(lines[i].strip())
            i += 1
        blocks.append(Block('paragraph', text=' '.join(body)))
    return blocks


def render(blocks, links, headings):
    out = []
    for b in blocks:
        if b.kind == 'heading':
            content = inline(b.text, links)
            anchor = slug(content)
            headings.append((b.level, anchor, re.sub(r'<[^>]+>', '', content)))
            if b.level == 1:
                out.append(f'<h1 id="{anchor}">{content}</h1>')
            else:
                out.append(f'<h{b.level} id="{anchor}"><a class="anchor" href="#{anchor}">#</a>{content}</h{b.level}>')
        elif b.kind == 'paragraph':
            out.append(f'<p>{inline(b.text, links)}</p>')
        elif b.kind == 'list':
            tag = 'ol' if b.ordered else 'ul'
            items = ''.join(f'<li>{inline(t, links)}</li>' for t in b.items)
            out.append(f'<{tag}>{items}</{tag}>')
        elif b.kind == 'table':
            head = ''.join(f'<th>{inline(c, links)}</th>' for c in b.rows[0])
            body = ''.join('<tr>' + ''.join(f'<td>{inline(c, links)}</td>' for c in row) + '</tr>' for row in b.rows[1:])
            out.append(f'<div class="table-wrap"><table><thead><tr>{head}</tr></thead><tbody>{body}</tbody></table></div>')
        elif b.kind == 'quote':
            out.append('<blockquote>' + render(b.blocks, links, []) + '</blockquote>')
        elif b.kind == 'image':
            # The pages of each language are in book/<language>/, the images in book/images/.
            alt = html.escape(b.text.replace('`', ''))
            out.append(f'<figure><img src="../{html.escape(b.source)}" alt="{alt}">'
                       f'<figcaption>{inline(b.text, links)}</figcaption></figure>')
        elif b.kind == 'code':
            lines = b.text.split('\n')
            hidden = 0
            if b.language == 'cromo':
                parts = []
                for line in lines:
                    if HIDDEN.match(line):
                        hidden += 1
                        parts.append(f'<span class="hidden-line">{highlight(HIDDEN.sub("", line, count=1))}\n</span>')
                    else:
                        shown = lines_of(line, highlight)  # with its end of line inside, when it is long
                        parts.append(shown if len(line) > LONG else shown + '\n')
                code = ''.join(parts)
                code = code[:-1] if code.endswith('\n') else code
            elif b.language in ('sh', 'shell', 'powershell', 'console'):
                code = highlight_shell(b.text)
            else:
                code = lines_of(b.text, lambda line: html.escape(line, quote=False))
            css = 'output' if b.language in ('output', 'text') else 'code'
            if b.flags.get('error'):
                css += ' does-not-compile'
            if hidden:
                css += ' has-hidden'
            if len(lines) - hidden <= 16:
                css += ' keep'  # printed on one page
            name = b.flags.get('file')
            caption = f'<div class="code-name">{html.escape(name)}</div>' if isinstance(name, str) else ''
            label = {'output': '', 'text': '', 'cromo': 'cromo'}.get(b.language, b.language)
            tag = f'<span class="code-lang">{html.escape(label)}</span>' if label else ''
            out.append(f'<div class="{css}">{caption}{tag}<pre><code>{code}</code></pre></div>')
    return '\n'.join(out)


# ---- The reference of the library (D519) ----

def library_reference(cromo):
    """The modules of the library with their declarations and documentation, as
    `cromo build --reference` writes them; None without cromo."""
    if not cromo or not os.path.exists(cromo):
        print(f'book: no reference of the library: {cromo} is missing')
        return None
    folder = tempfile.mkdtemp(prefix='cromo-reference-')
    path = os.path.join(folder, 'reference.json')
    try:
        result = subprocess.run([cromo, 'build', '--reference', path], capture_output=True, text=True, encoding='utf-8')
        if result.returncode != 0:
            print(f'book: no reference of the library:\n{result.stderr}')
            return None
        return json.load(open(path, encoding='utf-8'))['modules']
    finally:
        shutil.rmtree(folder, ignore_errors=True)


def library_page_name(module):
    return 'core.html' if module == 'std' else module + '.html'


# The decisions in the documentation of the library, as (D504) or (D37, D98, P13):
# for those who work on Cromo, not on the pages of the site.
DECISIONS = re.compile(r' ?\((?:[DP]\d+(?:[-\u2013][DP]?\d+)?(?:, )?)+\)')


def one_line(doc):
    return ' '.join(line.strip() for line in doc.split('\n') if line.strip())


def library_markdown(module, words):
    """A page of a module as Markdown: its documentation, then its types with their
    members and its functions, each with its signature and its documentation."""
    title = words['core'] if module['name'] == 'std' else module['name']
    out = [f'# {title}', '', module['doc'], '', f'*{words["library_note"]}*', '']

    def code(signature):
        out.extend(['```cromo', signature, '```'])

    def section(heading, items):
        if not items:
            return
        out.extend([f'## {heading}', ''])
        for item in items:
            out.extend([f'### {item["name"]}', ''])
            code(item['signature'])
            out.extend(['', item['doc'], ''])
            for m in item.get('members', []):
                if m['kind'] == 'constructor':
                    code(m['signature'])
                    out.extend(['', m['doc'], ''])
            simple = [m for m in item.get('members', []) if m['kind'] in ('field', 'case')]
            if simple:
                for m in simple:
                    text = f'- `{m["signature"]}`'
                    if m['doc']:
                        text += ': ' + one_line(m['doc'])
                    out.append(text)
                out.append('')
            for m in item.get('members', []):
                if m['kind'] in ('field', 'case', 'constructor'):
                    continue
                out.extend([f'#### {item["name"]}.{m["name"]}', ''])
                code(m['signature'])
                out.extend(['', m['doc'], ''])
    items = module['items']
    section(words['builtin_functions'], [i for i in items if i['kind'] == 'builtin'])
    section(words['builtin_types'], [i for i in items if i['kind'] == 'type'])
    section(words['types'], [i for i in items if i['kind'] in ('struct', 'enum', 'interface')])
    section(words['functions'], [i for i in items if i['kind'] == 'function'])
    return DECISIONS.sub('', '\n'.join(out))


# ---- The pages ----

NUMBERED = {}  # for each language, how many chapters are numbered: the appendices follow a line ---
LIBRARY = {}   # for each language, the index of the first page of the library (D519)


def read_summary(language):
    path = os.path.join(BOOK, language, 'SUMMARY.md')
    title, chapters = None, []
    for line in open(path, encoding='utf-8'):
        m = re.match(r'^#\s+(.*)$', line)
        if m:
            title = m.group(1).strip()
        if line.startswith('---'):
            NUMBERED[language] = len(chapters)
        m = re.match(r'^-\s+\[(.+)\]\((.+\.md)\)', line.strip())
        if m:
            chapters.append((m.group(1), m.group(2)))
    NUMBERED.setdefault(language, len(chapters))
    return title, chapters


def page_name(index, source):
    return 'index.html' if index == 0 else source[:-3] + '.html'


def chapter_links(chapters):
    names = {source: page_name(i, source) for i, (_, source) in enumerate(chapters)}

    def links(target):
        file, _, anchor = target.partition('#')
        if file in names:
            return names[file] + ('#' + anchor if anchor else '')
        return target
    return links


def sidebar(chapters, current, words, headings, numbered, library=None):
    items = []
    for i, (title, source) in enumerate(chapters):
        number = f'<span class="num">{i}.</span>' if 0 < i < numbered else ''
        if i == numbered:
            items.append('<li class="separator"></li>')
        if i == library:
            items.append(f'<li class="separator"></li><li class="part">{html.escape(words["library"])}</li>')
        active = ' class="active"' if i == current else ''
        items.append(f'<li{active}><a href="{page_name(i, source)}">{number}{html.escape(title)}</a>')
        if i == current and headings:
            sub = ''.join(f'<li><a href="#{a}">{html.escape(t)}</a></li>' for level, a, t in headings if level == 2)
            if sub:
                items.append(f'<ul class="sections">{sub}</ul>')
        items.append('</li>')
    return f'<nav class="toc" aria-label="{words["contents"]}"><ul>{"".join(items)}</ul></nav>'


LOGO = '''<svg width="26" height="26" viewBox="0 0 64 64" aria-hidden="true"><defs><linearGradient id="o" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#1b2230"/><stop offset=".38" stop-color="#5d697b"/><stop offset=".52" stop-color="#9ba6b5"/><stop offset=".72" stop-color="#465164"/><stop offset="1" stop-color="#171d29"/></linearGradient><radialGradient id="n" cx=".35" cy=".3" r=".75"><stop offset="0" stop-color="#fff"/><stop offset=".35" stop-color="#cfd6df"/><stop offset=".8" stop-color="#7c8797"/><stop offset="1" stop-color="#4b5565"/></radialGradient></defs><g transform="translate(32 32) rotate(-28)"><path class="orbit" d="M23.84 -5.16A27 11 0 1 0 23.84 5.16" fill="none" stroke="url(#o)" stroke-width="4" stroke-linecap="round"/><circle cx="23.84" cy="5.16" r="3.6" fill="#1d7a4c"/></g><circle cx="32" cy="32" r="9" fill="url(#n)" stroke="#3a4454" stroke-width="1"/></svg>'''

FONTS = ('<link rel="preconnect" href="https://fonts.googleapis.com"><link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>'
         '<link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600&family=JetBrains+Mono:wght@400;500;600'
         '&family=Space+Grotesk:wght@500;600;700&display=swap" rel="stylesheet">')

THEME_SCRIPT = ("<script>try{var t=localStorage.getItem('cromo-theme');if(t)document.documentElement.dataset.theme=t;}catch(e){}</script>")


def page(language, title, book_title, chapters, index, body, headings, previous, following):
    words = LANGUAGES[language]
    other = [l for l in LANGUAGES if l != language]
    switch = ''.join(f'<a class="lang" href="../{l}/{page_name(index, chapters[index][1])}" hreflang="{l}">{l.upper()}</a>' for l in other)
    nav = '<div class="pager">'
    if previous:
        nav += f'<a class="prev" href="{previous[0]}"><small>{words["previous"]}</small>{html.escape(previous[1])}</a>'
    else:
        nav += '<span></span>'
    if following:
        nav += f'<a class="next" href="{following[0]}"><small>{words["next"]}</small>{html.escape(following[1])}</a>'
    nav += '</div>'
    return f'''<!doctype html>
<html lang="{language}">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{html.escape(title)} · {html.escape(book_title)}</title>
<meta name="description" content="{html.escape(book_title)}: {html.escape(title)}">
<link rel="icon" href="../../logo-mark.svg" type="image/svg+xml">
{FONTS}
<link rel="stylesheet" href="../book.css">
{THEME_SCRIPT}
</head>
<body data-language="{language}">
<header class="bar">
  <button class="menu" aria-label="{words['menu']}" aria-expanded="false"><span></span></button>
  <a class="brand" href="../../">{LOGO}<b>cromo</b></a>
  <a class="book-title" href="index.html">{html.escape(book_title)}</a>
  <div class="tools">
    <input class="search" type="search" placeholder="{words['search']}" aria-label="{words['search']}" data-empty="{words['no_results']}">
    <a class="get" href="../../download/">{words['download']}</a>
    <a class="pdf" href="../{words['pdf']}" download>{words['pdf_link']}</a>
    {switch}
    <button class="theme" aria-label="{words['theme']}" title="{words['theme']}"></button>
  </div>
</header>
<div class="results" hidden></div>
<div class="layout">
<aside>{sidebar(chapters, index, words, headings, NUMBERED[language], LIBRARY.get(language))}</aside>
<main>
<article>
{body}
</article>
{nav}
<footer><a href="../../">{words['site']}</a> · <a href="../../download/">{words['download']}</a> · <a href="{REPOSITORY}">{words['source']}</a></footer>
</main>
</div>
<script src="../book.js"></script>
</body>
</html>
'''


PRINT_FONTS = FONTS.replace('&display=swap', '&family=Source+Serif+4:ital,opsz,wght@0,8..60,400;0,8..60,600;1,8..60,400'
                                             '&display=swap')


def version():
    text = open(os.path.join(ROOT, 'CMakeLists.txt'), encoding='utf-8').read()
    return re.search(r'project\(cromo VERSION ([0-9.]+)', text).group(1)


def chapter_parts(language, index, title):
    """The label, the number and the title of a chapter in the book: ('Chapter', '3',
    'Ownership'), ('Appendix', 'A', 'The command cromo'), or ('', '', 'Introduction')."""
    words = LANGUAGES[language]
    m = re.match(r'^(?:Appendix|Appendice)\s+([A-Z])\s*[:.]\s*(.*)$', title)
    if m:
        return words['appendix'], m.group(1), m.group(2)
    if 0 < index < NUMBERED[language]:
        return words['chapter'], str(index), title
    return '', '', title


def print_chapter(language, chapters, index, body, headings):
    """A chapter of the book: the ids take the chapter in front, as the same section
    can be in several chapters; the links between the pages become anchors; the
    first heading opens the chapter, and the sections take their number."""
    label, number, title = chapter_parts(language, index, chapters[index][0])
    pages = {page_name(i, source): i for i, (_, source) in enumerate(chapters)}
    body = re.sub(r'id="([^"]+)"', lambda m: f'id="c{index}-{m.group(1)}"', body)
    body = re.sub(r'href="#([^"]+)"', lambda m: f'href="#c{index}-{m.group(1)}"', body)

    def link(m):
        target = pages.get(m.group(1) + '.html')
        if target is None:
            return m.group(0)
        return f'href="#c{target}-{m.group(2)}"' if m.group(2) else f'href="#chapter-{target}"'
    body = re.sub(r'href="([\w-]+)\.html(?:#([^"]*))?"', link, body)
    opener = '<header class="opener">'
    if label:
        opener += f'<div class="label">{html.escape(label)}</div><div class="number">{number}</div>'
    opener += f'<h1>{html.escape(title)}</h1></header>'
    body = re.sub(r'<h1 id="[^"]*">.*?</h1>', lambda m: opener, body, count=1, flags=re.S)
    sections = []

    def section(m):
        sections.append((m.group(1), re.sub(r'<[^>]+>', '', m.group(2))))
        mark = f'<span class="section-number">{number}.{len(sections)}</span>' if number else ''
        return f'<h2 id="{m.group(1)}">{mark}{m.group(2)}</h2>'
    body = re.sub(r'<h2 id="([^"]+)"><a class="anchor" href="#[^"]*">#</a>(.*?)</h2>', section, body, flags=re.S)
    body = re.sub(r'<a class="anchor" href="#[^"]*">#</a>', '', body)
    running = f'{number} · {title}' if number else title
    return {'label': label, 'number': number, 'title': title, 'running': running, 'body': body,
            'sections': [(f'{number}.{k + 1}' if number else '', anchor, text) for k, (anchor, text) in enumerate(sections)]}


# The cover: the mark of Cromo in chrome, drawn large on the front.
COVER_DEFS = ('<svg class="cover-defs" aria-hidden="true"><defs>'
              '<linearGradient id="cover-chrome" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#3a4658"/>'
              '<stop offset=".3" stop-color="#9aa6b6"/><stop offset=".5" stop-color="#f1f4f8"/><stop offset=".68" stop-color="#8794a6"/>'
              '<stop offset="1" stop-color="#2a3444"/></linearGradient>'
              '<radialGradient id="cover-ball" cx=".35" cy=".3" r=".75"><stop offset="0" stop-color="#fff"/>'
              '<stop offset=".35" stop-color="#cfd6df"/><stop offset=".8" stop-color="#7c8797"/><stop offset="1" stop-color="#4b5565"/>'
              '</radialGradient><radialGradient id="cover-glow" cx=".5" cy=".5" r=".5"><stop offset="0" stop-color="#1d7a4c" '
              'stop-opacity=".55"/><stop offset="1" stop-color="#1d7a4c" stop-opacity="0"/></radialGradient></defs></svg>')


def cover_mark(css, hero=False):
    """The mark, small next to the name or large behind the title."""
    stroke, dot, ball, edge = ('2.6', '2.9', '8', '.6') if hero else ('4', '3.6', '9', '1')
    glow = '<circle cx="32" cy="32" r="26" fill="url(#cover-glow)" opacity=".28"/>' if hero else ''
    return (f'<svg class="{css}" viewBox="0 0 64 64" aria-hidden="true">{glow}<g transform="translate(32 32) rotate(-28)">'
            f'<path d="M23.84 -5.16A27 11 0 1 0 23.84 5.16" fill="none" stroke="url(#cover-chrome)" stroke-width="{stroke}" '
            f'stroke-linecap="round"/><circle cx="23.84" cy="5.16" r="{dot}" fill="#1d7a4c"/></g>'
            f'<circle cx="32" cy="32" r="{ball}" fill="url(#cover-ball)" stroke="#3a4454" stroke-width="{edge}"/></svg>')


def cover_front(language, book_title):
    """What the front of the cover shows: the mark, the title on two lines, the
    version of Cromo it is for and its month."""
    words = LANGUAGES[language]
    today = datetime.date.today()
    month = words['months'].split()[today.month - 1]
    first, _, last = book_title.rpartition(' ')
    return f'''{cover_mark('cover-hero', True)}
  <div class="cover-brand">{cover_mark('cover-logo')}<span>cromo</span></div>
  <div class="cover-title">
    <h1><span>{html.escape(first)}</span><span>{html.escape(last)}</span></h1>
    <p>{html.escape(words['subtitle'])}</p>
  </div>
  <div class="cover-bottom"><div><b>Cromo {version()}</b>{month} {today.year}</div><div>cromolang.org</div></div>'''


PAGE = (165, 240)  # the page of the book in mm, as in book.css
BLEED = 3          # what a printer cuts away around every page


def cover_page(language, book_title, spine, barcode):
    """The cover for a printer, two sheets with 3 mm of bleed: the back, the spine
    and the front, then the inside of the cover, which stays white."""
    words = LANGUAGES[language]
    width = 2 * PAGE[0] + spine + 2 * BLEED
    inside = ''.join(f'<div>{html.escape(item)}</div>' for item in words['back_inside'])
    text = ''.join(f'<p>{html.escape(paragraph)}</p>' for paragraph in words['back_text'])
    code = (f'<span class="c">// {html.escape(words["back_comment"])}</span>\n'
            '<span class="k">i32</span> main() {\n'
            '    <span class="k">list</span>&lt;<span class="k">string</span>&gt; names = [<span class="s">"Ada"</span>, '
            '<span class="s">"Alan"</span>]\n'
            '    <span class="k">for</span> (<span class="k">string</span> name <span class="k">in</span> names) {\n'
            '        println(<span class="s">"Hello, {}"</span>, name)\n    }\n    <span class="k">ret</span> 0\n}')
    return f'''<!doctype html>
<html lang="{language}">
<head>
<meta charset="utf-8">
<title>{html.escape(book_title)}</title>
{PRINT_FONTS}
<link rel="stylesheet" href="../book.css">
<style>
@page {{ size: {width:.1f}mm {PAGE[1] + 2 * BLEED}mm; margin: 0; }}
body.wrap .sheet {{ --spine: {spine:.1f}mm; }}
</style>
</head>
<body class="wrap">
{COVER_DEFS}
<div class="sheet">
  <div class="panel back">
    <h2>{html.escape(words['back_title'])}</h2>
    {text}
    <div class="inside">{inside}</div>
    <pre>{code}</pre>
    <div class="bottom">
      <div class="who"><b>cromo</b>cromolang.org<br>{REPOSITORY.replace('https://', '')}</div>
      {'<div class="barcode"></div>' if barcode else ''}
    </div>
  </div>
  <div class="panel spine">{cover_mark('spine-logo')}<div class="name">{html.escape(book_title)}</div><div class="edition">{version()}</div></div>
  <div class="panel front">
  {cover_front(language, book_title)}
  </div>
</div>
<div class="sheet inner"></div>
</body>
</html>
'''


def print_page(language, book_title, parts, pages, checked, press=False):
    """The whole guide in one page, printed as a book. `pages` gives the page of each
    anchor, from a first print; without it the contents leave the numbers empty, in a
    column of the same width, so that the pages do not move. For a printer, `press`,
    every page has 3 mm of bleed around it and the first one is a title page, as the
    cover is a sheet of its own."""
    words = LANGUAGES[language]
    today = datetime.date.today()
    month = words['months'].split()[today.month - 1]
    edition = words['version'].format(version(), f'{month} {today.year}')

    def entry(css, number, title, anchor):
        page = pages.get(anchor, '')
        return (f'<li class="{css}"><a href="#{anchor}"><span class="n">{html.escape(number)}</span>'
                f'<span class="t">{html.escape(title)}</span><span class="dots"></span><span class="p">{page}</span></a>')
    toc = []
    for i, part in enumerate(parts):
        toc.append(entry('toc-chapter' + (' toc-appendix' if part['number'].isalpha() else ''), part['number'],
                         part['title'], f'chapter-{i}'))
        if part['sections']:
            toc.append('<ol>' + ''.join(entry('toc-section', n, t, a) + '</li>' for n, a, t in part['sections']) + '</ol>')
        toc.append('</li>')
    css = []
    content = []
    for i, part in enumerate(parts):
        # The chapter at the foot of its pages: Chrome has no first page of a named
        # page, so a running head would stand above the title of the chapter.
        running = json.dumps(part['running'], ensure_ascii=False)
        css.append(f'.chapter-{i} {{ page: c{i}; }}\n@page c{i}:right {{ @bottom-left {{ content: {running}; }} }}\n'
                   f'@page c{i}:left {{ @bottom-right {{ content: {running}; }} }}')
        content.append(f'<section class="chapter chapter-{i}" id="chapter-{i}">{part["body"]}</section>')
    contents_title = words.get('print_contents', words['contents'])
    checked_line = f'<p>{html.escape(words["checked"].format(version()))}</p>' if checked else ''
    first = 'cover'
    if press:
        # The same pages, each larger by the bleed on its four sides.
        first = 'title-page'
        b = BLEED
        css.append(f'@page {{ size: {PAGE[0] + 2 * b}mm {PAGE[1] + 2 * b}mm; margin: {18 + b}mm {13 + b}mm {20 + b}mm {19 + b}mm; }}\n'
                   f'@page :right {{ margin-left: {19 + b}mm; margin-right: {13 + b}mm; }}\n'
                   f'@page :left {{ margin-left: {13 + b}mm; margin-right: {19 + b}mm; }}')
    return f'''<!doctype html>
<html lang="{language}">
<head>
<meta charset="utf-8">
<title>{html.escape(book_title)}</title>
{PRINT_FONTS}
<link rel="stylesheet" href="../book.css">
<style>
{chr(10).join(css)}
</style>
</head>
<body class="print">
<section class="{first}">
  {COVER_DEFS}{cover_front(language, book_title)}
</section>
<section class="colophon">
  <p><strong>{html.escape(book_title)}</strong><br>{html.escape(edition)}</p>
  <p>cromolang.org<br>{REPOSITORY.replace('https://', '')}</p>
  <p>{html.escape(words['license'])}</p>
  {checked_line}
</section>
<section class="contents">
  <h1>{html.escape(contents_title)}</h1>
  <ol class="toc-print">{''.join(toc)}</ol>
</section>
{''.join(content)}
</body>
</html>
'''


PRINT = {}  # for each language: the title of the book and its chapters, for the PDF


def build(out, checked=False, cromo=None):
    target = os.path.join(out, 'book')
    os.makedirs(target, exist_ok=True)
    shutil.copy(os.path.join(BOOK, 'book.css'), target)
    shutil.copy(os.path.join(BOOK, 'book.js'), target)
    images = os.path.join(BOOK, 'images')
    if os.path.isdir(images):
        shutil.copytree(images, os.path.join(target, 'images'), dirs_exist_ok=True)
    reference = library_reference(cromo)
    for language in LANGUAGES:
        book_title, chapters = read_summary(language)
        guide = len(chapters)  # the chapters, then the pages of the library (D519)
        words = LANGUAGES[language]
        pages = {}
        if reference:
            LIBRARY[language] = guide
            for module in reference:
                title = words['core'] if module['name'] == 'std' else module['name']
                source = library_page_name(module['name'])[:-5] + '.md'
                chapters.append((title, source))
                pages[source] = library_markdown(module, words)
        folder = os.path.join(target, language)
        if os.path.isdir(folder):
            shutil.rmtree(folder)
        os.makedirs(folder)
        links = chapter_links(chapters)
        rendered, search = [], []
        for i, (title, source) in enumerate(chapters):
            text = pages[source] if i >= guide else open(os.path.join(BOOK, language, source), encoding='utf-8').read()
            headings = []
            body = render(parse(text), links, headings)
            rendered.append((body, headings))
            plain = re.sub(r'<[^>]+>', ' ', body)
            for level, anchor, heading in headings:
                search.append({'title': heading, 'chapter': title, 'url': page_name(i, source) + ('#' + anchor if level > 1 else '')})
            search.append({'title': title, 'chapter': title, 'url': page_name(i, source), 'text': html.unescape(' '.join(plain.split()))[:4000]})
        for i, (title, source) in enumerate(chapters):
            body, headings = rendered[i]
            previous = (page_name(i - 1, chapters[i - 1][1]), chapters[i - 1][0]) if i > 0 else None
            following = (page_name(i + 1, chapters[i + 1][1]), chapters[i + 1][0]) if i + 1 < len(chapters) else None
            with open(os.path.join(folder, page_name(i, source)), 'w', encoding='utf-8', newline='\n') as f:
                f.write(page(language, title, book_title, chapters, i, body, headings, previous, following))
        # The PDF has the chapters of the guide, without the pages of the library.
        parts = [print_chapter(language, chapters, i, body, headings) for i, (body, headings) in enumerate(rendered[:guide])]
        PRINT[language] = (book_title, parts, checked)
        with open(os.path.join(folder, 'print.html'), 'w', encoding='utf-8', newline='\n') as f:
            f.write(print_page(language, book_title, parts, {}, checked))
        with open(os.path.join(folder, 'search.json'), 'w', encoding='utf-8', newline='\n') as f:
            json.dump(search, f, ensure_ascii=False, separators=(',', ':'))
    with open(os.path.join(target, 'index.html'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('''<!doctype html>
<html lang="en"><head><meta charset="utf-8"><title>The Cromo Guide</title>
<meta http-equiv="refresh" content="0; url=en/">
<script>var l=(navigator.language||'').toLowerCase();location.replace(l.indexOf('it')===0?'it/':'en/');</script>
</head><body><a href="en/">The Cromo Guide</a> · <a href="it/">La guida di Cromo</a></body></html>
''')
    print(f'book: {target}')


# ---- The check of the examples ----

def check_chapter(cromo, language, source):
    """The examples of one chapter, in a folder of its own: how many, how many failed, and what to say."""
    total = 0
    failures = 0
    said = []
    blocks = parse(open(os.path.join(BOOK, language, source), encoding='utf-8').read())
    work = tempfile.mkdtemp(prefix=f'book-{language}-')
    if os.path.isdir(os.path.join(BOOK, 'images')):  # for embed()
        shutil.copytree(os.path.join(BOOK, 'images'), os.path.join(work, 'images'))
    if os.path.isdir(os.path.join(BOOK, 'sounds')):  # the sounds that the examples embed
        shutil.copytree(os.path.join(BOOK, 'sounds'), os.path.join(work, 'sounds'))
    number = 0
    for i, b in enumerate(blocks):
        if b.kind != 'code':
            continue
        name = b.flags.get('file')
        if isinstance(name, str):
            os.makedirs(os.path.dirname(os.path.join(work, name)), exist_ok=True)
        if isinstance(name, str) and b.language != 'cromo':
            open(os.path.join(work, name), 'w', encoding='utf-8', newline='\n').write(b.text + '\n')
            continue
        if b.language != 'cromo' or b.flags.get('ignore'):
            continue
        text = program(b.text)
        is_program = re.search(r'^(i32|void)\s+main\s*\(', text, re.M)
        if isinstance(name, str):
            open(os.path.join(work, name), 'w', encoding='utf-8', newline='\n').write(text + '\n')
        if not is_program:
            continue
        number += 1
        total += 1
        file = name if isinstance(name, str) else f'example{number}.cro'
        path = os.path.join(work, file)
        open(path, 'w', encoding='utf-8', newline='\n').write(text + '\n')
        expected = None
        following = blocks[i + 1] if i + 1 < len(blocks) else None
        if following is not None and following.kind == 'code' and following.language == 'output':
            expected = following.text
        where = f'{language}/{source} example {number}'
        exe = os.path.join(work, f'example{number}.exe')
        build = subprocess.run([cromo, 'build', file, '-o', exe], capture_output=True, text=True, encoding='utf-8', cwd=work)
        if b.flags.get('error'):
            if build.returncode == 0:
                said.append(f'FAIL  {where}: it compiles, but it should not')
                failures += 1
            elif expected and any(line.strip() and line.strip() not in build.stdout + build.stderr for line in expected.split('\n')):
                said.append(f'FAIL  {where}: other errors\n{build.stdout}{build.stderr}')
                failures += 1
            continue
        if build.returncode != 0:
            said.append(f'FAIL  {where}: it does not compile\n{build.stdout}{build.stderr}')
            failures += 1
            continue
        if b.flags.get('norun'):
            continue
        args = b.flags.get('args')
        args = args.split(',') if isinstance(args, str) else []
        try:
            run = subprocess.run([exe] + args, capture_output=True, text=True, encoding='utf-8', cwd=work, timeout=60)
        except subprocess.TimeoutExpired:
            said.append(f'FAIL  {where}: still running after 60 s')
            failures += 1
            continue
        if expected is not None and not b.flags.get('nocheck'):
            got = (run.stdout + run.stderr).replace('\r\n', '\n').rstrip()
            if got != expected.rstrip():
                said.append(f'FAIL  {where}: it prints\n{got}\n      instead of\n{expected.rstrip()}')
                failures += 1
    shutil.rmtree(work, ignore_errors=True)
    return total, failures, said


def check(cromo, only=None, languages=None):
    cromo = os.path.abspath(cromo)
    failures = 0
    total = 0
    chapters_to_check = []
    for language in languages or LANGUAGES:
        _, chapters = read_summary(language)
        for _, source in chapters:
            if only and source not in only:
                continue
            if not os.path.exists(os.path.join(BOOK, language, source)):
                print(f'MISSING  {language}/{source}')
                failures += 1
                continue
            chapters_to_check.append((language, source))
    # The chapters at the same time, each in its folder: cromo and the examples are
    # other processes, so threads are enough. Their messages come in the order of the book.
    import concurrent.futures
    workers = max(1, min(8, (os.cpu_count() or 2) // 2))
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        results = list(pool.map(lambda chapter: check_chapter(cromo, chapter[0], chapter[1]), chapters_to_check))
    for count, failed, said in results:
        total += count
        failures += failed
        for line in said:
            print(line)
    print(f'{total - failures} examples passed, {failures} failed')
    return failures == 0


# ---- The PDF ----

def browser():
    candidates = [
        os.path.expandvars(r'%ProgramFiles(x86)%\Microsoft\Edge\Application\msedge.exe'),
        os.path.expandvars(r'%ProgramFiles%\Microsoft\Edge\Application\msedge.exe'),
        os.path.expandvars(r'%ProgramFiles%\Google\Chrome\Application\chrome.exe'),
        '/usr/bin/google-chrome', '/usr/bin/chromium', '/usr/bin/microsoft-edge',
    ]
    for c in candidates:
        if os.path.exists(c):
            return c
    sys.exit('no Edge or Chrome found for the PDF')


def print_pdf(exe, profile, source, target):
    if os.path.exists(target):
        os.remove(target)
    url = 'file:///' + source.replace('\\', '/').lstrip('/')
    subprocess.run([exe, '--headless=new', '--disable-gpu', f'--user-data-dir={profile}', '--no-pdf-header-footer',
                    '--generate-pdf-document-outline', '--virtual-time-budget=20000', '--run-all-compositor-stages-before-draw',
                    f'--print-to-pdf={target}', url], check=True, capture_output=True)
    # The browser can return before the file is whole, when another one of it runs.
    size, waited = -1, 0
    while waited < 300:
        now = os.path.getsize(target) if os.path.exists(target) else -1
        if now > 0 and now == size:
            return
        size = now
        time.sleep(1)
        waited += 1
    sys.exit(f'the browser did not print {target}')


def destinations(path):
    """The page, from 1, of each anchor of a PDF of Chrome: the catalog names the
    destinations of the links, each a page object, and the tree of the pages gives
    their order. Also the number of pages."""
    data = open(path, 'rb').read()

    def obj(number):
        m = re.search(rb'(?:^|\n)%d 0 obj\s*(.*?)\s*endobj' % number, data, re.S)
        return m.group(1) if m else b''
    catalog = obj(int(re.findall(rb'/Root (\d+) 0 R', data)[-1]))
    order = []

    def walk(number):
        node = obj(number)
        kids = re.search(rb'/Kids\s*\[([^\]]*)\]', node)
        if kids:
            for kid in re.findall(rb'(\d+) 0 R', kids.group(1)):
                walk(int(kid))
        else:
            order.append(number)
    walk(int(re.search(rb'/Pages (\d+) 0 R', catalog).group(1)))
    page = {number: i + 1 for i, number in enumerate(order)}
    dests = re.search(rb'/Dests (\d+) 0 R', catalog)
    found = {}
    for name, target in re.findall(rb'/([^\s\[/<>()]+)\s*\[(\d+) 0 R', obj(int(dests.group(1))) if dests else b''):
        # A name of PDF escapes its bytes with #, and Chrome escapes the id as in a URL.
        name = re.sub(rb'#([0-9A-Fa-f]{2})', lambda m: bytes([int(m.group(1), 16)]), name).decode('utf-8', 'replace')
        found[urllib.parse.unquote(name)] = page.get(int(target), '')
    return found, len(order)


def print_book(exe, profile, language, source, target, pages, press=False):
    """Prints the guide until its contents give the pages where the chapters are:
    the numbers come from the print before, and a number can move a line."""
    book_title, parts, checked = PRINT[language]
    for _ in range(5):
        with open(source, 'w', encoding='utf-8', newline='\n') as f:
            f.write(print_page(language, book_title, parts, pages, checked, press))
        print_pdf(exe, profile, source, target)
        found, count = destinations(target)
        if found == pages:
            return found, count
        pages = found
    print(f'warning: the pages of {target} still move after five prints')
    return found, count


def press_pdf(exe, profile, out, language, pages, paper, spine, barcode):
    """The file for a printer, in the order of its template: the cover open, back,
    spine and front, the inside of the cover, then the pages one by one, each with
    3 mm of bleed. The pages are filled with white ones up to a multiple of four."""
    try:
        from pypdf import PdfReader, PdfWriter
    except ImportError:
        sys.exit('the file for a printer needs pypdf: pip install pypdf')
    words = LANGUAGES[language]
    book_title = PRINT[language][0]
    folder = os.path.abspath(os.path.join(out, 'book', language))
    inside = os.path.join(folder, 'press-pages.pdf')
    _, count = print_book(exe, profile, language, os.path.join(folder, 'press.html'), inside, pages, True)
    total = count + (-count) % 4
    if spine is None:
        spine = round(total * paper, 1)
    cover = os.path.join(folder, 'cover.html')
    with open(cover, 'w', encoding='utf-8', newline='\n') as f:
        f.write(cover_page(language, book_title, spine, barcode))
    cover_target = os.path.join(folder, 'press-cover.pdf')
    print_pdf(exe, profile, cover, cover_target)
    writer = PdfWriter()
    points = 72 / 25.4
    height = PAGE[1] + 2 * BLEED

    def add(sheet, width):
        # The browser rounds a sheet to its pixels: the box gets the exact measure,
        # from the top left corner, where the page starts.
        top = float(sheet.mediabox.top)
        for box in (sheet.mediabox, sheet.cropbox, sheet.trimbox, sheet.bleedbox, sheet.artbox):
            box.lower_left = (0, top - height * points)
            box.upper_right = (width * points, top)
        writer.add_page(sheet)
    sheets = PdfReader(cover_target).pages
    if len(sheets) != 2:
        sys.exit(f'the cover of {language} has {len(sheets)} sheets instead of 2')
    for sheet in sheets:
        add(sheet, 2 * PAGE[0] + spine + 2 * BLEED)
    for sheet in PdfReader(inside).pages:
        add(sheet, PAGE[0] + 2 * BLEED)
    for _ in range(total - count):
        writer.add_blank_page((PAGE[0] + 2 * BLEED) * points, (PAGE[1] + 2 * BLEED) * points)
    target = os.path.abspath(os.path.join(out, 'book', words['press_pdf']))
    with open(target, 'wb') as f:
        writer.write(f)
    os.remove(inside)
    os.remove(cover_target)
    print(f'press: {target} (the cover with a spine of {spine:.1f} mm, then {total} pages, {total - count} of them white at the end)')


def pdf(out, paper=0.057, barcode=False, press=False, spine=None):
    exe = browser()
    profile = tempfile.mkdtemp(prefix='book-pdf-')
    for language, words in LANGUAGES.items():
        source = os.path.abspath(os.path.join(out, 'book', language, 'print.html'))
        target = os.path.abspath(os.path.join(out, 'book', words['pdf']))
        again, count = print_book(exe, profile, language, source, target, {})
        print(f'pdf: {target} ({count} pages, {os.path.getsize(target) // 1024} KB)')
        if press:
            press_pdf(exe, profile, out, language, again, paper, spine, barcode)
    shutil.rmtree(profile, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser(description='Builds the Cromo Guide.')
    parser.add_argument('--out', default=os.path.join(ROOT, 'dist', 'site'))
    parser.add_argument('--check', action='store_true', help='compile and run the examples')
    parser.add_argument('--only', nargs='+', help='with --check, only these chapters, as modules.md; without building')
    parser.add_argument('--language', nargs='+', choices=list(LANGUAGES), help='with --check, only these languages')
    parser.add_argument('--pdf', action='store_true', help='print the guide to PDF')
    parser.add_argument('--paper', type=float, default=0.057,
                        help='how thick a page is in mm, for the spine of the cover (default: 0.057)')
    parser.add_argument('--spine', type=float, help='the width of the spine in mm, as the printer gives it')
    parser.add_argument('--barcode', action='store_true', help='a white box for the barcode on the back of the cover')
    parser.add_argument('--press', action='store_true', help='accepted for the old command lines: --pdf always makes the file for a printer')
    parser.add_argument('--cromo', default=os.path.join(ROOT, 'build', 'cromo.exe' if os.name == 'nt' else 'cromo'))
    options = parser.parse_args()
    if options.only:
        sys.exit(0 if check(options.cromo, options.only, options.language) else 1)
    if options.check and not check(options.cromo, None, options.language):
        sys.exit(1)
    build(options.out, options.check, options.cromo)
    if options.pdf:
        pdf(options.out, options.paper, options.barcode, True, options.spine)


if __name__ == '__main__':
    main()
