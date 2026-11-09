/*
 * kayte-web.js – browser runtime for Kayte .kjs programs (KayteIDE)
 *
 * Kayte (github.com/ringsce/kayte-lang) runs .kjs programs on the desktop
 * through libkayte_qt6, driven by qt("…") calls. This file gives the same
 * qt("…") API to a web page, built on the DOM, so a .kjs program written
 * for a Kayte form also runs in a browser:
 *
 *   qt("init");
 *   const win = qt("loadform", "Login.kfm");      // forms are embedded in the page
 *   const ok  = qt("find", win, "OkBtn");
 *   qt("show", win);
 *   qt("run");                                      // returns – the browser runs the event loop
 *   function OkBtn_Click() { qt("settext", ok, "Thanks"); }
 *
 * As in libkayte_qt6, a function named <Control>_Click answers a control's
 * click / selection change, and qt("on", handle, "FunctionName") binds one
 * explicitly. Forms come from <script type="application/json"
 * class="kayte-form"> blocks that KayteIDE writes into the page.
 *
 * What a browser cannot do: blocking calls (qt("wait") / qt("poll")),
 * process(), qml() and native file dialogs – they raise a clear error.
 */
(function (global) {
  "use strict";

  // ── Handles ───────────────────────────────────────────────────────────────
  // Every widget, layout, menu, action and timer is an integer handle, as in
  // libkayte_qt6; 0 is "no parent" / failure.
  const objects = [null];
  function register(obj) {
    objects.push(obj);
    obj.id = objects.length - 1;
    return obj.id;
  }
  function fail(cmd, msg) { throw new Error('qt("' + cmd + '") - ' + msg); }
  function obj(cmd, id, what) {
    const o = objects[Number(id)];
    if (!o) fail(cmd, "invalid handle " + id + (what ? " (expected " + what + ")" : ""));
    return o;
  }
  function str(v) { return v === undefined || v === null ? "" : String(v); }
  function int(v) { const n = Number(v); return Number.isFinite(n) ? Math.trunc(n) : 0; }

  // ── Output: print / alert / msgbox ───────────────────────────────────────
  function consoleEl() { return document.getElementById("kayte-console"); }
  function kaytePrint() {
    const line = Array.prototype.map.call(arguments, str).join(" ");
    console.log(line);
    const c = consoleEl();
    if (c) { c.hidden = false; c.textContent += line + "\n"; c.scrollTop = c.scrollHeight; }
  }

  // ── Events ────────────────────────────────────────────────────────────────
  // A handler is looked up by name when the event happens, so functions may
  // be declared anywhere in the program (as with Kayte's SUBs).
  function callHandler(name, explicit) {
    const fn = global[name];
    if (typeof fn === "function") {
      try { fn(); } catch (e) { reportError(e); }
    } else if (explicit) {
      reportError(new Error('handler "' + name + '" is not a function in this program'));
    }
  }
  function fire(o) {
    if (o.blockEvents) return;
    if (o.handler) callHandler(o.handler, true);
    else if (o.name) callHandler(o.name + "_Click", false);
  }
  function reportError(e) {
    console.error(e);
    kaytePrint("Error: " + (e && e.message ? e.message : e));
  }

  // ── DOM helpers ───────────────────────────────────────────────────────────
  function el(tag, cls, parent) {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    if (parent) parent.appendChild(e);
    return e;
  }
  // Where children of a container go (a window's client area, a group's body…).
  function body(o) { return o.body || o.el; }
  function containerFor(cmd, parentId) {
    const p = obj(cmd, parentId, "a window, group, panel or tab");
    if (!p.isContainer) fail(cmd, "the parent must be a window, group, panel or tab");
    return p;
  }
  function place(o, x, y, w, h) {
    const s = o.el.style;
    if (x !== undefined) s.left = int(x) + "px";
    if (y !== undefined) s.top = int(y) + "px";
    if (w > 0) s.width = int(w) + "px";
    if (h > 0) s.height = int(h) + "px";
  }
  function newWidget(kind, tag, cls, parent, extra) {
    const o = Object.assign({ kind: kind, el: el(tag, "kw " + cls), name: "", win: parent ? parent.win : null }, extra || {});
    if (parent) body(parent).appendChild(o.el);
    register(o);
    return o;
  }

  // ── Windows ───────────────────────────────────────────────────────────────
  let zTop = 10;
  function makeWindow(title, width, height, name) {
    const host = document.getElementById("kayte-app") || document.body;
    const o = { kind: "window", isContainer: true, name: name || "", el: el("div", "kw-window", host) };
    o.win = o;
    o.el.hidden = true;
    o.titleBar = el("div", "kw-titlebar", o.el);
    o.titleText = el("span", "kw-title", o.titleBar);
    o.titleText.textContent = str(title);
    o.closeBtn = el("button", "kw-close", o.titleBar);
    o.closeBtn.type = "button";
    o.closeBtn.setAttribute("aria-label", "Close");
    o.closeBtn.textContent = "×";
    o.closeBtn.addEventListener("click", function () { o.el.hidden = true; });
    o.body = el("div", "kw-client", o.el);
    o.body.style.width = int(width || 400) + "px";
    o.body.style.height = int(height || 300) + "px";
    o.el.addEventListener("mousedown", function () { o.el.style.zIndex = ++zTop; });
    register(o);
    return o;
  }

  // ── Building widgets (shared by code-built GUIs and forms) ────────────────
  function makeButton(parent, text) {
    const o = newWidget("button", "button", "kw-button", parent);
    o.el.type = "button";
    o.el.textContent = str(text);
    o.el.addEventListener("click", function () { fire(o); });
    return o;
  }
  function makeLabel(parent, text) {
    const o = newWidget("label", "div", "kw-label", parent);
    o.textEl = el("span", "", o.el);
    o.textEl.textContent = str(text);
    o.el.addEventListener("click", function () { if (o.handler) fire(o); });
    return o;
  }
  function makeCheck(parent, text, type, groupName) {
    const o = newWidget(type === "radio" ? "radio" : "checkbox", "label", "kw-check" + (type === "toggle" ? " kw-toggle" : ""), parent);
    o.input = el("input", "", o.el);
    o.input.type = type === "radio" ? "radio" : "checkbox";
    if (type === "radio") o.input.name = groupName || ("kw-radio-" + (parent ? parent.id : 0));
    if (type === "toggle") el("span", "kw-switch", o.el);
    o.textEl = el("span", "kw-check-text", o.el);
    o.textEl.textContent = str(text);
    o.input.addEventListener("change", function () { fire(o); });
    return o;
  }
  function makeEdit(parent, text, multiline) {
    const o = newWidget(multiline ? "textedit" : "edit", multiline ? "textarea" : "input", "kw-edit", parent);
    o.input = o.el;
    if (!multiline) o.el.type = "text";
    o.el.value = str(text);
    if (multiline) o.el.addEventListener("input", function () { if (o.handler) fire(o); });
    else o.el.addEventListener("keydown", function (ev) { if (ev.key === "Enter") fire(o); }); // Kayte: an edit's event is Enter
    return o;
  }
  function makeCombo(parent) {
    const o = newWidget("combo", "select", "kw-combo", parent);
    o.el.addEventListener("change", function () { fire(o); });
    return o;
  }
  function makeList(parent) {
    const o = newWidget("list", "select", "kw-list", parent);
    o.el.size = 4;
    o.el.addEventListener("change", function () { fire(o); });
    return o;
  }
  function makeSpin(parent, min, max) {
    const o = newWidget("spin", "input", "kw-spin", parent);
    o.el.type = "number";
    o.el.min = int(min); o.el.max = int(max); o.el.step = 1; o.el.value = int(min);
    o.el.addEventListener("change", function () { fire(o); });
    return o;
  }
  function makeSlider(parent, min, max, step) {
    const o = newWidget("slider", "input", "kw-slider", parent);
    o.el.type = "range";
    o.el.min = min; o.el.max = max; o.el.step = step || 1; o.el.value = min;
    o.el.addEventListener("input", function () { fire(o); });
    return o;
  }
  function makeProgress(parent) {
    const o = newWidget("progress", "div", "kw-progress", parent);
    o.el.setAttribute("role", "progressbar");
    o.bar = el("div", "kw-progress-bar", o.el);
    o.textEl = el("span", "kw-progress-text", o.el);
    o.min = 0; o.max = 100; o.value = 0; o.format = "%p%"; o.showText = true;
    updateProgress(o);
    return o;
  }
  function updateProgress(o) {
    const span = o.max - o.min;
    const pct = span > 0 ? Math.round(((o.value - o.min) * 100) / span) : 0;
    o.bar.style.width = Math.max(0, Math.min(100, pct)) + "%";
    o.el.setAttribute("aria-valuenow", o.value);
    o.textEl.textContent = o.showText ? o.format.replace(/%p/g, pct).replace(/%v/g, o.value) : "";
  }
  function makeGroup(parent, title) {
    const o = newWidget("group", "div", "kw-group", parent, { isContainer: true });
    o.titleEl = el("span", "kw-group-title", o.el);
    o.titleEl.textContent = str(title);
    o.body = o.el;
    return o;
  }
  function makePanel(parent) {
    return newWidget("panel", "div", "kw-panel", parent, { isContainer: true });
  }
  function makePicture(parent, src, stretch) {
    const o = newWidget("picture", "div", "kw-picture", parent);
    o.img = el("img", "", o.el);
    o.img.alt = "";
    if (src) o.img.src = src; else o.img.hidden = true;
    if (stretch) o.el.classList.add("kw-stretch");
    o.el.addEventListener("click", function () { if (o.handler) fire(o); });
    return o;
  }
  function makeTabs(parent) {
    const o = newWidget("tabs", "div", "kw-tabs", parent);
    o.bar = el("div", "kw-tabbar", o.el);
    o.bar.setAttribute("role", "tablist");
    o.pages = [];
    o.current = -1;
    return o;
  }
  function selectTab(o, index, notify) {
    o.pages.forEach(function (p, i) {
      p.el.hidden = i !== index;
      p.button.setAttribute("aria-selected", i === index ? "true" : "false");
    });
    const changed = o.current !== index;
    o.current = index;
    if (changed && notify) fire(o);
  }

  // ── Menus ─────────────────────────────────────────────────────────────────
  function menuBarOf(win) {
    if (!win.menuBar) {
      win.menuBar = el("div", "kw-menubar");
      win.menuBar.setAttribute("role", "menubar");
      win.body.insertBefore(win.menuBar, win.body.firstChild);
    }
    return win.menuBar;
  }
  let openMenu = null;
  function closeMenus() { if (openMenu) { openMenu.popup.hidden = true; openMenu = null; } }
  document.addEventListener("mousedown", function (ev) {
    if (openMenu && !openMenu.root.contains(ev.target)) closeMenus();
  });
  function makeMenu(parent, title) {
    const o = { kind: "menu", name: "", win: parent.win, items: [] };
    o.root = el("div", "kw-menu");
    o.button = el("button", "kw-menu-title", o.root);
    o.button.type = "button";
    o.button.textContent = mnemonicless(title);
    o.popup = el("div", "kw-menu-popup", o.root);
    o.popup.setAttribute("role", "menu");
    o.popup.hidden = true;
    if (parent.kind === "menu") {
      o.root.classList.add("kw-submenu");
      parent.popup.appendChild(o.root);
      o.root.addEventListener("mouseenter", function () { o.popup.hidden = false; });
      o.root.addEventListener("mouseleave", function () { o.popup.hidden = true; });
    } else {
      menuBarOf(parent.win).appendChild(o.root);
      o.button.addEventListener("click", function () {
        const wasOpen = openMenu === o;
        closeMenus();
        if (!wasOpen) { o.popup.hidden = false; openMenu = o; }
      });
    }
    register(o);
    return o;
  }
  function makeContextMenu(win, title) {
    const o = { kind: "menu", name: "", win: win, items: [], isContext: true };
    o.root = el("div", "kw-menu kw-context", win.body);
    o.popup = el("div", "kw-menu-popup", o.root);
    o.popup.setAttribute("role", "menu");
    o.popup.hidden = true;
    o.title = str(title);
    register(o);
    return o;
  }
  function showContextMenu(menu, ev) {
    ev.preventDefault();
    closeMenus();
    const r = menu.win.body.getBoundingClientRect();
    menu.popup.style.left = (ev.clientX - r.left) + "px";
    menu.popup.style.top = (ev.clientY - r.top) + "px";
    menu.popup.hidden = false;
    openMenu = menu;
  }
  function mnemonicless(t) { return str(t).replace(/&(.)/g, "$1"); }
  function makeAction(menu, text, shortcut, checkable) {
    const o = { kind: "action", name: "", win: menu.win, checkable: !!checkable, checked: false };
    o.el = el("button", "kw-action", menu.popup);
    o.el.type = "button";
    o.el.setAttribute("role", checkable ? "menuitemcheckbox" : "menuitem");
    o.check = el("span", "kw-action-check", o.el);
    o.textEl = el("span", "kw-action-text", o.el);
    o.textEl.textContent = mnemonicless(text);
    if (shortcut) { const k = el("span", "kw-action-key", o.el); k.textContent = shortcut; }
    o.el.addEventListener("click", function () {
      if (o.checkable) setActionChecked(o, !o.checked);
      closeMenus();
      fire(o);
    });
    register(o);
    return o;
  }
  function setActionChecked(o, on) {
    o.checked = !!on;
    o.check.textContent = o.checked ? "✓" : "";
    o.el.setAttribute("aria-checked", o.checked ? "true" : "false");
  }

  // ── Layouts ───────────────────────────────────────────────────────────────
  function makeLayout(kind, parentId) {
    const o = { kind: "layout", layoutKind: kind, el: el("div", "kw-layout kw-" + kind), cursor: 0 };
    if (parentId) {
      const p = containerFor(kind, parentId);
      if (p.layout) fail(kind, "the parent already has a layout");
      p.layout = o;
      o.win = p.win;
      body(p).appendChild(o.el);
      p.el.classList.add("kw-has-layout");
    }
    register(o);
    return o;
  }
  function layoutAdd(cmd, l, item, a, b, c, d) {
    if (l.kind !== "layout") fail(cmd, "the first argument must be a layout");
    item.el.classList.add("kw-in-layout");
    item.el.style.left = item.el.style.top = "";
    if (l.layoutKind === "grid") {
      const rs = c > 0 ? c : 1, cs = d > 0 ? d : 1;
      item.el.style.gridRow = (int(a) + 1) + " / span " + rs;
      item.el.style.gridColumn = (int(b) + 1) + " / span " + cs;
    } else if (l.layoutKind === "form") {
      item.el.style.gridColumn = "1 / span 2";
    } else if (a >= 0) {
      item.el.style.flex = String(int(a));
    }
    l.el.appendChild(item.el);
  }

  // ── Forms (.kfm / InForm, embedded by KayteIDE as JSON) ───────────────────
  const formSources = Object.create(null);
  function scanForms() {
    document.querySelectorAll('script[type="application/json"].kayte-form').forEach(function (s) {
      if (s.dataset.kayteLoaded) return;
      s.dataset.kayteLoaded = "1";
      const form = JSON.parse(s.textContent);
      [form.file, baseName(form.file), stripExt(baseName(form.file)), form.name].forEach(function (k) {
        if (k) formSources[k.toLowerCase()] = form;
      });
    });
  }
  function baseName(p) { return str(p).replace(/^.*[\\/]/, ""); }
  function stripExt(p) { return str(p).replace(/\.[^.]*$/, ""); }

  function applyCommon(o, c) {
    if (c.tooltip) o.el.title = c.tooltip;
    if (c.disabled) setEnabled(o, false);
    if (c.hidden) o.el.hidden = true;
    const s = o.el.style;
    if (c.foreColor) s.color = c.foreColor;
    if (c.backColor && !c.transparent) s.background = c.backColor;
    if (c.fontSize) s.fontSize = c.fontSize + "px";
    if (c.hasBorder === false) o.el.classList.add("kw-noborder");
    if (c.hasBorder === true) o.el.classList.add("kw-border");
    if (c.handlers && c.handlers.onclick) o.handler = c.handlers.onclick;
  }

  function buildForm(form) {
    const win = makeWindow(form.caption || form.name, form.width, form.height, form.name);
    win.formName = form.name;
    win.controls = Object.create(null);
    const byName = win.controls;
    const pending = form.controls.slice();
    // Menus first, then frames, then the rest – parents always exist before children.
    const rank = { MenuBar: 0, ContextMenu: 0, MenuItem: 1, Frame: 2 };
    pending.sort(function (a, b) { return (rank[a.type] ?? 3) - (rank[b.type] ?? 3); });
    const menuItems = [];

    pending.forEach(function (c) {
      const parent = c.parent && byName[c.parent.toLowerCase()];
      const host = parent && parent.isContainer ? parent : win;
      let o = null;
      switch (c.type) {
        case "Button":       o = makeButton(host, c.caption); if (c.default) o.el.classList.add("kw-default"); break;
        case "Label":        o = makeLabel(host, c.caption); break;
        case "TextBox":
          o = makeEdit(host, c.text, c.multiline);
          if (c.caption) o.el.placeholder = c.caption;
          if (c.password) o.el.type = "password";
          if (c.numeric !== "none" && !c.multiline) {
            o.el.inputMode = "decimal";
            o.numeric = c.numeric; o.min = c.min; o.max = c.max;
          }
          break;
        case "CheckBox":     o = makeCheck(host, c.caption, "checkbox"); break;
        case "ToggleSwitch": o = makeCheck(host, c.caption, "toggle"); break;
        case "RadioButton":  o = makeCheck(host, c.caption, "radio", "kw-" + win.id + "-" + (c.parent || "form")); break;
        case "ListBox":      o = makeList(host); break;
        case "DropdownList": o = makeCombo(host); break;
        case "TrackBar": {
          const step = c.minInterval > 0 ? c.minInterval : 1;
          o = makeSlider(host, c.min, c.max, step);
          break;
        }
        case "ProgressBar":
          o = makeProgress(host);
          o.min = c.min; o.max = c.max || 100;
          o.showText = !!(c.showPercentage || c.caption);
          if (c.caption) o.format = c.caption;
          break;
        case "PictureBox":   o = makePicture(host, c.text, c.stretch); break;
        case "Frame":        o = makeGroup(host, c.caption); break;
        case "MenuBar":      o = makeMenu(win, c.caption); break;
        case "ContextMenu":  o = makeContextMenu(win, c.caption); break;
        case "MenuItem":     menuItems.push(c); return;
        default:             return;
      }
      o.name = c.name;
      o.win = win;
      byName[c.name.toLowerCase()] = o;
      if (o.el && c.type !== "MenuBar" && c.type !== "ContextMenu") {
        place(o, c.x, c.y, c.w, c.h);
        if (c.type === "Label" || c.type === "PictureBox") {
          o.el.style.justifyContent = { center: "center", right: "flex-end" }[c.align] || "flex-start";
          o.el.style.alignItems = { middle: "center", bottom: "flex-end" }[c.valign] || "flex-start";
          if (c.wordWrap) o.el.classList.add("kw-wrap");
        }
        applyCommon(o, c);
      }
      if (c.type === "MenuBar" && c.hidden) o.root.hidden = true;
    });

    // Menu items, in file order, so nested items find their parent.
    menuItems.forEach(function (c) {
      const parent = byName[c.parent.toLowerCase()];
      if (!parent || parent.kind !== "menu") return;
      const sepAfter = /-$/.test(c.caption);
      const caption = sepAfter ? c.caption.slice(0, -1) : c.caption;
      let o;
      if (form.controls.some(function (k) { return k.parent && k.parent.toLowerCase() === c.name.toLowerCase(); })) {
        o = makeMenu(parent, caption);
      } else {
        o = makeAction(parent, caption, "", c.hasValue || c.bullet);
        if (c.value) setActionChecked(o, true);
        if (c.disabled) o.el.disabled = true;
        if (c.hidden) o.el.hidden = true;
        if (c.handlers && c.handlers.onclick) o.handler = c.handlers.onclick;
      }
      o.name = c.name;
      byName[c.name.toLowerCase()] = o;
      if (sepAfter) el("hr", "kw-separator", parent.popup);
    });

    // Items, values and context menus once every control exists.
    form.controls.forEach(function (c) {
      const o = byName[c.name.toLowerCase()];
      if (!o) return;
      o.blockEvents = true;
      (c.items || []).forEach(function (item) { addItem(o, item); });
      if (c.type === "ListBox" || c.type === "DropdownList") o.el.selectedIndex = c.value > 0 ? c.value - 1 : -1;
      else if (o.input && (c.type === "CheckBox" || c.type === "ToggleSwitch" || c.type === "RadioButton")) o.input.checked = !!c.value;
      else if (c.type === "TrackBar") o.el.value = c.value;
      else if (c.type === "ProgressBar") { o.value = c.value; updateProgress(o); }
      o.blockEvents = false;
      if (c.contextMenu) {
        const menu = byName[c.contextMenu.toLowerCase()];
        if (menu) o.el.addEventListener("contextmenu", function (ev) { showContextMenu(menu, ev); });
      }
    });
    if (form.contextMenu) {
      const menu = byName[form.contextMenu.toLowerCase()];
      if (menu) win.body.addEventListener("contextmenu", function (ev) { if (ev.target === win.body) showContextMenu(menu, ev); });
    }
    // Enter anywhere on the form presses its default button, as in InForm.
    win.body.addEventListener("keydown", function (ev) {
      if (ev.key !== "Enter" || ev.target.tagName === "TEXTAREA") return;
      const def = win.body.querySelector(".kw-default");
      if (def && !def.disabled && ev.target !== def) { ev.preventDefault(); def.click(); }
    });
    return win;
  }

  // ── Widget state ──────────────────────────────────────────────────────────
  function setText(o, t) {
    t = str(t);
    switch (o.kind) {
      case "window": o.titleText.textContent = t; break;
      case "group": o.titleEl.textContent = t; break;
      case "edit": case "textedit": o.el.value = t; break;
      case "combo": case "list": {
        const i = Array.prototype.findIndex.call(o.el.options, function (op) { return op.text === t; });
        if (i >= 0) o.el.selectedIndex = i;
        break;
      }
      case "progress": o.format = t; o.showText = true; updateProgress(o); break;
      case "picture": o.img.hidden = !t; o.img.src = t; break;
      case "menu": o.button.textContent = mnemonicless(t); break;
      case "spin": case "slider": o.el.value = int(t); break;
      default: if (o.textEl) o.textEl.textContent = t; else if (o.el) o.el.textContent = t;
    }
  }
  function getText(o) {
    switch (o.kind) {
      case "window": return o.titleText.textContent;
      case "group": return o.titleEl.textContent;
      case "edit": case "textedit": case "spin": case "slider": return o.el.value;
      case "combo": case "list": return o.el.selectedIndex >= 0 ? o.el.options[o.el.selectedIndex].text : "";
      case "progress": return o.textEl.textContent;
      case "picture": return o.img.getAttribute("src") || "";
      case "menu": return o.button ? o.button.textContent : o.title;
      default: return o.textEl ? o.textEl.textContent : (o.el ? o.el.textContent : "");
    }
  }
  function getValue(o) {
    switch (o.kind) {
      case "checkbox": case "radio": return o.input.checked ? 1 : 0;
      case "action": return o.checked ? 1 : 0;
      case "combo": case "list": return o.el.selectedIndex;
      case "spin": case "slider": return int(o.el.value);
      case "progress": return o.value;
      case "tabs": return o.current;
      default: return 0;
    }
  }
  function setValue(o, v) {
    // Like libkayte_qt6, setting a value doesn't raise the control's event.
    o.blockEvents = true;
    switch (o.kind) {
      case "checkbox": case "radio": o.input.checked = int(v) !== 0; break;
      case "action": if (!o.checkable) { o.blockEvents = false; return 0; } setActionChecked(o, int(v) !== 0); break;
      case "combo": case "list": o.el.selectedIndex = int(v); break;
      case "spin": case "slider": o.el.value = int(v); break;
      case "progress": o.value = int(v); updateProgress(o); break;
      case "tabs": selectTab(o, int(v), false); break;
      default: o.blockEvents = false; return 0;
    }
    o.blockEvents = false;
    return 1;
  }
  function addItem(o, text) {
    if (o.kind !== "combo" && o.kind !== "list") return 0;
    const op = document.createElement("option");
    op.text = str(text);
    o.el.appendChild(op);
    if (o.kind === "combo" && o.el.options.length === 1) o.el.selectedIndex = 0; // as QComboBox
    return 1;
  }
  function setEnabled(o, on) {
    if (o.kind === "window" || o.kind === "group" || o.kind === "panel") {
      o.el.querySelectorAll("button, input, select, textarea").forEach(function (e) { e.disabled = !on; });
      o.el.classList.toggle("kw-disabled", !on);
    } else if (o.input) {
      o.input.disabled = !on;
      o.el.classList.toggle("kw-disabled", !on);
    } else if (o.el) {
      o.el.disabled = !on;
      o.el.classList.toggle("kw-disabled", !on);
    }
    return 1;
  }
  // Qt style sheets: plain "prop: value" pairs apply directly; a selector
  // block (QPushButton { … }) contributes its declarations.
  function applyStyle(o, css) {
    css = str(css).replace(/[^{}]*\{([^}]*)\}/g, "$1");
    o.el.style.cssText += ";" + css;
    return 1;
  }

  // ── The qt() entry point ──────────────────────────────────────────────────
  const unsupported = {
    wait: "blocks until an event - in a browser, use handlers (<Control>_Click or qt(\"on\", …))",
    poll: "polls the event loop - in a browser, use handlers (<Control>_Click or qt(\"on\", …))",
    openfile: "native file dialogs are not available in a browser",
    savefile: "native file dialogs are not available in a browser",
    connect: "Qt signal connections are not available in a browser - use qt(\"on\", …)",
    eventarg: "Qt signal arguments are not available in a browser",
    call: "Qt method calls are not available in a browser",
    qml: "QML is not available in a browser", qmlsource: "QML is not available in a browser",
    qmlwidget: "QML is not available in a browser", qmlimport: "QML is not available in a browser",
  };

  function qt(cmd) {
    const a = Array.prototype.slice.call(arguments, 1);
    cmd = str(cmd).toLowerCase();
    if (unsupported[cmd]) fail(cmd, unsupported[cmd]);
    let o;
    switch (cmd) {
      // Application / event loop
      case "init": return 1;
      case "run": case "exec": return 0;     // the browser runs the event loop

      // Widget creation – same arguments as libkayte_qt6
      case "window": return makeWindow(a[0], a[1], a[2]).id;
      case "label":  o = makeLabel(containerFor(cmd, a[0]), a[1]); place(o, a[2], a[3]); return o.id;
      case "button": o = makeButton(containerFor(cmd, a[0]), a[1]); place(o, a[2], a[3]); return o.id;
      case "checkbox": o = makeCheck(containerFor(cmd, a[0]), a[1], "checkbox"); place(o, a[2], a[3]); return o.id;
      case "edit":   o = makeEdit(containerFor(cmd, a[0]), a[1], false); place(o, a[2], a[3], a[4]); return o.id;
      case "textedit": o = makeEdit(containerFor(cmd, a[0]), "", true); place(o, a[1], a[2], a[3], a[4]); return o.id;
      case "combo":  o = makeCombo(containerFor(cmd, a[0])); place(o, a[1], a[2], a[3]); return o.id;
      case "list":   o = makeList(containerFor(cmd, a[0])); place(o, a[1], a[2], a[3], a[4]); return o.id;
      case "spin":   o = makeSpin(containerFor(cmd, a[0]), a[1], a[2]); place(o, a[3], a[4]); return o.id;
      case "slider": o = makeSlider(containerFor(cmd, a[0]), int(a[1]), int(a[2]), 1); place(o, a[3], a[4], a[5]); return o.id;
      case "progress": o = makeProgress(containerFor(cmd, a[0])); place(o, a[1], a[2], a[3]); return o.id;
      case "timer": {
        const ms = int(a[0]);
        if (ms <= 0) return 0;
        o = { kind: "timer", name: "" };
        register(o);
        o.interval = setInterval(function () { fire(o); }, ms);
        return o.id;
      }

      // Containers and layouts
      case "group": o = makeGroup(containerFor(cmd, a[0]), a[1]); place(o, a[2], a[3]); return o.id;
      case "panel": o = makePanel(containerFor(cmd, a[0])); place(o, a[1], a[2], a[3], a[4]); return o.id;
      case "vbox": case "hbox": case "grid": case "form": return makeLayout(cmd, a[0]).id;
      case "add": {
        const l = obj(cmd, a[0], "a layout"), item = obj(cmd, a[1], "a widget or layout");
        layoutAdd(cmd, l, item, a[2] === undefined ? -1 : int(a[2]), a[3] === undefined ? -1 : int(a[3]),
                  a[4] === undefined ? -1 : int(a[4]), a[5] === undefined ? -1 : int(a[5]));
        return 1;
      }
      case "addrow": {
        const l = obj(cmd, a[0], "a form layout"), item = obj(cmd, a[2], "a widget or layout");
        if (l.layoutKind !== "form") fail(cmd, "the first argument must be a form layout");
        const lab = el("label", "kw-form-label", l.el);
        lab.textContent = str(a[1]);
        item.el.classList.add("kw-in-layout");
        l.el.appendChild(item.el);
        return 1;
      }
      case "stretch": {
        const l = obj(cmd, a[0], "a layout");
        const s = el("div", "kw-stretch", l.el);
        s.style.flex = String(a[1] === undefined ? 1 : int(a[1]));
        return 1;
      }
      case "colstretch": case "rowstretch": return 1;
      case "spacing": obj(cmd, a[0], "a layout").el.style.gap = int(a[1]) + "px"; return 1;
      case "margins": obj(cmd, a[0], "a layout").el.style.padding = int(a[1]) + "px"; return 1;

      // Menus and tabs
      case "menu": {
        const p = obj(cmd, a[0], "a window or menu");
        if (p.kind !== "window" && p.kind !== "menu") fail(cmd, "the parent must be a window or a menu");
        return makeMenu(p, a[1]).id;
      }
      case "action": {
        const m = obj(cmd, a[0], "a menu");
        if (m.kind !== "menu") fail(cmd, "the first argument must be a menu");
        return makeAction(m, a[1], str(a[2]), int(a[3])).id;
      }
      case "separator": el("hr", "kw-separator", obj(cmd, a[0], "a menu").popup); return 1;
      case "tabs": o = makeTabs(containerFor(cmd, a[0])); place(o, a[1], a[2], a[3], a[4]); return o.id;
      case "tab": {
        const t = obj(cmd, a[0], "a tabs widget");
        if (t.kind !== "tabs") fail(cmd, "the first argument must be a tabs widget");
        const page = { kind: "tab", isContainer: true, win: t.win, name: "", el: el("div", "kw-tabpage", t.el) };
        page.button = el("button", "kw-tab", t.bar);
        page.button.type = "button";
        page.button.setAttribute("role", "tab");
        page.button.textContent = str(a[1]);
        const index = t.pages.length;
        page.button.addEventListener("click", function () { selectTab(t, index, true); });
        t.pages.push(page);
        register(page);
        selectTab(t, t.current < 0 ? 0 : t.current, false);
        return page.id;
      }

      // Widget state
      case "settext": setText(obj(cmd, a[0]), a[1]); return 1;
      case "gettext": return getText(obj(cmd, a[0]));
      case "getvalue": return getValue(obj(cmd, a[0]));
      case "setvalue": return setValue(obj(cmd, a[0]), a[1]);
      case "additem": return addItem(obj(cmd, a[0]), a[1]);
      case "removeitem": {
        o = obj(cmd, a[0]);
        if (o.kind !== "combo" && o.kind !== "list") return 0;
        const i = int(a[1]);
        if (i < 0 || i >= o.el.options.length) return 0;
        o.el.remove(i);
        return 1;
      }
      case "clear":
        o = obj(cmd, a[0]);
        if (o.kind === "combo" || o.kind === "list") o.el.innerHTML = "";
        else setText(o, "");
        return 1;
      case "geometry": o = obj(cmd, a[0]); place(o.kind === "window" ? { el: o.body } : o, a[1], a[2], a[3], a[4]); return 1;
      case "enable": return setEnabled(obj(cmd, a[0]), true);
      case "disable": return setEnabled(obj(cmd, a[0]), false);
      case "style": return applyStyle(obj(cmd, a[0]), a[1]);
      case "show":
        o = obj(cmd, a[0]);
        if (o.el) { o.el.hidden = false; if (o.kind === "window") o.el.style.zIndex = ++zTop; }
        return 1;
      case "hide": o = obj(cmd, a[0]); if (o.el) o.el.hidden = true; return 1;
      case "close":
        o = obj(cmd, a[0]);
        if (o.kind === "timer") clearInterval(o.interval);
        else if (o.el) o.el.hidden = true;
        return 1;

      // Dialogs
      case "message": global.alert(str(a[1])); return 1;
      case "confirm": return global.confirm(str(a[1])) ? 1 : 0;
      case "input": { const r = global.prompt(str(a[1]), str(a[2])); return r === null ? "" : r; }

      // Forms and objects by name
      case "loadform": {
        scanForms();
        const key = str(a[0]);
        const form = formSources[key.toLowerCase()] || formSources[baseName(key).toLowerCase()] ||
                     formSources[stripExt(baseName(key)).toLowerCase()];
        if (!form) fail(cmd, 'no form "' + key + '" in this page (KayteIDE embeds forms when it exports the web app)');
        return buildForm(form).id;
      }
      case "formhandlers": return "";
      case "find": {
        const w = obj(cmd, a[0], "a window");
        const name = str(a[1]).toLowerCase();
        o = w.controls && w.controls[name];
        if (!o) {
          for (let i = 1; i < objects.length; i++) {
            const c = objects[i];
            if (c && c.win === w && c.name && c.name.toLowerCase() === name) { o = c; break; }
          }
        }
        if (!o) fail(cmd, 'no object named "' + a[1] + '"');
        return o.id;
      }
      case "on": {
        o = obj(cmd, a[0]);
        o.handler = str(a[1]);
        return 1;
      }
      case "getprop": {
        o = obj(cmd, a[0]);
        switch (str(a[1]).toLowerCase()) {
          case "text": return getText(o);
          case "value": case "checked": case "currentindex": return getValue(o);
          case "enabled": return o.el && !o.el.classList.contains("kw-disabled") ? 1 : 0;
          case "visible": return o.el && !o.el.hidden ? 1 : 0;
          default: fail(cmd, 'property "' + a[1] + '" is not available in a browser');
        }
        break;
      }
      case "setprop": {
        o = obj(cmd, a[0]);
        switch (str(a[1]).toLowerCase()) {
          case "text": setText(o, a[2]); return 1;
          case "value": case "checked": case "currentindex": return setValue(o, a[2]);
          case "enabled": return setEnabled(o, int(a[2]) !== 0);
          case "visible": o.el.hidden = int(a[2]) === 0; return 1;
          default: fail(cmd, 'property "' + a[1] + '" is not available in a browser');
        }
        break;
      }
    }
    fail(cmd, "unknown command");
  }

  function unavailable(name) {
    return function () { throw new Error(name + "() is not available in a browser"); };
  }

  // ── Globals the .kjs front end provides ───────────────────────────────────
  global.qt = qt;
  global.print = kaytePrint;            // replaces window.print (the print dialog), as .kjs's print
  global.msgbox = function (m) { global.alert(str(m)); };
  global.process = unavailable("process");
  global.qml = unavailable("qml");
  global.addEventListener("error", function (ev) { kaytePrint("Error: " + ev.message); });
  global.KayteWeb = { version: "1.0", objects: objects };
})(window);
