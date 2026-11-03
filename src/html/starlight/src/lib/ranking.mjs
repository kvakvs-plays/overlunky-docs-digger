// Pagefind ranking of the search box, shared by astro.config.mjs and scripts/check-search.mjs so the check measures
// what readers get. Tuned on tools/site_samples.json (plan DIG-7c).
export const ranking = {
    // Long pages (a usertype with hundreds of members) are not penalised much: they are the symbols' own pages.
    pageLength: 0.1,
    // Presence counts, not repetition: a page that mentions a name often is not the page about it.
    termFrequency: 0,
    termSaturation: 2,
    // Whole-word matches far above longer words that start with the query.
    termSimilarity: 9,
    // A match in the page's own symbols (SearchSymbols.astro) beats a mention.
    metaWeights: {symbols: 10},
};
