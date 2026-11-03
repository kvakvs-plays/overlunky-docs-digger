// Search check of the built site (plan DIG-7 definition of done): for every sample of tools/site_samples.json the
// symbol's page is the first Pagefind result, and the symbol's heading its best sub-result (where it has one).
// Runs Pagefind's own browser search code, with fetch reading the index from disk.
// Usage: node scripts/check-search.mjs <site dir> <samples json>
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath, pathToFileURL} from 'node:url';
import {ranking} from '../src/lib/ranking.mjs';

const [siteDir, samplesFile] = process.argv.slice(2);
const pagefindDir = path.join(siteDir, 'pagefind') + path.sep;
const oldAnchors = JSON.parse(fs.readFileSync(path.join(siteDir, 'old-anchors.json'), 'utf-8'));
const {samples} = JSON.parse(fs.readFileSync(samplesFile, 'utf-8'));

// Pagefind fetches its index relative to pagefind.js; serve those files from disk.
globalThis.fetch = async (url) => {
    const file = fileURLToPath(new URL(String(url)));
    const type = file.endsWith('.wasm') ? 'application/wasm' : 'application/octet-stream';
    return new Response(fs.readFileSync(file), {headers: {'content-type': type}});
};
const pagefind = await import(pathToFileURL(pagefindDir + 'pagefind.js').href);
// DD_SEARCH_RANKING (JSON) tries other ranking weights without a rebuild; ranking.mjs is what the site uses.
const tried = process.env.DD_SEARCH_RANKING ? JSON.parse(process.env.DD_SEARCH_RANKING) : ranking;
await pagefind.options({basePath: pathToFileURL(pagefindDir).href, baseUrl: '/', ranking: tried});
await pagefind.init();

// The id Slate gave the old heading of a name.
const slateId = (text) => text.replace(/[^A-Za-z0-9_]+/g, '-').replace(/^-|-$/g, '');
// Where a sample lives on this site: page path and heading id ('' when the page itself is the symbol).
const target = (s) => {
    if (s.kind === 'member') {
        return {page: `/types/${s.type}/`, id: s.name};
    }
    if (s.kind === 'enum_value' || s.kind === 'alias') {
        // Table rows: no heading of their own.
        if (s.kind === 'alias') {
            return {page: '/aliases/', id: ''};
        }
        return {page: `/enums/${s.enum}/`, id: ''};
    }
    const name = s.kind === 'event' ? `${s.table}.${s.name}` : s.name;
    const [page, id = ''] = (oldAnchors[slateId(name)] ?? '?').split('#');
    return {page, id};
};
// A result URL (site path, baseUrl `/`) as page and id.
const sitePath = (url) => {
    const [page, id = ''] = url.split('#');
    return {page, id};
};
// Sub-results as the search box orders them: by the weight of their matches.
const score = (sub) => (sub.weighted_locations ?? []).reduce((sum, l) => sum + l.weight * l.balanced_score, 0);

// Rank (1-based) of the wanted page among the results, 0 when it is missing.
const rankOf = async (results, page) => {
    for (const [i, result] of results.slice(0, 20).entries()) {
        if (sitePath((await result.data()).url).page === page) {
            return i + 1;
        }
    }
    return 0;
};

let failures = 0;
let accepted = 0;
for (const sample of samples) {
    const query = sample.query ?? sample.name;
    const want = target(sample);
    const search = await pagefind.search(query);
    const first = search.results[0] ? await search.results[0].data() : undefined;
    const got = first ? sitePath(first.url) : {page: '(none)', id: ''};
    const best = first ? [...(first.sub_results ?? [])].sort((a, b) => score(b) - score(a))[0] : undefined;
    const bestId = best ? sitePath(best.url).id : '';
    const ok = got.page === want.page && (!want.id || bestId === want.id);
    const status = ok ? 'ok  ' : sample.accepted ? 'acc ' : 'FAIL';
    failures += ok || sample.accepted ? 0 : 1;
    accepted += !ok && sample.accepted ? 1 : 0;
    const rank = ok ? '' : `   (want ${want.page}${want.id ? '#' + want.id : ''}, rank ${await rankOf(search.results, want.page)})`;
    console.log(`${status} ${query.padEnd(28)} ${got.page}${bestId ? '#' + bestId : ''}${rank}`);
    if (!ok && sample.accepted) {
        console.log(`       accepted: ${sample.accepted}`);
    }
}
console.log(`search check: ${samples.length - failures - accepted} of ${samples.length} samples first, ${accepted} accepted`);
process.exit(failures ? 1 : 0);
