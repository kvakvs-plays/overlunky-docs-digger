// HTML pieces built from the JSON: Lua types with links, signatures and doc text. Pages put them in with set:html.
import {markdownToHtml} from 'satteri';
import {docsUrl, resolveAnchor, slug, symbols, url} from './api.mjs';

// Text for HTML.
export const escape = (text) =>
    String(text).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');

// Lua and LuaLS words that are never symbols of the API.
const builtins = new Set(['nil', 'boolean', 'integer', 'number', 'string', 'table', 'function', 'fun', 'any', 'userdata']);

// A Lua type (`Entity?`, `table<integer, ENT_TYPE>`, `fun(e: Movable): boolean`) as HTML, every API name a link.
export const typeHtml = (lua) =>
    escape(lua ?? 'any').replace(/[A-Za-z_][A-Za-z0-9_]*/g, (word) => {
        const href = builtins.has(word) ? undefined : symbols.get(word);
        return href ? `<a href="${url(href)}">${word}</a>` : word;
    });

// Parameters as Lua sees them: sol2's injected ones (`sol::this_state`) are hidden, and a method's object (`self`
// as the first parameter) is the `:` of the call.
export const visibleParams = (sig) => {
    const params = (sig.params ?? []).filter((p) => !p.type.hidden);
    return sig.self === 'first_param' ? params.slice(1) : params;
};
// The written name of a parameter: `...` for varargs, `name?` when it may be left out, `p1` when unnamed.
export const paramName = (p, i) => (p.type.kind === 'variadic' ? '...' : (p.name || `p${i + 1}`) + (p.omittable ? '?' : ''));
// The type of a parameter; an omittable one drops its own `?` (the name carries it).
const paramLua = (p) => (p.omittable ? (p.type.lua ?? 'any').replace(/\?$/, '') : p.type.lua);
// A callback's type as a LuaLS `fun(...)`, from its params and returns.
export const callbackLua = (cb) => {
    const params = (cb.params ?? []).filter((p) => !p.type.hidden).map((p, i) => `${paramName(p, i)}: ${paramLua(p)}`);
    const returns = (cb.returns ?? []).map((r) => r.lua).join(', ');
    return `fun(${params.join(', ')})${returns ? ': ' + returns : ''}`;
};
// The Lua type HTML of one parameter: its callback when it has a resolved one, else its own type.
export const paramTypeHtml = (p) => {
    const cb = p.callback;
    return cb && cb.params ? typeHtml(callbackLua(cb)) : typeHtml(paramLua(p));
};

// `function name(a: A, b?: B): R` with linked types; self is a method's object (`Entity:kill()`).
export const signatureHtml = (name, sig) => {
    const params = visibleParams(sig).map((p, i) => `${escape(paramName(p, i))}: ${paramTypeHtml(p)}`);
    const returns = (sig.returns ?? []).map((r) => typeHtml(r.lua)).join(', ');
    return `function ${escape(name)}(${params.join(', ')})${returns ? ': ' + returns : ''}`;
};

// Doc lines as segments: Markdown text, and the fenced code blocks between, which pages give to Expressive Code.
export const docSegments = (lines) => {
    const text = (Array.isArray(lines) ? lines.join('\n') : lines ?? '').replace(/([^\n])```/g, '$1\n```');
    const segments = [];
    const fence = /^```([\w-]*)[^\n]*\n([\s\S]*?)^```[^\n]*$/gm;
    let last = 0;
    for (let match; (match = fence.exec(text)); ) {
        segments.push({html: markdownHtml(text.slice(last, match.index))});
        segments.push({code: match[2].replace(/\n$/, ''), lang: match[1] || 'lua'});
        last = match.index + match[0].length;
    }
    segments.push({html: markdownHtml(text.slice(last))});
    return segments.filter((s) => s.code !== undefined || s.html);
};

// A hand-written text cut at its `## ` headings: the text before the first, and {title, body} per section.
export const textSections = (text) => {
    const [intro, ...rest] = text.replace(/\r\n/g, '\n').split(/^## /m);
    const sections = rest.map((part) => {
        const end = part.indexOf('\n');
        return {title: part.slice(0, end).trim(), body: part.slice(end + 1)};
    });
    return {intro, sections};
};
// The TOC entries of such a text.
export const textHeadings = (text) =>
    textSections(text).sections.map((s) => ({depth: 2, slug: slug(s.title), text: s.title}));

// Markdown as HTML; old single-page links (`(#spawn)`) point at the pages that have those items now. A link to an
// anchor nothing has stays as its text (astro.config.mjs lists those at the end of the build). A relative link
// (`game_data/textures.txt`) was relative to the old page in overlunky's docs/, so it points there.
const markdownHtml = (md) => {
    if (!md.trim()) {
        return '';
    }
    const {html} = markdownToHtml(md, {features: {smartPunctuation: false, frontmatter: false}});
    return html
        .trim()
        .replace(/<a href="#([^"]*)">([\s\S]*?)<\/a>/g, (whole, anchor, text) => {
            const href = resolveAnchor(decodeURIComponent(anchor));
            return href ? `<a href="${url(href)}">${text}</a>` : text;
        })
        .replace(/<a href="(?![a-z][a-z0-9+.-]*:|[#/])([^"]*)"/gi, (whole, path) => `<a href="${docsUrl}/${path}"`);
};
