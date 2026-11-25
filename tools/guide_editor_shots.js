/* SPDX-License-Identifier: GPL-3.0-only
 * The web editor's screens for the user guide (docs/guide.html): web/editor.html in Chromium, connected to the PC
 * twin (serve) through a Web MIDI stand-in, as tests/editor_e2e.js does; a screenshot of each tab in OUT.
 *   node tools/guide_editor_shots.js TWIN OUT      (TWIN: build/twin or build/twin-mac; playwright on NODE_PATH) */
const { chromium } = require('playwright');
const { spawn } = require('child_process');
const path = require('path'), fs = require('fs');
const TWIN = process.argv[2] || './build/twin', OUT = path.resolve(process.argv[3] || 'build/guide/editor');
fs.mkdirSync(OUT + '/twin', { recursive: true });
(async () => {
  const tw = spawn(TWIN, ['serve', OUT + '/twin', '-', '1']);
  let buf = '', ready, readyP = new Promise(r => ready = r), deliver = null;
  tw.stdout.on('data', d => { buf += d; let i; while ((i = buf.indexOf('\n')) >= 0) { const l = buf.slice(0, i); buf = buf.slice(i + 1);
    if (l === 'READY') ready(); else if (l.startsWith('M ') && deliver) deliver(l.slice(2).split(' ').map(h => parseInt(h, 16))); } });
  await readyP;
  const br = await chromium.launch();
  const pg = await br.newPage({ viewport: { width: 1280, height: 800 }, deviceScaleFactor: 2 });
  await pg.exposeFunction('twinSend', a => tw.stdin.write('M ' + a.map(x => x.toString(16)).join(' ') + '\n'));
  let sx = null;
  deliver = bytes => { for (const b of bytes) { if (b === 0xF0) sx = []; if (sx) { sx.push(b); if (b === 0xF7) { const m = sx; sx = null; pg.evaluate(m => window.__twinIn(m), m).catch(() => {}); } } } };
  await pg.addInitScript(() => {
    const inp = { name: 'SLOOP', onmidimessage: null }, out = { name: 'SLOOP', send: d => window.twinSend(Array.from(d)) };
    window.__twinIn = m => { if (inp.onmidimessage) inp.onmidimessage({ data: Uint8Array.from(m) }); };
    navigator.requestMIDIAccess = async () => ({ inputs: new Map([['i', inp]]), outputs: new Map([['o', out]]), onstatechange: null });
    try { localStorage.clear(); } catch (e) {}
  });
  await pg.goto('file://' + path.resolve('web/editor.html'));
  await pg.waitForTimeout(600);
  await pg.screenshot({ path: OUT + '/connect.png' });
  await pg.click('#con'); await pg.waitForTimeout(1800);
  const tab = async (t, wait) => { await pg.click('#tabs button[data-t="' + t + '"]'); await pg.waitForTimeout(wait || 1200); };
  await tab('live'); await pg.click('#tab_live .btn.big.grn'); await pg.waitForTimeout(1500);
  await pg.screenshot({ path: OUT + '/live.png' });
  for (const t of ['sound', 'seq', 'mixer', 'song', 'library', 'samples', 'kits', 'projects', 'settings']) {
    await tab(t, t === 'seq' || t === 'sound' ? 1800 : 1200);
    await pg.screenshot({ path: OUT + '/' + t + '.png' });
  }
  await br.close(); tw.stdin.write('Q\n'); tw.kill();
  console.log('editor shots in', OUT);
})().catch(e => { console.error(e); process.exit(1); });
