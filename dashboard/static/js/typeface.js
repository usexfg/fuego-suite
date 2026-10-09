// ── Maison typeface ───────────────────────────────────────────────────────────
// The register and the typeface are independent axes: the typeface only applies
// while maison is active, and is remembered across register switches.
//
// Reads the active register from the document rather than subscribing to each
// page's own theme bus — the bench and the live page expose different APIs, and
// coupling this control to either would break the other's independence.
'use strict';

const MaisonTypeface = (() => {
  const KEY = 'xfg.maisonTypeface';
  const DEFAULT = 'saira';
  const ALLOWED = ['saira', 'cormorant', 'bitter', 'sourceserif', 'plex'];

  const root = () => document.documentElement;

  function stored() {
    try {
      const v = localStorage.getItem(KEY);
      return ALLOWED.includes(v) ? v : DEFAULT;
    } catch (e) {
      return DEFAULT;   // private mode
    }
  }

  function apply(name) {
    const el = root();
    const v = ALLOWED.includes(name) ? name : DEFAULT;
    if (v === DEFAULT) el.removeAttribute('data-typeface');
    else el.setAttribute('data-typeface', v);
    try { localStorage.setItem(KEY, v); } catch (e) { /* private mode */ }
    return v;
  }

  // The control is meaningless outside maison: the other registers pin their own
  // face, so showing a live-looking selector there would be a lie.
  function sync() {
    const box = document.getElementById('typeface-switch');
    const sel = document.getElementById('maison-typeface');
    if (!box || !sel) return;
    const maison = root().getAttribute('data-theme') === 'maison';
    box.hidden = !maison;
    sel.value = root().getAttribute('data-typeface') || DEFAULT;
  }

  function init() {
    const box = document.getElementById('typeface-switch');
    const sel = document.getElementById('maison-typeface');
    if (!box || !sel) return;
    apply(stored());
    sel.value = root().getAttribute('data-typeface') || DEFAULT;
    sel.addEventListener('change', () => {
      apply(sel.value);
      sel.value = root().getAttribute('data-typeface') || DEFAULT;
    });
    sync();
    if (typeof MutationObserver !== 'undefined') {
      new MutationObserver(sync).observe(root(), { attributes: true, attributeFilter: ['data-theme'] });
    }
  }

  return { init, apply, sync, ALLOWED, DEFAULT };
})();

if (typeof module !== 'undefined' && module.exports) module.exports = MaisonTypeface;
