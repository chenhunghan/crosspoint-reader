// Dev-loop client. Active only when the page is served by sim/dev.sh, which
// answers GET __dev/state; anywhere else (GitHub Pages, `agentmux daemon --web`)
// the probe 404s and nothing else happens.
//   __dev/state   {version, building, error, built}
//   __dev/events  server-sent "state" events with the same object
// A new version reloads the page (after `beforeReload()`); a failed build shows
// an overlay with the compiler output and keeps the running firmware.
// Resolves to null when not served by the dev loop, else {loaded()}: call it once
// the firmware drew its first frame (the server logs edit-to-screen latency).
export async function startDevLoop({onStatus = () => {}, beforeReload = () => {}} = {}) {
  let first;
  try {
    const r = await fetch('__dev/state', {cache: 'no-store'});
    if (!r.ok) return null;
    first = await r.json();
  } catch (e) {
    return null;
  }
  const loaded = first.version;
  const overlay = makeOverlay();
  const apply = (s) => {
    if (s.error) {
      overlay.show(s.error);
      onStatus('error', 'build failed');
      return;
    }
    overlay.hide();
    if (s.building) { onStatus('busy', 'rebuilding…'); return; }
    if (s.version !== loaded) {
      onStatus('busy', 'reloading…');
      try { beforeReload(); } catch (e) { console.warn(e); }
      location.reload();
      return;
    }
    onStatus('ok', 'auto-reload' + (s.built ? ' · built ' + s.built : ''));
  };
  apply(first);
  const es = new EventSource('__dev/events');
  es.addEventListener('state', (ev) => apply(JSON.parse(ev.data)));
  es.onerror = () => onStatus('err', 'dev server offline');
  return {loaded: () => fetch('__dev/loaded', {method: 'POST', body: loaded}).catch(() => {})};
}

function makeOverlay() {
  const box = document.createElement('div');
  box.id = 'buildError';
  box.hidden = true;
  box.innerHTML = `<style>
    #buildError{position:fixed;left:16px;right:16px;bottom:16px;max-height:55vh;z-index:100;display:flex;flex-direction:column;
      background:#1d1416;color:#f3d9d9;border:1px solid #6b2a30;border-radius:10px;box-shadow:0 20px 60px #0007;overflow:hidden}
    #buildError header{display:flex;align-items:center;gap:10px;padding:10px 14px;background:#3a1a1f;font:600 13px system-ui,sans-serif}
    #buildError header span{flex:1}
    #buildError button{font:inherit;font-weight:500;background:none;border:1px solid #8a3b42;color:inherit;border-radius:6px;padding:2px 9px;cursor:pointer}
    #buildError pre{margin:0;padding:12px 14px;overflow:auto;font:12px/1.45 ui-monospace,SFMono-Regular,Menlo,monospace;white-space:pre-wrap}
    #buildError pre b{color:#ff8f8f}
  </style><header><span>Build failed · the page keeps running the last good firmware</span><button type="button">Hide</button></header><pre></pre>`;
  box.querySelector('button').onclick = () => (box.hidden = true);
  document.body.appendChild(box);
  let last = '';
  return {
    show(text) {
      if (text !== last) {
        last = text;
        const pre = box.querySelector('pre');
        pre.textContent = '';
        for (const line of text.split('\n')) {
          const el = document.createElement(/error/i.test(line) ? 'b' : 'span');
          el.textContent = line + '\n';
          pre.appendChild(el);
        }
        box.hidden = false;
      }
    },
    hide() { last = ''; box.hidden = true; },
  };
}
