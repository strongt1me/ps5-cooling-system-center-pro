/* PS5 Temperature Manager — dashboard logic.
 *
 * Reads /api/v1/status once a second and /api/v1/system every ten. Anything
 * the console does not report is left blank rather than filled with a guess:
 * the PS5 has no separate GPU sensor and no GPU load counter, so those simply
 * do not appear.
 */
(() => {
  "use strict";

  const HISTORY = 120;                 /* 2 minutes at one sample per second */
  const W = 720, H = 210;
  const DIAG_WINDOW_KEY = "ps5tm.diagWindowMin";
  const EXPERT_MODE_KEY = "ps5tm.expertMode";
  const FULLSCREEN_KEY = "ps5tm.fullscreen";      /* "0" = nicht automatisch */
  const CHANRAW_CONFIRMED_ONLY_KEY = "ps5tm.chanrawConfirmedOnly";
  const RISKY_AUTO_KEY = "ps5tm.riskyAuto";
  const RISKY_AUTO_INTERVAL_KEY = "ps5tm.riskyAutoIntervalSec";
  const MON_ALERT_ENABLED_KEY = "ps5tm.monitorAlert.enabled";
  const MON_ALERT_WARN_C_KEY = "ps5tm.monitorAlert.warnC";
  const MON_ALERT_HOT_C_KEY = "ps5tm.monitorAlert.hotC";
  const MON_ALERT_FAN_MIN_KEY = "ps5tm.monitorAlert.fanMinPct";
  const FAN_CURVE_TEMP_MIN = 45;
  const FAN_CURVE_TEMP_MAX = 80;
  const FAN_CURVE_MAX_POINTS = 8;
  const FAN_CURVE_DEFAULT = [
    { temperature_c: 45, duty_pct: 0 },
    { temperature_c: 68, duty_pct: 0 },
    { temperature_c: 72, duty_pct: 15 },
    { temperature_c: 76, duty_pct: 35 },
    { temperature_c: 80, duty_pct: 75 }
  ];
  const WEB_PROFILE_FORMAT = "ps5tm-web-profile-v1";
  const MON_ALERT_PRESETS = {
    quiet: { warn_c: 77, hot_c: 82, fan_min_pct: 24 },
    standard: { warn_c: 74, hot_c: 79, fan_min_pct: 28 },
    strict: { warn_c: 71, hot_c: 76, fan_min_pct: 32 }
  };

  const state = {
    page: "cooling",
    cfg: null,
    status: null,
    hist: [],                          /* {cpu, soc, fan, t} */
    fanDiagHist: [],                   /* {target, measured} for 60s sparkline */
    diagEvents: [],                    /* {ts, tag, title, detail} */
    lastAmpelLevel: null,
    lastFanDiagLevel: null,
    diagWindowMin: 2,
    expertMode: false,
    chanrawConfirmedOnly: false,
    riskyAutoOn: false,
    riskyAutoIntervalSec: 15,
    riskyAutoTimer: null,
    riskyBusy: false,
    probeDiagSession: null,
    probeDiagTickTimer: null,
    probeDiagBusy: 0,                  /* writes of the page's own in flight */
    monitorAlertEnabled: false,
    monitorWarnC: 74,
    monitorHotC: 79,
    monitorFanMinPct: 28,
    monitorAlertLevel: null,
    monitorAlertEvents: [],
    powerHist: [],                    /* {ts, uptimeSec, idleSec, mode, socPowerW|null} */
    logTailOn: false,
    logTailTimer: null,
    logTailBusy: false,
    toastTimer: null,
    targetDirty: false
  };

  /* Zahlen und Zeiten richten sich nach der gewählten Sprache (i18n-boot.js setzt PS5_LOCALE, z. B. "en-GB"). */
  const LOCALE = window.PS5_LOCALE || "de-DE";

  const $  = (s) => document.querySelector(s);
  const $$ = (s) => [...document.querySelectorAll(s)];
  const txt = (sel, v) => { const n = $(sel); if (n) n.textContent = v; };

  /* ── Hilfen ─────────────────────────────────────────────────────── */

  const num = (v, digits = 0) =>
    Number(v).toLocaleString(LOCALE, { minimumFractionDigits: digits,
                                        maximumFractionDigits: digits });

  const esc = (s) => String(s)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/\"/g, "&quot;")
    .replace(/'/g, "&#39;");

  const bytes = (b) => {
    if (!b || b < 0) return "--";
    const u = ["B", "KB", "MB", "GB", "TB"];
    let i = 0, v = b;
    while (v >= 1024 && i < u.length - 1) { v /= 1024; i++; }
    return `${num(v, v < 10 && i > 2 ? 1 : 0)} ${u[i]}`;
  };

  const duration = (sec) => {
    if (!sec || sec < 0) return "--";
    const d = Math.floor(sec / 86400);
    const h = Math.floor((sec % 86400) / 3600);
    const m = Math.floor((sec % 3600) / 60);
    if (d) return `${d} Tage, ${h} Std.`;
    if (h) return `${h} Std. ${m} Min.`;
    return `${m} Min.`;
  };

  const offsetToHm = (mins) => {
    const m = Number(mins);
    if (!Number.isFinite(m)) return "--";
    const sign = m >= 0 ? "+" : "-";
    const a = Math.abs(Math.trunc(m));
    const h = Math.floor(a / 60);
    const mm = a % 60;
    return `UTC${sign}${String(h).padStart(2, "0")}:${String(mm).padStart(2, "0")}`;
  };

  /* Locale-Codes bleiben die Primärquelle. Diese Tabellen liefern nur
     vorsichtige UI-Hinweise, falls ein bekannter Wert auftaucht. */
  const LOCALE_LANGUAGE_HINTS = new Map([
    [0, "Japanisch"],
    [1, "Englisch (USA)"],
    [2, "Französisch"],
    [3, "Spanisch"],
    [4, "Deutsch"],
    [5, "Italienisch"],
    [6, "Niederländisch"],
    [7, "Portugiesisch"],
    [8, "Russisch"],
    [9, "Koreanisch"],
    [10, "Chinesisch (trad.)"],
    [11, "Chinesisch (vereinf.)"],
    [12, "Finnisch"],
    [13, "Schwedisch"],
    [14, "Dänisch"],
    [15, "Norwegisch"],
    [16, "Polnisch"],
    [17, "Portugiesisch (Brasilien)"],
    [18, "Englisch (UK)"],
    [19, "Türkisch"],
    [20, "Spanisch (Lateinamerika)"],
    [21, "Arabisch"],
    [22, "Französisch (Kanada)"],
    [23, "Tschechisch"],
    [24, "Ungarisch"],
    [25, "Griechisch"],
    [26, "Rumänisch"],
    [27, "Thai"],
    [28, "Vietnamesisch"],
    [29, "Indonesisch"]
  ]);

  /* Für die Region gibt es hier bewusst keine Deutungstabelle mehr.
   *
   * Es stand eine da, mit den Schlüsseln 0/1/2 für Japan/Nordamerika/Europa.
   * Sie konnte nie greifen: die Konsole liefert die Region als kurzen Text
   * aus der Registry, und `Number("...")` darauf ergibt NaN. Die Zeile
   * "Region (Best-Effort)" wurde deshalb seit ihrer Einführung kein einziges
   * Mal gezeichnet — der Fehler fiel nur niemandem auf, weil eine fehlende
   * Zeile aussieht wie ein fehlender Wert.
   *
   * Statt die Tabelle auf Text umzustellen und dabei zu raten, welcher Text
   * für welche Region steht, zeigt die Oberfläche jetzt den Rohwert. Was die
   * Konsole wirklich hineinschreibt, ist unbelegt; und ein falscher Name lädt
   * zu Entscheidungen ein, ein fehlender nicht. */

  const localeHint = (map, code) => {
    const n = Number(code);
    if (!Number.isFinite(n)) return "";
    return map.get(Math.trunc(n)) || "";
  };

  const toast = (msg, kind) => {
    const el = $("#toast");
    el.textContent = msg;
    el.classList.toggle("err", kind === "error");
    el.classList.add("show");
    clearTimeout(state.toastTimer);
    state.toastTimer = setTimeout(() => el.classList.remove("show"), 3800);
  };

  /* Ein offenes Fenster (jedes .gm-overlay) hält die Seite dahinter fest (Wunsch vom 06.10.2026): Der rechte Stick
     scrollte im PS5-Browser die abgedunkelte Seite mit. html.modal-open sperrt ihr Scrollen; ein Rad- oder Stick-
     Ereignis außerhalb des Fensterinhalts wird verworfen, im Fenster scrollt nur das Fenster. */
  const modalOpen = () => !!document.querySelector(".gm-overlay:not([hidden])");
  const syncModal = () => document.documentElement.classList.toggle("modal-open", modalOpen());
  new MutationObserver(syncModal).observe(document.body, { childList: true, subtree: true, attributes: true, attributeFilter: ["hidden"] });
  document.addEventListener("wheel", (e) => {
    if (!modalOpen()) return;
    /* erlaubt, wenn unter dem Zeiger etwas im Fenster selbst scrollen kann (das Fenster oder ein Textfeld darin) */
    for (let el = e.target; el && el !== document.body; el = el.parentElement) {
      if (el.scrollHeight > el.clientHeight + 1 && /(auto|scroll)/.test(getComputedStyle(el).overflowY) && el.closest(".gm-overlay")) return;
    }
    e.preventDefault();
  }, { passive: false });

  /* Credits: Die Links zu GitHub sind nur anklickbar, wenn dieser Browser GitHub auch erreicht (Wunsch vom
     06.10.2026: auf der PS5 nur mit Internet, nicht schon mit Netzwerk). Bis das feststeht, sind sie Text. Geprüft
     wird beim Öffnen der Seite mit dem kleinen Symbolbild von github.com (die einzige fremde Adresse, die die
     Sicherheitsregeln der Seite erlauben, und nur für Bilder); die App selbst geht dafür nicht ins Internet. */
  const crLinks = () => $$("#page-credits a");
  crLinks().forEach((a) => {
    a.dataset.href = a.getAttribute("href");
    a.removeAttribute("href");
    a.classList.add("cr-off");
  });
  const crSet = (online) => {
    crLinks().forEach((a) => {
      if (online) { a.setAttribute("href", a.dataset.href); a.classList.remove("cr-off"); }
      else { a.removeAttribute("href"); a.classList.add("cr-off"); }
    });
    txt("#cr-net", online ? "Internet erreichbar: Die Links zu GitHub lassen sich anklicken."
      : "Keine Internetverbindung: Die Links zu GitHub sind deshalb nicht anklickbar.");
  };
  let crProbing = false;
  const crProbe = () => {
    if (crProbing) return;
    crProbing = true;
    txt("#cr-net", "Prüfe, ob GitHub erreichbar ist …");
    const img = new Image();
    let done = false;
    const end = (ok) => { if (done) return; done = true; crProbing = false; clearTimeout(t); crSet(ok); };
    const t = setTimeout(() => end(false), 6000);
    img.onload = () => end(true);
    img.onerror = () => end(false);
    img.src = `https://github.com/favicon.ico?t=${Date.now()}`;
  };

  const loadDiagWindowMin = () => {
    try {
      const raw = localStorage.getItem(DIAG_WINDOW_KEY);
      const v = Number(raw);
      return [2, 5, 10].includes(v) ? v : 2;
    } catch {
      return 2;
    }
  };

  const saveDiagWindowMin = (v) => {
    try { localStorage.setItem(DIAG_WINDOW_KEY, String(v)); } catch {}
  };

  const loadExpertMode = () => {
    try {
      return localStorage.getItem(EXPERT_MODE_KEY) === "1";
    } catch {
      return false;
    }
  };

  const saveExpertMode = (on) => {
    try { localStorage.setItem(EXPERT_MODE_KEY, on ? "1" : "0"); } catch {}
  };

  const loadChanrawConfirmedOnly = () => {
    try {
      return localStorage.getItem(CHANRAW_CONFIRMED_ONLY_KEY) === "1";
    } catch {
      return false;
    }
  };

  const saveChanrawConfirmedOnly = (on) => {
    try { localStorage.setItem(CHANRAW_CONFIRMED_ONLY_KEY, on ? "1" : "0"); } catch {}
  };

  const loadRiskyAutoOn = () => {
    try {
      return localStorage.getItem(RISKY_AUTO_KEY) === "1";
    } catch {
      return false;
    }
  };

  const saveRiskyAutoOn = (on) => {
    try { localStorage.setItem(RISKY_AUTO_KEY, on ? "1" : "0"); } catch {}
  };

  const loadRiskyAutoIntervalSec = () => {
    try {
      const v = Number(localStorage.getItem(RISKY_AUTO_INTERVAL_KEY));
      return [10, 15, 30, 60].includes(v) ? v : 15;
    } catch {
      return 15;
    }
  };

  const saveRiskyAutoIntervalSec = (sec) => {
    try { localStorage.setItem(RISKY_AUTO_INTERVAL_KEY, String(sec)); } catch {}
  };

  const loadMonitorBool = (key, def = false) => {
    try {
      const raw = localStorage.getItem(key);
      if (raw === "1") return true;
      if (raw === "0") return false;
    } catch {}
    return def;
  };

  const saveMonitorBool = (key, val) => {
    try { localStorage.setItem(key, val ? "1" : "0"); } catch {}
  };

  const loadMonitorNum = (key, def, min, max) => {
    try {
      const v = Number(localStorage.getItem(key));
      if (Number.isFinite(v)) return Math.max(min, Math.min(max, Math.round(v)));
    } catch {}
    return def;
  };

  const saveMonitorNum = (key, val) => {
    try { localStorage.setItem(key, String(val)); } catch {}
  };

  const monitorLevelBadge = (lvl) => lvl === "hot"
    ? "<span class=\"badge hot\">KRITISCH</span>"
    : lvl === "warn"
      ? "<span class=\"badge warn\">WARNUNG</span>"
      : "<span class=\"badge ok\">OK</span>";

  const safeNum = (v) => {
    const n = Number(v);
    return Number.isFinite(n) ? n : null;
  };

  /* Unlike safeNum, an absent field stays absent: Number(null) would be 0. */
  const finiteOrNull = (v) => (typeof v === "number" && Number.isFinite(v) ? v : null);

  const clampInt = (v, min, max, fallback) => {
    const n = Number(v);
    if (!Number.isFinite(n)) return fallback;
    return Math.max(min, Math.min(max, Math.round(n)));
  };

  const normalizeCurvePoints = (points) => {
    const base = Array.isArray(points) ? points : [];
    const parsed = [];
    for (const p of base) {
      if (!p) continue;
      const t = clampInt(p.temperature_c, FAN_CURVE_TEMP_MIN, FAN_CURVE_TEMP_MAX, NaN);
      const d = clampInt(p.duty_pct, 0, 100, NaN);
      if (!Number.isFinite(t) || !Number.isFinite(d)) continue;
      parsed.push({ temperature_c: t, duty_pct: d });
    }

    const src = parsed.length >= 2 ? parsed : FAN_CURVE_DEFAULT.map((p) => ({ ...p }));
    src.sort((a, b) => a.temperature_c - b.temperature_c);

    const merged = [];
    for (const p of src) {
      const last = merged[merged.length - 1];
      if (last && last.temperature_c === p.temperature_c) {
        last.duty_pct = p.duty_pct;
      } else {
        merged.push({ ...p });
      }
    }

    if (merged.length < 2) return FAN_CURVE_DEFAULT.map((p) => ({ ...p }));
    return merged.slice(0, FAN_CURVE_MAX_POINTS);
  };

  /* Rows in screen order, exactly as typed. The "Entfernen" index refers to
     the row that was clicked, not to the sorted and merged list that goes to
     the console: after retyping a temperature the two no longer match. */
  const readCurveEditorRows = () => $$("#fan-curve-list [data-curve-row]").map((row) => {
    const t = row.querySelector(".fan-curve-temp");
    const d = row.querySelector(".fan-curve-duty");
    return {
      temperature_c: Number(t && t.value),
      duty_pct: Number(d && d.value)
    };
  });

  const readCurveEditorPoints = () => normalizeCurvePoints(readCurveEditorRows());

  const renderCurveEditor = (points) => {
    const host = $("#fan-curve-list");
    if (!host) return;
    const normalized = normalizeCurvePoints(points);
    host.innerHTML = normalized.map((p, idx) => `
      <div class="row-gap mt-10" data-curve-row>
        <label><span>Punkt ${idx + 1} · Temperatur (°C)</span>
          <input class="fan-curve-temp" type="number" min="${FAN_CURVE_TEMP_MIN}" max="${FAN_CURVE_TEMP_MAX}" value="${p.temperature_c}">
        </label>
        <label><span>Punkt ${idx + 1} · Ziel-Lüfter (%)</span>
          <input class="fan-curve-duty" type="number" min="0" max="100" value="${p.duty_pct}">
        </label>
        <button class="btn ghost" data-curve-remove="${idx}" ${normalized.length <= 2 ? "disabled" : ""}>Entfernen</button>
      </div>`).join("");

    $$("#fan-curve-list [data-curve-remove]").forEach((btn) => {
      btn.addEventListener("click", () => {
        const idx = Number(btn.getAttribute("data-curve-remove"));
        const cur = readCurveEditorRows();
        if (!Number.isFinite(idx) || idx < 0 || idx >= cur.length) return;
        cur.splice(idx, 1);
        if (cur.length < 2) {
          toast("Eine Lüfterkurve braucht mindestens 2 Punkte.", "error");
          return;
        }
        renderCurveEditor(cur);
      });
    });
  };

  const fmtDelta = (cur, ref, unit = "") => {
    const a = safeNum(cur);
    const b = safeNum(ref);
    if (a === null || b === null) return "--";
    const d = a - b;
    return `${d >= 0 ? "+" : ""}${num(d, 1)}${unit}`;
  };

  const capturePowerHistory = (sys, status) => {
    if (!sys) return;
    const now = Date.now();
    const pw = sys.power_state || {};
    const load = status && status.load ? status.load : {};
    const sample = {
      ts: now,
      uptimeSec: Number.isFinite(sys.uptime_sec) ? Number(sys.uptime_sec) : null,
      idleSec: Number.isFinite(pw.idle_s) ? Number(pw.idle_s) : null,
      mode: pw.mode || "--",
      /* Der Status liefert soc_power_w nur, wenn der Wert gültig ist — ein
         Feld soc_power_valid gibt es dort nicht, die alte Abfrage darauf blieb
         deshalb immer leer. Seit 1.45.0 zählt die Summe der Stromschienen. */
      socPowerW: status && status.power && status.power.valid && Number.isFinite(status.power.live_w)
        ? Number(status.power.live_w)
        : (Number.isFinite(load.soc_power_w) ? Number(load.soc_power_w) : null)
    };

    const last = state.powerHist[state.powerHist.length - 1];
    if (last && sample.uptimeSec !== null && last.uptimeSec === sample.uptimeSec)
      return;

    state.powerHist.push(sample);
    if (state.powerHist.length > 180) state.powerHist.shift();
  };

  const renderPowerHistory = () => {
    const list = $("#power-hist-list");
    if (!list) return;
    const data = state.powerHist;

    txt("#power-hist-count", String(data.length));
    const powers = data.map((s) => s.socPowerW).filter((v) => Number.isFinite(v));
    if (!powers.length) {
      txt("#power-hist-now", "--");
      txt("#power-hist-avg", "--");
      txt("#power-hist-max", "--");
    } else {
      const now = powers[powers.length - 1];
      const avg = powers.reduce((a, b) => a + b, 0) / powers.length;
      const max = Math.max(...powers);
      txt("#power-hist-now", `${num(now, 1)} W`);
      txt("#power-hist-avg", `${num(avg, 1)} W`);
      txt("#power-hist-max", `${num(max, 1)} W`);
    }

    if (!data.length) {
      list.innerHTML = `<p class="muted">noch keine Daten erfasst</p>`;
      return;
    }

    const rows = data.slice(-10).reverse().map((s) => {
      const stamp = new Date(s.ts).toLocaleTimeString(LOCALE);
      const up = s.uptimeSec === null ? "--" : duration(s.uptimeSec);
      const idle = s.idleSec === null ? "--" : (s.idleSec >= 60
        ? `${Math.round(s.idleSec / 60)} Min.`
        : `${s.idleSec} s`);
      const soc = s.socPowerW === null ? "--" : `${num(s.socPowerW, 1)} W`;
      return `<div><span>${esc(stamp)} · ${esc(s.mode)}</span><b>SoC ${esc(soc)} · Laufzeit ${esc(up)} · Idle ${esc(idle)}</b></div>`;
    });
    list.innerHTML = rows.join("");
  };

  const renderDiagMaster = (sys, drive, driveErr, driveOff) => {
    const card = $("#diag-master-card");
    const list = $("#diag-master-list");
    if (!card || !list) return;

    if (!state.expertMode) {
      card.classList.add("is-hidden");
      return;
    }
    card.classList.remove("is-hidden");

    const s = state.status || {};
    const t = s.temperatures || {};
    const f = s.fan || {};
    const c = s.control || {};
    const adapters = s.adapters || {};
    const findings = [];

    const addFinding = (level, title, detail, action) => {
      findings.push({ level, title, detail, action });
    };

    const hottest = Number(t.hottest_c);
    const target = Number.isFinite(c.effective_target_c)
      ? Number(c.effective_target_c)
      : Number(c.target_temp_c);
    const measuredFan = f.measured_valid ? Number(f.measured_duty_pct) : null;
    const targetFan = Number.isFinite(f.target_duty_pct) ? Number(f.target_duty_pct) : null;
    const deltaFan = (measuredFan !== null && targetFan !== null)
      ? measuredFan - targetFan
      : null;

    if (c.safety_active) {
      addFinding(
        "hot",
        "Sicherheitsmodus aktiv",
        `Temperaturregelung ist im Schutzpfad (${Number.isFinite(hottest) ? `${num(hottest, 1)} °C` : "--"}).`,
        "Last reduzieren, Luftwege prüfen, danach Verlauf beobachten."
      );
    }

    if (Number.isFinite(hottest) && Number.isFinite(target) && hottest >= target + 6) {
      addFinding(
        "warn",
        "Wärmelage über Ziel",
        `Wärmster Sensor liegt ${num(hottest - target, 1)} °C über dem Ziel (${num(target, 1)} °C).`,
        "Kurven-/Zielwert prüfen oder Profil vorübergehend auf Ausgewogen/Kühl stellen."
      );
    }

    if (adapters.sensors !== "ready") {
      addFinding(
        "hot",
        "Sensoradapter nicht bereit",
        "Temperaturdaten sind unvollständig oder fehlen.",
        "Payload/Jailbreak-Umgebung prüfen und Sensorpfad erneut testen."
      );
    }

    if (!f.available || adapters.fan !== "ready") {
      addFinding(
        "hot",
        "Lüfteradapter nicht bereit",
        "Direkte Lüftersteuerung ist aktuell nicht verfügbar.",
        "ICC-Zugriff und Rechte prüfen, dann Direktwert testweise setzen."
      );
    }

    if (deltaFan !== null && Math.abs(deltaFan) >= 15) {
      addFinding(
        "warn",
        "Soll-/Ist-Abweichung Lüfter",
        `Abweichung aktuell ${num(deltaFan, 1)} % (Soll ${num(targetFan, 1)} %, Ist ${num(measuredFan, 1)} %).`,
        "Kurz weiter beobachten; bei Dauerabweichung Reapply-Intervall und Kurve prüfen."
      );
    }

    if (sys && Number.isFinite(sys.uptime_sec) && sys.uptime_sec < 180) {
      addFinding(
        "info",
        "Frisch gestartete Sitzung",
        `Uptime nur ${duration(sys.uptime_sec)}. Trends sind noch nicht stabil.`,
        "Diagnosen nach einigen Minuten Last erneut vergleichen."
      );
    }

    /* Switched off in the settings is a state, not a fault: it is the default,
       and a default must not colour the overall status. */
    if (driveOff) {
      addFinding(
        "info",
        "Laufwerkstelemetrie ausgeschaltet",
        "Die optionale Zusatzabfrage für Laufwerke ist aus.",
        "Bei Bedarf unter Zusatzabfragen einschalten."
      );
    } else if (driveErr) {
      addFinding(
        "warn",
        "Laufwerkstelemetrie eingeschränkt",
        driveErr,
        "Zusatzabfrage Laufwerke aktivieren oder Zugriffspfad prüfen."
      );
    }
    /* Der Hinweis „M.2 als Proxy-Sensor" ist mit 1.45.1 entfallen: SoC-Kanal
       2 misst den Hauptchip, nicht die SSD (Messung 27.09.2026). */

    /* Hints alone do not make the status worse, so they must not hide the
       "all clear" line either. */
    if (!findings.some((fnd) => fnd.level === "hot" || fnd.level === "warn")) {
      addFinding(
        "ok",
        "Keine akuten Auffälligkeiten",
        "Alle aktuell verfügbaren Kernindikatoren liegen im erwarteten Bereich.",
        "Regelbetrieb fortsetzen und Langzeittrend im Blick behalten."
      );
    }

    const order = { hot: 0, warn: 1, info: 2, ok: 3 };
    findings.sort((a, b) => order[a.level] - order[b.level]);

    const hot = findings.filter((fnd) => fnd.level === "hot").length;
    const warn = findings.filter((fnd) => fnd.level === "warn").length;
    const info = findings.filter((fnd) => fnd.level === "info").length;

    txt("#diag-master-hot", String(hot));
    txt("#diag-master-warn", String(warn));
    txt("#diag-master-info", String(info));
    txt("#diag-master-status", hot ? "KRITISCH" : warn ? "WARNUNG" : "OK");

    list.innerHTML = findings.map((fnd) => {
      const badge = fnd.level === "hot"
        ? "<span class=\"badge hot\">KRITISCH</span>"
        : fnd.level === "warn"
          ? "<span class=\"badge warn\">WARNUNG</span>"
          : fnd.level === "ok"
            ? "<span class=\"badge ok\">OK</span>"
            : "<span class=\"badge\">INFO</span>";
      return `<div><span>${badge} ${esc(fnd.title)}</span><b class=\"multiline\">${esc(fnd.detail)}\nMaßnahme: ${esc(fnd.action)}</b></div>`;
    }).join("");
  };

  const resolveSnapshotData = (section) => {
    if (!section) return null;
    if (section.ok && section.data) return section.data;
    return section;
  };

  const buildSnapshotObject = async () => {
    const includeRisky = isRiskyProbeEnabled();
    const [st, sys, ch, dg, dr, rs] = await Promise.all([
      apiSafe("/api/v1/status"),
      apiSafe("/api/v1/system"),
      apiSafe("/api/v1/channels"),
      apiSafe("/api/v1/controller/diag"),
      apiSafe("/api/v1/drives"),
      includeRisky ? apiSafe("/api/v1/sensors/risky")
                   : Promise.resolve({ ok: false, error: "Risikotelemetrie ist ausgeschaltet" })
    ]);
    return {
      ok: true,
      collected_at_iso: new Date().toISOString(),
      collected_at_local: new Date().toLocaleString(LOCALE),
      source: "PS5 Cooling & System Center - Pro WebUI",
      ui_state: {
        page: state.page,
        expert_mode: !!state.expertMode,
        chanraw_confirmed_only: !!state.chanrawConfirmedOnly,
        risky_auto_on: !!state.riskyAutoOn,
        risky_auto_interval_sec: Number(state.riskyAutoIntervalSec) || 15,
        monitor_alert: {
          enabled: !!state.monitorAlertEnabled,
          warn_c: state.monitorWarnC,
          hot_c: state.monitorHotC,
          fan_min_pct: state.monitorFanMinPct,
          level: state.monitorAlertLevel || "ok",
          events: state.monitorAlertEvents.slice(0, 16)
        },
        diag_events: state.diagEvents.slice(0, 16)
      },
      api: {
        status: st,
        system: sys,
        channels: ch,
        controller_diag: dg,
        drives: dr,
        risky: rs
      }
    };
  };

  const buildWebProfileObject = async () => {
    const c = state.cfg || await api("/api/v1/config");
    return {
      ok: true,
      format: WEB_PROFILE_FORMAT,
      exported_at_iso: new Date().toISOString(),
      profile: {
        backend: {
          mode: c.mode,
          profile: c.profile,
          target_temp_c: c.target_temp_c,
          fan_threshold_c: c.fan_threshold_c,
          fan_reapply_sec: c.fan_reapply_sec,
          warning_cpu_c: c.warning_cpu_c,
          safety_temp_c: c.safety_temp_c,
          /* The mask the console returns to, not a temporary diagnosis one. */
          probe_mask: finiteOrNull(c.probe_revert_mask) ?? c.probe_mask,
          curve: normalizeCurvePoints(c.curve)
        },
        browser: {
          expert_mode: !!state.expertMode,
          chanraw_confirmed_only: !!state.chanrawConfirmedOnly,
          risky_auto_on: !!state.riskyAutoOn,
          risky_auto_interval_sec: clampInt(state.riskyAutoIntervalSec, 10, 60, 15),
          diag_window_min: [2, 5, 10].includes(Number(state.diagWindowMin))
            ? Number(state.diagWindowMin)
            : 2,
          monitor_alert: {
            enabled: !!state.monitorAlertEnabled,
            warn_c: clampInt(state.monitorWarnC, 55, 95, 74),
            hot_c: clampInt(state.monitorHotC, 56, 98, 79),
            fan_min_pct: clampInt(state.monitorFanMinPct, 15, 95, 28)
          }
        }
      }
    };
  };

  const applyWebProfileBrowser = (browser) => {
    if (!browser || typeof browser !== "object") return;

    if (browser.expert_mode !== undefined) {
      state.expertMode = !!browser.expert_mode;
      saveExpertMode(state.expertMode);
      /* Beide Schalter und die Seitenklasse, wie applyExpertMode() weiter
         unten — hier von Hand, weil die Funktion erst später definiert ist. */
      ["#expert-mode", "#expert-mode-top"].forEach((sel) => {
        const el = $(sel);
        if (el) el.checked = state.expertMode;
      });
      document.body.classList.toggle("expert", state.expertMode);
    }

    if (browser.chanraw_confirmed_only !== undefined) {
      state.chanrawConfirmedOnly = !!browser.chanraw_confirmed_only;
      saveChanrawConfirmedOnly(state.chanrawConfirmedOnly);
      const el = $("#chanraw-confirmed-only");
      if (el) el.checked = state.chanrawConfirmedOnly;
    }

    if (browser.risky_auto_on !== undefined) {
      state.riskyAutoOn = !!browser.risky_auto_on;
      saveRiskyAutoOn(state.riskyAutoOn);
      const el = $("#risky-auto-toggle");
      if (el) el.checked = state.riskyAutoOn;
    }

    if (browser.risky_auto_interval_sec !== undefined) {
      const interval = [10, 15, 30, 60].includes(Number(browser.risky_auto_interval_sec))
        ? Number(browser.risky_auto_interval_sec)
        : 15;
      state.riskyAutoIntervalSec = interval;
      saveRiskyAutoIntervalSec(interval);
      const el = $("#risky-auto-interval");
      if (el) el.value = String(interval);
    }

    if (browser.diag_window_min !== undefined) {
      const mins = [2, 5, 10].includes(Number(browser.diag_window_min))
        ? Number(browser.diag_window_min)
        : 2;
      state.diagWindowMin = mins;
      saveDiagWindowMin(mins);
      const box = $("#diag-window");
      if (box) {
        box.querySelectorAll("button[data-minutes]")
          .forEach((b) => b.classList.toggle("active",
            Number(b.getAttribute("data-minutes")) === mins));
      }
    }

    if (browser.monitor_alert && typeof browser.monitor_alert === "object") {
      const m = browser.monitor_alert;
      state.monitorAlertEnabled = !!m.enabled;
      state.monitorWarnC = clampInt(m.warn_c, 55, 95, 74);
      state.monitorHotC = Math.max(state.monitorWarnC + 1,
        clampInt(m.hot_c, 56, 98, 79));
      state.monitorFanMinPct = clampInt(m.fan_min_pct, 15, 95, 28);

      saveMonitorBool(MON_ALERT_ENABLED_KEY, state.monitorAlertEnabled);
      saveMonitorNum(MON_ALERT_WARN_C_KEY, state.monitorWarnC);
      saveMonitorNum(MON_ALERT_HOT_C_KEY, state.monitorHotC);
      saveMonitorNum(MON_ALERT_FAN_MIN_KEY, state.monitorFanMinPct);

      const en = $("#mon-alert-enable");
      const warn = $("#mon-alert-warn");
      const hot = $("#mon-alert-hot");
      const fanMin = $("#mon-alert-fanmin");
      if (en) en.checked = state.monitorAlertEnabled;
      if (warn) warn.value = String(state.monitorWarnC);
      if (hot) hot.value = String(state.monitorHotC);
      if (fanMin) fanMin.value = String(state.monitorFanMinPct);
      state.monitorAlertLevel = null;
      renderMonitorAlert();
    }

    restartRiskyAutoTimer();
    renderDiagEvents(true);
  };

  const renderSnapshotCompare = (refSnap, curSnap) => {
    const host = $("#snapshot-compare-result");
    if (!host) return;

    const classifyHealthLevel = (status) => {
      if (!status) return "unknown";
      const t = status.temperatures || {};
      const f = status.fan || {};
      const c = status.control || {};
      const adapters = status.adapters || {};
      const hotNow = Number(t.hottest_c);
      const targetRef = Number.isFinite(c.effective_target_c)
        ? c.effective_target_c : c.target_temp_c;
      const hasWarn = !!(status.warnings && (status.warnings.cpu || status.warnings.soc));

      if (c.safety_active || hasWarn) return "hot";
      if (adapters.sensors !== "ready" || !f.available) return "warn";
      if (Number.isFinite(hotNow) && Number.isFinite(targetRef) && hotNow >= targetRef + 5)
        return "warn";
      return "ok";
    };

    const healthLabel = (lvl) => lvl === "hot" ? "ROT" : lvl === "warn" ? "GELB"
      : lvl === "ok" ? "GRÜN" : "unbekannt";
    const healthRank = (lvl) => lvl === "hot" ? 2 : lvl === "warn" ? 1 : lvl === "ok" ? 0 : -1;

    const rs = resolveSnapshotData(refSnap && refSnap.api && refSnap.api.status);
    const cs = resolveSnapshotData(curSnap && curSnap.api && curSnap.api.status);
    const ry = resolveSnapshotData(refSnap && refSnap.api && refSnap.api.risky);
    const cy = resolveSnapshotData(curSnap && curSnap.api && curSnap.api.risky);
    const rSys = resolveSnapshotData(refSnap && refSnap.api && refSnap.api.system);
    const cSys = resolveSnapshotData(curSnap && curSnap.api && curSnap.api.system);

    if (!rs || !cs) {
      host.innerHTML = `<p class="muted">Vergleich fehlgeschlagen: Snapshot enthält keine Statusdaten.</p>`;
      return;
    }

    const rt = rs.temperatures || {};
    const ct = cs.temperatures || {};
    const rf = rs.fan || {};
    const cf = cs.fan || {};
    const rr = ry && ry.load ? ry.load : {};
    const cr = cy && cy.load ? cy.load : {};

    const refLevel = classifyHealthLevel(rs);
    const curLevel = classifyHealthLevel(cs);
    const dir = healthRank(curLevel) > healthRank(refLevel)
      ? "verschlechtert"
      : healthRank(curLevel) < healthRank(refLevel)
        ? "verbessert"
        : "stabil";

    const rows = [
      ["Referenz", refSnap.collected_at_local || refSnap.collected_at_iso || "--"],
      ["Aktuell", curSnap.collected_at_local || curSnap.collected_at_iso || "--"],
      ["Ampel-Diff", `${healthLabel(refLevel)} -> ${healthLabel(curLevel)} (${dir})`],
      ["Wärmster Sensor", `${num(ct.hottest_c ?? -1, 1)} °C (Delta ${fmtDelta(ct.hottest_c, rt.hottest_c, " °C")})`],
      ["CPU", `${num(ct.cpu_c ?? -1, 1)} °C (Delta ${fmtDelta(ct.cpu_c, rt.cpu_c, " °C")})`],
      ["SoC", `${num(ct.soc_c ?? -1, 1)} °C (Delta ${fmtDelta(ct.soc_c, rt.soc_c, " °C")})`],
      /* Seit 1.45.1, nur auf der PS5 Pro. undefined statt null für „fehlt":
         safeNum(null) ergäbe 0, und ein Snapshot von 1.45.0 hätte dann ein
         Delta von +40 °C geliefert. */
      ...(ct.gpu_valid || rt.gpu_valid ? [[
        "Grafik",
        `${ct.gpu_valid ? `${num(ct.gpu_c, 1)} °C` : "--"} (Delta ${fmtDelta(
          ct.gpu_valid ? ct.gpu_c : undefined, rt.gpu_valid ? rt.gpu_c : undefined, " °C")})`
      ]] : []),
      ["Lüfter Ist", `${cf.measured_valid ? `${num(cf.measured_duty_pct, 1)} %` : "--"} (Delta ${fmtDelta(cf.measured_duty_pct, rf.measured_duty_pct, " %")})`],
      ["Lüfter Soll", `${num(cf.target_duty_pct ?? -1, 1)} % (Delta ${fmtDelta(cf.target_duty_pct, rf.target_duty_pct, " %")})`],
      ["SoC-Leistung risky", `${Number.isFinite(cr.soc_power_w) ? `${num(cr.soc_power_w, 1)} W` : "--"} (Delta ${fmtDelta(cr.soc_power_w, rr.soc_power_w, " W")})`],
      ["CPU-Takt risky", `${cr.cpu_mhz_valid ? `${num(cr.cpu_mhz, 0)} MHz` : "--"} (Delta ${fmtDelta(cr.cpu_mhz, rr.cpu_mhz, " MHz")})`],
      ["Firmware", `${(cSys && (cSys.firmware_version || cSys.firmware_raw)) || "--"} / Ref ${(rSys && (rSys.firmware_version || rSys.firmware_raw)) || "--"}`],
      ["Adapter Sensor/Lüfter", `${(cs.adapters && cs.adapters.sensors) || "--"} / ${(cs.adapters && cs.adapters.fan) || "--"} (Ref ${((rs.adapters && rs.adapters.sensors) || "--")} / ${((rs.adapters && rs.adapters.fan) || "--")})`]
    ];

    host.innerHTML = rows.map(([k, v]) => `<div><span>${esc(k)}</span><b>${esc(String(v))}</b></div>`).join("");
  };

  const renderMonitorAlert = () => {
    const stateEl = $("#mon-alert-state");
    const countEl = $("#mon-alert-count");
    const reasonEl = $("#mon-alert-reason");
    const listEl = $("#mon-alert-list");

    if (stateEl) {
      if (!state.monitorAlertEnabled) {
        stateEl.textContent = "aus";
      } else {
        stateEl.innerHTML = monitorLevelBadge(state.monitorAlertLevel || "ok");
      }
    }
    if (countEl) countEl.textContent = String(state.monitorAlertEvents.length);

    if (reasonEl) {
      let txtReason = "Monitoring ist ausgeschaltet.";
      if (state.monitorAlertEnabled) {
        txtReason = state.monitorAlertEvents.length
          ? state.monitorAlertEvents[0].detail
          : "Keine Grenzwertverletzung im bisherigen Verlauf.";
      }
      reasonEl.innerHTML = `<div><span>Auslöser</span><b>${esc(txtReason)}</b></div>`;
    }

    if (listEl) {
      if (!state.monitorAlertEvents.length) {
        listEl.innerHTML = `<p class="muted">noch keine Alarmwechsel</p>`;
      } else {
        listEl.innerHTML = state.monitorAlertEvents.map((e) => {
          const label = e.level === "hot" ? "KRITISCH" : e.level === "warn" ? "WARNUNG" : "OK";
          const cls = e.level === "hot" ? "hot" : e.level === "warn" ? "warn" : "ok";
          return `<div class="row"><time>${new Date(e.ts).toLocaleTimeString(LOCALE)}</time>`
            + `<i class="reason-tag ${cls}">${label}</i>`
            + `<span>${esc(e.detail)}</span></div>`;
        }).join("");
      }
    }
  };

  const pushMonitorAlertEvent = (level, detail) => {
    state.monitorAlertEvents.unshift({ ts: Date.now(), level, detail });
    if (state.monitorAlertEvents.length > 16) state.monitorAlertEvents.length = 16;
  };

  const evaluateMonitorAlert = (s) => {
    if (!state.monitorAlertEnabled) {
      renderMonitorAlert();
      return;
    }

    const T = s.temperatures || {};
    const F = s.fan || {};
    const C = s.control || {};
    const hotNow = Number(T.hottest_c);
    const reasons = [];
    let level = "ok";

    if (C.safety_active) {
      level = "hot";
      reasons.push("Sicherheitsmodus aktiv");
    }
    if (Number.isFinite(hotNow) && hotNow >= state.monitorHotC) {
      level = "hot";
      reasons.push(`Wärmster Sensor ${num(hotNow, 1)} °C (>= ${state.monitorHotC} °C)`);
    } else if (Number.isFinite(hotNow) && hotNow >= state.monitorWarnC && level !== "hot") {
      level = "warn";
      reasons.push(`Wärmster Sensor ${num(hotNow, 1)} °C (>= ${state.monitorWarnC} °C)`);
    }

    if (!F.available && level === "ok") {
      level = "warn";
      reasons.push("Lüfteradapter nicht verfügbar");
    }
    if (!F.measured_valid && level === "ok") {
      level = "warn";
      reasons.push("Ist-Drehzahl nicht verfügbar");
    }
    if (F.measured_valid && Number.isFinite(hotNow)
        && F.measured_duty_pct <= state.monitorFanMinPct
        && hotNow >= (state.monitorWarnC - 2)
        && level === "ok") {
      level = "warn";
      reasons.push(`Lüfter niedrig (${num(F.measured_duty_pct, 0)} %) bei hoher Temperatur`);
    }

    const detail = reasons.length ? reasons.join("; ") : "Keine Grenzwertverletzung";
    if (state.monitorAlertLevel !== level) {
      if (state.monitorAlertLevel !== null) {
        pushMonitorAlertEvent(level, detail);
      }
      state.monitorAlertLevel = level;
    }
    if (!state.monitorAlertEvents.length && level !== "ok") {
      pushMonitorAlertEvent(level, detail);
    }
    renderMonitorAlert();
  };

  const levelLabel = (lvl) => lvl === "hot" ? "ROT" : lvl === "warn" ? "GELB" : "OK";
  const levelRank = (lvl) => lvl === "hot" ? 2 : lvl === "warn" ? 1 : 0;

  const pushDiagEvent = (tag, source, title, detail, fromLevel, toLevel) => {
    const from = fromLevel || tag;
    const to = toLevel || tag;
    const dir = levelRank(to) > levelRank(from)
      ? "escalate"
      : levelRank(to) < levelRank(from)
        ? "recover"
        : "steady";
    state.diagEvents.unshift({
      ts: Date.now(),
      tag,
      source,
      title,
      detail,
      transition: `${levelLabel(from)}→${levelLabel(to)}`,
      transitionClass: dir
    });
    if (state.diagEvents.length > 8) state.diagEvents.length = 8;
  };

  const relAge = (ms) => {
    const s = Math.max(0, Math.floor((Date.now() - ms) / 1000));
    if (s < 60) return `vor ${s}s`;
    const m = Math.floor(s / 60);
    if (m < 60) return `vor ${m}m`;
    return `vor ${Math.floor(m / 60)}h`;
  };

  const renderDiagEvents = (animate) => {
    const host = $("#diag-events-list");
    const cnt = $("#diag-events-count");
    const hotEl = $("#diag-count-hot");
    const warnEl = $("#diag-count-warn");
    const okEl = $("#diag-count-ok");
    const noteEl = $("#diag-summary-note");
    if (!host) return;
    if (animate) {
      host.classList.remove("diag-swap");
      requestAnimationFrame(() => {
        host.classList.add("diag-swap");
        setTimeout(() => host.classList.remove("diag-swap"), 240);
      });
    }
    const cutoff = Date.now() - (state.diagWindowMin * 60 * 1000);
    const windowed = state.diagEvents.filter((e) => e.ts >= cutoff);
    if (cnt)
      cnt.textContent = `${windowed.length}/${state.diagEvents.length} Einträge`;
    const agg = { hot: 0, warn: 0, ok: 0 };
    windowed.forEach((e) => {
      if (e.tag === "hot") agg.hot++;
      else if (e.tag === "warn") agg.warn++;
      else agg.ok++;
    });
    if (hotEl) hotEl.textContent = String(agg.hot);
    if (warnEl) warnEl.textContent = String(agg.warn);
    if (okEl) okEl.textContent = String(agg.ok);
    if (noteEl)
      noteEl.textContent = `Fenster: letzte ${state.diagWindowMin} Minuten (${windowed.length} gewertet)`;
    if (!state.diagEvents.length) {
      host.innerHTML = `<p class="muted">noch keine Wechsel erkannt</p>`;
      return;
    }
    if (!windowed.length) {
      host.innerHTML = `<p class="muted">Keine Wechsel im gewählten Zeitfenster.</p>`;
      return;
    }
    host.innerHTML = windowed.map((e) => {
      const srcLabel = e.source === "fan"
        ? "Lüfter"
        : e.source === "ampel"
          ? "Ampel"
          : "System";
      const srcIcon = e.source === "fan"
        ? "🌀"
        : e.source === "ampel"
          ? "🚦"
          : "•";
      return (
      `<div class="row"><time>${relAge(e.ts)}</time>`
      + `<span class="diag-src">${srcIcon} ${srcLabel}</span>`
      + `<i class="reason-tag ${e.tag === "hot" || e.tag === "warn" ? e.tag : "ok"}">${e.tag === "hot" ? "ROT" : e.tag === "warn" ? "GELB" : "OK"}</i>`
      + `<span class="diag-diff ${esc(e.transitionClass || "steady")}">${esc(e.transition || "OK→OK")}</span>`
      + `<b>${esc(e.title)}</b><span>${esc(e.detail)}</span></div>`
      );
    }).join("");
  };

  /* Jede Abfrage bekommt eine Frist.
   *
   * Ohne sie wartet `fetch` unbegrenzt. Antwortet die Konsole nicht mehr —
   * nach dem Ruhezustand, bei abgerissenem WLAN, oder weil der Payload weg
   * ist — landet der Aufruf niemals im Fehlerzweig. Die Anzeige stand dann
   * mit alten Zahlen da und die Ampel meldete weiter "Verbunden": der
   * schlimmste Fall für eine Temperaturanzeige, weil falsche Werte
   * überzeugender aussehen als gar keine.
   *
   * Acht Sekunden sind großzügig für eine Antwort aus dem eigenen Netz und
   * kurz genug, dass ein Ausfall innerhalb eines Wimpernschlags sichtbar
   * wird. Wer länger braucht — das Hochladen des Kachel-Pakets — reicht
   * `timeoutMs` durch. */
  const API_TIMEOUT_MS = 8000;

  const api = async (path, opts = {}) => {
    const { timeoutMs, ...rest } = opts;
    const headers = { ...(rest.headers || {}) };
    if (rest.body && !headers["Content-Type"]) headers["Content-Type"] = "application/json";

    const ctl = new AbortController();
    const timer = setTimeout(() => ctl.abort(), timeoutMs || API_TIMEOUT_MS);

    /* The timer must outlive the body. A console that sends the headers and
       then stalls would otherwise leave res.json() waiting for good, and with
       it every caller that holds a lock around this call (the one-second
       status loop). An abort mid-body rejects res.json() the same way. */
    try {
      const res = await fetch(path, { ...rest, headers, signal: ctl.signal });
      let data = null;
      try {
        data = await res.json();
      } catch (e) {
        if (e && e.name === "AbortError") throw e;
      }

      const body = data && typeof data === "object" ? data : null;
      if (!res.ok || (body && body.ok === false)) {
        const err = new Error((body && body.message) || `HTTP ${res.status}`);
        err.status = res.status;
        err.code = body && body.code;
        err.data = body;
        throw err;
      }
      /* An unreadable answer is an error, not an empty object: the callers
         would render "undefined" from it. */
      if (!body) throw new Error("Die Antwort der Konsole war nicht lesbar.");
      return body;
    } catch (e) {
      if (e && e.name === "AbortError")
        throw new Error("Die Konsole antwortet nicht.");
      throw e;
    } finally {
      clearTimeout(timer);
    }
  };

  const apiSafe = async (path, opts = {}) => {
    try {
      return { ok: true, data: await api(path, opts) };
    } catch (e) {
      return { ok: false, error: e && e.message ? e.message : "unbekannter Fehler" };
    }
  };

  /* Rückfrage im Knopf statt confirm().
   *
   * Native Dialoge sind hier nicht verlässlich: der Browser des Users
   * beantwortet sie stillschweigend mit Nein, und ein abgelehnter Dialog ist
   * von einem toten Knopf nicht zu unterscheiden — beim Profilbild hat genau
   * das eine Fehlersuche gekostet. Zwei Klicks auf denselben Knopf sind
   * ebenso eindeutig, sichtbar, und lassen sich nicht unterdrücken.
   *
   * `warn` wird beim Bewaffnen als Hinweis gezeigt, damit die Folge genannt
   * ist, bevor sie eintritt. Nach zehn Sekunden entschärft sich der Knopf
   * von selbst.
   *
   * `holdMs`: so lange nach dem ersten Klick bleibt ein zweiter wirkungslos.
   * Ein Doppelklick wäre sonst Bewaffnen und Bestätigen in einem, ehe jemand
   * den Hinweis lesen konnte (bei „Ausschalten“ nicht harmlos). */
  const armConfirm = (btn, armedLabel, run, warn, holdMs = 0) => {
    const label = btn.textContent;
    let timer = 0, armedAt = 0;
    /* data-armed lässt ein Stylesheet den bereiten Zustand zeigen (die Kopfzeilenknöpfe tun es). */
    const disarm = () => { clearTimeout(timer); timer = 0; btn.textContent = label; delete btn.dataset.armed; };

    btn.addEventListener("click", async () => {
      if (!timer) {
        btn.textContent = armedLabel;
        btn.dataset.armed = "1";
        if (warn) toast(warn);
        timer = setTimeout(disarm, 10000);
        armedAt = performance.now();
        return;
      }
      if (performance.now() - armedAt < holdMs) return;
      disarm();
      await run();
    });
  };

  const saveConfig = async (patch, note) => {
    await api("/api/v1/config", { method: "PUT", body: JSON.stringify(patch) });
    if (note) toast(note);
    await loadConfig();
  };

  /* Anything that reads the forms must wait for the first config load:
     saving from empty fields would write zeros over the real settings. */
  const needCfg = () => {
    if (state.cfg) return true;
    toast("Die Einstellungen sind noch nicht geladen. Bitte einen Moment warten.", "error");
    return false;
  };

  /* ── Klartext-Status ────────────────────────────────────────────── */

  const describe = (s) => {
    const hot = s.temperatures.hottest_c;
    const fan = s.fan.measured_valid ? s.fan.measured_duty_pct : null;
    const target = s.control.target_temp_c;

    if (!s.adapters || s.adapters.sensors !== "ready")
      return { cls: "warn", icon: "!", title: "Sensoren nicht lesbar",
               sub: s.messages.sensors };

    if (s.control.safety_active)
      return { cls: "hot", icon: "!", title: "Notfallmodus aktiv",
               sub: `Die Konsole ist ungewöhnlich warm (${hot} °C). Der Lüfter ` +
                    `läuft mit voller Kraft, bis die Temperatur wieder normal ist.` };

    if (s.warnings.cpu || s.warnings.soc)
      return { cls: "hot", icon: "!", title: "Warnschwelle überschritten",
               sub: `Der wärmste Sensor liegt bei ${hot} °C. Sorge für freie ` +
                    `Belüftung rund um die Konsole.` };

    if (!s.fan.available)
      return { cls: "warn", icon: "!", title: "Lüftersteuerung nicht verfügbar",
               sub: s.messages.fan };

    if (!s.fan.automatic)
      return { cls: "warn", icon: "•", title: "Die PS5 regelt selbst",
               sub: `Die Automatik ist aus, die Konsole steuert den Lüfter mit ` +
                    `ihrer eigenen Kennlinie. Aktuell ${hot} °C bei ` +
                    `${fan ?? "--"} % Drehzahl.` };

    const quiet = fan !== null && fan <= 35;
    if (quiet)
      return { cls: "", icon: "✓", title: "Alles in Ordnung",
               sub: `Die Konsole läuft bei ${hot} °C und der Lüfter ist mit ` +
                    `${fan} % im leisen Bereich. Ziel: ${target} °C.` };

    return { cls: "", icon: "✓", title: "Kühlung arbeitet",
             sub: `Bei ${hot} °C hält die Regelung mit ${fan ?? "--"} % ` +
                  `Lüfterdrehzahl gegen. Ziel: ${target} °C.` };
  };

  /* ── Diagramme ──────────────────────────────────────────────────── */

  const buildPath = (vals, lo, hi) => {
    const pts = vals.map((v, i) => {
      if (v === null || v === undefined) return null;
      const x = vals.length === 1 ? W : (i / (vals.length - 1)) * W;
      const y = H - ((v - lo) / (hi - lo)) * (H - 12) - 6;
      return [x, Math.max(2, Math.min(H - 2, y))];
    });
    let d = "", pen = false;
    for (const p of pts) {
      if (!p) { pen = false; continue; }
      d += (pen ? "L" : "M") + p[0].toFixed(1) + " " + p[1].toFixed(1) + " ";
      pen = true;
    }
    return d.trim();
  };

  const areaFrom = (d) => d ? `${d} L${W} ${H} L0 ${H} Z` : "";

  const miniPath = (vals, lo, hi, w = 280, h = 56) => {
    const pts = vals.map((v, i) => {
      if (v === null || v === undefined) return null;
      const x = vals.length === 1 ? w : (i / (vals.length - 1)) * w;
      const y = h - ((v - lo) / (hi - lo)) * (h - 8) - 4;
      return [x, Math.max(2, Math.min(h - 2, y))];
    });
    let d = "", pen = false;
    for (const p of pts) {
      if (!p) { pen = false; continue; }
      d += (pen ? "L" : "M") + p[0].toFixed(1) + " " + p[1].toFixed(1) + " ";
      pen = true;
    }
    return d.trim();
  };

  const firstLastDelta = (arr) => {
    const ok = (arr || []).filter((v) => v !== null && v !== undefined);
    if (ok.length < 2) return null;
    return ok[ok.length - 1] - ok[0];
  };

  const drawGrid = (id, lo, hi, axisId, unit) => {
    const g = $(id), ax = $(axisId);
    let paths = "", labels = "";
    for (let i = 0; i <= 3; i++) {
      const y = 6 + (i / 3) * (H - 12);
      paths += `<path d="M0 ${y.toFixed(1)}H${W}"/>`;
      labels += `<span>${Math.round(hi - (i / 3) * (hi - lo))}${unit}</span>`;
    }
    g.innerHTML = paths;
    ax.innerHTML = labels;
  };

  const niceRange = (vals, pad, min, max) => {
    const ok = vals.filter((v) => v !== null && v !== undefined);
    if (!ok.length) return [min, max];
    let lo = Math.floor((Math.min(...ok) - pad) / 5) * 5;
    let hi = Math.ceil((Math.max(...ok) + pad) / 5) * 5;
    if (hi - lo < 15) hi = lo + 15;
    return [Math.max(0, lo), hi];
  };

  const renderCharts = () => {
    const h = state.hist;
    if (!h.length) return;

    const cpu = h.map((r) => r.cpu), soc = h.map((r) => r.soc);
    /* Grafik (Kanal 7) seit 1.45.1 — nur auf der PS5 Pro, sonst durchweg
       null: dann bleiben Linie und Legende leer. */
    const gpu = h.map((r) => r.gpu ?? null);
    const target = state.status ? state.status.control.target_temp_c : null;
    const [lo, hi] = niceRange([...cpu, ...soc, ...gpu, target], 6, 40, 80);

    drawGrid("#grid-temp", lo, hi, "#axis-temp", "°");
    $("#line-cpu").setAttribute("d", buildPath(cpu, lo, hi));
    $("#line-soc").setAttribute("d", buildPath(soc, lo, hi));
    $("#line-gpu").setAttribute("d", buildPath(gpu, lo, hi));
    $("#legend-gpu").classList.toggle("is-hidden", !gpu.some((v) => v !== null));
    $("#area-cpu").setAttribute("d", areaFrom(buildPath(cpu, lo, hi)));
    $("#area-soc").setAttribute("d", areaFrom(buildPath(soc, lo, hi)));

    if (target !== null) {
      const y = H - ((target - lo) / (hi - lo)) * (H - 12) - 6;
      $("#line-target").setAttribute("d", `M0 ${y.toFixed(1)}H${W}`);
    }

    const fan = h.map((r) => r.fan);
    drawGrid("#grid-fan", 0, 100, "#axis-fan", "%");
    const fd = buildPath(fan, 0, 100);
    $("#line-fan").setAttribute("d", fd);
    $("#area-fan").setAttribute("d", areaFrom(fd));

    state.range = { lo, hi };
  };

  /* `countFn` lets a chart supply its own series length; without it the tooltip
     follows the live two-minute buffer. */
  const wireTooltip = (hostId, crossId, tipId, format, countFn) => {
    const host = $(hostId), cross = $(crossId), tip = $(tipId);
    if (!host) return;
    const move = (ev) => {
      const svg = host.querySelector("svg");
      const box = svg.getBoundingClientRect();
      const rel = Math.max(0, Math.min(1, (ev.clientX - box.left) / box.width));
      const n = countFn ? countFn() : state.hist.length;
      if (!n) return;
      const idx = Math.round(rel * (n - 1));
      const x = n === 1 ? W : (idx / (n - 1)) * W;
      cross.setAttribute("x1", x); cross.setAttribute("x2", x);
      tip.innerHTML = format(countFn ? null : state.hist[idx], idx);
      tip.style.left = `${34 + rel * box.width}px`;
      tip.style.top = "26px";
      host.classList.add("hover");
    };
    host.addEventListener("mousemove", move);
    host.addEventListener("mouseleave", () => host.classList.remove("hover"));
    host.addEventListener("touchmove", (e) => {
      if (e.touches[0]) move(e.touches[0]);
    }, { passive: true });
  };

  /* ── Statusdarstellung ──────────────────────────────────────────── */

  const setBar = (id, pct, hotAt) => {
    const el = $(id);
    if (!el) return;
    el.style.width = `${Math.max(0, Math.min(100, pct))}%`;
    if (hotAt) {
      el.classList.toggle("warn", pct >= hotAt && pct < hotAt + 12);
      el.classList.toggle("hot", pct >= hotAt + 12);
    }
  };

  /* ── Übersicht (Reihe D, seit 19.09.2026) ───────────────────────────
   * Ring für den wärmsten Punkt (Skala 30–90 °C), Lüfterring in der Kachel
   * daneben, Voreinstellungen in der Lüftersteuerung. Vorher: Muster A mit
   * zwei Einzelringen um einen großen Lüfter. */
  const RG_CIRC = 402.12;                     /* Umfang bei r = 64 */
  const ringArc = (sel, temp, valid) => {
    const el = $(sel);
    if (!el) return;
    const f = valid ? Math.max(0, Math.min(1, (temp - 30) / 60)) : 0;
    el.style.strokeDasharray = `${(f * RG_CIRC).toFixed(1)} ${RG_CIRC}`;
  };

  /* Der Lüfter drehte sich ursprünglich (Muster A, 18.09.2026) — drei
   * verschiedene Techniken (JS-Schleife, CSS-Dauer, WAAPI-Tempo) zeigten auf
   * der PS5 alle denselben leichten Hänger bei jeder Drehzahländerung.
   * Vermutlich ein kurzer Haupt-Thread-Block beim sekündlichen
   * Status-Update, der bei einer Rotation mit sichtbaren Blättern sofort
   * auffällt. Jetzt zeigt ein Füllring die Drehzahl — dieselbe Technik wie
   * `ringArc` oben, bei den Temperatur-Ringen nie ein Problem gewesen, weil
   * ein Wert, der sanft nachzieht, keinen Rotationswinkel hat, der springen
   * könnte. Die Blätter drehen seit v1.35.1 wieder, aber mit fester
   * CSS-Geschwindigkeit, die JS nie anfasst. Details: [[ui-redesign]].
   * Es gibt einen Ring (Kühlungsseite; bis 1.45.x hatten Übersicht und Kühlung je einen).
   * Seit v1.38.0 liegt er auf der Leuchtröhre des Lüfterbilds (r = 110 von
   * 256): Füllung und ihr breiter Schein wachsen gemeinsam. */
  const FAN_RING_CIRC = 691.2;                /* Umfang bei r = 110 */
  const fanRing = (pct) => {
    const f = Math.max(0, Math.min(1, pct / 100));
    const dash = `${(f * FAN_RING_CIRC).toFixed(1)} ${FAN_RING_CIRC}`;
    $$(".fan-ring-fill, .fan-ring-glow").forEach((el) => { el.style.strokeDasharray = dash; });
  };

  /* Die drei Voreinstellungen, genau wie presetApply() sie setzt: im Modus
     Automatik als Zieltemperatur, im Modus Beobachten als feste
     Lüfterschwelle. Eine Voreinstellung ist nur „aktiv", solange der Wert
     noch genau stimmt — danach ist es ein eigener Wert. */
  /* Die Zieltemperatur der Schnellwahl (Automatik). Bis 03.10.2026 reichte der Regler bis
     72 °C und die Werte waren 62 / 66 / 70; bis 07.10.2026 ging er bis 78 °C (62 / 70 / 78);
     seit er bis 91 °C geht, verteilen sich die Werte über den ganzen Bereich: „Leise" ist der
     leiseste Wert, den der Regler zulässt (91 °C, der Wert der Konsole selbst), „Kühl" bleibt
     bei 62 °C und „Ausgewogen" liegt in der Mitte zwischen den beiden (76,5, aufgerundet).
     Die Schwellen der Beobachtungsart (observe) sind etwas anderes — eine feste Schwelle der
     Konsole, kein Ziel — und gelten unverändert. */
  const PRESETS = {
    automatic: { cool: 62, balanced: 77, quiet: 91 },
    observe:   { cool: 58, balanced: 65, quiet: 74 }
  };
  const PRESET_NAMES = { cool: "Kühl", balanced: "Ausgewogen", quiet: "Leise" };

  const renderReactor = (s) => {
    const T = s.temperatures, F = s.fan, C = s.control;
    /* Ring: wärmster Punkt, gleiche Skala 30–90 °C wie früher die Einzelringe */
    ringArc("#ov-hot-ring", T.hottest_c, T.hottest_c >= 0);
    fanRing(F.measured_valid ? F.measured_duty_pct : 0);

    const auto = !!F.automatic;
    /* Wirksames Ziel (eine Spielregel kann es überschreiben) wird gezeigt.
       Verglichen wird mit dem Grundwert, denn nur den setzt eine
       Voreinstellung. */
    const target = Number.isFinite(C.effective_target_c) ? C.effective_target_c : C.target_temp_c;
    const shown = auto ? C.target_temp_c : F.threshold_c;
    const byRule = auto && Number.isFinite(C.effective_target_c) && C.effective_target_c !== C.target_temp_c;

    /* Unter dem Ring steht, wie weit der wärmste Punkt vom Ziel entfernt ist.
       Das Ziel selbst zeigt die Karte „Zieltemperatur" gleich darunter — hier
       noch einmal dieselbe Zahl zu nennen wäre eine Doppelung (seit 1.46.0
       liegen Übersicht und Kühlung auf einer Seite). */
    const hot = T.hottest_c >= 0 ? T.hottest_c : null;
    const ref = auto ? target : shown;
    const diff = hot !== null && Number.isFinite(ref) ? Math.round(hot - ref) : null;
    const goal = auto ? "dem Ziel" : "der Schwelle";
    const rule = byRule ? " (Spielregel)" : "";
    txt("#hero-diff", diff === null ? "wird gelesen …"
      : diff > 0 ? `${diff} °C über ${goal}${rule}`
      : diff < 0 ? `${-diff} °C unter ${goal}${rule}`
      : `${auto ? "genau am Ziel" : "genau an der Schwelle"}${rule}`);

    /* Chip-Bild in „Sensoren": rot bei Warnschwelle oder Notfallmodus,
       orange, wenn die Konsole mehr als 5 °C über dem Ziel liegt. */
    const chip = $("#ov-chip");
    if (chip) {
      const warn = s.warnings || {};
      const heat = (warn.cpu || warn.soc || C.safety_active) ? "hot"
        : (diff !== null && diff > 5) ? "warm" : "ok";
      if (chip.dataset.heat !== heat) chip.dataset.heat = heat;
    }

    /* Die drei Voreinstellungen der Zieltemperatur-Karte */
    const set = PRESETS[auto ? "automatic" : "observe"];
    let active = null;
    Object.keys(set).forEach((kind) => {
      const on = Number.isFinite(shown) && shown === set[kind];
      if (on) active = kind;
      const btn = $(`#preset-${kind}`);
      if (btn) {
        btn.classList.toggle("on", on);
        btn.setAttribute("aria-pressed", on ? "true" : "false");
        btn.disabled = !F.available;
      }
      txt(`#co-preset-${kind}-v`, auto ? `Ziel ${set[kind]} °C` : `Schwelle ${set[kind]} °C`);
    });
    txt("#co-preset-note", !F.available
      ? "Lüftersteuerung nicht verfügbar — die App beobachtet nur."
      : active
        ? `Voreinstellung ${PRESET_NAMES[active]} aktiv.`
        : Number.isFinite(shown)
          ? `Eigener Wert: ${shown} °C. Eine Voreinstellung setzt ihn neu.`
          : "Noch kein Wert gesetzt.");
  };

  const renderStatus = (s) => {
    state.status = s;
    const T = s.temperatures, F = s.fan, C = s.control, L = s.load;

    /* Klartext */
    const d = describe(s);
    const hero = $("#hero");
    hero.classList.remove("warn", "hot");
    if (d.cls) hero.classList.add(d.cls);
    txt("#hero-icon", d.icon);
    txt("#hero-title", d.title);
    txt("#hero-sub", d.sub);
    txt("#hero-temp", T.hottest_c >= 0 ? T.hottest_c : "--");

    /* Kacheln */
    $("#t-cpu").innerHTML = `${T.cpu_valid ? T.cpu_c : "--"}<i>°C</i>`;
    $("#t-soc").innerHTML = `${T.soc_valid ? T.soc_c : "--"}<i>°C</i>`;
    $("#t-fan").innerHTML =
      `${F.measured_valid ? F.measured_duty_pct : "--"}<i>%</i>`;
    /* Not every firmware reports CPU utilisation. Rather than leave a tile
       showing "--" forever, it falls back to the figure the controller is
       actually working towards. */
    const loadEl = $("#t-load");
    if (loadEl) {
      if (L.cpu_valid) {
        txt("#t-load-title", "Auslastung");
        loadEl.innerHTML = `${Math.round(L.cpu_pct)}<i>%</i>`;
      } else {
        txt("#t-load-title", "Zieltemperatur");
        loadEl.innerHTML = `${C.target_temp_c}<i>°C</i>`;
      }
    }

    setBar("#b-cpu", ((T.cpu_c - 30) / 60) * 100, 70);
    setBar("#b-soc", ((T.soc_c - 30) / 60) * 100, 70);
    setBar("#b-fan", F.measured_valid ? F.measured_duty_pct : 0);
    setBar("#b-load", L.cpu_valid ? L.cpu_pct
                                  : ((C.target_temp_c - 55) / 20) * 100);

    txt("#t-cpu-note", T.cpu_valid ? "Hauptprozessor" : "nicht lesbar");
    /* Zeigt seit 1.17.0 die heißeste chipnahe Messstelle statt Kanal 0 —
       der lag bis zu 17 °C zu niedrig. Siehe platform.c. */
    txt("#t-soc-note", T.soc_valid
      ? "heißeste Messstelle am Chip" : "nicht lesbar");
    /* Kanal 7, seit 1.45.1: der Sensor, der der Grafikeinheit am nächsten
       liegt. Die API liefert ihn nur auf der PS5 Pro, wo er vermessen ist;
       auf anderen Modellen bleibt die Zeile weg, statt eine fremde
       Zuordnung zu borgen. */
    $("#t-gpu-row").classList.toggle("is-hidden", !T.gpu_valid);
    if (T.gpu_valid) {
      $("#t-gpu").innerHTML = `${T.gpu_c}<i>°C</i>`;
      txt("#t-gpu-note", "Messstelle an der Grafikeinheit");
    }
    txt("#t-fan-note", F.available ? "gemessene Drehzahl"
                                   : "Lüfter nicht steuerbar");
    txt("#t-load-note", L.cpu_valid
      ? (L.cpu_mhz ? `Prozessor · ${num(L.cpu_mhz)} MHz` : "Prozessor")
      : `darauf wird geregelt${L.cpu_mhz ? ` · ${num(L.cpu_mhz)} MHz` : ""}`);

    /* Laufendes Spiel — nur zeigen, wenn die Konsole eines meldet. */
    const g = s.game || {};
    /* Seit der Neugestaltung ist das eine feste Kachel der Kühlungsseite: Sie
       bleibt stehen und sagt ehrlich „kein Spiel", statt zu verschwinden und
       ein Loch im Raster zu hinterlassen. */
    const dot = $("#run-dot");
    const fps = s.fps && s.fps.valid ? s.fps.fps : null;
    if (g.title_name) {
      txt("#run-name", g.title_name);
      /* Bildwechsel pro Sekunde zählt die Bildausgabe; mit dem Spiel im
         Vordergrund ist das dessen Bildrate. */
      const fpsTxt = g.foreground && fps !== null ? ` · ${Math.round(fps)} Bilder/s` : "";
      /* The id is missing when only the focus is known (no title in the log). */
      txt("#run-state", g.foreground ? `im Vordergrund${fpsTxt}`
        : `${g.state}${g.title_id ? ` · ${g.title_id}` : ""}`);
      if (dot) dot.classList.toggle("live", !!g.foreground);
    } else {
      txt("#run-name", "Kein Spiel erkannt");
      txt("#run-state", "");
      if (dot) dot.classList.remove("live");
    }
    /* „Spiel beenden" nur, wenn ein Spiel mit Titel-ID läuft. */
    const runClose = $("#run-close");
    if (runClose) {
      const can = !!(g.title_id && g.title_name);
      runClose.hidden = !can;
      runClose.dataset.id = can ? String(g.title_id) : "";
      runClose.dataset.name = can ? String(g.title_name) : "";
    }
    /* Die Spieleseite markiert das laufende Spiel; neu gezeichnet wird sie
       nur, wenn es wechselt, nicht mit jeder Abfrage. */
    if (state.page === "games" && gm.loaded &&
        (gmRunningId() !== gm.running || gmLiveId() !== gm.live))
      renderGames();

    /* Reaktor-Ringe + Lüfterrotor */
    renderReactor(s);

    /* Verlauf */
    if (T.cpu_valid || T.soc_valid) {
      state.hist.push({
        t: s.timestamp_ms,
        cpu: T.cpu_valid ? T.cpu_c : null,
        soc: T.soc_valid ? T.soc_c : null,
        gpu: T.gpu_valid ? T.gpu_c : null,
        fan: F.measured_valid ? F.measured_duty_pct : null
      });
      if (state.hist.length > HISTORY) state.hist.shift();
      renderCharts();
    }

    /* Prozessor.
     *
     * Die acht Kerne sind EINE Messung über acht Plätze, nicht acht
     * verschiedene Dinge — deshalb eine Farbe und die Länge als Wert. Acht
     * Farben würden eine Bedeutung behaupten, die es nicht gibt. Vorher
     * standen hier zwölf gleich aussehende Zahlenkacheln, zwischen denen das
     * Auge nichts vergleichen konnte. */
    const RING = 351.86;                       /* Umfang bei r = 56 */
    const arc  = $("#cpu-arc");
    const tot  = $("#cpu-total");

    if (arc && tot) {
      const pct = L.cpu_valid ? Math.max(0, Math.min(100, L.cpu_pct)) : 0;
      arc.style.strokeDasharray = `${(pct / 100) * RING} ${RING}`;
      tot.textContent = L.cpu_valid ? `${Math.round(pct)}%` : "--";
    }

    /* Takt: bevorzugt aus Sonys Energiezustands-Tabelle, weil die alle acht
       Kerne einzeln nennt. sceKernelGetCpuFrequency liefert nur eine Zahl —
       gemessen 800 MHz, während vier Kerne auf 3200 liefen. Diese eine Zahl
       war nicht falsch, aber irreführend.
       „Modus Boost" stand hier auch einmal, ungeprüft aus fremden Payloads
       übernommen und wieder entfernt. */
    const meta = [];
    const K = L.clocks;
    /* Seit 1.45.0 gibt es den Takt auch live (sceKernelGetSocClock). Die
       Energiezustands-Tabelle nennt dagegen, was der Modus erlaubt — auf der
       PS5 Pro 2350 MHz Grafik, egal was die GPU gerade tut. Live geht vor. */
    const LK = L.live_clocks;
    if (LK && (LK.core_mhz || LK.gfx_mhz)) {
      if (LK.core_mhz && LK.core_mhz.length) {
        const lo = Math.min(...LK.core_mhz), hi = Math.max(...LK.core_mhz);
        meta.push(lo === hi ? `Kerne ${num(hi)} MHz` : `Kerne ${num(lo)}–${num(hi)} MHz`);
      }
      if (LK.gfx_mhz) meta.push(`Grafik ${num(LK.gfx_mhz)} MHz`);
      meta.push("live");
    } else if (K && K.core_mhz && K.core_mhz.length) {
      const lo = Math.min(...K.core_mhz), hi = Math.max(...K.core_mhz);
      meta.push(lo === hi ? `Kerne ${num(hi)} MHz`
                          : `Kerne ${num(lo)}–${num(hi)} MHz`);
      if (K.gfx_mhz) meta.push(`Grafik ${num(K.gfx_mhz)} MHz`);
      if (K.age_s >= 0) {
        const a = K.age_s;
        meta.push(a < 90 ? "gerade gemessen"
                : a < 5400 ? `Stand vor ${Math.round(a / 60)} Min.`
                : `Stand vor ${Math.round(a / 3600)} Std.`);
      }
    } else if (L.cpu_mhz) {
      meta.push(`${num(L.cpu_mhz)} MHz Takt`);
    }
    /* Spiele bekommen elf der sechzehn logischen CPUs, das System fünf. Die
       Aufteilung sagt mehr als der Mittelwert über alle. */
    if (Number.isFinite(L.game_pct) && Number.isFinite(L.system_pct))
      meta.push(`Spiel-CPUs ${Math.round(L.game_pct)} % · System ${Math.round(L.system_pct)} %`);
    txt("#cpu-sub", meta.join(" · ") || "Die Konsole meldet keinen Takt.");

    const coreBox = $("#cores");
    if (coreBox) {
      const cores = L.cores || [];
      coreBox.innerHTML = cores.length
        ? cores.map((v, i) => {
            /* −1 heißt „nicht gemessen". Eine leere Spur mit Strich sagt das
               ehrlich; eine 0 wäre eine erfundene Zahl zwischen echten. */
            if (v < 0)
              return `<div class="core" title="Kern ${i + 1}: nicht messbar">
                <i>Kern ${i + 1}</i>
                <div class="track"></div>
                <u>–</u>
              </div>`;
            const w = Math.max(0, Math.min(100, v));
            return `<div class="core" title="Kern ${i + 1}: ${esc(v)} %">
              <i>Kern ${i + 1}</i>
              <div class="track"><span class="fill" style="width:${w}%"></span></div>
              <u>${esc(v)} %</u>
            </div>`;
          }).join("")
        : `<p class="muted">Die Konsole meldet keine Werte je Kern.</p>`;
    }

    /* Was übrig bleibt: alles, was keine Prozessorauslastung ist. */
    const cells = [];
    (T.channels || []).forEach((c) => {
      /* Kanal 7 (Grafik) steht seit 1.45.1 in der Sensorkarte oben. */
      if (c.channel === 7) return;
      cells.push(`<div class="sensor"><span>${esc(c.label)}</span>
        <b>${esc(c.temp_c)}<em>°C</em></b></div>`);
    });
    /* Leistung aus den Stromschienen des Chips (1.45.0). Leistung, nicht
       Auslastung: Einen Zähler für die GPU-Auslastung gibt es auf der PS5
       nicht, die Watt der Grafikschiene sind der ehrliche Ersatz. */
    /* Nur was sich nachweislich bewegt. Die CPU-Schienen standen auf der
       PS5 Pro eine Spielstunde lang bei exakt 111,4 W — kein Messwert. Sie
       und die Summe erscheinen erst, wenn die App sie als live erkennt. */
    const PW = s.power;
    if (PW && PW.valid) {
      if (PW.cpu_live)
        cells.push(`<div class="sensor"><span>Leistung gesamt</span>
          <b>${num(PW.total_w, 0)}<em>W</em></b></div>`);
      cells.push(`<div class="sensor"><span>Grafik (GPU)</span>
        <b>${num(PW.gpu_w, 0)}<em>W</em></b></div>`);
      if (PW.cpu_live)
        cells.push(`<div class="sensor"><span>Prozessor + SoC</span>
          <b>${num(PW.cpu_w, 0)}<em>W</em></b></div>`);
      cells.push(`<div class="sensor"><span>Grafikspeicher</span>
        <b>${num(PW.mem_w, 0)}<em>W</em></b></div>`);
    } else if (L.soc_power_w) {
      cells.push(`<div class="sensor"><span>Stromverbrauch Hauptchip</span>
        <b>${num(L.soc_power_w, 1)}<em>W</em></b></div>`);
    }
    if (fps !== null)
      cells.push(`<div class="sensor"><span>Bilder pro Sekunde</span>
        <b>${Math.round(fps)}<em>fps</em></b></div>`);
    $("#sensor-grid").innerHTML = cells.join("") ||
      `<p class="muted">Keine weiteren Sensoren verfügbar.</p>`;
    updateProbeLiveState();

    /* Regelzustand */
    txt("#c-avg", C.avg_temp_c10 >= 0 ? `${num(C.avg_temp_c10 / 10, 1)} °C` : "--");
    const tr = C.trend_c100;
    txt("#c-trend", Math.abs(tr) < 15 ? "stabil"
                  : tr > 0 ? `steigend (+${num(tr / 100, 2)} °C)`
                           : `fallend (${num(tr / 100, 2)} °C)`);
    txt("#c-tduty", `${F.target_duty_pct} %`);
    txt("#c-rduty", F.measured_valid ? `${F.measured_duty_pct} %` : "--");
    txt("#c-thr", F.threshold_c ? `${F.threshold_c} °C`
                                : (F.automatic ? "--" : "nicht gesetzt"));
    txt("#c-safety", C.safety_active ? "aktiv" : "aus");

    state.fanDiagHist.push({
      target: Number.isFinite(F.target_duty_pct) ? F.target_duty_pct : null,
      measured: F.measured_valid ? F.measured_duty_pct : null
    });
    if (state.fanDiagHist.length > 60) state.fanDiagHist.shift();

    /* Erweiterte Lüfterdiagnose: Soll/Ist-Differenz, wirksames Ziel,
       Regelmodus und erkennbare Clamp-Lage. */
    const clampText = (() => {
      if (C.safety_active) return "Notfallmodus aktiv (Sicherheitsgrenze)";
      if (!F.automatic) return Number.isFinite(F.threshold_c)
        ? `manueller Wert ${F.threshold_c} °C`
        : "manueller Wert ohne Rückmeldung";
      /* Die Schwelle der Konsole, nicht das Ziel: unten der Anschlag der App, oben die Ruhelage
         (Ziel + 10 °C, mindestens 80, höchstens 91), in der der Lüfter nichts zu tun hat. */
      const minT = state.cfg && Number(state.cfg.threshold_min_c);
      const maxT = state.cfg && Number(state.cfg.threshold_max_c);
      const goal = Number.isFinite(C.effective_target_c) ? C.effective_target_c : C.target_temp_c;
      if (Number.isFinite(F.threshold_c) && Number.isFinite(minT) && Number.isFinite(maxT) && Number.isFinite(goal)) {
        const restT = Math.min(maxT, Math.max(80, goal + 10));
        if (F.threshold_c <= minT) return `unterer Anschlag (${minT} °C)`;
        if (F.threshold_c >= restT) return `oberer Anschlag (${restT} °C)`;
        return `im Regelbereich (${minT}-${restT} °C)`;
      }
      return "Automatik aktiv";
    })();

    txt("#f-mode", F.automatic ? "Automatik" : "Beobachten");
    txt("#f-availability", F.available ? "bereit" : "nicht verfügbar");
    txt("#f-effective-target",
      Number.isFinite(C.effective_target_c)
        ? `${C.effective_target_c} °C`
        : `${C.target_temp_c} °C`);
    txt("#f-rule", (C.active_game_rule && C.active_game_rule !== "-")
      ? C.active_game_rule : "keine");
    txt("#f-delta", F.measured_valid
      ? `${(F.measured_duty_pct - F.target_duty_pct) >= 0 ? "+" : ""}`
        + `${num(F.measured_duty_pct - F.target_duty_pct, 1)} %`
      : "--");
    txt("#f-raw", F.measured_valid
      ? `${F.measured_duty_raw} (0x${(F.measured_duty_raw >>> 0).toString(16).toUpperCase()})`
      : "--");
    txt("#f-clamp", clampText);
    txt("#f-profile", `${C.profile || "--"} / ${C.samples || 0}`);

    const fanTarget = $("#fan-balance-target");
    if (fanTarget)
      fanTarget.style.width = `${Math.max(0, Math.min(100, F.target_duty_pct || 0))}%`;
    const fanMeasured = $("#fan-balance-measured");
    if (fanMeasured)
      fanMeasured.style.width = `${Math.max(0, Math.min(100,
        F.measured_valid ? F.measured_duty_pct : 0))}%`;

    const miniTarget = state.fanDiagHist.map((r) => r.target);
    const miniMeasured = state.fanDiagHist.map((r) => r.measured);
    const fLoHi = niceRange([...miniTarget, ...miniMeasured], 4, 0, 100);
    const elMiniTarget = $("#fan-mini-target");
    const elMiniMeasured = $("#fan-mini-measured");
    if (elMiniTarget) elMiniTarget.setAttribute("d", miniPath(miniTarget, fLoHi[0], fLoHi[1]));
    if (elMiniMeasured) elMiniMeasured.setAttribute("d", miniPath(miniMeasured, fLoHi[0], fLoHi[1]));
    const dt = firstLastDelta(miniTarget);
    const dm = firstLastDelta(miniMeasured);
    const fmtTrend = (v) => v === null ? "--" : `${v >= 0 ? "+" : ""}${num(v, 1)} %`;
    txt("#fan-mini-meta", `60s-Trend Soll ${fmtTrend(dt)} · Ist ${fmtTrend(dm)}`);

    const adviceEl = $("#fan-diag-advice");
    if (adviceEl) {
      const delta = F.measured_valid ? (F.measured_duty_pct - F.target_duty_pct) : null;
      let sev = "ok";
      let msg = "Regelung wirkt stabil. Keine auffällige Soll-Ist-Abweichung.";
      let primaryStep = "Keine Aktion nötig. Werte weiter normal beobachten.";
      const optionalSteps = [];

      if (!F.available) {
        sev = "hot";
        msg = "Lüfteradapter nicht verfügbar: Es wird nur beobachtet. Payload erneut laden und Adapterstatus prüfen.";
        primaryStep = "Payload neu starten und danach Adapterstatus erneut prüfen.";
        optionalSteps.push("Bis zur Freigabe keine Automatik erzwingen, nur beobachten.");
      } else if (!F.measured_valid) {
        sev = "warn";
        msg = "Keine gültige Ist-Drehzahl. Bitte 20-30 Sekunden laufen lassen und erneut prüfen.";
        primaryStep = "30 Sekunden warten und erneut auf Soll/Ist schauen.";
        optionalSteps.push("Wenn weiter kein Ist-Wert kommt, Payload neu laden.");
      } else if (C.safety_active) {
        sev = "hot";
        msg = "Sicherheitsmodus aktiv: Temperatur priorisiert, Soll/Ist-Abweichung ist in diesem Zustand erwartbar.";
        primaryStep = "Konsole freier belüften und Last kurz reduzieren.";
        optionalSteps.push("Bei Bedarf Zieltemperatur um 1-2 °C senken.");
      } else if (delta !== null && Math.abs(delta) >= 15) {
        sev = "warn";
        msg = delta > 0
          ? `Ist liegt deutlich über Soll (${num(delta, 1)} %): Die PS5 priorisiert vermutlich eigene Schutz-/Lastpfade.`
          : `Ist liegt deutlich unter Soll (${num(delta, 1)} %): Regelung oder Rückmeldung noch im Nachlauf, Verlauf weiter beobachten.`;
        if (delta > 0) {
          primaryStep = "60 Sekunden unter ähnlicher Last beobachten.";
          optionalSteps.push("Wenn gleichzeitig die Temperatur steigt, Zieltemperatur um 1 °C senken.");
        } else {
          primaryStep = "60 Sekunden warten, bis sich Soll und Ist angleichen.";
          optionalSteps.push("Wenn Abweichung bleibt, Payload neu laden und erneut prüfen.");
        }
      } else if (delta !== null && Math.abs(delta) >= 8) {
        sev = "warn";
        msg = `Mittlere Soll-Ist-Abweichung (${num(delta, 1)} %). Bei Lastwechsel meist normal, sollte sich binnen ~60 s beruhigen.`;
        primaryStep = "Verlauf eine Minute beobachten, ohne Profilwechsel.";
      }

      if (F.automatic && clampText.startsWith("oberer Anschlag")) {
        msg += " Automatik ist am oberen Anschlag; bei steigender Temperatur Zieltemperatur etwas senken.";
        optionalSteps.push("Bei dauerhaft oberem Anschlag Zieltemperatur schrittweise (-1 °C) anpassen.");
      }
      if (F.automatic && clampText.startsWith("unterer Anschlag")) {
        msg += " Automatik ist am unteren Anschlag; derzeit ist zusätzliche Entlastung kaum nötig.";
        optionalSteps.push("Wenn Lautstärke stört, Zieltemperatur testweise um +1 °C erhöhen.");
      }

      if (dm !== null && dt !== null && Math.abs(dm - dt) >= 10) {
        msg += " Trendhinweis: Soll und Ist driften im 60s-Fenster sichtbar auseinander.";
        optionalSteps.push("2-3 Minuten unter gleicher Last weiter beobachten, um Lastwechsel als Ursache auszuschließen.");
      }

      if (C.active_game_rule && C.active_game_rule !== "-")
        optionalSteps.push(`Aktive Spielregel prüfen: ${C.active_game_rule}.`);

      const uniqOptional = [...new Set(optionalSteps)];
      const numberedSteps = [`1. Erstmaßnahme: ${primaryStep}`];
      uniqOptional.forEach((step, idx) => {
        numberedSteps.push(`${idx + 2}. Optional: ${step}`);
      });

      adviceEl.classList.remove("ok", "warn", "hot");
      adviceEl.classList.add("diag-advice", sev);
      const headTag = sev === "hot"
        ? "<span class=\"reason-tag hot\">ROT</span>"
        : sev === "warn"
          ? "<span class=\"reason-tag warn\">GELB</span>"
          : "<span class=\"reason-tag ok\">OK</span>";
      const lines = [
        `${headTag} Diagnose: ${esc(msg)}`,
        `<span class=\"reason-tag ${sev === "hot" ? "hot" : "warn"}\">1</span> ${esc(numberedSteps[0].replace(/^1\.\s*/, ""))}`
      ];
      for (let i = 1; i < numberedSteps.length; i++) {
        lines.push(`<span class=\"reason-tag info\">${i + 1}</span> ${esc(numberedSteps[i].replace(/^\d+\.\s*/, ""))}`);
      }
      adviceEl.innerHTML = lines.join("<br>");

      if (state.lastFanDiagLevel !== sev) {
        if (state.lastFanDiagLevel !== null)
          pushDiagEvent(sev, "fan", "Lüfterdiagnose gewechselt", msg,
            state.lastFanDiagLevel, sev);
        state.lastFanDiagLevel = sev;
      }
    }

    const liveHealth = $("#health-live-list");
    if (liveHealth) {
      const warn = [];
      if (s.warnings && s.warnings.cpu) warn.push("CPU");
      if (s.warnings && s.warnings.soc) warn.push("SoC");

      const targetRef = Number.isFinite(C.effective_target_c)
        ? C.effective_target_c : C.target_temp_c;
      const hotNow = Number(s.temperatures && s.temperatures.hottest_c);
      const sensorReady = s.adapters && s.adapters.sensors === "ready";
      const fanReady = !!F.available;
      let ampel = "<span class=\"badge ok\">GRÜN</span>";
      let ampelLevel = "ok";
      const ampelGruende = [];
      if (C.safety_active || warn.length) {
        ampel = "<span class=\"badge hot\">ROT</span>";
        ampelLevel = "hot";
        if (C.safety_active) ampelGruende.push("Sicherheitsmodus aktiv");
        if (warn.length) ampelGruende.push(`Warnflag aktiv (${warn.join(" + ")})`);
      } else {
        if (!sensorReady) ampelGruende.push("Sensoradapter nicht bereit");
        if (!fanReady) ampelGruende.push("Lüfteradapter nicht bereit");
        if (Number.isFinite(hotNow) && Number.isFinite(targetRef) && hotNow >= targetRef + 5)
          ampelGruende.push(`Wärmster Sensor ${num(hotNow, 1)} °C (>= Ziel + 5 °C)`);
        if (ampelGruende.length) {
          ampel = "<span class=\"badge warn\">GELB</span>";
          ampelLevel = "warn";
        }
      }

      let ampelGrund = "1. <span class=\"reason-tag ok\">OK</span> Hauptauslöser: keiner"
        + "<br>2. <span class=\"reason-tag ok\">OK</span> Sekundär: keine";
      if (ampelGruende.length) {
        const ordered = [];
        const pushIf = (needle) => {
          const hit = ampelGruende.find((g) => g.indexOf(needle) >= 0);
          if (hit && !ordered.includes(hit)) ordered.push(hit);
        };
        /* Reihenfolge nach Dringlichkeit statt Fundreihenfolge. */
        pushIf("Sicherheitsmodus aktiv");
        pushIf("Warnflag aktiv");
        pushIf("Wärmster Sensor");
        pushIf("Sensoradapter nicht bereit");
        pushIf("Lüfteradapter nicht bereit");
        ampelGruende.forEach((g) => { if (!ordered.includes(g)) ordered.push(g); });
        const secondary = ordered.slice(1);
        const mainTag = ampel.indexOf("badge hot") >= 0
          ? "<span class=\"reason-tag hot\">ROT</span>"
          : "<span class=\"reason-tag warn\">GELB</span>";
        ampelGrund = `1. ${mainTag} Hauptauslöser: ${esc(ordered[0])}`;
        if (secondary.length) {
          ampelGrund += `<br>2. <span class=\"reason-tag warn\">INFO</span> `
            + `Sekundär: ${esc(secondary.join("; "))}`;
        } else {
          ampelGrund += `<br>2. <span class=\"reason-tag ok\">OK</span> Sekundär: keine`;
        }
      }

      const liveRows = [
        ["Ampelstatus", ampel],
        ["Ampelgrund", ampelGrund],
        ["Schwellen", "Grün: keine Warnung; Gelb: >= Ziel+5 °C oder Adapter fehlt; Rot: Warnflag oder Sicherheitsmodus"],
        ["Thermische Warnung", warn.length ? `aktiv (${warn.join(" + ")})` : "keine"],
        ["Sicherheitsmodus", C.safety_active ? "aktiv" : "aus"],
        ["Lüftersteuerung", F.available ? (F.automatic ? "automatisch" : "nur beobachten")
                 : "nicht verfügbar"],
        ["Sensoradapter", (s.adapters && s.adapters.sensors) || "--"],
        ["Lüfteradapter", (s.adapters && s.adapters.fan) || "--"],
        ["Regelziel", Number.isFinite(C.effective_target_c)
          ? `${C.effective_target_c} °C` : `${C.target_temp_c} °C`],
        ["Lüfter Soll / Ist", F.measured_valid
          ? `${F.target_duty_pct} % / ${F.measured_duty_pct} %`
          : `${F.target_duty_pct} % / --`]
      ];

      if (s.messages && s.messages.sensors)
        liveRows.push(["Sensorhinweis", s.messages.sensors]);
      if (s.messages && s.messages.fan)
        liveRows.push(["Lüfterhinweis", s.messages.fan]);

      /* Only the two rows built above from fixed markup carry HTML; the rest
         includes the console's own message strings. */
      liveHealth.innerHTML = liveRows
        .map(([k, v]) => {
          if (k === "Ampelstatus" || k === "Ampelgrund")
            return `<div><span>${k}</span><b class="${k === "Ampelgrund" ? "multiline" : ""}">${v}</b></div>`;
          return `<div><span>${esc(k)}</span><b>${esc(v)}</b></div>`;
        }).join("");

      const warnNote = $("#warning-countdown-note");
      if (warnNote) {
        if (C.warning_active && Number.isFinite(C.warning_countdown_s) && C.warning_countdown_s >= 0) {
          warnNote.classList.remove("ok", "hot");
          warnNote.classList.add("warn", "diag-advice");
          warnNote.textContent = `Warn-Countdown aktiv: noch ${C.warning_countdown_s}s bis Eskalationsempfehlung.`;
        } else {
          warnNote.classList.remove("warn", "hot");
          warnNote.classList.add("ok", "diag-advice");
          warnNote.textContent = "Warn-Countdown inaktiv. Temperatur aktuell unter Warnschwelle.";
        }
      }

      const lbState = $("#lightbar-state");
      const lbColor = $("#lightbar-color");
      if (lbState && lbColor) {
        const lb = C.lightbar || {};
        const lbl = lb.state_label === "hot" ? "ROT"
          : lb.state_label === "warn" ? "GELB"
          : lb.state_label === "ok" ? "GRÜN"
          : "--";
        lbState.textContent = lb.enabled ? "aktiv" : "aus";
        lbColor.textContent = lb.enabled
          ? `${lbl}${lb.supported ? "" : " (best effort)"}`
          : "nicht aktiv";
      }

      if (state.lastAmpelLevel !== ampelLevel) {
        if (state.lastAmpelLevel !== null)
          pushDiagEvent(ampelLevel, "ampel", "Ampelstatus gewechselt",
            ampelGruende.length ? ampelGruende[0] : "kein Auslöser aktiv",
            state.lastAmpelLevel, ampelLevel);
        state.lastAmpelLevel = ampelLevel;
      }
      renderDiagEvents();
    }

    /* Verbindung */
    const conn = $("#conn");
    conn.classList.add("on"); conn.classList.remove("off");
    document.body.classList.remove("offline");
    txt("#conn-text", F.automatic ? "Verbunden · Automatik" : "Verbunden · Beobachten");

    if (!state.targetDirty && state.cfg)
      $("#auto-mode").checked = F.automatic;
  };

  /* ── Konfiguration ──────────────────────────────────────────────── */

  const loadConfig = async () => {
    const c = await api("/api/v1/config");
    state.cfg = c;

    if (!state.targetDirty) {
      $("#target-range").min = c.target_min_c ?? 60;
      $("#target-range").max = c.target_max_c ?? 91;
      $("#target-range").value = c.target_temp_c;
      txt("#target-out", `${c.target_temp_c} °C`);
      updateTargetHint(c.target_temp_c);
    }

    $$("input[name=profile]").forEach((r) => { r.checked = r.value === c.profile; });
    $("#auto-mode").checked = c.mode === "automatic";
    /* Fehlt das Feld (ältere App), gilt die Vorgabe „an". */
    if ($("#mic-button-status")) $("#mic-button-status").checked = c.ps_button_status !== 0;
    if ($("#gm-cache-on")) $("#gm-cache-on").checked = !!c.library_cache;

    /* Regelbereich, Ruhezone, Intervall, Glättung und Schrittweite haben
       keine Felder mehr — sie ergeben sich aus der Betriebsart. Die Werte
       stehen weiterhin in /api/v1/config, falls jemand nachsehen will. */
    $("#s-safe").value = c.safety_temp_c;
    $("#s-wcpu").value = c.warning_cpu_c;
    $("#s-port").value = c.http_port;
    if ($("#s-bind")) $("#s-bind").value = c.bind_address || "0.0.0.0";
    if ($("#s-warn-countdown")) $("#s-warn-countdown").value = c.warning_countdown_s ?? 120;
    if ($("#s-retention-days")) $("#s-retention-days").value = c.telemetry_retention_days ?? 90;
    if ($("#s-lightbar-enabled")) $("#s-lightbar-enabled").checked = !!c.lightbar_enabled;
    if ($("#s-lightbar-warn")) $("#s-lightbar-warn").value = c.lightbar_warn_c ?? 70;
    if ($("#s-lightbar-hot")) $("#s-lightbar-hot").value = c.lightbar_hot_c ?? 76;
    if ($("#s-fan-reapply")) $("#s-fan-reapply").value = c.fan_reapply_sec ?? 15;
    renderCurveEditor(c.curve);

    txt("#eff-band", Number.isFinite(c.control_band_c) ? `${c.control_band_c} °C` : "--");
    txt("#eff-deadband", Number.isFinite(c.deadband_c) ? `±${c.deadband_c} °C` : "--");
    txt("#eff-interval", Number.isFinite(c.control_interval_s) ? `${c.control_interval_s} s` : "--");
    txt("#eff-window", Number.isFinite(c.average_window_s) ? `${c.average_window_s} s` : "--");
    txt("#eff-step", Number.isFinite(c.max_step_pct) ? `${c.max_step_pct} %` : "--");

    const directInput = $("#s-direct-threshold");
    if (directInput) {
      if (c.mode === "automatic") {
        directInput.min = String(c.target_min_c ?? 60);
        directInput.max = String(c.target_max_c ?? 91);
        directInput.value = String(c.target_temp_c ?? 66);
      } else {
        directInput.min = String(c.threshold_min_c ?? 45);
        directInput.max = String(c.threshold_max_c ?? 91);
        directInput.value = String(c.fan_threshold_c ?? 65);
      }
    }
    const directNote = $("#s-direct-threshold-note");
    if (directNote) {
      directNote.textContent = c.mode === "automatic"
        ? `Automatik: setzt die Zieltemperatur (${c.target_min_c ?? 60}-${c.target_max_c ?? 91} °C, bis 72 °C empfohlen).`
        : `Beobachten: setzt eine feste Lüfterschwelle (${c.threshold_min_c ?? 45}-${c.threshold_max_c ?? 91} °C).`;
    }

    /* Ohne diese Zeile standen die drei Schalter immer auf „aus", ganz gleich
       was gespeichert war — und weil „Übernehmen" den Wert aus ihnen
       zusammensetzt, hätte ein Klick alles abgeschaltet, was eingeschaltet
       war. Die Funktion existierte, sie wurde nur nie aufgerufen. */
    showProbes(c.probe_mask ?? 0);
    /* A timed revert still pending on the console is this page's diagnosis
       session too, e.g. after a reload or on a second device. */
    syncProbeDiagSession(c);
  };

  /* The first load is the one every form depends on. A single failed attempt
     (console busy, slow Wi-Fi) must not leave the page with empty fields, so
     it is repeated with a growing pause until it works. */
  let cfgRetryTimer = 0;
  let cfgFailShown = false;
  const loadConfigUntilOk = (wait = 2000) => {
    clearTimeout(cfgRetryTimer);
    cfgRetryTimer = 0;
    if (state.cfg) return;
    const again = () => { cfgRetryTimer = setTimeout(() => loadConfigUntilOk(Math.min(wait * 2, 15000)), wait); };
    if (document.hidden) { again(); return; }
    loadConfig().catch((e) => {
      if (!cfgFailShown) {
        cfgFailShown = true;
        toast(`Einstellungen noch nicht geladen (${String(e.message).replace(/\.$/, "")}). Es wird weiter versucht.`, "error");
      }
      again();
    });
  };

  const updateTargetHint = (v) => {
    /* Über 72 °C (bis 78 °C seit 03.10.2026, bis 91 °C seit 07.10.2026): sehr leise, aber
       die Konsole läuft heiß. Die Notfallgrenze liegt mindestens vier Grad über dem Ziel
       (die Einstellungen heben sie von selbst an), die Warnschwelle der App bleibt, wo sie
       ist, und liegt bei hohen Zielen darunter. Bei 91 °C ist das der Wert, den die Konsole
       selbst einstellt. */
    const cfgNum = (k, dflt) => (state.cfg && Number.isFinite(state.cfg[k]) ? state.cfg[k] : dflt);
    const warnC = Math.max(cfgNum("warning_cpu_c", 80), v + 2);   /* die Warnschwelle geht mit dem Ziel mit (Ziel + 2) */
    const safeC = Math.max(cfgNum("safety_temp_c", 78), v + 4);
    const hint = v <= 62 ? "Sehr kühl — der Lüfter ist dauerhaft gut hörbar."
               : v <= 65 ? "Kühl — spürbar mehr Lüftergeräusch."
               : v <= 68 ? "Empfohlen — leiser Betrieb bei sicheren Temperaturen."
               : v <= 72 ? "Leise — die Konsole läuft wärmer, bleibt aber im grünen Bereich."
               : v >= 91 ? `So leise wie ohne die App (91 °C stellt die Konsole selbst ein), aber heiß — die Notfallgrenze liegt bei ${safeC} °C, die Warnschwelle bei ${warnC} °C.`
                         : `Sehr leise, aber heiß — die Notfallgrenze liegt bei ${safeC} °C, die Warnschwelle bei ${warnC} °C.`;
    txt("#target-hint", hint);
  };

  /* ── Langzeitverlauf ────────────────────────────────────────────── */

  const loadHistory = async () => {
    let h;
    try { h = await api("/api/v1/history"); } catch { return; }

    const n = h.count || 0;
    txt("#hist-count", n ? `${num(n)} Messwerte` : "noch keine");
    txt("#peak-cpu", h.peaks.cpu_c   >= 0 ? `${h.peaks.cpu_c} °C`   : "--");
    txt("#peak-soc", h.peaks.soc_c   >= 0 ? `${h.peaks.soc_c} °C`   : "--");
    txt("#peak-fan", h.peaks.fan_pct >= 0 ? `${h.peaks.fan_pct} %`  : "--");

    if (!n) {
      txt("#hist-range", "noch keine Aufzeichnung");
      /* After a reset the old curve has to go as well, and the tooltip data
         with it; only the numbers were cleared before. */
      ["#line-hcpu", "#area-hcpu", "#line-hsoc"].forEach((sel) => {
        const el = $(sel);
        if (el) el.setAttribute("d", "");
      });
      state.hist2 = null;
      return;
    }

    /* -1 markiert eine Lücke; buildPath zeichnet dort nicht durch. */
    const clean = (a) => a.map((v) => (v < 0 ? null : v));
    const cpu = clean(h.cpu_c), soc = clean(h.soc_c);
    state.hist2 = { t: h.t_ms, cpu, soc, fan: clean(h.fan_pct) };

    const span = (h.t_ms[n - 1] - h.t_ms[0]) / 3600000;
    txt("#hist-range", span >= 1 ? `letzte ${num(span, 1)} Stunden`
                                 : `letzte ${Math.round(span * 60)} Minuten`);

    const [lo, hi] = niceRange([...cpu, ...soc], 6, 40, 80);
    drawGrid("#grid-hist", lo, hi, "#axis-hist", "°");
    const dc = buildPath(cpu, lo, hi);
    $("#line-hcpu").setAttribute("d", dc);
    $("#area-hcpu").setAttribute("d", areaFrom(dc));
    $("#line-hsoc").setAttribute("d", buildPath(soc, lo, hi));
  };

  /* ── Spielprofile ───────────────────────────────────────────────── */

  const profileLabel = { comfort: "Leise", balanced: "Ausgewogen", cool: "Kühl" };

  const loadRules = async () => {
    let d;
    try { d = await api("/api/v1/games"); } catch { return; }
    state.rules = d.rules || [];

    /* Title name and id are free text: a game's own metadata, or a profile
       file imported from anywhere. Both go through esc(), also inside the
       data-remove attribute that the click handler below reads back. */
    $("#rule-list").innerHTML = state.rules.length ? state.rules.map((r) => `
      <div class="rule${r.title_id === d.active ? " live" : ""}">
        <div><b>${esc(r.title_name || r.title_id)}</b>
          <small>${esc(r.target_temp_c)} °C · ${esc(profileLabel[r.profile] || r.profile)}
            · ${esc(r.title_id)}</small></div>
        <button data-remove="${esc(r.title_id)}">Entfernen</button>
      </div>`).join("")
      : `<div class="empty-state"><img src="/img/empty-profiles.png" alt="">
          <p class="muted">Noch keine Spielprofile gespeichert.</p></div>`;

    $$("#rule-list [data-remove]").forEach((b) =>
      b.addEventListener("click", async () => {
        try {
          await api("/api/v1/games", { method: "PUT",
            body: JSON.stringify({ title_id: b.dataset.remove, remove: true }) });
          toast("Spielprofil entfernt.");
          loadRules();
        } catch (e) { toast(e.message, "error"); }
      }));

    /* Der Merken-Knopf braucht ein laufendes Spiel. */
    const g = state.status && state.status.game;
    const box = $("#rule-current");
    if (g && g.title_id) {
      box.classList.remove("is-hidden");
      $("#rule-nogame").classList.add("is-hidden");
      txt("#rule-game", g.title_name || g.title_id);
      const known = state.rules.find((r) => r.title_id === g.title_id);
      txt("#rule-hint", known
        ? `gespeichert: ${known.target_temp_c} °C · ${profileLabel[known.profile]}`
        : "noch kein Profil gespeichert");
    } else {
      box.classList.add("is-hidden");
      $("#rule-nogame").classList.remove("is-hidden");
    }
  };

  /* ── Payloads (seit 1.46.0 eine eigene Seite) ───────────────────────
     Drei Listen: was gerade läuft (Prozesse, „Beenden"), was im Ordner
     /data/PS5-Cooling-Center/payloads liegt, und was auf USB-Sticks liegt
     (Hauptverzeichnis und Ordner „payloads"). „Starten" schickt die Datei an
     den Payload-Lader der Konsole. Die Seite nennt der Konsole nie einen
     Pfad, nur Ort, Ordner und Dateiname aus deren eigener Liste — was sie
     davon annimmt, prüft payloads.c noch einmal. */
  const PL_SVG = {
    box:   '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 3 3.5 7.5v9L12 21l8.5-4.5v-9L12 3z"/><path d="M3.5 7.5 12 12l8.5-4.5M12 12v9"/></svg>',
    play:  '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M7 4.5v15l12-7.5z"/></svg>',
    trash: '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 7h16M10 11v6M14 11v6M6 7l1 12a1 1 0 0 0 1 1h8a1 1 0 0 0 1-1l1-12M9 7V4h6v3"/></svg>',
    usb:   '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 3v13"/><circle cx="12" cy="19" r="2"/><path d="M12 9l-4-2v3M12 12l4-2v3"/><rect x="14" y="4.5" width="4" height="3" rx=".5"/><rect x="5.5" y="9" width="4" height="3" rx=".5"/></svg>',
    copy:  '<svg viewBox="0 0 24 24" aria-hidden="true"><rect x="8" y="8" width="12" height="12" rx="2"/><path d="M16 8V6a2 2 0 0 0-2-2H6a2 2 0 0 0-2 2v8a2 2 0 0 0 2 2h2"/></svg>'
  };

  const plSize = (b) => {
    if (!(b > 0)) return "0 B";
    if (b < 1024) return `${b} B`;
    if (b < 1024 * 1024) return `${Math.round(b / 1024)} KB`;
    return `${num(b / 1048576, 1)} MB`;
  };
  const plDate = (t) => (t > 0
    ? new Date(t * 1000).toLocaleDateString(LOCALE, { day: "2-digit", month: "2-digit", year: "numeric" })
    : "");
  /* „kstuff_v1.13_dr_Beta4.elf" → Titel „kstuff v1.13 dr Beta4", Version „1.13".
     Eine Version erkennt nur, wer Punkte hat; „PS5" ist keine. */
  const plTitle = (name) => name.replace(/\.elf$/i, "").replace(/_/g, " ");
  const plVersion = (name) => {
    const m = /(?:^|[^A-Za-z0-9])v?(\d+(?:\.\d+)+[A-Za-z0-9]*)/.exec(name.replace(/\.elf$/i, ""));
    return m ? m[1] : "";
  };
  const plCount = (n, one, many) => (n === 1 ? `1 ${one}` : `${n} ${many}`);

  /* Eine Karte; alles, was von außen kommt, geht durch esc(). */
  const plCard = ({ icon, title, ver, bad, line, actions, cls }) => `
    <div class="pl-card${cls ? ` ${cls}` : ""}${bad ? " bad" : ""}">
      <div class="pl-ico">${icon}</div>
      <div class="pl-info">
        <b class="pl-name" title="${esc(title)}">${esc(title)}</b>
        ${bad ? `<span class="pl-badtxt">${esc(bad)}</span>`
              : ver ? `<span class="pl-ver">v${esc(ver)}</span>` : ""}
        <small class="pl-path" title="${esc(line)}">${esc(line)}</small>
      </div>
      <div class="pl-actions">${actions}</div>
    </div>`;

  const plFileLine = (path, f) => [path, plSize(f.size), plDate(f.mtime)].filter(Boolean).join(" · ");

  let plLast = { running: "", files: "" };

  /* Laufende Payloads (Prozesse) */
  const loadPayloads = async (force) => {
    let d;
    try { d = await api("/api/v1/payloads"); } catch { return; }
    const list = d.payloads || [];
    const sig = JSON.stringify(list);
    if (!force && sig === plLast.running) return;
    plLast.running = sig;

    txt("#pl-count-running", list.length === 1 ? "1 läuft" : `${list.length} laufen`);
    /* Process names come from whatever runs on the console. */
    $("#pl-list").innerHTML = list.length ? list.map((p) => `
      <div class="pl-card${p.is_self ? " self" : ""}">
        <div class="pl-ico">${PL_SVG.box}</div>
        <div class="pl-info">
          <b class="pl-name" title="${esc(p.name)}">${esc(p.name)}</b>
          <small class="pl-path">Nr. ${esc(p.pid)}${p.memory_mb ? ` · ${esc(p.memory_mb)} MB Speicher` : ""}${p.is_self ? " · diese App" : ""}</small>
        </div>
        <div class="pl-actions">${p.is_self
          ? `<span class="pl-live">läuft</span>`
          : `<button class="btn pl-act" data-kill="${esc(p.pid)}" data-name="${esc(p.name)}">Beenden</button>`}</div>
      </div>`).join("")
      : `<p class="muted">Keine weiteren Zusatzprogramme gefunden.</p>`;

    $$("#pl-list [data-kill]").forEach((b) =>
      armConfirm(b, "Wirklich? Nochmal klicken", async () => {
        try {
          await api("/api/v1/payloads/kill", { method: "POST",
            body: JSON.stringify({ pid: Number(b.dataset.kill) }) });
          toast(`${b.dataset.name} beendet.`);
          loadPayloads(true);
        } catch (e) { toast(e.message, "error"); }
      }, `${b.dataset.name} wird sofort angehalten — alles, was dieses Programm `
       + `noch nicht gespeichert hat, geht verloren.`));
  };

  /* Gespeicherte Payloads und USB-Speicher */
  const renderPayloadFiles = (d) => {
    const internal = Array.isArray(d.internal) ? d.internal : [];
    plInternal = internal;
    if (plEdit) plRenderEdit();
    const dir = d.dir || "/data/PS5-Cooling-Center/payloads";
    txt("#pl-dir", dir);
    txt("#pl-count-stored", plCount(internal.length, "Datei", "Dateien"));
    const have = new Map(internal.map((f) => [f.name.toLowerCase(), f]));

    $("#pl-stored").innerHTML = internal.length ? internal.map((f) => plCard({
      icon: PL_SVG.box, title: plTitle(f.name), ver: plVersion(f.name),
      bad: f.valid ? "" : `Nicht startbar: ${f.why || "kein gültiges ELF"}`,
      line: plFileLine(`${dir}/${f.name}`, f),
      actions:
        `<button class="btn primary pl-act" data-pl-start data-src="internal" data-name="${esc(f.name)}"${f.valid ? "" : " disabled"}>${PL_SVG.play}Starten</button>`
        + `<button class="pl-trash" data-pl-del data-name="${esc(f.name)}" title="Aus dem Ordner löschen" aria-label="${esc(f.name)} löschen">${PL_SVG.trash}<span class="pl-trash-ask">Wirklich löschen?</span></button>`
    })).join("")
      : `<p class="muted">${d.dir_exists === false
          ? "Den Ordner gibt es gerade nicht — die App legt ihn beim nächsten Start an."
          : "Noch leer. Lege .elf-Dateien in den Ordner oben (zum Beispiel per FTP) oder kopiere sie von einem USB-Stick."}</p>`;

    const drives = Array.isArray(d.usb) ? d.usb : [];
    const nUsb = drives.reduce((n, u) => n + (u.items ? u.items.length : 0), 0);
    txt("#pl-count-usb", drives.length ? plCount(nUsb, "Datei", "Dateien") : "kein Stick");
    $("#pl-usb").innerHTML = drives.length ? drives.map((u) => {
      const items = Array.isArray(u.items) ? u.items : [];
      const cards = items.map((f) => {
        const where = `${u.mount}/${f.dir ? `${f.dir}/` : ""}${f.name}`;
        const mine = have.get(f.name.toLowerCase());
        const attrs = `data-src="usb" data-mount="${esc(u.mount)}" data-dir="${esc(f.dir)}" data-name="${esc(f.name)}"`;
        return plCard({
          cls: "pl-wide",
          icon: PL_SVG.usb, title: plTitle(f.name), ver: plVersion(f.name),
          bad: f.valid ? "" : `Nicht startbar: ${f.why || "kein gültiges ELF"}`,
          line: plFileLine(where, f),
          actions:
            `<button class="btn primary pl-act" data-pl-start ${attrs}${f.valid ? "" : " disabled"}>${PL_SVG.play}Starten</button>`
            + `<button class="btn pl-act pl-copy" data-pl-copy ${attrs}${f.valid && !mine ? "" : " disabled"}`
            + ` title="Kopiert die Datei in den internen Speicher; das Original bleibt auf dem Stick.">${PL_SVG.copy}`
            + `${mine ? "Schon im internen Speicher" : "In den internen Speicher kopieren"}</button>`
        });
      }).join("");
      return `<h4 class="pl-drive">${PL_SVG.usb}<span>${esc(u.label)}</span><code>${esc(u.mount)}</code></h4>`
        + (items.length ? `<div class="pl-grid">${cards}</div>`
          : `<p class="muted">Keine .elf-Dateien im Hauptverzeichnis oder im Ordner „payloads".</p>`);
    }).join("")
      : `<p class="muted">Kein USB-Speicher gefunden. Stick einstecken und „Aktualisieren" drücken.</p>`;

    /* Zwei Klicks wie bei armConfirm(), aber der Knopf zeigt ein Symbol: Dessen
       Beschriftung würde armConfirm() beim Entschärfen durch Leere ersetzen.
       Hier zeigt die Klasse „armed" den Fragetext, und er verschwindet von
       selbst nach acht Sekunden. */
    $$("#pl-stored [data-pl-del]").forEach((b) => {
      let timer = 0;
      const disarm = () => {
        clearTimeout(timer);
        timer = 0;
        b.classList.remove("armed");
        b.title = "Aus dem Ordner löschen";
      };
      b.addEventListener("click", async () => {
        if (!timer) {
          b.classList.add("armed");
          b.title = "Noch einmal klicken, um zu löschen";
          timer = setTimeout(disarm, 8000);
          return;
        }
        disarm();
        try {
          await plPost("delete", { name: b.dataset.name });
          toast(`„${b.dataset.name}" gelöscht.`);
          await loadPayloadFiles(true);
        } catch (e) { toast(e.message, "error"); }
      });
    });
  };

  const loadPayloadFiles = async (force) => {
    let d;
    try {
      d = await api("/api/v1/payload-files");
    } catch (e) {
      const msg = `<p class="muted">Die Liste ließ sich nicht laden (${esc(e.message)}).</p>`;
      $("#pl-stored").innerHTML = msg;
      $("#pl-usb").innerHTML = msg;
      plLast.files = "";
      return;
    }
    const sig = JSON.stringify(d);
    if (!force && sig === plLast.files) return;
    plLast.files = sig;
    renderPayloadFiles(d);
  };

  const loadPayloadsPage = () => { loadPayloads(); loadPayloadFiles(); loadProfiles(); };

  const plPost = (route, body, timeoutMs) =>
    api(`/api/v1/payload-files/${route}`, { method: "POST", body: JSON.stringify(body), timeoutMs });
  const plBody = (b) => ({
    source: b.dataset.src || "internal", mount: b.dataset.mount || "",
    dir: b.dataset.dir || "", name: b.dataset.name
  });

  /* Der Knopf bleibt kurz gesperrt: Ein Doppelklick soll nicht zweimal
     starten (die Konsole weist den zweiten ohnehin ab). */
  const plStart = async (b) => {
    const label = b.innerHTML;
    b.disabled = true;
    b.textContent = "Wird gesendet …";
    try {
      const r = await plPost("start", plBody(b), 70000);
      const first = String(r.output || "").split("\n").map((x) => x.trim()).filter(Boolean)[0];
      toast(`„${b.dataset.name}" gesendet (${plSize(r.bytes)}).${first ? ` Antwort: ${first.slice(0, 120)}` : ""}`);
      /* Ein Payload, das dauerhaft läuft, steht gleich in der Liste oben. */
      setTimeout(() => loadPayloads(), 1500);
      setTimeout(() => loadPayloads(), 5000);
    } catch (e) {
      /* Startet das Payload diese App selbst neu, reißt die Verbindung ab,
         bevor die Antwort kommt — das ist kein Fehlschlag. */
      toast(e.name === "TypeError"
        ? "Die Verbindung wurde unterbrochen — möglicherweise hat sich die App selbst neu gestartet."
        : e.message, "error");
    } finally {
      setTimeout(() => { b.innerHTML = label; b.disabled = false; }, 2500);
    }
  };

  const plCopy = async (b) => {
    const label = b.innerHTML;
    b.disabled = true;
    b.textContent = "Wird kopiert …";
    try {
      const r = await plPost("copy", plBody(b), 130000);
      toast(`„${b.dataset.name}" liegt jetzt im internen Speicher (${plSize(r.bytes)}).`);
      await loadPayloadFiles(true);
    } catch (e) {
      toast(e.message, "error");
      b.innerHTML = label;
      b.disabled = false;
    }
  };


  /* ── Payload-Profile ─────────────────────────────────────────────────
     Eine Abfolge aus .elf-Dateinamen (Ordner der Konsole) und Pausen „!ms".
     Die Seite schickt das ganze Dokument; payprofiles.c prüft es noch einmal.
     Idee: ps5-payload-manager (itsPLK), hier neu gebaut. */
  let plProf = { startup: "", profiles: [] };
  let plInternal = [];
  let plEdit = null;            /* { id, name, items[], isNew } */
  let plRunTimer = 0;

  const plNewId = () => `p${Date.now().toString(36)}${Math.floor(Math.random() * 1296).toString(36)}`;
  const plIsDelay = (it) => it.charAt(0) === "!";
  const plDelayTxt = (it) => {
    const ms = Number(it.slice(1));
    return ms >= 1000 ? `${num(ms / 1000, ms % 1000 ? 1 : 0)} s Pause` : `${ms} ms Pause`;
  };
  const plSummary = (items) => {
    const files = items.filter((i) => !plIsDelay(i));
    if (!files.length) return "leer";
    const names = files.slice(0, 3).map((n) => plTitle(n)).join(" → ");
    return files.length > 3 ? `${names} → … (${files.length} Payloads)` : names;
  };

  const plProfSave = async (doc) => {
    const r = await api("/api/v1/payload-profiles", { method: "POST", body: JSON.stringify(doc) });
    plProf = { startup: r.startup || "", profiles: Array.isArray(r.profiles) ? r.profiles : [] };
    plRenderProfiles();
  };

  const plRenderProfiles = () => {
    const list = plProf.profiles;
    txt("#pl-count-prof", plCount(list.length, "Profil", "Profile"));
    const box = $("#pl-prof-list");
    if (!box) return;
    const running = plRunState && plRunState.running;
    box.innerHTML = list.length ? list.map((p) => {
      const isStart = p.id === plProf.startup;
      return `
      <div class="pl-card pl-prof${isStart ? " is-startup" : ""}">
        <div class="pl-ico">${PL_SVG.box}</div>
        <div class="pl-info">
          <b class="pl-name" title="${esc(p.name)}">${esc(p.name)}</b>
          ${isStart ? `<span class="pl-startup-tag">Startprofil</span>` : ""}
          <small class="pl-path" title="${esc(plSummary(p.items))}">${esc(plSummary(p.items))}</small>
        </div>
        <div class="pl-actions">
          <button class="btn primary pl-act" data-pp-run="${esc(p.id)}"${running || !p.items.some((i) => !plIsDelay(i)) ? " disabled" : ""}>${PL_SVG.play}Ausführen</button>
          <button class="btn pl-act" data-pp-edit="${esc(p.id)}">Bearbeiten</button>
          <button class="btn pl-act${isStart ? " on" : ""}" data-pp-startup="${esc(p.id)}" title="${isStart ? "Das Startprofil wird beim Start der App nicht mehr ausgeführt." : "Dieses Profil führt die App nach jedem Start von selbst aus."}">${isStart ? "Startprofil aufheben" : "Als Startprofil"}</button>
        </div>
      </div>`;
    }).join("") : `<p class="muted">Noch kein Profil. „Neues Profil“ legt eines an; dort lassen sich auch Payloads vom PC importieren.</p>`;
  };

  let plRunState = null;
  const plRenderRun = (d) => {
    plRunState = d;
    const box = $("#pl-prof-run");
    if (!box) return;
    const res = Array.isArray(d.results) ? d.results : [];
    if (!d.running && !res.length) { box.classList.add("is-hidden"); return; }
    box.classList.remove("is-hidden");
    const head = d.running
      ? `„${esc(d.name)}“ läuft: Schritt ${esc(d.step)} von ${esc(d.total)}${d.startup ? " (Startprofil)" : ""}`
      : `„${esc(d.name)}“ ist fertig.`;
    box.innerHTML = `<p><b>${head}</b>${d.running ? ` <button class="btn pl-act" data-pp-stop type="button">Anhalten</button>` : ""}</p>`
      + (res.length ? `<ul>${res.map((r) => `<li class="${r.ok ? "ok" : "bad"}">${esc(plIsDelay(r.item) ? plDelayTxt(r.item) : r.item)} – ${esc(r.msg)}</li>`).join("")}</ul>` : "");
    plRenderProfiles();
  };
  const plPollRun = async () => {
    clearTimeout(plRunTimer);
    plRunTimer = 0;
    let d;
    try { d = await api("/api/v1/payload-profiles/status"); } catch { return; }
    plRenderRun(d);
    if (d.running) plRunTimer = setTimeout(plPollRun, 1200);
  };

  const loadProfiles = async () => {
    let d;
    try { d = await api("/api/v1/payload-profiles"); } catch { return; }
    plProf = { startup: d.startup || "", profiles: Array.isArray(d.profiles) ? d.profiles : [] };
    plRenderProfiles();
    plPollRun();
  };

  /* Editor */
  const plRenderEdit = () => {
    const box = $("#pl-prof-edit");
    if (!box) return;
    if (!plEdit) { box.classList.add("is-hidden"); box.innerHTML = ""; return; }
    box.classList.remove("is-hidden");
    const have = new Set(plEdit.items.filter((i) => !plIsDelay(i)).map((i) => i.toLowerCase()));
    const opts = plInternal.filter((f) => f.valid && !have.has(f.name.toLowerCase()))
      .map((f) => `<option value="${esc(f.name)}">${esc(plTitle(f.name))}</option>`).join("");
    const rows = plEdit.items.map((it, i) => {
      const missing = !plIsDelay(it) && !plInternal.some((f) => f.name.toLowerCase() === it.toLowerCase());
      return `<li class="pl-ed-row${missing ? " bad" : ""}">
        <span class="pl-ed-n">${i + 1}</span>
        <span class="pl-ed-t">${plIsDelay(it)
          ? `<input type="number" min="1" max="600000" step="100" value="${esc(it.slice(1))}" data-pp-delay="${i}" aria-label="Pause in Millisekunden"> ms Pause`
          : `${esc(plTitle(it))}${missing ? ` <em>(Datei fehlt im Ordner)</em>` : ""}`}</span>
        <button class="btn pl-mini" data-pp-up="${i}" title="Nach oben"${i === 0 ? " disabled" : ""}>↑</button>
        <button class="btn pl-mini" data-pp-down="${i}" title="Nach unten"${i === plEdit.items.length - 1 ? " disabled" : ""}>↓</button>
        <button class="btn pl-mini" data-pp-del="${i}" title="Entfernen">✕</button>
      </li>`;
    }).join("");
    box.innerHTML = `
      <h4>${plEdit.isNew ? "Neues Profil" : "Profil bearbeiten"}</h4>
      <label class="pl-ed-name">Name <input type="text" id="pl-ed-name" maxlength="60" value="${esc(plEdit.name)}"></label>
      <ol class="pl-ed-list">${rows || `<li class="muted">Noch keine Einträge. Unten Payloads hinzufügen oder importieren.</li>`}</ol>
      <div class="pl-ed-add">
        <select id="pl-ed-pick" aria-label="Payload hinzufügen"${opts ? "" : " disabled"}><option value="">${opts ? "Payload aus dem Ordner wählen …" : "Keine weiteren Payloads im Ordner"}</option>${opts}</select>
        <button class="btn" data-pp-add type="button"${opts ? "" : " disabled"}>Hinzufügen</button>
        <button class="btn" data-pp-pause type="button">Pause einfügen</button>
        <label class="btn" for="pl-ed-import">Vom PC importieren und hinzufügen</label>
        <input type="file" id="pl-ed-import" accept=".elf" multiple hidden>
      </div>
      <div class="pl-ed-foot">
        <button class="btn primary" data-pp-save type="button">Speichern</button>
        <button class="btn ghost" data-pp-cancel type="button">Abbrechen</button>
        ${plEdit.isNew ? "" : `<button class="btn pl-ed-delete" data-pp-remove type="button">Profil löschen</button>`}
      </div>`;
  };

  /* Eine Datei in den Ordner der Konsole laden (derselbe Weg wie auf der Seite „Dateien“). */
  const plUpload = (file, dir) => new Promise((resolve, reject) => {
    const x = new XMLHttpRequest();
    x.onload = () => {
      let d = null;
      try { d = JSON.parse(x.responseText); } catch { /* keine Antwort */ }
      if (x.status === 200 && d && d.ok) resolve("neu");
      else if (x.status === 409 && d && d.code === "exists") resolve("vorhanden");
      else reject(new Error((d && d.message) || `HTTP ${x.status}`));
    };
    x.onerror = () => reject(new Error("Die Verbindung brach ab."));
    x.open("POST", `/api/v1/files/upload?path=${encodeURIComponent(dir)}&name=${encodeURIComponent(file.name)}`);
    x.send(file);
  });

  const plImport = async (files) => {
    const dir = ($("#pl-dir") && $("#pl-dir").textContent) || "/data/PS5-Cooling-Center/payloads";
    const added = [];
    for (const f of files) {
      if (!/\.elf$/i.test(f.name)) { toast(`„${f.name}“ ist keine .elf-Datei.`, "error"); continue; }
      try {
        const r = await plUpload(f, dir);
        toast(r === "neu" ? `„${f.name}“ importiert.` : `„${f.name}“ gab es schon; die vorhandene Datei bleibt.`);
        added.push(f.name);
      } catch (e) { toast(`„${f.name}“: ${e.message}`, "error"); }
    }
    await loadPayloadFiles(true);
    return added;
  };

  const plEditSync = () => {
    if (!plEdit) return;
    const n = $("#pl-ed-name");
    if (n) plEdit.name = n.value;
    $$("#pl-prof-edit [data-pp-delay]").forEach((i) => {
      const v = Math.max(1, Math.min(600000, Math.round(Number(i.value) || 1000)));
      plEdit.items[Number(i.dataset.ppDelay)] = `!${v}`;
    });
  };

  const plDoc = (list) => ({ startup: plProf.startup, profiles: list });

  const wireProfiles = () => {
    const sec = $("#pl-prof-list") && $("#pl-prof-list").closest(".pl-sec");
    if (!sec) return;
    sec.addEventListener("click", async (e) => {
      const b = e.target.closest("button");
      if (!b || b.disabled) return;
      const d = b.dataset;
      try {
        if (b.id === "pl-prof-new") {
          plEdit = { id: plNewId(), name: "", items: [], isNew: true };
          plRenderEdit();
          const n = $("#pl-ed-name"); if (n) n.focus();
        } else if (b.id === "pl-prof-export") {
          const blob = new Blob([JSON.stringify(plProf, null, 2)], { type: "application/json" });
          const a = document.createElement("a");
          a.href = URL.createObjectURL(blob);
          a.download = "payload-profile.json";
          a.click();
          setTimeout(() => URL.revokeObjectURL(a.href), 2000);
        } else if (d.ppRun !== undefined) {
          await api("/api/v1/payload-profiles/run", { method: "POST", body: JSON.stringify({ id: d.ppRun }) });
          toast("Profil wird ausgeführt.");
          plPollRun();
        } else if (b.hasAttribute("data-pp-stop")) {
          await api("/api/v1/payload-profiles/stop", { method: "POST", body: "{}" });
          toast("Das Profil wird nach dem laufenden Schritt angehalten.");
        } else if (d.ppEdit !== undefined) {
          const p = plProf.profiles.find((x) => x.id === d.ppEdit);
          if (p) { plEdit = { id: p.id, name: p.name, items: p.items.slice(), isNew: false }; plRenderEdit(); }
        } else if (d.ppStartup !== undefined) {
          const off = plProf.startup === d.ppStartup;
          await plProfSave({ startup: off ? "" : d.ppStartup, profiles: plProf.profiles });
          toast(off ? "Kein Startprofil mehr." : "Startprofil gesetzt: Es läuft nach jedem Start der App.");
        } else if (plEdit) {
          plEditSync();
          if (d.ppUp !== undefined || d.ppDown !== undefined) {
            const i = Number(d.ppUp !== undefined ? d.ppUp : d.ppDown), j = d.ppUp !== undefined ? i - 1 : i + 1;
            if (j >= 0 && j < plEdit.items.length) [plEdit.items[i], plEdit.items[j]] = [plEdit.items[j], plEdit.items[i]];
            plRenderEdit();
          } else if (d.ppDel !== undefined) {
            plEdit.items.splice(Number(d.ppDel), 1);
            plRenderEdit();
          } else if (b.hasAttribute("data-pp-add")) {
            const v = $("#pl-ed-pick").value;
            if (v && plEdit.items.length < 64) plEdit.items.push(v);
            plRenderEdit();
          } else if (b.hasAttribute("data-pp-pause")) {
            if (plEdit.items.length < 64) plEdit.items.push("!2000");
            plRenderEdit();
          } else if (b.hasAttribute("data-pp-cancel")) {
            plEdit = null;
            plRenderEdit();
          } else if (b.hasAttribute("data-pp-save")) {
            const name = plEdit.name.trim();
            if (!name) { toast("Das Profil braucht einen Namen.", "error"); return; }
            const entry = { id: plEdit.id, name, items: plEdit.items };
            const list = plEdit.isNew ? plProf.profiles.concat([entry])
              : plProf.profiles.map((x) => (x.id === entry.id ? entry : x));
            await plProfSave(plDoc(list));
            toast(`Profil „${name}“ gespeichert.`);
            plEdit = null;
            plRenderEdit();
          } else if (b.hasAttribute("data-pp-remove")) {
            const id = plEdit.id;
            const startup = plProf.startup === id ? "" : plProf.startup;
            await plProfSave({ startup, profiles: plProf.profiles.filter((x) => x.id !== id) });
            toast("Profil gelöscht.");
            plEdit = null;
            plRenderEdit();
          }
        }
      } catch (err) { toast(err.message, "error"); }
    });
    sec.addEventListener("change", async (e) => {
      const t = e.target;
      if (t.id === "pl-ed-import" && t.files && t.files.length && plEdit) {
        plEditSync();
        const added = await plImport(Array.from(t.files));
        t.value = "";
        added.forEach((n) => { if (plEdit && plEdit.items.length < 64 && !plEdit.items.includes(n)) plEdit.items.push(n); });
        plRenderEdit();
      } else if (t.id === "pl-prof-import-file" && t.files && t.files[0]) {
        try {
          const doc = JSON.parse(await t.files[0].text());
          const profiles = Array.isArray(doc.profiles) ? doc.profiles : [];
          const known = new Set(plProf.profiles.map((p) => p.id));
          const merged = plProf.profiles.slice();
          profiles.forEach((p) => {
            const q = { id: known.has(p.id) ? plNewId() : p.id, name: p.name, items: p.items };
            known.add(q.id);
            merged.push(q);
          });
          await plProfSave({ startup: plProf.startup, profiles: merged });
          toast(`${plCount(profiles.length, "Profil", "Profile")} geladen.`);
        } catch (err) { toast(`Die Datei ließ sich nicht laden: ${err.message}`, "error"); }
        t.value = "";
      }
    });
  };

  const wirePayloads = () => {
    const page = $("#page-payloads");
    if (!page) return;
    page.addEventListener("click", (e) => {
      const b = e.target.closest("button");
      if (!b || b.disabled || !page.contains(b)) return;
      if (b.hasAttribute("data-pl-start")) plStart(b);
      else if (b.hasAttribute("data-pl-copy")) plCopy(b);
    });
    wireProfiles();
    const imp = $("#pl-import-file");
    if (imp) imp.addEventListener("change", async () => {
      if (imp.files && imp.files.length) await plImport(Array.from(imp.files));
      imp.value = "";
    });
    const refresh = $("#pl-refresh");
    if (refresh) refresh.addEventListener("click", async () => {
      refresh.disabled = true;
      try { await Promise.all([loadPayloads(true), loadPayloadFiles(true)]); }
      finally { refresh.disabled = false; }
    });
  };

  /* ── Kühlleistung über die Zeit ─────────────────────────────────── */

  const loadHealth = async () => {
    const box = $("#health-box");
    if (!box) return;
    let d;
    try { d = await api("/api/v1/cooling-health"); } catch { return; }

    if (!d.verdict_ready) {
      /* Ehrlich sagen, wie weit es ist, statt ein vorläufiges Urteil zu
         fällen — vier Wochen sind das Minimum für eine Aussage. */
      const w = d.weeks_usable || 0;
      box.innerHTML = `<p class="muted">Noch keine Aussage möglich —
        <b>${w} von 4</b> benötigten Wochen erfasst. Die App zählt nur
        Zeiten mit vergleichbarer Last und Drehzahl; gelegentliches Spielen
        reicht, es dauert nur.</p>`;
      return;
    }

    const dl = d.delta_c;
    let cls, title, text;
    if (dl >= 5) {
      cls = "hot"; title = "Kühlleistung lässt deutlich nach";
      text = `Bei gleicher Last und Drehzahl <b>${dl.toFixed(1)} °C wärmer</b>
              als am Anfang. Das spricht für Staub im Lüfter oder in den
              Kühlrippen.`;
    } else if (dl >= 2) {
      cls = "warn"; title = "Kühlleistung lässt leicht nach";
      text = `<b>${dl.toFixed(1)} °C wärmer</b> als am Anfang, bei gleicher
              Last und Drehzahl. Noch unkritisch — im Auge behalten.`;
    } else if (dl <= -2) {
      cls = "ok"; title = "Kühlleistung besser als am Anfang";
      text = `<b>${Math.abs(dl).toFixed(1)} °C kühler</b> als am Anfang.
              Nach einer Reinigung oder bei kühlerem Raum zu erwarten.`;
    } else {
      cls = "ok"; title = "Kühlleistung stabil";
      text = `Unverändert gegenüber dem Anfang (${dl >= 0 ? "+" : ""}${dl.toFixed(1)} °C).`;
    }

    box.innerHTML = `
      <div class="hero-copy" style="margin-bottom:10px">
        <h3 class="sub-head" style="margin-top:0">${title}</h3>
        <p class="muted">${text}</p>
      </div>
      <div class="kv-grid">
        <div><span>Am Anfang</span><b>${d.baseline_c.toFixed(1)} °C</b></div>
        <div><span>Jetzt</span><b>${d.current_c.toFixed(1)} °C</b></div>
        <div><span>Unterschied</span><b>${dl >= 0 ? "+" : ""}${dl.toFixed(1)} °C</b></div>
        <div><span>Wochen erfasst</span><b>${d.weeks_usable}</b></div>
      </div>`;
    box.className = cls;
  };

  /* ── Zusatzabfragen ─────────────────────────────────────────────── */

  const PROBE_BITS = {
    "#probe-game": 1,
    "#probe-pad": 2,
    "#probe-netdisp": 4,
    "#probe-risky": 8,
    "#probe-drive": 16,
    "#probe-fps": 32
  };

  const PROBE_PRESETS = {
    safe: 0,
    monitor: PROBE_BITS["#probe-game"] | PROBE_BITS["#probe-netdisp"] | PROBE_BITS["#probe-risky"],
    full: PROBE_BITS["#probe-game"] | PROBE_BITS["#probe-pad"] | PROBE_BITS["#probe-netdisp"]
      | PROBE_BITS["#probe-risky"] | PROBE_BITS["#probe-drive"] | PROBE_BITS["#probe-fps"]
  };

  const PROBE_DIAG_PROFILES = {
    quick: { label: "2-Minuten-Test", mask: PROBE_PRESETS.monitor, seconds: 120 },
    medium: { label: "5-Minuten-Test", mask: PROBE_PRESETS.monitor, seconds: 300 },
    intensive: { label: "10-Minuten-Test", mask: PROBE_PRESETS.full, seconds: 600 }
  };

  const currentProbeMask = () => {
    let mask = 0;
    Object.entries(PROBE_BITS).forEach(([sel, bit]) => {
      if ($(sel) && $(sel).checked) mask |= bit;
    });
    return mask;
  };

  const fmtLeft = (sec) => {
    const s = Math.max(0, Math.floor(sec));
    const m = Math.floor(s / 60);
    const r = s % 60;
    return `${String(m).padStart(2, "0")}:${String(r).padStart(2, "0")}`;
  };

  const renderProbeDiagState = () => {
    const mode = $("#probe-diag-mode");
    const left = $("#probe-diag-left");
    const stop = $("#probe-diag-stop");
    const running = !!state.probeDiagSession;
    if (mode) mode.textContent = running ? state.probeDiagSession.label : "aus";
    if (left) {
      if (!running) left.textContent = "--";
      else left.textContent = fmtLeft((state.probeDiagSession.endsAt - Date.now()) / 1000);
    }
    if (stop) stop.disabled = !running;
  };

  const isRiskyProbeEnabled = () => {
    const el = $("#probe-risky");
    return !!(el && el.checked);
  };

  const updateRiskyAutoState = () => {
    const st = $("#risky-auto-state");
    const cur = $("#risky-auto-current");
    const riskyOn = isRiskyProbeEnabled();
    if (cur) cur.textContent = `${state.riskyAutoIntervalSec} s`;
    if (st) {
      if (!state.riskyAutoOn) st.textContent = "aus";
      else st.textContent = riskyOn ? "aktiv" : "wartet auf Zusatzabfrage";
      st.classList.toggle("probe-on", state.riskyAutoOn && riskyOn);
      st.classList.toggle("probe-off", !state.riskyAutoOn || !riskyOn);
    }
  };

  /* `opts.revertAfterS` hands the way back to the console: it applies the mask
     at once, remembers the previous one and restores it by itself after that
     many seconds, even if this page is gone by then. A write without it
     cancels a revert that is still pending. */
  const applyProbeMaskToConfig = async (mask, msg, opts = {}) => {
    const body = { probe_mask: mask };
    if (opts.revertAfterS) body.probe_revert_after_s = opts.revertAfterS;
    await api("/api/v1/config", { method: "PUT", body: JSON.stringify(body) });
    showProbes(mask);
    updateRiskyAutoState();
    if (!opts.revertAfterS) endProbeDiagLocal();
    if (!opts.silent && msg) toast(msg);
  };

  /* Drops the page's side of a diagnosis session; the console owns the revert. */
  const endProbeDiagLocal = () => {
    if (state.probeDiagTickTimer) {
      clearInterval(state.probeDiagTickTimer);
      state.probeDiagTickTimer = null;
    }
    state.probeDiagSession = null;
    renderProbeDiagState();
  };

  /* Countdown at zero: the console reverts on its own clock, so this side only
     waits a moment and reads the result back. No restore is sent from here, the
     page could be showing a mask the console has long replaced. If the revert
     is still pending (clock skew), loadConfig() picks the countdown up again. */
  const finishProbeDiagSession = () => {
    endProbeDiagLocal();
    setTimeout(async () => {
      try {
        await loadConfig();
        updateRiskyAutoState();
        if (!state.probeDiagSession)
          toast("Diagnoseprofil abgeschlossen. Zusatzabfragen zurückgestellt.");
      } catch (e) {
        toast(`Diagnoseprofil abgeschlossen, Zustand nicht lesbar: ${e.message}`, "error");
      }
    }, 2000);
  };

  const ensureProbeDiagTicker = () => {
    if (state.probeDiagTickTimer) return;
    state.probeDiagTickTimer = setInterval(() => {
      const sess = state.probeDiagSession;
      if (!sess) {
        clearInterval(state.probeDiagTickTimer);
        state.probeDiagTickTimer = null;
        return;
      }
      if (sess.endsAt - Date.now() <= 0) {
        finishProbeDiagSession();
        return;
      }
      renderProbeDiagState();
    }, 1000);
  };

  /* A revert pending on the console is a session of this page as well: after a
     reload (or on another device) the countdown and the Stop button must still
     be there. The console's figures win over anything remembered here. */
  const syncProbeDiagSession = (c) => {
    if (state.probeDiagBusy) return;           /* our own write is in flight */
    const left = finiteOrNull(c.probe_revert_in_s);
    if (left === null) {
      if (state.probeDiagSession) endProbeDiagLocal();
      return;
    }
    const endsAt = Date.now() + Math.max(left, 2) * 1000;
    if (state.probeDiagSession) {
      state.probeDiagSession.endsAt = endsAt;
    } else {
      state.probeDiagSession = {
        key: "restored",
        label: "Diagnoseprofil",
        restoreMask: clampInt(c.probe_revert_mask, 0, 63, 0),
        endsAt
      };
      ensureProbeDiagTicker();
    }
    renderProbeDiagState();
  };

  const stopProbeDiagSession = async (reason) => {
    const sess = state.probeDiagSession;
    if (!sess) {
      renderProbeDiagState();
      return;
    }
    state.probeDiagBusy++;
    endProbeDiagLocal();
    try {
      /* Written without a revert time: that also cancels the pending one. */
      await applyProbeMaskToConfig(sess.restoreMask, "", { silent: true });
      toast(reason || "Diagnoseprofil beendet. Ursprüngliche Zusatzabfragen wiederhergestellt.");
    } catch (e) {
      toast(`Diagnoseprofil beendet, Rückstellung fehlgeschlagen: ${e.message}`, "error");
    } finally {
      state.probeDiagBusy--;
    }
  };

  const startProbeDiagSession = async (profileKey) => {
    const p = PROBE_DIAG_PROFILES[profileKey];
    if (!p) return;
    if (state.probeDiagSession) {
      await stopProbeDiagSession("Laufendes Diagnoseprofil gestoppt und ersetzt.");
    }
    state.probeDiagBusy++;
    try {
      const restoreMask = currentProbeMask();
      await applyProbeMaskToConfig(p.mask, "", { silent: true, revertAfterS: p.seconds });
      state.probeDiagSession = {
        key: profileKey,
        label: p.label,
        restoreMask,
        endsAt: Date.now() + (p.seconds * 1000)
      };
      /* The console knows the mask it will put back (the forms may hold
         unsaved choices) and how long is really left. */
      try {
        const c = await api("/api/v1/config");
        const left = finiteOrNull(c.probe_revert_in_s);
        if (left !== null) state.probeDiagSession.endsAt = Date.now() + left * 1000;
        if (finiteOrNull(c.probe_revert_mask) !== null)
          state.probeDiagSession.restoreMask = clampInt(c.probe_revert_mask, 0, 63, restoreMask);
      } catch { /* the local figures are close enough */ }
      ensureProbeDiagTicker();
      renderProbeDiagState();
      toast(`${p.label} gestartet. Danach wird automatisch zurückgestellt.`);
    } finally {
      state.probeDiagBusy--;
    }
  };

  const restartRiskyAutoTimer = () => {
    if (state.riskyAutoTimer) {
      clearInterval(state.riskyAutoTimer);
      state.riskyAutoTimer = null;
    }
    if (!state.riskyAutoOn) {
      updateRiskyAutoState();
      return;
    }
    const every = Math.max(10, Number(state.riskyAutoIntervalSec) || 15) * 1000;
    state.riskyAutoTimer = setInterval(() => {
      if (state.page !== "system") return;
      loadRiskyTelemetry({ silent: true }).catch(() => {});
    }, every);
    updateRiskyAutoState();
  };

  const updateProbeLiveState = () => {
    const risky = $("#probe-risky-state");
    const drive = $("#probe-drive-state");
    const riskyOn = Boolean($("#probe-risky") && $("#probe-risky").checked);
    const driveOn = Boolean($("#probe-drive") && $("#probe-drive").checked);
    if (risky) {
      risky.textContent = riskyOn ? "aktiv" : "inaktiv";
      risky.classList.toggle("probe-on", riskyOn);
      risky.classList.toggle("probe-off", !riskyOn);
    }
    if (drive) {
      drive.textContent = driveOn ? "aktiv" : "inaktiv";
      drive.classList.toggle("probe-on", driveOn);
      drive.classList.toggle("probe-off", !driveOn);
    }
    /* Beim Zähler zählt nicht nur der Schalter: Die Konsole kann das Gerät
       verweigern. Dann steht das hier, statt stumm „aktiv" zu behaupten. */
    const fps = $("#probe-fps-state");
    if (fps) {
      const fpsOn = Boolean($("#probe-fps") && $("#probe-fps").checked);
      const st = (state.status && state.status.fps && state.status.fps.state) || "";
      const label = !fpsOn ? "inaktiv"
                  : st === "verweigert" ? "von der Konsole verweigert"
                  : st === "fehler" ? "Abfrage schlägt fehl"
                  : st === "kein Spiel" ? "aktiv, zählt sobald ein Spiel läuft"
                  : st === "ok" ? "aktiv" : "aktiv, noch kein Wert";
      fps.textContent = label;
      fps.classList.toggle("probe-on", fpsOn && st !== "verweigert" && st !== "fehler");
      fps.classList.toggle("probe-off", !fpsOn || st === "verweigert" || st === "fehler");
    }
  };

  const showProbes = (mask) => {
    Object.entries(PROBE_BITS).forEach(([sel, bit]) => {
      const el = $(sel);
      if (el) el.checked = (mask & bit) !== 0;
    });
    updateProbeLiveState();
  };

  const wireProbes = () => {
    const btn = $("#probe-save");
    if (!btn) return;
    ["#probe-risky", "#probe-drive"].forEach((sel) => {
      const el = $(sel);
      if (el) el.addEventListener("change", () => {
        updateProbeLiveState();
        updateRiskyAutoState();
      });
    });
    updateProbeLiveState();
    updateRiskyAutoState();
    btn.addEventListener("click", async () => {
      let mask = 0;
      Object.entries(PROBE_BITS).forEach(([sel, bit]) => {
        if ($(sel) && $(sel).checked) mask |= bit;
      });
      try {
        await applyProbeMaskToConfig(mask, mask
          ? "Übernommen. Seite neu laden und beobachten."
          : "Alle Zusatzabfragen aus.");
      } catch (e) { toast(e.message, "error"); }
    });

    const setPreset = async (name, label) => {
      const mask = PROBE_PRESETS[name];
      if (mask === undefined) return;
      try {
        await applyProbeMaskToConfig(mask, `Preset ${label} übernommen.`);
      } catch (e) {
        toast(e.message, "error");
      }
    };

    const pSafe = $("#probe-preset-safe");
    const pMonitor = $("#probe-preset-monitor");
    const pFull = $("#probe-preset-full");
    if (pSafe) pSafe.addEventListener("click", () => setPreset("safe", "Sicher"));
    if (pMonitor) pMonitor.addEventListener("click", () => setPreset("monitor", "Monitoring"));
    if (pFull) pFull.addEventListener("click", () => setPreset("full", "Voll"));

    const d2 = $("#probe-diag-2m");
    const d5 = $("#probe-diag-5m");
    const d10 = $("#probe-diag-10m");
    const dStop = $("#probe-diag-stop");
    if (d2) d2.addEventListener("click", () => {
      startProbeDiagSession("quick").catch((e) => toast(e.message, "error"));
    });
    if (d5) d5.addEventListener("click", () => {
      startProbeDiagSession("medium").catch((e) => toast(e.message, "error"));
    });
    if (d10) d10.addEventListener("click", () => {
      startProbeDiagSession("intensive").catch((e) => toast(e.message, "error"));
    });
    if (dStop) dStop.addEventListener("click", () => {
      stopProbeDiagSession("Diagnoseprofil manuell gestoppt. Zusatzabfragen zurückgestellt.")
        .catch((e) => toast(e.message, "error"));
    });

    renderProbeDiagState();
  };

  /* ── Benutzerprofil ─────────────────────────────────────────────── */

  /* Der Hinweistext wechselt je nach Lage; das Original wird beim ersten
     Zeichnen festgehalten, damit es zurückkommen kann. */
  let userNoteDefault = null;

  const renderUser = (sys) => {
    const uid = $("#user-uid"), inp = $("#user-name"),
          btn = $("#user-save"), note = $("#user-note");
    if (!uid || !inp || !btn || !note) return;
    if (userNoteDefault === null) userNoteDefault = note.innerHTML;

    const u = sys.user;
    if (!u || !u.valid) {
      uid.textContent = "--";
      inp.value = "";
      inp.disabled = btn.disabled = true;
      note.textContent = "Es ist kein Benutzer angemeldet — im Moment lässt " +
        "sich kein Name ändern.";
      return;
    }

    uid.textContent = u.uid_hex;
    inp.maxLength = u.name_max || 16;
    /* Nicht dazwischenfunken, während jemand tippt. */
    if (document.activeElement !== inp) inp.value = u.username || "";
    state.userName = u.username || "";

    inp.disabled = btn.disabled = !u.can_rename;
    note.innerHTML = u.can_rename ? userNoteDefault
      : "Diese Firmware stellt das Umbenennen nicht bereit.";
  };

  /* ── Profil-Seite (19.09.2026) ──────────────────────────────────────
     Kopf mit dem echten Profilbild der Konsole (GET …/avatar/current, rein
     lesend). Ohne eigenes Bild liefert die Konsole 404 — dann steht der
     Anfangsbuchstabe des Namens im Kreis. */
  const setProfileName = (name) => {
    txt("#pf-name", name || "--");
    txt("#pf-avatar-initial", name ? name.trim().charAt(0).toUpperCase() : "?");
  };

  const renderProfileHero = (sys) => {
    const u = sys.user || {};
    setProfileName(u.valid ? u.username : "");
    if (!u.valid) txt("#pf-name", "Kein Benutzer angemeldet");
    txt("#pf-uid", u.valid ? u.uid_hex : "--");
    txt("#pf-console", [sys.console_name, sys.model].filter(Boolean).join(" · ") || "PlayStation 5");
  };

  const refreshProfilePicture = () => {
    const box = $("#pf-avatar"), img = $("#pf-avatar-img");
    if (!box || !img) return;
    img.onload = () => { img.hidden = false; box.classList.add("has-pic"); };
    img.onerror = () => { img.hidden = true; box.classList.remove("has-pic"); };
    img.src = `/api/v1/profile/avatar/current?t=${Date.now()}`;
  };

  const loadProfile = async () => {
    let sys;
    try { sys = await api("/api/v1/system"); } catch { return; }
    renderUser(sys);
    renderProfileHero(sys);
    refreshProfilePicture();
  };

  const wireUsername = () => {
    const inp = $("#user-name"), btn = $("#user-save");
    if (!inp || !btn) return;

    const save = async () => {
      const name = inp.value.trim();
      if (!name) { toast("Bitte einen Namen eingeben.", "error"); return; }
      if (name === state.userName) { toast("Der Name ist unverändert."); return; }

      btn.disabled = true;
      try {
        /* Die Konsole entscheidet, wie der Name am Ende lautet — deshalb wird
           ihre Antwort übernommen und nicht die Eingabe. */
        const r = await api("/api/v1/profile/username",
          { method: "POST", body: JSON.stringify({ name }) });
        state.userName = r.username;
        inp.value = r.username;
        setProfileName(r.username);
        toast(`Benutzer heißt jetzt „${r.username}“.`);
      } catch (e) {
        toast(e.message, "error");
      } finally {
        btn.disabled = false;
      }
    };

    btn.addEventListener("click", save);
    inp.addEventListener("keydown", (e) => { if (e.key === "Enter") save(); });
  };

  /* ── Profilbild ─────────────────────────────────────────────────── */

  /* Die PS5 liest ihr Profilbild als DXT5-komprimierte DDS-Texturen in vier
     Größen, jede doppelt abgelegt. Das Umrechnen passiert hier im Browser:
     die Konsole hat keinen Bilddecoder, den wir aufrufen könnten, und ein
     eigener wäre viel Code für wenig Gewinn. Format, Größen und die Vorlage
     für online.json stammen aus dem Quellcode von PS5Upload. */
  const AV_SIZES = [64, 128, 260, 440];

  /* Ein 4×4-Block wird zu 16 Byte: zwei Alpha-Endwerte und 16 Indizes zu je
     drei Bit, danach zwei RGB565-Farbendwerte und 16 Indizes zu je zwei Bit.
     Eins zu eins portiert aus `compress_dxt5_block`. */
  const dxt5Block = (px) => {
    const out = new Uint8Array(16);

    let aMin = 255, aMax = 0;
    for (let i = 0; i < 16; i++) {
      const a = px[i * 4 + 3];
      if (a < aMin) aMin = a;
      if (a > aMax) aMax = a;
    }
    const a0 = aMax, a1 = aMin;
    const apal = new Array(8);
    apal[0] = a0; apal[1] = a1;
    if (a0 > a1) {
      for (let i = 0; i < 6; i++)
        apal[2 + i] = Math.floor(((6 - i) * a0 + (1 + i) * a1) / 7);
    } else {
      for (let i = 0; i < 4; i++)
        apal[2 + i] = Math.floor(((4 - i) * a0 + (1 + i) * a1) / 5);
      apal[6] = 0; apal[7] = 255;
    }

    /* 16 Indizes zu drei Bit sind 48 Bit. JS rechnet bitweise nur mit 32,
       deshalb zwei Hälften zu je acht Indizes — das sind genau drei Byte. */
    let lo = 0, hi = 0;
    for (let i = 0; i < 16; i++) {
      const a = px[i * 4 + 3];
      let best = 0, dist = 256;
      for (let j = 0; j < 8; j++) {
        const d = Math.abs(a - apal[j]);
        if (d < dist) { dist = d; best = j; }
      }
      if (i < 8) lo |= best << (i * 3);
      else       hi |= best << ((i - 8) * 3);
    }
    out[0] = a0; out[1] = a1;
    out[2] = lo & 0xff; out[3] = (lo >> 8) & 0xff; out[4] = (lo >> 16) & 0xff;
    out[5] = hi & 0xff; out[6] = (hi >> 8) & 0xff; out[7] = (hi >> 16) & 0xff;

    const mn = [255, 255, 255], mx = [0, 0, 0];
    for (let i = 0; i < 16; i++)
      for (let c = 0; c < 3; c++) {
        const v = px[i * 4 + c];
        if (v < mn[c]) mn[c] = v;
        if (v > mx[c]) mx[c] = v;
      }
    const to565 = (r, g, b) => ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    const to888 = (c) =>
      [((c >> 11) & 0x1f) << 3, ((c >> 5) & 0x3f) << 2, (c & 0x1f) << 3];

    let c0 = to565(mx[0], mx[1], mx[2]);
    let c1 = to565(mn[0], mn[1], mn[2]);
    if (c0 < c1) { const t = c0; c0 = c1; c1 = t; }
    else if (c0 === c1 && c0 < 0xffff) c0 += 1;

    const p0 = to888(c0), p1 = to888(c1);
    const pal = [p0, p1, [0, 0, 0], [0, 0, 0]];
    for (let c = 0; c < 3; c++) {
      pal[2][c] = Math.floor((2 * p0[c] + p1[c]) / 3);
      pal[3][c] = Math.floor((p0[c] + 2 * p1[c]) / 3);
    }

    let ci = 0;
    for (let i = 0; i < 16; i++) {
      const r = px[i * 4], g = px[i * 4 + 1], b = px[i * 4 + 2];
      let best = 0, dist = Infinity;
      for (let j = 0; j < 4; j++) {
        const dr = r - pal[j][0], dg = g - pal[j][1], db = b - pal[j][2];
        const d = dr * dr + dg * dg + db * db;
        if (d < dist) { dist = d; best = j; }
      }
      ci = (ci | (best << (i * 2))) >>> 0;
    }
    out[8]  = c0 & 0xff;         out[9]  = (c0 >> 8) & 0xff;
    out[10] = c1 & 0xff;         out[11] = (c1 >> 8) & 0xff;
    out[12] = ci & 0xff;         out[13] = (ci >>> 8) & 0xff;
    out[14] = (ci >>> 16) & 0xff; out[15] = (ci >>> 24) & 0xff;
    return out;
  };

  /* 128-Byte-DDS-Kopf, danach die Blöcke. Jedes Feld steht dort, wo die
     Referenz es schreibt — die Konsole prüft den Kopf. */
  const encodeDds = (rgba, size) => {
    const bw = Math.ceil(size / 4), bh = Math.ceil(size / 4);
    const comp = bw * bh * 16;
    const buf = new Uint8Array(128 + comp);
    const dv = new DataView(buf.buffer);

    buf.set([0x44, 0x44, 0x53, 0x20], 0);                 /* "DDS " */
    buf[4] = 124;
    dv.setUint32(8, 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000, true);
    dv.setUint32(12, size, true);                          /* Höhe   */
    dv.setUint32(16, size, true);                          /* Breite */
    dv.setUint32(20, comp, true);
    buf[76] = 32;
    buf[80] = 4;                                           /* FOURCC */
    buf.set([0x44, 0x58, 0x54, 0x35], 84);                 /* "DXT5" */
    buf[109] = 0x10;                                       /* TEXTURE */

    const block = new Uint8Array(64);
    let p = 128;
    for (let by = 0; by < bh; by++)
      for (let bx = 0; bx < bw; bx++) {
        for (let y = 0; y < 4; y++) {
          const py = Math.min(by * 4 + y, size - 1);
          for (let x = 0; x < 4; x++) {
            const pxx = Math.min(bx * 4 + x, size - 1);
            const s = (py * size + pxx) * 4, d = (y * 4 + x) * 4;
            block[d] = rgba[s]; block[d + 1] = rgba[s + 1];
            block[d + 2] = rgba[s + 2]; block[d + 3] = rgba[s + 3];
          }
        }
        buf.set(dxt5Block(block), p);
        p += 16;
      }
    return buf;
  };

  /* Quadratisch machen und auf die Zielkante bringen. „Zuschneiden" nimmt das
     größte mittige Quadrat, „Einpassen" behält alles und füllt mit
     Transparenz auf — deshalb clearRect statt eines Hintergrunds. */
  const squareCanvas = (img, mode, size) => {
    const c = document.createElement("canvas");
    c.width = c.height = size;
    const g = c.getContext("2d");
    g.clearRect(0, 0, size, size);
    g.imageSmoothingEnabled = true;
    g.imageSmoothingQuality = "high";

    const w = img.width, h = img.height;
    if (mode === "fit") {
      const s = size / Math.max(w, h);
      g.drawImage(img, (size - w * s) / 2, (size - h * s) / 2, w * s, h * s);
    } else {
      const side = Math.min(w, h);
      g.drawImage(img, (w - side) / 2, (h - side) / 2, side, side,
                  0, 0, size, size);
    }
    return c;
  };

  /* Vorlage aus dem Referenzcode; nur firstName wird ersetzt. */
  const onlineJson = (username) => JSON.stringify({
    avatarUrl: "http://static-resource.np.community.playstation.net/avatar_xl/WWS_E/E0012_XL.png",
    firstName: username || "",
    lastName: "",
    pictureUrl: "https://image.api.np.km.playstation.net/images/?format=png&w=440&h=440&image=https%3A%2F%2Fkfscdn.api.np.km.playstation.net%2F00000000000008%2F000000000000003.png&sign=blablabla019501",
    trophySummary: "{\"level\":1,\"progress\":0,\"earnedTrophies\":{\"platinum\":0,\"gold\":0,\"silver\":0,\"bronze\":0}}",
    isOfficiallyVerified: "true",
  });

  const buildAvatarFiles = async (img, mode, username) => {
    const files = [];
    for (const size of AV_SIZES) {
      const c = squareCanvas(img, mode, size);
      const data = c.getContext("2d").getImageData(0, 0, size, size).data;
      const dds = encodeDds(data, size);
      files.push([`avatar${size}.dds`, dds]);
      files.push([`picture${size}.dds`, dds]);
    }

    const big = squareCanvas(img, mode, 440);
    const blob = await new Promise((r) => big.toBlob(r, "image/png"));
    const png = new Uint8Array(await blob.arrayBuffer());
    files.push(["avatar.png", png]);
    files.push(["picture.png", png]);

    files.push(["online.json",
      new TextEncoder().encode(onlineJson(username))]);
    return files;
  };

  /* Jeder Schritt wird angeschrieben, und es gibt keinen stillen Abbruch
     mehr. Beim ersten Anlauf hieß es „ich klicke, es passiert nichts" — und
     genau das ist der Zustand, aus dem man nichts lernt: ein deaktivierter
     Knopf, ein `return` ohne Meldung und ein Fehler im Browser sehen von
     aussen alle gleich aus. Jetzt sagt die Zeile unter den Knöpfen, wie weit
     es gekommen ist. */
  const wireAvatar = () => {
    const file = $("#av-file"), canvas = $("#av-canvas"),
          empty = $("#av-empty"), apply = $("#av-apply"),
          restore = $("#av-restore"), status = $("#av-status"),
          savePack = $("#av-save-pack"),
          packName = $("#av-pack-name"),
          packList = $("#av-pack-list"),
          packRefresh = $("#av-pack-refresh"),
          packLoad = $("#av-pack-load"),
          packDelete = $("#av-pack-delete");

    let hasStagePack = false;

    const say = (msg, bad) => {
      if (status) {
        status.textContent = msg;
        status.classList.toggle("err", !!bad);
      }
      if (bad) toast(msg, "error");
    };

    if (!file || !canvas || !apply) {
      say("Die Bedienelemente fehlen — bitte die Seite neu laden.", true);
      return;
    }

    const mode = () =>
      ($('input[name="av-mode"]:checked') || {}).value || "crop";

    const selectedPackName = () => {
      const typed = String(packName && packName.value ? packName.value : "").trim();
      if (typed) return typed;
      return String(packList && packList.value ? packList.value : "").trim();
    };

    const loadPackList = async () => {
      if (!packList) return;
      const d = await api("/api/v1/profile/avatar/library");
      const packs = (d.packs || [])
        .map((p) => ({
          name: p && p.name ? String(p.name) : "",
          mtime: Number(p && p.mtime)
        }))
        .filter((p) => p.name)
        .sort((a, b) => (Number.isFinite(b.mtime) ? b.mtime : 0) - (Number.isFinite(a.mtime) ? a.mtime : 0));

      if (!packs.length) {
        packList.innerHTML = `<option value="">-- keine gespeicherten Pakete --</option>`;
        return;
      }

      packList.innerHTML = packs.map((p) => {
        const ts = Number.isFinite(p.mtime)
          ? new Date(p.mtime * 1000).toLocaleString(LOCALE)
          : "unbekannt";
        return `<option value="${esc(p.name)}">${esc(p.name)} · ${esc(ts)}</option>`;
      }).join("");
    };

    const uploadAvatarFiles = async (files) => {
      let i = 0, sent = 0;
      for (const [name, bytes] of files) {
        say(`Übertrage ${++i}/${files.length}: ${name} (${Math.round(bytes.length / 1024)} KB) …`);
        const res = await fetch(
          `/api/v1/profile/avatar/file?name=${encodeURIComponent(name)}`,
          { method: "POST", body: bytes });
        if (!res.ok) {
          const d = await res.json().catch(() => ({}));
          throw new Error(`${name}: ${d.message || "HTTP " + res.status}`);
        }
        sent += bytes.length;
      }
      hasStagePack = true;
      return { count: files.length, sent };
    };

    const preview = () => {
      if (!state.avImg) return;
      try {
        const c = squareCanvas(state.avImg, mode(), 440);
        const g = canvas.getContext("2d");
        g.clearRect(0, 0, 440, 440);
        g.drawImage(c, 0, 0);
        canvas.classList.add("on");
        if (empty) empty.style.display = "none";
      } catch (e) {
        /* Die Vorschau ist Beiwerk. Wenn sie scheitert, darf sie nicht das
           Übernehmen blockieren — dann eben ohne Bild daneben. */
        say(`Vorschau nicht möglich (${e.name}: ${e.message}) — übernehmen `
            + `geht trotzdem.`, true);
      }
    };

    /* Ein gewähltes Bild — aus einer Datei oder aus der eingebauten Auswahl —
       wird immer auf dieselbe Weise übernommen: Vorschau, vier Größen,
       Sicherung, zweiter Klick. Einen zweiten Weg zur Konsole gibt es nicht. */
    const adopt = (bmp, what) => {
      state.avImg = bmp;
      hasStagePack = false;
      /* Freigeben, bevor die Vorschau läuft: ein Fehler in der Vorschau darf
         die eigentliche Funktion nicht sperren. */
      apply.disabled = false;
      if (savePack) savePack.disabled = false;
      say(`${what}Bereit: ${bmp.width} × ${bmp.height} Pixel.`);
      preview();
    };

    file.addEventListener("change", async () => {
      const f = file.files && file.files[0];
      if (!f) { say("Keine Datei ausgewählt."); return; }
      hasStagePack = false;
      markPicked(null);

      say(`„${f.name}“ (${Math.round(f.size / 1024)} KB) wird gelesen …`);
      let bmp;
      try {
        bmp = await createImageBitmap(f);
      } catch (e) {
        state.avImg = null;
        apply.disabled = true;
        say(`Bild nicht lesbar (${e.name}: ${e.message}).`, true);
        return;
      }
      adopt(bmp, "");
    });

    $$('input[name="av-mode"]').forEach((r) =>
      r.addEventListener("change", preview));

    /* Die fertigen Bilder. /avatars/index.json und die Bilddateien liegen in
       der App selbst (web/avatars, gebaut mit tools/build_avatars.py): Die
       Auswahl geht deshalb von jedem Gerät aus, auch ohne Datei und ohne PC.
       Das gewählte Bild läuft danach genau wie eine Datei durch adopt(). */
    const gallery = $("#av-gallery"), galleryWrap = $("#av-gallery-wrap");
    let picking = false;

    const markPicked = (btn) => {
      $$("#av-gallery .av-thumb").forEach((b) => {
        b.classList.toggle("on", b === btn);
        b.setAttribute("aria-pressed", b === btn ? "true" : "false");
      });
    };

    const pickBuiltin = async (btn) => {
      if (picking) return;
      picking = true;
      const label = btn.dataset.label || "Bild";
      say(`„${label}“ wird geladen …`);
      try {
        const res = await fetch(`/avatars/${encodeURIComponent(btn.dataset.file)}`);
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        const bmp = await createImageBitmap(await res.blob());
        if (file) file.value = "";
        markPicked(btn);
        adopt(bmp, `„${label}“ — `);
        canvas.scrollIntoView({ block: "nearest", behavior: "smooth" });
      } catch (e) {
        say(`Bild nicht ladbar (${e.message}).`, true);
      } finally {
        picking = false;
      }
    };

    const loadGallery = async () => {
      if (!gallery || !galleryWrap) return;
      let list;
      try {
        const res = await fetch("/avatars/index.json");
        if (!res.ok) return;
        list = (await res.json()).images;
      } catch { return; }
      /* Nur schlichte Dateinamen: Das Verzeichnis kommt zwar aus der App
         selbst, aber ein Name mit Pfadteilen hat dort nichts zu suchen. */
      list = (Array.isArray(list) ? list : []).filter((im) =>
        im && typeof im.file === "string" && /^[\w.-]+\.jpg$/.test(im.file));
      if (!list.length) return;
      gallery.innerHTML = list.map((im) => {
        const label = String(im.label || im.file);
        return `<button type="button" class="av-thumb" data-file="${esc(im.file)}" `
          + `data-label="${esc(label)}" aria-pressed="false" title="${esc(label)}">`
          + `<img src="/avatars/${esc(encodeURIComponent(im.file))}" alt="${esc(label)}" `
          + `width="88" height="88" loading="lazy" decoding="async"></button>`;
      }).join("");
      txt("#av-gallery-count", String(list.length));
      galleryWrap.hidden = false;
    };

    if (gallery) gallery.addEventListener("click", (e) => {
      const btn = e.target.closest(".av-thumb");
      if (btn && gallery.contains(btn)) pickBuiltin(btn);
    });

    /* Die Rückfrage steckt im Knopf, nicht in einem confirm().
     *
     * Der native Dialog ist die einzige Stelle im ganzen Ablauf, die
     * außerhalb unserer Kontrolle liegt: Browser unterdrücken ihn — etwa
     * wenn jemand einmal „weitere Dialoge verhindern" angehakt hat — und
     * liefern dann stillschweigend `false`. Auf der Konsole des Users war
     * genau das der Fall, und weil der Abbruchpfad zuerst schwieg, sah es
     * nach einem toten Knopf aus. Zwei Klicks auf denselben Knopf sind
     * genauso eindeutig und können nicht unterdrückt werden. */
    const APPLY_LABEL = "Profilbild übernehmen";
    let armTimer = 0;

    const disarm = () => {
      clearTimeout(armTimer);
      armTimer = 0;
      apply.textContent = APPLY_LABEL;
    };

    apply.addEventListener("click", async () => {
      if (!state.avImg && !hasStagePack) {
        say("Erst ein Bild auswählen oder ein gespeichertes Paket laden.", true);
        return;
      }

      if (!armTimer) {
        apply.textContent = "Wirklich ersetzen? Nochmal klicken";
        say("Das bisherige Bild wird vorher gesichert und lässt sich mit "
            + "„Vorheriges zurückholen“ wiederherstellen. Zum Bestätigen "
            + "noch einmal klicken.");
        armTimer = setTimeout(() => {
          disarm();
          say("Rückfrage abgelaufen — es wurde nichts geändert.");
        }, 10000);
        return;
      }
      disarm();

      apply.disabled = true;
      try {
        if (state.avImg) {
          say("Bild wird in die vier Größen umgerechnet …");
          const files = await buildAvatarFiles(state.avImg, mode(), state.userName);
          const up = await uploadAvatarFiles(files);
          say(`${up.count} Dateien übertragen (${Math.round(up.sent / 1024)} KB) — wird übernommen …`);
        } else {
          say("Gespeichertes Paket wird übernommen …");
        }
        const r = await api("/api/v1/profile/avatar/apply", { method: "POST" });
        say(`Fertig: ${r.copied} Dateien in das Profil kopiert. Wenn das `
            + `Startmenü noch das alte Bild zeigt, hilft ein Benutzerwechsel.`);
        toast("Profilbild übernommen.");
        refreshProfilePicture();
      } catch (e) {
        say(`Fehlgeschlagen — ${e.message}`, true);
      } finally {
        apply.disabled = false;
      }
    });

    const RESTORE_LABEL = "Vorheriges zurückholen";
    let restoreTimer = 0;

    if (restore) restore.addEventListener("click", async () => {
      if (!restoreTimer) {
        restore.textContent = "Sicherung zurückspielen? Nochmal klicken";
        restoreTimer = setTimeout(() => {
          restoreTimer = 0;
          restore.textContent = RESTORE_LABEL;
        }, 10000);
        return;
      }
      clearTimeout(restoreTimer);
      restoreTimer = 0;
      restore.textContent = RESTORE_LABEL;

      restore.disabled = true;
      try {
        say("Sicherung wird zurückgespielt …");
        const r = await api("/api/v1/profile/avatar/restore",
                            { method: "POST" });
        /* Null zurückgespielte Dateien ist kein Fehler: dann war vorher
           nichts da, und genau dieser Zustand wurde wiederhergestellt. */
        say(r.copied
          ? `Zurückgesetzt — ${r.copied} Datei(en) aus der Sicherung `
            + `zurückgespielt, alles Neuere entfernt.`
          : "Zurückgesetzt — die Konsole hatte vorher kein Profilbild, "
            + "der Ordner ist jetzt wieder leer.");
        refreshProfilePicture();
      } catch (e) {
        say(`Zurückholen fehlgeschlagen — ${e.message}`, true);
      } finally {
        restore.disabled = false;
      }
    });

    if (savePack) {
      savePack.addEventListener("click", async () => {
        const name = selectedPackName();
        if (!name) {
          say("Bitte einen Paketnamen eingeben oder aus der Liste wählen.", true);
          return;
        }

        savePack.disabled = true;
        try {
          if (state.avImg) {
            say("Bild wird für die Avatar-Bibliothek vorbereitet …");
            const files = await buildAvatarFiles(state.avImg, mode(), state.userName);
            await uploadAvatarFiles(files);
          } else if (!hasStagePack) {
            throw new Error("Es liegen keine Avatar-Dateien zum Speichern vor.");
          }

          const r = await api("/api/v1/profile/avatar/library/save", {
            method: "POST",
            body: JSON.stringify({ name })
          });
          say(`Avatar-Paket gespeichert: ${r.name} (${r.copied} Dateien).`);
          toast("Avatar im Avatars-Ordner gespeichert.");
          await loadPackList();
          if (packName) packName.value = r.name || name;
        } catch (e) {
          say(`Speichern fehlgeschlagen — ${e.message}`, true);
        } finally {
          savePack.disabled = false;
        }
      });
    }

    if (packRefresh) {
      packRefresh.addEventListener("click", async () => {
        try {
          await loadPackList();
          say("Avatar-Liste aktualisiert.");
        } catch (e) {
          say(`Liste konnte nicht geladen werden — ${e.message}`, true);
        }
      });
    }

    if (packLoad) {
      packLoad.addEventListener("click", async () => {
        const name = selectedPackName();
        if (!name) { say("Bitte ein Paket auswählen.", true); return; }
        packLoad.disabled = true;
        try {
          const r = await api("/api/v1/profile/avatar/library/load", {
            method: "POST",
            body: JSON.stringify({ name })
          });
          hasStagePack = true;
          apply.disabled = false;
          if (savePack) savePack.disabled = false;
          say(`Paket geladen: ${r.name} (${r.copied} Dateien). Mit „Profilbild übernehmen“ aktivieren.`);
          toast("Avatar-Paket in den Vorschaubereich geladen.");
          if (packName) packName.value = r.name || name;
        } catch (e) {
          say(`Laden fehlgeschlagen — ${e.message}`, true);
        } finally {
          packLoad.disabled = false;
        }
      });
    }

    if (packDelete) {
      packDelete.addEventListener("click", async () => {
        const name = selectedPackName();
        if (!name) { say("Bitte ein Paket auswählen.", true); return; }
        packDelete.disabled = true;
        try {
          const r = await api("/api/v1/profile/avatar/library/delete", {
            method: "POST",
            body: JSON.stringify({ name })
          });
          say(`Paket gelöscht: ${r.name} (${r.removed} Dateien entfernt).`);
          toast("Avatar-Paket gelöscht.");
          await loadPackList();
          if (packName) packName.value = "";
        } catch (e) {
          say(`Löschen fehlgeschlagen — ${e.message}`, true);
        } finally {
          packDelete.disabled = false;
        }
      });
    }

    loadPackList().catch(() => {});
    loadGallery().catch(() => {});
  };

  /* ── Power-Optionen ─────────────────────────────────────────────── */

  /* Jeder dieser Befehle unterbricht, was gerade läuft, deshalb wird immer
     gefragt — und die Frage benennt die Folge, nicht nur die Aktion. Sie
     läuft über armConfirm(): der native Dialog wurde auf dieser Konsole
     unterdrückt und machte alle vier Knöpfe wirkungslos. */
  const POWER_ACTIONS = {
    "#pw-standby": ["standby", "Ruhemodus",
      "Die Konsole geht in den Ruhemodus. Ein laufendes Spiel wird pausiert."],
    "#pw-reboot": ["reboot", "Neustart",
      "Die Konsole startet neu. Nicht gespeicherte Spielstände gehen verloren."],
    "#pw-off": ["off", "Ausschalten",
      "Die Konsole wird ausgeschaltet. Nicht gespeicherte Spielstände gehen verloren."],
    "#pw-safe": ["safemode", "Abgesicherter Modus",
      "Die Konsole startet neu und bleibt im Menü des abgesicherten Modus " +
      "stehen. Von dort führt „PS5 neu starten“ ganz normal zurück — es wird " +
      "nichts gelöscht."]
  };

  const wirePower = () => {
    Object.entries(POWER_ACTIONS).forEach(([sel, [action, , warn]]) => {
      const el = $(sel);
      if (!el) return;
      /* In der Kopfzeile ist wenig Platz: das Etikett bleibt kurz, die Folge nennt der Hinweis. */
      armConfirm(el, "Wirklich? Nochmal klicken", async () => {
        try {
          const r = await api("/api/v1/power", { method: "POST",
            body: JSON.stringify({ action }) });
          toast(r.message || "Befehl angenommen.");
        } catch (e) { toast(e.message, "error"); }
      }, warn, 800);
    });
  };

  /* ── Systemseite ────────────────────────────────────────────────── */

  const loadSystem = async () => {
    let sys;
    try { sys = await api("/api/v1/system"); } catch { return; }

    let diag = null;
    try { diag = await api("/api/v1/controller/diag"); } catch {}

    let chan = null;
    try { chan = await api("/api/v1/channels"); } catch {}

    let drive = null;
    let driveErr = "";
    let driveOff = false;
    try { drive = await api("/api/v1/drives"); }
    catch (e) {
      driveErr = e.message || "nicht verfügbar";
      /* The console answers 409 while the probe is switched off. */
      driveOff = e.code === "drive_probe_disabled";
    }

    txt("#console-model", sys.model || "PlayStation 5");
    /* txt() setzt textContent, kein innerHTML — hier gehört also das echte
       Zeichen hin und nicht &amp;, sonst stünde die Entity wörtlich da. */
    txt("#foot-version", `PS5 Cooling & System Center - Pro ${sys.app_version || ""}`.trim());

    /* Banner der Systemseite */
    txt("#sy-model", sys.model || "--");
    txt("#sy-fw", sys.firmware_version || "--");
    txt("#sy-uptime", sys.uptime_sec ? duration(sys.uptime_sec) : "--");

    const rows = [
      ["Modell", sys.model || "--"],
      ["Seriennummer", sys.serial_masked || "nicht verfügbar"],
      ["Firmware", sys.firmware_version || "--"],
      /* „Firmware (intern)" (Rohwert + Offset-Gruppe) stand hier einmal. Das
         war eine Entwicklerangabe — für die Bedienung sagt sie nichts, was
         die Zeile „Firmware" nicht schon sagt. /api/v1/system liefert
         firmware_raw und firmware_group weiterhin. */
      ["Laufzeit seit Start", sys.uptime_sec ? duration(sys.uptime_sec) : "--"],
      ["App-Version", sys.app_version || "--"]
    ];
    /* Nur zeigen, wenn die Konsole sie überhaupt meldet — sonst bleibt eine
       Zeile stehen, die dauerhaft „fehlt" sagt.
       Position über den Namen statt über einen Zählwert: Der feste Index 4
       zeigte auf die Zeile nach „Firmware (intern)" und wäre beim Entfernen
       jener Zeile stillschweigend verrutscht. */
    if (sys.firmware_reported && sys.firmware_reported !== sys.firmware_version)
      rows.splice(rows.findIndex(([k]) => k === "Firmware") + 1, 0,
                  ["Firmware laut System", sys.firmware_reported]);
    /* ⚠ Nicht auf c.connected prüfen. Das setzt nur der scePad-Pfad, und der
       wird einem Payload verweigert — die Zeile erschien deshalb nie. Der
       Ladestand kommt aus dem Kernelprotokoll (c.from_log) und ist völlig
       unabhängig davon, ob sich der Controller öffnen ließ. */
    const c = sys.controller;

    /* Erst der Controller, dann sein Akku — in dieser Reihenfolge liest es
       sich wie eine Aussage über ein Gerät statt wie zwei lose Zeilen.
       Ob überhaupt jemand spielt, steht nirgends sonst: die Controller-
       Schnittstelle verweigert unserer App die Auskunft, deshalb ist
       c.connected dauerhaft falsch. Ein protokollierter Druck auf die
       PS-Taste ist dagegen ein Beweis. */
    if (c && c.in_use) {
      const a = c.press_age_s;
      /* a < 0: Die App hat beim Start nur gezählt, wie viele Tastendrücke
         schon im Protokoll standen, und kann daraus keinen Zeitpunkt
         ableiten. Erst der nächste Druck liefert einen. Die Zeile trotzdem
         zeigen — dass ein Controller im Einsatz ist, steht ja fest. */
      rows.push(["Controller",
        a < 0       ? "wird benutzt — genauer Zeitpunkt ab dem nächsten Druck auf die PS-Taste"
      : a < 120     ? "gerade in Benutzung"
      : a < 5400    ? `zuletzt vor ${Math.round(a / 60)} Min. benutzt`
      : a < 172800  ? `zuletzt vor ${Math.round(a / 3600)} Std. benutzt`
                    : `zuletzt vor ${Math.round(a / 86400)} Tagen benutzt`]);
    }

    if (c && c.battery_pct !== undefined) {
      const zustand = c.full ? "voll geladen"
                    : c.charging ? "lädt"
                    : "im Akkubetrieb";
      let text = `${c.battery_pct} % · ${zustand}`;

      if (c.restored)
        text += " · aus letzter Speicherung";

      /* Der Wert wird beim Einschalten eines Controllers protokolliert, nicht
         laufend. Das Alter gehört deshalb dazu — eine Zahl ohne Alter würde
         Aktualität vortäuschen, die sie nicht hat. */
      if (c.from_log && c.age_s >= 0) {
        const a = c.age_s;
        const alt = a < 90        ? "gerade eben"
                  : a < 5400      ? `vor ${Math.round(a / 60)} Min.`
                  : a < 172800    ? `vor ${Math.round(a / 3600)} Std.`
                                  : `vor ${Math.round(a / 86400)} Tagen`;
        text += ` · Stand ${alt}`;

        /* Ab einer Stunde der Hinweis, wie man ihn auffrischt. Die Konsole
           schreibt den Ladestand nur beim Verbinden eines Controllers ins
           Protokoll; abfragen lässt er sich nicht (es gibt keine HID-
           Bibliothek im SDK). Ohne diesen Satz müsste man das wissen. */
        if (a >= 3600)
          text += " — Controller kurz aus- und einschalten für einen aktuellen Wert";
      }
      rows.push(["Controller-Akku", text]);
    }
    $("#sys-list").innerHTML = rows
      .map(([k, v]) => `<div><span>${esc(k)}</span><b>${esc(v)}</b></div>`).join("");

    const localeCard = $("#locale-expert-card");
    const localeList = $("#locale-expert-list");
    if (localeCard && localeList) {
      if (state.expertMode && sys.locale) {
        const locRows = [];
        if (sys.locale.language !== undefined)
          locRows.push(["Sprache (Code)", String(sys.locale.language)]);
        {
          const hint = localeHint(LOCALE_LANGUAGE_HINTS, sys.locale.language);
          if (hint)
            locRows.push(["Sprache (Best-Effort)", hint]);
        }
        if (sys.locale.time_zone !== undefined)
          locRows.push(["Zeitzone (Code)", String(sys.locale.time_zone)]);
        if (sys.locale.time_zone !== undefined && sys.locale.timezone_offset_min !== undefined)
          locRows.push(["Zeitzone (Best-Effort)", `${offsetToHm(sys.locale.timezone_offset_min)} (aus Offset)`]);
        if (sys.locale.timezone_offset_min !== undefined)
          locRows.push(["Zeitzonen-Offset", `${offsetToHm(sys.locale.timezone_offset_min)} (${sys.locale.timezone_offset_min} min)`]);
        if (sys.locale.region_code !== undefined)
          locRows.push(["Region (Code)", String(sys.locale.region_code)]);
        locRows.push(["Hinweis", "Rohcodes sind maßgeblich; Zuordnung kann je Firmware/Region abweichen."]);

        localeCard.classList.remove("is-hidden");
        localeList.innerHTML = locRows.length
          ? locRows.map(([k, v]) => `<div><span>${esc(k)}</span><b>${esc(v)}</b></div>`).join("")
          : `<p class="muted">Keine Locale-Rohwerte verfügbar.</p>`;
      } else {
        localeCard.classList.add("is-hidden");
        localeList.innerHTML = "";
      }
    }

    const fwCard = $("#firmware-expert-card");
    const fwList = $("#firmware-expert-list");
    if (fwCard && fwList) {
      if (state.expertMode) {
        const liveLoad = (state.status && state.status.load) ? state.status.load : {};
        const fwRows = [
          ["Firmware (roh)", sys.firmware_raw || "--"],
          ["Firmware-Gruppe", sys.firmware_group || "--"],
          ["Firmware (Text)", sys.firmware_reported || sys.firmware_version || "--"],
          /* /status carries cpu_mode only while it is valid and has no
             separate *_valid flag (that one belongs to /sensors/risky). */
          ["CPU-Modus (roh)", Number.isFinite(liveLoad.cpu_mode) ? String(liveLoad.cpu_mode) : "nicht verfügbar"]
        ];
        fwCard.classList.remove("is-hidden");
        fwList.innerHTML = fwRows
          .map(([k, v]) => `<div><span>${esc(k)}</span><b>${esc(v)}</b></div>`).join("");
      } else {
        fwCard.classList.add("is-hidden");
        fwList.innerHTML = "";
      }
    }

    const vols = sys.volumes || [];
    $("#storage-list").innerHTML = vols.length ? vols.map((v) => {
      const pct = Math.round((v.used_bytes / v.total_bytes) * 100);
      const cls = pct >= 92 ? "hot" : pct >= 80 ? "warn" : "s1";
      return `<div class="vol">
        <div class="vol-head"><b>${esc(v.label)}</b>
          <span>${bytes(v.free_bytes)} frei von ${bytes(v.total_bytes)}</span></div>
        <div class="bar"><span class="${cls}" style="width:${pct}%"></span></div>
      </div>`;
    }).join("") : `<p class="muted">Keine Laufwerke lesbar.</p>`;

    const dHost = $("#drive-telemetry-list");
    if (dHost) {
      if (drive && Array.isArray(drive.drives) && drive.drives.length) {
        const cnt = Number.isFinite(drive.count) ? Number(drive.count) : drive.drives.length;
        const daCnt = Number.isFinite(drive.raw_da_count) ? Number(drive.raw_da_count) : 0;
        const tempCnt = Number.isFinite(drive.temp_capable_count) ? Number(drive.temp_capable_count) : 0;
        const head = `<div><span>Volumes</span><b>${cnt}</b></div>
          <div><span>/dev/daN</span><b>${daCnt}</b></div>
          <div><span>Temp-Quellen</span><b>${tempCnt}</b></div>`;
        dHost.innerHTML = drive.drives.map((d) => {
          const used = Number(d.used_bytes) || 0;
          const total = Number(d.total_bytes) || 0;
          const pct = total > 0 ? Math.round((used / total) * 100) : 0;
          const cls = pct >= 92 ? "hot" : pct >= 80 ? "warn" : "s1";
          const temp = d.temp_valid ? `${d.temp_c} °C` : (d.temp_note || "nicht verfügbar");
          const kind = d.kind || "unknown";
          const dev = d.device || "kein Blockdevice";
          return `<div class="vol">
            <div class="vol-head"><b>${esc(dev)} · ${esc(d.label || "Laufwerk")}</b>
              <span>${esc(d.mount || "--")}</span></div>
            <div class="bar"><span class="${cls}" style="width:${pct}%"></span></div>
            <div class="kv-list" style="margin-top:8px">
              <div><span>Belegt</span><b>${bytes(used)} / ${bytes(total)}</b></div>
              <div><span>Typ</span><b>${esc(kind)}</b></div>
              <div><span>Temperatur</span><b>${esc(temp)}</b></div>
            </div>
          </div>`;
        }).join("");
        dHost.innerHTML = `<div class="kv-grid">${head}</div>${dHost.innerHTML}`;
      } else if (driveErr) {
        dHost.innerHTML = `<p class="muted">${esc(driveErr)}</p>`;
      } else {
        dHost.innerHTML = `<p class="muted">Keine /dev/daN-Laufwerke gemeldet.</p>`;
      }
    }

    capturePowerHistory(sys, state.status);
    renderPowerHistory();
    renderDiagMaster(sys, drive, driveErr, driveOff);

    const diagRows = [];
    if (diag && diag.ok) {
      const yesNo = (v) => v ? "ja" : "nein";
      diagRows.push(["Host-Test", yesNo(!!diag.host_test)]);
      const padSummary = !!(diag.scepad &&
        (diag.scepad.init || diag.scepad.open || diag.scepad.read_state ||
         diag.scepad.get_handle || diag.scepad.set_lightbar ||
         diag.scepad.reset_lightbar || diag.scepad.set_process_privilege));
      /* The three symbol flags are what the controller probe found when it
         looked (it is off by default); until then they are "not checked",
         not "absent". The two device nodes are looked at on every request. */
      const hidChecked = !!(diag.hid && diag.hid.resolved);
      const hidSummary = !!(diag.hid &&
        (diag.hid.dev_hid_present || diag.hid.dev_bluetooth_hid_present ||
         (hidChecked && (diag.hid.hidcontrol_get_battery_state ||
          diag.hid.hidcontrol_init || diag.hid.bluetoothhid_init))));
      const symState = (v) => hidChecked ? yesNo(!!v) : "nicht geprüft";
      diagRows.push(["Pad-Schnittstellen", yesNo(padSummary)]);
      diagRows.push(["HID-Schnittstellen", yesNo(hidSummary)]);

      if (state.expertMode) {
        if (diag.scepad) {
          diagRows.push(["scePadInit", yesNo(!!diag.scepad.init)]);
          diagRows.push(["scePadOpen", yesNo(!!diag.scepad.open)]);
          diagRows.push(["scePadReadState", yesNo(!!diag.scepad.read_state)]);
          diagRows.push(["scePadGetHandle", yesNo(!!diag.scepad.get_handle)]);
          diagRows.push(["scePadSetLightBar", yesNo(!!diag.scepad.set_lightbar)]);
          diagRows.push(["scePadResetLightBar", yesNo(!!diag.scepad.reset_lightbar)]);
          diagRows.push(["scePadSetProcessPrivilege",
            yesNo(!!diag.scepad.set_process_privilege)]);
          if (diag.scepad.cached_refused_rc)
            diagRows.push(["Letzte Ablehnung (rc)", `0x${(diag.scepad.cached_refused_rc >>> 0).toString(16).toUpperCase()}`]);
        }
        if (diag.hid) {
          diagRows.push(["/dev/hid vorhanden", yesNo(!!diag.hid.dev_hid_present)]);
          diagRows.push(["/dev/bluetooth_hid vorhanden",
            yesNo(!!diag.hid.dev_bluetooth_hid_present)]);
          diagRows.push(["sceHidControlGetBatteryState",
            symState(diag.hid.hidcontrol_get_battery_state)]);
          diagRows.push(["sceHidControlInit", symState(diag.hid.hidcontrol_init)]);
          diagRows.push(["sceBluetoothHidInit", symState(diag.hid.bluetoothhid_init)]);
        }
      }
    }
    $("#pad-diag-list").innerHTML = diagRows.length
      ? diagRows.map(([k, v]) => `<div><span>${esc(k)}</span><b>${esc(v)}</b></div>`).join("")
      : `<p class="muted">Noch keine Diagnose verfügbar.</p>`;

    const cExpert = $("#controller-expert-list");
    if (cExpert) {
      if (state.expertMode && c) {
        const exRows = [];
        if (c.status_byte !== undefined) exRows.push(["Statusbyte", String(c.status_byte)]);
        if (c.status_offset !== undefined) exRows.push(["Status-Offset", String(c.status_offset)]);
        if (c.candidates !== undefined) exRows.push(["Kandidaten", String(c.candidates)]);
        if (c.calibrated !== undefined) exRows.push(["Kalibriert", c.calibrated ? "ja" : "nein"]);
        if (c.raw) exRows.push(["Rohdaten", String(c.raw)]);
        if (c.from_log !== undefined) exRows.push(["Quelle", c.from_log ? "Kernel-Protokoll" : "Live-Abfrage"]);
        if (c.restored !== undefined) exRows.push(["Aus Sicherung", c.restored ? "ja" : "nein"]);

        cExpert.classList.remove("is-hidden");
        cExpert.innerHTML = exRows.length
          ? exRows.map(([k, v]) => `<div><span>${esc(k)}</span><b>${esc(v)}</b></div>`).join("")
          : `<p class="muted">Keine zusätzlichen Controller-Rohdaten vorhanden.</p>`;
      } else {
        cExpert.classList.add("is-hidden");
        cExpert.innerHTML = "";
      }
    }

    const rawBox = $("#diag-raw-box");
    if (rawBox) {
      if (state.expertMode && diag) {
        rawBox.classList.remove("is-hidden");
        rawBox.innerHTML = `<pre>${esc(JSON.stringify(diag, null, 2))}</pre>`;
      } else {
        rawBox.classList.add("is-hidden");
        rawBox.innerHTML = "";
      }
    }

    const live = state.status || {};
    const L = live.load || {};
    const K = L.clocks || null;
    const M = sys.memory || {};

    const PW = live.power || {};
    const LK = L.live_clocks || null;
    /* Grafiktemperatur seit 1.45.1: SoC-Kanal 7, vermessen nur auf der PS5
       Pro (Modellnummern CFI-7…). */
    const LT = live.temperatures || {};
    const gpuRows = [
      ["Grafiktemperatur", LT.gpu_valid
        ? `${num(LT.gpu_c)} °C · SoC-Kanal 7`
        : /^CFI-7/.test(sys.model || "") ? "nicht lesbar"
                                         : "nur auf der PS5 Pro vermessen"],
      ["GPU-Auslastung", PW.valid
        ? "kein Zähler vorhanden — siehe Grafikleistung"
        : "wird von dieser Firmware nicht bereitgestellt"]
    ];
    if (PW.valid && Array.isArray(PW.rails) && PW.rails.length >= 2) {
      gpuRows.push(["Grafikleistung", `${num(PW.gpu_w, 1)} W`]);
      gpuRows.push(["Grafikspannung", `${num(PW.rails[0].v, 3)} V · ${num(PW.rails[0].a, 1)} A`]);
      gpuRows.push(["Grafikspeicher", `${num(PW.mem_w, 1)} W`]);
    }
    if (LK && LK.gfx_mhz) {
      gpuRows.push(["Grafiktakt live", `${num(LK.gfx_mhz)} MHz`]);
      if (LK.gfx_limit_mhz) gpuRows.push(["Grafiktakt-Grenze", `${num(LK.gfx_limit_mhz)} MHz`]);
    }
    if (K && K.gfx_mhz) gpuRows.push(["Grafiktakt laut Energiemodus", `${num(K.gfx_mhz)} MHz`]);
    if (LK && LK.mem_mhz) gpuRows.push(["Speichertakt live", `${num(LK.mem_mhz)} MHz`]);
    else if (K && K.mem_mhz) gpuRows.push(["Speichertakt", `${num(K.mem_mhz)} MHz`]);
    if (LK && LK.fabric_mhz) gpuRows.push(["Fabric-Takt live", `${num(LK.fabric_mhz)} MHz`]);
    else if (K && K.fabric_mhz) gpuRows.push(["Fabric-Takt", `${num(K.fabric_mhz)} MHz`]);
    if (K && K.pcie) gpuRows.push(["PCIe", K.pcie]);
    if (M.pt_gpu_total)
      gpuRows.push(["Seitentabellen Grafik", `${M.pt_gpu_used} / ${M.pt_gpu_total}`]);
    /* K.pcie is a string the console read out of its own log. */
    $("#gpu-list").innerHTML = gpuRows
      .map(([k, v]) => `<div><span>${esc(k)}</span><b>${esc(v)}</b></div>`).join("");

    const chanRows = [];
    const chanHints = [];
    let chanCorr = "<span class=\"muted\">Zu wenig Daten für Korrelation.</span>";
    if (chan && chan.ok && Array.isArray(chan.channels)) {
      const lastValid = (arr) => {
        if (!Array.isArray(arr)) return null;
        for (let i = arr.length - 1; i >= 0; i--) {
          const v = Number(arr[i]);
          if (Number.isFinite(v) && v >= 0) return v;
        }
        return null;
      };
      const lastByChannel = new Map();
      chan.channels.forEach((series, idx) => {
        const v = lastValid(series);
        if (v !== null) {
          chanRows.push([`Kanal ${idx}`, `${num(v, 1)} °C`]);
          lastByChannel.set(idx, v);
        }
      });

      /* Gemessen statt vermutet (1.45.1): Kanal 7 folgt als einziger der
         Grafikleistung stärker als die übrigen, auf der PS5 Pro am 26. und
         27.09.2026. Dieselbe Messung hat die früheren Vermutungen widerlegt
         (Kanal 1 = Grafikschiene, Kanal 2 = CPU-Schiene, Kanal 5 als
         Kandidat); sie sind entfallen. Als Messergebnis steht der Hinweis
         auch bei „Nur bestätigte Rohsensorwerte". */
      const liveT = (state.status && state.status.temperatures) || {};
      if (liveT.gpu_valid && lastByChannel.has(7)) {
        chanHints.push(["Zuordnung Kanal 7", "Grafikeinheit, auf der PS5 Pro gemessen"]);
      }

      /* Grobe Lag-Analyse: bei steigendem Referenzkanal wird gezählt,
        welcher Kanal in den nächsten 3 Samples zuerst deutlich anzieht. */
      const n = Math.max(...chan.channels.map((s) => Array.isArray(s) ? s.length : 0));
      const ref = Array.isArray(chan.channels[0]) ? chan.channels[0] : [];
      const wins = new Array(chan.channels.length).fill(0);
      let events = 0;
      for (let i = 1; i < n - 3; i++) {
        const rv = Number(ref[i]);
        const rp = Number(ref[i - 1]);
        if (!Number.isFinite(rv) || !Number.isFinite(rp) || rv < 0 || rp < 0) continue;
        if ((rv - rp) < 0.5) continue;

        let bestCh = -1;
        let bestLag = 99;
        let bestDelta = -999;
        /* Ab Kanal 1: Kanal 0 ist der Bezug und steigt bei jedem Ereignis
           per Definition sofort. Bis 1.45.1 zählte er mit und gewann damit
           fast jedes Mal („Kanal 0 zieht zuerst an", 13 von 14). */
        for (let ch = 1; ch < chan.channels.length; ch++) {
          const s = chan.channels[ch];
          if (!Array.isArray(s)) continue;
          for (let lag = 0; lag <= 3; lag++) {
            const a = Number(s[i + lag]);
            const b = Number(s[i + lag - 1]);
            if (!Number.isFinite(a) || !Number.isFinite(b) || a < 0 || b < 0) continue;
            const d = a - b;
            if (d < 0.35) continue;
            if (lag < bestLag || (lag === bestLag && d > bestDelta)) {
              bestLag = lag;
              bestDelta = d;
              bestCh = ch;
            }
            break;
          }
        }
        if (bestCh >= 0) {
          wins[bestCh] += 1;
          events += 1;
        }
      }
      if (events >= 4) {
        let leader = 1;
        for (let i = 2; i < wins.length; i++) if (wins[i] > wins[leader]) leader = i;
        const pct = (wins[leader] / events) * 100;
        chanCorr = `Anstiege von Kanal 0 mit Folge: <b>${events}</b>. `
          + `Am häufigsten zieht <b>Kanal ${leader}</b> als Erster nach `
          + `(<b>${wins[leader]}</b> von ${events}, ${num(pct, 0)} %). `
          + `Hinweis: heuristische Zuordnung, keine Hardware-Labels.`;

        if (state.expertMode && !state.chanrawConfirmedOnly) {
          const ranked = wins
            .map((w, ch) => ({ ch, w }))
            .filter((x) => x.ch !== 0 && x.w > 0)
            .sort((a, b) => b.w - a.w)
            .slice(0, 3);
          if (ranked.length) {
            chanHints.push([
              "Heuristik Lastdynamik",
              `früh reagierende Kanäle: ${ranked.map((x) => `K${x.ch} (${x.w}/${events})`).join(", ")} · unbestätigt`
            ]);
          }
        }
      } else if (events > 0) {
        chanCorr = `Nur ${events} auswertbare Lastspitzen gefunden. `
          + `Für eine stabilere Zuordnung länger laufen lassen.`;
      }
    }
    const chanViewRows = chanRows.concat(chanHints);
    $("#chanraw-list").innerHTML = chanViewRows.length
      ? chanViewRows.map(([k, v]) => `<div><span>${esc(k)}</span><b>${esc(v)}</b></div>`).join("")
      : `<p class="muted">Noch keine Kanalwerte verfügbar.</p>`;
    if (state.chanrawConfirmedOnly) {
      chanCorr += "<br><small>Filter aktiv: Unbestätigte Heuristik-Hinweise sind ausgeblendet.</small>";
    }
    const corrEl = $("#chanraw-corr");
    if (corrEl) corrEl.innerHTML = chanCorr;

    /* Der Konsolenname kommt aus der Registry (SYSTEM_nickname) — der einzige
       Wert aus dieser Quelle, den diese Firmware tatsächlich pflegt. */
    if (sys.console_name) {
      const cm = $("#console-model");
      if (cm) cm.textContent = `${sys.console_name} · ${sys.model || "PlayStation 5"}`;
    }

     /* Arbeitsspeicher und Energiezustand — beides aus dem Systemprotokoll,
       nicht aus einer Abfrage. Deshalb mit Altersangabe. */
     const P = sys.power_state;
    if (M) {
      const mrows = [];
      if (M.rss_mb)    mrows.push(["Belegt", `${Math.round(M.rss_mb)} MB`]);
      if (M.kernel_mb) mrows.push(["Systemkern", `${Math.round(M.kernel_mb)} MB`]);
      if (M.wire_mb)   mrows.push(["Fest reserviert", `${Math.round(M.wire_mb)} MB`]);
      if (M.pt_cpu_total) {
        mrows.push(["Seitentabellen Prozessor",
          `${M.pt_cpu_used} / ${M.pt_cpu_total}`]);
        mrows.push(["Seitentabellen Grafik",
          `${M.pt_gpu_used} / ${M.pt_gpu_total}`]);
      }
      if (P && P.mode)          mrows.push(["Energiezustand", P.mode]);
      if (P && P.idle_s >= 0)   mrows.push(["Ohne Eingabe seit",
        P.idle_s >= 60 ? `${Math.round(P.idle_s / 60)} Min.` : `${P.idle_s} s`]);
      if (P && P.age_s >= 0)    mrows.push(["Stand",
        P.age_s < 90 ? "gerade eben" : `vor ${Math.round(P.age_s / 60)} Min.`]);

      /* The power mode is text from the console's log. */
      $("#mem-list").innerHTML = mrows.length
        ? mrows.map(([k, v]) => `<div><span>${esc(k)}</span><b>${esc(v)}</b></div>`).join("")
        : `<p class="muted">Noch keine Angaben im Protokoll.</p>`;

      /* Größte Verbraucher zuerst — die Liste ist sonst nur eine Wand. */
      const ps = (M.processes || []).slice().sort((a, b) => b.used_mb - a.used_mb);
      $("#mem-procs").innerHTML = ps.length ? ps.slice(0, 12).map((p) => {
        const pct = p.total_mb > 0
          ? Math.max(0, Math.min(100, (p.used_mb / p.total_mb) * 100)) : 0;
        const mine = /ps5tm/.test(p.name);
        return `<div class="vol">
          <div class="vol-head"><b>${esc(p.name)}${mine ? " · diese App" : ""}</b>
            <span>${p.used_mb.toFixed(1)} / ${p.total_mb.toFixed(1)} MB</span></div>
          <div class="bar"><span class="${mine ? "s3" : "s1"}" style="width:${pct}%"></span></div>
        </div>`;
      }).join("") : "";
    }

    renderUser(sys);

    /* Netzwerk und Bildschirm — beides beantwortet „ist das so eingerichtet,
       wie ich denke", nicht die Kühlung. */
    /* Network name, address and display name are set by other devices (the
       access point, the TV's EDID), so they are text, never markup. */
    const kv = (sel, rows) =>
      $(sel).innerHTML = rows.length
        ? rows.map(([k, v]) => `<div><span>${esc(k)}</span><b>${esc(v)}</b></div>`).join("")
        : `<p class="muted">Keine Angaben verfügbar.</p>`;

    /* "Nicht gemessen" ist etwas anderes als "nichts vorhanden". Ohne diese
       Unterscheidung stand hier "Keine Angaben verfügbar", während die Seite
       über genau dieses Netzwerk ausgeliefert wurde — und der Schalter, der
       es beheben würde, war nirgends erwähnt. */
    const probeHint = `<p class="muted">Abfrage ist ausgeschaltet — einschalten
      unter „Zusatzabfragen" im Reiter Einstellungen.</p>`;

    const n = sys.network || {};
    if (n.probed === false) $("#net-list").innerHTML = probeHint;
    else kv("#net-list", !n.up ? [] : [
      ["Verbindung", n.kind === "wlan" ? "WLAN" : "Netzwerkkabel"],
      ...(n.ssid ? [["Netzname", n.ssid]] : []),
      ...(n.ip ? [["IP-Adresse", n.ip]] : []),
      ...(n.signal_pct !== undefined ? [["Signalstärke", `${n.signal_pct} %`]] : []),
      ...(n.band_ghz !== undefined ? [["Frequenzband", `${n.band_ghz} GHz`]] : []),
      ...(n.link_mbit !== undefined ? [["Geschwindigkeit", `${n.link_mbit} Mbit/s`]] : [])
    ]);

    /* Der Bildschirm hängt an derselben Abfrage wie das Netzwerk. Ist sie an
       und kommt trotzdem nichts, liegt es nicht an der Konsole: die dafür
       nötige Systembibliothek wird bewusst nicht geladen (siehe netdisp.c).
       Das gehört hingeschrieben, statt die Karte ewig leer zu lassen. */
    const d = sys.display || {};
    if (n.probed === false) $("#disp-list").innerHTML = probeHint;
    else if (!sys.display) $("#disp-list").innerHTML =
      `<p class="muted">Noch keine Angaben. Die Abfrage läuft im Hintergrund —
       nach dem Einschalten dauert es einen Durchlauf, bis Werte erscheinen.</p>`;
    else kv("#disp-list", [
      ...(d.name ? [["Gerät", d.name]] : []),
      ...(d.width ? [["Auflösung", `${d.width} × ${d.height}`]] : []),
      ...(d.refresh_hz ? [["Bildwiederholrate", `${d.refresh_hz} Hz`]] : []),
      ...(d.hdr !== undefined ? [["HDR", d.hdr ? "aktiv" : "aus"]] : [])
    ]);

    if (state.status) {
      txt("#tile-msg", state.status.messages.tile);

      /* Hochladen und Installieren nur zeigen, solange es etwas zu tun gibt.
         Steht die Kachel, sind zwei Knöpfe und eine Anleitung für einen
         erledigten Vorgang bloß Beiwerk — die Statuszeile genügt. */
      const setup = $("#tile-setup");
      if (setup)
        setup.style.display =
          state.status.adapters.tile === "installed" ? "none" : "";
    }
  };

  const loadRiskyTelemetry = async (opts = {}) => {
    const host = $("#risky-list");
    if (!host) return;
    if (!isRiskyProbeEnabled()) {
      host.innerHTML = `<p class="muted">Abfrage ist ausgeschaltet — einschalten unter „Zusatzabfragen".</p>`;
      updateRiskyAutoState();
      return;
    }
    if (state.riskyBusy) return;
    state.riskyBusy = true;
    if (!opts.silent) host.innerHTML = `<p class="muted">Messung läuft …</p>`;
    try {
      const d = await api("/api/v1/sensors/risky");
      const l = d.load || {};
      const rows = [];
      rows.push(["Zeitpunkt", d.timestamp_ms
        ? new Date(d.timestamp_ms).toLocaleTimeString(LOCALE)
        : "--"]);
      rows.push(["CPU gesamt (16 logische)", l.cpu_valid ? `${num(l.cpu_pct, 1)} %` : "nicht verfügbar"]);
      if (Number.isFinite(l.game_pct)) rows.push(["Spiel-CPUs", `${num(l.game_pct, 1)} %`]);
      if (Number.isFinite(l.system_pct)) rows.push(["System-CPUs", `${num(l.system_pct, 1)} %`]);
      if (Array.isArray(l.cores) && l.cores.length) {
        const coreText = l.cores.map((v, i) => {
          if (!Number.isFinite(v) || v < 0) return `K${i + 1}: -`;
          return `K${i + 1}: ${num(v, 0)} %`;
        }).join(" · ");
        rows.push(["Kerne (je 2 logische)", coreText]);
      }
      if (Array.isArray(l.cpus) && l.cpus.length) {
        const sysMask = Number(l.system_cpu_mask) || 0;
        const cpuText = l.cpus.map((v, i) => {
          const tag = (sysMask >> i) & 1 ? "S" : "";
          return `${i}${tag}: ${Number.isFinite(v) && v >= 0 ? `${num(v, 0)} %` : "-"}`;
        }).join(" · ");
        rows.push(["Logische CPUs (S = System)", cpuText]);
      }
      rows.push(["CPU-Takt", l.cpu_mhz_valid ? `${num(l.cpu_mhz)} MHz` : "nicht verfügbar"]);
      const lk = l.live_clocks;
      if (lk && Array.isArray(lk.core_mhz))
        rows.push(["Kerntakt live", lk.core_mhz.map((v) => num(v)).join(" · ") + " MHz"]);
      if (lk && lk.gfx_mhz)
        rows.push(["Grafiktakt live", `${num(lk.gfx_mhz)} MHz${lk.gfx_limit_mhz ? ` (Grenze ${num(lk.gfx_limit_mhz)})` : ""}`]);
      rows.push(["CPU-Modus", l.cpu_mode_valid ? String(l.cpu_mode) : "nicht verfügbar"]);
      const p = d.power || {};
      if (p.valid && Array.isArray(p.rails)) {
        rows.push(["Leistung, die sich bewegt", `${num(p.live_w, 1)} W`]);
        rows.push(["Summe aller Schienen", `${num(p.total_w, 1)} W${p.cpu_live ? "" : " (enthält stillstehende Werte)"}`]);
        p.rails.forEach((r, i) => {
          const still = !p.cpu_live && (i === 2 || i === 3) ? " — steht still, kein Messwert" : "";
          rows.push([`Schiene ${r.name}`, `${num(r.w, 2)} W · ${num(r.v, 3)} V · ${num(r.a, 2)} A${still}`]);
        });
        if (p.aux && Array.isArray(p.aux.bytes))
          rows.push(["Zusatzwerte (ungedeutet)", `${p.aux.bytes.join(" / ")} · ${(p.aux.words || []).join(" / ")}`]);
      } else {
        rows.push(["SoC-Leistung", l.soc_power_valid ? `${num(l.soc_power_w, 1)} W` : "nicht verfügbar"]);
      }
      /* Wer die Prozessoren beschäftigt — alle 10 s aus denselben
         Thread-Zeiten wie die Last. */
      if (Array.isArray(d.top_threads) && d.top_threads.length)
        rows.push(["Aktivste Threads (10 s)", d.top_threads
          .map((t) => `${t.name || "?"}${t.own ? " (diese App)" : ""} ${num(t.pct, 0)} %`)
          .join(" · ")]);
      host.innerHTML = rows
        .map(([k, v]) => `<div><span>${esc(k)}</span><b>${esc(v)}</b></div>`)
        .join("");
    } catch (e) {
      host.innerHTML = `<p class="muted">${esc(e.message || "nicht verfügbar")}</p>`;
    } finally {
      state.riskyBusy = false;
    }
  };

  /* ── Auslastung je Kern ─────────────────────────────────────────── */

  const renderCores = (s) => {
    const box = $("#core-list");
    if (!box) return;
    const cores = (s && s.load && s.load.cores) || [];
    if (!cores.length) {
      box.innerHTML = `<p class="muted">Die Konsole meldet keine Einzelwerte.</p>`;
      return;
    }
    /* Ab 1 gezählt, wie in der Prozessor-Karte der Übersicht — dieselben acht
       Kerne dort als 1–8 und hier als 0–7 zu zeigen, ist nur verwirrend.
       −1 heißt „nicht gemessen" und wird als Strich gezeigt, nicht als
       „−1 %". */
    const bar = (label, pct) => {
      const un  = pct < 0;
      const val = un ? 0 : Math.max(0, Math.min(100, pct));
      const cls = pct >= 90 ? "hot" : pct >= 70 ? "warn" : "s1";
      return `<div class="vol">
        <div class="vol-head"><b>${label}</b><span>${un ? "–" : `${pct} %`}</span></div>
        <div class="bar"><span class="${cls}" style="width:${val}%"></span></div>
      </div>`;
    };
    /* Seit 1.45.0 sind das die acht physischen Kerne, jeweils der Mittelwert
       ihrer zwei logischen CPUs. Bis 1.44.0 standen hier die logischen CPUs
       0–7 unter dem Namen „Kern 1–8" — alles Spielkerne; die Last des Systems
       blieb unsichtbar. */
    let html = cores.map((pct, i) => bar(`Kern ${i + 1}`, pct)).join("");
    const cpus = (s && s.load && s.load.cpus) || [];
    if (cpus.length) {
      const sysMask = Number(s.load.system_cpu_mask) || 0;
      html += `<p class="muted mt-12">Logische CPUs — Spiele bekommen elf, das System
        fünf (mit „System" markiert).</p>`;
      html += cpus.map((pct, i) =>
        bar(`CPU ${i}${(sysMask >> i) & 1 ? " · System" : ""}`, pct)).join("");
    }
    box.innerHTML = html;
  };

  /* ── Protokoll ──────────────────────────────────────────────────── */

  /* A log line carries whatever the app was told about: game names from the
     library, paths, other processes' names. It is data, never markup, and the
     level only selects one of three known classes. */
  const logEntryHtml = (e) => {
    const lv = String(e.level).toLowerCase();
    return `
            <div class="entry">
              <time>${new Date(e.timestamp_ms).toLocaleTimeString(LOCALE)}</time>
              <span class="lvl ${lv === "warn" || lv === "error" ? lv : "info"}">${esc(e.level)}</span>
              <div><p>${esc(e.message)}</p><code>${esc(e.code)}</code></div>
            </div>`;
  };

  const loadLog = async () => {
    try {
      const d = await api("/api/v1/logs");
      txt("#log-count", `${d.entries.length} Einträge`);
      /* Banner: Zählung nach Stufe (die App schreibt INFO, WARN, ERROR) */
      const byLevel = (lv) => d.entries.filter((e) => String(e.level).toUpperCase() === lv).length;
      txt("#lg-total", d.entries.length);
      txt("#lg-warn", byLevel("WARN"));
      txt("#lg-err", byLevel("ERROR"));
      $("#log-list").innerHTML = d.entries.length
        ? d.entries.slice().reverse().map(logEntryHtml).join("")
        : `<div class="empty-state"><img src="/img/empty-log.png" alt="">
            <p class="muted">Noch keine Ereignisse.</p></div>`;
    } catch (err) {
      $("#log-list").innerHTML = `<p class="muted">${esc(err.message)}</p>`;
    }
  };

  const loadLogTail = async () => {
    /* One request at a time: with the eight-second limit a slow console would
       otherwise collect up to four of them at the two-second beat. */
    if (!state.logTailOn || state.logTailBusy) return;
    state.logTailBusy = true;
    try {
      const d = await api("/api/v1/logs/tail?count=25");
      const box = $("#log-tail-box");
      if (!box) return;
      box.innerHTML = (d.entries || []).length
        ? d.entries.slice().reverse().map(logEntryHtml).join("")
        : `<p class="muted">Noch keine Live-Einträge.</p>`;
    } catch (e) {
      const box = $("#log-tail-box");
      if (box) box.innerHTML = `<p class="muted">${esc(e.message)}</p>`;
    } finally {
      state.logTailBusy = false;
    }
  };

  /* The live log runs only while it can be seen: switched on, on its own page,
     and with the page in front. Called whenever one of the three changes. */
  const syncLogTail = () => {
    const run = state.logTailOn && state.page === "system" && !document.hidden;
    if (run && !state.logTailTimer) {
      loadLogTail();
      state.logTailTimer = setInterval(loadLogTail, 2000);
    } else if (!run && state.logTailTimer) {
      clearInterval(state.logTailTimer);
      state.logTailTimer = null;
    }
  };

  /* ── Kernel-Log live (klog.c) ───────────────────────────────────────
   * Die Seite fragt im Sekundentakt, solange der Reiter vorn ist; der Server
   * schickt nur, was nach der letzten Zeilennummer kam. „Pause“ hält das
   * Fragen an, und danach kommt nach, was in der Zwischenzeit geschah (bis zu
   * ein paar tausend Zeilen; was darüber hinausgeht, ist eine Lücke, die als
   * Zeile dasteht). Die Zeilen sind Daten des Kernels und gehen nur als Text
   * in die Seite. */
  const KL_KEEP = 5000;               /* Zeilen, die die Seite behält */
  const KL_SHOW = 1500;               /* Zeilen, die zugleich im Fenster stehen */
  const kl = { tab: "app", lines: [], last: 0, timer: 0, busy: false, paused: false,
               follow: true, preset: "", inc: [], exc: [], err: "",
               lastSec: -1, lastClock: "", typing: 0 };

  const KL_RE_ERR   = /error|fail|crash|abnormal|panic|fatal|exception|\[ERR\]/i;
  const KL_RE_WARN  = /warn|retry|timeout|denied|refus/i;
  /* Was die Konsole alle paar Sekunden von selbst schreibt: Speicherberichte,
     Bildwechsel, Oberfläche, Ressourcenverwaltung. */
  const KL_RE_QUIET = /FMEM|dce-ft|Libc Heap|\[SceShellUI\] [IW]\/|ResArbitrator|AsyncStorage|AvControl|\[SceSystemStateMgr\]|BAPM|MP1\]|UninstallRecommend|CloudMessaging|LOGIN MGR|morpheus/;
  const KL_RE_GAMES = /SceLncService|Syscore App|AppMgr|EXEC \/app0|\[GAME\]|MDBG|crash/i;
  const klPreset = {
    quiet: (t) => !KL_RE_QUIET.test(t),
    app:   (t) => t.indexOf("[ps5tm.elf]") >= 0,
    games: (t) => KL_RE_GAMES.test(t),
    err:   (t) => KL_RE_ERR.test(t),
  };

  const klClock = (ms) => {
    const sec = Math.floor(ms / 1000);
    if (sec === kl.lastSec) return kl.lastClock;
    const d = new Date(ms), p2 = (n) => (n < 10 ? "0" : "") + n;
    kl.lastSec = sec;
    kl.lastClock = `${p2(d.getHours())}:${p2(d.getMinutes())}:${p2(d.getSeconds())}`;
    return kl.lastClock;
  };

  /* Hinweiszeilen der App bleiben immer stehen, auch bei einem Filter. */
  const klMatch = (e) => {
    if (e.m) return true;
    if (kl.preset && !klPreset[kl.preset](e.s)) return false;
    if (!kl.inc.length && !kl.exc.length) return true;
    const low = e.s.toLowerCase();
    return kl.inc.every((w) => low.indexOf(w) >= 0) &&
           !kl.exc.some((w) => low.indexOf(w) >= 0);
  };

  const klNode = (e) => {
    const d = document.createElement("div");
    d.className = "kl-line" + (e.m ? " kl-mark"
      : KL_RE_ERR.test(e.s) ? " kl-err"
      : KL_RE_WARN.test(e.s) ? " kl-warn"
      : e.s.indexOf("[ps5tm.elf]") >= 0 ? " kl-app" : "");
    const t = document.createElement("time");
    t.textContent = klClock(e.t);
    const x = document.createElement("span");
    x.textContent = e.s;
    d.appendChild(t);
    d.appendChild(x);
    return d;
  };

  const klSub = () => {
    const sub = $("#kl-sub"), box = $("#kl-view");
    if (!sub) return;
    sub.textContent = kl.err ||
      `${num(box ? box.childNodes.length : 0)} von ${num(kl.lines.length)} Zeilen` +
      (kl.paused ? " · angehalten" : "");
  };

  const klRenderAll = () => {
    const box = $("#kl-view");
    if (!box) return;
    const keep = [];
    for (let i = kl.lines.length - 1; i >= 0 && keep.length < KL_SHOW; i--)
      if (klMatch(kl.lines[i])) keep.push(kl.lines[i]);
    keep.reverse();
    const frag = document.createDocumentFragment();
    keep.forEach((e) => frag.appendChild(klNode(e)));
    box.textContent = "";
    box.appendChild(frag);
    if (kl.follow) box.scrollTop = box.scrollHeight;
    klSub();
  };

  const klAppend = (fresh) => {
    const box = $("#kl-view");
    if (!box) return;
    const frag = document.createDocumentFragment();
    let n = 0;
    fresh.forEach((e) => { if (klMatch(e)) { frag.appendChild(klNode(e)); n++; } });
    if (!n) return;
    box.appendChild(frag);
    while (box.childNodes.length > KL_SHOW) box.removeChild(box.firstChild);
    if (kl.follow) box.scrollTop = box.scrollHeight;
  };

  const klPoll = async () => {
    if (kl.busy) return;
    kl.busy = true;
    try {
      /* Nach einer Pause können mehr als 800 Zeilen warten: gleich weiterfragen. */
      for (let round = 0; round < 8; round++) {
        const d = await api(`/api/v1/klog?after=${kl.last}&max=800`, { timeoutMs: 6000 });
        kl.err = "";
        const fresh = [];
        if (d.lost && kl.last)
          fresh.push({ n: 0, t: Date.now(), m: 1,
            s: "— Lücke: Zeilen fehlen (die Seite hat zu lange nicht gefragt, oder die App wurde neu gestartet) —" });
        d.lines.forEach((e) => fresh.push(e));
        if (d.lines.length) kl.last = d.lines[d.lines.length - 1].n;
        if (fresh.length) {
          fresh.forEach((e) => kl.lines.push(e));
          if (kl.lines.length > KL_KEEP) kl.lines.splice(0, kl.lines.length - (KL_KEEP - 1000));
          klAppend(fresh);
        }
        if (!d.more) break;
      }
    } catch (e) {
      kl.err = e.message;
    } finally {
      kl.busy = false;
      klSub();
    }
  };

  /* Läuft nur, solange man es sehen kann: Reiter vorn, Seite vorn, nicht angehalten. */
  const syncKlog = () => {
    const run = state.page === "log" && kl.tab === "klog" && !kl.paused && !document.hidden;
    if (run && !kl.timer) {
      klPoll();
      kl.timer = setInterval(klPoll, 1000);
    } else if (!run && kl.timer) {
      clearInterval(kl.timer);
      kl.timer = 0;
    }
  };

  const klSelectTab = (which) => {
    kl.tab = which;
    $("#log-pane-app").hidden  = which !== "app";
    $("#log-pane-klog").hidden = which !== "klog";
    [["app", "#log-tab-app"], ["klog", "#log-tab-klog"]].forEach(([k, sel]) => {
      const b = $(sel);
      b.classList.toggle("active", which === k);
      b.setAttribute("aria-selected", which === k ? "true" : "false");
    });
    if (which === "app") loadLog();
    if (which === "klog") klFilesLoad();
    syncKlog();
    klRecSync();
  };

  /* ── Abfragen ───────────────────────────────────────────────────── */

  const refresh = async () => {
    try {
      const s = await api("/api/v1/status");
      renderStatus(s);
      evaluateMonitorAlert(s);
      if (state.page === "system") renderCores(s);
    } catch {
      const conn = $("#conn");
      conn.classList.remove("on"); conn.classList.add("off");
      txt("#conn-text", "Keine Verbindung");
      document.body.classList.add("offline");   /* zeigt .offline-note */
    }
  };

  /* ── Spiele (1.46.0) ────────────────────────────────────────────────
   * Die Spiele-Reihe des Startbildschirms aus /api/v1/library, in der
   * Reihenfolge der Konsole. Cover kommen über /api/v1/library/cover und
   * dürfen im Browser-Cache bleiben: Ihre URL trägt den Zeitstempel der
   * Konsole und ändert sich mit dem Bild. */
  /* open: Karten mit aufgeklappten Infos — das Raster wird bei jedem Filtern
     und bei jedem Spielwechsel neu gebaut und soll sie dabei nicht zuklappen. */
  const gm = { list: [], meta: null, error: "", loaded: false,
               plat: "", q: "", sort: "recent", running: "", open: new Set(),
               wanted: null /* {id, name, at}: Start, der an einem laufenden Spiel scheiterte */ };

  /* Manuelles Hochladen von AMPR-EMU-/PlayGo-Bibliotheken (eigener
     Backport-Ordner) und von fertig gebauten AMPR-EMU-Asset-Packs gab es
     beide einmal hier, beide am 02.10.2026 wieder entfernt: ShadowMountPlus'
     Overlay-Mount für einen eigenen Backport-Ordner schlägt bei
     exFAT-Ordner-Titeln zuverlässig mit "Operation not permitted" fehl und
     blockiert dann den Spielstart komplett (zweimal live reproduziert); das
     Asset-Pack-Hochladen funktionierte zwar technisch, wurde aber auf
     ausdrücklichen Wunsch ebenfalls entfernt. Die Erkennung, ob ein Spiel
     Backport/AMPR EMU/PlayGo selbst mitbringt (gmTags() unten), bleibt
     unverändert — die liest nur den Spielordner, baut nichts ein. */

  const gmRunningId = () => {
    const g = (state.status && state.status.game) || {};
    return g.foreground && g.title_id ? String(g.title_id) : "";
  };
  /* Läuft überhaupt, auch hinter dem Browser der Konsole: Dort steht das
     Spiel nie im Vordergrund, solange diese Seite offen ist. */
  const gmLiveId = () => {
    const g = (state.status && state.status.game) || {};
    return g.title_id ? String(g.title_id) : "";
  };

  const gmDate = (iso, withTime) => {
    if (!iso) return "—";
    const d = new Date(iso);
    if (isNaN(d.getTime())) return "—";
    return withTime
      ? d.toLocaleString(LOCALE, { day: "2-digit", month: "2-digit", year: "numeric",
                                    hour: "2-digit", minute: "2-digit" })
      : d.toLocaleDateString(LOCALE, { day: "2-digit", month: "2-digit", year: "numeric" });
  };

  const gmHasMods = (x) => !!(x.mods && x.mods.state === "checked"
    && (x.mods.backport || x.mods.ampr_emu || x.mods.playgo));

  /* Form der Spieldaten (library.c, detect_storage): wie ShadowMountPlus sie
     meldet, sonst aus mount.lnk und /user/app geschlossen. */
  const GM_FORMATS = {
    folder: "Dump-Ordner",
    exfat:  "exFAT-Abbild (.exfat)",
    ffpkg:  "ffpkg (UFS-Abbild)",
    ffpfs:  "ffpfs (PFS-Abbild)",
    ffpfsc: "ffpfsc (komprimiertes PFS)",
    image:  "Abbild (ShadowMountPlus)",
    pkg:    "PKG (installiert)"
  };

  /* Das Laufwerk zu einem Pfad, wie die System-Seite es nennt. */
  const gmDrive = (p) => {
    if (!p) return "";
    if (/^\/(data|user)\//.test(p)) return "Interne SSD";
    const usb = p.match(/^\/mnt\/usb(\d)/);
    if (usb) return `USB-Speicher ${Number(usb[1]) + 1}`;
    if (/^\/mnt\/ext[01]\//.test(p)) return "M.2-Erweiterung";
    return "";
  };
  const gmDir = (p) => (p && p.lastIndexOf("/") > 0 ? p.slice(0, p.lastIndexOf("/") + 1) : p || "");
  const gmBase = (p) => (p ? p.slice(p.lastIndexOf("/") + 1) : "");

  /* Das Format steht als Marke in derselben Reihe wie Backport, AMPR EMU und
     PlayGo: kurz auf der Karte, ausführlich im Hinweis (und unter „Infos &
     Metadaten"). Ein installiertes Paket heißt nach der Plattform PS4 PKG oder
     PS5 PKG. Ob es ein echtes oder ein gefälschtes Paket (fpkg) ist, lässt
     sich nicht lesen: Beide liegen als /user/app/<ID>/app.pkg, mit denselben
     Angaben in der Datenbank — deshalb gibt es dafür keine eigene Marke. */
  const GM_FORMAT_TAGS = {
    folder: "Dump-Ordner",
    exfat:  "exFAT",
    ffpkg:  "ffpkg",
    ffpfs:  "ffpfs",
    ffpfsc: "ffpfsc",
    image:  "Abbild"
  };
  const gmFormatLabel = (x) => {
    if (x.format === "pkg")
      return x.platform === "PS4" ? "PS4 PKG" : x.platform === "PS5" ? "PS5 PKG" : "PKG";
    return GM_FORMAT_TAGS[x.format] || "";
  };

  const gmTags = (x) => {
    const m = x.mods;
    const fmt = gmFormatLabel(x);
    const t = fmt
      ? [`<span class="gm-tag fmt" title="${esc(GM_FORMATS[x.format] || x.format)}">${esc(fmt)}</span>`]
      : [];
    if (!m) return t.join("");
    if (m.state !== "checked") {
      /* Kurz, damit die Marke neben dem Format in eine Zeile passt; der Grund
         steht im Hinweis. */
      t.push(`<span class="gm-tag unk" title="${esc("Backport, AMPR EMU und PlayGo lassen sich gerade nicht prüfen. "
        + (m.reason || ""))}">Anpassungen unbekannt</span>`);
      return t.join("");
    }
    if (m.backport) {
      const why = [];
      if (m.eboot_sdk && x.sdk && m.eboot_sdk !== x.sdk)
        why.push(`eboot.bin auf SDK ${m.eboot_sdk} abgesenkt (param.json: SDK ${x.sdk})`);
      if (m.backport_libs > 0)
        why.push(`${m.backport_libs} Systembibliothek(en) im Ordner ${m.fakelib2 ? "fakelib2" : "fakelib"}`
          + (m.libs_from === "folder" ? " (Backport-Ordner neben dem Spiel)" : ""));
      t.push(`<span class="gm-tag bp" title="${esc(why.join(" · "))}">Backport</span>`);
    }
    if (m.ampr_emu)
      t.push(`<span class="gm-tag ampr" title="${esc((m.fakelib2 ? "fakelib2" : "fakelib") + "/libSceAmpr.sprx"
        + (m.ampr_index ? " · ampr_emu.index vorhanden: die Emulation lief schon" : ""))}">AMPR EMU</span>`);
    if (m.playgo)
      t.push(`<span class="gm-tag pg" title="${esc((m.fakelib2 ? "fakelib2" : "fakelib") + "/libScePlayGo.sprx"
        + (m.playgo_log ? " · playlgo.log vorhanden" : ""))}">PlayGo</span>`);
    return t.join("");
  };

  /* Die Knöpfe einer Karte: auf jeder Karte dieselben vier an derselben Stelle
     (Starten; Kopieren und Verschieben nebeneinander; Konvertieren darunter).
     So liegen sie in einer Reihe von Karten auf einer Linie, auch wenn eine
     Karte das eine oder andere nicht kann. Was nicht geht, ist ausgegraut, und
     der Hinweis dazu sagt warum. */
  const gmButtons = (x, live) => {
    const id = esc(x.title_id);
    const pkg = x.format === "pkg";
    const why = {
      launch: "Für diese Kachel gibt es keinen Startlink.",
      copy: pkg ? "Installierte Spiele (PKG) lassen sich nicht kopieren: Es gibt weder einen Ordner noch eine Abbilddatei."
        : !x.path ? "Wo die Spieldaten dieses Titels liegen, ist nicht bekannt."
        : "Dieses Spiel lässt sich nicht kopieren.",
      move: pkg ? "Installierte Spiele (PKG) lassen sich nicht verschieben."
        : !x.smp ? "Nur für Spiele, die ShadowMountPlus verwaltet."
        : "ShadowMountPlus kann dieses Spiel gerade nicht verschieben (Laufwerk nicht erreichbar, oder es bietet die Funktion nicht an).",
      convert: pkg ? "Installierte Spiele (PKG) lassen sich nicht konvertieren."
        : !x.path ? "Wo die Spieldaten dieses Titels liegen, ist nicht bekannt."
        : "Konvertieren geht nur bei Dump-Ordnern und bei Abbildern (exFAT, ffpkg, ffpfs, ffpfsc)."
    };
    const btn = (cls, attrs, label, ok, reason) =>
      `<button type="button" class="btn${cls}" ${attrs}${ok ? "" : ` disabled title="${esc(reason)}"`}>${label}</button>`;
    /* Das laufende Spiel bekommt zusätzlich „Spiel beenden" (wie „Close App"
       in Elf Arsenal); die Seite fragt vorher nach. */
    const close = live
      ? `<button type="button" class="btn gm-close" data-close="${id}" title="Beendet das Spiel sofort, ohne Rückfrage. Was darin nicht gespeichert ist, geht verloren.">Spiel beenden</button>`
      : "";
    return `${btn(" primary gm-start", `data-launch="${id}"`, "Starten", !!x.can_launch, why.launch)}
          ${close}
          <div class="gm-more">
            ${btn("", `data-copy="${id}"`, "Kopieren", !!x.can_copy, why.copy)}
            ${btn("", `data-store="move" data-id="${id}"`, "Verschieben", !!x.can_move, why.move)}
            ${btn(" gm-conv", `data-convert="${id}"`, "Konvertieren", !!(x.can_convert || x.can_unpack), why.convert)}
            ${btn(" gm-del", `data-delete="${id}"`, "Löschen", true, "")}
          </div>`;
  };

  const gmCard = (x, running, live) => {
    const m = x.mods && x.mods.state === "checked" ? x.mods : null;
    const rows = [
      ["Titel-ID", x.title_id],
      ["Content-ID", x.content_id || "—", "mono"],
      ["Version", x.version || "—"]
    ];
    /* Benötigte Firmware: bei PS5-Titeln requiredSystemSoftwareVersion aus
       param.json, bei PS4-Titeln SYSTEM_VER aus param.sfo — die PS4-Firmware,
       gegen die die Konsole nicht verglichen wird. */
    if (x.system_ver) {
      const cfw = gm.meta && gm.meta.console_fw;
      const newer = x.platform === "PS5" && cfw && parseFloat(x.system_ver) > parseFloat(cfw);
      let why = "";
      if (newer) {
        why = `Neuer als die Firmware der Konsole (${cfw}).`;
        if (m && m.eboot_sdk && parseFloat(m.eboot_sdk) <= parseFloat(cfw))
          why += ` Die eboot.bin nutzt SDK ${m.eboot_sdk}, damit startet es trotzdem.`;
      }
      rows.push([x.platform === "PS4" ? "Benötigte PS4-Firmware" : "Benötigte Firmware",
                 newer ? `${x.system_ver} (Konsole ${cfw})` : x.system_ver,
                 newer ? "warn" : "", why]);
    }
    if (x.sdk) rows.push(["SDK", x.sdk]);
    rows.push(["Spielzeit", x.played_s > 0 ? duration(x.played_s) : "—"]);
    rows.push(["Zuletzt gespielt", gmDate(x.last_played, true)]);
    rows.push(["Installiert", gmDate(x.installed, false)]);
    /* Bei eingehängten Spielen zählt die Konsole nur den Teil auf der
       internen SSD — die Spieldaten liegen woanders. */
    rows.push([x.source ? "Größe intern" : "Größe",
               x.size_bytes > 0 ? bytes(x.size_bytes) : "—"]);
    /* Wo die Daten wirklich liegen: bei einem Abbild die Datei, nicht der
       Ordner unter /mnt/shadowmnt, in den ShadowMountPlus es einhängt. */
    if (x.format) rows.push(["Format", x.format === "pkg" && x.platform
      ? `${x.platform} PKG (installiert)` : (GM_FORMATS[x.format] || x.format)]);
    if (x.path) {
      const drive = gmDrive(x.path);
      rows.push(["Speicherort", gmDir(x.path), "mono"]);
      if (drive) rows.push(["Laufwerk", drive]);
      rows.push([x.format === "folder" || x.format === "pkg" ? "Ordner" : "Datei",
                 gmBase(x.path), "mono"]);
    }
    if (x.source && x.source.startsWith("/mnt/shadowmnt"))
      rows.push(["Eingehängt unter", x.source, "mono"]);
    else if (x.source && !x.path) rows.push(["Spieldaten", x.source, "mono"]);
    if (m && m.eboot_sdk && x.sdk && m.eboot_sdk !== x.sdk)
      rows.push(["eboot.bin", `SDK ${m.eboot_sdk}`]);
    if (m && m.fakelib && m.fakelib.length) {
      rows.push([m.fakelib2 ? "fakelib2" : "fakelib", m.fakelib.join(", ")
        + (m.fakelib_more ? ` +${m.fakelib_more}` : ""), "mono"]);
      rows.push(["Bibliotheken aus", m.libs_from === "folder"
        ? `dem Backport-Ordner (…/backports/${x.title_id})` : "dem Spielordner bzw. Abbild"]);
    }

    /* Ohne \p{L}: Ein Browser, der das nicht kennt, verwirft das ganze
       Skript, nicht nur diese Zeile. */
    const initials = x.name.split(/\s+/)
      .map((w) => w.replace(/[^0-9A-Za-zÀ-ÿ]/g, "")).filter(Boolean)
      .slice(0, 2).map((w) => w[0]).join("").toUpperCase();
    const tags = gmTags(x);
    /* Die Angaben stehen hinter „Infos & Metadaten" und klappen darunter
       auf; zugeklappt bleiben die Karten kurz genug für einen Blick über
       die ganze Reihe. */
    const open = gm.open.has(x.title_id);
    return `<article class="gm-card${running ? " running" : ""}">
      <div class="gm-cover">
        <span class="gm-fallback">${esc(initials || "?")}</span>
        <img src="${esc(x.cover)}" alt="" loading="lazy" decoding="async">
        ${x.platform ? `<span class="gm-plat">${esc(x.platform)}</span>` : ""}
        ${running ? `<span class="gm-live">läuft</span>` : ""}
      </div>
      <div class="gm-body">
        <h3 title="${esc(x.name)}">${esc(x.name)}</h3>
        ${tags ? `<div class="gm-tags">${tags}</div>` : ""}
        <div class="gm-actions">
          <button type="button" class="btn gm-info" data-info="${esc(x.title_id)}"
            aria-expanded="${open ? "true" : "false"}">Infos &amp; Metadaten</button>
          <dl class="gm-meta"${open ? "" : " hidden"}>${rows.map(([k, v, cls, tip]) =>
            `<dt>${esc(k)}</dt><dd${cls ? ` class="${cls}"` : ""}${tip ? ` title="${esc(tip)}"` : ""}>${esc(v)}</dd>`).join("")}</dl>
          ${gmButtons(x, live)}
        </div>
      </div>
    </article>`;
  };

  /* „Starten" startet das Spiel direkt und vorn, wie der Homebrew Launcher
     (library.c, gemessen am 29.09.2026); läuft es schon im Hintergrund, holt
     die App es über den Link seiner Kachel nach vorn. Öffnet die Kachel nur
     eine Webseite, kommt eine Meldung mit Start-Taste.
     Läuft ein anderes Spiel, startet die App nichts und schickt auch keine
     Meldung: Sie fragt, ob genau dieses Spiel beendet und das gewünschte
     danach gestartet werden soll (Wunsch des Users, 03.10.2026). Erst nach
     „Ja" geht eine zweite Anfrage mit close=<Titel-ID> hinaus; die Konsole
     beendet das Spiel im Hintergrund und startet dann das neue (api.c).
     Ein und dasselbe Spiel wird nie zweimal gestartet — das brachte laut
     Elf Arsenal die Konsole zum Absturz. */
  const onPs5 = /PlayStation/i.test(navigator.userAgent);

  /* Im PS5-Browser startet ein Spiel hinter der Seite: Die Konsole nimmt
     der Seite im Vordergrund den Fokus nicht weg. Die App bittet deshalb die
     Systemoberfläche, zum Startbildschirm zu gehen — das schließt den
     Browser — und holt das Spiel dann über den Link seiner Kachel nach vorn
     (library.c, gemessen am 29.09.2026). Steht die Seite danach noch da,
     hat die Konsole nicht mitgespielt: Dann hilft die Kreistaste. */
  const gmShowStarted = (name, closing) => {
    let o = $("#gm-overlay");
    if (!o) {
      o = document.createElement("div");
      o.id = "gm-overlay";
      o.className = "gm-overlay";
      document.body.appendChild(o);
      o.addEventListener("click", () => o.remove());
    }
    const box = (text) => `<div class="gm-overlay-box">
        <b>▶ „${esc(name)}“ läuft</b>
        <p>${text}</p>
        <button type="button" class="btn">OK</button>
      </div>`;
    const circle = "Drücke die Kreistaste ◯, um diese Oberfläche zu schließen. "
                 + "Dahinter wartet das Spiel.";
    o.innerHTML = box(closing ? "Diese Oberfläche schließt sich, das Spiel kommt nach vorn."
                              : circle);
    if (closing) setTimeout(() => { if (o.isConnected) o.innerHTML = box(circle); }, 6000);
  };

  /* Ein Hinweis ohne Knopf (Wunsch vom 03.10.2026), als Overlay wie
     gmShowStarted: Er schließt sich nach `ms` von selbst, ein Tipp darauf
     schließt ihn sofort. Namen gehen durch esc(): Sie stammen aus den
     Metadaten der Spiele. */
  const gmNotice = (title, text, ms = 5000) => {
    const o = document.createElement("div");
    o.className = "gm-overlay";
    o.innerHTML = `<div class="gm-overlay-box" role="alertdialog" aria-live="assertive">
        <b>${esc(title)}</b>
        <p>${esc(text)}</p>
      </div>`;
    let timer = 0;
    const done = () => { clearTimeout(timer); o.remove(); };
    timer = setTimeout(done, ms);
    o.addEventListener("click", done);
    document.body.appendChild(o);
  };

  /* Das Beenden läuft auf der Konsole weiter, auch wenn diese Seite dabei
     verschwindet (im PS5-Browser schließt der Start den Browser). Solange sie
     da ist, fragt sie nach, wie es ausging. `next`: das Spiel, das danach
     gestartet werden soll — die Meldung sagt dann, dass es jetzt geht. */
  const gmFollowClose = async (next) => {
    for (let i = 0; i < 30; i++) {
      await new Promise((r) => setTimeout(r, 1000));
      let st;
      try { st = await api("/api/v1/library/close", { timeoutMs: 4000 }); } catch (e) { continue; }
      if (st.state === "done") {
        toast(next ? `${st.message} Jetzt kannst du „${next}“ starten.` : st.message);
        return;
      }
      if (st.state === "failed") { toast(st.message, "error"); return; }
    }
    toast("Keine Rückmeldung der Konsole. Was geschah, steht im Protokoll.", "error");
  };

  /* „Spiel beenden“ beendet sofort, ohne Rückfrage (Wunsch vom 03.10.2026).
     Gewarnt wird vorher, im Hinweis „läuft noch“ und am Knopf selbst. */
  const gmCloseGame = async (id, name, btn) => {
    if (btn) btn.disabled = true;
    /* Kam vorher der Hinweis „läuft noch“, nennt die Meldung danach das Spiel,
       das gestartet werden sollte. */
    const w = gm.wanted;
    gm.wanted = null;
    const next = w && w.id !== id && Date.now() - w.at < 120000 ? w.name : "";
    try {
      await api("/api/v1/library/close", { method: "POST", body: JSON.stringify({ id }) });
      toast(`„${name}“ wird beendet.`);
      await gmFollowClose(next);
    } catch (e) {
      toast(e.message, "error");
    }
    if (btn) setTimeout(() => { btn.disabled = false; }, 2000);
  };

  const gmLaunch = async (id, btn) => {
    const g = gm.list.find((x) => x.title_id === id);
    const name = g ? g.name : id;
    btn.disabled = true;
    try {
      const r = await api("/api/v1/library/launch", { method: "POST", body: JSON.stringify({ id }) });
      if (r.already_starting) {
        toast(`„${name}“ startet bereits. Bitte einen Moment warten.`);
        setTimeout(() => { btn.disabled = false; }, 3000);
        return;
      }
      if (r.started || r.brought_forward || r.already_front) {
        toast(r.started ? `„${name}“ wird gestartet.`
          : r.brought_forward ? `„${name}“ wird nach vorn geholt.`
          : `„${name}“ läuft schon im Vordergrund.`);
        if ((r.started || r.brought_forward) && (r.on_console || onPs5))
          gmShowStarted(name, !!r.closing);
        setTimeout(() => { btn.disabled = false; }, 3000);
        return;
      }
      toast(r.already_running
        ? `„${name}“ läuft schon. Die Meldung auf der PS5 holt es nach vorn.`
        : r.other_running
          ? `Es läuft noch ein anderes Spiel. Auf der PS5 „Starten“ wählen, die Konsole fragt vor dem Schließen nach.`
          : `„${name}“: Meldung an die PS5 geschickt. Dort oben rechts „Starten“ wählen.`);
    } catch (e) {
      if (e.code === "other_running" && e.data && e.data.running) {
        /* Nur ein Hinweis, kein Knopf, nach 5 s wieder weg (Wunsch vom
           03.10.2026). Beendet wird ausdrücklich über „Spiel beenden“ — auf der
           Karte des laufenden Spiels oder bei „Läuft gerade“ —, gestartet
           danach mit einem eigenen Tipp auf „Starten“. */
        const run = e.data.running;
        gm.wanted = { id, name, at: Date.now() };
        gmNotice(`„${run.name}“ läuft noch`,
          `Bevor „${name}“ starten kann, beende „${run.name}“ zuerst mit „Spiel beenden“ `
          + `(auf seiner Karte oder bei „Läuft gerade“). Achtung: Das Spiel wird sofort `
          + `geschlossen und speichert vorher nicht – was du darin nicht selbst gespeichert `
          + `hast, geht verloren.`);
      } else {
        toast(e.message, "error");
      }
    }
    setTimeout(() => { btn.disabled = false; }, 3000);
  };

  /* ── Kopieren (1.46.0) ────────────────────────────────────────────────
   * Ein Spiel, das aus einem Ordner läuft, auf ein anderes Laufwerk
   * kopieren (gamecopy.c). Je Laufwerk zwei Ziele, die Wahl trifft der
   * Benutzer: der Ordner homebrew, den ShadowMountPlus durchsucht, oder
   * PS5-Sicherung/Spiele, drei Ebenen tief, wo ShadowMountPlus nie sucht.
   * Die Kopie läuft auf der Konsole weiter, auch wenn das Fenster zu ist;
   * der Streifen über den Karten zeigt sie dann an. */
  const cp = { id: "", plan: null, drive: "", mode: "", job: null,
               view: "", timer: 0 };

  const cpBytes = (b) => (b > 0 ? bytes(b) : "0 B");
  const cpFiles = (n) => `${num(n)} ${n === 1 ? "Datei" : "Dateien"}`;
  const cpEta = (s) => (!(s > 0) ? "" : s < 60 ? "noch unter 1 Min."
    : `noch etwa ${duration(s)}`);
  const cpFs = (fs) => ({ exfatfs: "exFAT", msdosfs: "FAT32", ufs: "UFS",
                          nullfs: "UFS" }[fs] || fs);

  /* What the finished job says about its checksum file: where it is and how a
     PC uses it, or why there is none. The name is text from the console. */
  const cpSums = (j, what) => {
    let html = "";
    if (j.sums) {
      const base = String(j.sums).split("/").pop();
      html += `<p class="muted">Prüfsummen (SHA-256 im Format von sha256sum): <code>${esc(j.sums)}</code>.
        Am PC prüft <code>sha256sum -c ${esc(base)}</code> in dem Ordner, der ${esc(what)} und diese Datei
        enthält, die Sicherung jederzeit später.</p>`;
    }
    if (j.note) html += `<p class="cp-why">${esc(j.note)}</p>`;
    return html;
  };

  /* One dialog serves copying, converting and moving. Every open and every
     close takes a new number, and an answer to a question asked under an older
     number is dropped: scanning a plan can take a minute, and by then the
     dialog may belong to another game or another action. A move deletes its
     source, so a plan must never end up under the wrong title. */
  const dlg = { seq: 0 };

  const cpClose = () => {
    const o = $("#cp-overlay");
    if (o) o.remove();
    cp.view = "";
    dlg.seq++;
  };

  const cpShell = () => {
    let o = $("#cp-overlay");
    if (!o) {
      o = document.createElement("div");
      o.id = "cp-overlay";
      o.className = "gm-overlay";
      o.innerHTML = `<div class="cp-dialog" role="dialog" aria-modal="true" aria-labelledby="cp-title">
          <div class="cp-head"><h3 id="cp-title">Kopieren</h3>
            <button type="button" class="cp-x" data-cp="close" aria-label="Schließen">×</button></div>
          <div id="cp-body"></div>
        </div>`;
      document.body.appendChild(o);
      /* Dasselbe Fenster für Kopieren (cp…) und für Verschieben und
         Entpacken durch ShadowMountPlus (mv…); cp.view sagt, was drin ist. */
      o.addEventListener("click", (e) => {
        if (e.target === o) { cpClose(); return; }
        const b = e.target.closest("[data-cp]");
        if (!b || b.disabled) return;
        const act = b.dataset.cp;
        const kind = cp.view.slice(0, 2);
        const start = kind === "mv" ? mvStart : kind === "cv" ? cvStart : cpStart;
        const cancel = kind === "mv" ? mvCancel : kind === "cv" ? cvCancel : cpCancel;
        if (act === "close") cpClose();
        else if (act === "start") start(b);
        else if (act === "cancel") cancel(b);
      });
      o.addEventListener("change", (e) => {
        const n = e.target.name || "";
        if (n === "cp-drive") cp.drive = e.target.value;
        if (n === "cp-mode") cp.mode = e.target.value;
        if (n === "mv-dest") mv.dest = e.target.value;
        if (n === "mv-del") mv.del = e.target.checked;
        if (n === "cv-drive") cv.drive = e.target.value;
        if (n === "cv-mode") cv.mode = e.target.value;
        if (n === "cv-op") {
          if (e.target.value === "unpack") { mvOpen(cv.id, "unpack"); return; }
          cv.op = e.target.value;
        }
        if (n.startsWith("mv-")) mvRenderPlan();
        else if (n.startsWith("cv-")) cvRenderPlan();
        else cpRenderPlan();
      });
    }
    return o;
  };

  /* Warum ein Laufwerk für die gewählte Art nicht in Frage kommt, oder "". */
  const cpBlocked = (d) => {
    const p = cp.plan;
    if (d.fat32_too_big) return "FAT32 nimmt keine Datei über 4 GB auf";
    if (!d.enough_space) return `Zu wenig Platz: Die Kopie braucht ${bytes(p.size_bytes)}`;
    const dest = cp.mode && d[cp.mode];
    /* The drive-wide figure above is the best case over both destinations.
       What counts is the one chosen: its own unfinished leftover is removed
       first and gives its room back, which only that destination gets. */
    if (dest && dest.enough_space === false)
      return `Zu wenig Platz: Die Kopie braucht ${bytes(p.size_bytes)}`;
    if (dest && dest.path_too_long) return "Ein Pfad im Spiel würde dort zu lang";
    if (dest && dest.path === p.source) return "Hier liegt das Original";
    if (dest && dest.exists) return "Dort gibt es diesen Ordner schon";
    return "";
  };

  const cpRenderPlan = () => {
    const p = cp.plan;
    const body = $("#cp-body");
    if (!p || !body) return;
    cp.view = "plan";
    txt("#cp-title", `„${p.name}“ kopieren`);

    const drives = p.targets.map((d) => {
      const why = cpBlocked(d);
      if (why && cp.drive === d.mount) cp.drive = "";
      return `<label class="cp-choice${why ? " off" : ""}">
          <input type="radio" name="cp-drive" value="${esc(d.mount)}"
            ${cp.drive === d.mount ? "checked" : ""} ${why ? "disabled" : ""}>
          <span class="cp-choice-text"><b>${esc(d.label)}</b>
            <span class="muted">${cpBytes(d.free_bytes)} frei${d.fs ? ` · ${esc(cpFs(d.fs))}` : ""}${d.source_here ? " · hier liegt das Original" : ""}</span>
            ${why ? `<span class="cp-why">${esc(why)}</span>` : ""}</span>
        </label>`;
    }).join("");

    const sel = p.targets.find((d) => d.mount === cp.drive);
    const dest = sel && cp.mode ? sel[cp.mode] : null;
    const blockedRun = p.running ? "Das Spiel läuft gerade. Bitte erst beenden: Es schreibt womöglich in seinen Ordner."
                     : p.busy ? "Es läuft schon eine Kopie."
                     : p.other_busy ? "Es läuft gerade eine Konvertierung. Kopieren, Konvertieren und Verschieben laufen nicht gleichzeitig." : "";
    const ready = dest && !blockedRun;

    body.innerHTML = `
      <p class="cp-src"><span class="muted">Quelle</span>
        <code>${esc(p.source)}</code>
        <span>${bytes(p.size_bytes)} in ${cpFiles(p.files)}${p.skipped
          ? ` · ${num(p.skipped)} Verknüpfungen oder Sonderdateien bleiben weg` : ""}</span></p>

      <h4 class="cp-step">1. Als was?</h4>
      <div class="cp-list">
        <label class="cp-choice">
          <input type="radio" name="cp-mode" value="homebrew" ${cp.mode === "homebrew" ? "checked" : ""}>
          <span class="cp-choice-text"><b>A · Spielbar</b>
            <span class="muted">In den Ordner <code>homebrew</code> des Ziels.
              ShadowMountPlus findet die Kopie dort und bindet sie ein.</span></span>
        </label>
        <label class="cp-choice">
          <input type="radio" name="cp-mode" value="backup" ${cp.mode === "backup" ? "checked" : ""}>
          <span class="cp-choice-text"><b>B · Sicherung</b>
            <span class="muted">In den Ordner <code>PS5-Sicherung/Spiele</code>.
              Dort sucht ShadowMountPlus nicht, die Kopie wird nur aufbewahrt.</span></span>
        </label>
      </div>
      ${cp.mode === "homebrew" ? `<div class="cp-warn"><b>⚠ ShadowMountPlus findet das Spiel
          danach zweimal</b>: das Original und die Kopie, beide mit der Titel-ID
          ${esc(p.title_id)}. Zwei Ordner mit derselben Titel-ID können beim Einbinden
          Probleme machen: Es wird womöglich der falsche eingebunden oder das Spiel
          doppelt gemeldet. ShadowMountPlus rät, je Titel-ID nur einen Ordner zu
          behalten. Wolltest du das Spiel verschieben, lösche nach der Kopie das Original.</div>` : ""}

      <h4 class="cp-step">2. Wohin?</h4>
      <div class="cp-list">${drives || `<p class="muted">Kein Laufwerk gefunden.</p>`}</div>

      ${dest ? `<p class="cp-dest"><span class="muted">Ziel</span> <code>${esc(dest.path)}</code>
        ${dest.unfinished ? `<span class="cp-why">Dort liegt eine abgebrochene Kopie dieser App, sie wird ersetzt${
          dest.unfinished_bytes > 0 ? ` (${bytes(dest.unfinished_bytes)} werden dabei frei)` : ""}.</span>` : ""}</p>` : ""}
      ${blockedRun ? `<p class="cp-err">${esc(blockedRun)}</p>` : ""}
      ${dest ? `<p class="muted">Jede Datei wird gleich nach dem Schreiben vom Ziel zurückgelesen und mit
        dem Original verglichen; das dauert etwa noch einmal so lange wie das Kopieren. Daneben entsteht
        <code>${esc(dest.path)}.sha256</code> mit den Prüfsummen (SHA-256).</p>` : ""}

      <div class="cp-actions">
        <button type="button" class="btn" data-cp="close">Abbrechen</button>
        <button type="button" class="btn primary" data-cp="start" ${ready ? "" : "disabled"}>Kopieren starten</button>
      </div>`;
  };

  const cpRenderJob = () => {
    const j = cp.job;
    const body = $("#cp-body");
    if (!j || !body) return;
    cp.view = "job";
    txt("#cp-title", `„${j.name || j.title_id}“ kopieren`);
    /* Every byte is worked twice — written, then read back from the target —
       and both count towards the bar. */
    const work = (j.done_bytes || 0) + (j.checked_bytes || 0);
    const pct = j.total_bytes > 0 ? Math.min(100, Math.floor(work * 100 / (2 * j.total_bytes))) : 0;
    const where = `<p class="cp-dest"><span class="muted">Ziel</span> <code>${esc(j.target || "")}</code></p>`;
    let html;
    if (j.state === "scanning") {
      html = `<p>Der Spielordner wird vermessen …</p>${where}`;
    } else if (j.state === "copying" || j.state === "finishing") {
      html = `
        <div class="cp-bar"><div style="width:${pct}%"></div></div>
        <p class="cp-num"><b>${pct} %</b> · ${cpBytes(j.done_bytes)} von ${bytes(j.total_bytes)} kopiert,
          ${cpBytes(j.checked_bytes)} zurückgelesen · ${num(j.files_done)} von ${cpFiles(j.files_total)}</p>
        <p class="muted">${j.state === "finishing" ? "Die letzten Dateien werden geschrieben …"
          : `${j.bytes_per_s ? `${bytes(j.bytes_per_s)}/s` : "Tempo wird gemessen"}${j.eta_s ? ` · ${cpEta(j.eta_s)}` : ""}`}</p>
        ${j.current ? `<p class="cp-cur"><code>${esc(j.current)}</code></p>` : ""}
        ${where}
        <p class="muted">Jede Datei wird gleich nach dem Schreiben vom Ziel zurückgelesen und mit dem Original
          verglichen. Die Kopie läuft auf der PS5 weiter, auch wenn du dieses Fenster schließt.</p>`;
    } else if (j.state === "done") {
      /* What was really written, not what was planned: the console only calls a
         copy done when it matches the plan, so these are the same or more. */
      html = `<p class="cp-ok">✓ Fertig: ${bytes(j.done_bytes)} in ${cpFiles(j.files_done)} kopiert und zurückgelesen.</p>${where}
        ${cpSums(j, "die Kopie")}
        ${j.mode === "homebrew" ? `<p class="muted">ShadowMountPlus bindet die Kopie bei seinem nächsten
          Durchlauf ein. Das Spiel liegt jetzt zweimal vor. Wenn du es nur verschieben wolltest,
          lösche das Original.</p>` : `<p class="muted">Die Sicherung wird nicht eingebunden.</p>`}`;
    } else if (j.state === "cancelled") {
      html = `<p>Abgebrochen. Was schon kopiert war, hat die App wieder entfernt
        (oder ersetzt es beim nächsten Versuch, falls das Laufwerk das Löschen verweigert).</p>${where}`;
    } else if (j.state === "failed") {
      html = `<p class="cp-err">✗ ${esc(j.error || "Die Kopie ist gescheitert.")}</p>${where}
        <p class="muted">Was schon kopiert war, hat die App wieder entfernt, oder sie ersetzt es beim
          nächsten Versuch, falls das Laufwerk das Löschen verweigert. Das Original ist unberührt.</p>`;
    } else {
      html = `<p class="muted">Keine Kopie.</p>`;
    }
    const active = !!j.active;
    body.innerHTML = `${html}
      <div class="cp-actions">
        ${active ? `<button type="button" class="btn" data-cp="cancel">Kopie abbrechen</button>` : ""}
        <button type="button" class="btn${active ? "" : " primary"}" data-cp="close">${active ? "Im Hintergrund weiter" : "Schließen"}</button>
      </div>`;
  };

  /* Streifen über den Karten: eine laufende Kopie oder ein laufender Auftrag
     von ShadowMountPlus, oder einer, der in dieser Sitzung zu Ende ging.
     ShadowMountPlus meldet seinen letzten Auftrag für immer — alte Aufträge,
     auch aus seiner eigenen Oberfläche, bleiben deshalb weg. */
  const gmName = (id) => { const g = gm.list.find((x) => x.title_id === id); return g ? g.name : id; };
  const mvVerb = (op) => ({ move: "Verschieben", unpack: "Entpacken", copy: "Kopieren",
                           delete: "Löschen" }[op] || op || "Auftrag");
  const jobBanner = () => {
    const b = $("#gm-copy-banner");
    if (!b) return;
    const c = cp.job && cp.job.state !== "idle" ? cp.job : null;
    const m = mv.job && mv.job.job_id > 0 && (mv.job.active || mv.job.job_id === mv.seen) ? mv.job : null;
    const k = cv.job && cv.job.state !== "idle" ? cv.job : null;
    let label = "", live = false, kind = "";
    if (k && k.active) {
      const pct = k.total_bytes > 0 ? Math.min(100, Math.floor(k.done_bytes * 100 / k.total_bytes)) : 0;
      const cpct = k.check_total_bytes > 0
        ? Math.min(100, Math.floor(k.check_done_bytes * 100 / k.check_total_bytes)) : 0;
      label = `Konvertieren läuft: „${k.name || k.title_id}“ → ${
        k.op === "exfat" ? "exFAT" : k.op === "ffpkg" ? "ffpkg" : "ffpfsc"} · ${
        k.state === "scanning" ? "wird vermessen" : k.state === "verifying" ? `wird geprüft ${cpct} %` : `${pct} %`}`;
      live = true; kind = "convert";
    } else if (m && m.active) {
      const pct = Math.floor(Number(m.progress_percent) || 0);
      label = `ShadowMountPlus · ${mvVerb(m.operation)}: „${gmName(m.title_id)}“ · ${pct} %`;
      live = true; kind = "store";
    } else if (c && c.active) {
      /* written and read back count together, as in the dialog */
      const pct = c.total_bytes > 0
        ? Math.min(100, Math.floor(((c.done_bytes || 0) + (c.checked_bytes || 0)) * 100 / (2 * c.total_bytes))) : 0;
      label = `Kopie läuft: „${c.name || c.title_id}“ → ${c.drive || ""} · ${c.state === "scanning" ? "wird vermessen" : `${pct} %`}`;
      live = true; kind = "copy";
    } else if (m) {
      label = `ShadowMountPlus · ${mvVerb(m.operation)} ${m.state === "completed" ? "fertig"
        : m.state === "failed" ? "gescheitert" : "abgebrochen"}: „${gmName(m.title_id)}“`;
      kind = "store";
    } else if (k) {
      label = k.state === "done" ? `Konvertieren fertig: „${k.name || k.title_id}“ → ${k.target || ""}`
        : k.state === "failed" ? `Konvertieren gescheitert: „${k.name || k.title_id}“`
        : `Konvertieren abgebrochen: „${k.name || k.title_id}“`;
      kind = "convert";
    } else if (c) {
      label = c.state === "done" ? `Kopie fertig: „${c.name || c.title_id}“ → ${c.drive || ""}`
        : c.state === "failed" ? `Kopie gescheitert: „${c.name || c.title_id}“`
        : `Kopie abgebrochen: „${c.name || c.title_id}“`;
      kind = "copy";
    }
    b.hidden = !label;
    b.textContent = label;
    b.dataset.kind = kind;
    b.classList.toggle("live", live);
  };
  const cpBanner = jobBanner;

  const cpPoll = async () => {
    clearTimeout(cp.timer);
    try {
      cp.job = await api("/api/v1/library/copy");
    } catch (e) {
      cp.timer = setTimeout(cpPoll, 3000);
      return;
    }
    cpBanner();
    if (cp.view === "job") cpRenderJob();
    if (cp.job.active) cp.timer = setTimeout(cpPoll, 1000);
  };

  const cpOpen = async (id) => {
    cpShell();
    const seq = ++dlg.seq;
    /* Eine laufende Kopie geht vor: Es gibt nur eine auf einmal. */
    if (cp.job && cp.job.active || !id) {
      if (!cp.job) await cpPoll();
      if (seq !== dlg.seq) return;
      if (cp.job && cp.job.state !== "idle") { cpRenderJob(); return; }
      if (!id) { cpClose(); return; }
    }
    cp.id = id; cp.plan = null; cp.drive = ""; cp.mode = ""; cp.view = "loading";
    const g = gm.list.find((x) => x.title_id === id);
    txt("#cp-title", `„${g ? g.name : id}“ kopieren`);
    $("#cp-body").innerHTML = `<p class="muted">Der Spielordner wird vermessen …</p>`;
    try {
      const plan = await api(`/api/v1/library/copy/plan?id=${encodeURIComponent(id)}`,
                             { timeoutMs: 120000 });
      if (seq !== dlg.seq || !$("#cp-overlay")) return;
      cp.plan = plan;
      const first = plan.targets.find((d) => !d.source_here && d.enough_space && !d.fat32_too_big);
      cp.drive = first ? first.mount : "";
      cpRenderPlan();
    } catch (e) {
      if (seq !== dlg.seq || !$("#cp-overlay")) return;
      $("#cp-body").innerHTML = `<p class="cp-err">${esc(e.message)}</p>
        <div class="cp-actions"><button type="button" class="btn primary" data-cp="close">Schließen</button></div>`;
    }
  };

  const cpStart = async (btn) => {
    btn.disabled = true;
    try {
      cp.job = await api("/api/v1/library/copy", { method: "POST",
        body: JSON.stringify({ id: cp.id, target: cp.drive, mode: cp.mode }) });
      cpRenderJob();
      cpBanner();
      cp.timer = setTimeout(cpPoll, 1000);
    } catch (e) {
      toast(e.message, "error");
      btn.disabled = false;
    }
  };

  const cpCancel = async (btn) => {
    btn.disabled = true;
    try {
      await api("/api/v1/library/copy/cancel", { method: "POST" });
      toast("Die Kopie wird abgebrochen.");
    } catch (e) {
      toast(e.message, "error");
    }
    cpPoll();
  };

  /* ── Verschieben und Konvertieren (1.46.0) ───────────────────────────
   * Beides macht ShadowMountPlus selbst (gamemove.c): Es weiß, welches
   * Spiel wo eingetragen und was eingehängt ist. Die App plant die Ziele —
   * nur Ordner, in denen ShadowMountPlus sucht — und zeigt den Auftrag an.
   * Konvertieren heißt bisher: ein Abbild in einen Dump-Ordner entpacken.
   * Die andere Richtung beherrscht noch keiner der beiden. */
  const mv = { id: "", op: "", plan: null, dest: "", del: false, job: null,
               seen: 0, timer: 0, misses: 0 };

  const MV_STATES = { preparing: "Wird vorbereitet", measuring: "Wird vermessen",
    transferring: "", deleting: "Alter Ort wird geräumt", finalizing: "Wird abgeschlossen" };

  const mvBlocked = (d) => {
    const p = mv.plan;
    if (p.op === "move" && d.here) return "Hier liegt das Spiel schon";
    if (d.fat32_too_big) return "FAT32 nimmt keine Datei über 4 GB auf";
    if (!d.enough_space) return `Zu wenig Platz: Nötig sind mindestens ${bytes(p.size_bytes)}`;
    /* Both operations refuse an existing target now (the console re-checks at
       the start): unpack names a folder after the title, move keeps the name. */
    if (d.target_exists)
      return p.op === "unpack" ? `Dort gibt es den Ordner ${p.title_id} schon`
                               : `Dort gibt es schon „${gmBase(p.source)}“`;
    return "";
  };

  const mvRenderPlan = () => {
    const p = mv.plan;
    const body = $("#cp-body");
    if (!p || !body) return;
    cp.view = "mvplan";
    const g = gm.list.find((x) => x.title_id === p.title_id);
    const fmt = g && g.format ? (GM_FORMATS[g.format] || g.format) : "";
    const unpack = p.op === "unpack";
    txt("#cp-title", `„${gmName(p.title_id)}“ ${unpack ? "konvertieren" : "verschieben"}`);

    const rows = p.destinations.map((d) => {
      const why = mvBlocked(d);
      if (why && mv.dest === d.path) mv.dest = "";
      return `<label class="cp-choice${why ? " off" : ""}">
          <input type="radio" name="mv-dest" value="${esc(d.path)}"
            ${mv.dest === d.path ? "checked" : ""} ${why ? "disabled" : ""}>
          <span class="cp-choice-text"><b><code>${esc(d.path)}/</code></b>
            <span class="muted">${esc(d.drive)} · ${cpBytes(d.free_bytes)} frei${d.fs ? ` · ${esc(cpFs(d.fs))}` : ""}${d.exists ? "" : " · Ordner wird angelegt"}</span>
            ${why ? `<span class="cp-why">${esc(why)}</span>` : ""}</span>
        </label>`;
    }).join("");

    const sel = p.destinations.find((d) => d.path === mv.dest);
    const target = sel ? (unpack ? sel.target : `${sel.path}/${gmBase(p.source)}`) : "";
    const blocked = p.running ? "Das Spiel läuft gerade. Bitte erst beenden." : "";
    const intro = unpack
      ? `<p class="muted">ShadowMountPlus entpackt das Abbild in einen Dump-Ordner namens
          <code>${esc(p.title_id)}</code> im gewählten Ziel.${p.image_type === "pfsc"
          ? " Ein komprimiertes Abbild wird dabei deutlich größer als die Datei." : ""}
          Die andere Richtung, also ein Ordner als exFAT-, ffpkg-, ffpfs- oder ffpfsc-Abbild,
          beherrscht bisher weder ShadowMountPlus noch diese App.</p>`
      : `<p class="muted">ShadowMountPlus verschiebt das Spiel selbst: Es kopiert die Daten,
          trägt das Spiel am neuen Ort ein und löscht es am alten erst, wenn alles angekommen
          ist. Zur Wahl stehen nur Ordner, in denen ShadowMountPlus sucht, damit das Spiel
          spielbar bleibt.</p>`;

    body.innerHTML = `
      <p class="cp-src"><span class="muted">Jetzt</span> <code>${esc(p.source)}</code>
        <span>${fmt ? `${esc(fmt)} · ` : ""}${bytes(p.size_bytes)}</span></p>
      ${intro}
      <h4 class="cp-step">${unpack ? "Wohin entpacken?" : "Wohin?"}</h4>
      <div class="cp-list">${rows || `<p class="muted">Kein Ziel gefunden.</p>`}</div>
      ${unpack ? `<label class="cp-choice cp-check"><input type="checkbox" name="mv-del" ${mv.del ? "checked" : ""}>
          <span class="cp-choice-text"><b>Abbild danach löschen</b>
            <span class="muted">Erst wenn das Entpacken fertig und fehlerfrei ist.</span></span></label>` : ""}
      ${target ? `<p class="cp-dest"><span class="muted">Ziel</span> <code>${esc(target)}</code></p>` : ""}
      ${sel && (!unpack || mv.del) ? `<div class="cp-warn">${unpack
        ? "Die Abbild-Datei wird nach dem Entpacken gelöscht."
        : "Nach dem Verschieben gibt es das Spiel am alten Ort nicht mehr."}</div>` : ""}
      ${blocked ? `<p class="cp-err">${esc(blocked)}</p>` : ""}
      <div class="cp-actions">
        <button type="button" class="btn" data-cp="close">Abbrechen</button>
        <button type="button" class="btn primary" data-cp="start" ${sel && !blocked ? "" : "disabled"}>${unpack ? "Entpacken starten" : "Verschieben starten"}</button>
      </div>`;
  };

  const mvRenderJob = () => {
    const j = mv.job;
    const body = $("#cp-body");
    if (!j || !body) return;
    cp.view = "mvjob";
    txt("#cp-title", `„${gmName(j.title_id)}“ · ${mvVerb(j.operation)}`);
    const pct = Math.max(0, Math.min(100, Math.floor(Number(j.progress_percent) || 0)));
    const route = `<p class="cp-dest"><span class="muted">Von</span> <code>${esc(j.source || "")}</code>
        <span class="muted">nach</span> <code>${esc(j.destination || "")}</code></p>`;
    let html;
    if (j.active) {
      const left = j.speed_bytes_per_second > 0 && j.total_bytes > j.processed_bytes
        ? (j.total_bytes - j.processed_bytes) / j.speed_bytes_per_second : 0;
      const stateTxt = [MV_STATES[j.state] || "",
        j.speed_bytes_per_second > 0 ? `${bytes(j.speed_bytes_per_second)}/s` : "",
        left ? cpEta(left) : ""].filter(Boolean).join(" · ");
      html = `<div class="cp-bar"><div style="width:${pct}%"></div></div>
        <p class="cp-num"><b>${pct} %</b>${j.total_bytes > 0 ? ` · ${cpBytes(j.processed_bytes)} von ${bytes(j.total_bytes)}` : ""}${j.total_files > 0 ? ` · ${num(j.processed_files)} von ${num(j.total_files)} Dateien` : ""}</p>
        ${stateTxt ? `<p class="muted">${esc(stateTxt)}</p>` : ""}
        ${route}
        <p class="muted">ShadowMountPlus arbeitet weiter, auch wenn du dieses Fenster schließt.</p>`;
    } else if (j.state === "completed") {
      html = `<p class="cp-ok">✓ Fertig.</p>${route}
        <p class="muted">ShadowMountPlus trägt das Spiel neu ein; die Liste zeigt gleich den neuen Ort.</p>`;
    } else if (j.state === "failed") {
      html = `<p class="cp-err">✗ ${esc(j.result_error || "Der Auftrag ist gescheitert.")}</p>${route}`;
    } else if (j.state === "cancelled") {
      html = `<p>Abgebrochen.</p>${route}`;
    } else {
      html = `<p class="muted">ShadowMountPlus hat keinen Auftrag.</p>`;
    }
    body.innerHTML = `${html}
      <div class="cp-actions">
        ${j.active && j.cancellable ? `<button type="button" class="btn" data-cp="cancel" ${j.cancel_requested ? "disabled" : ""}>${j.cancel_requested ? "Wird abgebrochen …" : "Auftrag abbrechen"}</button>` : ""}
        <button type="button" class="btn${j.active ? "" : " primary"}" data-cp="close">${j.active ? "Im Hintergrund weiter" : "Schließen"}</button>
      </div>`;
  };

  /* ShadowMountPlus can be too busy copying to answer in time. One lost answer
     must not end the display of a job it is still running: the last state
     stays and the question is asked again, like cpPoll and cvPoll do. After
     this many misses in a row the job is given up, so a listener that is gone
     is not polled for ever. With no job on screen there is nothing to keep, so
     no retry either: the page does not nag a console that has no SMP. */
  const MV_MAX_MISSES = 60;

  const mvPoll = async () => {
    clearTimeout(mv.timer);
    let j;
    try {
      j = await api("/api/v1/library/storage");
    } catch (e) {
      if (mv.job && mv.job.active && ++mv.misses <= MV_MAX_MISSES) {
        mv.timer = setTimeout(mvPoll, 3000);
        return;
      }
      mv.job = null;              /* ShadowMountPlus antwortet nicht: nichts zu zeigen */
      jobBanner();
      return;
    }
    mv.misses = 0;
    const wasActive = !!(mv.job && mv.job.active);
    mv.job = j;
    if (j.active) mv.seen = j.job_id;
    jobBanner();
    if (cp.view === "mvjob") mvRenderJob();
    if (j.active) mv.timer = setTimeout(mvPoll, 1000);
    else if (wasActive) loadGames();              /* der neue Ort */
  };

  const mvOpen = async (id, op) => {
    cpShell();
    const seq = ++dlg.seq;
    if (mv.job && mv.job.active) { mvRenderJob(); return; }
    mv.id = id; mv.op = op; mv.plan = null; mv.dest = ""; mv.del = false;
    cp.view = "mvloading";
    txt("#cp-title", `„${gmName(id)}“ ${op === "unpack" ? "konvertieren" : "verschieben"}`);
    $("#cp-body").innerHTML = `<p class="muted">Ziele werden ermittelt …</p>`;
    try {
      const plan = await api(`/api/v1/library/storage/plan?id=${encodeURIComponent(id)}&op=${encodeURIComponent(op)}`,
                             { timeoutMs: 120000 });
      if (seq !== dlg.seq || !$("#cp-overlay")) return;
      mv.plan = plan;
      mvRenderPlan();
    } catch (e) {
      if (seq !== dlg.seq || !$("#cp-overlay")) return;
      $("#cp-body").innerHTML = `<p class="cp-err">${esc(e.message)}</p>
        <div class="cp-actions"><button type="button" class="btn primary" data-cp="close">Schließen</button></div>`;
    }
  };

  const mvStart = async (btn) => {
    btn.disabled = true;
    try {
      mv.job = await api("/api/v1/library/storage", { method: "POST", timeoutMs: 20000,
        body: JSON.stringify({ id: mv.id, op: mv.op, target: mv.dest,
                               delete_source: mv.op === "unpack" && mv.del }) });
      if (mv.job.job_id) mv.seen = mv.job.job_id;
      mvRenderJob();
      jobBanner();
      mv.timer = setTimeout(mvPoll, 1000);
    } catch (e) {
      toast(e.message, "error");
      btn.disabled = false;
    }
  };

  const mvCancel = async (btn) => {
    btn.disabled = true;
    try {
      await api("/api/v1/library/storage/cancel", { method: "POST" });
      toast("ShadowMountPlus bricht den Auftrag ab.");
    } catch (e) {
      toast(e.message, "error");
    }
    mvPoll();
  };

  /* ── Konvertieren (1.46.0) ───────────────────────────────────────────
   * Ordner → exFAT-Abbild oder → ffpfsc (exFAT innen, in einem Durchgang),
   * vorhandenes Abbild → ffpfsc: macht die App selbst, übertragen aus MkPFS
   * (gameconvert.c). Ein Abbild zu einem Ordner entpacken macht
   * ShadowMountPlus (mv…). Die Ziele sind die des Kopierens: der Ordner
   * homebrew eines Laufwerks oder PS5-Sicherung/Spiele. */
  const cv = { id: "", plan: null, op: "", drive: "", mode: "", job: null, timer: 0 };

  const CV_OPS = {
    ffpfsc: {
      t: "ffpfsc · komprimiert",
      d: (p) => p.kind === "folder"
        ? "Der Ordner wird als exFAT-Abbild in einen komprimierten PFS-Container gepackt, in einem Durchgang ohne Zwischendatei. Der Aufbau, den MkPFS und ShadowMountPlus empfehlen."
        : "Das Abbild wird in einen komprimierten PFS-Container gepackt."
    },
    ffpkg: {
      t: "ffpkg (UFS2) · unkomprimiert",
      d: (p) => "Ein UFS2-Abbild direkt aus dem Ordner, im Aufbau, den ShadowMountPlus empfiehlt (64-KiB-Blöcke). Unkomprimiert"
        + (p.ffpkg_bytes && !p.ffpkg_error ? `, das Abbild wird ${bytes(p.ffpkg_bytes)} groß.` : ".")
    },
    exfat: {
      t: "exFAT-Abbild · unkomprimiert",
      d: () => "Ein exFAT-Abbild mit 64-KiB-Clustern, wie ShadowMountPlus es für seinen schnellen Weg braucht."
    }
  };
  const CV_EXT = { ffpfsc: ".ffpfsc", ffpkg: ".ffpkg", exfat: ".exfat" };

  const cvBlocked = (d) => {
    const p = cv.plan;
    /* A .ffpkg is as big as its own image, which is not the exFAT one's size. */
    const need = cv.op === "ffpkg" && p.ffpkg_bytes ? p.ffpkg_bytes : p.raw_bytes;
    const fat32 = cv.op === "ffpkg" && d.fat32_too_big_ffpkg !== undefined ? d.fat32_too_big_ffpkg : d.fat32_too_big;
    if (fat32) return "FAT32 nimmt keine Datei über 4 GB auf";
    if (!d.enough_space) return `Zu wenig Platz: Nötig sind bis zu ${bytes(need)}`;
    const t = cv.mode && cv.op && d[cv.mode] ? d[cv.mode][cv.op] : null;
    /* Per target, not per drive: the drive-wide figure is the best case, and
       the leftover of this very target gives its room back only to it. */
    if (t && t.enough_space === false) return `Zu wenig Platz: Nötig sind bis zu ${bytes(need)}`;
    if (t && t.exists) return "Dort gibt es diese Datei schon";
    return "";
  };

  const cvRenderPlan = () => {
    const p = cv.plan;
    const body = $("#cp-body");
    if (!p || !body) return;
    cp.view = "cvplan";
    txt("#cp-title", `„${p.name}“ konvertieren`);
    const g = gm.list.find((x) => x.title_id === p.title_id);
    const fmt = g && g.format ? (GM_FORMATS[g.format] || g.format) : "";

    /* Only operations this page knows by name; the value is text like any other. */
    /* A .ffpkg the app already knows it cannot build (a folder with too many entries, a file too big) is shown
       with the reason, and cannot be chosen. */
    const ops = p.ops.filter((op) => CV_OPS[op]).map((op) => {
      const off = op === "ffpkg" && p.ffpkg_error;
      return `<label class="cp-choice${off ? " off" : ""}">
        <input type="radio" name="cv-op" value="${esc(op)}" ${cv.op === op ? "checked" : ""} ${off ? "disabled" : ""}>
        <span class="cp-choice-text"><b>${esc(CV_OPS[op].t)}${op === "ffpfsc" && p.kind === "folder" ? " (empfohlen)" : ""}</b>
          <span class="muted">${esc(CV_OPS[op].d(p))}</span>
          ${off ? `<span class="cp-why">Bei diesem Spiel nicht möglich: ${esc(p.ffpkg_error)}</span>` : ""}</span></label>`;
    }).join("")
      + (g && g.can_unpack ? `<label class="cp-choice">
        <input type="radio" name="cv-op" value="unpack">
        <span class="cp-choice-text"><b>Dump-Ordner · entpacken</b>
          <span class="muted">ShadowMountPlus entpackt das Abbild in einen Ordner.</span></span></label>` : "");

    const drives = p.targets.map((d) => {
      const why = cvBlocked(d);
      if (why && cv.drive === d.mount) cv.drive = "";
      return `<label class="cp-choice${why ? " off" : ""}">
          <input type="radio" name="cv-drive" value="${esc(d.mount)}"
            ${cv.drive === d.mount ? "checked" : ""} ${why ? "disabled" : ""}>
          <span class="cp-choice-text"><b>${esc(d.label)}</b>
            <span class="muted">${cpBytes(d.free_bytes)} frei${d.fs ? ` · ${esc(cpFs(d.fs))}` : ""}${d.source_here ? " · hier liegt das Original" : ""}</span>
            ${why ? `<span class="cp-why">${esc(why)}</span>` : ""}</span>
        </label>`;
    }).join("");

    const sel = p.targets.find((d) => d.mount === cv.drive);
    const tgt = sel && cv.mode && cv.op && sel[cv.mode] ? sel[cv.mode][cv.op] : null;
    const target = tgt ? tgt.path : "";
    const blockedRun = p.running ? "Das Spiel läuft gerade. Bitte erst beenden."
                     : p.busy ? "Es läuft schon eine Konvertierung."
                     : p.other_busy ? "Es läuft gerade eine Kopie. Kopieren, Konvertieren und Verschieben laufen nicht gleichzeitig." : "";

    body.innerHTML = `
      <p class="cp-src"><span class="muted">Quelle</span> <code>${esc(p.source)}</code>
        <span>${fmt ? `${esc(fmt)} · ` : ""}${p.kind === "folder"
          ? `${cpFiles(p.files)}, als exFAT-Abbild ${bytes(p.raw_bytes)}`
          : bytes(p.raw_bytes)}</span></p>

      <h4 class="cp-step">1. Was soll entstehen?</h4>
      <div class="cp-list">${ops}</div>

      <h4 class="cp-step">2. Als was?</h4>
      <div class="cp-list">
        <label class="cp-choice">
          <input type="radio" name="cv-mode" value="homebrew" ${cv.mode === "homebrew" ? "checked" : ""}>
          <span class="cp-choice-text"><b>A · Spielbar</b>
            <span class="muted">In den Ordner <code>homebrew</code> des Ziels. ShadowMountPlus findet
              das Ergebnis dort und bindet es ein.</span></span>
        </label>
        <label class="cp-choice">
          <input type="radio" name="cv-mode" value="backup" ${cv.mode === "backup" ? "checked" : ""}>
          <span class="cp-choice-text"><b>B · Sicherung</b>
            <span class="muted">In den Ordner <code>PS5-Sicherung/Spiele</code>. Dort sucht
              ShadowMountPlus nicht.</span></span>
        </label>
      </div>
      ${cv.mode === "homebrew" ? `<div class="cp-warn"><b>⚠ ShadowMountPlus findet das Spiel
          danach zweimal</b>: das Original und das neue Abbild, beide mit der Titel-ID
          ${esc(p.title_id)}. Das kann beim Einbinden Probleme machen. ShadowMountPlus rät, je
          Titel-ID nur eine Quelle zu behalten. Lösche das Original erst, wenn das neue Abbild
          startet.</div>` : ""}

      <h4 class="cp-step">3. Wohin?</h4>
      <div class="cp-list">${drives || `<p class="muted">Kein Laufwerk gefunden.</p>`}</div>

      ${target ? `<p class="cp-dest"><span class="muted">Ziel</span> <code>${esc(target)}</code>
        ${tgt && tgt.unfinished ? `<span class="cp-why">Dort liegt eine unfertige Datei dieser App, sie wird ersetzt${
          tgt.unfinished_bytes > 0 ? ` (${bytes(tgt.unfinished_bytes)} werden dabei frei)` : ""}.</span>` : ""}</p>` : ""}
      ${blockedRun ? `<p class="cp-err">${esc(blockedRun)}</p>` : ""}
      <p class="muted">Die PS5 rechnet dabei mit niedriger Priorität, damit Lüftersteuerung und
        Oberfläche Vorrang behalten. Bei großen Spielen dauert das. Danach liest sie das ganze
        Ergebnis zur Prüfung noch einmal vom Laufwerk${target ? `; daneben entsteht
        <code>${esc(target)}.sha256</code> mit der Prüfsumme (SHA-256)` : ""}.</p>
      <div class="cp-actions">
        <button type="button" class="btn" data-cp="close">Abbrechen</button>
        <button type="button" class="btn primary" data-cp="start" ${target && !blockedRun ? "" : "disabled"}>Konvertieren starten</button>
      </div>`;
  };

  const cvRenderJob = () => {
    const j = cv.job;
    const body = $("#cp-body");
    if (!j || !body) return;
    cp.view = "cvjob";
    txt("#cp-title", `„${j.name || j.title_id}“ konvertieren`);
    const pct = j.total_bytes > 0 ? Math.min(100, Math.floor(j.done_bytes * 100 / j.total_bytes)) : 0;
    const where = `<p class="cp-dest"><span class="muted">Ziel</span> <code>${esc(j.target || "")}</code></p>`;
    const verb = j.op === "ffpfsc" ? "komprimiert" : "geschrieben";
    let html;
    if (j.state === "scanning") {
      html = `<p>Das Spiel wird vermessen …</p>${where}`;
    } else if (j.state === "writing") {
      html = `<div class="cp-bar"><div style="width:${pct}%"></div></div>
        <p class="cp-num"><b>${pct} %</b> · ${cpBytes(j.done_bytes)} von ${bytes(j.total_bytes)} ${verb}</p>
        <p class="muted">${j.bytes_per_s ? `${bytes(j.bytes_per_s)}/s` : "Tempo wird gemessen"}${j.eta_s ? ` · ${cpEta(j.eta_s)}` : ""}</p>
        ${where}
        <p class="muted">Das läuft auf der PS5 weiter, auch wenn du dieses Fenster schließt.</p>`;
    } else if (j.state === "verifying") {
      /* The result is read back from the drive and checked, all of it. */
      const cpct = j.check_total_bytes > 0
        ? Math.min(100, Math.floor(j.check_done_bytes * 100 / j.check_total_bytes)) : 0;
      html = `<div class="cp-bar"><div style="width:${cpct}%"></div></div>
        <p class="cp-num"><b>${cpct} %</b> · das Ergebnis wird vom Laufwerk zurückgelesen und geprüft</p>
        <p class="muted">${j.bytes_per_s ? `${bytes(j.bytes_per_s)}/s` : "Tempo wird gemessen"}${j.eta_s ? ` · ${cpEta(j.eta_s)}` : ""}</p>
        ${where}
        <p class="muted">Das läuft auf der PS5 weiter, auch wenn du dieses Fenster schließt.</p>`;
    } else if (j.state === "done") {
      const ratio = j.total_bytes > 0 && j.output_bytes ? Math.round(j.output_bytes * 100 / j.total_bytes) : 0;
      html = `<p class="cp-ok">✓ Fertig und geprüft: ${bytes(j.output_bytes)}${j.op === "ffpfsc" && ratio
          ? ` statt ${bytes(j.total_bytes)} (${ratio} %)` : ""}.</p>${where}
        ${cpSums(j, "das Ergebnis")}
        ${j.mode === "homebrew" ? `<p class="muted">ShadowMountPlus findet das neue Abbild bei seinem
          nächsten Durchlauf. Das Spiel liegt jetzt zweimal vor. Lösche das Original erst, wenn das
          neue Abbild startet.</p>` : `<p class="muted">Die Sicherung wird nicht eingebunden.</p>`}`;
    } else if (j.state === "cancelled") {
      html = `<p>Abgebrochen. Die unfertige Datei hat die App wieder entfernt
        (oder ersetzt sie beim nächsten Versuch, falls das Laufwerk das Löschen verweigert).</p>${where}`;
    } else if (j.state === "failed") {
      html = `<p class="cp-err">✗ ${esc(j.error || "Das Konvertieren ist gescheitert.")}</p>${where}
        <p class="muted">Die unfertige Datei hat die App wieder entfernt, oder sie ersetzt sie beim
          nächsten Versuch, falls das Laufwerk das Löschen verweigert. Das Original ist unberührt.</p>`;
    } else {
      html = `<p class="muted">Keine Konvertierung.</p>`;
    }
    const active = !!j.active;
    body.innerHTML = `${html}
      <div class="cp-actions">
        ${active ? `<button type="button" class="btn" data-cp="cancel">Konvertieren abbrechen</button>` : ""}
        <button type="button" class="btn${active ? "" : " primary"}" data-cp="close">${active ? "Im Hintergrund weiter" : "Schließen"}</button>
      </div>`;
  };

  const cvPoll = async () => {
    clearTimeout(cv.timer);
    try {
      cv.job = await api("/api/v1/library/convert");
    } catch (e) {
      cv.timer = setTimeout(cvPoll, 3000);
      return;
    }
    jobBanner();
    if (cp.view === "cvjob") cvRenderJob();
    if (cv.job.active) cv.timer = setTimeout(cvPoll, 1000);
  };

  const cvOpen = async (id) => {
    cpShell();
    const seq = ++dlg.seq;
    if (cv.job && cv.job.active) { cvRenderJob(); return; }
    const g = gm.list.find((x) => x.title_id === id);
    /* Ein komprimiertes Abbild lässt sich nur entpacken: gleich dorthin. */
    if (g && !g.can_convert && g.can_unpack) { mvOpen(id, "unpack"); return; }
    cv.id = id; cv.plan = null; cv.op = ""; cv.drive = ""; cv.mode = ""; cp.view = "cvloading";
    txt("#cp-title", `„${g ? g.name : id}“ konvertieren`);
    $("#cp-body").innerHTML = `<p class="muted">Das Spiel wird vermessen …</p>`;
    try {
      const plan = await api(`/api/v1/library/convert/plan?id=${encodeURIComponent(id)}`,
                             { timeoutMs: 120000 });
      if (seq !== dlg.seq || !$("#cp-overlay")) return;
      cv.plan = plan;
      cv.op = plan.ops.find((op) => CV_OPS[op] && !(op === "ffpkg" && plan.ffpkg_error)) || "";
      const first = plan.targets.find((d) => !d.source_here && d.enough_space && !d.fat32_too_big)
                 || plan.targets.find((d) => d.enough_space && !d.fat32_too_big);
      cv.drive = first ? first.mount : "";
      cvRenderPlan();
    } catch (e) {
      if (seq !== dlg.seq || !$("#cp-overlay")) return;
      $("#cp-body").innerHTML = `<p class="cp-err">${esc(e.message)}</p>
        <div class="cp-actions"><button type="button" class="btn primary" data-cp="close">Schließen</button></div>`;
    }
  };

  const cvStart = async (btn) => {
    btn.disabled = true;
    try {
      cv.job = await api("/api/v1/library/convert", { method: "POST",
        body: JSON.stringify({ id: cv.id, op: cv.op, target: cv.drive, mode: cv.mode }) });
      cvRenderJob();
      jobBanner();
      cv.timer = setTimeout(cvPoll, 1000);
    } catch (e) {
      toast(e.message, "error");
      btn.disabled = false;
    }
  };

  const cvCancel = async (btn) => {
    btn.disabled = true;
    try {
      await api("/api/v1/library/convert/cancel", { method: "POST" });
      toast("Das Konvertieren wird abgebrochen.");
    } catch (e) {
      toast(e.message, "error");
    }
    cvPoll();
  };

  const gmSorted = (list) => {
    const cmp = {
      name:      (a, b) => a.name.localeCompare(b.name, "de"),
      last:      (a, b) => (b.last_played || "").localeCompare(a.last_played || ""),
      played:    (a, b) => (b.played_s || 0) - (a.played_s || 0),
      size:      (a, b) => (b.size_bytes || 0) - (a.size_bytes || 0),
      installed: (a, b) => (b.installed || "").localeCompare(a.installed || "")
    }[gm.sort];
    return cmp ? list.slice().sort(cmp) : list.slice();   /* "recent": Konsole */
  };

  const renderGames = () => {
    const grid = $("#gm-grid");
    if (!grid) return;
    gm.running = gmRunningId();
    gm.live = gmLiveId();

    if (!gm.loaded) {
      txt("#gm-sub", "wird gelesen …");
      grid.innerHTML = "";
      return;
    }
    if (gm.error && !gm.list.length) {
      txt("#gm-sub", "nicht verfügbar");
      grid.innerHTML = `<p class="muted">Die Spieleliste ließ sich nicht lesen: ${esc(gm.error)}</p>`;
      return;
    }

    const q = gm.q.trim().toLowerCase();
    const shown = gmSorted(gm.list).filter((x) =>
      (!gm.plat || (gm.plat === "mods" ? gmHasMods(x) : x.platform === gm.plat)) &&
      (!q || `${x.name} ${x.title_id} ${x.content_id || ""}`.toLowerCase().includes(q)));

    const n5 = gm.list.filter((x) => x.platform === "PS5").length;
    const n4 = gm.list.filter((x) => x.platform === "PS4").length;
    const nm = gm.list.filter(gmHasMods).length;
    let sub = `${gm.list.length} Spiele · ${n5} PS5, ${n4} PS4`;
    if (nm) sub += ` · ${nm} mit Backport, AMPR oder PlayGo`;
    if (shown.length !== gm.list.length) sub += ` · ${shown.length} angezeigt`;
    if (gm.meta && gm.meta.from_home_screen === false)
      sub += " · ohne Benutzerdaten (Kacheltabelle nicht gefunden)";
    /* The console's database names more titles than the app keeps. */
    if (gm.meta && gm.meta.truncated)
      sub += " · Liste unvollständig, die Datenbank enthält mehr Einträge";
    txt("#gm-sub", sub);

    grid.innerHTML = shown.length
      ? shown.map((x) => gmCard(x, x.title_id === gm.running, x.title_id === gm.live)).join("")
      : `<p class="muted">Keine Treffer.</p>`;
    /* Kein Cover: das Bild verschwindet, die Initialen dahinter bleiben. */
    $$("#gm-grid img").forEach((img) =>
      img.addEventListener("error", () => img.classList.add("broken"), { once: true }));
  };

  /* ── Covers & Metadaten speichern (04.10.2026) ───────────────────────
   * Der Zwischenspeicher der Spieleliste (libcache.c): Titelbilder und die Angaben,
   * die ein Scan sonst bei jedem Lesen neu holt, liegen auf der Konsole in
   * /data/PS5-Cooling-Center/covers_and_more. Aus (die Vorgabe): nichts wird
   * geschrieben und nichts benutzt; was schon dort liegt, bleibt, bis jemand den
   * Ordner leert. Der Schalter ist eine Einstellung (library_cache); loadConfig()
   * zeigt ihn deshalb auch dann richtig, wenn diese Seite noch nie offen war. */
  const gcLoad = async () => {
    try {
      const d = await api("/api/v1/library/cache");
      const n = Number(d.titles) || 0;
      const info = $("#gm-cache-info");
      info.textContent = n
        ? `${num(n)} ${n === 1 ? "Spiel" : "Spiele"} gespeichert · ${bytes(d.bytes)}`
        : "noch nichts gespeichert";
      info.hidden = !(n || d.enabled);
      $("#gm-cache-clear").hidden = !n;
    } catch { /* die Anzeige bleibt, wie sie war */ }
  };

  const loadGames = async () => {
    try {
      const d = await api("/api/v1/library");
      gm.list  = Array.isArray(d.games) ? d.games : [];
      gm.meta  = d;
      gm.error = "";
    } catch (e) {
      gm.error = e.message || "nicht verfügbar";
    }
    gm.loaded = true;
    renderGames();
    gcLoad();
  };

  $("#gm-cache-on").addEventListener("change", async (e) => {
    const box = e.target, on = box.checked;
    box.disabled = true;
    try {
      await saveConfig({ library_cache: on ? 1 : 0 },
        on ? "Covers & Metadaten werden gespeichert."
           : "Covers & Metadaten werden nicht mehr gespeichert. Was schon im Ordner liegt, bleibt, bis er geleert wird.");
      loadGames();                       /* die nächste Liste füllt den Ordner (oder lässt ihn in Ruhe) */
    } catch (err) {
      box.checked = !on;
      toast(err.message, "error");
    }
    box.disabled = false;
  });
  armConfirm($("#gm-cache-clear"), "Wirklich leeren? Nochmal klicken", async () => {
    try {
      const r = await api("/api/v1/library/cache/clear", { method: "POST" });
      toast(r.titles ? `${num(r.titles)} ${r.titles === 1 ? "Spiel" : "Spiele"} aus dem Ordner entfernt (${bytes(r.bytes)}).`
                     : "Es gab nichts zu löschen.");
      loadGames();                       /* ist der Schalter an, füllt sich der Ordner mit dieser Liste neu */
    } catch (e) { toast(e.message, "error"); }
  }, "Alles im Ordner covers_and_more wird gelöscht. Ihre Spiele bleiben unberührt. Zum Bestätigen noch einmal klicken.");

  /* Ein Listener für alle Karten — das Raster wird bei jedem Filtern neu gebaut. */
  $("#gm-grid").addEventListener("click", (e) => {
    const info = e.target.closest("button[data-info]");
    if (info) {
      /* Nur diese Karte umschalten, nicht das Raster neu bauen: Die Seite
         bliebe sonst nicht, wo sie war. */
      const id = info.dataset.info;
      const open = !gm.open.has(id);
      if (open) gm.open.add(id); else gm.open.delete(id);
      info.setAttribute("aria-expanded", open ? "true" : "false");
      const dl = info.nextElementSibling;
      if (dl) dl.hidden = !open;
      return;
    }
    /* Ausgegraute Knöpfe stehen auf jeder Karte; sie tun nichts. */
    const copy = e.target.closest("button[data-copy]");
    if (copy) { if (!copy.disabled) cpOpen(copy.dataset.copy); return; }
    const store = e.target.closest("button[data-store]");
    if (store) { if (!store.disabled) mvOpen(store.dataset.id, store.dataset.store); return; }
    const conv = e.target.closest("button[data-convert]");
    if (conv) { if (!conv.disabled) cvOpen(conv.dataset.convert); return; }
    const del = e.target.closest("button[data-delete]");
    if (del) { if (!del.disabled) dlOpen("game", del.dataset.delete); return; }
    const closeBtn = e.target.closest("button[data-close]");
    if (closeBtn) {
      if (closeBtn.disabled) return;
      const cg = gm.list.find((x) => x.title_id === closeBtn.dataset.close);
      gmCloseGame(closeBtn.dataset.close, cg ? cg.name : closeBtn.dataset.close, closeBtn);
      return;
    }
    const btn = e.target.closest("button[data-launch]");
    if (btn && !btn.disabled) gmLaunch(btn.dataset.launch, btn);
  });
  /* Dasselbe auf der Kühlungsseite, an der Kachel „Läuft gerade". */
  $("#run-close").addEventListener("click", (e) => {
    const b = e.currentTarget;
    if (b.dataset.id) gmCloseGame(b.dataset.id, b.dataset.name || b.dataset.id, b);
  });
  $("#gm-copy-banner").addEventListener("click", (e) => {
    const kind = e.currentTarget.dataset.kind;
    if (kind === "store") { cpShell(); mvRenderJob(); }
    else if (kind === "convert") { cpShell(); cvRenderJob(); }
    else cpOpen("");
  });
  $("#gm-search").addEventListener("input", (e) => { gm.q = e.target.value; renderGames(); });
  $("#gm-sort").addEventListener("change", (e) => { gm.sort = e.target.value; renderGames(); });
  /* Nur die Auswahl dieser Seite: Die Reiter der Protokollseite und die Voreinstellungen
     des Kernel-Logs tragen dieselbe Klasse, und ein Klick darauf nahm der Spieleliste
     ihren Filter und den anderen Reitern ihre Hervorhebung. */
  $$("#gm-pane-lib .gm-chip").forEach((b) => b.addEventListener("click", () => {
    gm.plat = b.dataset.plat;
    $$("#gm-pane-lib .gm-chip").forEach((x) => x.classList.toggle("active", x === b));
    renderGames();
  }));

  /* ── Spielzeit (03.10.2026) ──────────────────────────────────────────
   * Die Sitzungen, die die App selbst mitschreibt (playtime.c,
   * /api/v1/playtime): wann ein Spiel lief, wie lange es vorn war und wie heiß
   * die Konsole dabei wurde. Die Spielzeit auf den Karten der Bibliothek kommt
   * dagegen von der Konsole und zählt von Anfang an.
   *
   * Gerechnet wird hier, nicht in der Konsole: Ob etwas „heute" war, entscheiden
   * Ortszeit und Konsolenuhr zusammen, und die Zeitzone kennt nur der Browser.
   * Als „jetzt" gilt die Uhr der Konsole — mit ihr sind Sitzungen und Tage in sich
   * stimmig, auch wenn sie falsch geht (dann sagt die Seite es). */
  const pt = { tab: "lib", data: null, error: "", loaded: false, shown: 15,
               rankAll: false, timer: 0 };

  const ptDay     = (ms) => { const d = new Date(ms); return new Date(d.getFullYear(), d.getMonth(), d.getDate()).getTime(); };
  const ptNextDay = (ms) => { const d = new Date(ms); return new Date(d.getFullYear(), d.getMonth(), d.getDate() + 1).getTime(); };
  const ptDaysAgo = (ms, n) => { const d = new Date(ms); return new Date(d.getFullYear(), d.getMonth(), d.getDate() - n).getTime(); };
  const ptClock = (ms) => new Date(ms).toLocaleTimeString(LOCALE, { hour: "2-digit", minute: "2-digit" });
  const ptDate = (ms) => new Date(ms).toLocaleDateString(LOCALE, { weekday: "short", day: "2-digit", month: "2-digit" });
  const ptDateFull = (ms) => new Date(ms).toLocaleDateString(LOCALE, { day: "2-digit", month: "2-digit", year: "numeric" });

  const ptDur = (sec) => {
    sec = Math.round(sec);
    if (!(sec > 0)) return "—";
    if (sec < 60) return `${sec} Sek.`;
    const h = Math.floor(sec / 3600), m = Math.floor((sec % 3600) / 60);
    return h ? `${h} Std. ${String(m).padStart(2, "0")} Min.` : `${m} Min.`;
  };
  /* Die Summenkacheln sind schmal: „18 h 06 min“ statt „18 Std. 06 Min.“. */
  const ptDurTile = (sec) => {
    sec = Math.round(sec);
    if (!(sec > 0)) return "—";
    if (sec < 60) return `${sec} s`;
    const h = Math.floor(sec / 3600), m = Math.floor((sec % 3600) / 60);
    return h ? `${h} h ${String(m).padStart(2, "0")} min` : `${m} min`;
  };
  const ptShort = (sec) => (sec >= 3600 ? `${num(sec / 3600, 1)} h` : `${Math.round(sec / 60)} min`);
  const ptTemp = (v) => (v >= 0 ? `${v} °C` : "—");

  /* Von … bis …, ohne das Datum zu wiederholen, wenn die Sitzung an einem Tag blieb. */
  const ptWhen = (start, end) => {
    const a = start * 1000, b = Math.max(end, start) * 1000;
    return ptDay(a) === ptDay(b)
      ? `${ptDate(a)}, ${ptClock(a)}–${ptClock(b)} Uhr`
      : `${ptDate(a)}, ${ptClock(a)} Uhr – ${ptDate(b)}, ${ptClock(b)} Uhr`;
  };

  /* Der Name der laufenden Sitzung; ist es nur die Titel-ID, gilt der aus den gespeicherten. */
  const ptLiveName = (d) => {
    const l = d.live;
    return l.name && l.name !== l.id ? l.name : (d.titles && d.titles[l.id]) || l.name || l.id;
  };

  const ptCompute = (d) => {
    const now = d.now * 1000;
    const today = ptDay(now);
    const perDay = new Map();
    const addDay = (day, sec) => perDay.set(day, (perDay.get(day) || 0) + sec);

    /* Die Spielzeit einer Sitzung verteilt sich auf die Tage, die sie berührt. */
    const split = (start, end, play) => {
      const s = start * 1000, e = Math.max(end, start) * 1000;
      if (e <= s) { addDay(ptDay(s), play); return; }
      for (let t = s, guard = 0; t < e && guard < 62; guard++) {
        const next = Math.min(ptNextDay(t), e);
        addDay(ptDay(t), play * (next - t) / (e - s));
        t = next;
      }
    };

    const titles = new Map();
    let total = 0, count = 0, first = 0;
    const take = (id, name, start, end, play, hot) => {
      split(start, end, play);
      total += play; count++;
      if (!first || start < first) first = start;
      let t = titles.get(id);
      if (!t) titles.set(id, (t = { id, name, play: 0, n: 0, last: 0, hot: -1 }));
      t.play += play; t.n++;
      if (end > t.last) t.last = end;
      if (hot > t.hot) t.hot = hot;
    };
    d.sessions.forEach((r) =>
      take(r.id, (d.titles && d.titles[r.id]) || r.id, r.start, r.end, r.play,
           Math.max(r.cpu_max, r.soc_max)));
    if (d.live) {
      take(d.live.id, ptLiveName(d), d.live.start, d.now, d.live.play,
           Math.max(d.live.cpu_max, d.live.soc_max));
      titles.get(d.live.id).name = ptLiveName(d);
    }

    const spanOf = (days) => {
      const from = ptDaysAgo(today, days - 1);
      let sec = 0;
      perDay.forEach((v, k) => { if (k >= from) sec += v; });
      /* Eine Sitzung zählt in einem Zeitraum, den sie berührt: Ihre Spielzeit steht ja auch dort.
         Die laufende reicht immer bis jetzt. */
      let n = d.sessions.filter((r) => r.end * 1000 >= from).length;
      if (d.live) n++;
      return { sec, n };
    };

    return {
      now, today, perDay, total, count, first,
      titles: [...titles.values()].sort((a, b) => b.play - a.play),
      spans: [["Heute", spanOf(1)], ["Letzte 7 Tage", spanOf(7)],
              ["Letzte 30 Tage", spanOf(30)], ["Insgesamt", { sec: total, n: count }]]
    };
  };

  const ptSessions = (n) => `${n} ${n === 1 ? "Sitzung" : "Sitzungen"}`;

  /* Oben: Hinweise, laufende Sitzung, Summen, Tage — alles, was sich während des
     Spielens bewegt. Wird bei jeder Abfrage neu gebaut, ist klein und lässt die
     Listen darunter in Ruhe. */
  const ptRenderTop = (c) => {
    const d = pt.data;

    const notes = [];
    if (d.enabled === false)
      notes.push("Die Spielerkennung ist ausgeschaltet (System → Zusatzabfragen). "
               + "Ohne sie wird keine Spielzeit mitgeschrieben.");
    const skew = Math.abs(Date.now() / 1000 - d.now);
    if (skew > 600)
      notes.push(`Die Uhr der Konsole geht um ${ptDur(skew)} ${d.now > Date.now() / 1000 ? "vor" : "nach"}. `
               + "Die Tage hier richten sich nach ihr.");
    const note = $("#pt-note");
    note.hidden = !notes.length;
    note.innerHTML = notes.map(esc).join("<br>");

    const live = $("#pt-live");
    if (d.live) {
      const l = d.live;
      const where = l.state === "front" ? "vorn"
                  : l.state === "back" ? "pausiert, der Startbildschirm ist vorn"
                  : "wird beendet …";
      const temps = l.cpu_max >= 0
        ? ` · CPU bis ${l.cpu_max} °C` + (l.soc_max >= 0 ? `, SoC bis ${l.soc_max} °C` : "") : "";
      live.hidden = false;
      live.innerHTML = `<span class="pt-live-dot ${esc(l.state)}"></span>
        <div><b>${esc(ptLiveName(d))}</b>
        <small>${esc(where)} · seit ${esc(ptClock(l.start * 1000))} Uhr · ${esc(ptDur(l.play))} gespielt${esc(temps)}</small></div>`;
    } else {
      live.hidden = true;
      live.innerHTML = "";
    }

    $("#pt-stats").innerHTML = c.spans.map(([label, s]) =>
      `<div class="pt-stat"><span class="ov-label">${esc(label)}</span>
        <strong>${esc(ptDurTile(s.sec))}</strong>
        <small>${s.n ? esc(ptSessions(s.n)) : "keine Sitzung"}</small></div>`).join("");

    txt("#pt-sub", c.count
      ? `${ptSessions(c.count)} seit dem ${ptDateFull(c.first * 1000)}`
        + (d.total >= d.kept ? ` · es bleiben die letzten ${d.kept} gespeichert` : "")
      : "noch keine Sitzung");

    const days = [];
    for (let i = 13; i >= 0; i--) {
      const k = ptDaysAgo(c.today, i);
      days.push({ k, sec: c.perDay.get(k) || 0 });
    }
    const max = Math.max(3600, ...days.map((x) => x.sec));
    $("#pt-bars").innerHTML = days.map((x) => {
      const h = x.sec > 0 ? Math.max(3, Math.round(120 * x.sec / max)) : 0;
      const wd = new Date(x.k).toLocaleDateString(LOCALE, { weekday: "short" });
      const dm = new Date(x.k).toLocaleDateString(LOCALE, { day: "2-digit", month: "2-digit" });
      const dd = new Date(x.k).getDate();
      const tip = `${ptDate(x.k)}: ${x.sec > 0 ? ptDur(x.sec) : "nicht gespielt"}`;
      return `<div class="pt-bar${x.k === c.today ? " today" : ""}" title="${esc(tip)}">
          <span class="pt-bar-val">${x.sec >= 60 ? esc(ptShort(x.sec)) : ""}</span>
          <span class="pt-bar-col" style="height:${h}px"></span><span class="pt-bar-base"></span>
          <span class="pt-bar-lbl">${esc(wd)}<br><span class="pt-d-full">${esc(dm)}</span><span class="pt-d-short">${dd}.</span></span></div>`;
    }).join("");
  };

  const ptCover = (id, name) => {
    const g = gm.list.find((x) => x.title_id === id);
    const ini = String(name).split(/\s+/)
      .map((w) => w.replace(/[^0-9A-Za-zÀ-ÿ]/g, "")).filter(Boolean)
      .slice(0, 2).map((w) => w[0]).join("").toUpperCase();
    return `<div class="pt-cover"><span>${esc(ini || "?")}</span>${g && g.cover
      ? `<img src="${esc(g.cover)}" alt="" loading="lazy" decoding="async">` : ""}</div>`;
  };

  /* Die Liste und die Rangfolge ändern sich erst, wenn eine Sitzung zu Ende ist. */
  const ptRenderRest = (c) => {
    const d = pt.data;

    const top = c.titles.length ? c.titles[0].play : 0;
    const shown = pt.rankAll ? c.titles : c.titles.slice(0, 8);
    txt("#pt-rank-sub", c.titles.length
      ? `${c.titles.length} ${c.titles.length === 1 ? "Spiel" : "Spiele"}, nach Spielzeit geordnet` : "");
    $("#pt-rank").innerHTML = shown.length ? shown.map((t) => {
      const avg = t.n ? t.play / t.n : 0;
      return `<div class="pt-row">${ptCover(t.id, t.name)}
        <div class="pt-main"><b>${esc(t.name)}</b>
          <small>${esc(ptSessions(t.n))} · im Schnitt ${esc(ptDur(avg))} · zuletzt ${esc(ptDateFull(t.last * 1000))}${
            t.hot >= 0 ? ` · höchste Temperatur ${t.hot} °C` : ""}</small>
          <div class="pt-share"><span style="width:${top ? Math.max(2, Math.round(100 * t.play / top)) : 0}%"></span></div></div>
        <div class="pt-side"><b>${esc(ptDur(t.play))}</b><small>${top ? Math.round(100 * t.play / c.total) : 0} % der Zeit</small></div>
      </div>`;
    }).join("") : `<p class="pt-empty">Noch keine Spielzeit gespeichert. Sobald ein Spiel mindestens
      30 Sekunden vorn war, steht es hier.</p>`;
    const more = $("#pt-rank-more");
    more.hidden = c.titles.length <= 8;
    more.textContent = pt.rankAll ? "Weniger anzeigen" : `Alle ${c.titles.length} anzeigen`;

    const rows = d.sessions.slice(0, pt.shown);
    txt("#pt-list-sub", d.sessions.length ? `die neuesten ${rows.length} von ${d.sessions.length}` : "");
    $("#pt-list").innerHTML = rows.length ? rows.map((r) => {
      const name = (d.titles && d.titles[r.id]) || r.id;
      const badges = (r.flags & 1 ? `<span class="badge hot" title="Die Regelung war im Notfallmodus.">Notfallmodus</span>` : "")
        + (r.flags & 2 ? `<span class="badge warn" title="Die Temperaturwarnung war aktiv.">Warnung</span>` : "")
        + (r.flags & 4 ? `<span class="badge quiet" title="Die App wurde zwischendurch beendet oder die Konsole ausgeschaltet. Das Ende ist der letzte bekannte Zeitpunkt.">Ende geschätzt</span>` : "");
      return `<div class="pt-row">${ptCover(r.id, name)}
        <div class="pt-main"><b>${esc(name)}</b>
          <small>${esc(ptWhen(r.start, r.end))}</small>${badges ? `<div>${badges}</div>` : ""}</div>
        <div class="pt-side"><b>${esc(ptDur(r.play))}</b>
          <small>CPU bis ${esc(ptTemp(r.cpu_max))} · SoC bis ${esc(ptTemp(r.soc_max))}${
            r.fan_avg >= 0 ? ` · Lüfter Ø ${r.fan_avg} %` : ""}</small></div>
      </div>`;
    }).join("") : `<p class="pt-empty">Noch keine Sitzung.</p>`;
    $("#pt-more").hidden = d.sessions.length <= pt.shown;

    /* Kein Cover: Das Bild verschwindet, die Initialen dahinter bleiben. */
    $$("#gm-pane-time .pt-cover img").forEach((img) =>
      img.addEventListener("error", () => img.classList.add("broken"), { once: true }));
  };

  const ptRender = () => {
    if (!pt.loaded) { txt("#pt-sub", "wird gelesen …"); return; }
    if (!pt.data) {
      txt("#pt-sub", "nicht verfügbar");
      $("#pt-stats").innerHTML = `<p class="pt-empty">Die Spielzeit ließ sich nicht lesen: ${esc(pt.error)}</p>`;
      ["#pt-bars", "#pt-rank", "#pt-list"].forEach((s) => { $(s).innerHTML = ""; });
      ["#pt-rank-sub", "#pt-list-sub"].forEach((s) => txt(s, ""));
      $("#pt-live").hidden = true;
      $("#pt-note").hidden = true;
      $("#pt-more").hidden = true;
      $("#pt-rank-more").hidden = true;
      return;
    }
    const c = ptCompute(pt.data);
    ptRenderTop(c);
    ptRenderRest(c);
  };

  const loadPlaytime = async () => {
    try {
      const d = await api("/api/v1/playtime");
      if (!d || !Array.isArray(d.sessions)) throw new Error("unerwartete Antwort der Konsole");
      pt.data = d;
      pt.error = "";
    } catch (e) {
      pt.error = e.message || "nicht verfügbar";
      pt.data = null;
    }
    pt.loaded = true;
    ptRender();
  };

  /* Zwischen den großen Abfragen genügt eine kleine: die laufende Sitzung und die
     Zahl der gespeicherten. Hat sich die geändert, ist eine Sitzung zu Ende und die
     Seite lädt neu. */
  const ptPoll = async () => {
    try {
      const d = await api("/api/v1/playtime?max=1");
      if (!pt.data || !d || d.total !== pt.data.total) { await loadPlaytime(); return; }
      pt.data.live = d.live;
      pt.data.now = d.now;
      pt.data.enabled = d.enabled;
      ptRenderTop(ptCompute(pt.data));
    } catch { /* die nächste Runde versucht es wieder */ }
  };

  /* Läuft nur, solange man es sehen kann: Reiter vorn, Seite vorn, nicht angehalten. */
  const syncPlaytime = () => {
    const run = state.page === "games" && pt.tab === "time" && !document.hidden;
    if (run && !pt.timer) {
      pt.timer = setInterval(ptPoll, 5000);
    } else if (!run && pt.timer) {
      clearInterval(pt.timer);
      pt.timer = 0;
    }
  };

  const ptSelectTab = (which) => {
    pt.tab = which;
    $("#gm-pane-lib").hidden   = which !== "lib";
    $("#gm-pane-time").hidden  = which !== "time";
    $("#gm-pane-saves").hidden = which !== "saves";
    $("#gm-pane-pkg").hidden   = which !== "pkg";
    $("#gm-pane-bk").hidden    = which !== "bk";
    [["lib", "#gm-tab-lib"], ["time", "#gm-tab-time"], ["saves", "#gm-tab-saves"], ["pkg", "#gm-tab-pkg"], ["bk", "#gm-tab-bk"]].forEach(([k, sel]) => {
      const b = $(sel);
      b.classList.toggle("active", which === k);
      b.setAttribute("aria-selected", which === k ? "true" : "false");
    });
    if (which === "time") loadPlaytime();
    if (which === "saves") loadSaves();
    if (which === "pkg") loadPackages();
    if (which === "bk") loadBackups();
    syncPlaytime();
    svSync();
    pkSync();
  };
  $("#gm-tab-lib").addEventListener("click", () => ptSelectTab("lib"));
  $("#gm-tab-time").addEventListener("click", () => ptSelectTab("time"));
  $("#gm-tab-saves").addEventListener("click", () => ptSelectTab("saves"));
  $("#gm-tab-pkg").addEventListener("click", () => ptSelectTab("pkg"));
  $("#gm-tab-bk").addEventListener("click", () => ptSelectTab("bk"));

  /* ── Löschen (gamedelete.c) ─────────────────────────────────────────
     Ein Spiel (installiert: über die Deinstallation der Konsole; ShadowMountPlus: abmelden, dann die Datei löschen)
     oder eine Sicherung der App. Der Plan der Konsole sagt genau, was weggeht, und gibt ein Kennwort aus, ohne das
     nichts gelöscht wird. Gefragt wird zweimal: ein Häkchen „endgültig“, dann zwei Klicks auf „Endgültig löschen“.
     Die Spielstände bleiben immer. */
  const dl = { kind: "", id: "", plan: null, timer: 0, armed: 0, poll: 0 };
  const dlShell = () => {
    let o = $("#dl-overlay");
    if (o) return o;
    o = document.createElement("div");
    o.id = "dl-overlay";
    o.className = "gm-overlay";
    o.hidden = true;
    o.innerHTML = `<div class="cp-dialog dl-dialog" role="dialog" aria-modal="true" aria-labelledby="dl-title">
        <div class="cp-head"><h3 id="dl-title">Löschen</h3>
          <button type="button" class="cp-x" data-dl="close" aria-label="Schließen">×</button></div>
        <div id="dl-body"></div>
      </div>`;
    document.body.appendChild(o);
    o.addEventListener("click", (e) => {
      if (e.target === o) { dlClose(); return; }
      const b = e.target.closest("[data-dl]");
      if (!b || b.disabled) return;
      if (b.dataset.dl === "close") dlClose();
      else if (b.dataset.dl === "go") dlGo(b);
    });
    o.addEventListener("change", (e) => {
      if (e.target.id === "dl-ack") {
        const go = $("#dl-go");
        if (go) { go.disabled = !e.target.checked; dlDisarm(go); }
      }
    });
    return o;
  };
  const dlClose = () => {
    const o = $("#dl-overlay");
    if (o) o.hidden = true;
    clearTimeout(dl.timer);
    clearTimeout(dl.poll);
    dl.poll = 0;
  };
  const dlDisarm = (go) => {
    clearTimeout(dl.timer);
    dl.armed = 0;
    go.removeAttribute("data-armed");
    go.textContent = "Endgültig löschen";
  };
  const dlOpen = async (kind, id) => {
    const o = dlShell();
    dl.kind = kind;
    dl.id = id;
    dl.plan = null;
    $("#dl-title").textContent = kind === "backup" ? "Sicherung löschen" : "Spiel löschen";
    $("#dl-body").innerHTML = `<p class="muted">Die Konsole prüft, was gelöscht würde …</p>`;
    o.hidden = false;
    try {
      dl.plan = await api("/api/v1/library/delete/plan", { method: "POST", body: JSON.stringify({ kind, id }) });
      dlRenderPlan();
    } catch (e) {
      $("#dl-body").innerHTML = `<p class="cp-err">${esc(e.message)}</p>
        <div class="cp-actions"><button type="button" class="btn" data-dl="close">Schließen</button></div>`;
    }
  };
  const dlRenderPlan = () => {
    const p = dl.plan;
    const what = p.kind === "installed" ? "installiertes Spiel"
      : p.kind === "smp" ? "Spiel von ShadowMountPlus"
      : p.kind === "backup" ? "Sicherung der App" : "Spiel";
    const size = p.size_bytes > 0 ? ` · ${bytes(p.size_bytes)}` : "";
    const ver = p.version ? ` · Version ${esc(p.version)}` : "";
    const items = (p.items || []).map((t) => `<li>${esc(t)}</li>`).join("");
    const ok = !!p.can_delete;
    $("#dl-body").innerHTML = `<p class="dl-name"><b>${esc(p.name || p.id)}</b><br>
        <span class="muted">${esc(what)}${p.kind !== "backup" ? ` · ${esc(p.id)}` : ""}${ver}${size}</span></p>
      ${p.path ? `<p class="muted">Ort: <code>${esc(p.path)}</code></p>` : ""}
      ${items ? `<p>Gelöscht wird:</p><ul class="dl-items">${items}</ul>` : ""}
      ${p.keeps ? `<p class="muted">Bleibt liegen: <code>${esc(p.keeps)}</code> – die Spieldaten, auf die das Spiel verweist. Welche Dateien dazugehören, weiß die App nicht sicher; sie lassen sich danach von Hand löschen.</p>` : ""}
      <p class="dl-keep">Deine Spielstände bleiben erhalten.</p>
      ${ok ? `<label class="dl-ack"><input type="checkbox" id="dl-ack"> <span>Ich verstehe: Das Löschen ist endgültig und lässt sich nicht rückgängig machen.</span></label>`
           : `<p class="cp-err">${esc(p.why || "Das lässt sich gerade nicht löschen.")}</p>`}
      <div class="cp-actions">
        <button type="button" class="btn" data-dl="close">${ok ? "Abbrechen" : "Schließen"}</button>
        ${ok ? `<button type="button" class="btn danger" id="dl-go" data-dl="go" disabled>Endgültig löschen</button>` : ""}
      </div>`;
  };
  const dlGo = async (go) => {
    const ack = $("#dl-ack");
    if (!ack || !ack.checked || !dl.plan) return;
    if (!dl.armed) {
      dl.armed = Date.now();
      go.setAttribute("data-armed", "");
      go.textContent = "Wirklich? Nochmal klicken";
      dl.timer = setTimeout(() => dlDisarm(go), 10000);
      return;
    }
    if (Date.now() - dl.armed < 800) return;            /* ein Doppelklick ist keine zweite Bestätigung */
    clearTimeout(dl.timer);
    go.disabled = true;
    try {
      await api("/api/v1/library/delete", { method: "POST",
        body: JSON.stringify({ kind: dl.kind, id: dl.id, confirm: dl.plan.confirm }) });
      dlFollow();
    } catch (e) {
      $("#dl-body").innerHTML = `<p class="cp-err">${esc(e.message)}</p>
        <div class="cp-actions"><button type="button" class="btn" data-dl="close">Schließen</button></div>`;
    }
  };
  const dlFollow = async () => {
    let j = null;
    try { j = await api("/api/v1/library/delete"); } catch { /* gleich noch einmal */ }
    if (j && !j.active && (j.state === "done" || j.state === "failed")) {
      const name = esc(j.name || dl.id);
      $("#dl-body").innerHTML = j.state === "done"
        ? `<p class="dl-done">„${name}“ ist gelöscht.</p>${j.note ? `<p class="muted">${esc(j.note)}</p>` : ""}
           <p class="dl-keep">Die Spielstände sind erhalten.</p>
           <div class="cp-actions"><button type="button" class="btn" data-dl="close">Schließen</button></div>`
        : `<p class="cp-err">${esc(j.error || "Das Löschen ist gescheitert.")}</p>
           <div class="cp-actions"><button type="button" class="btn" data-dl="close">Schließen</button></div>`;
      dl.poll = 0;
      if (dl.kind === "backup") loadBackups(); else loadGames();
      return;
    }
    const phase = j && j.phase ? esc(j.phase) : "Wird gelöscht";
    const pc = Math.max(0, Math.min(100, j && Number.isFinite(j.percent) ? Math.floor(j.percent) : 0));
    const amount = j && j.bytes_total > 0 ? `${j.bytes_done > 0 ? esc(bytes(j.bytes_done)) : "0 B"} von ${esc(bytes(j.bytes_total))} gelöscht` : "";
    const since = j && j.elapsed_s > 0 ? `seit ${esc(ptDur(j.elapsed_s))}` : "";
    const cur = [amount, since].filter(Boolean).join(" · ");
    $("#dl-body").innerHTML = `<p><b>${esc((dl.plan && dl.plan.name) || dl.id)}</b></p>
      <p class="cp-num"><b>${phase}</b> · ${pc} %</p>
      <div class="cp-bar" role="progressbar" aria-label="Fortschritt beim Löschen" aria-valuemin="0" aria-valuemax="100" aria-valuenow="${pc}"><div style="width:${pc}%"></div></div>
      ${cur ? `<p class="muted cp-cur">${cur}</p>` : ""}<p class="muted">Bitte die Seite offen lassen.</p>`;
    dl.poll = setTimeout(dlFollow, 1000);
  };

  /* ── Dateimanager (06.10.2026) ──────────────────────────────────────
     Was geändert werden darf, entscheidet die Konsole (filemgr.c); die Seite zeigt es nur an (writable je Ordner
     und Eintrag). Kopieren und Verschieben gehen über eine Zwischenablage: auswählen, „Kopieren“ oder
     „Ausschneiden“, in den Zielordner gehen, „Hier einfügen“. Löschen fragt wie bei den Spielen: Plan mit
     Kennwort, Häkchen, zwei Klicks. Hochladen läuft je Datei über XMLHttpRequest, weil nur das den Fortschritt
     des Sendens meldet. Namen kommen von der Konsole und gehen durch esc(); Knöpfe tragen nur Nummern. */
  const fm = { path: "", places: [], list: null, sel: new Set(), clip: null, poll: 0, xhr: null, upQueue: [],
               plan: null, armed: 0, timer: 0, loaded: false,
               sort: "name", desc: false, sorted: [], sizes: new Map(), sizing: false, viewSeq: 0 };
  /* Die Sortierung merkt sich der Browser (nur eine Annehmlichkeit, nichts davon hängt daran). */
  try {
    const s = JSON.parse(localStorage.getItem("fmSort") || "null");
    if (s && ["name", "size", "date"].includes(s.sort)) { fm.sort = s.sort; fm.desc = !!s.desc; }
  } catch { /* ohne gespeicherte Sortierung: nach Namen */ }
  const fmSaveSort = () => { try { localStorage.setItem("fmSort", JSON.stringify({ sort: fm.sort, desc: fm.desc })); } catch { /* egal */ } };
  const fmJoin = (dir, name) => `${dir === "/" ? "" : dir}/${name}`;
  const fmWhen = (s) => (s > 0 ? new Date(s * 1000).toLocaleString(LOCALE, { day: "2-digit", month: "2-digit",
    year: "numeric", hour: "2-digit", minute: "2-digit" }) : "");
  const FM_ICO = {
    dir: '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M3 6.5A1.5 1.5 0 0 1 4.5 5H9l2 2h8.5A1.5 1.5 0 0 1 21 8.5v9a1.5 1.5 0 0 1-1.5 1.5h-15A1.5 1.5 0 0 1 3 17.5z"/></svg>',
    file: '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M6 3h8l4 4v14H6z"/><path d="M14 3v4h4"/></svg>',
    link: '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M10 14a4 4 0 0 0 5.7 0l3-3a4 4 0 0 0-5.7-5.7l-1 1"/><path d="M14 10a4 4 0 0 0-5.7 0l-3 3a4 4 0 0 0 5.7 5.7l1-1"/></svg>',
  };
  /* Die Einträge in der Reihenfolge, in der sie auf der Seite stehen: Die Knöpfe tragen Nummern aus dieser Liste. */
  const fmEntries = () => fm.sorted;
  const fmSelected = () => fmEntries().filter((e) => fm.sel.has(e.name));
  const fmIsDir = (e) => e.type === "dir" || e.type === "dirlink";
  const fmSelectable = (e) => e.type !== "link" && e.type !== "dirlink" && e.type !== "other";
  /* Ordner stehen immer zuerst. Innerhalb der Gruppen: nach Namen (ohne Groß-/Kleinschreibung, Zahlen als Zahlen),
     nach Größe (ein Ordner zählt mit der berechneten Größe, ohne sie als 0) oder nach Datum. Gleiche Werte
     fallen auf den Namen zurück. */
  const fmSortList = (list) => {
    const dir = fm.desc ? -1 : 1;
    const size = (e) => {
      if (e.type === "file") return e.size;
      const s = fm.sizes.get(fmJoin(fm.path, e.name));
      return s ? s.bytes : 0;
    };
    const byName = (a, b) => a.name.localeCompare(b.name, undefined, { sensitivity: "base", numeric: true })
      || (a.name < b.name ? -1 : a.name > b.name ? 1 : 0);
    return list.slice().sort((a, b) => {
      const g = (fmIsDir(a) ? 0 : 1) - (fmIsDir(b) ? 0 : 1);
      if (g) return g;
      if (fm.sort === "name") return byName(a, b) * dir;
      const c = fm.sort === "size" ? size(a) - size(b) : a.mtime - b.mtime;
      return c ? c * dir : byName(a, b);
    });
  };

  const fmLoadPlaces = async () => {
    try { fm.places = (await api("/api/v1/files/places")).places || []; } catch { fm.places = []; }
  };

  /* Nur die neueste Antwort zählt: Ein Neuladen nach einem fertigen Auftrag, das erst ankommt, wenn schon ein
     anderer Ordner offen ist, darf nicht zurückspringen (und die Auswahl dort löschen). */
  let fmSeq = 0;
  const fmLoad = async (path) => {
    const seq = ++fmSeq;
    txt("#fm-sub", "wird geladen …");
    try {
      const d = await api(`/api/v1/files/list?path=${encodeURIComponent(path)}`, { timeoutMs: 30000 });
      if (seq !== fmSeq) return;
      if (fm.path !== d.path) fm.sel.clear();
      fm.path = d.path;
      fm.list = d;
      fm.sizes = new Map();                          /* berechnete Ordnergrößen gelten nur für diesen Stand */
    } catch (e) {
      if (seq !== fmSeq) return;
      txt("#fm-sub", `Nicht lesbar: ${e.message}`);
      if (!fm.list) $("#fm-list").innerHTML = "";
      return;
    }
    fmRender();
  };

  const fmRender = () => {
    const d = fm.list;
    if (!d) return;
    /* die Orte: der längste, in dem der Pfad liegt, ist hervorgehoben */
    let cur = "";
    fm.places.forEach((p) => {
      if ((d.path === p.path || d.path.startsWith(p.path === "/" ? "/" : `${p.path}/`)) && p.path.length > cur.length) cur = p.path;
    });
    $("#fm-places").innerHTML = fm.places.map((p, i) => `<button type="button" class="gm-chip${p.path === cur ? " active" : ""}" data-fm-place="${i}">${esc(p.label)}${p.writable && p.free_bytes > 0 ? ` <small>${esc(bytes(p.free_bytes))} frei</small>` : ""}</button>`).join("");
    /* der Pfad zum Anklicken */
    const parts = d.path === "/" ? [] : d.path.slice(1).split("/");
    let acc = "";
    $("#fm-crumbs").innerHTML = `<button type="button" class="fm-crumb" data-fm-crumb="/">/</button>` + parts.map((p) => {
      acc += `/${p}`;
      return `<span class="fm-sep">›</span><button type="button" class="fm-crumb" data-fm-crumb="${esc(acc)}">${esc(p)}</button>`;
    }).join("");
    const n = d.entries.length;
    txt("#fm-sub", `${d.path} · ${n} ${n === 1 ? "Eintrag" : "Einträge"}${d.truncated ? ` (die ersten ${n} von ${d.total})` : ""}`);
    txt("#fm-free", d.free_bytes > 0 ? `${bytes(d.free_bytes)} frei` : "");
    $("#fm-ro").hidden = !!d.writable;
    $("#fm-mkdir").disabled = !d.writable;
    $("#fm-upload-lbl").classList.toggle("disabled", !d.writable);
    $("#fm-upload").disabled = !d.writable;
    $("#fm-up").disabled = !d.parent;
    fm.sorted = fmSortList(d.entries);
    const sortSel = $("#fm-sort");
    if (sortSel) sortSel.value = fm.sort;
    const dirBtn = $("#fm-dir");
    if (dirBtn) {
      dirBtn.textContent = fm.desc ? "↓" : "↑";
      dirBtn.setAttribute("aria-label", fm.desc ? "absteigend sortiert" : "aufsteigend sortiert");
      dirBtn.title = fm.desc ? "Absteigend (zum Umkehren tippen)" : "Aufsteigend (zum Umkehren tippen)";
    }
    $("#fm-sizes").hidden = !fm.sorted.some((e) => e.type === "dir");
    $("#fm-list").innerHTML = n ? fm.sorted.map((e, i) => {
      const isDir = fmIsDir(e);
      const ico = isDir ? FM_ICO.dir : (e.type === "link" ? FM_ICO.link : FM_ICO.file);
      const canSel = fmSelectable(e);
      const name = isDir ? `<button type="button" class="fm-name fm-open" data-fm-open="${i}">${esc(e.name)}</button>`
        : `<span class="fm-name">${esc(e.name)}</span>`;
      const kind = e.type === "dirlink" ? "Verknüpfung zu einem Ordner" : e.type === "link" ? "Verknüpfung" : e.type === "other" ? "Gerät oder Sonderdatei" : "";
      const sz = e.type === "dir" ? fm.sizes.get(fmJoin(d.path, e.name)) : null;
      const sizeText = e.type === "file" ? bytes(e.size)
        : sz ? `${bytes(sz.bytes) === "--" ? "0 B" : bytes(sz.bytes)}${sz.partial ? "+" : ""} · ${num(sz.files)} ${sz.files === 1 ? "Datei" : "Dateien"}${sz.partial ? " (mindestens)" : ""}` : "";
      const full = encodeURIComponent(fmJoin(d.path, e.name));
      const acts = [
        e.type === "file" ? `<button type="button" class="btn ghost fm-act" data-fm-view="${i}">Ansehen</button>` : "",
        e.type === "dir" && !sz ? `<button type="button" class="btn ghost fm-act" data-fm-size="${i}">Größe</button>` : "",
        e.type === "file" ? `<a class="btn ghost fm-act" href="/api/v1/files/download?path=${full}" download>Herunterladen</a>` : "",
        e.writable ? `<button type="button" class="btn ghost fm-act" data-fm-ren="${i}">Umbenennen</button>` : "",
      ].join("");
      return `<div class="fm-row${fm.sel.has(e.name) ? " sel" : ""}" role="listitem">
          <input type="checkbox" class="fm-check" data-fm-sel="${i}" aria-label="${esc(e.name)} auswählen"${fm.sel.has(e.name) ? " checked" : ""}${canSel ? "" : " disabled"}>
          <span class="fm-ico">${ico}</span>
          <div class="fm-main">${name}<small class="muted">${[sizeText, fmWhen(e.mtime), kind].filter(Boolean).map(esc).join(" · ")}</small></div>
          <div class="fm-acts">${acts}</div>
        </div>`;
    }).join("") : `<p class="muted fm-empty">Dieser Ordner ist leer.</p>`;
    fmRenderBars();
  };

  const fmRenderBars = () => {
    const s = fmSelected();
    /* „Alle auswählen“: an, wenn alles Wählbare gewählt ist; halb, wenn nur ein Teil */
    const selectable = fmEntries().filter(fmSelectable);
    const all = $("#fm-selall");
    if (all) {
      all.disabled = !selectable.length;
      all.checked = selectable.length > 0 && s.length === selectable.length;
      all.indeterminate = s.length > 0 && s.length < selectable.length;
    }
    $("#fm-selbar").hidden = !s.length;
    txt("#fm-selcount", `${s.length} ausgewählt`);
    const allW = s.length && s.every((e) => e.writable);
    $("#fm-cut").disabled = !allW;
    $("#fm-del").disabled = !allW;
    $("#fm-cut").title = allW ? "" : "Nur auf den Laufwerken und in /data";
    $("#fm-del").title = $("#fm-cut").title;
    const c = fm.clip;
    $("#fm-clipbar").hidden = !c;
    if (c) {
      txt("#fm-cliptext", `${c.paths.length} ${c.paths.length === 1 ? "Eintrag" : "Einträge"} zum ${c.move ? "Verschieben" : "Kopieren"} gemerkt (aus ${c.from})`);
      $("#fm-paste").disabled = !(fm.list && fm.list.writable) || c.from === fm.path;
      $("#fm-paste").title = c.from === fm.path ? "Erst in den Zielordner wechseln" : (fm.list && fm.list.writable ? "" : "Hierhin darf nichts kopiert werden");
    }
  };

  /* ein kleines Fenster für Namen und das Löschen */
  const fmShell = () => {
    let o = $("#fm-overlay");
    if (o) return o;
    o = document.createElement("div");
    o.id = "fm-overlay";
    o.className = "gm-overlay";
    o.hidden = true;
    o.innerHTML = `<div class="cp-dialog" role="dialog" aria-modal="true" aria-labelledby="fm-dlg-title">
        <div class="cp-head"><h3 id="fm-dlg-title"></h3>
          <button type="button" class="cp-x" data-fmd="close" aria-label="Schließen">×</button></div>
        <div id="fm-dlg-body"></div>
      </div>`;
    document.body.appendChild(o);
    o.addEventListener("click", (e) => {
      if (e.target === o) { fmClose(); return; }
      const b = e.target.closest("[data-fmd]");
      if (!b || b.disabled) return;
      if (b.dataset.fmd === "close") fmClose();
      else if (b.dataset.fmd === "name") fmNameOk();
      else if (b.dataset.fmd === "del") fmDelGo(b);
    });
    o.addEventListener("change", (e) => {
      if (e.target.id === "fm-ack") {
        const go = $("#fm-del-go");
        if (go) { go.disabled = !e.target.checked; fmDisarm(go); }
      }
    });
    o.addEventListener("keydown", (e) => { if (e.key === "Enter" && e.target.id === "fm-name") { e.preventDefault(); fmNameOk(); } });
    return o;
  };
  const fmClose = () => {
    const o = $("#fm-overlay");
    if (o) { o.hidden = true; o.querySelector(".cp-dialog").classList.remove("fm-view"); }
    clearTimeout(fm.timer);
    fm.armed = 0;
    fm.viewSeq++;                                     /* eine noch laufende Vorschau darf nichts mehr einsetzen */
  };
  const fmDisarm = (go) => { clearTimeout(fm.timer); fm.armed = 0; go.removeAttribute("data-armed"); go.textContent = "Endgültig löschen"; };

  let fmNameAct = null;
  const fmAskName = (title, label, value, act) => {
    const o = fmShell();
    txt("#fm-dlg-title", title);
    $("#fm-dlg-body").innerHTML = `<label class="fm-field"><span>${esc(label)}</span>
        <input type="text" id="fm-name" autocomplete="off" spellcheck="false" maxlength="255" value="${esc(value)}"></label>
      <p class="cp-err" id="fm-name-err" hidden></p>
      <div class="cp-actions"><button type="button" class="btn" data-fmd="close">Abbrechen</button>
        <button type="button" class="btn primary" data-fmd="name">OK</button></div>`;
    fmNameAct = act;
    o.hidden = false;
    const inp = $("#fm-name");
    inp.focus();
    const dot = value.lastIndexOf(".");
    inp.setSelectionRange(0, dot > 0 ? dot : value.length);
  };
  const fmNameOk = async () => {
    const v = $("#fm-name").value.trim();
    const err = $("#fm-name-err");
    if (!v || v === "." || v === ".." || /[\/\\]/.test(v)) { err.textContent = "Bitte einen Namen ohne / und \\ eingeben."; err.hidden = false; return; }
    try {
      await fmNameAct(v);
      fmClose();
      fmLoad(fm.path);
    } catch (e) { err.textContent = e.message; err.hidden = false; }
  };

  /* Löschen: der Plan der Konsole, ein Häkchen, zwei Klicks */
  const fmDelete = async () => {
    const paths = fmSelected().map((e) => fmJoin(fm.path, e.name));
    if (!paths.length) return;
    const o = fmShell();
    txt("#fm-dlg-title", "Löschen");
    $("#fm-dlg-body").innerHTML = `<p class="muted">Die Konsole prüft, was gelöscht würde …</p>`;
    o.hidden = false;
    try {
      fm.plan = await api("/api/v1/files/delete/plan", { method: "POST", body: JSON.stringify({ paths }), timeoutMs: 60000 });
    } catch (e) {
      $("#fm-dlg-body").innerHTML = `<p class="cp-err">${esc(e.message)}</p>
        <div class="cp-actions"><button type="button" class="btn" data-fmd="close">Schließen</button></div>`;
      return;
    }
    const p = fm.plan;
    const shown = p.paths.slice(0, 8).map((x) => `<li><code>${esc(x)}</code></li>`).join("");
    const more = p.paths.length > 8 ? `<li class="muted">… und ${p.paths.length - 8} weitere</li>` : "";
    const what = [p.folders ? `${num(p.folders)} ${p.folders === 1 ? "Ordner" : "Ordner"}` : "", `${num(p.files)} ${p.files === 1 ? "Datei" : "Dateien"}`, bytes(p.bytes)].filter(Boolean).join(" · ");
    $("#fm-dlg-body").innerHTML = `<p><b>${num(p.items)} ${p.items === 1 ? "Eintrag" : "Einträge"}</b> · ${esc(what)}</p>
      <ul class="dl-items">${shown}${more}</ul>
      <label class="dl-ack"><input type="checkbox" id="fm-ack"> <span>Ich verstehe: Das Löschen ist endgültig und lässt sich nicht rückgängig machen.</span></label>
      <div class="cp-actions"><button type="button" class="btn" data-fmd="close">Abbrechen</button>
        <button type="button" class="btn danger" id="fm-del-go" data-fmd="del" disabled>Endgültig löschen</button></div>`;
  };
  const fmDelGo = async (go) => {
    const ack = $("#fm-ack");
    if (!ack || !ack.checked || !fm.plan) return;
    if (!fm.armed) {
      fm.armed = Date.now();
      go.setAttribute("data-armed", "");
      go.textContent = "Wirklich? Nochmal klicken";
      fm.timer = setTimeout(() => fmDisarm(go), 10000);
      return;
    }
    if (Date.now() - fm.armed < 800) return;            /* ein Doppelklick ist keine zweite Bestätigung */
    clearTimeout(fm.timer);
    go.disabled = true;
    try {
      await api("/api/v1/files/delete", { method: "POST", body: JSON.stringify({ paths: fm.plan.paths, confirm: fm.plan.confirm }) });
      fmClose();
      fm.sel.clear();
      fmFollow();
    } catch (e) {
      $("#fm-dlg-body").innerHTML = `<p class="cp-err">${esc(e.message)}</p>
        <div class="cp-actions"><button type="button" class="btn" data-fmd="close">Schließen</button></div>`;
    }
  };

  /* der laufende Auftrag (Kopieren, Verschieben, Löschen) */
  const FM_KIND = { copy: "Kopieren", move: "Verschieben", delete: "Löschen" };
  const fmFollow = async () => {
    clearTimeout(fm.poll);
    let j = null;
    try { j = await api("/api/v1/files/job"); } catch { /* gleich noch einmal */ }
    const box = $("#fm-job");
    if (!j || (!j.active && j.state === "idle")) { box.hidden = true; return; }
    box.hidden = false;
    const what = FM_KIND[j.kind] || "Vorgang";
    if (j.active) {
      const pc = Math.max(0, Math.min(100, Math.floor(Number(j.percent) || 0)));
      const amount = j.bytes_total > 0 ? `${j.bytes_done > 0 ? esc(bytes(j.bytes_done)) : "0 B"} von ${esc(bytes(j.bytes_total))}` : `${num(j.files_done)} von ${num(j.files_total)} Dateien`;
      box.innerHTML = `<p class="cp-num"><b>${esc(what)}${j.dest ? ` nach ${esc(j.dest)}` : ""}</b> · ${pc} %</p>
        <div class="cp-bar" role="progressbar" aria-label="Fortschritt" aria-valuemin="0" aria-valuemax="100" aria-valuenow="${pc}"><div style="width:${pc}%"></div></div>
        <p class="muted cp-cur">${amount}${j.elapsed_s > 0 ? ` · seit ${esc(ptDur(j.elapsed_s))}` : ""}${j.current ? `<br><code>${esc(j.current)}</code>` : ""}</p>
        <div class="cp-actions"><button type="button" class="btn" id="fm-cancel">Abbrechen</button></div>`;
      fm.poll = setTimeout(fmFollow, 800);
      return;
    }
    const msg = j.state === "done" ? `${esc(what)} fertig: ${num(j.items_done)} ${j.items_done === 1 ? "Eintrag" : "Einträge"}, ${esc(bytes(j.bytes_done))}.`
      : j.state === "cancelled" ? `${esc(what)} abgebrochen.` : `${esc(what)} gescheitert.`;
    box.innerHTML = `<p class="${j.state === "failed" ? "cp-err" : "dl-done"}">${msg}</p>
      ${j.error ? `<p class="cp-err">${esc(j.error)}</p>` : ""}${j.note ? `<p class="muted">${esc(j.note)}</p>` : ""}
      <div class="cp-actions"><button type="button" class="btn ghost" id="fm-jobhide">Ausblenden</button></div>`;
    if (j.state === "done" && j.kind === "move") fm.clip = null;
    if (j.finished_ago_s >= 0 && j.finished_ago_s < 5) fmLoad(fm.path);
  };

  const fmPaste = async () => {
    const c = fm.clip;
    if (!c) return;
    try {
      await api(`/api/v1/files/${c.move ? "move" : "copy"}`, { method: "POST", body: JSON.stringify({ paths: c.paths, dest: fm.path }), timeoutMs: 120000 });
      if (!c.move) fm.clip = null;
      fmRenderBars();
      fmFollow();
    } catch (e) {
      const names = e.data && Array.isArray(e.data.conflicts) && e.data.conflicts.length ? ` (${e.data.conflicts.slice(0, 5).join(", ")})` : "";
      toast(`${e.message}${names}`, "error");
    }
  };

  /* Hochladen: eine Datei nach der anderen, mit Fortschritt */
  const fmUpload = (files) => {
    if (!fm.list || !fm.list.writable) { toast("Hierhin darf nichts geladen werden (nur auf die Laufwerke und nach /data).", "error"); return; }
    const dir = fm.path;
    [...files].forEach((f) => fm.upQueue.push({ f, dir }));
    if (!fm.xhr) fmUpNext(0, fm.upQueue.length);
  };
  const fmUpNext = (done, total) => {
    const box = $("#fm-uploads");
    const it = fm.upQueue.shift();
    if (!it) {
      fm.xhr = null;
      box.innerHTML = `<p class="dl-done">${done} ${done === 1 ? "Datei" : "Dateien"} hochgeladen.</p>
        <div class="cp-actions"><button type="button" class="btn ghost" id="fm-uphide">Ausblenden</button></div>`;
      fmLoad(fm.path);
      return;
    }
    box.hidden = false;
    const x = new XMLHttpRequest();
    fm.xhr = x;
    const show = (pc) => {
      box.innerHTML = `<p class="cp-num"><b>Hochladen ${done + 1} von ${total}</b> · ${pc} %</p>
        <div class="cp-bar" role="progressbar" aria-label="Fortschritt beim Hochladen" aria-valuemin="0" aria-valuemax="100" aria-valuenow="${pc}"><div style="width:${pc}%"></div></div>
        <p class="muted cp-cur"><code>${esc(it.f.name)}</code> · ${esc(bytes(it.f.size))} nach ${esc(it.dir)}</p>
        <div class="cp-actions"><button type="button" class="btn" id="fm-upcancel">Abbrechen</button></div>`;
    };
    show(0);
    x.upload.onprogress = (e) => { if (e.lengthComputable) show(Math.floor((e.loaded / e.total) * 100)); };
    const fail = (m) => {
      fm.upQueue = [];
      fm.xhr = null;
      box.innerHTML = `<p class="cp-err">„${esc(it.f.name)}“: ${esc(m)}</p>
        <div class="cp-actions"><button type="button" class="btn ghost" id="fm-uphide">Ausblenden</button></div>`;
      fmLoad(fm.path);
    };
    x.onload = () => {
      let d = null;
      try { d = JSON.parse(x.responseText); } catch { /* keine Antwort */ }
      if (x.status === 200 && d && d.ok) fmUpNext(done + 1, total);
      else fail((d && d.message) || `HTTP ${x.status}`);
    };
    x.onerror = () => fail("Die Verbindung brach ab.");
    x.onabort = () => fail("Abgebrochen; nichts von dieser Datei wurde gespeichert.");
    x.open("POST", `/api/v1/files/upload?path=${encodeURIComponent(it.dir)}&name=${encodeURIComponent(it.f.name)}`);
    x.send(it.f);
  };

  /* Ordnergrößen: die Konsole zählt (höchstens 8 Sekunden je Ordner, dann „mindestens“). Je Ordner ein Knopf, oder
     alle Ordner dieser Ansicht nacheinander (höchstens 60). Ein Wechsel des Ordners bricht das Durchzählen ab. */
  const fmSizeFetch = async (e) => {
    const full = fmJoin(fm.path, e.name);
    const r = await api(`/api/v1/files/size?path=${encodeURIComponent(full)}`, { timeoutMs: 20000 });
    fm.sizes.set(full, { bytes: r.bytes, files: r.files, folders: r.folders, partial: !!r.partial });
  };
  const fmSizeOne = async (i) => {
    const e = fmEntries()[i];
    if (!e || fm.sizing) return;
    const seq = fmSeq;
    fm.sizing = true;
    const b = $(`[data-fm-size="${i}"]`);
    if (b) { b.disabled = true; b.textContent = "…"; }
    try { await fmSizeFetch(e); } catch (er) { toast(`${e.name}: ${er.message}`, "error"); }
    fm.sizing = false;
    if (seq === fmSeq) { fm.sorted = fmSortList(fm.list.entries); fmRender(); }
  };
  const fmSizeAll = async () => {
    if (fm.sizing) return;
    const seq = fmSeq;
    const dirs = fmEntries().filter((e) => e.type === "dir" && !fm.sizes.has(fmJoin(fm.path, e.name))).slice(0, 60);
    if (!dirs.length) return;
    fm.sizing = true;
    const btn = $("#fm-sizes");
    btn.disabled = true;
    let done = 0;
    for (const e of dirs) {
      if (seq !== fmSeq) break;
      btn.textContent = `Ordnergrößen ${done + 1} von ${dirs.length} …`;
      try { await fmSizeFetch(e); } catch (er) { toast(`${e.name}: ${er.message}`, "error"); }
      done++;
    }
    fm.sizing = false;
    btn.disabled = false;
    btn.textContent = "Ordnergrößen";
    if (seq === fmSeq) { fm.sorted = fmSortList(fm.list.entries); fmRender(); }
  };

  /* Vorschau: ein Bild (nach der Endung, nie SVG) oder der Anfang einer Textdatei, nur zum Ansehen. */
  const FM_IMG = /\.(png|jpe?g|gif|webp|bmp|ico)$/i;
  const fmView = async (i) => {
    const e = fmEntries()[i];
    if (!e) return;
    const full = encodeURIComponent(fmJoin(fm.path, e.name));
    const url = `/api/v1/files/view?path=${full}`;
    const o = fmShell();
    o.querySelector(".cp-dialog").classList.add("fm-view");
    txt("#fm-dlg-title", e.name);
    const actions = `<div class="cp-actions"><a class="btn ghost" href="/api/v1/files/download?path=${full}" download>Herunterladen</a>
        <button type="button" class="btn" data-fmd="close">Schließen</button></div>`;
    $("#fm-dlg-body").innerHTML = `<p class="muted">wird geladen …</p>`;
    o.hidden = false;
    const mine = ++fm.viewSeq;
    if (FM_IMG.test(e.name)) {
      $("#fm-dlg-body").innerHTML = `<img class="fm-view-img" alt="${esc(e.name)}" src="${url}">
        <p class="muted fm-view-note">${esc(bytes(e.size))} · ${esc(fmWhen(e.mtime))}</p>${actions}`;
      const img = $("#fm-dlg-body .fm-view-img");
      img.addEventListener("error", () => {
        if (mine !== fm.viewSeq) return;
        $("#fm-dlg-body").innerHTML = `<p class="cp-err">Das Bild lässt sich nicht anzeigen (zu groß, beschädigt oder kein Bild).</p>${actions}`;
      });
      return;
    }
    try {
      const r = await fetch(url, { cache: "no-store" });
      if (!r.ok) {
        let m = "";
        try { m = (await r.json()).message; } catch { /* keine Meldung */ }
        throw new Error(m || `HTTP ${r.status}`);
      }
      const text = await r.text();
      if (mine !== fm.viewSeq) return;
      const truncated = r.headers.get("X-Fm-Truncated") === "1";
      const total = Number(r.headers.get("X-Fm-Size"));
      const shown = Number(r.headers.get("Content-Length")) || text.length;
      $("#fm-dlg-body").innerHTML = `<pre class="fm-view-text"></pre>
        <p class="muted fm-view-note">${truncated ? `Nur der Anfang: ${esc(bytes(shown))} von ${esc(bytes(total))}. ` : ""}${esc(bytes(e.size))} · ${esc(fmWhen(e.mtime))}</p>${actions}`;
      $("#fm-dlg-body .fm-view-text").textContent = text || "(leer)";
    } catch (er) {
      if (mine !== fm.viewSeq) return;
      $("#fm-dlg-body").innerHTML = `<p class="cp-err">${esc(er.message)}</p>${actions}`;
    }
  };

  const fmOpen = async () => {
    if (!fm.loaded) {
      fm.loaded = true;
      await fmLoadPlaces();
      fm.path = fm.path || (fm.places[0] && fm.places[0].path) || "/data";
    }
    await fmLoad(fm.path);
    fmFollow();
  };

  $("#page-files").addEventListener("click", (e) => {
    const t = e.target.closest("button, a");
    if (!t || t.disabled) return;
    const d = t.dataset;
    if (d.fmPlace !== undefined) { const p = fm.places[+d.fmPlace]; if (p) fmLoad(p.path); }
    else if (d.fmCrumb !== undefined) fmLoad(d.fmCrumb);
    else if (d.fmOpen !== undefined) { const en = fmEntries()[+d.fmOpen]; if (en) fmLoad(fmJoin(fm.path, en.name)); }
    else if (d.fmRen !== undefined) {
      const en = fmEntries()[+d.fmRen];
      if (en) fmAskName("Umbenennen", "Neuer Name", en.name, (v) => api("/api/v1/files/rename", { method: "POST", body: JSON.stringify({ path: fmJoin(fm.path, en.name), name: v }) }));
    }
    else if (d.fmView !== undefined) fmView(+d.fmView);
    else if (d.fmSize !== undefined) fmSizeOne(+d.fmSize);
    else if (t.id === "fm-sizes") fmSizeAll();
    else if (t.id === "fm-dir") { fm.desc = !fm.desc; fmSaveSort(); fmRender(); }
    else if (t.id === "fm-up") { if (fm.list && fm.list.parent) fmLoad(fm.list.parent); }
    else if (t.id === "fm-reload") { fmLoadPlaces().then(() => fmLoad(fm.path)); }
    else if (t.id === "fm-mkdir") fmAskName("Neuer Ordner", "Name des Ordners", "Neuer Ordner", (v) => api("/api/v1/files/mkdir", { method: "POST", body: JSON.stringify({ path: fm.path, name: v }) }));
    else if (t.id === "fm-copy" || t.id === "fm-cut") {
      if (!fmSelected().length) return;
      fm.clip ={ move: t.id === "fm-cut", from: fm.path, paths: fmSelected().map((en) => fmJoin(fm.path, en.name)) };
      fm.sel.clear();
      fmRender();
      toast(fm.clip.move ? "Gemerkt. Jetzt den Zielordner öffnen und „Hier einfügen“." : "Gemerkt. Jetzt den Zielordner öffnen und „Hier einfügen“.");
    }
    else if (t.id === "fm-paste") fmPaste();
    else if (t.id === "fm-clipclear") { fm.clip = null; fmRenderBars(); }
    else if (t.id === "fm-del") fmDelete();
    else if (t.id === "fm-selnone") { fm.sel.clear(); fmRender(); }
    else if (t.id === "fm-cancel") { api("/api/v1/files/job/cancel", { method: "POST", body: "{}" }).catch((er) => toast(er.message, "error")); }
    else if (t.id === "fm-jobhide") $("#fm-job").hidden = true;
    else if (t.id === "fm-upcancel") { if (fm.xhr) fm.xhr.abort(); }
    else if (t.id === "fm-uphide") $("#fm-uploads").hidden = true;
  });
  $("#page-files").addEventListener("change", (e) => {
    const c = e.target;
    if (c.dataset && c.dataset.fmSel !== undefined) {
      const en = fmEntries()[+c.dataset.fmSel];
      if (!en) return;
      if (c.checked) fm.sel.add(en.name); else fm.sel.delete(en.name);
      c.closest(".fm-row").classList.toggle("sel", c.checked);
      fmRenderBars();
    } else if (c.id === "fm-upload" && c.files && c.files.length) {
      fmUpload(c.files);
      c.value = "";
    } else if (c.id === "fm-sort") {
      /* Größe und Datum: das Größte und Neueste zuerst; Name: von A bis Z */
      fm.sort = ["name", "size", "date"].includes(c.value) ? c.value : "name";
      fm.desc = fm.sort !== "name";
      fmSaveSort();
      fmRender();
    } else if (c.id === "fm-selall") {
      if (c.checked) fmEntries().filter(fmSelectable).forEach((en) => fm.sel.add(en.name));
      else fm.sel.clear();
      fmRender();
    }
  });
  const fmDrop = $("#fm-list");
  fmDrop.addEventListener("dragover", (e) => { if (fm.list && fm.list.writable) { e.preventDefault(); fmDrop.classList.add("drop"); } });
  fmDrop.addEventListener("dragleave", () => fmDrop.classList.remove("drop"));
  fmDrop.addEventListener("drop", (e) => {
    fmDrop.classList.remove("drop");
    if (!e.dataTransfer || !e.dataTransfer.files.length) return;
    e.preventDefault();
    fmUpload(e.dataTransfer.files);
  });

  /* Die Sicherungen der App, mit „Löschen“ je Eintrag. */
  const bk = { list: [] };
  const loadBackups = async () => {
    try {
      const d = await api("/api/v1/library/backups", { timeoutMs: 30000 });
      bk.list = d.backups || [];
    } catch (e) {
      $("#bk-sub").textContent = `Nicht lesbar: ${e.message}`;
      return;
    }
    const total = bk.list.reduce((s, x) => s + (x.size_bytes > 0 ? x.size_bytes : 0), 0);
    $("#bk-sub").textContent = bk.list.length
      ? `${bk.list.length} ${bk.list.length === 1 ? "Sicherung" : "Sicherungen"}, zusammen ${bytes(total)} (Ordner PS5-Sicherung/Spiele)`
      : "Keine Sicherungen gefunden (Ordner PS5-Sicherung/Spiele auf der internen SSD und den Laufwerken).";
    $("#bk-list").innerHTML = bk.list.map((x, i) => {
      const when = x.modified ? new Date(x.modified * 1000).toLocaleDateString() : "";
      return `<div class="bk-row">
          <div><b>${esc(x.name)}</b>${x.unfinished ? ` <span class="badge warn">unfertig</span>` : ""}${x.sums ? ` <span class="badge">mit Prüfsumme</span>` : ""}<br>
            <span class="muted">${esc(x.type)} · ${esc(x.drive)} · ${bytes(x.size_bytes)}${when ? ` · ${esc(when)}` : ""}</span></div>
          <button type="button" class="btn ghost gm-del" data-bk-del="${i}">Löschen</button>
        </div>`;
    }).join("");
  };
  $("#bk-reload").addEventListener("click", () => loadBackups());
  $("#bk-list").addEventListener("click", (e) => {
    const b = e.target.closest("button[data-bk-del]");
    if (!b) return;
    const x = bk.list[Number(b.dataset.bkDel)];
    if (x) dlOpen("backup", x.path);
  });
  $("#pt-more").addEventListener("click", () => { pt.shown += 30; ptRender(); });
  $("#pt-rank-more").addEventListener("click", () => { pt.rankAll = !pt.rankAll; ptRender(); });

  /* Die Tabelle: eine Zeile je Sitzung, ältere zuerst, mit der Ortszeit des Browsers. */
  $("#pt-export").addEventListener("click", () => {
    const d = pt.data;
    if (!d || !d.sessions.length) { toast("Es gibt noch nichts zu speichern."); return; }
    const p2 = (n) => (n < 10 ? "0" : "") + n;
    const stamp = (sec) => {
      const x = new Date(sec * 1000);
      return `${x.getFullYear()}-${p2(x.getMonth() + 1)}-${p2(x.getDate())} `
           + `${p2(x.getHours())}:${p2(x.getMinutes())}:${p2(x.getSeconds())}`;
    };
    /* Ein Spielname, der mit = + - @ beginnt, läse eine Tabellenkalkulation als Formel. */
    const q = (s) => {
      let t = String(s);
      if (/^[=+\-@\t\r]/.test(t)) t = "'" + t;
      return `"${t.replace(/"/g, '""')}"`;
    };
    const v = (n) => (n >= 0 ? n : "");
    const out = [["Titel-ID", "Spiel", "Beginn", "Ende", "Spielzeit in Sekunden", "CPU max", "CPU Mittel",
                  "SoC max", "SoC Mittel", "Lüfter Mittel in %", "Lüfter max in %",
                  "Notfallmodus", "Warnung", "Ende geschätzt"].map(q).join(";")];
    d.sessions.slice().reverse().forEach((r) => out.push([
      q(r.id), q((d.titles && d.titles[r.id]) || r.id), q(stamp(r.start)), q(stamp(r.end)), r.play,
      v(r.cpu_max), v(r.cpu_avg), v(r.soc_max), v(r.soc_avg), v(r.fan_avg), v(r.fan_max),
      r.flags & 1 ? "ja" : "nein", r.flags & 2 ? "ja" : "nein", r.flags & 4 ? "ja" : "nein"].join(";")));
    const url = URL.createObjectURL(new Blob(["\ufeff" + out.join("\r\n") + "\r\n"],
                                             { type: "text/csv;charset=utf-8" }));
    Object.assign(document.createElement("a"), { href: url, download: "ps5-spielzeit.csv" }).click();
    URL.revokeObjectURL(url);
  });

  /* Zwei Klicks statt einer Rückfrage-Box: wie überall auf dieser Oberfläche. */
  armConfirm($("#pt-reset"), "Wirklich löschen? Nochmal klicken", async () => {
    try {
      await api("/api/v1/playtime/reset", { method: "POST" });
      toast("Spielzeit-Verlauf gelöscht.");
      pt.shown = 15;
      loadPlaytime();
    } catch (e) { toast(e.message, "error"); }
  }, "Alle gespeicherten Sitzungen werden gelöscht. Ein Spiel, das gerade läuft, zählt weiter. "
   + "Zum Bestätigen noch einmal klicken.");

  /* ── Spielstände (04.10.2026) ────────────────────────────────────────
   * Sichern und Zurückspielen der Spielstände (savebackup.c, /api/v1/saves). Die Seite
   * zeigt, was die Konsole hat, wohin gesichert werden kann und was an Sicherungen schon
   * da ist. Der Auftrag selbst läuft auf der Konsole; die Seite fragt ihn im Sekundentakt
   * ab, solange er läuft und man ihn sehen kann.
   *
   * Zurückspielen schreibt in die Ordner der Konsole selbst. Es verlangt deshalb zwei
   * Klicks auf denselben Knopf (wie das Löschen anderswo hier), und die Anfrage trägt
   * "confirm": true — die Konsole nimmt sie sonst nicht an. */
  const sv = { data: null, error: "", loaded: false, sel: new Set(), known: new Set(), drive: "",
               open: new Set(), timer: 0, polling: false, job: null, dismissed: "", armed: new Map() };

  const svWhen = (sec) => (sec > 0
    ? new Date(sec * 1000).toLocaleString(LOCALE, { day: "2-digit", month: "2-digit", year: "numeric",
                                                      hour: "2-digit", minute: "2-digit" })
    : "—");
  /* Für Bildschirmleser: zwei Sicherungen derselben Minute sollen sich unterscheiden lassen. */
  const svWhenSec = (sec) => (sec > 0
    ? new Date(sec * 1000).toLocaleString(LOCALE, { day: "2-digit", month: "2-digit", year: "numeric",
                                                      hour: "2-digit", minute: "2-digit", second: "2-digit" })
    : "—");
  const svName = (t) => t.name || t.id;
  const svActive = () => !!(sv.job && sv.job.active);
  const SV_KIND = { backup: "Sicherung", verify: "Prüfung", restore: "Zurückspielen", delete: "Löschen" };
  /* PS5 und PS4 getrennt, Spiele und Apps getrennt (YouTube & Co. sind keine Spiele). */
  const SV_GROUPS = [["PS5", "game", "PS5-Spiele"], ["PS5", "app", "PS5-Apps"], ["PS4", "game", "PS4-Spiele"], ["PS4", "app", "PS4-Apps"]];

  const svSyncSelection = () => {
    const keys = new Set();
    sv.data.users.forEach((u) => u.titles.forEach((t) => {
      const k = `${u.uid}/${t.id}`;
      keys.add(k);
      if (!sv.known.has(k)) { sv.known.add(k); sv.sel.add(k); }     /* was not there before: chosen */
    }));
    /* what is gone from the console is gone from the choice too, and chosen again if it returns */
    [...sv.sel].forEach((k) => { if (!keys.has(k)) sv.sel.delete(k); });
    [...sv.known].forEach((k) => { if (!keys.has(k)) sv.known.delete(k); });
  };

  const svSelBytes = () => {
    let n = 0;
    sv.data.users.forEach((u) => u.titles.forEach((t) => { if (sv.sel.has(`${u.uid}/${t.id}`)) n += t.bytes; }));
    return n;
  };

  /* Die Kopfkästchen der Benutzer stehen auf „alle", „keiner" oder dazwischen. */
  const svUserBoxes = () => {
    sv.data.users.forEach((u) => {
      const cb = $(`#sv-users [data-sv-user="${u.uid}"]`);
      if (!cb) return;
      const n = u.titles.filter((t) => sv.sel.has(`${u.uid}/${t.id}`)).length;
      cb.checked = n > 0 && n === u.titles.length;
      cb.indeterminate = n > 0 && n < u.titles.length;
    });
  };

  const svRenderUsers = () => {
    const d = sv.data, host = $("#sv-users");
    if (!d.users.length) {
      host.innerHTML = `<p class="pt-empty">Auf der Konsole wurden keine Spielstände gefunden.</p>`;
      return;
    }
    host.innerHTML = d.users.map((u) => `<div class="sv-user">
      <label class="sv-user-head"><input type="checkbox" data-sv-user="${esc(u.uid)}">
        <span class="sv-avatar" aria-hidden="true">${esc((u.name || "?").trim().charAt(0).toUpperCase())}<img src="/api/v1/saves/avatar?uid=${esc(u.uid)}" alt="" loading="lazy"></span>
        <span class="sv-user-name"><b>${esc(u.name)}</b><span class="muted">Benutzer</span></span>
        <span class="muted sv-user-sum">${esc(String(u.titles.length))} Titel · ${esc(bytes(u.bytes))}</span></label>
      ${SV_GROUPS.map(([plat, kind, label]) => {
        const list = u.titles.filter((t) => t.platform === plat && (t.kind === "app" ? "app" : "game") === kind);
        if (!list.length) return "";
        return `<h4 class="sv-group">${esc(label)} <span class="muted">${esc(String(list.length))}</span></h4>`
          + list.map((t) => {
            const k = `${u.uid}/${t.id}`;
            return `<div class="sv-title-row"><label class="sv-title"><input type="checkbox" data-sv-title="${esc(k)}"${sv.sel.has(k) ? " checked" : ""}>
          <span class="sv-t-name"><b>${esc(svName(t))}</b>
            <small>${esc(t.id)} · ${esc(t.platform)}${t.kind === "app" ? " · App" : ""}${t.installed ? "" : " · nicht installiert"}</small></span>
          <span class="sv-t-size">${esc(bytes(t.bytes))}<small>${t.mtime ? esc(svWhen(t.mtime)) : ""}</small></span></label>
          <button type="button" class="btn ghost sv-del" data-sv-delete="${esc(k)}" data-name="${esc(svName(t))}" aria-label="${esc(svName(t))} löschen">Löschen</button></div>`;
          }).join("");
      }).join("")}</div>`).join("");
    /* No inline handler (the page's CSP forbids it): a user without a picture keeps the initial. */
    host.querySelectorAll(".sv-avatar img").forEach((im) => im.addEventListener("error", () => im.remove(), { once: true }));
    svUserBoxes();
  };

  const svRenderDrives = () => {
    const drives = sv.data.drives;
    if (!drives.some((x) => x.mount === sv.drive)) {
      /* Besser auf einen Stick oder eine Platte als auf den Speicher, der die Spielstände selbst hält. */
      const ext = drives.find((x) => x.mount !== "/user");
      sv.drive = (ext || drives[0] || {}).mount || "";
    }
    $("#sv-drives").innerHTML = drives.length ? drives.map((x) =>
      `<label class="cp-choice"><input type="radio" name="sv-drive" value="${esc(x.mount)}"${x.mount === sv.drive ? " checked" : ""}>
        <span class="cp-choice-text"><b>${esc(x.label)}</b><span class="muted">${esc(bytes(x.free_bytes))} frei</span></span></label>`).join("")
      : `<p class="pt-empty">Es wurde kein Laufwerk gefunden.</p>`;
  };

  /* Der Sicherungsknopf und der Satz darunter, der sagt, warum er gesperrt ist. */
  const svRenderStart = () => {
    const d = sv.data, btn = $("#sv-start"), hint = $("#sv-hint");
    const drive = d.drives.find((x) => x.mount === sv.drive);
    const need = svSelBytes();
    let why = "";
    if (d.game_running) why = "Es läuft ein Spiel. Beende es zuerst.";
    else if (svActive()) why = "Es läuft schon ein Vorgang.";
    else if (!sv.sel.size) why = "Wähle mindestens einen Titel.";
    else if (!drive) why = "Wähle ein Ziel.";
    else if (drive.free_bytes < need + 64 * 1048576) why = "Auf dem Ziel ist zu wenig Platz.";
    btn.disabled = !!why;
    hint.textContent = why || `${sv.sel.size} Titel ausgewählt, zusammen ${bytes(need)}.`;
    txt("#sv-sub", `${d.users.length} Benutzer · `
      + `${d.users.reduce((n, u) => n + u.titles.length, 0)} Titel · `
      + `${bytes(d.users.reduce((n, u) => n + u.bytes, 0))} auf der Konsole`);
  };

  const svRenderBackups = () => {
    sv.armed.forEach((a) => clearTimeout(a.timer));
    sv.armed.clear();
    const list = sv.data.backups;
    txt("#sv-bk-sub", list.length ? `${list.length} ${list.length === 1 ? "Sicherung" : "Sicherungen"}, die neueste zuerst` : "");
    const un = sv.data.unfinished > 0 ? sv.data.unfinished : 0;
    const note = $("#sv-unfinished");
    note.hidden = !un;
    note.textContent = un
      ? `${un} ${un === 1 ? "unfertige Sicherung liegt" : "unfertige Sicherungen liegen"} auf den Laufwerken (zum Beispiel nach einem Stromausfall mitten im Sichern). `
        + "Sie sind unbrauchbar und belegen Platz; die App zeigt und löscht sie nicht. Zu erkennen sind sie an der Datei .ps5cc-unfertig "
        + "im Ordner unter PS5-Sicherung/Spielstaende; am PC oder per FTP lassen sie sich entfernen."
      : "";
    $("#sv-backups").innerHTML = list.length ? list.map((b) => {
      const open = sv.open.has(b.path);
      return `<div class="sv-bk">
        <div class="sv-bk-head">
          <div class="sv-bk-main"><b>${esc(svWhen(b.created))}</b>
            ${b.kind === "undo" ? `<span class="badge quiet" title="So war der Stand, bevor ein Titel zurückgespielt wurde.">Stand vor dem Zurückspielen</span>` : ""}
            ${b.this_console ? "" : `<span class="badge warn" title="Diese Sicherung stammt von einer anderen Konsole und lässt sich hier nicht zurückspielen.">andere Konsole</span>`}
            <small>${esc(b.label)} · ${esc(String(b.titles.length))} Titel · ${esc(bytes(b.bytes))}
              · ${esc(String(b.users.length))} Benutzer</small></div>
          <div class="row-gap">
            <button type="button" class="btn ghost" data-sv-verify="${esc(b.path)}" aria-label="${b.kind === "undo" ? "Stand vor dem Zurückspielen" : "Sicherung"} vom ${esc(svWhenSec(b.created))} prüfen">Prüfen</button>
            <button type="button" class="btn ghost" data-sv-open="${esc(b.path)}" aria-expanded="${open ? "true" : "false"}">${open ? "Titel ausblenden" : "Titel anzeigen"}</button>
          </div>
        </div>
        <div class="sv-bk-titles"${open ? "" : " hidden"}>${b.titles.map((t) => {
          const u = b.users.find((x) => x.uid === t.uid);
          return `<div class="sv-bk-row">
            <span class="sv-t-name"><b>${esc(svName(t))}</b><small>${esc(t.id)} · ${esc(t.platform)}${u && b.users.length > 1 ? " · " + esc(u.name) : ""}</small></span>
            <span class="sv-t-size">${esc(bytes(t.bytes))}</span>
            ${b.this_console ? `<button type="button" class="btn ghost" data-sv-restore="${esc(b.path)}" data-uid="${esc(t.uid)}"
                data-id="${esc(t.id)}" data-name="${esc(svName(t))}" aria-label="${esc(svName(t))}${u && b.users.length > 1 ? " von " + esc(u.name) : ""} aus ${b.kind === "undo" ? "dem Stand vor dem Zurückspielen" : "der Sicherung"} vom ${esc(svWhenSec(b.created))} zurückspielen">Zurückspielen</button>` : ""}
          </div>`;
        }).join("")}</div>
      </div>`;
    }).join("") : `<p class="pt-empty">Noch keine Sicherung. Wähle oben aus, was gesichert werden soll, und wohin.</p>`;
  };

  const svRenderJob = () => {
    const j = sv.job, card = $("#sv-job-card");
    const recent = j && j.state !== "idle" && (j.active || (j.finished_ago_s >= 0 && j.finished_ago_s < 900));
    if (!recent || (!j.active && sv.dismissed === `${j.kind}|${j.path}|${j.state}`)) { card.hidden = true; return; }
    card.hidden = false;
    const kind = SV_KIND[j.kind] || "Vorgang";
    const cancel = $("#sv-cancel");
    let body = "";
    if (j.active) {
      txt("#sv-job-title", `${kind} läuft`);
      txt("#sv-job-sub", j.elapsed_s > 0 ? `seit ${ptDur(j.elapsed_s)}` : "");
      body = `<p class="cp-num"><b>${esc(j.phase)}</b> · ${esc(String(j.percent))} %</p>
        <div class="cp-bar" role="progressbar" aria-label="Fortschritt" aria-valuemin="0" aria-valuemax="100" aria-valuenow="${Math.max(0, Math.min(100, j.percent))}"><div style="width:${Math.max(0, Math.min(100, j.percent))}%"></div></div>
        <p class="muted cp-cur">${esc(String(j.files_done))} von ${esc(String(j.files_total))} Dateien${j.current ? " · " + esc(j.current) : ""}</p>`;
      cancel.textContent = "Abbrechen";
      cancel.hidden = !j.can_cancel;       /* beim Ersetzen der Dateien nimmt die Konsole keinen Abbruch mehr an */
    } else {
      cancel.hidden = false;
      cancel.textContent = "Ausblenden";
      if (j.state === "done") {
        txt("#sv-job-title", `${kind} fertig`);
        txt("#sv-job-sub", "");
        if (j.kind === "backup")
          body = `<p class="cp-ok">${esc(String(j.ok_files))} Dateien gesichert, zurückgelesen und geprüft.</p>
            <p class="sv-job-path muted">Ordner: <code>${esc(j.path)}</code></p>`;
        else if (j.kind === "verify")
          body = `<p class="cp-ok">Alle ${esc(String(j.ok_files))} Dateien stimmen mit der Sicherung überein.</p>`;
        else if (j.kind === "delete")
          body = `<p class="cp-ok">Die Spielstände wurden gelöscht.</p>
            ${j.undo ? `<p class="sv-job-path muted">Der Stand davor liegt hier und lässt sich zurückspielen: <code>${esc(j.undo)}</code></p>` : ""}`;
        else
          body = `<p class="cp-ok">${esc(String(j.ok_files))} Dateien zurückgespielt und geprüft.</p>
            ${j.undo ? `<p class="sv-job-path muted">Der Stand davor liegt hier und lässt sich genauso zurückspielen: <code>${esc(j.undo)}</code></p>` : ""}
            ${j.note ? `<p class="muted">${esc(j.note)}</p>` : ""}`;
      } else if (j.state === "failed") {
        txt("#sv-job-title", `${kind} fehlgeschlagen`);
        txt("#sv-job-sub", "");
        body = `<p class="cp-err">${esc(j.error)}</p>
          ${j.undo ? `<p class="sv-job-path muted">Der Stand davor liegt hier: <code>${esc(j.undo)}</code></p>` : ""}`;
      } else {
        txt("#sv-job-title", `${kind} abgebrochen`);
        txt("#sv-job-sub", "");
        body = `<p class="muted">${j.kind === "verify" ? "Die Prüfung wurde abgebrochen."
          : j.kind === "restore" ? "Auf der Konsole wurde nichts verändert."
          : "Das Angefangene wurde entfernt."}</p>
          ${j.kind === "restore" && j.undo ? `<p class="sv-job-path muted">Die Sicherung des jetzigen Stands, die schon fertig war, bleibt liegen: <code>${esc(j.undo)}</code></p>` : ""}`;
      }
    }
    $("#sv-job").innerHTML = body;
  };

  const svRender = () => {
    if (!sv.loaded) { txt("#sv-sub", "wird gelesen …"); return; }
    if (!sv.data) {
      txt("#sv-sub", "nicht verfügbar");
      $("#sv-users").innerHTML = `<p class="pt-empty">Die Spielstände ließen sich nicht lesen: ${esc(sv.error)}</p>`;
      ["#sv-drives", "#sv-backups"].forEach((s) => { $(s).innerHTML = ""; });
      txt("#sv-bk-sub", "");
      txt("#sv-hint", "");
      $("#sv-start").disabled = true;
      $("#sv-game").hidden = true;
      $("#sv-job-card").hidden = true;
      return;
    }
    const d = sv.data;
    const game = $("#sv-game");
    game.hidden = !d.game_running;
    game.textContent = d.game_running
      ? `Ein Spiel läuft (${d.game_id}). Solange es läuft, werden keine Spielstände gesichert oder zurückgespielt, `
        + "auch von pausierten Spielen nicht. Beende das Spiel zuerst." : "";
    svRenderUsers();
    svRenderDrives();
    svRenderStart();
    svRenderBackups();
    svRenderJob();
  };

  const loadSaves = async () => {
    try {
      /* Die Konsole misst jeden Titel nach; während einer Sicherung dauert das länger. */
      const d = await api("/api/v1/saves", { timeoutMs: 20000 });
      if (!d || !Array.isArray(d.users) || !Array.isArray(d.drives) || !Array.isArray(d.backups))
        throw new Error("unerwartete Antwort der Konsole");
      sv.data = d;
      if (d.job) sv.job = d.job;
      sv.error = "";
      svSyncSelection();
    } catch (e) {
      sv.error = e.message || "nicht verfügbar";
      sv.data = null;
    }
    sv.loaded = true;
    svRender();
    svSync();
  };

  /* Zwischen den großen Abfragen genügt der Auftrag allein. Ist er zu Ende, lädt die Seite
     neu: Es gibt dann eine Sicherung mehr, und ein Spiel kann inzwischen laufen. */
  const svPoll = async () => {
    if (sv.polling) return;                     /* die Konsole antwortet langsam: nicht stapeln */
    sv.polling = true;
    try {
      const j = await api("/api/v1/saves/job");
      const was = svActive();
      sv.job = j;
      svRenderJob();
      if (was && !j.active) await loadSaves();
      svSync();
    } catch { /* die nächste Runde versucht es wieder */ }
    sv.polling = false;
  };

  /* Läuft nur, solange man es sehen kann: Reiter vorn, Seite vorn, ein Auftrag läuft. */
  const svSync = () => {
    const run = state.page === "games" && pt.tab === "saves" && !document.hidden && svActive();
    if (run && !sv.timer) {
      sv.timer = setInterval(svPoll, 1000);
    } else if (!run && sv.timer) {
      clearInterval(sv.timer);
      sv.timer = 0;
    }
  };

  const svStarted = (j) => {
    sv.job = j;
    sv.dismissed = "";
    if (sv.data) svRenderStart();
    svRenderJob();
    svSync();
    /* Wer weiter unten in der Liste geklickt hat, soll den Fortschritt sehen. */
    $("#sv-job-card").scrollIntoView({ behavior: "smooth", block: "start" });
  };

  const svStart = async () => {
    const d = sv.data;
    if (!d) return;
    const users = d.users.filter((u) => u.titles.some((t) => sv.sel.has(`${u.uid}/${t.id}`))).map((u) => u.uid);
    try {
      svStarted(await api("/api/v1/saves/backup", { method: "POST",
        body: JSON.stringify({ target: sv.drive, users, titles: [...sv.sel] }) }));
    } catch (e) { toast(e.message, "error"); }
  };

  /* Zwei Klicks auf denselben Knopf, innerhalb von zehn Sekunden. */
  const svArm = (btn, key, label, warn, run) => {
    const armed = sv.armed.get(key);
    if (!armed) {
      const orig = btn.textContent;
      btn.textContent = label;
      toast(warn);
      sv.armed.set(key, { orig, at: performance.now(), timer: setTimeout(() => { sv.armed.delete(key); btn.textContent = orig; }, 10000) });
      return;
    }
    /* Ein Doppelklick wäre Bewaffnen und Bestätigen in einem, ehe jemand die Warnung lesen konnte. */
    if (performance.now() - armed.at < 800) return;
    clearTimeout(armed.timer);
    sv.armed.delete(key);
    btn.textContent = armed.orig;
    run();
  };

  const svPane = $("#gm-pane-saves");
  svPane.addEventListener("change", (e) => {
    const t = e.target;
    if (t.matches("[data-sv-title]")) {
      if (t.checked) sv.sel.add(t.dataset.svTitle); else sv.sel.delete(t.dataset.svTitle);
      svUserBoxes();
      svRenderStart();
    } else if (t.matches("[data-sv-user]")) {
      const u = sv.data.users.find((x) => x.uid === t.dataset.svUser);
      if (u) u.titles.forEach((x) => {
        const k = `${u.uid}/${x.id}`;
        if (t.checked) sv.sel.add(k); else sv.sel.delete(k);
      });
      $$(`#sv-users [data-sv-title^="${t.dataset.svUser}/"]`).forEach((cb) => { cb.checked = t.checked; });
      svUserBoxes();
      svRenderStart();
    } else if (t.name === "sv-drive") {
      sv.drive = t.value;
      svRenderStart();
    }
  });
  svPane.addEventListener("click", async (e) => {
    const b = e.target.closest("button");
    if (!b) return;
    if (b.id === "sv-start") { svStart(); return; }
    if (b.id === "sv-reload") { loadSaves(); return; }
    if (b.id === "sv-cancel") {
      if (svActive()) {
        try { svStarted(await api("/api/v1/saves/cancel", { method: "POST" })); } catch (err) { toast(err.message, "error"); }
      } else if (sv.job) {
        sv.dismissed = `${sv.job.kind}|${sv.job.path}|${sv.job.state}`;
        svRenderJob();
      }
      return;
    }
    if (b.dataset.svOpen) {
      const path = b.dataset.svOpen;
      const open = !sv.open.has(path);
      if (open) sv.open.add(path); else sv.open.delete(path);
      b.setAttribute("aria-expanded", open ? "true" : "false");
      b.textContent = open ? "Titel ausblenden" : "Titel anzeigen";
      const box = b.closest(".sv-bk").querySelector(".sv-bk-titles");
      if (box) box.hidden = !open;
      return;
    }
    if (b.dataset.svVerify) {
      try {
        svStarted(await api("/api/v1/saves/verify", { method: "POST", body: JSON.stringify({ path: b.dataset.svVerify }) }));
      } catch (err) { toast(err.message, "error"); }
      return;
    }
    if (b.dataset.svDelete) {
      const key = b.dataset.svDelete, name = b.dataset.name;
      const [uid, id] = key.split("/");
      svArm(b, `del|${key}`, "Wirklich löschen?",
        `Die Spielstände von „${name}“ werden gelöscht. Der Stand davor wird zuerst auf dem gewählten Ziel gesichert und lässt `
        + "sich dann zurückspielen. Das geht nur, wenn kein Spiel läuft. Zum Bestätigen noch einmal klicken.", async () => {
          try {
            svStarted(await api("/api/v1/saves/delete", { method: "POST", body: JSON.stringify({ uid, id, mount: sv.drive || "" }) }));
          } catch (err) { toast(err.message, "error"); }
        });
      return;
    }
    if (b.dataset.svRestore) {
      const path = b.dataset.svRestore, uid = b.dataset.uid, id = b.dataset.id, name = b.dataset.name;
      svArm(b, `${path}|${uid}|${id}`, "Wirklich? Nochmal klicken",
        `„${name}“ wird zurückgespielt. Der jetzige Stand davon wird vorher als eigene Sicherung abgelegt. `
        + "Das geht nur, wenn kein Spiel läuft. Zum Bestätigen noch einmal klicken.", async () => {
          try {
            svStarted(await api("/api/v1/saves/restore", { method: "POST",
              body: JSON.stringify({ path, user: uid, title: id, confirm: true }) }));
          } catch (err) { toast(err.message, "error"); }
        });
    }
  });

  /* ── Pakete (04.10.2026) ─────────────────────────────────────────────
   * Spiel-Pakete (.pkg) auf Sticks, Discs und im Konsolenspeicher (pkgscan.c, /api/v1/packages):
   * finden, anzeigen und große Pakete in Teile aufteilen (pkgsplit.c), die auf einen FAT32-Stick
   * oder auf Discs passen. Die Suche und das Aufteilen laufen auf der Konsole; die Seite fragt
   * den Auftrag im Sekundentakt ab, solange er läuft und man ihn sehen kann.
   *
   * Der Browser nennt der Konsole nie einen Pfad: Pakete haben eine Kennung aus der Liste der
   * letzten Suche, und nur danach wird gefragt (Bild, Aufteilen). */
  const pk = { data: null, error: "", loaded: false, q: "", kind: "", open: "", target: "", preset: "fat32", gb: "",
               timer: 0, polling: false, job: null, dismissed: "", scanning: false,
               ipanel: "", iplan: null, iplanErr: "", ijob: null, idismissed: "",
               igen: 0, iplanTok: 0, ipollFail: 0, ijobRetry: 0, ijobTimer: 0 };
  const PK_KIND = { base: "Spiel", update: "Update", dlc: "Zusatzinhalt" };
  const PK_PRESET = {
    fat32: { mb: 4095, label: "FAT32-Stick", sub: "Teile bis 4095 MB (eine Datei darf dort nicht größer sein)" },
    bd25:  { mb: 23 * 1024, label: "Blu-ray, 25 GB", sub: "Teile von höchstens 23 GB" },
    bd50:  { mb: 46 * 1024, label: "Blu-ray, 50 GB", sub: "Teile von höchstens 46 GB" },
  };
  const PK_MIN_MB = 64;
  const PK_RESERVE = 64 * 1048576;
  const pkActive = () => !!(pk.job && pk.job.active);
  const pkiActive = () => !!(pk.ijob && pk.ijob.active);

  /* Was die Konsole als Zustand nennt, auf Deutsch; ein Wort, das hier fehlt, bleibt, wie es ist. */
  const PK_SYS_WORD = { running: "läuft", playable: "spielbar", completed: "abgeschlossen", installing: "wird installiert", queued: "wartet",
                        paused: "pausiert", error: "Fehler", none: "noch nicht angemeldet" };
  const pkiSysWord = (w) => PK_SYS_WORD[w] || w;

  /* Fragt den Installationsauftrag und übernimmt die Antwort nur, wenn zwischen Frage und Antwort nichts gestartet oder
     abgebrochen wurde (pk.igen zählt das): eine Antwort, die vor dem Start abgeschickt wurde, zeigt sonst „ruht“, obwohl
     die Installation läuft, und mit ihr stünden die Knöpfe wieder da und die Abfrage im Sekundentakt still. */
  const pkiRefresh = async () => {
    const g = pk.igen;
    try {
      const ij = await api("/api/v1/packages/install/job");
      if (g !== pk.igen || !ij || typeof ij.state !== "string") return false;
      pk.ipollFail = 0;
      if (pk.ijob && pk.ijob.state === "lost" && ij.state === "idle") return true;     /* „unterbrochen“ bleibt stehen, bis man es ausblendet */
      pk.ijob = ij;
      return true;
    } catch { return false; }
  };
  /* Beim Laden der Seite scheitert die Frage nach dem Auftrag manchmal einmal (die Konsole ist beschäftigt); dann wird sie
     noch ein paarmal wiederholt, damit eine laufende Installation nicht unsichtbar bleibt. */
  const pkiRetry = async () => {
    pk.ijobTimer = 0;
    if (pk.ijobRetry >= 3) return;
    pk.ijobRetry++;
    if (await pkiRefresh()) { pkRender(); pkSync(); } else pk.ijobTimer = setTimeout(pkiRetry, 2500);
  };

  /* Teilgröße in MB nach der Auswahl; 0, wenn die eigene Eingabe keine Zahl ist. */
  const pkMb = () => {
    if (pk.preset !== "custom") return PK_PRESET[pk.preset].mb;
    const v = Number(String(pk.gb).replace(",", "."));
    return Number.isFinite(v) && v > 0 ? Math.round(v * 1024) : 0;
  };
  /* Wie viele Teile es würden und wie viel Platz sie brauchen (auf 4096 Byte Kopf je Teil genau; das Bild kommt dazu). */
  const pkPlan = (p, mb) => {
    const per = mb * 1048576 - 4096;
    const parts = per > 0 ? Math.max(1, Math.ceil(p.size / per)) : 0;
    return { parts, need: p.size + parts * 4096 + PK_RESERVE };
  };
  const pkInitials = (n) => (String(n).match(/[A-Za-zÄÖÜäöü0-9]/g) || ["?"]).slice(0, 2).join("").toUpperCase();

  const pkMatch = (p) => {
    if (pk.kind && p.kind !== pk.kind) return false;
    const q = pk.q.trim().toLowerCase();
    return !q || [p.name, p.title_id, p.file, p.content_id].some((s) => String(s || "").toLowerCase().includes(q));
  };

  /* Das Aufteilen: wohin, in welchen Stücken, und ob der Platz reicht. Wird bei jeder Auswahl an Ort und Stelle
     aufgefrischt, damit die Tastaturführung nicht verloren geht. */
  const pkPanelHtml = (p) => {
    const d = pk.data;
    if (!d.drives.some((x) => x.mount === pk.target)) {
      const other = d.drives.find((x) => x.mount !== p.drive && x.mount !== "/user") || d.drives.find((x) => x.mount !== p.drive) || d.drives[0];
      pk.target = other ? other.mount : "";
    }
    return `<div class="pk-split" data-pk-panel="${esc(p.id)}">
      <p class="cp-step">Wohin sollen die Teile?</p>
      <div class="cp-list">${d.drives.map((x) =>
        `<label class="cp-choice"><input type="radio" name="pk-target" value="${esc(x.mount)}"${x.mount === pk.target ? " checked" : ""}>
          <span class="cp-choice-text"><b>${esc(x.label)}</b><span class="muted">${esc(bytes(x.free_bytes))} frei<span data-pk-room="${esc(x.mount)}"></span></span></span></label>`).join("")}</div>
      <p class="cp-step">Wie groß darf ein Teil sein?</p>
      <div class="cp-list">${Object.keys(PK_PRESET).map((k) =>
        `<label class="cp-choice"><input type="radio" name="pk-preset" value="${k}"${k === pk.preset ? " checked" : ""}>
          <span class="cp-choice-text"><b>${esc(PK_PRESET[k].label)}</b><span class="muted">${esc(PK_PRESET[k].sub)}</span></span></label>`).join("")}
        <label class="cp-choice"><input type="radio" name="pk-preset" value="custom"${pk.preset === "custom" ? " checked" : ""}>
          <span class="cp-choice-text"><b>Eigene Größe</b><span class="muted"><input type="text" inputmode="decimal" class="pk-gb" id="pk-gb" value="${esc(pk.gb)}"
            placeholder="z. B. 8" aria-label="Größe eines Teils in GB"> GB</span></span></label></div>
      <p class="muted pk-note" id="pk-split-hint"></p>
      <div class="cp-actions"><button type="button" class="btn ghost" data-pk-close>Schließen</button>
        <button type="button" class="btn primary" data-pk-split="${esc(p.id)}" disabled>Aufteilen</button></div>
    </div>`;
  };

  const pkPanelUpdate = () => {
    const panel = $("#pk-list [data-pk-panel]");
    if (!panel || !pk.data) return;
    const p = pk.data.packages.find((x) => x.id === panel.dataset.pkPanel);
    if (!p) return;
    const mb = pkMb(), { parts, need } = pkPlan(p, mb);
    const drive = pk.data.drives.find((x) => x.mount === pk.target);
    $$("#pk-list [data-pk-room]").forEach((s) => {
      const x = pk.data.drives.find((y) => y.mount === s.dataset.pkRoom);
      s.textContent = parts >= 2 && x && x.free_bytes < need ? " · zu wenig Platz" : "";
    });
    let why = "", ok = false;
    if (!drive) why = "Es ist kein Ziel gewählt.";
    else if (!(mb >= PK_MIN_MB)) why = `Ein Teil muss mindestens ${PK_MIN_MB} MB groß sein.`;
    else if (parts < 2) why = "Das Paket ist nicht größer als ein Teil und muss nicht geteilt werden.";
    else if (parts > 256) why = "Das wären mehr als 256 Teile. Bitte größere Teile wählen.";
    else if (drive.free_bytes < need) why = `Auf dem Ziel ist nicht genug Platz: nötig sind etwa ${bytes(need)}, frei sind ${bytes(drive.free_bytes)}.`;
    else if (pkActive()) why = "Es läuft schon ein Vorgang mit einem Paket.";
    else if (pk.data.scanning) why = "Die Suche läuft noch.";
    else ok = true;
    $("#pk-split-hint").textContent = why || `Etwa ${parts} Teile, zusammen ${bytes(p.size)}. Auf dem Ziel müssen etwa ${bytes(need)} frei sein; `
      + `die Teile kommen in den Ordner pkg des Ziels, jeder wird nach dem Schreiben zurückgelesen und geprüft, und das Paket bleibt unverändert.`;
    panel.querySelector("[data-pk-split]").disabled = !ok;
  };

  const pkCardHtml = (p) => {
    const open = pk.open === p.id;
    const parts = p.parts;
    const plat = p.plat === 5 ? "PS5" : p.plat === 4 ? "PS4" : "";
    const canSplit = !parts && p.size >= PK_MIN_MB * 1048576 * 2 && !pkActive() && !pkiActive();
    const canInstall = (!parts || parts.complete) && !pkActive() && !pkiActive();
    const iopen = pk.ipanel === p.id;
    return `<div class="pk-card" data-pk-id="${esc(p.id)}">
      <div class="pk-ico">${p.has_icon ? `<img src="/api/v1/packages/icon?id=${esc(p.id)}&m=${esc(String(p.mtime))}" alt="" loading="lazy">` : ""}<span>${esc(pkInitials(p.name))}</span></div>
      <div class="pk-main"><b title="${esc(p.name)}">${esc(p.name)}</b>
        <small>${esc(p.title_id)}${p.version ? " · " + esc(p.version) : ""}</small>
        <div class="pk-badges"><span class="badge">${esc(PK_KIND[p.kind] || "Paket")}</span>${plat ? `<span class="badge quiet">${plat}</span>` : ""}
          ${parts ? `<span class="badge ${parts.complete ? "quiet" : "warn"}">${parts.complete ? "geteilt, vollständig" : `Teile: ${esc(String(parts.found))} von ${esc(String(parts.total))}`}</span>` : ""}</div>
        ${parts && !parts.complete ? `<small>Es fehlt: ${parts.missing.map((n) => "Teil " + esc(String(n))).join(", ")}. Die fehlenden Teile liegen vielleicht auf einem anderen Laufwerk.</small>` : ""}
        <small class="pk-path" title="${esc(p.path)}">${esc(p.path)}</small></div>
      <div class="pk-side"><span class="pk-size">${esc(bytes(parts ? p.total : p.size))}</span>
        ${canInstall ? `<button type="button" class="btn" data-pk-iopen="${esc(p.id)}" aria-expanded="${iopen ? "true" : "false"}"
            aria-label="${esc(p.name)} installieren">${iopen ? "Schließen" : "Installieren …"}</button>` : ""}
        ${canSplit ? `<button type="button" class="btn ghost" data-pk-open="${esc(p.id)}" aria-expanded="${open ? "true" : "false"}"
            aria-label="${esc(p.name)} aufteilen">${open ? "Schließen" : "Aufteilen …"}</button>` : ""}</div>
      ${open ? pkPanelHtml(p) : ""}
      ${iopen ? pkInstPanelHtml(p) : ""}
    </div>`;
  };

  /* Das Installieren: erst fragt die Seite die Konsole, was passieren würde (Plan), dann kommt der Klick. Die Konsole
     prüft alles beim Start noch einmal selbst; der Plan zeigt es nur vorher. */
  const PK_INST_KIND = { base: "Spiel", update: "Update", dlc: "Zusatzinhalt" };
  const pkInstPanelHtml = (p) => {
    const q = pk.iplan && pk.iplan.id === p.id ? pk.iplan : null;
    let body;
    if (pk.iplanErr) {
      body = `<p class="cp-err">${esc(pk.iplanErr)}</p>`;
    } else if (!q) {
      body = `<p class="muted">Die Konsole prüft das Paket …</p>`;
    } else {
      const inst = q.installed && q.installed.state === 1
        ? `<li>Schon installiert: ${esc(q.installed.version || "Version unbekannt")}</li>` : "";
      body = `<ul class="pk-facts">
          <li><b>${esc(q.name)}</b> · ${esc(PK_INST_KIND[q.kind] || "Paket")}${q.version ? " · " + esc(q.version) : ""} · ${q.plat === 5 ? "PS5" : q.plat === 4 ? "PS4" : "unbekannte Plattform"}</li>
          <li>${esc(bytes(q.size))}${q.parts ? ` in ${esc(String(q.parts))} Teilen` : ""}${q.space && q.space.known ? ` · auf dem internen Speicher sind ${esc(bytes(q.space.free))} frei` : ""}</li>
          ${inst}</ul>
        ${q.warning ? `<p class="cp-warn">${esc(q.warning)}</p>` : ""}
        ${q.blocked ? `<p class="cp-err">${esc(q.blocked)}</p>` : ""}
        <p class="muted pk-note">Die Konsole installiert das Paket selbst, mit ihrer eigenen Installation. Die App stellt es ihr dafür bereit
          (nur auf der Konsole selbst, für diese eine Installation) und zeigt den Fortschritt. Das Paket wird nur gelesen; es wird nichts
          gelöscht oder überschrieben. Bricht man ab, stoppt die Installation; was die Konsole schon angelegt hat, kann liegen bleiben.
          Dazu muss die Konsole mit einem Netzwerk verbunden sein (LAN oder WLAN, Internet ist nicht nötig).</p>`;
    }
    const ok = !!(q && q.can_install && !pkActive() && !pkiActive() && !(pk.data && pk.data.scanning));
    return `<div class="pk-split" data-pk-ipanel="${esc(p.id)}">
      <p class="cp-step">Installieren</p>
      ${body}
      <div class="cp-actions"><button type="button" class="btn ghost" data-pk-iclose>Schließen</button>
        <button type="button" class="btn primary" data-pk-install="${esc(p.id)}"${ok ? "" : " disabled"}>Jetzt installieren</button></div>
    </div>`;
  };

  const pkiOpen = async (id) => {
    pk.ipanel = id;
    pk.open = "";
    pk.iplan = null;
    pk.iplanErr = "";
    const tok = ++pk.iplanTok;                           /* nur die Antwort auf die letzte Frage zählt, auch bei A → B → A */
    pkRenderList();
    try {
      const q = await api(`/api/v1/packages/install/plan?id=${encodeURIComponent(id)}`);
      if (tok !== pk.iplanTok || pk.ipanel !== id) return;     /* der Nutzer hat inzwischen etwas anderes aufgemacht */
      pk.iplan = q;
    } catch (e) {
      if (tok !== pk.iplanTok || pk.ipanel !== id) return;
      pk.iplanErr = e.message || "Die Konsole konnte das Paket nicht prüfen.";
    }
    pkRenderList();
  };

  const pkiRenderJob = () => {
    const j = pk.ijob, card = $("#pk-inst-card");
    const recent = j && j.state !== "idle" && (j.active || j.state === "lost" || (j.finished_ago_s >= 0 && j.finished_ago_s < 900));
    const key = j ? `${j.name}|${j.state}|${j.finished_ago_s >= 0 || j.state === "lost" ? "end" : ""}` : "";
    if (!recent || (!j.active && pk.idismissed === key)) { card.hidden = true; return; }
    card.hidden = false;
    const cancel = $("#pk-inst-cancel");
    let body = "";
    txt("#pk-inst-sub", j.name ? `${j.name}${j.version ? " · " + j.version : ""}` : "");
    const note = j.note ? `<p class="cp-warn">${esc(j.note)}</p>` : "";
    const left = `<p class="muted">${j.started ? "Was die Konsole schon angelegt hat, kann liegen bleiben; die App löscht nichts." : "Auf der Konsole wurde nichts angelegt; die App löscht nichts."}</p>`;
    if (j.active) {
      txt("#pk-inst-title", j.cancelling ? "Installieren wird abgebrochen" : "Paket wird installiert");
      const pc = Math.max(0, Math.min(100, Number.isFinite(j.percent) ? j.percent : 0));
      const sys = j.system_status ? ` · Konsole: ${esc(pkiSysWord(j.system_status))}` : "";
      const remain = j.remain_s > 0 && j.remain_s < 30 * 86400 && pc < 100 ? " · noch etwa " + esc(ptDur(j.remain_s)) : "";
      const stale = pk.ipollFail >= 3 ? `<p class="cp-warn">Die Seite erreicht die App gerade nicht. Die Anzeige kann veraltet sein; die Installation läuft auf der Konsole weiter.</p>` : "";
      body = `<p class="cp-num"><b>${esc(j.cancelling ? "Wird abgebrochen …" : j.phase)}</b> · ${esc(String(Math.round(pc)))} %</p>
        <div class="cp-bar" role="progressbar" aria-label="Fortschritt" aria-valuemin="0" aria-valuemax="100" aria-valuenow="${pc}"><div style="width:${pc}%"></div></div>
        <p class="muted cp-cur">${esc(bytes(j.bytes_done))} von ${esc(bytes(j.bytes_total))}${sys}${j.attempt > 1 ? ` · Versuch ${esc(String(j.attempt))}` : ""}${j.elapsed_s > 0 ? " · seit " + esc(ptDur(j.elapsed_s)) : ""}${remain}</p>${stale}${note}`;
      cancel.textContent = j.cancelling ? "Wird abgebrochen …" : "Abbrechen";
      cancel.disabled = !!j.cancelling;
    } else {
      cancel.textContent = "Ausblenden";
      cancel.disabled = false;
      if (j.state === "done" && j.verified) {
        txt("#pk-inst-title", "Paket installiert");
        body = `<p class="cp-ok">Die Konsole hat „${esc(j.name)}“ installiert${j.elapsed_s > 0 ? " (" + esc(ptDur(j.elapsed_s)) + ")" : ""}.</p>${note}`;
      } else if (j.state === "done" && j.blind) {
        /* Der Installationshelfer war nicht mehr zu haben: die App hat das ganze Paket geliefert, die Konsole aber nie nach dem Ergebnis fragen können. */
        txt("#pk-inst-title", "Paket geliefert, Ergebnis nicht bestätigt");
        body = `<p class="cp-warn">Die App hat „${esc(j.name)}“ vollständig an die Konsole geliefert${j.elapsed_s > 0 ? " (" + esc(ptDur(j.elapsed_s)) + ")" : ""}. Die Konsole ließ sich dabei nicht nach dem Fortschritt fragen, und ob die Installation fertig ist, ließ sich nicht bestätigen. Bitte an der Konsole nachsehen.</p>${note}`;
      } else if (j.state === "done") {
        /* Die Konsole sagt „fertig“, aber das Ergebnis steht (noch) nicht in ihrer Liste: das ist kein Erfolg, den die App bestätigen kann. */
        txt("#pk-inst-title", "Von der Konsole als fertig gemeldet");
        body = `<p class="cp-warn">Die Konsole meldet „${esc(j.name)}“ als fertig installiert${j.elapsed_s > 0 ? " (" + esc(ptDur(j.elapsed_s)) + ")" : ""}. Dass der Titel in ihrer Liste steht, ließ sich nicht bestätigen. Bitte an der Konsole nachsehen.</p>${note}`;
      } else if (j.state === "failed") {
        txt("#pk-inst-title", "Installieren fehlgeschlagen");
        body = `<p class="cp-err">${esc(j.error)}</p>${note}${left}`;
      } else if (j.state === "lost") {
        txt("#pk-inst-title", "Installieren unterbrochen");
        body = `<p class="cp-err">Die App weiß von dieser Installation nichts mehr (sie wurde vermutlich neu gestartet). Ob die Konsole sie zu Ende bringt, steht in ihrer Download-Liste.</p>${left}`;
      } else {
        txt("#pk-inst-title", "Installieren abgebrochen");
        body = note || `<p class="muted">Die Installation wurde gestoppt.</p>`;
      }
    }
    $("#pk-inst").innerHTML = body;
  };

  const pkRenderList = () => {
    const d = pk.data, host = $("#pk-list");
    const shown = d.packages.filter(pkMatch);
    if (!d.packages.length) {
      host.innerHTML = `<p class="pt-empty">${d.scanning ? "Die Suche läuft …" : "Auf den angesteckten Laufwerken liegt kein Paket. Gesucht wird im Hauptordner und im Ordner pkg jedes Laufwerks (im Konsolenspeicher nur in /data/pkg)."}</p>`;
      return;
    }
    if (!shown.length) { host.innerHTML = `<p class="pt-empty">Keine Treffer.</p>`; return; }
    host.innerHTML = d.drives.map((x) => {
      const mine = shown.filter((p) => p.drive === x.mount);
      if (!mine.length) return "";
      return `<h3 class="pk-drive"><span>${esc(x.label)}</span><span class="muted">${esc(String(mine.length))} ${mine.length === 1 ? "Paket" : "Pakete"} · ${esc(bytes(x.free_bytes))} frei</span></h3>
        ${mine.map(pkCardHtml).join("")}`;
    }).join("");
    /* Kein Bild im Paket oder nicht lesbar: das Bild verschwindet, die Initialen dahinter bleiben. */
    $$("#pk-list .pk-ico img").forEach((img) => img.addEventListener("error", () => img.classList.add("broken"), { once: true }));
    if (pk.open) pkPanelUpdate();
  };

  const pkRenderJob = () => {
    const j = pk.job, card = $("#pk-job-card");
    const recent = j && j.state !== "idle" && (j.active || (j.finished_ago_s >= 0 && j.finished_ago_s < 900));
    if (!recent || (!j.active && pk.dismissed === `${j.file}|${j.state}|${j.finished_ago_s >= 0 ? "end" : ""}`)) { card.hidden = true; return; }
    card.hidden = false;
    const cancel = $("#pk-cancel");
    let body = "";
    txt("#pk-job-sub", j.name || "");
    if (j.active) {
      txt("#pk-job-title", "Paket wird aufgeteilt");
      const pc = Math.max(0, Math.min(100, j.percent));
      body = `<p class="cp-num"><b>${esc(j.phase)}</b> · ${esc(String(Math.round(pc)))} %</p>
        <div class="cp-bar" role="progressbar" aria-label="Fortschritt" aria-valuemin="0" aria-valuemax="100" aria-valuenow="${pc}"><div style="width:${pc}%"></div></div>
        <p class="muted cp-cur">${j.part ? `Teil ${esc(String(j.part))} von ${esc(String(j.parts))} · ` : ""}${esc(bytes(j.bytes_done))} von ${esc(bytes(j.bytes_total))}${j.elapsed_s > 0 ? " · seit " + esc(ptDur(j.elapsed_s)) : ""}</p>`;
      cancel.textContent = "Abbrechen";
    } else {
      cancel.textContent = "Ausblenden";
      if (j.state === "done") {
        txt("#pk-job-title", "Paket aufgeteilt");
        body = `<p class="cp-ok">${esc(j.note)}</p>
          <p class="muted pk-sha">SHA-256 des Pakets: <code>${esc(j.sha256)}</code></p>`;
      } else if (j.state === "failed") {
        txt("#pk-job-title", "Aufteilen fehlgeschlagen");
        body = `<p class="cp-err">${esc(j.error)}</p><p class="muted">Auf dem Ziel wurde nichts angelegt.</p>`;
      } else {
        txt("#pk-job-title", "Aufteilen abgebrochen");
        body = `<p class="muted">Das Angefangene wurde entfernt.</p>`;
      }
    }
    $("#pk-job").innerHTML = body;
  };

  const pkRender = () => {
    if (!pk.loaded) { txt("#pk-sub", "wird gelesen …"); return; }
    if (!pk.data) {
      txt("#pk-sub", "nicht verfügbar");
      $("#pk-list").innerHTML = `<p class="pt-empty">Die Pakete ließen sich nicht lesen: ${esc(pk.error)}</p>`;
      $("#pk-job-card").hidden = true;
      $("#pk-inst-card").hidden = true;
      return;
    }
    const d = pk.data, n = d.packages.length;
    txt("#pk-sub", d.scanning
      ? `Die Suche läuft … ${d.parsed} gelesen${d.current ? " · " + d.current : ""}`
      : n ? `${n} ${n === 1 ? "Paket" : "Pakete"}${d.scanned_at ? ", gesucht " + svWhen(d.scanned_at) : ""}` : "keine Pakete gefunden");
    $("#pk-scan").disabled = !!d.scanning;
    pkRenderList();
    pkRenderJob();
    pkiRenderJob();
  };

  const loadPackages = async (scan) => {
    try {
      const d = await api(scan ? "/api/v1/packages/scan" : "/api/v1/packages", scan ? { method: "POST" } : {});
      if (!d || !Array.isArray(d.packages) || !Array.isArray(d.drives)) throw new Error("unerwartete Antwort der Konsole");
      pk.data = d;
      pk.error = "";
      /* Noch nie gesucht: das tut die erste Ansicht von selbst. */
      if (!d.ever && !d.scanning && !scan) { loadPackages(true); return; }
      const j = await api("/api/v1/packages/job");
      if (j && typeof j.state === "string") pk.job = j;
      /* eine Installation, die schon läuft, zeigt sich auch nach dem Neuladen; scheitert die Frage, wird sie bald wiederholt */
      if (await pkiRefresh()) pk.ijobRetry = 0;
      else if (!pk.ijobTimer) { pk.ijobRetry = 0; pk.ijobTimer = setTimeout(pkiRetry, 2500); }
    } catch (e) {
      pk.error = e.message || "nicht verfügbar";
      pk.data = null;
    }
    pk.loaded = true;
    pkRender();
    pkSync();
  };

  /* Läuft nur, solange man es sehen kann: Reiter vorn, Seite vorn, eine Suche oder ein Auftrag läuft. */
  const pkSync = () => {
    const run = state.page === "games" && pt.tab === "pkg" && !document.hidden && (pkActive() || pkiActive() || !!(pk.data && pk.data.scanning));
    if (run && !pk.timer) {
      pk.timer = setInterval(pkPoll, 1000);
    } else if (!run && pk.timer) {
      clearInterval(pk.timer);
      pk.timer = 0;
    }
  };

  const pkPoll = async () => {
    if (pk.polling) return;                     /* die Konsole antwortet langsam: nicht stapeln */
    pk.polling = true;
    try {
      const wasJob = pkActive(), wasScan = !!(pk.data && pk.data.scanning);
      if (wasJob) {
        const j = await api("/api/v1/packages/job");
        pk.job = j;
        pkRenderJob();
        if (!j.active) await loadPackages();    /* es gibt jetzt Teile mehr: die Liste neu */
      }
      if (pkiActive()) {
        const was = pk.ijob;
        if (await pkiRefresh()) {
          /* Eine Installation, die eben noch lief und jetzt „ruht“, ist nicht still zu Ende gegangen: die App wurde neu gestartet. */
          if (was.active && pk.ijob.state === "idle") pk.ijob = { state: "lost", active: false, name: was.name, version: was.version, started: was.started, finished_ago_s: 0 };
          pkiRenderJob();
          if (!pk.ijob.active) await loadPackages();    /* das Paket ist jetzt installiert (oder nicht): die Liste neu */
        } else if (pk.ijob === was) {
          pk.ipollFail++;                              /* die Antwort blieb aus: nach drei Runden sagt die Karte, dass sie veraltet sein kann */
          pkiRenderJob();
        }
      }
      if (wasScan) await loadPackages();
      pkSync();
    } catch { /* die nächste Runde versucht es wieder */ }
    pk.polling = false;
  };

  $("#pk-scan").addEventListener("click", () => loadPackages(true));
  $("#pk-search").addEventListener("input", (e) => { pk.q = e.target.value; if (pk.data) pkRenderList(); });
  $$("#gm-pane-pkg [data-pk-kind]").forEach((b) => b.addEventListener("click", () => {
    pk.kind = b.dataset.pkKind;
    $$("#gm-pane-pkg [data-pk-kind]").forEach((x) => x.classList.toggle("active", x === b));
    if (pk.data) pkRenderList();
  }));
  $("#pk-cancel").addEventListener("click", async () => {
    const j = pk.job;
    if (!j) return;
    if (!j.active) { pk.dismissed = `${j.file}|${j.state}|${j.finished_ago_s >= 0 ? "end" : ""}`; pkRenderJob(); return; }
    try {
      pk.job = await api("/api/v1/packages/cancel", { method: "POST" });
      pkRenderJob();
      pkSync();
      if (!pk.job.active) loadPackages();       /* schon zu Ende: die Knöpfe an den Karten kommen wieder, nichts fragt später danach */
    } catch (e) { toast(e.message, "error"); }
  });
  $("#pk-inst-cancel").addEventListener("click", async () => {
    const j = pk.ijob;
    if (!j) return;
    if (!j.active) { pk.idismissed = `${j.name}|${j.state}|${j.finished_ago_s >= 0 || j.state === "lost" ? "end" : ""}`; pkiRenderJob(); return; }
    if (j.cancelling) return;
    pk.igen++;                                   /* Antworten, die vor dem Abbrechen abgeschickt wurden, gelten nicht mehr */
    j.cancelling = true;                         /* die Karte sagt es sofort; die Konsole bestätigt es mit der Antwort */
    pkiRenderJob();
    try {
      pk.ijob = await api("/api/v1/packages/install/cancel", { method: "POST" });
      pkiRenderJob();
      pkSync();
      if (!pk.ijob.active) loadPackages();      /* schon zu Ende: die Knöpfe an den Karten kommen wieder, nichts fragt später danach */
    } catch (e) {
      j.cancelling = false;
      pkiRenderJob();
      toast(e.message, "error");
    }
  });
  $("#pk-list").addEventListener("click", async (e) => {
    const iopen = e.target.closest("[data-pk-iopen]");
    if (iopen) {
      if (pk.ipanel === iopen.dataset.pkIopen) { pk.ipanel = ""; pkRenderList(); } else pkiOpen(iopen.dataset.pkIopen);
      return;
    }
    if (e.target.closest("[data-pk-iclose]")) { pk.ipanel = ""; pkRenderList(); return; }
    const inst = e.target.closest("[data-pk-install]");
    if (inst && !inst.disabled) {
      inst.disabled = true;
      pk.igen++;                                          /* eine ältere Antwort auf die Frage nach dem Auftrag gilt von jetzt an nicht mehr */
      try {
        pk.ijob = await api("/api/v1/packages/install", { method: "POST", body: JSON.stringify({ id: inst.dataset.pkInstall }) });
        pk.idismissed = "";
        pk.ipanel = "";
        pkRender();
        pkSync();
        $("#pk-inst-card").scrollIntoView({ behavior: "smooth", block: "start" });
      } catch (err) {
        toast(err.message, "error");
        /* Blieb die Antwort aus (Zeitüberschreitung, kein Netz), kann der Start trotzdem angekommen sein: erst nachsehen. */
        if (!err.status && await pkiRefresh() && pkiActive()) { pk.ipanel = ""; pkRender(); pkSync(); return; }
        pkiOpen(inst.dataset.pkInstall);                /* der Plan zeigt, was jetzt im Weg ist */
      }
      return;
    }
    const open = e.target.closest("[data-pk-open]");
    if (open) { pk.open = pk.open === open.dataset.pkOpen ? "" : open.dataset.pkOpen; pk.ipanel = ""; pkRenderList(); return; }
    if (e.target.closest("[data-pk-close]")) { pk.open = ""; pkRenderList(); return; }
    const go = e.target.closest("[data-pk-split]");
    if (go && !go.disabled) {
      go.disabled = true;
      try {
        pk.job = await api("/api/v1/packages/split", { method: "POST",
          body: JSON.stringify({ id: go.dataset.pkSplit, target: pk.target, part_mb: pkMb() }) });
        pk.dismissed = "";
        pk.open = "";
        pkRender();
        pkSync();
        $("#pk-job-card").scrollIntoView({ behavior: "smooth", block: "start" });
      } catch (err) {
        toast(err.message, "error");
        pkPanelUpdate();
      }
    }
  });
  $("#pk-list").addEventListener("change", (e) => {
    if (e.target.name === "pk-target") pk.target = e.target.value;
    else if (e.target.name === "pk-preset") pk.preset = e.target.value;
    else return;
    pkPanelUpdate();
  });
  $("#pk-list").addEventListener("input", (e) => {
    if (e.target.id !== "pk-gb") return;
    pk.gb = e.target.value;
    pk.preset = "custom";
    const radio = $('#pk-list input[name=pk-preset][value=custom]');
    if (radio) radio.checked = true;
    pkPanelUpdate();
  });

  /* ── Bedienung ──────────────────────────────────────────────────── */

  $$(".tab").forEach((b) => b.addEventListener("click", () => {
    if (!b.dataset.page) return;                       /* Handbuch und FAQ sind Links, keine Seiten */
    state.page = b.dataset.page;
    try { sessionStorage.setItem("ps5page", state.page); } catch { /* ohne Speicher: kein Merken */ }
    $$(".tab").forEach((x) => {
      x.classList.toggle("active", x === b);
      x.setAttribute("aria-selected", x === b ? "true" : "false");
    });
    txt("#page-title", b.textContent.trim());
    $$(".page").forEach((p) =>
      p.classList.toggle("active", p.id === `page-${state.page}`));
    if (state.page === "credits") crProbe();
    if (state.page === "files") fmOpen();
    if (state.page === "log" && kl.tab === "app") loadLog();
    if (state.page === "log" && kl.tab === "klog") klFilesLoad();
    syncKlog();
    klRecSync();
    if (state.page === "system") {
      loadSystem(); loadHealth();
      if (state.status) renderCores(state.status);
    }
    if (state.page === "cooling") { loadRules(); renderCharts(); loadHistory(); }
    if (state.page === "payloads") loadPayloadsPage();
    if (state.page === "profile") loadProfile();
    if (state.page === "games") {
      loadGames(); cpPoll(); mvPoll(); cvPoll();
      if (pt.tab === "time") loadPlaytime();
      if (pt.tab === "saves") loadSaves();
      if (pt.tab === "pkg") loadPackages();
    }
    syncPlaytime();
    svSync();
    pkSync();
    syncLogTail();
  }));

  /* Nach dem Umschalten der Sprache lädt die Seite neu (i18n.js); sie öffnet dann wieder die Seite, auf der man war. */
  try {
    const back = sessionStorage.getItem("ps5restore");
    if (back) {
      sessionStorage.removeItem("ps5restore");
      const tab = $(`.tab[data-page="${back}"]`);
      if (tab) tab.click();
    }
  } catch { /* ohne Speicher: es bleibt bei der Kühlung */ }

  $("#target-range").addEventListener("input", (e) => {
    state.targetDirty = true;
    txt("#target-out", `${e.target.value} °C`);
    updateTargetHint(Number(e.target.value));
  });

  $("#apply-target").addEventListener("click", async () => {
    if (!needCfg()) return;
    try {
      await saveConfig({ target_temp_c: Number($("#target-range").value) },
                       "Zieltemperatur gespeichert.");
      state.targetDirty = false;
    } catch (e) { toast(e.message, "error"); }
  });

  $$("input[name=profile]").forEach((r) => r.addEventListener("change", () => {
    saveConfig({ profile: r.value }, "Betriebsart geändert.")
      .catch((e) => toast(e.message, "error"));
  }));

  $("#auto-mode").addEventListener("change", (e) => {
    saveConfig({ mode: e.target.checked ? "automatic" : "observe" },
               e.target.checked ? "Automatik eingeschaltet."
                                : "Automatik aus — nur Beobachtung.")
      .catch((err) => toast(err.message, "error"));
  });

  /* Der Schlüssel heißt weiter ps_button_status: so bleiben alte Einstellungen
     und die API gültig, obwohl die Anzeige seit 1.46.0 auf der Mikrofon-Taste liegt. */
  if ($("#mic-button-status")) $("#mic-button-status").addEventListener("change", (e) => {
    saveConfig({ ps_button_status: e.target.checked ? 1 : 0 },
               e.target.checked ? "Anzeige per Mikrofon-Taste eingeschaltet."
                                : "Anzeige per Mikrofon-Taste ausgeschaltet.")
      .catch((err) => toast(err.message, "error"));
  });

  /* [field, config key, label]. The allowed range is the field's own min/max.
     An empty or non-numeric field is refused, never sent as 0: Number("") is 0,
     the console would clamp a 0 port to 1, and the page would be unreachable
     (and the saved setting would keep it so after every restart). */
  const ADV_FIELDS = [
    ["#s-safe", "safety_temp_c", "Notfallgrenze"],
    ["#s-wcpu", "warning_cpu_c", "Warnung ab"],
    ["#s-port", "http_port", "Port der Weboberfläche"],
    ["#s-warn-countdown", "warning_countdown_s", "Warn-Countdown"],
    ["#s-retention-days", "telemetry_retention_days", "Retention"],
    ["#s-lightbar-warn", "lightbar_warn_c", "Lightbar GELB"],
    ["#s-lightbar-hot", "lightbar_hot_c", "Lightbar ROT"],
    ["#s-fan-reapply", "fan_reapply_sec", "Reapply-Intervall"]
  ];

  const readAdvancedPatch = () => {
    const patch = {}, bad = [];
    ADV_FIELDS.forEach(([sel, key, label]) => {
      const el = $(sel);
      if (!el) return;
      const raw = String(el.value).trim();
      const v = Number(raw);
      const lo = el.min === "" ? -Infinity : Number(el.min);
      const hi = el.max === "" ? Infinity : Number(el.max);
      if (raw === "" || !Number.isInteger(v) || v < lo || v > hi)
        bad.push(Number.isFinite(lo) && Number.isFinite(hi) ? `${label} (${lo}–${hi})` : label);
      else
        patch[key] = v;
    });
    return { patch, bad };
  };

  $("#save-adv").addEventListener("click", () => {
    if (!needCfg()) return;
    const { patch, bad } = readAdvancedPatch();
    if (bad.length) {
      toast(`Bitte ganze Zahlen im erlaubten Bereich eintragen: ${bad.join(", ")}.`, "error");
      return;
    }
    patch.bind_address = String($("#s-bind") ? $("#s-bind").value : "0.0.0.0").trim() || "0.0.0.0";
    patch.lightbar_enabled = $("#s-lightbar-enabled") && $("#s-lightbar-enabled").checked ? 1 : 0;
    saveConfig(patch, "Einstellungen gespeichert.").catch((e) => toast(e.message, "error"));
  });

  const applyDirectThreshold = $("#apply-direct-threshold");
  const applyDirectValue = async (value) => {
    /* An empty field is not a 0: Number("") would pass the check below. */
    const val = value === "" || value === null || value === undefined ? NaN : Number(value);
    if (!Number.isFinite(val)) {
      toast("Bitte einen gültigen Direktwert eingeben.", "error");
      return;
    }
    try {
      const r = await api("/api/v1/fan/threshold", {
        method: "POST",
        body: JSON.stringify({ threshold_c: Math.round(val) })
      });
      toast(r.message || "Direktwert gesetzt.");
      await loadConfig();
      await refresh();
    } catch (e) {
      toast(e.message, "error");
    }
  };

  if (applyDirectThreshold)
    applyDirectThreshold.addEventListener("click", () => {
      const inp = $("#s-direct-threshold");
      applyDirectValue(inp ? inp.value : NaN);
    });

  const presetApply = (kind) => {
    /* Mode and limits come from what the console reports right now, the same
       source that labels the buttons (renderReactor). A config that is stale
       or not loaded yet must not turn an automatic-mode click into an
       observe-mode value: 58 would be clamped to the lower end of the range. */
    const auto = state.status ? !!state.status.fan.automatic
               : state.cfg ? state.cfg.mode === "automatic" : null;
    if (auto === null) {
      toast("Der Zustand der Konsole ist noch nicht geladen.", "error");
      return Promise.resolve();
    }
    let value = PRESETS[auto ? "automatic" : "observe"][kind];
    if (value === undefined) return Promise.resolve();
    if (auto) {
      const lo = state.cfg && Number.isFinite(state.cfg.target_min_c) ? state.cfg.target_min_c : 60;
      const hi = state.cfg && Number.isFinite(state.cfg.target_max_c) ? state.cfg.target_max_c : 91;
      value = Math.max(lo, Math.min(hi, value));
    }
    const inp = $("#s-direct-threshold");
    if (inp) inp.value = String(value);
    return applyDirectValue(value);
  };

  /* Die drei Voreinstellungen — die Schnellwahl der Kühlungsseite
     (.co-preset, #preset-*). Während der
     Anfrage gesperrt, damit ein Doppeldruck nicht zweimal sendet. Kein
     eigenes refresh() danach: Der Sekundentakt unten hat eine Sperre gegen
     sich stapelnde Abfragen, an der ein Extra-Aufruf vorbeiliefe. */
  $$(".co-preset").forEach((b) => b.addEventListener("click", async () => {
    b.disabled = true;
    try { await presetApply(b.dataset.kind); } finally { b.disabled = false; }
  }));

  const curveAdd = $("#fan-curve-add");
  if (curveAdd) {
    curveAdd.addEventListener("click", () => {
      const points = readCurveEditorPoints();
      if (points.length >= FAN_CURVE_MAX_POINTS) {
        toast(`Maximal ${FAN_CURVE_MAX_POINTS} Punkte erlaubt.`, "error");
        return;
      }
      const last = points[points.length - 1] || { temperature_c: 70, duty_pct: 30 };
      points.push({
        temperature_c: Math.min(FAN_CURVE_TEMP_MAX, last.temperature_c + 2),
        duty_pct: Math.min(100, last.duty_pct + 10)
      });
      renderCurveEditor(points);
    });
  }

  const curveSave = $("#fan-curve-save");
  if (curveSave) {
    curveSave.addEventListener("click", async () => {
      /* Before the first load the editor holds nothing, and normalising an
         empty list yields the default curve, which would overwrite the real one. */
      if (!needCfg()) return;
      const points = readCurveEditorPoints();
      try {
        await saveConfig({ curve: points }, "Lüfterkurve gespeichert.");
      } catch (e) {
        toast(e.message, "error");
      }
    });
  }

  const curveReset = $("#fan-curve-reset");
  if (curveReset) {
    curveReset.addEventListener("click", () => {
      renderCurveEditor(FAN_CURVE_DEFAULT);
      toast("Standardkurve geladen. Mit \"Kurve speichern\" übernehmen.");
    });
  }

  const bindLan = $("#bind-lan");
  if (bindLan) bindLan.addEventListener("click", () => {
    const inp = $("#s-bind");
    if (inp) inp.value = "0.0.0.0";
    toast("Bind-Adresse auf LAN-Zugriff vorbereitet. Mit Speichern übernehmen.");
  });
  const bindLocal = $("#bind-local");
  if (bindLocal) bindLocal.addEventListener("click", () => {
    const inp = $("#s-bind");
    if (inp) inp.value = "127.0.0.1";
    toast("Bind-Adresse auf lokal vorbereitet. Mit Speichern übernehmen.");
  });

  $("#ensure-tile").addEventListener("click", async () => {
    try {
      const r = await api("/api/v1/tile/ensure", { method: "POST" });
      toast(r.message || "Kachel wird installiert.");
      txt("#tile-msg", r.message || "");
      /* Die Installation läuft im Hintergrund; das Protokoll zeigt sie. */
      setTimeout(loadLog, 2500);
    } catch (e) { toast(e.message, "error"); txt("#tile-msg", e.message); }
  });

  /* Das Paket wandert roh in den Rumpf der Anfrage — kein Formular-Wrapper,
     damit die Konsole es unverändert auf die Platte schreiben kann. */
  $("#tile-upload").addEventListener("click", async () => {
    const f = $("#tile-file").files[0];
    if (!f) { toast("Erst eine .pkg-Datei auswählen.", "error"); return; }

    txt("#tile-msg", `${f.name} wird übertragen …`);
    try {
      const res = await fetch("/api/v1/tile/upload", {
        method: "POST",
        headers: { "Content-Type": "application/octet-stream" },
        body: f
      });
      const d = await res.json().catch(() => ({}));
      if (!res.ok || d.ok === false) throw new Error(d.message || `HTTP ${res.status}`);
      toast(d.message || "Paket empfangen.");
      txt("#tile-msg", d.message || "");
    } catch (e) { toast(e.message, "error"); txt("#tile-msg", e.message); }
  });

  $("#hist-reset").addEventListener("click", async () => {
    try {
      await api("/api/v1/history", { method: "POST" });
      toast("Verlauf zurückgesetzt.");
      loadHistory();
    } catch (e) { toast(e.message, "error"); }
  });

  $("#rule-save").addEventListener("click", async () => {
    const g = state.status && state.status.game;
    if (!g || !g.title_id) { toast("Es läuft kein erkanntes Spiel.", "error"); return; }
    if (!needCfg()) return;
    const c = state.cfg;
    try {
      await api("/api/v1/games", { method: "PUT", body: JSON.stringify({
        title_id:      g.title_id,
        title_name:    g.title_name || g.title_id,
        target_temp_c: c.target_temp_c,
        profile:       c.profile
      })});
      toast(`Profil für ${g.title_name || g.title_id} gespeichert.`);
      loadRules();
    } catch (e) { toast(e.message, "error"); }
  });

  wireTooltip("#host-hist", "#cross-hist", "#tip-hist", (_, idx) => {
    const h = state.hist2;
    if (!h) return "";
    return `<b>${new Date(h.t[idx]).toLocaleString(LOCALE)}</b>
      <u><i style="background:var(--s1)"></i>CPU ${h.cpu[idx] ?? "--"} °C</u>
      <u><i style="background:var(--s2)"></i>Hauptchip ${h.soc[idx] ?? "--"} °C</u>
      <u><i style="background:var(--s3)"></i>Lüfter ${h.fan[idx] ?? "--"} %</u>`;
  }, () => (state.hist2 ? state.hist2.t.length : 0));

  wirePayloads();
  wirePower();
  wireProbes();
  wireUsername();
  wireAvatar();
  const diagWindow = $("#diag-window");
  if (diagWindow) {
    state.diagWindowMin = loadDiagWindowMin();
    diagWindow.querySelectorAll("button[data-minutes]")
      .forEach((b) => b.classList.toggle("active",
        Number(b.getAttribute("data-minutes")) === state.diagWindowMin));
    diagWindow.querySelectorAll("button[data-minutes]").forEach((btn) => {
      btn.addEventListener("click", () => {
        const mins = Number(btn.getAttribute("data-minutes"));
        if (!Number.isFinite(mins) || mins <= 0) return;
        state.diagWindowMin = mins;
        saveDiagWindowMin(mins);
        diagWindow.querySelectorAll("button[data-minutes]")
          .forEach((b) => b.classList.toggle("active", b === btn));
        renderDiagEvents(true);
      });
    });
  }
  const diagClear = $("#diag-events-clear");
  if (diagClear) {
    diagClear.addEventListener("click", () => {
      state.diagEvents.length = 0;
      state.lastAmpelLevel = null;
      state.lastFanDiagLevel = null;
      renderDiagEvents();
      toast("Diagnose-Zeitleiste zurückgesetzt.");
    });
  }
  $("#reload-log").addEventListener("click", loadLog);

  $("#log-tab-app").addEventListener("click", () => klSelectTab("app"));
  $("#log-tab-klog").addEventListener("click", () => klSelectTab("klog"));
  $("#kl-pause").addEventListener("click", () => {
    kl.paused = !kl.paused;
    $("#kl-pause").textContent = kl.paused ? "Weiter" : "Pause";
    klSub();
    syncKlog();
  });
  $("#kl-clear").addEventListener("click", () => { kl.lines = []; klRenderAll(); });
  $("#kl-follow").addEventListener("change", (ev) => {
    kl.follow = ev.target.checked;
    if (kl.follow) { const v = $("#kl-view"); v.scrollTop = v.scrollHeight; }
  });
  /* Wer hochscrollt, will lesen: „Mitlaufen“ geht aus, und kommt wieder, wenn
     er ans Ende zurückscrollt. */
  $("#kl-view").addEventListener("scroll", () => {
    const v = $("#kl-view");
    const atEnd = v.scrollHeight - v.scrollTop - v.clientHeight < 24;
    if (atEnd !== kl.follow) { kl.follow = atEnd; $("#kl-follow").checked = atEnd; }
  });
  $("#kl-filter").addEventListener("input", (ev) => {
    const words = ev.target.value.toLowerCase().split(/\s+/).filter(Boolean);
    kl.inc = words.filter((w) => w[0] !== "-");
    kl.exc = words.filter((w) => w[0] === "-" && w.length > 1).map((w) => w.slice(1));
    clearTimeout(kl.typing);
    kl.typing = setTimeout(klRenderAll, 150);
  });
  $("#kl-presets").addEventListener("click", (ev) => {
    const b = ev.target.closest("[data-kl]");
    if (!b) return;
    kl.preset = b.dataset.kl;
    $$("#kl-presets .gm-chip").forEach((x) => x.classList.toggle("active", x === b));
    klRenderAll();
  });
  /* Der Auszug, den der Filter durchlässt, mit Datum und Uhrzeit des Browsers (die Zeitzone steht in
     der ersten Zeile). Er geht als Datei herunter („Herunterladen“) und auf die Konsole („Speichern“,
     Ordner klog-live-log). */
  const klExcerpt = () => {
    const p2 = (n) => (n < 10 ? "0" : "") + n;
    const out = [];
    kl.lines.forEach((e) => {
      if (!klMatch(e)) return;
      const d = new Date(e.t);
      out.push(`${d.getFullYear()}-${p2(d.getMonth() + 1)}-${p2(d.getDate())} ` +
               `${p2(d.getHours())}:${p2(d.getMinutes())}:${p2(d.getSeconds())}  ${e.s}`);
    });
    if (!out.length) return "";
    const tz = -new Date().getTimezoneOffset();
    const zone = tz === 0 ? "UTC" : `UTC${tz < 0 ? "-" : "+"}${p2(Math.floor(Math.abs(tz) / 60))}:${p2(Math.abs(tz) % 60)}`;
    return `# PS5 Kernel-Log, Auszug (Filter angewandt); Zeiten nach der Zeitzone des Browsers (${zone})\n` +
           out.join("\n") + "\n";
  };

  $("#kl-dl").addEventListener("click", () => {
    const text = klExcerpt();
    if (!text) { toast("Keine Zeilen zum Herunterladen."); return; }
    const url = URL.createObjectURL(new Blob([text], { type: "text/plain;charset=utf-8" }));
    Object.assign(document.createElement("a"), { href: url, download: "ps5-kernel-log.log" }).click();
    URL.revokeObjectURL(url);
  });

  $("#kl-save").addEventListener("click", async () => {
    const text = klExcerpt();
    if (!text) { toast("Keine Zeilen zum Speichern."); return; }
    const btn = $("#kl-save");
    btn.disabled = true;
    try {
      const r = await api(`/api/v1/klog/save?tz=${-new Date().getTimezoneOffset()}`, {
        method: "POST", body: text, headers: { "Content-Type": "text/plain; charset=utf-8" }, timeoutMs: 30000 });
      toast(`Gespeichert auf der Konsole: ${r.name} (${bytes(r.bytes)}), Ordner klog-live-log.`);
      klFilesLoad();
    } catch (e) { toast(e.message, "error"); }
    btn.disabled = false;
  });

  /* Aufnahme: die Konsole schreibt selbst mit (klogfiles.c); die Seite zeigt nur den Stand. */
  const klRec = { on: false, timer: 0 };
  const klRecShow = (r) => {
    klRec.on = !!(r && r.recording);
    const b = $("#kl-rec");
    b.setAttribute("aria-pressed", klRec.on ? "true" : "false");
    b.textContent = klRec.on ? "Aufnahme beenden" : "Aufnahme";
  };
  const klRecPoll = () => klFilesLoad(true);
  /* Fragt alle zwei Sekunden, solange man es sehen kann und eine Aufnahme läuft. */
  const klRecSync = () => {
    const run = klRec.on && state.page === "log" && kl.tab === "klog" && !document.hidden;
    if (run && !klRec.timer) {
      klRec.timer = setInterval(klRecPoll, 2000);
    } else if (!run && klRec.timer) {
      clearInterval(klRec.timer);
      klRec.timer = 0;
    }
  };
  $("#kl-rec").addEventListener("click", async () => {
    const btn = $("#kl-rec");
    btn.disabled = true;
    try {
      const r = await api("/api/v1/klog/record", { method: "POST",
        body: JSON.stringify({ on: !klRec.on, tz: -new Date().getTimezoneOffset() }) });
      klRecShow(r);
      toast(r.recording ? `Aufnahme läuft: ${r.file}` : "Aufnahme beendet.");
      klFilesLoad();
    } catch (e) { toast(e.message, "error"); }
    btn.disabled = false;
    klRecSync();
  });

  /* Was im Ordner liegt. `quiet`: nur die Liste auffrischen, keine Fehlermeldung. */
  const klFilesLoad = async (quiet) => {
    try {
      const d = await api("/api/v1/klog/files");
      const was = klRec.on;
      klRecShow(d.record);
      /* ended by itself (size, space, a write error) or from another tab: say why */
      if (was && d.record && !d.record.recording && d.record.reason && d.record.reason !== "angehalten")
        toast(`Aufnahme beendet: ${d.record.reason}`);
      txt("#kl-files-sub", d.count
        ? `${num(d.count)} ${d.count === 1 ? "Datei" : "Dateien"} · ${bytes(d.bytes)}` +
          (d.record && d.record.recording ? ` · Aufnahme läuft: ${bytes(d.record.bytes)}` : "")
        : "noch nichts gespeichert");
      $("#kl-files").innerHTML = d.files.length ? d.files.map((f) => `<div class="kl-file">
        <span class="kl-file-name"><b>${esc(f.name)}</b>
          <small>${f.kind === "rec" ? "Aufnahme" : "Auszug"} · ${esc(new Date(f.mtime * 1000).toLocaleString(LOCALE, {
            day: "2-digit", month: "2-digit", year: "numeric", hour: "2-digit", minute: "2-digit" }))}</small></span>
        ${f.active ? `<span class="badge warn">läuft</span>` : ""}
        <span class="kl-file-size">${esc(bytes(f.size))}</span>
        <a class="btn ghost" href="/api/v1/klog/file?name=${encodeURIComponent(f.name)}" download>Herunterladen</a>
      </div>`).join("") : `<p class="pt-empty">Der Ordner ist leer.</p>`;
      klRecSync();
    } catch (e) {
      if (!quiet) { txt("#kl-files-sub", "nicht lesbar"); $("#kl-files").innerHTML = `<p class="pt-empty">${esc(e.message)}</p>`; }
    }
  };
  $("#kl-files-reload").addEventListener("click", () => klFilesLoad());
  armConfirm($("#kl-files-clear"), "Wirklich leeren? Nochmal klicken", async () => {
    try {
      const r = await api("/api/v1/klog/files/clear", { method: "POST" });
      toast(r.deleted ? `${r.deleted} ${r.deleted === 1 ? "Datei" : "Dateien"} gelöscht (${bytes(r.bytes)}).` +
                        (r.kept ? ` ${r.kept} bleibt: die laufende Aufnahme.` : "") : "Es gab nichts zu löschen.");
      klFilesLoad();
    } catch (e) { toast(e.message, "error"); }
  }, "Alle gespeicherten Auszüge und Aufnahmen im Ordner klog-live-log werden gelöscht. Eine laufende Aufnahme bleibt. " +
     "Zum Bestätigen noch einmal klicken.");

  $("#export-log").addEventListener("click", async () => {
    try {
      const res = await fetch("/api/v1/logs/export");
      if (!res.ok) throw new Error("Export fehlgeschlagen");
      const url = URL.createObjectURL(await res.blob());
      Object.assign(document.createElement("a"),
        { href: url, download: "ps5-temperatur-manager.log" }).click();
      URL.revokeObjectURL(url);
    } catch (e) { toast(e.message, "error"); }
  });

  const rulesExport = $("#rules-export");
  if (rulesExport) {
    rulesExport.addEventListener("click", async () => {
      try {
        const res = await fetch("/api/v1/games/export");
        if (!res.ok) throw new Error("Export fehlgeschlagen");
        const url = URL.createObjectURL(await res.blob());
        Object.assign(document.createElement("a"),
          { href: url, download: "ps5tm-game-rules.json" }).click();
        URL.revokeObjectURL(url);
      } catch (e) { toast(e.message, "error"); }
    });
  }

  const rulesImport = $("#rules-import");
  if (rulesImport) {
    rulesImport.addEventListener("click", async () => {
      const fi = $("#rules-import-file");
      const f = fi && fi.files && fi.files[0];
      if (!f) { toast("Bitte zuerst eine JSON-Datei wählen.", "error"); return; }
      try {
        const txtRaw = await f.text();
        await api("/api/v1/games/import", { method: "POST", body: txtRaw });
        toast("Spielprofile importiert.");
        loadRules();
      } catch (e) { toast(e.message, "error"); }
    });
  }

  const cfgExport = $("#config-export");
  if (cfgExport) {
    cfgExport.addEventListener("click", async () => {
      try {
        const res = await fetch("/api/v1/config/export");
        if (!res.ok) throw new Error("Export fehlgeschlagen");
        const url = URL.createObjectURL(await res.blob());
        Object.assign(document.createElement("a"),
          { href: url, download: "ps5tm-config.json" }).click();
        URL.revokeObjectURL(url);
      } catch (e) { toast(e.message, "error"); }
    });
  }

  const cfgImport = $("#config-import");
  if (cfgImport) {
    cfgImport.addEventListener("click", async () => {
      const fi = $("#config-import-file");
      const f = fi && fi.files && fi.files[0];
      if (!f) { toast("Bitte zuerst eine JSON-Datei wählen.", "error"); return; }
      try {
        const txtRaw = await f.text();
        await api("/api/v1/config/import", { method: "POST", body: txtRaw });
        toast("Konfiguration importiert.");
        await loadConfig();
        await refresh();
        await loadRules();
        await loadSystem();
      } catch (e) { toast(e.message, "error"); }
    });
  }

  const webProfileExport = $("#webprofile-export");
  if (webProfileExport) {
    webProfileExport.addEventListener("click", async () => {
      try {
        const out = await buildWebProfileObject();
        const json = `${JSON.stringify(out, null, 2)}\n`;
        const blob = new Blob([json], { type: "application/json" });
        const url = URL.createObjectURL(blob);
        const stamp = new Date().toISOString().replace(/[:]/g, "-").replace(/\..+$/, "");
        Object.assign(document.createElement("a"), {
          href: url,
          download: `ps5tm-web-profile-${stamp}.json`
        }).click();
        URL.revokeObjectURL(url);
        toast("Web-Profil exportiert.");
      } catch (e) {
        toast(e.message || "Web-Profil-Export fehlgeschlagen.", "error");
      }
    });
  }

  const webProfileImport = $("#webprofile-import");
  if (webProfileImport) {
    webProfileImport.addEventListener("click", async () => {
      const fi = $("#webprofile-import-file");
      const f = fi && fi.files && fi.files[0];
      if (!f) { toast("Bitte zuerst eine Web-Profildatei wählen.", "error"); return; }
      try {
        const textRaw = await f.text();
        const parsed = JSON.parse(textRaw);
        const payload = parsed && parsed.profile ? parsed.profile : parsed;
        if (!parsed || parsed.format !== WEB_PROFILE_FORMAT || !payload) {
          throw new Error("Dateiformat nicht unterstützt.");
        }

        const backend = payload.backend && typeof payload.backend === "object"
          ? payload.backend
          : {};
        const browser = payload.browser && typeof payload.browser === "object"
          ? payload.browser
          : {};

        const patch = {};
        if (typeof backend.mode === "string") patch.mode = backend.mode;
        if (typeof backend.profile === "string") patch.profile = backend.profile;
        if (backend.target_temp_c !== undefined)
          patch.target_temp_c = clampInt(backend.target_temp_c, 60, 91, 66);
        if (backend.fan_threshold_c !== undefined)
          patch.fan_threshold_c = clampInt(backend.fan_threshold_c, 45, 80, 65);
        if (backend.fan_reapply_sec !== undefined)
          patch.fan_reapply_sec = clampInt(backend.fan_reapply_sec, 1, 300, 15);
        if (backend.warning_cpu_c !== undefined)
          patch.warning_cpu_c = clampInt(backend.warning_cpu_c, 40, 110, 80);
        if (backend.safety_temp_c !== undefined)
          patch.safety_temp_c = clampInt(backend.safety_temp_c, 72, 95, 78);
        if (backend.probe_mask !== undefined)
          patch.probe_mask = clampInt(backend.probe_mask, 0, 63, 0);
        if (Array.isArray(backend.curve))
          patch.curve = normalizeCurvePoints(backend.curve);

        if (Object.keys(patch).length) {
          await saveConfig(patch, null);
        } else {
          await loadConfig();
        }

        applyWebProfileBrowser(browser);
        await refresh();
        await loadSystem();
        await loadRules();
        toast("Web-Profil importiert.");
      } catch (e) {
        toast(e.message || "Web-Profil-Import fehlgeschlagen.", "error");
      }
    });
  }

  /* Zwei Schalter, ein Zustand: oben in der Kopfzeile (neu, 19.09.2026) und
     der bisherige auf der System-Seite. Die Klasse am <body> blendet den
     Expertenbereich ein (siehe .expert-only in style.css). */
  const expertSwitches = ["#expert-mode", "#expert-mode-top"].map((sel) => $(sel)).filter(Boolean);
  const applyExpertMode = (on) => {
    state.expertMode = !!on;
    document.body.classList.toggle("expert", state.expertMode);
    expertSwitches.forEach((el) => { el.checked = state.expertMode; });
  };
  applyExpertMode(loadExpertMode());
  expertSwitches.forEach((el) => el.addEventListener("change", async () => {
    applyExpertMode(el.checked);
    saveExpertMode(state.expertMode);
    if (state.page === "cooling") renderCharts();
    if (state.page === "system") await loadSystem();
  }));

  /* ── Vollbild (Wunsch 19.09.2026) ──────────────────────────────────
     Die Kachel öffnet den Browser der PS5; dessen Leiste und Rahmen kann
     eine Seite nicht selbst entfernen. Einziger Weg von innen ist die
     Vollbild-Schnittstelle. Ob die Konsole sie zulässt, ist noch nicht
     getestet: Der Knopf erscheint nur, wenn der Browser sie anbietet, und
     verschwindet wieder, sobald er sie ablehnt.
     „Direkt im Vollbild starten": Browser erlauben Vollbild nur als Folge
     eines Klicks, nie beim Laden. Deshalb schaltet der ERSTE Druck
     irgendwo auf der Seite um; die gedrückte Schaltfläche tut trotzdem ihre
     Arbeit (nur lauschen, nichts abfangen). Wer „Vollbild beenden" drückt,
     schaltet das Automatische für die nächsten Starts ab. */
  const fsAutoWanted = () => {
    try { return localStorage.getItem(FULLSCREEN_KEY) !== "0"; } catch { return true; }
  };
  const fsAutoSave = (on) => {
    try { localStorage.setItem(FULLSCREEN_KEY, on ? "1" : "0"); } catch {}
  };
  /* A refusal is remembered for a week, not for good: it can have a passing
     cause, and a firmware update can change the answer. Only the explicit
     "Vollbild beenden" above is a permanent choice. */
  const FS_REFUSED_KEY = "ps5tm.fullscreenRefusedUntil";
  const fsRefusedRecently = () => {
    try { return Number(localStorage.getItem(FS_REFUSED_KEY)) > Date.now(); } catch { return false; }
  };
  const fsRememberRefusal = () => {
    try { localStorage.setItem(FS_REFUSED_KEY, String(Date.now() + 7 * 86400000)); } catch {}
  };
  const fsBtn = $("#fs-btn");
  if (fsBtn) {
    const root = document.documentElement;
    const fsRequest = root.requestFullscreen || root.webkitRequestFullscreen || root.webkitRequestFullScreen;
    const fsExit = document.exitFullscreen || document.webkitExitFullscreen || document.webkitCancelFullScreen;
    const fsOn = () => !!(document.fullscreenElement || document.webkitFullscreenElement
      || document.webkitCurrentFullScreenElement);
    const allowed = document.fullscreenEnabled ?? document.webkitFullscreenEnabled ?? true;
    let refused = fsRefusedRecently();
    const refuse = () => {
      if (refused) return;           /* Promise und Fehlerereignis melden beide */
      refused = true;
      fsBtn.hidden = true;
      fsRememberRefusal();           /* sonst käme der Hinweis bei jedem Öffnen */
      toast("Der Browser der Konsole lässt kein Vollbild zu.", "error");
    };
    const sync = () => {
      const on = fsOn();
      txt("#fs-text", on ? "Vollbild beenden" : "Vollbild");
      fsBtn.setAttribute("aria-pressed", on ? "true" : "false");
    };
    const enter = () => {
      if (refused || fsOn()) return;
      try {
        const r = fsRequest.call(root);
        if (r && typeof r.catch === "function") r.catch(refuse);
      } catch (e) { refuse(); }
    };
    if (fsRequest && fsExit && allowed && !refused) {
      fsBtn.hidden = false;
      ["fullscreenchange", "webkitfullscreenchange"].forEach((ev) => document.addEventListener(ev, sync));
      ["fullscreenerror", "webkitfullscreenerror"].forEach((ev) => document.addEventListener(ev, refuse));

      /* Erster Druck irgendwo → Vollbild. Der Knopf selbst ist ausgenommen,
         sonst gingen zwei Anfragen hinaus (Einfang-Phase läuft vor ihm).
         Only a real click counts, no key press: Shift, Ctrl or Tab are no
         dependable permission in every browser, and a refusal of such a
         request used to switch the feature off for good. */
      const stopAuto = () => document.removeEventListener("click", autoClick, true);
      const autoClick = (e) => {
        if (!e.isTrusted || fsBtn.contains(e.target)) return;
        stopAuto();
        enter();
      };
      if (fsAutoWanted()) document.addEventListener("click", autoClick, true);

      fsBtn.addEventListener("click", () => {
        stopAuto();
        if (fsOn()) {
          fsAutoSave(false);
          try { fsExit.call(document); } catch (err) { /* bleibt im Vollbild, Knopf bleibt */ }
          return;
        }
        fsAutoSave(true);
        enter();
      });
    }
  }

  const chanrawConfirmedOnly = $("#chanraw-confirmed-only");
  state.chanrawConfirmedOnly = loadChanrawConfirmedOnly();
  if (chanrawConfirmedOnly) {
    chanrawConfirmedOnly.checked = state.chanrawConfirmedOnly;
    chanrawConfirmedOnly.addEventListener("change", async () => {
      state.chanrawConfirmedOnly = !!chanrawConfirmedOnly.checked;
      saveChanrawConfirmedOnly(state.chanrawConfirmedOnly);
      if (state.page === "system") await loadSystem();
    });
  }

  const logTailToggle = $("#log-tail-toggle");
  if (logTailToggle) {
    logTailToggle.addEventListener("click", () => {
      state.logTailOn = !state.logTailOn;
      logTailToggle.textContent = state.logTailOn
        ? "Live-Protokoll stoppen"
        : "Live-Protokoll starten";
      syncLogTail();
    });
  }

  const riskyRefresh = $("#risky-refresh");
  if (riskyRefresh)
    riskyRefresh.addEventListener("click", () => {
      loadRiskyTelemetry().catch((e) => toast(e.message, "error"));
    });

  const snapshotExport = $("#snapshot-export");
  if (snapshotExport) {
    snapshotExport.addEventListener("click", async () => {
      snapshotExport.disabled = true;
      const oldLabel = snapshotExport.textContent;
      snapshotExport.textContent = "Snapshot wird gesammelt …";
      try {
        const out = await buildSnapshotObject();

        const json = `${JSON.stringify(out, null, 2)}\n`;
        const blob = new Blob([json], { type: "application/json" });
        const url = URL.createObjectURL(blob);
        const stamp = new Date().toISOString().replace(/[:]/g, "-").replace(/\..+$/, "");
        Object.assign(document.createElement("a"), {
          href: url,
          download: `ps5tm-diagnose-snapshot-${stamp}.json`
        }).click();
        URL.revokeObjectURL(url);
        toast("Diagnose-Snapshot exportiert.");
      } catch (e) {
        toast(e.message || "Snapshot-Export fehlgeschlagen.", "error");
      } finally {
        snapshotExport.disabled = false;
        snapshotExport.textContent = oldLabel || "Diagnose-Snapshot exportieren";
      }
    });
  }

  const snapshotCompareFile = $("#snapshot-compare-file");
  const snapshotCompareRun = $("#snapshot-compare-run");
  const snapshotCompareClear = $("#snapshot-compare-clear");
  if (snapshotCompareRun) {
    snapshotCompareRun.addEventListener("click", async () => {
      try {
        const f = snapshotCompareFile && snapshotCompareFile.files && snapshotCompareFile.files[0];
        if (!f) {
          toast("Bitte zuerst eine Referenzdatei wählen.", "error");
          return;
        }
        const text = await f.text();
        const refSnap = JSON.parse(text);
        const curSnap = await buildSnapshotObject();
        renderSnapshotCompare(refSnap, curSnap);
        toast("Snapshot-Vergleich erstellt.");
      } catch (e) {
        toast(`Snapshot-Vergleich fehlgeschlagen: ${e.message || e}`, "error");
      }
    });
  }
  if (snapshotCompareClear) {
    snapshotCompareClear.addEventListener("click", () => {
      const host = $("#snapshot-compare-result");
      if (host) host.innerHTML = `<p class="muted">Kein Snapshot-Vergleich aktiv.</p>`;
    });
  }

  const riskyAutoToggle = $("#risky-auto-toggle");
  const riskyAutoInterval = $("#risky-auto-interval");
  state.riskyAutoOn = loadRiskyAutoOn();
  state.riskyAutoIntervalSec = loadRiskyAutoIntervalSec();
  if (riskyAutoInterval) riskyAutoInterval.value = String(state.riskyAutoIntervalSec);
  if (riskyAutoToggle) riskyAutoToggle.checked = state.riskyAutoOn;
  if (riskyAutoInterval) {
    riskyAutoInterval.addEventListener("change", () => {
      const v = Number(riskyAutoInterval.value);
      state.riskyAutoIntervalSec = [10, 15, 30, 60].includes(v) ? v : 15;
      saveRiskyAutoIntervalSec(state.riskyAutoIntervalSec);
      updateRiskyAutoState();
      restartRiskyAutoTimer();
    });
  }
  if (riskyAutoToggle) {
    riskyAutoToggle.addEventListener("change", () => {
      state.riskyAutoOn = !!riskyAutoToggle.checked;
      saveRiskyAutoOn(state.riskyAutoOn);
      updateRiskyAutoState();
      restartRiskyAutoTimer();
      if (state.riskyAutoOn && state.page === "system") {
        loadRiskyTelemetry({ silent: true }).catch(() => {});
      }
    });
  }
  restartRiskyAutoTimer();

  const monEnable = $("#mon-alert-enable");
  const monWarn = $("#mon-alert-warn");
  const monHot = $("#mon-alert-hot");
  const monFanMin = $("#mon-alert-fanmin");
  const monSave = $("#mon-alert-save");
  const monReset = $("#mon-alert-reset");
  const monPresetQuiet = $("#mon-alert-preset-quiet");
  const monPresetStandard = $("#mon-alert-preset-standard");
  const monPresetStrict = $("#mon-alert-preset-strict");
  const powerHistClear = $("#power-hist-clear");

  state.monitorAlertEnabled = loadMonitorBool(MON_ALERT_ENABLED_KEY, false);
  state.monitorWarnC = loadMonitorNum(MON_ALERT_WARN_C_KEY, 74, 55, 95);
  state.monitorHotC = loadMonitorNum(MON_ALERT_HOT_C_KEY, 79, 56, 98);
  state.monitorFanMinPct = loadMonitorNum(MON_ALERT_FAN_MIN_KEY, 28, 15, 95);

  if (monEnable) monEnable.checked = state.monitorAlertEnabled;
  if (monWarn) monWarn.value = String(state.monitorWarnC);
  if (monHot) monHot.value = String(state.monitorHotC);
  if (monFanMin) monFanMin.value = String(state.monitorFanMinPct);

  if (monEnable) {
    monEnable.addEventListener("change", () => {
      state.monitorAlertEnabled = !!monEnable.checked;
      saveMonitorBool(MON_ALERT_ENABLED_KEY, state.monitorAlertEnabled);
      state.monitorAlertLevel = null;
      renderMonitorAlert();
    });
  }

  if (monSave) {
    monSave.addEventListener("click", () => {
      const warnC = Math.max(55, Math.min(95, Math.round(Number(monWarn && monWarn.value) || 74)));
      const hotCBase = Math.max(56, Math.min(98, Math.round(Number(monHot && monHot.value) || 79)));
      const fanMin = Math.max(15, Math.min(95, Math.round(Number(monFanMin && monFanMin.value) || 28)));
      const hotC = Math.max(warnC + 1, hotCBase);

      state.monitorWarnC = warnC;
      state.monitorHotC = hotC;
      state.monitorFanMinPct = fanMin;

      if (monWarn) monWarn.value = String(state.monitorWarnC);
      if (monHot) monHot.value = String(state.monitorHotC);
      if (monFanMin) monFanMin.value = String(state.monitorFanMinPct);

      saveMonitorNum(MON_ALERT_WARN_C_KEY, state.monitorWarnC);
      saveMonitorNum(MON_ALERT_HOT_C_KEY, state.monitorHotC);
      saveMonitorNum(MON_ALERT_FAN_MIN_KEY, state.monitorFanMinPct);
      toast("Monitoring-Grenzwerte gespeichert.");
      state.monitorAlertLevel = null;
      renderMonitorAlert();
    });
  }

  const applyMonPreset = (preset, label) => {
    if (!preset) return;
    state.monitorWarnC = preset.warn_c;
    state.monitorHotC = Math.max(state.monitorWarnC + 1, preset.hot_c);
    state.monitorFanMinPct = preset.fan_min_pct;
    if (monWarn) monWarn.value = String(state.monitorWarnC);
    if (monHot) monHot.value = String(state.monitorHotC);
    if (monFanMin) monFanMin.value = String(state.monitorFanMinPct);
    saveMonitorNum(MON_ALERT_WARN_C_KEY, state.monitorWarnC);
    saveMonitorNum(MON_ALERT_HOT_C_KEY, state.monitorHotC);
    saveMonitorNum(MON_ALERT_FAN_MIN_KEY, state.monitorFanMinPct);
    state.monitorAlertLevel = null;
    renderMonitorAlert();
    toast(`Monitoring-Profil ${label} aktiviert.`);
  };

  if (monPresetQuiet)
    monPresetQuiet.addEventListener("click", () => applyMonPreset(MON_ALERT_PRESETS.quiet, "Leise"));
  if (monPresetStandard)
    monPresetStandard.addEventListener("click", () => applyMonPreset(MON_ALERT_PRESETS.standard, "Standard"));
  if (monPresetStrict)
    monPresetStrict.addEventListener("click", () => applyMonPreset(MON_ALERT_PRESETS.strict, "Streng"));

  if (monReset) {
    monReset.addEventListener("click", () => {
      state.monitorAlertEvents.length = 0;
      state.monitorAlertLevel = null;
      renderMonitorAlert();
      toast("Monitoring-Verlauf zurückgesetzt.");
    });
  }

  if (powerHistClear) {
    powerHistClear.addEventListener("click", () => {
      state.powerHist.length = 0;
      renderPowerHistory();
      toast("Power-Telemetrie-Verlauf zurückgesetzt.");
    });
  }

  renderMonitorAlert();
  renderPowerHistory();

  wireTooltip("#host-temp", "#cross-temp", "#tip-temp", (r) => `
    <b>${new Date(r.t).toLocaleTimeString(LOCALE)}</b>
    <u><i style="background:var(--s1)"></i>CPU ${r.cpu ?? "--"} °C</u>
    <u><i style="background:var(--s2)"></i>Hauptchip ${r.soc ?? "--"} °C</u>${r.gpu != null ? `
    <u><i style="background:var(--s4)"></i>Grafik ${r.gpu} °C</u>` : ""}`);

  wireTooltip("#host-fan", "#cross-fan", "#tip-fan", (r) => `
    <b>${new Date(r.t).toLocaleTimeString(LOCALE)}</b>
    <u><i style="background:var(--s3)"></i>Lüfter ${r.fan ?? "--"} %</u>`);

  /* ── Start ──────────────────────────────────────────────────────── */

  refresh();
  loadConfigUntilOk();
  loadSystem();
  loadHistory();
  loadRules();

  /* Verkettet statt starr im Takt.
   *
   * `setInterval` startet jede Sekunde einen neuen Durchgang, ganz gleich ob
   * der vorige fertig ist. Hängt die Verbindung, stapeln sich die offenen
   * Abfragen, und weil jede für sich noch wartet, wird der Fehlerzweig nie
   * erreicht — die Ampel blieb grün. Ein Durchgang zur Zeit: dauert einer
   * länger als eine Sekunde, wird der nächste Takt einfach ausgelassen. */
  const pollBusy = { status: false, system: false };
  const pollStatus = () => {
    if (pollBusy.status) return;
    pollBusy.status = true;
    refresh().finally(() => { pollBusy.status = false; });
  };
  const pollSystem = () => {
    if (pollBusy.system) return;
    pollBusy.system = true;
    loadSystem().finally(() => { pollBusy.system = false; });
  };

  /* A page that is not in front needs no live data. The beat keeps running
     but idles while document.hidden and takes up again by itself, so a browser
     that never sends visibilitychange cannot leave it stopped. */
  setInterval(() => { if (!document.hidden) pollStatus(); }, 1000);
  setInterval(() => { if (!document.hidden) pollSystem(); }, 10000);
  setInterval(() => { if (!document.hidden && state.page === "log" && kl.tab === "app") loadLog(); }, 5000);
  /* A minute is the sampling period, so anything faster would redraw the
     same picture. */
  setInterval(() => { if (!document.hidden && state.page === "cooling") loadHistory(); }, 60000);
  setInterval(() => { if (!document.hidden && state.page === "cooling") loadRules(); }, 15000);
  /* Payloads: was läuft, ändert sich nach jedem Start; die Dateilisten nur,
     wenn jemand einen Stick steckt oder per FTP kopiert. Neu gezeichnet wird
     nur bei einer Änderung (plLast), damit ein halb bestätigtes „Löschen"
     nicht mitten im Klick verschwindet. */
  setInterval(() => { if (!document.hidden && state.page === "payloads") loadPayloads(); }, 8000);
  setInterval(() => { if (!document.hidden && state.page === "payloads") loadPayloadFiles(); }, 15000);
  /* Spielzeit und „zuletzt gespielt" ändern sich erst nach einer Sitzung. */
  setInterval(() => { if (!document.hidden && state.page === "games") loadGames(); }, 60000);

  /* Back in front: look at once instead of waiting for the next beat. The
     two-minute charts run by sample, not by time, so after a longer pause they
     would join two unrelated moments; they start over. */
  let hiddenSince = 0;
  document.addEventListener("visibilitychange", () => {
    if (document.hidden) {
      hiddenSince = Date.now();
    } else {
      if (hiddenSince && Date.now() - hiddenSince > 15000) {
        state.hist.length = 0;
        state.fanDiagHist.length = 0;
      }
      hiddenSince = 0;
      pollStatus();
      pollSystem();
      loadConfigUntilOk();
    }
    syncLogTail();
    syncKlog();
    klRecSync();
    syncPlaytime();
    svSync();
    pkSync();
  });
})();
