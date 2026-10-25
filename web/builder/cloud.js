// SPDX-License-Identifier: GPL-3.0-only
// The builder's cloud build (docs/WEB.md): what it builds from (main, a tag or a branch: the relay's ?refs), the request
// (the relay with a Turnstile check: never the GitHub issue form, the user 2026-10-25), then the build followed to its end (the relay's
// ?status=, which reads the workflow's markers) and handed to Flash and the Simulator. builder.js mounts it on its own
// state and elements: mountCloud({ S, $, el, toast, opts }).
//
// The host hears every stage: opts.onBuild(ev) and, in the Mirror app's web view, its "optimist" message handler, with
// ev = {type: "cloud-build", stage, issue, url, id?, run?, name?, sim?, reason?}; stage sent, building, done, refused,
// failed or error. The two buttons of a finished build call opts.onNavigate("install" | "sim", {build: id}).
import { CLOUD_RELAY, TURNSTILE_JS, TURNSTILE_SITEKEY, relayRequest } from "./logic.js";

const WATCH_KEY = "optimist-builder-watch";   // the build followed, {issue, url, name, t0, id?, stage?}: a reload goes on
const MY_BUILD = "optimist-my-build";        // the last build finished, {id, name, issue, t}: Flash offers it first
const FOLLOW_EVERY = 20000, FOLLOW_FOR = 90 * 60000;   // (the relay caches 15 s; a build waits and runs 40 min at most)
const SHELF_EVERY = 15000, SHELF_FOR = 10 * 60000;     // (the build reaches the cloud-builds branch just after "done")
const FAILED = ["refused", "failed", "error"];

const store = {
  get(k) { try { return JSON.parse(localStorage.getItem(k) || "null"); } catch (e) { return null; } },
  set(k, v) { try { if (v) localStorage.setItem(k, JSON.stringify(v)); else localStorage.removeItem(k); } catch (e) { /* no storage */ } },
};

export function mountCloud({ S, $, el, toast, opts }) {
  let timer = 0, turnstileJs = null;
  const link = (href, text) => el("a", { href, target: "_blank", rel: "noopener", text });

  function emit(ev) {
    const m = { type: "cloud-build", ...ev };
    if (opts.onBuild) opts.onBuild(m);
    const app = typeof window !== "undefined" && window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.optimist;
    if (app) app.postMessage(m);
  }
  function go(where, id) {
    if (opts.onNavigate) opts.onNavigate(where, { build: id });
    else location.href = where === "sim" ? `../editor/?sim=${id}#sim` : `../installer/?build=${id}`;
  }

  /* ---- what it builds from: main (the default), the tags (the releases), the other branches; the relay lists them */
  /** ref: the version the page reads (null: main, the site's copy); pick(sha | ""): the page again on another one */
  async function loadRefs(ref = null, pick = null) {
    const sel = $("bref");
    const mark = () => {      // (the version shown; one the relay no longer lists: by its commit)
      if (ref && ![...sel.options].some((o) => o.value === ref)) sel.append(el("option", { value: ref, text: `commit ${ref.slice(0, 8)}` }));
      sel.value = ref || "";
    };
    sel.replaceChildren(el("option", { value: "", text: "main (latest)" }));
    mark();
    if (pick) sel.onchange = () => pick(sel.value);
    if (!TURNSTILE_SITEKEY) return;
    try {
      const r = await fetch(`${CLOUD_RELAY}?refs`, { signal: AbortSignal.timeout(10000) });
      if (!r.ok) return;
      const out = await r.json();
      const ok = (x) => x && typeof x.name === "string" && /^[0-9a-f]{40}$/.test(x.sha || "");
      const branches = (out.branches || []).filter(ok), tags = (out.tags || []).filter(ok);
      const main = branches.find((b) => b.name === "main"), others = branches.filter((b) => b.name !== "main");
      const opt = (x) => el("option", { value: x.sha, text: x.name });
      sel.replaceChildren(el("option", { value: "", text: "main (latest)" }),
        ...(tags.length ? [el("optgroup", { label: "Releases and tags" }, tags.map(opt))] : []),
        ...(others.length ? [el("optgroup", { label: "Branches" }, others.map(opt))] : []));
      mark();
    } catch (e) { /* main only */ }
  }

  /* ---- the cloud-builds branch: a build there is ready for Flash and the Simulator */
  async function shelfBuild(id) {
    const r = await fetch(`https://api.github.com/repos/${S.repo}/branches/cloud-builds`, { signal: AbortSignal.timeout(8000) });
    if (!r.ok) return null;
    const sha = ((await r.json()).commit || {}).sha;
    if (!/^[0-9a-f]{40}$/.test(sha || "")) return null;
    const ix = await fetch(`https://raw.githubusercontent.com/${S.repo}/${sha}/index.json`, { signal: AbortSignal.timeout(8000) });
    if (!ix.ok) return null;
    const b = ((await ix.json()).builds || []).find((x) => x && x.id === id);
    return b ? { id, name: String(b.name || ""), sim: typeof b.sim === "string" && b.sim.endsWith(".wasm") } : null;
  }

  /* ---- the progress, in the Build card */
  const STEPS = [["sent", "Sent"], ["building", "Building"], ["ready", "Ready"]];
  function show(w, b) {
    const box = $("buildwatch");
    box.hidden = !w;
    if (!w) { box.replaceChildren(); return; }
    const at = w.stage === "ready" ? 2 : w.stage === "building" || w.stage === "done" ? 1 : 0, bad = FAILED.includes(w.stage);
    const steps = el("ol", { class: "steps" }, STEPS.map(([, t], i) => el("li", { "data-st": bad && i === at ? "fail" : i < at || w.stage === "ready" ? "done" : i === at ? "on" : "wait", text: t })));
    const gh = w.url ? link(w.url, "on GitHub") : null, log = w.run ? link(w.run, "the log") : null;
    const say = {
      queued: ["Sent: GitHub starts it in a moment. ", gh],
      building: ["Building: a few minutes. ", log || gh],
      done: ["Built: getting it ready for Flash… ", gh],
      ready: [el("b", { text: `${(b && b.name) || w.name || "Your build"} is ready. ` }),
        el("button", { type: "button", class: "primary", text: "Flash it", onclick: () => go("install", w.id) }), " ",
        b && b.sim ? el("button", { type: "button", text: "Try it in the simulator", onclick: () => go("sim", w.id) }) : el("span", { class: "o-dim", text: "(no simulator for this build) " }),
        " ", gh],
      refused: ["Refused: ", gh, el("pre", { class: "why", text: w.reason || "" })],
      failed: ["The build failed: ", log || gh, el("pre", { class: "why", text: w.reason || "" })],
      error: ["The build could not run (not your configuration): ", log || gh],
      lost: ["This build is no longer followed here: ", gh],
    }[w.stage || "queued"] || [];
    const end = w.stage === "ready" || bad || w.stage === "lost";
    box.replaceChildren(steps, el("p", {}, ...say.filter(Boolean)),
      el("button", { type: "button", class: "quiet sm", text: end ? "Dismiss" : "Stop following", onclick: () => { stop(); store.set(WATCH_KEY, null); show(null); } }));
  }

  function stop() { clearTimeout(timer); timer = 0; }

  /** follow a request the relay sent (w.issue): the stages, then the build onto the cloud-builds branch */
  function follow(w) {
    stop();
    store.set(WATCH_KEY, w);
    show(w);
    if (w.stage === "done") { waitShelf(w); return; }
    const tick = async () => {
      if (Date.now() - w.t0 > FOLLOW_FOR) { w.stage = "lost"; store.set(WATCH_KEY, null); show(w); return; }
      let st = null;
      try {
        const r = await fetch(`${CLOUD_RELAY}?status=${w.issue}`, { signal: AbortSignal.timeout(20000) });
        if (r.status === 404) { w.stage = "lost"; store.set(WATCH_KEY, null); show(w); return; }
        if (r.ok) st = await r.json();
      } catch (e) { st = null; }   // (offline a moment: the next check)
      if (st && st.stage && st.stage !== (w.stage || "queued")) {
        Object.assign(w, { stage: st.stage, id: /^[0-9a-f]{16}$/.test(st.id || "") ? st.id : w.id, run: st.run || w.run, reason: String(st.reason || "") });
        store.set(WATCH_KEY, w);
        if (w.stage === "building") emit({ stage: "building", issue: w.issue, url: w.url, id: w.id, run: w.run });
        if (FAILED.includes(w.stage)) {
          emit({ stage: w.stage, issue: w.issue, url: w.url, reason: w.reason });
          store.set(WATCH_KEY, null);
          show(w);
          return;
        }
        if (w.stage === "done") { waitShelf(w); return; }
      }
      show(w);
      timer = setTimeout(tick, FOLLOW_EVERY);
    };
    tick();
  }

  /** a build done: its files reach the cloud-builds branch a moment later; then it is ready */
  function waitShelf(w) {
    show(w);
    const t1 = Date.now();
    const tick = async () => {
      let b = null;
      if (w.id) { try { b = await shelfBuild(w.id); } catch (e) { b = null; } }
      if (b) { ready(w, b); return; }
      if (Date.now() - t1 > SHELF_FOR) { w.stage = "lost"; store.set(WATCH_KEY, null); show(w); return; }
      timer = setTimeout(tick, SHELF_EVERY);
    };
    tick();
  }

  function ready(w, b) {
    w.stage = "ready";
    store.set(WATCH_KEY, null);
    store.set(MY_BUILD, { id: w.id, name: b.name || w.name || "", issue: w.issue || null, t: Date.now() });
    emit({ stage: "done", issue: w.issue || null, url: w.url || "", id: w.id, name: b.name || w.name || "", sim: !!b.sim });
    show(w, b);
    toast(`${b.name || w.name || "Your build"} is ready: flash it, or try it in the simulator`, "ok");
  }

  /* ---- the request */
  function loadTurnstile() {
    if (window.turnstile) return Promise.resolve(window.turnstile);
    if (!turnstileJs) turnstileJs = new Promise((ok, no) => {   /* (loaded at the first build: no third party before) */
      const sc = el("script", { src: TURNSTILE_JS, async: true });
      sc.onload = () => (window.turnstile ? ok(window.turnstile) : no(new Error("no turnstile")));
      sc.onerror = () => { turnstileJs = null; no(new Error("the check did not load")); };
      document.head.append(sc);
    });
    return turnstileJs;
  }
  /** a Turnstile token (one a request): the check shows itself only when Cloudflare wants a click */
  async function botToken() {
    const ts = await loadTurnstile();
    const box = $("botcheck");
    box.hidden = false;
    box.replaceChildren();
    try {
      return await new Promise((ok, no) => {
        ts.render(box, { sitekey: TURNSTILE_SITEKEY, action: "cloud-build", appearance: "interaction-only",
          callback: ok, "error-callback": () => no(new Error("the check failed")), "timeout-callback": () => no(new Error("the check timed out")) });
        setTimeout(() => no(new Error("the check timed out")), 120000);
      });
    } finally { box.hidden = true; box.replaceChildren(); }
  }

  /** the request through the relay -> "sent" (followed from now on), "refused" (the relay said why) or "failed" (no check,
      no answer): said in the Build card, nothing opened elsewhere */
  async function relaySend(msg, commit) {
    msg.textContent = "Checking you are not a robot…";
    let token;
    try { token = await botToken(); }
    catch (e) { msg.textContent = `Not sent: ${e.message}. Try again in a moment.`; return "failed"; }
    msg.textContent = "Sending…";
    let r = null, out = {};
    try {
      r = await fetch(CLOUD_RELAY, { method: "POST", headers: { "Content-Type": "application/json" }, signal: AbortSignal.timeout(30000),
        body: JSON.stringify(relayRequest(token, commit, S.view.dump, S.name)) });
      out = await r.json().catch(() => ({}));
    } catch (e) { r = null; }
    const url = /^https:\/\/github\.com\//.test(out.issue || "") ? out.issue : "";
    const why = String(out.error || "").slice(0, 160);
    if (r && (r.status === 201 || r.status === 200) && url && Number.isInteger(out.number)) {
      msg.textContent = "";
      emit({ stage: "sent", issue: out.number, url });
      follow({ issue: out.number, url, name: S.name, t0: Date.now() });
      return "sent";
    }
    if (r && r.status >= 400 && r.status < 500 && why) {
      msg.replaceChildren(`Not sent: ${why}`, ...(url ? [" (", link(url, "your build on GitHub"), ")"] : []));
      return "refused";
    }
    msg.textContent = `Not sent: the build service did not answer${why ? ` (${why})` : ""}. Try again in a moment.`;
    return "failed";
  }

  /** Build: the same configuration from the site's own commit built before is ready at once; else the relay (the build
      service: no GitHub account, no issue form) */
  async function build(ev) {
    const msg = $("buildmsg"), commit = $("bref").value || "";
    if (ev.key && commit && commit === S.commit) {
      msg.textContent = "Looking for a build of this configuration…";
      let b = null;
      try { b = await shelfBuild(ev.key.slice(0, 16)); } catch (e) { b = null; }
      if (b) { msg.textContent = "Built before."; ready({ id: b.id, name: S.name, t0: Date.now() }, b); return; }
      msg.textContent = "";
    }
    if (!TURNSTILE_SITEKEY) { msg.textContent = "The build service is not set up on this page (TURNSTILE_SITEKEY)."; return; }
    await relaySend(msg, commit);
  }

  /** a build followed before a reload: followed again */
  function resume() {
    const w = store.get(WATCH_KEY);
    if (w && Number.isInteger(w.issue) && typeof w.t0 === "number" && Date.now() - w.t0 < FOLLOW_FOR) follow(w);
    else store.set(WATCH_KEY, null);
  }

  return { build, loadRefs, resume, stop };
}
