"""Export local edits and verify that patches reproduce the edited files."""
import difflib
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent
manifest = {}
for project in ['binutils', 'gcc', 'newlib']:
    chunks = []
    changed = []
    for old in sorted((ROOT / 'originals' / project).rglob('*')):
        if not old.is_file():
            continue
        relative = old.relative_to(ROOT / 'originals' / project)
        new = ROOT / 'src' / project / relative
        diff = ''.join(difflib.unified_diff(
            old.read_text().splitlines(keepends=True),
            new.read_text().splitlines(keepends=True),
            fromfile='a/' + str(relative), tofile='b/' + str(relative)))
        if diff:
            chunks.append(diff)
            changed.append(str(relative))
    dst = ROOT / 'patches' / (project + '-ax3292-little-endian.patch')
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text(''.join(chunks))
    with tempfile.TemporaryDirectory(prefix='ax3292-patch-check-') as temporary:
        check_dir = Path(temporary)
        for relative in changed:
            copy = check_dir / relative
            copy.parent.mkdir(parents=True, exist_ok=True)
            copy.write_bytes((ROOT / 'originals' / project / relative).read_bytes())
        subprocess.run(['patch', '-p1', '-i', str(dst)], cwd=check_dir, check=True)
        for relative in changed:
            assert (check_dir / relative).read_bytes() == (ROOT / 'src' / project / relative).read_bytes()
    manifest[project] = {'patch': str(dst.relative_to(ROOT)),
                         'sha256': hashlib.sha256(dst.read_bytes()).hexdigest(),
                         'changed_files': changed, 'applied_patch_verified': True}
(ROOT / 'patches/manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
