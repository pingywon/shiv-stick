// Drives the real control panel inside headless Chromium (served by mock_stick.py at /test).
// Results land in <pre id="testout"> as JSON so `chromium --dump-dom` can read them.
(async () => {
  const R = { errors: [], checks: {} };
  window.addEventListener('error', e => R.errors.push(String(e.message)));
  window.addEventListener('unhandledrejection', e => R.errors.push('rejection: ' + String(e.reason)));
  const sleep = ms => new Promise(r => setTimeout(r, ms));
  const ok = (name, cond, detail) => { R.checks[name] = cond ? 'PASS' : 'FAIL' + (detail !== undefined ? ' -> ' + JSON.stringify(detail) : ''); };
  const q = s => document.querySelector(s);
  try {
    for (let i = 0; i < 40 && !window.CFG && typeof CFG === 'undefined'; i++) await sleep(100);
    await sleep(900);
    // --- config load + fill
    ok('config loaded', !!CFG && CFG.place === 'Your City', CFG && CFG.place);
    ok('plain field filled', q('[data-k=place]').value === 'Your City');
    ok('secret never prefilled', q('[data-k=pi6Pass]').value === '' && /saved/.test(q('[data-k=pi6Pass]').placeholder), q('[data-k=pi6Pass]').placeholder);
    ok('unset secret says not set', /not set/.test(q('[data-k=haToken]').placeholder));
    ok('wifi ssid filled, pass blank', q('[data-w="0s"]').value === 'HomeNet' && q('[data-w="0p"]').value === '');
    ok('five wifi slots, second one filled', document.querySelectorAll('[data-w$="s"]').length === 5 && q('[data-w="1s"]').value === 'Workshop' && q('[data-w="4s"]').value === '');
    ok('25 screen checkboxes', document.querySelectorAll('[data-sc]').length === 25);
    ok('favicon link present', !!q('link[rel=icon]') && /svg/.test(q('link[rel=icon]').href));
    ok('theme select lists 3 skins', q('[data-s=theme]').options.length === 3 && /AMBER/.test(q('[data-s=theme]').textContent));
    ok('skin colours applied as CSS vars', /00f0ff/i.test(getComputedStyle(document.documentElement).getPropertyValue('--cy')));
    // --- tabs
    ok('5 tabs: STATUS, SCREENS, CONNECTIONS, MACROS & IR, SYSTEM', [...document.querySelectorAll('nav button')].map(b => b.textContent).join('|') === 'STATUS|SCREENS|CONNECTIONS|MACROS & IR|SYSTEM', [...document.querySelectorAll('nav button')].map(b => b.textContent));
    { const keys = [...document.querySelectorAll('[data-k],[data-s]')].map(e => (e.dataset.k ? 'k:' + e.dataset.k : 's:' + e.dataset.s)); const dup = keys.filter((k, i) => keys.indexOf(k) !== i);
      ok('every setting appears exactly once', dup.length === 0 && keys.length > 20, [dup, keys.length]); }
    ok('mic card: the kept mic settings', !!q('[data-s=micOn]') && !!q('[data-s=micSens]') && !!q('[data-s=spectrumIdle]') && !!q('[data-s=petHears]'));
    ok('no toy settings left', !q('[data-s=jackIn]') && !q('[data-s=edgeRamp]') && !q('[data-k=joyName]') && !q('[data-k=bridgeHost]') && !q('[data-s=wledOn]') && !q('[data-s=handsFree]'));
    ok('HA address + favorites + macros + IR on their tabs', !!q('#t-data [data-k=haUrl]') && !!q('#t-data #favs') && !!q('#t-macros #irs') && !!q('#t-macros #macros'));
    tab('home'); ok('old HOME ASSISTANT link lands on CONNECTIONS', q('#t-data').classList.contains('on')); tab('status');

    // --- chevron fold on the mic card (a .sec)
    secTog(q('#s-mic .chev'));
    ok('chevron toggles a section and remembers it', (q('#s-mic').classList.contains('closed') ? localStorage.getItem('sec-s-mic') === '1' : localStorage.getItem('sec-s-mic') === '0'));
    secTog(q('#s-mic .chev'));

    // --- SCREENS tab
    tab('screens'); await sleep(100);
    ok('HOW IT WORKS folds all start closed', document.querySelectorAll('.how').length >= 4 && !document.querySelector('.how.open'), document.querySelectorAll('.how').length);
    howTog(q('#t-screens .how > button')); ok('a fold opens on tap', q('#t-screens .how').classList.contains('open')); howTog(q('#t-screens .how > button'));
    ok('clock+system locked', q('[data-sc="0"]').disabled && q('[data-sc="20"]').disabled);
    ok('favorites loop rendered with 5 stops, no toy pages', q('#favsc').children.length === 5 && /TV-OFF/.test(q('#favsc').children[1].textContent) && !/LIVE/.test(q('#favsc').textContent), q('#favsc').textContent);

    // --- CONNECTIONS tab lists
    tab('data'); await sleep(50);
    ok('lists rendered', q('#hosts').children.length === 2 && q('#favs').children.length === 1 && q('#macros').children.length === 1);
    ok('no per-tab SAVE buttons', ![...document.querySelectorAll('section button')].some(b => b.textContent.trim() === 'SAVE'));
    ok('no browser confirm() left in the page', !/confirm\(/.test(document.documentElement.innerHTML));

    // --- edit + save round-trip
    tab('system'); await sleep(50);
    q('[data-s=theme]').value = '2';
    q('[data-k=haToken]').value = 'tok"en<1>';
    tab('screens'); await sleep(50);
    q('[data-sc="6"]').checked = false;            // hide SHOP (index 6)
    favMove(1, -1);                                // TV-OFF to the front of the loop
    tab('data'); await sleep(50);
    addRow('hosts');
    const hr = q('#hosts').lastElementChild.querySelectorAll('input');
    hr[0].value = 'PRINTER'; hr[1].value = '192.168.1.216'; hr[2].value = '9100';
    addRow('favs');                                // left blank on purpose: must be dropped
    await saveCfg(); await sleep(500);
    let sent = (await (await fetch('/api/log')).json()).filter(l => l.path === '/api/config').pop().body;
    ok('save: theme sent as number', sent.set.theme === 2, sent.set.theme);
    ok('save: mute sent as boolean', sent.set.mute === false, sent.set.mute);
    ok('save: micOn + otaOn sent as booleans', sent.set.micOn === false && sent.set.otaOn === true && typeof sent.set.micSens === 'number', sent.set);
    ok('save: screenMask - SHOP cleared, CLOCK+SYSTEM kept', (sent.set.screenMask & (1 << 6)) === 0 && (sent.set.screenMask & 1) === 1 && (sent.set.screenMask & (1 << 20)) !== 0, sent.set.screenMask);
    ok('save: only the typed secret is sent', sent.haToken === 'tok"en<1>' && !('pi6Pass' in sent) && !('webPass' in sent), Object.keys(sent));
    ok('save: wifi slot 1 has no pass key', !('pass' in sent.wifi[0]) && sent.wifi[0].ssid === 'HomeNet', sent.wifi);
    ok('save: all five wifi slots sent', sent.wifi.length === 5 && sent.wifi[1].ssid === 'Workshop' && sent.wifi[4].ssid === '', sent.wifi);
    ok('save: 3 hosts, port numeric', sent.hosts.length === 3 && sent.hosts[2].port === 9100, sent.hosts);
    ok('save: blank favorite dropped', sent.favs.length === 1, sent.favs);
    ok('save: favorites order sent', JSON.stringify(sent.set.favOrder) === '[15,0,10,3,20]', sent.set.favOrder);
    ok('save: no toy keys in payload', !('joyProgs' in sent) && !('moodLights' in sent) && !('bridgeHost' in sent) && !('wledHosts' in sent) && !('jackIn' in sent.set) && !('edgeRamp' in sent.set), Object.keys(sent));
    await poll(); await sleep(50);
    ok('after save: secret shows saved', /saved/.test(q('[data-k=haToken]').placeholder) && q('[data-k=haToken]').value === '');
    ok('after save: theme persisted', q('[data-s=theme]').value === '2');

    // --- status tiles + OTA card
    ok('status polled', /LINKED/.test(q('#pill').textContent), q('#pill').textContent);
    ok('status tiles are non-toy', /MQTT/.test(q('#stats').textContent) && /LAST RESET/.test(q('#stats').textContent) && !/JACK IN/.test(q('#stats').textContent), q('#stats').textContent.slice(0, 200));
    ok('curl hint has key', /key=mock0123456789ab/.test(q('#curl').textContent));
    ok('no-password warning shown', q('#pwwarn').style.display === 'block');
    ok('auto-update card: up to date message', /On 1\.0\.0-mock/.test(q('#otamsg').textContent) && q('#otaInstall').style.display === 'none', q('#otamsg').textContent);
    await otaPost('check'); await sleep(200); await poll(); await sleep(50);
    ok('auto-update card: offers install after a check', /Update ready/.test(q('#otamsg').textContent) && q('#otaInstall').style.display !== 'none', q('#otamsg').textContent);

    // --- hostile strings must stay text
    await loadEntities();
    ok('entities: no injected elements', q('#ents').querySelectorAll('b,img,script').length === 0 && q('#ents').children.length === 3);
    CFG.ir = [{ name: '<img src=x onerror="window.__xss=1">', len: 5 }]; irList();
    await sleep(200);
    ok('ir names escaped', !window.__xss && q('#irs').querySelectorAll('img').length === 0);

    // --- IR rename + page
    await load();
    q('[data-ir="1"]').value = 'soundbar';
    await irDo('rename', 1);
    ok('ir rename round-trip', CFG.ir[1].name === 'SOUNDBAR', CFG.ir);
    q('#pmsg').value = 'test page';
    sendPage(); await sleep(400);
    const st = await (await fetch('/api/status')).json();
    ok('page delivered', st.unread === 1 && st.screen === 10, st);

    // --- Pi-hole button + settings
    tab('status'); await poll();
    ok('pihole button offers disable', /DISABLE FOR 10 H/.test(q('#pibtn').textContent), q('#pibtn').textContent);
    piToggle(); await sleep(400); await poll();
    ok('pihole button flips to enable + countdown', /ENABLE NOW/.test(q('#pibtn').textContent) && /back in 9h 59m|back in 10h 0m/.test(q('#pistate').textContent), q('#pistate').textContent);
    const plog = (await (await fetch('/api/log')).json()).filter(l => l.path === '/api/pihole').pop().body;
    ok('pihole post asks for disable explicitly', plog.enable === false, plog);
    ok('pihole settings round-trip', q('[data-s=piTarget]').value === '5' && q('[data-s=piOffHours]').value === '10');

    // --- Home Assistant test button
    tab('data'); await haTest(); await sleep(300);
    ok('HA test reports success text', /Connected to "Home"/.test(q('#hamsg').textContent), q('#hamsg').textContent);
    q('[data-k=haToken]').value = 'bad'; await haTest(); await sleep(300);
    ok('HA test reports the exact failure', /rejected the token/.test(q('#hamsg').textContent), q('#hamsg').textContent);

    // --- live screen mirror (environmental in headless; may be the only flaky one)
    tab('status'); await mirror();
    const px = q('#mirror').getContext('2d').getImageData(0, 0, 240, 135).data;
    let lit = 0; for (let i = 0; i < px.length; i += 4) if (px[i] + px[i + 1] + px[i + 2] > 60) lit++;
    ok('screen mirror drew pixels', lit > 500, lit);
    ok('tab deep-link helper exists', typeof tab === 'function');
  } catch (e) { R.errors.push('test crashed: ' + (e && e.stack || e)); }
  const pre = document.createElement('pre'); pre.id = 'testout'; pre.textContent = 'TESTOUT' + JSON.stringify(R) + 'TESTEND';
  document.body.appendChild(pre);
})();
