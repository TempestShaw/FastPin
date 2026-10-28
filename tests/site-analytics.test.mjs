import assert from 'node:assert/strict';
import test from 'node:test';
import vm from 'node:vm';
import { readFileSync } from 'node:fs';

const source = readFileSync(new URL('../site/analytics.js', import.meta.url), 'utf8');
test('analytics loads only on this production site or explicit debug QA', () => {
  for (const [url, enabled, debug] of [
    ['https://tempestshaw.github.io/FastPin/', true, undefined],
    ['https://tempestshaw.github.io/FastPin/details/', true, undefined],
    ['https://tempestshaw.github.io/Dopamine/', false, undefined],
    ['http://localhost:3000/', false, undefined],
    ['http://localhost:3000/?analytics_debug=true', false, undefined],
    ['http://localhost:3000/?analytics_debug=1', true, true],
  ]) {
    const scripts = [];
    const context = { location: new URL(url), URLSearchParams, window: {}, document: { createElement: () => ({}), head: { appendChild: script => scripts.push(script) } } };
    vm.runInNewContext(source, context);
    assert.equal(scripts.length, Number(enabled), url);
    if (!enabled) { assert.equal(context.window.dataLayer, undefined); continue; }
    assert.equal(scripts[0].src, 'https://www.googletagmanager.com/gtag/js?id=G-1H324QQV06');
    const configs = context.window.dataLayer.filter(args => args[0] === 'config');
    assert.equal(configs.length, 1);
    assert.equal(configs[0][1], 'G-1H324QQV06');
    assert.equal(configs[0][2].debug_mode, debug);
    assert.equal(configs[0][2].allow_google_signals, false);
    assert.equal(configs[0][2].allow_ad_personalization_signals, false);
  }
});
