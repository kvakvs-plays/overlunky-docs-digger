// Astro + Starlight configuration of the HTML reference. Everything comes from the digger JSON (src/lib/api.mjs);
// site.bat sets the inputs (DD_SITE_*) and the output directory (DD_SITE_OUT).
import fs from 'node:fs';
import {defineConfig} from 'astro/config';
import starlight from '@astrojs/starlight';
import {
    base,
    enums,
    eventTables,
    functionPage,
    functionPages,
    site,
    typeSections,
    unresolvedDocLinks,
} from './src/lib/api.mjs';
import {ranking} from './src/lib/ranking.mjs';

// Lists the doc links nothing resolves (they render as text): doc fixes for overlunky, not build problems.
const docLinkReport = {
    name: 'docs-digger-doc-links',
    hooks: {
        'astro:build:done': ({logger}) => {
            const texts = fs.readdirSync('src/text').map((f) => fs.readFileSync(`src/text/${f}`, 'utf-8'));
            const unresolved = unresolvedDocLinks(texts);
            if (unresolved.length) {
                logger.info(`doc links to anchors nothing has (shown as text): ${unresolved.join(', ')}`);
            }
        },
    },
};

// A sidebar link to a site path (Starlight adds the base).
const link = (label, path) => ({label, link: path});

// Functions by group, types by section and group (collapsed: there are hundreds), enums, events.
const sidebar = [
    link('Home', '/'),
    link('Lua libraries', '/libraries/'),
    link('Global variables', '/globals/'),
    link('STD library containers', '/containers/'),
    {label: 'Functions', items: functionPages.map((p) => link(p.group, functionPage(p.group)))},
    {
        label: 'Types',
        items: [
            link('All types', '/types/'),
            ...typeSections.map((s) => ({
                label: s.section,
                collapsed: true,
                items: s.groups.map((g) => ({
                    label: g.group,
                    collapsed: true,
                    items: g.types.map((u) => link(u.name, `/types/${u.name}/`)),
                })),
            })),
        ],
    },
    {label: 'Events', items: eventTables.map((t) => link(t.table, `/events/${t.table}/`))},
    {
        label: 'Enums',
        collapsed: true,
        items: [link('About enums', '/enums/'), ...[...enums.keys()].map((name) => link(name, `/enums/${name}/`))],
    },
    link('Casting', '/casting/'),
    link('Aliases', '/aliases/'),
];

export default defineConfig({
    site,
    base: base || '/',
    trailingSlash: 'always',
    outDir: process.env.DD_SITE_OUT ?? './dist',
    cacheDir: process.env.DD_SITE_CACHE ?? './node_modules/.astro',
    integrations: [
        starlight({
            title: 'Overlunky/Playlunky Lua API',
            description: 'Documentation for the unofficial Spelunky 2 Lua modding API',
            social: [{icon: 'github', label: 'GitHub', href: 'https://github.com/spelunky-fyi/overlunky'}],
            sidebar,
            customCss: ['./src/styles/api.css'],
            pagination: false,
            lastUpdated: false,
            // src/content/docs/404.md replaces it.
            disable404Route: true,
            pagefind: {ranking},
        }),
        docLinkReport,
    ],
});
