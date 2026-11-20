"""Walk PE imports starting from the given binaries; report DLLs not found in the folder (excluding system DLLs)."""
import os, subprocess, sys, re
out = sys.argv[1]
dumpbin = sys.argv[2]
roots = sys.argv[3:]
sysdir = os.path.join(os.environ['SystemRoot'], 'System32')
seen, missing, needed = set(), set(), set()
stack = list(roots)
while stack:
    f = stack.pop()
    if f.lower() in seen: continue
    seen.add(f.lower())
    txt = subprocess.run([dumpbin, '/nologo', '/dependents', os.path.join(out, f)], capture_output=True, text=True).stdout
    for d in re.findall(r'^\s+(\S+\.dll)\s*$', txt, re.M | re.I):
        if d.lower().startswith(('api-ms-', 'ext-ms-')): continue
        if os.path.exists(os.path.join(out, d)):
            needed.add(d); stack.append(d)
        elif not os.path.exists(os.path.join(sysdir, d)):
            missing.add(d)
print('NEEDED:', sorted(needed, key=str.lower))
print('MISSING:', sorted(missing))
present = {x for x in os.listdir(out) if x.lower().endswith('.dll')}
print('UNUSED in folder:', sorted(present - needed - set(roots), key=str.lower))
