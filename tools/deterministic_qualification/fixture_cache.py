"""Cache compiled fixtures, never test execution or mandatory shipped checks.

Only simple, inspected MSVC batch recipes are cacheable. Every lookup reruns
preprocessing, including header search, and verifies actual link inputs.
"""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time
import uuid
from .artifacts import sha256_file
from .run_journal import atomic_json


def _receipt(result, status, reason, started, **extra):
    result.fixture_cache=dict(status=status,reason=reason,elapsed_seconds=time.perf_counter()-started,
        start_monotonic_ns=int(started*1e9),end_monotonic_ns=time.perf_counter_ns(),returncode=result.returncode,**extra)
    directory=os.environ.get('HORSE_FIXTURE_RECEIPTS')
    if directory: atomic_json(Path(directory)/(uuid.uuid4().hex+'.json'),result.fixture_cache)
    return result


def run_compile(command, **kwargs):
    if os.environ.get('HORSE_TEST_LAYER') in ('unit', 'workflow'):
        raise RuntimeError('compiled fixture requires the native_contract pytest marker')
    started=time.perf_counter()
    def bypass(reason):
        return _receipt(subprocess.run(command,**kwargs),'bypass',reason,started)
    if any(k.endswith('_RED_HEADER') and v for k,v in os.environ.items()): return bypass('explicit RED override')
    if os.name!='nt' or not kwargs.get('text') or not kwargs.get('capture_output'): return bypass('unsupported invocation')
    cwd=Path(kwargs.get('cwd',Path.cwd())).resolve()
    recipe=Path(command[-1])
    if recipe.suffix.lower() not in ('.cmd','.bat'): return bypass('not a compile recipe')
    raw=recipe.read_text()
    lines=[line.strip() for line in raw.splitlines() if line.strip()]
    setup=[line for line in lines if line.lower().startswith('call ') and 'vcvars' in line.lower()]
    compilations=[line for line in lines if re.match(r'^cl\s',line,re.I)]
    allowed=[line for line in lines if line.lower() in ('@echo off','if errorlevel 1 exit /b 1')]
    if len(setup)!=1 or len(compilations)!=1 or len(setup)+len(compilations)+len(allowed)!=len(lines):
        return bypass('recipe contains unclassified commands')
    if any(re.search(r'[@<>|&]|/LIBPATH|/GL|/Yu|/Fp',line,re.I) for line in compilations):
        return bypass('unclassified compiler dependency or redirection')
    extra_link_input=r'/DEF:|/MANIFESTINPUT:|/ORDER:|/PGD:|\.(?:obj|o|res|exp|rsp)(?:"|\s|$)'
    inputs=re.sub(r'/F[eo](?::)?(?:"[^"]*"|\S+)','',compilations[0],flags=re.I)
    if re.search(extra_link_input,inputs,re.I):return bypass('unclassified explicit link input')
    root=Path(os.environ.get('HORSE_FIXTURE_CACHE',str(Path(__file__).resolve().parents[2]/'build_cmake_LessEqual421__Shipping__Win64/replay-tests/fixture-cache')))
    root.mkdir(parents=True,exist_ok=True)
    probe=cwd/('.cache-probe-'+uuid.uuid4().hex+'.cmd')
    envfile=cwd/('.cache-env-'+uuid.uuid4().hex+'.txt')
    preprocess=[]
    body=['@echo off',setup[0],'set VSLANG=1033',f'where cl > "{envfile}"',f'where link >> "{envfile}"',f'set LIB >> "{envfile}"',f'set INCLUDE >> "{envfile}"']
    # CL/_CL_ and LINK/_LINK_ can change code generation without changing /EP.
    for name in ('CL','_CL_','LINK','_LINK_'):
        body.append(f'if defined {name} set {name} >> "{envfile}"')
    for i,line in enumerate(compilations):
        path=cwd/('.cache-preprocess-'+uuid.uuid4().hex+'.txt');preprocess.append(path)
        clean=re.sub(r'/F[eo](?::)?(?:"[^"]*"|\S+)','',line,flags=re.I)
        clean=re.sub(r'\s/LD\b','',clean,flags=re.I).split(' /link')[0]
        body.extend([clean+f' /EP > "{path}"','if errorlevel 1 exit /b 1'])
    probe.write_text('\n'.join(body)+'\n')
    try:
        observed=subprocess.run(['cmd','/d','/c',str(probe)],**kwargs)
        if observed.returncode: return bypass('preprocessing unavailable')
        environment=envfile.read_text(errors='replace').splitlines()
        if any(re.search(extra_link_input,line,re.I) for line in environment if re.match(r'^(?:_?CL_?|_?LINK_?)=',line,re.I)):
            return bypass('unclassified environment link input')
        if any(re.search(r'/DEF:|/MANIFESTINPUT:|/ORDER:|/PGD:',p.read_text(errors='replace'),re.I) for p in preprocess):
            return bypass('unclassified preprocessor link directive')
        executables=[Path(v.strip()) for v in environment if v.lower().endswith('.exe')]
        if len(executables)<2 or not all(p.is_file() for p in executables): return bypass('compiler identity unavailable')
        identities={str(p):sha256_file(p) for p in executables}
        for name in ('c1xx.dll','c2.dll'):
            p=executables[0].with_name(name)
            if not p.is_file(): return bypass('compiler backend unavailable')
            identities[str(p)]=sha256_file(p)
        libline=next((v[4:] for v in environment if v.upper().startswith('LIB=')),None)
        if libline is None: return bypass('library search path unavailable')
        local={p.relative_to(cwd).as_posix():sha256_file(p) for p in cwd.rglob('*')
               if p.is_file() and p.suffix.lower() in ('.cpp','.c','.hpp','.h','.inl','.lib')}
        key=dict(schema=1,recipe=raw.replace(str(cwd),'<fixture>'),preprocessed=[sha256_file(p) for p in preprocess],
                 local=local,toolchain=identities,environment=environment)
        digest=hashlib.sha256(json.dumps(key,sort_keys=True).encode()).hexdigest()
        entry=root/digest;metadata=entry/'receipt.json';reason='no matching compilation'
        family=hashlib.sha256(json.dumps(sorted(local)).encode()).hexdigest()
        previous=root/'last-recipe'/(family+'.json')
        if previous.is_file() and not metadata.is_file():
            try:
                prior=json.loads(previous.read_bytes())
                changed=[name for name in key if key[name]!=prior.get(name)]
                reason='changed '+', '.join(changed) if changed else 'entry absent'
            except (OSError,ValueError):reason='prior recipe receipt unreadable'
        if metadata.exists():
            try:
                cached=json.loads(metadata.read_bytes())
                if cached['key']!=key:raise ValueError('cache recipe identity differs')
                if (not isinstance(cached['outputs'],list) or not cached['outputs']
                    or not isinstance(cached['libraries'],list) or not cached['libraries']):
                    raise ValueError('cache dependency/output receipt incomplete')
                for item in cached['libraries']:
                    original=Path(item['path'])
                    selected=next((d/original.name for d in [cwd,*map(Path,libline.split(';'))] if (d/original.name).is_file()),None)
                    if selected is None or selected.resolve()!=original.resolve() or sha256_file(selected)!=item['sha256']:
                        raise ValueError('link input or search resolution changed')
                for item in cached['outputs']:
                    name=item['name']
                    if (Path(name).name!=name or Path(item['destination']).name!=item['destination']
                            or sha256_file(entry/name)!=item['sha256']): raise ValueError('cached output corrupt')
                for item in cached['outputs']: shutil.copyfile(entry/item['name'],cwd/item['destination'])
                result=subprocess.CompletedProcess(command,0,cached['stdout'],cached['stderr'])
                return _receipt(result,'hit','verified preprocessing/toolchain/link inputs',started,key=digest,compiled=False)
            except (OSError,ValueError,KeyError,TypeError,AttributeError) as error:
                reason=str(error)
                atomic_json(root/'invalid'/((digest+'-'+uuid.uuid4().hex)+'.json'),dict(key=digest,error=reason))
        elif entry.exists():
            reason='incomplete cache entry: receipt missing'
            atomic_json(root/'invalid'/((digest+'-'+uuid.uuid4().hex)+'.json'),dict(key=digest,error=reason))
        # Verbose library resolution is compiler evidence, not a mocked closure.
        compile_probe=cwd/('.cache-build-'+uuid.uuid4().hex+'.cmd')
        built_lines=['@echo off',setup[0],'set VSLANG=1033']
        for line in compilations:
            built_lines.extend([line+(' /VERBOSE:LIB' if ' /link' in line else ' /link /VERBOSE:LIB'),'if errorlevel 1 exit /b 1'])
        compile_probe.write_text('\n'.join(built_lines)+'\n')
        try: result=subprocess.run(['cmd','/d','/c',str(compile_probe)],**kwargs)
        finally: compile_probe.unlink(missing_ok=True)
        if result.returncode: return _receipt(result,'miss',reason,started,key=digest,compiled=True)
        libraries=[]
        for match in re.findall(r'Searching\s+(.+?\.lib):',result.stdout+result.stderr,re.I):
            p=Path(match.strip())
            if not p.is_absolute(): p=cwd/p
            if not p.is_file(): return _receipt(result,'bypass','unresolved link evidence',started,compiled=True)
            libraries.append(dict(path=str(p.resolve()),sha256=sha256_file(p)))
        if not libraries: return _receipt(result,'bypass','link dependency evidence unavailable',started,compiled=True)
        outputs=[p for p in cwd.iterdir() if p.is_file() and p.suffix.lower() in ('.exe','.dll','.lib')]
        if not outputs: return _receipt(result,'bypass','compiled outputs unavailable',started,compiled=True)
        # Cache contents are rebuildable. Preserve invalid entries; publish a
        # replacement under a new generation and atomically switch the receipt.
        entry.mkdir(exist_ok=True)
        rows=[]
        for p in outputs:
            name=sha256_file(p)+p.suffix.lower();target=entry/name
            if not target.exists() or sha256_file(target)!=sha256_file(p):
                temporary=entry/(uuid.uuid4().hex+'.tmp')
                shutil.copyfile(p,temporary);os.replace(temporary,target)
            rows.append(dict(name=name,destination=p.name,sha256=sha256_file(p)))
        atomic_json(metadata,dict(key=key,libraries=libraries,outputs=rows,stdout=result.stdout,stderr=result.stderr))
        atomic_json(previous,key)
        return _receipt(result,'miss',reason,started,key=digest,compiled=True)
    finally:
        probe.unlink(missing_ok=True);envfile.unlink(missing_ok=True)
        for p in preprocess:p.unlink(missing_ok=True)
