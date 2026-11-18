// Bot builds only (NHWEB_BOT), appended to nethack.js: Module.nhbot reads the core's state by the names nhbot.c lists,
// from the wasm memory. It never writes.
//
//   nhbot.global("u").ux                     a field of a global struct
//   nhbot.global("levl", x, y).typ           an element of a global array
//   nhbot.list(nhbot.global("fmon"), "nmon") a list, from its first element along a pointer field
//   m.data.mlet                              a pointer field to a listed struct reads as that struct, or null
//   nhbot.value("COLNO")                     a constant
//   nhbot.ptr(a), nhbot.u8(a), nhbot.string(a) raw reads, for what has no struct, as gv.viz_array's rows
Module["nhbot"] = (() => {
  let layout = null;
  let view = null;
  const types = {};

  const mem = () => (view && view.buffer === HEAPU8.buffer ? view : (view = new DataView(HEAPU8.buffer)));

  function int(addr, size, signed) {
    const d = mem();
    if (size === 1) return signed ? d.getInt8(addr) : d.getUint8(addr);
    if (size === 2) return signed ? d.getInt16(addr, true) : d.getUint16(addr, true);
    if (size === 4) return signed ? d.getInt32(addr, true) : d.getUint32(addr, true);
    if (size === 8) return Number(signed ? d.getBigInt64(addr, true) : d.getBigUint64(addr, true));
    throw new Error(`nhbot: no ${size}-byte integers`);
  }

  function reader([offset, size, kind, extra]) {
    if (kind === "i" || kind === "u") return (addr) => int(addr + offset, size, kind === "i");
    if (kind === "p") {
      if (!extra) return (addr) => int(addr + offset, size, false);
      return (addr) => {
        const p = int(addr + offset, size, false);
        return p ? at(extra, p) : null;
      };
    }
    const shift = 31 - Math.clz32(extra & -extra);
    const width = 32 - Math.clz32(extra) - shift;
    return (addr) => {
      const v = ((mem().getUint32(addr + offset, true) & extra) >>> 0) / 2 ** shift;
      return kind === "j" && v >= 2 ** (width - 1) ? v - 2 ** width : v;
    };
  }

  function load() {
    if (layout) return layout;
    layout = JSON.parse(UTF8ToString(_nhbot_layout()));
    for (const [name, s] of Object.entries(layout.structs)) {
      function View(addr) {
        this.addr = addr;
      }
      for (const [field, spec] of Object.entries(s.fields)) {
        const read = reader(spec);
        Object.defineProperty(View.prototype, field, {
          get() {
            return read(this.addr);
          },
        });
      }
      types[name] = { View, size: s.size };
    }
    return layout;
  }

  function at(type, addr) {
    load();
    const t = types[type];
    if (!t) throw new Error(`nhbot: no struct ${type} in the layout`);
    return new t.View(addr);
  }

  function global(name, ...index) {
    const g = load().globals[name];
    if (!g) throw new Error(`nhbot: no global ${name} in the layout`);
    if (g.ptr) {
      const p = int(g.addr, 4, false);
      return g.type ? (p ? at(g.type, p) : null) : p;
    }
    if (!g.dims) return at(g.type, g.addr);
    if (index.length !== g.dims.length || index.some((i, k) => !(i >= 0 && i < g.dims[k]))) {
      throw new Error(`nhbot: ${name}[${index.join("][")}] is outside [${g.dims.join("][")}]`);
    }
    const flat = index.reduce((n, i, k) => n * g.dims[k] + i, 0);
    return at(g.type, g.addr + flat * types[g.type].size);
  }

  function list(first, next) {
    const out = [];
    for (let v = first; v; v = v[next]) out.push(v);
    return out;
  }

  return {
    layout: load,
    at,
    global,
    list,
    value: (name) => load().values[name],
    ptr: (addr) => int(addr, 4, false),
    u8: (addr) => int(addr, 1, false),
    string: (addr) => UTF8ToString(addr),
  };
})();
