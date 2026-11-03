// Starlight's collections. Every page is generated from the digger JSON (src/pages/), so docs only holds the 404 page
// (Starlight's own 404 route is off) and i18n stays empty (English only).
import {defineCollection} from 'astro:content';
import {docsLoader, i18nLoader} from '@astrojs/starlight/loaders';
import {docsSchema, i18nSchema} from '@astrojs/starlight/schema';

export const collections = {
    docs: defineCollection({loader: docsLoader(), schema: docsSchema()}),
    i18n: defineCollection({loader: i18nLoader(), schema: i18nSchema()}),
};
