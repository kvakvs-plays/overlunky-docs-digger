// The staged Lua definition files and their ZIPs, published as they are under /downloads/.
import fs from 'node:fs';
import path from 'node:path';
import {downloadPaths, downloadsDir} from '../../lib/api.mjs';

export function getStaticPaths() {
    return downloadPaths.map((p) => ({params: {path: p}}));
}

export function GET({params}) {
    return new Response(fs.readFileSync(path.join(downloadsDir, params.path)));
}
