/* PS5 cooling gauge · standalone Web Component · no dependencies.
 *
 * Design: the user's own gauge picture (10.10.2026), a half-round dial with a rainbow arc and a needle. Until 1.55.x this
 * element drew a turning fan (the user's "PS5-Luefter-Demo", 08.10.2026); the tag name and the interface stayed, so the
 * page did not have to change. The dial is a picture without its needle; the needle and the dimming of the arc behind it
 * are drawn here and move to the value like a speedometer. Nothing runs between two changes of the value.
 * For the console's own browser (an old WebKit): no aspect-ratio, inset or "rgb(r g b / a)" colours, no
 * MediaQueryList.addEventListener, and every turn is a CSS transform of a whole <svg> box with its centre given in
 * percent: a centre in px, or a turn of an element inside the svg, lay beside the middle when the page was zoomed. */
(function () {
  'use strict';
  const clamp = (v, min = 0, max = 100) => Math.min(max, Math.max(min, v));
  const BLUE = [65, 170, 255], GREEN = [71, 222, 145], YELLOW = [250, 214, 64], RED = [255, 85, 67], GREY = [116, 130, 151];
  const isValid = v => (typeof v === 'number' || (typeof v === 'string' && v.trim() !== '')) && Number.isFinite(Number(v));
  const normalize = v => isValid(v) ? clamp(Number(v)) : null;
  /* Stufenlos Blau (bis 25 %) -> Grün (55 %) -> Gelb (75 %) -> Rot (ab 95 %); Gelb kam am 10.10.2026 dazu (Wunsch des Users). */
  function colorAt(percent) {
    const p = clamp(percent);
    const mix = (a, b, t) => a.map((n, i) => Math.round(n + (b[i] - n) * clamp(t, 0, 1)));
    if (p <= 55) return mix(BLUE, GREEN, (p - 25) / 30);
    if (p <= 75) return mix(GREEN, YELLOW, (p - 55) / 20);
    return mix(YELLOW, RED, (p - 75) / 20);
  }
  const zoneAt = p => p <= 35 ? 'cool' : p < 80 ? 'normal' : 'high';
  /* a media query's change event, in the old and the new form */
  const listen = (q, fn) => { if (q.addEventListener) q.addEventListener('change', fn); else if (q.addListener) q.addListener(fn); };
  const unlisten = (q, fn) => { if (q.removeEventListener) q.removeEventListener('change', fn); else if (q.removeListener) q.removeListener(fn); };

  class PS5CoolingFan extends HTMLElement {
    static get observedAttributes() { return ['speed', 'connected', 'motion', 'label', 'aria-label', 'lite']; }
    constructor() {
      super();
      this.attachShadow({ mode: 'open' });
      this._target = 20; this._display = 0; this._angle = -90; this._last = 0;
      this._frame = 0; this._connected = true; this._alive = false;
      this._frames = 0; this._slow = 0; this._lite = false; this._static = false;
      this._onScreen = true; this._io = null; this._shown = {};
      this._query = window.matchMedia('(prefers-reduced-motion: reduce)');
      this._onPreference = () => this._wake();
      this._onVisibility = () => {
        this._last = 0;
        if (document.hidden) { cancelAnimationFrame(this._frame); this._frame = 0; this._display = this._connected ? this._target : 0; this._paint(); }
        else this._wake();
      };
      this._tick = this._tick.bind(this);
      this.shadowRoot.innerHTML = this._template();
      this._needle = this.shadowRoot.querySelector('.needle');
      this._dim = this.shadowRoot.querySelector('.dim');
      this._visual = this.shadowRoot.querySelector('.assembly');
      this._label = this.shadowRoot.querySelector('.accessible');
      this._paint();
    }
    connectedCallback() {
      this._alive = true;
      listen(this._query, this._onPreference);
      document.addEventListener('visibilitychange', this._onVisibility);
      /* Außerhalb des Bildschirms wird nicht gerechnet: das Scrollen der Seite bleibt flüssig, und beim Zurückkommen läuft er weiter. */
      if (typeof IntersectionObserver === 'function') {
        this._io = new IntersectionObserver(entries => {
          const on = entries[entries.length - 1].isIntersecting;
          if (on === this._onScreen) return;
          this._onScreen = on; this._last = 0;
          if (on) this._wake(); else { cancelAnimationFrame(this._frame); this._frame = 0; this._display = this._connected ? this._target : 0; this._paint(); }
        });
        this._io.observe(this);
      }
      this._wake();
    }
    disconnectedCallback() {
      this._alive = false;
      cancelAnimationFrame(this._frame); this._frame = 0; this._last = 0;
      unlisten(this._query, this._onPreference);
      document.removeEventListener('visibilitychange', this._onVisibility);
      if (this._io) { this._io.disconnect(); this._io = null; }
      this._onScreen = true;
    }
    attributeChangedCallback(name, oldValue, newValue) {
      if (oldValue === newValue) return;
      if (name === 'speed') {
        const value = normalize(newValue);
        if (value !== null) this._target = value;
      }
      if (name === 'connected') this._connected = newValue !== 'false';
      this._paint(); this._wake();
    }
    /** Percentage 0..100; returns false for missing or invalid telemetry. */
    setSpeed(value) {
      const n = normalize(value);
      if (n === null) return false;
      this.setAttribute('speed', String(n));
      return true;
    }
    /** Supply true measured console data here; this never controls hardware. */
    setTelemetry(data) {
      if (!data || typeof data !== 'object' || Array.isArray(data)) return false;
      if ('connected' in data && typeof data.connected !== 'boolean') return false;
      const percent = 'percent' in data ? normalize(data.percent) : undefined;
      if (percent === null) return false;
      // Validate the complete payload before applying either field.
      if (percent !== undefined) this.setSpeed(percent);
      if ('connected' in data) this.setAttribute('connected', String(data.connected));
      return true;
    }
    get speed() { return this._target; }
    set speed(value) { this.setSpeed(value); }
    get telemetryConnected() { return this._connected; }
    get state() {
      return Object.freeze({ target: this._target, displayed: this._display, angle: this._angle,
        revolutionsPerSecond: 0,
        color: this._connected ? colorAt(this._display) : GREY,
        zone: this._connected ? zoneAt(this._target) : 'none',
        connected: this._connected, reducedMotion: this._motionReduced(), lite: this._lite, stopped: this._static });
    }
    _motionReduced() { return this._static || this.getAttribute('motion') === 'reduced' || this.getAttribute('motion') === 'off' || this._query.matches; }
    _wake() {
      if (!this._alive || this._frame) return;
      /* Unsichtbar (Tab im Hintergrund, Kachel außerhalb des Bildes): nicht animieren, aber gleich den Endstand zeigen,
         damit Nadel und Zahl stimmen, wenn man wieder hinsieht. */
      if (document.hidden || !this._onScreen) {
        const desired = this._connected ? this._target : 0;
        if (this._display !== desired) { this._display = desired; this._paint(); }
        return;
      }
      this._frame = requestAnimationFrame(this._tick);
      /* Kommt kein Bild (der Browser hält die Seite an), stehen Nadel und Zahl nach einer Sekunde trotzdem auf dem Endwert. */
      clearTimeout(this._guard);
      this._guard = setTimeout(() => {
        if (!this._frame) return;
        cancelAnimationFrame(this._frame); this._frame = 0; this._last = 0;
        this._display = this._connected ? this._target : 0; this._paint();
      }, 1000);
    }
    /* Frames that take too long: first the heavy light effects go, then the rotation. */
    _watch(rawMs) {
      if (rawMs > 1000) return;                               // a pause (tab hidden, page frozen) is not slowness
      this._frames++;
      if (rawMs > 55) this._slow++;
      if (this._frames >= 45) {
        const slow = this._slow > 30;
        this._frames = 0; this._slow = 0;
        if (slow && !this._lite) { this._lite = true; this._visual.classList.add('lite'); }
        else if (slow && this._lite && this.getAttribute('motion') !== 'full') { this._static = true; }
      }
    }
    _tick(timestamp) {
      this._frame = 0; clearTimeout(this._guard);
      const raw = this._last ? timestamp - this._last : 0;
      const dt = this._last ? Math.min(raw / 1000, 0.08) : 0;
      this._last = timestamp;
      if (raw) this._watch(raw);
      const desired = this._connected ? this._target : 0;
      const tau = desired > this._display ? 1.3 : 1.8;
      if (this._motionReduced()) this._display = desired;
      else {
        this._display += (desired - this._display) * (1 - Math.exp(-dt / tau));
        if (Math.abs(desired - this._display) < 0.03) this._display = desired;
      }
      this._paint();
      if (this._display !== desired) this._wake();
      else this._last = 0;
    }
    _paint() {
      if (!this._visual) return;
      /* Farbe, Leuchtstärke und Klassen nur schreiben, wenn sie sich ändern: jedes Schreiben lässt den Browser die Leuchteffekte
         neu zeichnen, und das verlangsamte an der Konsole das Scrollen der Seite. */
      const s = this._shown, v = this._visual.style;
      const color = (this._connected ? colorAt(this._display) : GREY).join(',');
      if (s.color !== color) { s.color = color; v.setProperty('--fan-rgb', color); }
      const glow = this._connected ? (0.23 + this._display * 0.005).toFixed(3) : '0.08';
      if (s.glow !== glow) { s.glow = glow; v.setProperty('--glow-strength', glow); }
      /* Die Nadel ist ein eigenes <svg> über dem Zifferblatt und wird als Ganzes gedreht (Mitte in Prozent), von -90 Grad (0 %)
         bis +90 Grad (100 %). Hinter ihr wird der Bogen abgedunkelt: eine zweite Grafik, um die Mitte des Bogens gedreht. */
      this._angle = -90 + 1.8 * this._display;
      /* Die Zahl neben dem Tacho läuft mit der Nadel mit: Die Seite hört auf dieses Ereignis (app.js) statt den Endwert sofort zu zeigen. */
      const shownPct = Math.round(this._display);
      if (s.pct !== shownPct) { s.pct = shownPct; this.dispatchEvent(new CustomEvent('fan-display', { detail: { percent: shownPct } })); }
      const turn = `rotate(${this._angle.toFixed(2)}deg)`;
      if (s.turn !== turn) {
        s.turn = turn; this._needle.style.transform = turn;
        this._dim.style.transform = `rotate(${(this._angle + 0.03 * this._display).toFixed(2)}deg)`;
      }
      const off = !this._connected, lite = this._lite || this.getAttribute('lite') === 'true';
      if (s.off !== off) { s.off = off; this._visual.classList.toggle('offline', off); }
      if (s.lite !== lite) { s.lite = lite; this._visual.classList.toggle('lite', lite); }
      const description = this.getAttribute('aria-label') || this.getAttribute('label') || '';
      if (this._label.textContent !== description) this._label.textContent = description;
    }
    _template() {
      return `<style>
        :host { display:block; width:100%; position:relative; contain:layout style; isolation:isolate; }
        :host::before { content:""; display:block; padding-top:68.83%; }
        *{box-sizing:border-box}
        .assembly{position:absolute;top:0;left:0;right:0;bottom:0;--fan-rgb:65,170,255;color:rgb(var(--fan-rgb))}
        img{display:block;position:absolute;top:0;left:0;width:100%;height:100%}
        svg{display:block;width:100%;height:100%;overflow:visible}
        .clip{position:absolute;top:0;left:0;width:100%;height:70.19%;overflow:hidden}
        .dim{position:absolute;top:0;left:0;width:100%;height:142.47%;transform-origin:49.74% 69.43%;transform:rotate(-90deg)}
        .dim path{fill:rgba(7,13,25,.72)}.offline .dim path{fill:rgba(7,13,25,.86)}
        .needle{position:absolute;top:0;left:0;width:100%;height:100%;transform-origin:49.74% 66.23%;transform:rotate(-90deg);will-change:transform}
        .g1{fill:rgba(var(--fan-rgb),.22);stroke:rgba(var(--fan-rgb),.22);stroke-width:26;stroke-linejoin:round}
        .g2{fill:rgba(var(--fan-rgb),.35);stroke:rgba(var(--fan-rgb),.35);stroke-width:12;stroke-linejoin:round}
        .core{fill:rgb(var(--fan-rgb));stroke:rgb(var(--fan-rgb));stroke-width:2;stroke-linejoin:round}
        .hl{fill:none;stroke:rgba(255,255,255,.55);stroke-width:2.4;stroke-linecap:round}
        .offline .hl{opacity:.35}
        .lite .g1,.lite .g2{display:none}
        .accessible{position:absolute;width:1px;height:1px;padding:0;margin:-1px;overflow:hidden;clip:rect(0 0 0 0);white-space:nowrap;border:0}
      </style><div class="assembly" role="img" aria-labelledby="fan-description"><span class="accessible" id="fan-description"></span>
      <img src="/img/gauge-dial.png" alt="" draggable="false">
      <div class="clip"><div class="dim"><svg viewBox="0 0 770 530" aria-hidden="true" focusable="false"><path d="M383.0 56.0 A312 312 0 1 1 350.4 678.3 L353.5 648.5 A282 282 0 1 0 383.0 86.0 Z"/></svg></div></div>
      <div class="needle"><svg viewBox="0 0 770 530" aria-hidden="true" focusable="false">
        <path class="g1" d="M375 285 L381.4 89 L383 83 L384.6 89 L391 285 Z"/><path class="g2" d="M375 285 L381.4 89 L383 83 L384.6 89 L391 285 Z"/><path class="core" d="M375 285 L381.4 89 L383 83 L384.6 89 L391 285 Z"/><path class="hl" d="M383 281 L383 96"/>
      </svg></div></div>`;
    }
  }
  // Testable deterministic mappings, also useful to colour the host application's card.
  window.PS5FanModel = Object.freeze({ normalize, colorAt, zoneAt, GREY });
  if (!customElements.get('ps5-cooling-fan')) customElements.define('ps5-cooling-fan', PS5CoolingFan);
})();
