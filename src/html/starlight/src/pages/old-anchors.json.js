// Old single-page anchor → site path (without the base), for OldAnchors.astro and tools/parity.py.
import {oldAnchors} from '../lib/api.mjs';

export function GET() {
    const sorted = [...oldAnchors.entries()].sort((a, b) => (a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0));
    return new Response(JSON.stringify(Object.fromEntries(sorted), null, 1));
}
