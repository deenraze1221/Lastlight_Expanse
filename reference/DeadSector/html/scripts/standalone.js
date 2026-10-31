import { readFile, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';

// Ship one playable file: no imports, network requests or external stylesheets.
let html = await readFile('dist/app.html', 'utf8');
const script = html.match(/<script\b[^>]*src="([^"]+)"[^>]*><\/script>/);
const style = html.match(/<link\b[^>]*href="([^"]+\.css)"[^>]*>/);
if (!script || !style) throw new Error('Build output is missing its JavaScript or CSS');
const js = await readFile(resolve('dist', script[1]), 'utf8');
const css = await readFile(resolve('dist', style[1]), 'utf8');
html = html.replace(script[0], () => `<script type="module">${js.replace(/<\/script/gi, '<\\/script')}</script>`);
html = html.replace(style[0], () => `<style>${css}</style>`);
await writeFile('index.html', html);
await writeFile('dist/index.html', html);
console.log('Standalone index.html generated: open directly, no server required.');
