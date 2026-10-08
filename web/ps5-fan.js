/* PS5-inspired cooling fan · standalone Web Component · no dependencies.
 *
 * Design and code: the user's own demo ("PS5-Luefter-Demo", 08.10.2026), taken over for the web interface of this
 * app. Changes for the console's own browser (an old WebKit): no aspect-ratio, inset, "rgb(r g b / a)" colours or
 * MediaQueryList.addEventListener; the screen-reader text comes from the page (attribute "label", translated there);
 * and a guard against a slow browser: when frames take too long the heavy light effects are switched off ("lite"), and
 * when they still take too long the rotor stops turning (colour and values stay). */
(function () {
  'use strict';
  const clamp = (v, min = 0, max = 100) => Math.min(max, Math.max(min, v));
  const BLUE = [65, 170, 255], GREEN = [71, 222, 145], RED = [255, 85, 67], GREY = [116, 130, 151];
  const isValid = v => (typeof v === 'number' || (typeof v === 'string' && v.trim() !== '')) && Number.isFinite(Number(v));
  const normalize = v => isValid(v) ? clamp(Number(v)) : null;
  function colorAt(percent) {
    const p = clamp(percent);
    const a = p <= 55 ? BLUE : GREEN, b = p <= 55 ? GREEN : RED;
    const t = p <= 55 ? clamp((p - 25) / 30, 0, 1) : clamp((p - 65) / 35, 0, 1);
    return a.map((n, i) => Math.round(n + (b[i] - n) * t));
  }
  // These are intentionally DISPLAY revolutions/s, not physical console RPM.
  const revolutionsAt = p => p <= 0 ? 0 : 0.15 + 0.85 * (clamp(p) / 100);   /* at most 1 rev/s: the 23 blades (15.7 deg apart) must not move further than half a pitch per frame, or the picture jumps and seems to turn unevenly */
  const zoneAt = p => p <= 35 ? 'cool' : p < 80 ? 'normal' : 'high';
  const polar = (r, degrees) => [200 + r * Math.cos(degrees * Math.PI / 180), 200 + r * Math.sin(degrees * Math.PI / 180)];
  const point = p => p.map(n => n.toFixed(2)).join(' ');
  /* a media query's change event, in the old and the new form */
  const listen = (q, fn) => { if (q.addEventListener) q.addEventListener('change', fn); else if (q.addListener) q.addListener(fn); };
  const unlisten = (q, fn) => { if (q.removeEventListener) q.removeEventListener('change', fn); else if (q.removeListener) q.removeListener(fn); };

  class PS5CoolingFan extends HTMLElement {
    static get observedAttributes() { return ['speed', 'connected', 'motion', 'label', 'aria-label', 'lite']; }
    constructor() {
      super();
      this.attachShadow({ mode: 'open' });
      this._target = 20; this._display = 0; this._angle = 0; this._last = 0;
      this._frame = 0; this._connected = true; this._alive = false;
      this._frames = 0; this._slow = 0; this._lite = false; this._static = false;
      this._query = window.matchMedia('(prefers-reduced-motion: reduce)');
      this._onPreference = () => this._wake();
      this._onVisibility = () => {
        this._last = 0;
        if (document.hidden) { cancelAnimationFrame(this._frame); this._frame = 0; }
        else this._wake();
      };
      this._tick = this._tick.bind(this);
      this.shadowRoot.innerHTML = this._template();
      this._rotor = this.shadowRoot.querySelector('.rotor');
      this._visual = this.shadowRoot.querySelector('.assembly');
      this._label = this.shadowRoot.querySelector('.accessible');
      this._paint();
    }
    connectedCallback() {
      this._alive = true;
      listen(this._query, this._onPreference);
      document.addEventListener('visibilitychange', this._onVisibility);
      this._wake();
    }
    disconnectedCallback() {
      this._alive = false;
      cancelAnimationFrame(this._frame); this._frame = 0; this._last = 0;
      unlisten(this._query, this._onPreference);
      document.removeEventListener('visibilitychange', this._onVisibility);
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
        revolutionsPerSecond: this._connected ? revolutionsAt(this._display) : 0,
        color: this._connected ? colorAt(this._display) : GREY,
        zone: this._connected ? zoneAt(this._target) : 'none',
        connected: this._connected, reducedMotion: this._motionReduced(), lite: this._lite, stopped: this._static });
    }
    _motionReduced() { return this._static || this.getAttribute('motion') === 'reduced' || this.getAttribute('motion') === 'off' || this._query.matches; }
    _wake() {
      if (!this._alive || document.hidden || this._frame) return;
      this._frame = requestAnimationFrame(this._tick);
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
      this._frame = 0;
      const raw = this._last ? timestamp - this._last : 0;
      const dt = this._last ? Math.min(raw / 1000, 0.08) : 0;
      this._last = timestamp;
      if (raw) this._watch(raw);
      const desired = this._connected ? this._target : 0;
      const tau = desired > this._display ? 1.3 : 1.8;
      const previous = this._display;
      this._display += (desired - this._display) * (1 - Math.exp(-dt / tau));
      if (Math.abs(desired - this._display) < 0.03) this._display = desired;
      if (!this._motionReduced()) {
        this._angle = (this._angle + revolutionsAt((previous + this._display) / 2) * 360 * dt) % 360;
      }
      this._paint();
      if ((this._display > 0 && !this._motionReduced()) || this._display !== desired) this._wake();
      else this._last = 0;
    }
    _paint() {
      if (!this._visual) return;
      const color = this._connected ? colorAt(this._display) : GREY;
      this._visual.style.setProperty('--fan-rgb', color.join(','));
      this._visual.style.setProperty('--glow-strength', this._connected ? (0.23 + this._display * 0.005).toFixed(3) : '0.08');
      this._rotor.style.transform = `rotate(${this._angle.toFixed(3)}deg)`;
      this._visual.classList.toggle('offline', !this._connected);
      this._visual.classList.toggle('lite', this._lite || this.getAttribute('lite') === 'true');
      const description = this.getAttribute('aria-label') || this.getAttribute('label') || '';
      if (this._label.textContent !== description) this._label.textContent = description;
    }
    _template() {
      const blades = Array.from({ length: 23 }, (_, i) => `<g transform="rotate(${(i * 360 / 23).toFixed(3)} 200 200)"><path d="M 179 145 C 146 129 122 96 119 73 C 129 66 140 61 152 57 C 145 89 158 120 193 140 Z" fill="url(#blade-face)" stroke="#090e16" stroke-width="1.4"/><path d="M 120 73 C 123 99 149 132 179 145" fill="none" stroke="url(#blade-edge)" stroke-width="1.4"/><path d="M 151 59 C 147 88 159 119 189 137" fill="none" stroke="#95a2b6" stroke-opacity=".12" stroke-width=".75"/></g>`).join('');
      const ticks = Array.from({ length: 72 }, (_, i) => {
        const a = polar(169, i * 5), b = polar(i % 6 === 0 ? 176 : 172, i * 5);
        return `<path d="M ${point(a)} L ${point(b)}" stroke="${i % 6 === 0 ? '#8795a9' : '#394558'}" stroke-width="${i % 6 === 0 ? 1.3 : .8}"/>`;
      }).join('');
      const vents = Array.from({ length: 7 }, (_, i) => `<path d="M ${55 + i * 7} ${41 + i * 2.3} l -10 23" stroke="#141b27" stroke-width="3.2" stroke-linecap="round"/>`).join('');
      const hubRings = Array.from({ length: 14 }, (_, i) => `<circle cx="200" cy="200" r="${24 + i * 1.7}" fill="none" stroke="#c5cfdf" stroke-width=".35" opacity=".09"/>`).join('');
      return `<style>
        :host { display:block; width:100%; position:relative; contain:layout style; isolation:isolate; }
        :host::before { content:""; display:block; padding-top:100%; }
        *{box-sizing:border-box} .assembly{position:absolute;top:0;left:0;right:0;bottom:0;--fan-rgb:65,170,255;--glow-strength:.35;color:rgb(var(--fan-rgb))}
        .halo{position:absolute;top:11%;left:11%;right:11%;bottom:11%;border-radius:50%;background:radial-gradient(circle,transparent 48%,rgba(var(--fan-rgb),.075) 65%,transparent 75%);filter:blur(12px);opacity:.9;pointer-events:none}
        svg{display:block;width:100%;height:100%;overflow:visible}.rotor{transform-origin:200px 200px;will-change:transform}.light-ring{filter:drop-shadow(0 0 3px currentColor) drop-shadow(0 0 11px rgba(var(--fan-rgb),var(--glow-strength)))}
        .surface-light{opacity:var(--glow-strength)}.offline .light-ring{opacity:.35}.offline .surface-light{opacity:.08}
        .lite .halo{display:none}.lite .light-ring{filter:none}.lite .surface-light{display:none}
        .accessible{position:absolute;width:1px;height:1px;padding:0;margin:-1px;overflow:hidden;clip:rect(0 0 0 0);white-space:nowrap;border:0}
        @media(prefers-reduced-motion:reduce){.rotor{will-change:auto}}
      </style><div class="assembly" role="img" aria-labelledby="fan-description"><span class="accessible" id="fan-description"></span><div class="halo"></div>
      <svg viewBox="0 0 400 400" aria-hidden="true" focusable="false"><defs>
        <linearGradient id="shell" x1="0" y1="0" x2="1" y2="1"><stop stop-color="#e6ecf3"/><stop offset=".17" stop-color="#929dad"/><stop offset=".38" stop-color="#e2e7ef"/><stop offset=".65" stop-color="#5a6575"/><stop offset="1" stop-color="#b4bfcd"/></linearGradient>
        <linearGradient id="rim" x1="0" y1="0" x2="1" y2="1"><stop stop-color="#8290a3"/><stop offset=".3" stop-color="#1c2635"/><stop offset=".6" stop-color="#0a101a"/><stop offset="1" stop-color="#4f6178"/></linearGradient>
        <linearGradient id="blade-face" x1="119" y1="58" x2="188" y2="145" gradientUnits="userSpaceOnUse"><stop stop-color="#5b6a7a"/><stop offset=".22" stop-color="#303d4b"/><stop offset=".46" stop-color="#1b2532"/><stop offset=".83" stop-color="#0c1420"/><stop offset="1" stop-color="#465361"/></linearGradient>
        <linearGradient id="blade-edge" x1="122" y1="73" x2="180" y2="145" gradientUnits="userSpaceOnUse"><stop stop-color="#d3dce8" stop-opacity=".48"/><stop offset=".45" stop-color="#65819c" stop-opacity=".22"/><stop offset="1" stop-color="#c2cdd9" stop-opacity=".2"/></linearGradient>
        <radialGradient id="well"><stop stop-color="#142032"/><stop offset=".78" stop-color="#0c1320"/><stop offset="1" stop-color="#030710"/></radialGradient>
        <linearGradient id="hub" x1=".15" y1="0" x2=".9" y2="1"><stop stop-color="#c9d3df"/><stop offset=".22" stop-color="#718194"/><stop offset=".45" stop-color="#283646"/><stop offset=".6" stop-color="#56687b"/><stop offset=".8" stop-color="#91a1b2"/><stop offset="1" stop-color="#303e50"/></linearGradient>
        <linearGradient id="light-reflection" x1="0" y1="0" x2="1" y2="1"><stop stop-color="currentColor" stop-opacity=".45"/><stop offset=".5" stop-color="currentColor" stop-opacity="0"/><stop offset="1" stop-color="currentColor" stop-opacity=".3"/></linearGradient>
      </defs>
      <ellipse cx="200" cy="213" rx="172" ry="172" fill="#000" opacity=".3"/>
      <g><path d="M 49 32 L 128 45 Q 155 22 200 22 Q 245 22 273 46 L 347 32 Q 362 33 365 48 L 351 123 Q 376 158 376 200 Q 376 244 352 274 L 365 347 Q 363 363 348 365 L 274 352 Q 242 376 200 376 Q 157 376 126 352 L 50 365 Q 34 363 33 348 L 46 275 Q 22 242 22 200 Q 22 157 46 126 L 33 50 Q 33 35 49 32 Z" fill="url(#shell)" stroke="#d5deeb" stroke-opacity=".32"/>
      <path d="M 50 39 L 121 52 M 279 53 L 346 39 M 359 54 L 346 119 M 54 358 L 122 346" stroke="#fff" stroke-opacity=".4" fill="none"/>
      <g>${vents}</g><g transform="rotate(90 200 200)">${vents}</g><g transform="rotate(180 200 200)">${vents}</g><g transform="rotate(270 200 200)">${vents}</g>
      ${[[52,52],[348,52],[52,348],[348,348]].map(([x,y]) => `<g><circle cx="${x}" cy="${y}" r="8" fill="#475362"/><circle cx="${x}" cy="${y}" r="5.8" fill="#1a2330" stroke="#8d9aac" stroke-width="1"/><path d="M ${x-3} ${y} h 6 M ${x} ${y-3} v 6" stroke="#778596" stroke-width="1.2"/></g>`).join('')}</g>
      <circle cx="200" cy="200" r="174" fill="url(#rim)" stroke="#0c111b" stroke-width="2"/>
      <circle cx="200" cy="200" r="162" fill="url(#well)" stroke="#000" stroke-width="7"/>
      <circle cx="200" cy="200" r="158" fill="none" stroke="#647790" stroke-opacity=".26" stroke-width="1"/>
      <g opacity=".65">${ticks}</g>
      <g class="light-ring" fill="none" stroke="currentColor" stroke-linecap="round"><circle cx="200" cy="200" r="165" stroke-width="1.7" opacity=".28"/><circle cx="200" cy="200" r="165" stroke-width="2.5" stroke-dasharray="170 89 84 176 194 90 55 179" transform="rotate(-57 200 200)"/></g>
      <g class="rotor"><circle cx="200" cy="200" r="150" fill="#0a111c"/>${blades}</g>
      <g class="hub-static" pointer-events="none">
        <circle cx="200" cy="200" r="58" fill="#060d17" stroke="#617187" stroke-opacity=".3" stroke-width="2"/>
        <circle cx="200" cy="200" r="52" fill="url(#hub)" stroke="#aab8c9" stroke-opacity=".4" stroke-width="1"/>${hubRings}
        <circle cx="200" cy="200" r="47" fill="none" stroke="#e4edff" stroke-opacity=".18" stroke-width=".7"/>
        <path d="M 164 182 A 40 40 0 0 1 204 160" stroke="#fff" stroke-opacity=".25" fill="none"/>
        <circle cx="200" cy="200" r="14" fill="#172432" stroke="#9aaabf" stroke-opacity=".45"/>
        <circle cx="200" cy="200" r="4" fill="#6b7b90"/><path d="M 198 197 L 202 203" stroke="#1b293a" stroke-width="1.2"/>
      </g>
      <circle class="surface-light" cx="200" cy="200" r="155" fill="url(#light-reflection)" pointer-events="none"/>
      <path d="M 99 335 Q 195 384 304 337" fill="none" stroke="#e3ebf5" stroke-width="1" stroke-opacity=".4"/>
      <path d="M 119 62 Q 188 27 264 54" fill="none" stroke="#ecf5ff" stroke-width="1" stroke-opacity=".25"/>
      </svg></div>`;
    }
  }
  // Testable deterministic mappings, also useful to colour the host application's card.
  window.PS5FanModel = Object.freeze({ normalize, colorAt, revolutionsAt, zoneAt, GREY });
  if (!customElements.get('ps5-cooling-fan')) customElements.define('ps5-cooling-fan', PS5CoolingFan);
})();
