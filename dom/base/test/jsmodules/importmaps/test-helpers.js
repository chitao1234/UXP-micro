// Shared SimpleTest adapter for the native import-map integration cases.
const root = new URL("importmaps/", location.href).href;
const moduleURL = root + "module.js";
const otherURL = root + "other.js";
const endScript = "</scr" + "ipt>";
const tag = (type, source, attributes = "") =>
  '<script type="' + type + '" ' + attributes + '>' + source + endScript;
const mapTag = (map, attributes) => tag("importmap", JSON.stringify(map), attributes);
const nextTurn = () => new Promise(resolve => setTimeout(resolve, 0));

function frame(markup = "", source = null) {
  return new Promise(resolve => {
    const f = document.createElement("iframe");
    f.onload = () => {
      f.onload = null;
      f.contentWindow.errors = [];
      f.contentWindow.onerror = (m, u, l, c, e) => {
        f.contentWindow.errors.push(e ? e.name : m);
        return true;
      };
      resolve(f);
    };
    if (source) f.src = source;
    else f.srcdoc = '<!doctype html><base href="' + root + '">' + markup;
    document.body.appendChild(f);
  });
}

function map(f, value, type = "importmap", nonce = "") {
  const s = f.contentDocument.createElement("script");
  s.type = type;
  s.nonce = nonce;
  s.textContent = typeof value === "string" ? value : JSON.stringify(value);
  f.contentDocument.head.appendChild(s);
  return s;
}

function run(f, code, nonce = "", integrity = null) {
  return new Promise((resolve, reject) => {
    f.contentWindow.moduleDone = resolve;
    const s = f.contentDocument.createElement("script");
    s.type = "module";
    s.nonce = nonce;
    if (integrity !== null) s.setAttribute("integrity", integrity);
    s.onerror = () => reject(new Error("module element error: " + code));
    s.textContent = code;
    f.contentDocument.head.appendChild(s);
  });
}

function dynamic(f, specifier, nonce = "") {
  return run(f, "import(" + JSON.stringify(specifier) +
    ").then(m => moduleDone({value:m.value}), e => moduleDone({error:e.name}));", nonce);
}

function resolve(f, specifier) {
  return run(f, "try { moduleDone({url:import.meta.resolve(" + JSON.stringify(specifier) +
    ")}); } catch(e) { moduleDone({error:e.name}); }");
}

async function withFrame(markup, body, source = null) {
  const f = await frame(markup, source);
  try { await body(f); } finally { f.remove(); }
}
