// The digger JSON as the site sees it: which pages exist, where every symbol lives, and the old Slate anchors.
// Imported by astro.config.mjs (sidebar) and by the pages, so both agree on every URL.
import fs from 'node:fs';
import path from 'node:path';

// Inputs, set by site.bat (see README.md, "HTML reference").
const jsonPath = process.env.DD_SITE_JSON;
if (!jsonPath) {
    throw new Error('DD_SITE_JSON is not set: run site.bat, or set it to the digger JSON');
}
// The digger output.
export const api = JSON.parse(fs.readFileSync(jsonPath, 'utf-8'));
// Where the published site lives; links inside the site start with base.
export const site = process.env.DD_SITE_URL ?? 'https://spelunky-fyi.github.io';
export const base = (process.env.DD_SITE_BASE ?? '/overlunky').replace(/\/$/, '');
// Base of the source links: source paths in the JSON are relative to overlunky's src/game_api.
export const sourceUrl = process.env.DD_SITE_SOURCE_URL ?? 'https://github.com/spelunky-fyi/overlunky/blob/main/src/game_api';
// overlunky's docs/examples/ and docs-digger's examples/ (both optional), and the staged Lua downloads (optional).
const examplesDir = process.env.DD_SITE_EXAMPLES ?? '';
const diggerExamplesDir = process.env.DD_SITE_DIGGER_EXAMPLES ?? '';
export const downloadsDir = process.env.DD_SITE_DOWNLOADS ?? '';

if (api.schema_version !== 4) {
    throw new Error(`${jsonPath}: schema_version ${api.schema_version}, this site needs 4`);
}

// A site path with the base in front (`/types/Entity/` → `/overlunky/types/Entity/`).
export const url = (p) => base + p;
// The page slug of a group name (`Monsters, Inc.` → `monsters-inc`).
export const slug = (text) =>
    text
        .toLowerCase()
        .replace(/[^a-z0-9]+/g, '-')
        .replace(/^-|-$/g, '');
// The id Slate (redcarpet) gave a heading: runs of other characters than letters, digits and `_` become `-`.
export const slateId = (text) => text.replace(/[^A-Za-z0-9_]+/g, '-').replace(/^-|-$/g, '');
// Keys that are Lua names; others (`TYPE_MAP[...]`, `[sol::metatable_key]`) are left out, as in the Lua files.
const isLuaName = (name) => /^[A-Za-z_][A-Za-z0-9_]*$/.test(name);
// By name, keeping the first (functions) or last (members, as sol2 does) entry of a repeated key.
const byName = (items, keepLast) => {
    const map = new Map();
    for (const item of items) {
        if (keepLast || !map.has(item.name)) {
            map.set(item.name, item);
        }
    }
    return map;
};
const byKey = (a, b) => (a < b ? -1 : a > b ? 1 : 0);

// --- Functions ---------------------------------------------------------------------------------------------------

// Qualified Lua name of a function (`io.open_data`, `spawn`).
export const qualifiedName = (f) => [...(f.root ? [f.root] : []), ...(f.owner ?? []), f.name].join('.');
// Documented functions with Lua names, the first binding of each qualified name (as in the Lua files).
const qualifiedFirst = new Map();
for (const f of api.functions.filter((f) => !f.nodoc && qualifiedName(f).split('.').every(isLuaName))) {
    if (!qualifiedFirst.has(qualifiedName(f))) {
        qualifiedFirst.set(qualifiedName(f), f);
    }
}
// The name of the page that lists deprecated functions and the script callbacks they replace.
export const deprecatedGroup = 'Deprecated functions';
// Function pages: one per group, sorted by name as generate.py did, deprecated functions last.
export const functionPages = (() => {
    const groups = new Map();
    for (const f of qualifiedFirst.values()) {
        const group = f.deprecated ? deprecatedGroup : f.group;
        if (!groups.has(group)) {
            groups.set(group, []);
        }
        groups.get(group).push(f);
    }
    const names = [...groups.keys()].filter((g) => g !== deprecatedGroup).sort(byKey);
    if (groups.has(deprecatedGroup) || api.script_callbacks.length) {
        names.push(deprecatedGroup);
    }
    return names.map((group) => ({
        group,
        slug: slug(group),
        functions: (groups.get(group) ?? []).sort((a, b) => byKey(qualifiedName(a), qualifiedName(b))),
        scriptCallbacks: group === deprecatedGroup ? api.script_callbacks.filter((c) => !c.nodoc) : [],
    }));
})();

// --- Types -------------------------------------------------------------------------------------------------------

// Documented usertypes by Lua name.
export const usertypes = new Map(api.usertypes.filter((u) => !u.nodoc).map((u) => [u.name, u]));
// Usertypes by C++ type, for bases (`sol::bases<Entity, Movable>` names C++ types).
const byCpp = new Map(api.usertypes.map((u) => [u.cpp, u]));
// The usertype of a C++ base name.
export const baseType = (cpp) => byCpp.get(cpp) ?? usertypes.get(cpp.split('::').pop());
// Direct parent: sol::bases lists the ancestors from the root down, so it is the last one.
const parentOf = (u) => (u.bases?.length ? baseType(u.bases[u.bases.length - 1]) : undefined);
// Types whose direct parent is u, by name.
export const derivedTypes = (() => {
    const map = new Map();
    for (const u of usertypes.values()) {
        const parent = parentOf(u);
        if (parent) {
            map.set(parent.name, [...(map.get(parent.name) ?? []), u.name]);
        }
    }
    return map;
})();
// Members as Lua sees them: documented, last binding of a key.
export const membersOf = (u) => [...byName(u.members, true).values()].filter((m) => !m.nodoc);
// Type groups inside sections (generate.py's type_cats), both sorted by name.
export const typeSections = (() => {
    const sections = new Map();
    for (const u of usertypes.values()) {
        const section = u.section ?? 'Types';
        if (!sections.has(section)) {
            sections.set(section, new Map());
        }
        const groups = sections.get(section);
        groups.set(u.group, [...(groups.get(u.group) ?? []), u]);
    }
    return [...sections.keys()].sort(byKey).map((section) => ({
        section,
        groups: [...sections.get(section).keys()].sort(byKey).map((group) => ({
            group,
            types: sections
                .get(section)
                .get(group)
                .sort((a, b) => byKey(a.name, b.name)),
        })),
    }));
})();

// --- Enums, events, globals, aliases -----------------------------------------------------------------------------

// Documented enums by name.
export const enums = new Map(api.enums.filter((e) => !e.nodoc).map((e) => [e.name, e]));
// Documented aliases by name.
export const aliases = new Map(api.aliases.filter((a) => !a.nodoc).map((a) => [a.name, a]));
// Event tables (`ON`, `THEME_OVERRIDE`), each with its keys in enum order and the callbacks per storage.
export const eventTables = (() => {
    const tables = new Map();
    for (const event of api.events.filter((e) => !e.nodoc)) {
        if (!tables.has(event.table)) {
            tables.set(event.table, new Map());
        }
        const keys = tables.get(event.table);
        keys.set(event.name, [...(keys.get(event.name) ?? []), event]);
    }
    return [...tables.keys()].sort(byKey).map((table) => {
        const order = (enums.get(table)?.values ?? []).map((v) => v.name);
        const rank = (name) => (order.includes(name) ? order.indexOf(name) : order.length);
        const keys = [...tables.get(table).entries()].sort((a, b) => rank(a[0]) - rank(b[0]) || byKey(a[0], b[0]));
        return {table, keys: keys.map(([name, storages]) => ({name, storages}))};
    });
})();
// Global variables with the value that documents each (a doc comment preferred, then a known type).
export const globals = api.global_variables.map((name) => {
    const values = api.values.filter((v) => v.name === name && !v.owner && !v.root && !v.nodoc);
    const value = values.find((v) => v.doc?.length) ?? values.find((v) => v.type.lua !== 'any') ?? values[0];
    return {name, value};
});
// The entity casts (`Entity:as_movable`): the casting page lists them.
export const casts = (usertypes.get('Entity')?.members ?? []).filter((m) => m.name.startsWith('as_') && !m.nodoc);

// --- Examples ----------------------------------------------------------------------------------------------------

// An example file as Markdown: a .md file as it is, a .lua file as a Lua block.
const exampleText = (file) => {
    const text = fs.readFileSync(file, 'utf-8').replace(/\r\n/g, '\n');
    return path.extname(file) === '.md' ? text : '```lua\n' + text.trimEnd() + '\n```';
};
// Whether a file is an example (.md or .lua).
const isExample = (file) => ['.md', '.lua'].includes(path.extname(file));

// overlunky's docs/examples/<name>.md and <name>.lua, flat, by name.
const overlunkyExamples = (() => {
    const map = new Map();
    if (!examplesDir || !fs.existsSync(examplesDir)) {
        return map;
    }
    for (const file of fs.readdirSync(examplesDir).filter(isExample).sort()) {
        const name = path.basename(file, path.extname(file));
        map.set(name, [...(map.get(name) ?? []), exampleText(path.join(examplesDir, file))]);
    }
    return map;
})();

// Section titles of the hand-written texts (src/text/*.md), which the text kind of examples names.
const textTitles = () => {
    const dir = path.resolve('src/text');
    return fs
        .readdirSync(dir)
        .filter((f) => f.endsWith('.md'))
        .flatMap((f) => [...fs.readFileSync(path.join(dir, f), 'utf-8').matchAll(/^## (.+)$/gm)].map((m) => m[1].trim()));
};
// The names each kind of docs-digger example may have: what the pages show an example for.
const exampleNames = () =>
    new Map([
        ['functions', [...qualifiedFirst.keys()]],
        ['types', [...usertypes.values()].flatMap((u) => [u.name, ...membersOf(u).map((m) => `${u.name}.${m.name}`)])],
        ['enums', [...enums.keys()]],
        ['events', eventTables.flatMap((t) => t.keys.map((k) => `${t.table}.${k.name}`))],
        ['globals', globals.map((g) => g.name)],
        ['text', textTitles()],
    ].map(([kind, names]) => [kind, new Set(names)]));
// The files under dir, recursively, as paths relative to it with `/`.
const filesUnder = (dir) =>
    fs
        .readdirSync(dir, {recursive: true})
        .map((f) => f.split(path.sep).join('/'))
        .filter((f) => fs.statSync(path.join(dir, f)).isFile())
        .sort();
// docs-digger's examples/<kind>/<name with `.` as `/`>.md|.lua (`types/Entity/destroy.lua` is `Entity.destroy`), by
// `<kind>/<name>`. A file that names nothing on the pages fails the build, so a renamed symbol does not lose its
// example silently.
const localExamples = (() => {
    const map = new Map();
    const dir = diggerExamplesDir;
    if (!dir || !fs.existsSync(dir)) {
        return map;
    }
    const known = exampleNames();
    for (const file of filesUnder(dir).filter((f) => isExample(f) && f.includes('/'))) {
        const [kind, ...parts] = file.slice(0, -path.extname(file).length).split('/');
        const name = parts.join('.');
        if (!known.get(kind)?.has(name)) {
            throw new Error(`examples/${file}: no ${kind} named ${name} on the pages (kinds: ${[...known.keys()].join(', ')})`);
        }
        const key = `${kind}/${name}`;
        map.set(key, [...(map.get(key) ?? []), exampleText(path.join(dir, file))]);
    }
    return map;
})();

// The examples of a symbol: overlunky's by bare name first, then docs-digger's of that kind.
export const examplesOf = (kind, name) => [...(overlunkyExamples.get(name) ?? []), ...(localExamples.get(`${kind}/${name}`) ?? [])];

// --- Lua definition file downloads ------------------------------------------------------------------------------

// The dialects site.bat stages under DD_SITE_DOWNLOADS (<style>/*.lua and spel2-<style>.zip), in page order.
const dialects = [
    ['luals', 'LuaLS', 'lua-language-server (the VS Code Lua extension) with its newer annotations'],
    ['emmylua', 'EmmyLua', 'the classic, more limited dialect of older IDE plugins'],
];
// Each staged dialect: its files (published under /downloads/<style>/) and its ZIP.
export const downloads = dialects
    .map(([style, label, about]) => {
        const dir = downloadsDir ? path.join(downloadsDir, style) : '';
        const files = dir && fs.existsSync(dir) ? fs.readdirSync(dir).filter((f) => f.endsWith('.lua')).sort() : [];
        const zip = `spel2-${style}.zip`;
        return {style, label, about, files, zip: fs.existsSync(path.join(downloadsDir, zip)) ? zip : ''};
    })
    .filter((d) => d.files.length);
// Every published download path, relative to /downloads/.
export const downloadPaths = downloads.flatMap((d) => [
    ...d.files.map((f) => `${d.style}/${f}`),
    ...(d.zip ? [d.zip] : []),
]);

// --- Where symbols live ------------------------------------------------------------------------------------------

// Page path of a function group.
export const functionPage = (group) => `/functions/${slug(group)}/`;
// Site path of every documented symbol, by the name a doc comment or type uses for it.
export const symbols = (() => {
    const map = new Map();
    const add = (name, href) => {
        if (!map.has(name)) {
            map.set(name, href);
        }
    };
    for (const page of functionPages) {
        for (const f of page.functions) {
            add(qualifiedName(f), `${functionPage(page.group)}#${qualifiedName(f)}`);
        }
        for (const c of page.scriptCallbacks) {
            add(c.name, `${functionPage(page.group)}#${c.name}`);
        }
    }
    for (const u of usertypes.values()) {
        add(u.name, `/types/${u.name}/`);
    }
    for (const e of enums.values()) {
        add(e.name, `/enums/${e.name}/`);
    }
    for (const a of aliases.values()) {
        add(a.name, `/aliases/#${a.name}`);
    }
    for (const g of globals) {
        add(g.name, `/globals/#${g.name}`);
    }
    for (const t of eventTables) {
        for (const k of t.keys) {
            add(`${t.table}.${k.name}`, `/events/${t.table}/#${k.name}`);
        }
    }
    return map;
})();

// Headings of the old single page that are not symbols: their Slate id and the page that took their text.
const oldSections = {
    'Overlunky-Playlunky-Lua-API': '/',
    'Read-this-first': '/#read-this-first',
    'External-Function-Library': '/#external-function-library',
    'Lua-libraries': '/libraries/',
    'Unsafe-mode': '/libraries/#unsafe-mode',
    Modules: '/libraries/#modules',
    Aliases: '/aliases/',
    'Global-variables': '/globals/',
    'STD-Library-Containers': '/containers/',
    Functions: functionPage(functionPages[0]?.group ?? deprecatedGroup),
    'Non-Entity-types': '/types/#non-entity-types',
    'Entity-types': '/types/#entity-types',
    Events: '/events/ON/',
    Enums: '/enums/',
    'Automatic-casting-of-entities': '/casting/',
};
// The library headings of the old page (`#io`, `#json`) and their sections on the libraries page.
const oldLibraries = [
    'io',
    'os',
    'json',
    'inspect',
    'format',
    ...api.libraries.filter((l) => l.function === 'load_libraries').flatMap((l) => l.names),
];

// Old single-page anchor (`index.html#spawn_entity`) → site path, for the redirect script and doc links.
export const oldAnchors = (() => {
    const map = new Map();
    const add = (id, href) => {
        if (!map.has(id)) {
            map.set(id, href);
        }
    };
    for (const [id, href] of Object.entries(oldSections)) {
        add(id, href);
    }
    for (const lib of oldLibraries) {
        add(lib, `/libraries/#${lib}`);
    }
    for (const page of functionPages) {
        add(slateId(page.group), functionPage(page.group));
    }
    for (const s of typeSections) {
        for (const g of s.groups) {
            add(slateId(g.group), `/types/#${slug(g.group)}`);
        }
    }
    for (const [name, href] of symbols) {
        add(slateId(name), href);
    }
    return map;
})();

// Anchors by lower case, for doc links that got the case wrong (`#Get_frame`).
const anchorsByLowerCase = new Map(
    [...symbols, ...oldAnchors].reverse().map(([anchor, href]) => [anchor.toLowerCase(), href]),
);
// Site path of a doc comment link target (`#spawn`, `#Entity`), or undefined when nothing has that anchor.
export const resolveAnchor = (anchor) =>
    oldAnchors.get(anchor) ?? symbols.get(anchor) ?? anchorsByLowerCase.get(anchor.toLowerCase());

// Doc comment links (`[x](#anchor)`) of the JSON and the given texts that nothing resolves: doc fixes for overlunky.
export const unresolvedDocLinks = (texts) => {
    const found = new Set();
    const check = (line) => {
        for (const match of line.matchAll(/\]\(#([^)]*)\)/g)) {
            if (!resolveAnchor(match[1])) {
                found.add(match[1]);
            }
        }
    };
    const walk = (node) => {
        if (Array.isArray(node)) {
            node.forEach(walk);
        } else if (node && typeof node === 'object') {
            for (const [key, value] of Object.entries(node)) {
                (key === 'doc' || key === 'cpp_doc') && Array.isArray(value) ? value.forEach(check) : walk(value);
            }
        }
    };
    walk(api);
    texts.forEach(check);
    return [...found].sort();
};
// Source line on GitHub.
export const sourceLink = (source) => `${sourceUrl}/${source.file}#L${source.line}`;
