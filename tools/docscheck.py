#!/usr/bin/env python3
# =====================================================================
#  SynthMiner  --  the player's manual holds together (docs/)
# ---------------------------------------------------------------------
#  docs/ is the PLAYER's documentation, served as GitHub Pages. It is
#  not built here -- Jekyll does that on GitHub -- so nothing else would
#  ever notice a link that points at a page which does not exist. A
#  manual with a 404 in it is worse than a missing page: the reader
#  cannot tell whether the answer is absent or the link is.
#
#  So this measures the OUTCOME and not a proxy for it (F-130, and the
#  whole family before it): it opens every page, resolves every internal
#  link against the set of files that actually exist, and checks the
#  sidebar against the same set in both directions.
#
#  What it checks:
#
#    * every internal link resolves to a page that exists;
#    * every page is listed in the sidebar (_data/nav.yml) -- an
#      unreachable page is a page nobody will read;
#    * every sidebar entry points at a page that exists;
#    * every page has the front matter the layout reads (`title`, and
#      `summary` on everything but the index, which has its own lede).
#
#  What it deliberately does NOT check: prose, external links (the
#  network is not a build dependency), or spelling.
# =====================================================================

import os
import re
import sys
from glob import glob

DOCS = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'docs')

# A markdown link target, and an href in the raw HTML the pages use for
# card grids and boxed asides.
LINK = re.compile(r'\]\(([^)"\s]+)')
HREF = re.compile(r'href="([^"]+)"')
# The url of one sidebar row: "- {url: /plants/wheat.html, name: Wheat}".
NAV_URL = re.compile(r'\{url:\s*(/[^,]+?),')


def pages():
    out = {}
    for f in sorted(glob(os.path.join(DOCS, '**', '*.md'), recursive=True)):
        rel = os.path.relpath(f, DOCS)
        out[rel[:-3] + '.html'] = f
    return out


def front_matter(text):
    if not text.startswith('---\n'):
        return None
    end = text.find('\n---\n', 3)
    return text[4:end] if end > 0 else None


def main():
    if not os.path.isdir(DOCS):
        print('docscheck: no docs/ directory', file=sys.stderr)
        return 1

    known = pages()
    problems = []

    for html, path in sorted(known.items()):
        rel = os.path.relpath(path, DOCS)
        here = os.path.dirname(rel)
        text = open(path, encoding='utf-8').read()

        fm = front_matter(text)
        if fm is None:
            problems.append(f'{rel}: no front matter, so the layout has no title')
        else:
            if 'title:' not in fm:
                problems.append(f'{rel}: front matter has no title')
            if rel != 'index.md' and 'summary:' not in fm:
                problems.append(f'{rel}: front matter has no summary (the page lede)')

        # LINKS ARE RESOLVED RELATIVE TO THE PAGE, not to the site root:
        # the manual uses relative links on purpose, so it works whatever
        # baseurl the repository is served under.
        for m in list(LINK.finditer(text)) + list(HREF.finditer(text)):
            target = m.group(1)
            if target.startswith(('http://', 'https://', 'mailto:', '#', '{{', '{%')):
                continue
            target = target.split('#')[0]
            if not target:
                continue
            resolved = os.path.normpath(os.path.join(here, target))
            if resolved not in known:
                line = text[:m.start()].count('\n') + 1
                problems.append(f'{rel}:{line}: link to "{target}" -> no such page ({resolved})')

    nav_path = os.path.join(DOCS, '_data', 'nav.yml')
    if not os.path.isfile(nav_path):
        problems.append('_data/nav.yml is missing, so there is no sidebar')
    else:
        nav = open(nav_path, encoding='utf-8').read()
        listed = {u.lstrip('/') for u in NAV_URL.findall(nav)}
        for orphan in sorted(set(known) - listed):
            problems.append(f'{orphan} is not in the sidebar: nothing links to it from the nav')
        for ghost in sorted(listed - set(known)):
            problems.append(f'the sidebar lists {ghost}, which does not exist')

    if problems:
        print('docscheck: FAILED', file=sys.stderr)
        for p in problems:
            print('  ' + p, file=sys.stderr)
        return 1

    print(f'docscheck: {len(known)} pages, every link resolves, sidebar matches')
    return 0


if __name__ == '__main__':
    sys.exit(main())
