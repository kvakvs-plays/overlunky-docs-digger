"""Checks a built HTML reference (plan DIG-7), whatever generator made it (standard library only).

python tools/check_site.py <site dir> [--base /overlunky] [--samples tools/site_samples.json]
                            [--old-index <overlunky>/docs/index.html] [--compare <other dir>]

- links: every internal link points at an existing page, and its #fragment at an element id on that page;
- old anchors: for each sample the old single page had a heading for, <site>/old-anchors.json sends that anchor to a
  page and element that name the symbol;
- --old-index: every heading anchor (h1 to h3) of the old single page is in old-anchors.json and lands on a built page
  (the h4 signature anchors spell the old signature text and are not mapped; tools/parity.py reports both);
- --compare: the two builds are byte for byte the same (building twice from one JSON gives the same pages).
Exits 1 when a check fails.
"""
import argparse
import filecmp
import html.parser
import json
import os
import re
import sys
import urllib.parse


class PageParser(html.parser.HTMLParser):
    """Collects the ids, the links and the text of each element with an id."""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.ids = set()
        self.links = []
        self.title = ''
        self.text_of = {}
        self._open = []  # ids of the elements being read, with their tag
        self._in_title = False

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if 'id' in attrs:
            self.ids.add(attrs['id'])
            self.text_of[attrs['id']] = ''
            self._open.append((tag, attrs['id']))
        if tag == 'a' and attrs.get('href'):
            self.links.append(attrs['href'])
        self._in_title = tag == 'title'

    def handle_endtag(self, tag):
        if self._open and self._open[-1][0] == tag:
            self._open.pop()
        self._in_title = False

    def handle_data(self, data):
        for _, element_id in self._open:
            self.text_of[element_id] += data
        if self._in_title:
            self.title += data


def read_pages(site):
    """Every HTML file of the site, parsed, by its site path (`/types/Entity/`, `/404.html`)."""
    pages = {}
    for folder, _, files in os.walk(site):
        for name in files:
            if name.endswith('.html'):
                file = os.path.join(folder, name)
                rel = os.path.relpath(file, site).replace(os.sep, '/')
                path = '/' + (rel[:-len('index.html')] if rel.endswith('index.html') else rel)
                parser = PageParser()
                with open(file, encoding='utf-8') as f:
                    parser.feed(f.read())
                pages[path] = parser
    return pages


def resolve(page_path, href, base):
    """(site path, fragment) of an internal link, or None for external ones."""
    parts = urllib.parse.urlsplit(href)
    if parts.scheme or parts.netloc or href.startswith(('mailto:', 'javascript:')):
        return None
    path = parts.path
    if not path:
        path = page_path
    elif not path.startswith('/'):
        path = urllib.parse.urljoin(page_path, path)
    elif base and (path == base or path.startswith(base + '/')):
        path = path[len(base):] or '/'
    return path, urllib.parse.unquote(parts.fragment)


def exists(site, path):
    """True when a site path is a file of the build (a page or an asset)."""
    file = os.path.join(site, path.lstrip('/').replace('/', os.sep))
    return os.path.isfile(file) or os.path.isfile(os.path.join(file, 'index.html'))


def check_links(site, pages, base):
    """Broken internal links: (page, href, why)."""
    broken = []
    for page_path, page in sorted(pages.items()):
        for href in page.links:
            target = resolve(page_path, href, base)
            if target is None:
                continue
            path, fragment = target
            if not exists(site, path):
                broken.append((page_path, href, 'no such page'))
            elif fragment and fragment != '_top' and path in pages and fragment not in pages[path].ids:
                broken.append((page_path, href, f'no id {fragment!r} on {path}'))
    return broken


def slate_id(text):
    """The id the old Slate page gave a heading."""
    return re.sub(r'^-|-$', '', re.sub(r'[^A-Za-z0-9_]+', '-', text))


def check_old_anchors(site, pages, samples):
    """Samples whose old anchor does not land on their symbol: (anchor, why)."""
    with open(os.path.join(site, 'old-anchors.json'), encoding='utf-8') as f:
        anchors = json.load(f)
    wrong = []
    for sample in samples:
        if sample['kind'] in ('member', 'enum_value'):
            continue  # the old page had no heading for these
        name = f'{sample["table"]}.{sample["name"]}' if sample['kind'] == 'event' else sample['name']
        anchor = slate_id(name)
        target = anchors.get(anchor)
        if not target:
            wrong.append((anchor, 'not in old-anchors.json'))
            continue
        path, _, fragment = target.partition('#')
        page = pages.get(path)
        if page is None:
            wrong.append((anchor, f'{target}: no such page'))
        elif fragment and sample['name'] not in page.text_of.get(fragment, ''):
            wrong.append((anchor, f'{target}: element {fragment!r} does not name {sample["name"]}'))
        elif not fragment and sample['name'] not in page.title:
            wrong.append((anchor, f'{target}: page title {page.title!r} does not name {sample["name"]}'))
    return wrong


def old_page_anchors(index_html):
    """Ids of the old single page's headings by level ('1' to '4'), in page order."""
    with open(index_html, encoding='utf-8') as f:
        text = f.read()
    anchors = {}
    for level, anchor in re.findall(r"<h([1-4]) id='([^']*)'", text):
        anchors.setdefault(level, []).append(anchor)
    return anchors


def unmapped_old_anchors(site, index_html):
    """(h1-h3 anchors of the old page, h4 anchors, [(anchor, why)] for the headings that land on no built page)."""
    with open(os.path.join(site, 'old-anchors.json'), encoding='utf-8') as f:
        mapped = json.load(f)
    anchors = old_page_anchors(index_html)
    headings = [a for level in '123' for a in anchors.get(level, [])]
    unmapped = []
    for anchor in headings:
        target = mapped.get(anchor)
        if not target:
            unmapped.append((anchor, 'maps to no page'))
        elif not exists(site, target.split('#')[0]):
            unmapped.append((anchor, f'maps to {target}, which is not built'))
    return headings, anchors.get('4', []), unmapped


def compare(site, other):
    """Files that differ between two builds, or exist in one only."""
    differences = []
    pending = [('',)]
    while pending:
        rel = os.path.join(*pending.pop())
        cmp = filecmp.dircmp(os.path.join(site, rel), os.path.join(other, rel))
        differences += [os.path.join(rel, n) for n in cmp.left_only + cmp.right_only + cmp.funny_files]
        _, mismatch, errors = filecmp.cmpfiles(cmp.left, cmp.right, cmp.common_files, shallow=False)
        differences += [os.path.join(rel, n) for n in mismatch + errors]
        pending += [(rel, d) for d in cmp.common_dirs]
    return sorted(differences)


def main():
    """Runs the checks the arguments ask for and prints what failed."""
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('site')
    parser.add_argument('--base', default='', help='path the site is served under (`/overlunky`)')
    parser.add_argument('--samples', help='tools/site_samples.json')
    parser.add_argument('--old-index', help="overlunky's old docs/index.html")
    parser.add_argument('--compare', help='a second build of the same JSON')
    args = parser.parse_args()
    base = args.base.rstrip('/')
    pages = read_pages(args.site)
    failed = False

    broken = check_links(args.site, pages, base)
    links = sum(len(p.links) for p in pages.values())
    print(f'links: {len(pages)} pages, {links} links, {len(broken)} broken')
    for page, href, why in broken[:50]:
        print(f'  {page}: {href} ({why})')
    failed |= bool(broken)

    if args.samples:
        with open(args.samples, encoding='utf-8') as f:
            samples = json.load(f)['samples']
        wrong = check_old_anchors(args.site, pages, samples)
        print(f'old anchors: {len(samples)} samples, {len(wrong)} wrong')
        for anchor, why in wrong:
            print(f'  #{anchor}: {why}')
        failed |= bool(wrong)

    if args.old_index:
        headings, signatures, unmapped = unmapped_old_anchors(args.site, args.old_index)
        print(f'old page: {len(headings)} heading anchors, {len(unmapped)} unmapped; {len(signatures)} signature '
              f'anchors (h4) not mapped by design')
        for anchor, why in unmapped:
            print(f'  #{anchor}: {why}')
        failed |= bool(unmapped)

    if args.compare:
        differences = compare(args.site, args.compare)
        print(f'compare with {args.compare}: {len(differences)} files differ')
        for name in differences[:50]:
            print(f'  {name}')
        failed |= bool(differences)
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
