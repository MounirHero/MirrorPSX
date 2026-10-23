"""Bundle the MinGW DLL dependency closure beside a built executable."""
from pathlib import Path
import re, shutil, subprocess, sys
exe=Path(sys.argv[1]).resolve()
prefix=Path(sys.argv[2])
pending=[exe]; seen=set()
while pending:
    path=pending.pop()
    output=subprocess.check_output([str(prefix/'bin/objdump.exe'),'-p',str(path)],text=True)
    for name in re.findall(r'DLL Name: (\S+)',output):
        key=name.lower()
        if key in seen: continue
        seen.add(key)
        source=prefix/'bin'/name
        if source.exists():
            target=exe.parent/name
            shutil.copy2(source,target)
            pending.append(target)
print(f'Bundled {len(seen)} direct/transitive DLL references')
