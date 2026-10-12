from pathlib import Path
from html.parser import HTMLParser

root = Path(__file__).parent / 'installation-website' / 'dist'
routes = {'es': 'instalacion', 'en': 'en/installation', 'pt': 'pt/instalacao'}

class Page(HTMLParser):
    def __init__(self, html):
        super().__init__()
        self.links, self.ids, self.canonical, self.lang = [], set(), None, None
        self.feed(html)
    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if 'id' in a: self.ids.add(a['id'])
        if tag == 'html': self.lang = a.get('lang')
        if tag == 'a': self.links.append(a.get('href', ''))
        if tag == 'link' and a.get('rel') == 'canonical': self.canonical = a['href']

for lang, route in routes.items():
    html = (root / route / 'index.html').read_text(encoding='utf-8')
    page = Page(html)
    assert page.lang == lang
    assert page.canonical == f'https://banchoxterm.gargadonapps.com/{route}/'
    assert 'freerdp3-dev' in html and 'winpr-devel' in html
    assert '--recurse-submodules' in html
    for target in routes.values(): assert f'/{target}/' in page.links
    for href in page.links:
        if href.startswith('#'): assert href[1:] in page.ids, href
        if href.startswith('/'):
            path, _, fragment = href.partition('#')
            file = root / path.strip('/') / 'index.html'
            assert file.exists(), href
            if fragment: assert fragment in Page(file.read_text(encoding='utf-8')).ids, href
    home = root / (lang if lang != 'es' else '') / 'index.html'
    assert f'/{route}/' in Page(home.read_text(encoding='utf-8')).links
    print(f'OK: /{route}/ — language, canonical, navigation, anchors and dependencies')
