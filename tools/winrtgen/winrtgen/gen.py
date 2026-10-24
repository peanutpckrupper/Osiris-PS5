# Generates WRL classes for WinRT ABI interfaces from SDK headers: every method present, unimplemented ones log and
# return E_NOTIMPL; bodies for chosen methods come from the spec.
import re, sys, os
HDR = os.path.join(os.path.dirname(__file__), 'hdr')
def methods(header, iface):
    text = open(os.path.join(HDR, header), encoding='utf-8', errors='replace').read()
    m = re.search(r'\n\s*' + re.escape(iface) + r'\s*:\s*(public\s+)?IInspectable\s*\{(.*?)\n\s*\};', text, re.S)
    if not m: raise SystemExit(f'{iface} not found in {header}')
    body = re.sub(r'/\*.*?\*/', '', m.group(2), flags=re.S)
    out = []
    for mm in re.finditer(r'virtual\s+HRESULT\s+STDMETHODCALLTYPE\s+(\w+)\s*\((.*?)\)\s*=\s*0;', body, re.S):
        name, params = mm.group(1), mm.group(2)
        params = re.sub(r'\b_*RPC__\w+(\([^)]*\))?', '', params)
        params = re.sub(r'\b_(In|Out|Inout)\w*(\([^)]*\))?', '', params)
        params = ' '.join(params.split())
        out.append((name, params))
    return out
def default_body(short, name, params):
    """Property bag style defaults: getters give a zero value, setters and events succeed; other methods log."""
    args = [a.strip() for a in params.split(',') if a.strip()]
    last = args[-1].split()[-1] if args else ''
    if name.startswith('get_') and len(args) == 1: return f'{{ CompatMiss("{short}.{name} (default value)"); *{last} = {{}}; return S_OK; }}'
    if name.startswith('put_') and len(args) == 1: return '{ return S_OK; }'
    if name.startswith('add_') and len(args) == 2: return f'{{ static INT64 n = 1; {last}->value = n++; return S_OK; }}'
    if name.startswith('remove_') and len(args) == 1: return '{ return S_OK; }'
    return f'{{ CompatMiss("{short}.{name}"); return E_NOTIMPL; }}'
def emit(cls, iface, header, runtime_name, extra_members='', bodies=None, ns=None):
    bodies = bodies or {}
    lines = [f'class {cls} final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, {iface}> {{']
    if runtime_name: lines.append(f'    InspectableClass(L"{runtime_name}", BaseTrust)')
    lines.append('public:')
    if extra_members: lines.append(extra_members)
    if not runtime_name: lines.append(f'    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {{ HRESULT h = RuntimeClass::QueryInterface(riid, ppv); if (FAILED(h)) CompatMissQI("{cls}", riid); return h; }}')
    used = set()
    for name, params in methods(header, iface.split('::')[-1]):
        if name in bodies: b = bodies[name]; used.add(name)
        else: b = default_body(iface.split("::")[-1], name, params)
        lines.append(f'    HRESULT STDMETHODCALLTYPE {name}({params}) override {b}')
    missing = set(bodies) - used
    if missing: raise SystemExit(f'{cls}: no such methods {missing}')
    lines.append('};')
    return '\n'.join(lines)
def emit_statics(cls, ifaces, extra_members='', bodies=None):
    """A factory: IActivationFactory + the given (iface, header) statics interfaces."""
    bodies = dict(bodies or {}); used = set()
    names = ', '.join(i for i, _ in ifaces)
    lines = [f'class {cls} final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IActivationFactory, {names}> {{', 'public:']
    if extra_members: lines.append(extra_members)
    lines.append(f'    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {{ HRESULT h = RuntimeClass::QueryInterface(riid, ppv); if (FAILED(h)) CompatMissQI("{cls}", riid); return h; }}')
    lines.append('    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** out) override ' + bodies.pop('ActivateInstance', '{ if (out) *out = nullptr; return E_NOTIMPL; }'))
    for iface, header in ifaces:
        short = iface.split('::')[-1]
        for name, params in methods(header, short):
            if name in bodies: b = bodies[name]; used.add(name)
            else: b = default_body(short, name, params)
            lines.append(f'    HRESULT STDMETHODCALLTYPE {name}({params}) override {b}')
    missing = set(bodies) - used
    if missing: raise SystemExit(f'{cls}: no such methods {missing}')
    lines.append('};')
    return '\n'.join(lines)
