"""The Cromo Guide: from the Markdown of book/<language>/ to the pages of the site.

    python tools/book.py [--out dist/site] [--check] [--pdf] [--cromo build/cromo.exe]

Each language is a folder of book/ with SUMMARY.md, the list of its chapters:

    # The Cromo Guide
    - [Introduction](introduction.md)
    - [Getting started](getting-started.md)

The chapters are written in a subset of Markdown: headings, paragraphs, lists,
tables, block quotes, code blocks, and inline code, bold, italic and links. The
pages go to <out>/book/<language>/, with print.html, the whole guide in one page
for the PDF, and search.json for the search box.

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
book: a cover, the contents with the pages, and a chapter on each new page. It
prints twice: the pages of the chapters and of their sections come from the first
PDF, from the destinations of its links.
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
import urllib.parse

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BOOK = os.path.join(ROOT, 'book')
REPOSITORY = 'https://github.com/cromolang/cromo'

LANGUAGES = {
    'en': {
        'name': 'English', 'pdf': 'cromo-guide-en.pdf',
        'previous': 'Previous', 'next': 'Next', 'search': 'Search the guide',
        'pdf_link': 'PDF', 'contents': 'Contents', 'theme': 'Light or dark theme',
        'site': 'cromolang.org', 'source': 'Source', 'no_results': 'No results',
        'menu': 'Chapters', 'chapter': 'Chapter', 'appendix': 'Appendix',
        'subtitle': 'The Cromo programming language',
        'version': 'Version {} · {}',
        'months': 'January February March April May June July August September October November December',
        'license': 'The text and the examples are part of the sources of Cromo, under the MIT or the Apache 2.0 license.',
        'checked': 'Every example of this guide was compiled and run with cromo {}, and its output checked, when the guide was made.',
    },
    'it': {
        'name': 'Italiano', 'pdf': 'cromo-guida-it.pdf',
        'previous': 'Precedente', 'next': 'Successivo', 'search': 'Cerca nella guida',
        'pdf_link': 'PDF', 'contents': 'Indice', 'theme': 'Tema chiaro o scuro',
        'site': 'cromolang.org', 'source': 'Sorgenti', 'no_results': 'Nessun risultato',
        'menu': 'Capitoli', 'chapter': 'Capitolo', 'appendix': 'Appendice',
        'subtitle': 'Il linguaggio di programmazione Cromo',
        'version': 'Versione {} · {}',
        'months': 'gennaio febbraio marzo aprile maggio giugno luglio agosto settembre ottobre novembre dicembre',
        'license': 'Il testo e gli esempi fanno parte dei sorgenti di Cromo, con la licenza MIT o Apache 2.0.',
        'checked': 'Ogni esempio di questa guida è stato compilato ed eseguito con cromo {}, e il suo output verificato, '
                   'quando la guida è stata generata.',
        'print_contents': 'Sommario',
    },
}

KEYWORDS = set('''struct interface enum import extern unsafe const void if else for in break
continue ret switch case default try catch finally throw ref move public true false func'''.split())
TYPES = set('i8 i16 i32 i64 u8 u16 u32 u64 f32 f64 bool char string list map addr Self'.split())
BUILTINS = set('print println eprint eprintln format panic assert'.split())


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
        while i < len(lines) and lines[i].strip() and not re.match(r'^(```|#{1,4}\s|>|\||\s*([-*]|\d+\.)\s)', lines[i]):
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
                        parts.append(highlight(line) + '\n')
                code = ''.join(parts)
                code = code[:-1] if code.endswith('\n') else code
            elif b.language in ('sh', 'shell', 'powershell', 'console'):
                code = highlight_shell(b.text)
            else:
                code = html.escape(b.text, quote=False)
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


# ---- The pages ----

NUMBERED = {}  # for each language, how many chapters are numbered: the appendices follow a line ---


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


def sidebar(chapters, current, words, headings, numbered):
    items = []
    for i, (title, source) in enumerate(chapters):
        number = f'<span class="num">{i}.</span>' if 0 < i < numbered else ''
        if i == numbered:
            items.append('<li class="separator"></li>')
        active = ' class="active"' if i == current else ''
        items.append(f'<li{active}><a href="{page_name(i, source)}">{number}{html.escape(title)}</a>')
        if i == current and headings:
            sub = ''.join(f'<li><a href="#{a}">{html.escape(t)}</a></li>' for level, a, t in headings if level == 2)
            if sub:
                items.append(f'<ul class="sections">{sub}</ul>')
        items.append('</li>')
    return f'<nav class="toc" aria-label="{words["contents"]}"><ul>{"".join(items)}</ul></nav>'


LOGO = '''<svg width="26" height="26" viewBox="0 0 64 64" aria-hidden="true"><defs><linearGradient id="o" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#1b2230"/><stop offset=".38" stop-color="#5d697b"/><stop offset=".52" stop-color="#9ba6b5"/><stop offset=".72" stop-color="#465164"/><stop offset="1" stop-color="#171d29"/></linearGradient><radialGradient id="n" cx=".35" cy=".3" r=".75"><stop offset="0" stop-color="#fff"/><stop offset=".35" stop-color="#cfd6df"/><stop offset=".8" stop-color="#7c8797"/><stop offset="1" stop-color="#4b5565"/></radialGradient></defs><g transform="translate(32 32) rotate(-28)"><path class="orbit" d="M23.84 -5.16A27 11 0 1 0 23.84 5.16" fill="none" stroke="url(#o)" stroke-width="4" stroke-linecap="round"/><circle cx="23.84" cy="5.16" r="3.6" fill="#0ea5c6"/></g><circle cx="32" cy="32" r="9" fill="url(#n)" stroke="#3a4454" stroke-width="1"/></svg>'''

FONTS = ('<link rel="preconnect" href="https://fonts.googleapis.com"><link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>'
         '<link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600&family=JetBrains+Mono:wght@400;500'
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
    <a class="pdf" href="../{words['pdf']}" download>{words['pdf_link']}</a>
    {switch}
    <button class="theme" aria-label="{words['theme']}" title="{words['theme']}"></button>
  </div>
</header>
<div class="results" hidden></div>
<div class="layout">
<aside>{sidebar(chapters, index, words, headings, NUMBERED[language])}</aside>
<main>
<article>
{body}
</article>
{nav}
<footer><a href="../../">{words['site']}</a> · <a href="{REPOSITORY}">{words['source']}</a></footer>
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


def print_page(language, book_title, parts, pages, checked):
    """The whole guide in one page, printed as a book. `pages` gives the page of each
    anchor, from a first print; without it the contents leave the numbers empty, in a
    column of the same width, so that the pages do not move."""
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
        css.append(f'.chapter-{i} {{ page: c{i}; }}\n@page c{i} {{ @bottom-left {{ content: {running}; }} }}')
        content.append(f'<section class="chapter chapter-{i}" id="chapter-{i}">{part["body"]}</section>')
    contents_title = words.get('print_contents', words['contents'])
    mark = LOGO[LOGO.index('>') + 1:]
    checked_line = f'<p>{html.escape(words["checked"].format(version()))}</p>' if checked else ''
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
<section class="cover">
  <svg class="cover-orbit" viewBox="0 0 64 64" aria-hidden="true">{mark}
  <div class="cover-top"><svg class="cover-logo" viewBox="0 0 64 64" aria-hidden="true">{mark}<span>cromo</span></div>
  <div class="cover-title">
    <h1>{html.escape(book_title)}</h1>
    <p>{html.escape(words['subtitle'])}</p>
  </div>
  <div class="cover-bottom"><span>{html.escape(edition)}</span><span>cromolang.org</span></div>
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


def build(out, checked=False):
    target = os.path.join(out, 'book')
    os.makedirs(target, exist_ok=True)
    shutil.copy(os.path.join(BOOK, 'book.css'), target)
    shutil.copy(os.path.join(BOOK, 'book.js'), target)
    for language in LANGUAGES:
        book_title, chapters = read_summary(language)
        folder = os.path.join(target, language)
        if os.path.isdir(folder):
            shutil.rmtree(folder)
        os.makedirs(folder)
        links = chapter_links(chapters)
        rendered, search = [], []
        for i, (title, source) in enumerate(chapters):
            text = open(os.path.join(BOOK, language, source), encoding='utf-8').read()
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
        parts = [print_chapter(language, chapters, i, body, headings) for i, (body, headings) in enumerate(rendered)]
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

def check(cromo):
    cromo = os.path.abspath(cromo)
    failures = 0
    total = 0
    for language in LANGUAGES:
        _, chapters = read_summary(language)
        for _, source in chapters:
            if not os.path.exists(os.path.join(BOOK, language, source)):
                print(f'MISSING  {language}/{source}')
                failures += 1
                continue
            blocks = parse(open(os.path.join(BOOK, language, source), encoding='utf-8').read())
            work = tempfile.mkdtemp(prefix=f'book-{language}-')
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
                        print(f'FAIL  {where}: it compiles, but it should not')
                        failures += 1
                    elif expected and any(line.strip() and line.strip() not in build.stdout + build.stderr for line in expected.split('\n')):
                        print(f'FAIL  {where}: other errors\n{build.stdout}{build.stderr}')
                        failures += 1
                    continue
                if build.returncode != 0:
                    print(f'FAIL  {where}: it does not compile\n{build.stdout}{build.stderr}')
                    failures += 1
                    continue
                if b.flags.get('norun'):
                    continue
                args = b.flags.get('args')
                args = args.split(',') if isinstance(args, str) else []
                try:
                    run = subprocess.run([exe] + args, capture_output=True, text=True, encoding='utf-8', cwd=work, timeout=60)
                except subprocess.TimeoutExpired:
                    print(f'FAIL  {where}: still running after 60 s')
                    failures += 1
                    continue
                if expected is not None and not b.flags.get('nocheck'):
                    got = (run.stdout + run.stderr).replace('\r\n', '\n').rstrip()
                    if got != expected.rstrip():
                        print(f'FAIL  {where}: it prints\n{got}\n      instead of\n{expected.rstrip()}')
                        failures += 1
            shutil.rmtree(work, ignore_errors=True)
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
    url = 'file:///' + source.replace('\\', '/').lstrip('/')
    subprocess.run([exe, '--headless=new', '--disable-gpu', f'--user-data-dir={profile}', '--no-pdf-header-footer',
                    '--generate-pdf-document-outline', '--virtual-time-budget=20000', '--run-all-compositor-stages-before-draw',
                    f'--print-to-pdf={target}', url], check=True, capture_output=True)


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


def pdf(out):
    exe = browser()
    profile = tempfile.mkdtemp(prefix='book-pdf-')
    for language, words in LANGUAGES.items():
        book_title, parts, checked = PRINT[language]
        source = os.path.abspath(os.path.join(out, 'book', language, 'print.html'))
        target = os.path.abspath(os.path.join(out, 'book', words['pdf']))
        print_pdf(exe, profile, source, target)
        pages, _ = destinations(target)
        with open(source, 'w', encoding='utf-8', newline='\n') as f:
            f.write(print_page(language, book_title, parts, pages, checked))
        print_pdf(exe, profile, source, target)
        again, count = destinations(target)
        moved = [name for name in pages if again.get(name) != pages[name]]
        if moved:
            print(f'warning: {len(moved)} anchors moved between the two prints, as {moved[0]}')
        print(f'pdf: {target} ({count} pages, {os.path.getsize(target) // 1024} KB)')
    shutil.rmtree(profile, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser(description='Builds the Cromo Guide.')
    parser.add_argument('--out', default=os.path.join(ROOT, 'dist', 'site'))
    parser.add_argument('--check', action='store_true', help='compile and run the examples')
    parser.add_argument('--pdf', action='store_true', help='print the guide to PDF')
    parser.add_argument('--cromo', default=os.path.join(ROOT, 'build', 'cromo.exe' if os.name == 'nt' else 'cromo'))
    options = parser.parse_args()
    if options.check and not check(options.cromo):
        sys.exit(1)
    build(options.out, options.check)
    if options.pdf:
        pdf(options.out)


if __name__ == '__main__':
    main()
