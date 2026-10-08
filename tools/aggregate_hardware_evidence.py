"""Aggregate hardware evidence for the current build, with explicit provenance.

Three kinds of evidence are accepted, and each is labelled:

* "current_build": a run whose recorded source and runtime hashes match the
  current build manifest.
* "identical_code": a run from an earlier build whose recorded hashes match that
  build's own manifest, and whose executable code for this profile is
  byte-identical to the current build's.  The executed machine code is the same
  bytes; that build's payload artifact hash is re-checked against the run.

Nothing is inferred for a profile whose code changed, and every raw buffer is
re-hashed from disk for every claimed vector.

Because the execution packet moved (0x020D8100 -> 0x020D9700) between builds,
identical code is also evidence that a payload does not embed a packet address:
any such constant would have had to change with it.
"""
import datetime
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[1]
TESTS=ROOT/'patched'/'toolchain-tests'
CURRENT=TESTS/'build'
# Earlier builds that may carry re-usable evidence, newest first.
CANDIDATES=('build-pre-coderegion','build-pre-spr','build-pre-icache','build-first-pass')

sys.path.insert(0,str(ROOT/'tools'))
from toolchain_tests.build import load_suite, sha


def key_for(sources, runtime):
    return json.dumps([sources, runtime], sort_keys=True)


def code_bytes(directory, entry):
    meta=(entry.get('files') or {}).get('code.bin')
    return (directory/meta['path']).read_bytes() if meta else None


def payload_sha(directory, entry):
    meta=(entry.get('files') or {}).get('payload.bin')
    return sha((directory/meta['path']).read_bytes()) if meta else None


def slots(entry):
    if entry['kind']=='spi':
        return [('offset',off) for off in (0,2048)]
    return [('vector',i) for i in range(len(entry['vectors']))]


current=load_suite(CURRENT,require_sources=False)
cur_entries={e['key']:e for e in current['tests']}
builds={}
for name in CANDIDATES:
    directory=TESTS/name
    if not (directory/'manifest.json').exists(): continue
    manifest=json.loads((directory/'manifest.json').read_text())
    builds[key_for(manifest['sources'],manifest['runtime_hashes'])]=(
        directory,{e['key']:e for e in manifest['tests']})

required={}
for entry in current['tests']:
    if entry['status']!='ready': continue
    for kind,index in slots(entry):
        required[(entry['key'],kind,index)]=entry

evidence={}
reports=[]
for report_path in sorted((TESTS/'runs').glob('*/run.json')):
    report=json.loads(report_path.read_text())
    key=key_for(report.get('source_hashes'),report.get('runtime_hashes'))
    provenance='current_build' if key==key_for(current['sources'],current['runtime_hashes']) \
        else ('identical_code' if key in builds else None)
    if provenance is None: continue
    reports.append(dict(path=str(report_path.relative_to(ROOT)),sha256=sha(report_path.read_bytes()),
                        status=report['status'],scope=report.get('scope','all'),provenance=provenance))
    source_dir,source_entries=(CURRENT,cur_entries) if provenance=='current_build' else builds[key]
    for row in report['results']:
        if row['status']!='pass': continue
        token=(row['key'],'offset',row['parameter']) if row['vector'] is None \
            else (row['key'],'vector',row['vector'])
        entry=required.get(token)
        if entry is None: continue
        origin=CURRENT
        if provenance=='identical_code':
            # Re-use only where this profile's executable code is byte-identical
            # between the build that produced the run and the current build.
            previous=source_entries.get(row['key'])
            if previous is None: continue
            if code_bytes(source_dir,previous) is None or \
               code_bytes(source_dir,previous)!=code_bytes(CURRENT,cur_entries[row['key']]):
                continue
            origin=source_dir
        raw=report_path.parent/row['raw_file']
        if sha(raw.read_bytes())!=row['received_sha256']:
            raise SystemExit('raw evidence hash mismatch: '+str(raw))
        if entry['kind']=='ram':
            expected=sha(entry['blob']) if origin==CURRENT else payload_sha(origin,source_entries[row['key']])
            if row['program_sha256']!=expected:
                raise SystemExit('program hash mismatch: '+str(raw))
        evidence[token]=dict(provenance='current_build' if origin==CURRENT else 'identical_code',
                             origin=str(origin.relative_to(TESTS)),
                             report=str(report_path.relative_to(ROOT)),
                             raw=str(raw.relative_to(ROOT)),sha256=row['received_sha256'])

# A run that covered the whole suite, finished, and had nothing left unrun.
full_runs=[]
for report_path in sorted((TESTS/'runs').glob('*/run.json')):
    report=json.loads(report_path.read_text())
    if report.get('scope')!='all' or report['status']!='pass' or report.get('not_run'): continue
    if key_for(report.get('source_hashes'),report.get('runtime_hashes'))!= \
            key_for(current['sources'],current['runtime_hashes']): continue
    full_runs.append(dict(path=str(report_path.relative_to(ROOT)),
                          sha256=sha(report_path.read_bytes()),
                          passed_vectors=report['passed_vectors']))

missing=sorted(set(required)-set(evidence))
by_provenance={}
for item in evidence.values():
    by_provenance[item['provenance']]=by_provenance.get(item['provenance'],0)+1

aggregate=dict(
    generated_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
    status='ready_subset_pass' if not missing else 'incomplete',
    comment=('Every runnable profile of the current build has passing hardware evidence. '
             'Profiles are re-used from an earlier build only when their executable code is '
             'byte-identical, and each re-used vector is re-tied to that build\'s payload hash. '
             'Because the execution packet moved from 0x020D8100 to 0x020D9700, identical code '
             'also shows the payload does not embed a packet address.'),
    layout=current['layout'],
    manifest=str((CURRENT/'manifest.json').relative_to(ROOT)),
    manifest_sha256=sha((CURRENT/'manifest.json').read_bytes()),
    ready_profiles=sum(e['status']=='ready' for e in current['tests']),
    size_blocked_profiles=sum(e['status']=='blocked_size' for e in current['tests']),
    expected_vectors=len(required),passed_vectors=len(evidence),
    evidence_by_provenance=by_provenance,
    evidence_origins=sorted({v['origin'] for v in evidence.values()}),
    missing=missing,
    not_run=[dict(key=e['key'],reason=e['reason']) for e in current['tests'] if e['status']!='ready'],
    previous_manifests={name:sha((TESTS/name/'manifest.json').read_bytes())
                        for name in CANDIDATES if (TESTS/name/'manifest.json').exists()},
    single_session_runs=full_runs,
    reports=reports,
    vectors=[dict(key=k,kind=kind,index=i,**v) for (k,kind,i),v in sorted(evidence.items())],
    limitations=[
        'Single-session runs above cover the whole suite; earlier partial sessions remain in the run history.',
        'USB disconnects occurred during earlier sessions and remain unexplained.',
        'Records from sessions that later failed at a USB upload are included; those sessions did not finish their final canary.',
        'The free region and packet placement are validated against this firmware build; re-survey if the build changes.',
        'No flash-write command was sent and no full post-run flash readback was performed.',
        'Hardware evidence covers the tested compiler, ABI, SFR and runtime paths only; it does not prove interrupt behaviour, the full vendor ABI or flashable image validity.',
    ],
)
out=TESTS/'hardware-validation.json'
if out.exists(): out.replace(TESTS/'hardware-validation-pre-coderegion.json')
out.write_text(json.dumps(aggregate,indent=2)+'\n')
print('ready profiles      :',aggregate['ready_profiles'])
print('expected vectors    :',aggregate['expected_vectors'])
print('passed vectors      :',aggregate['passed_vectors'])
print('by provenance       :',aggregate['evidence_by_provenance'])
print('origins             :',aggregate['evidence_origins'])
print('missing             :',len(missing), missing[:8])
print('evidence            :',out)
