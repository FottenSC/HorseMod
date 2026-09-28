"""Bounded passive replay control with independently verified module ownership."""
from __future__ import annotations

import os
import json
import math
import ctypes
from contextlib import ExitStack
import re
from pathlib import Path
import shutil
import time

import psutil

from .artifacts import sha256_file
from .source_retention import require_build_provenance
from .process_control import close_game, list_game_processes, ProcessExitWitness
from .replay_entry import qualification_root, wait_for_replay_entry
from .replay_run import ReplayRun
from .report import write_report
from .trace_parser import capture_log_offset


def retain_startup_failure(report, path, run_id, objects):
    # ExitStack runs this before journal cleanup. Retain bytes before parsing,
    # and never replace the original process exception with a collection error.
    try:
        if not path.is_file():return
        with path.open('rb') as stream:raw=stream.read(16385)
        from .replay_evidence import store_bytes
        retained=store_bytes(objects,raw)
        report['native_startup_failure_raw']=retained
        if len(raw)>16384:raise RuntimeError('startup failure diagnostic exceeded bound; retained prefix only')
        value=json.loads(raw)
        if not isinstance(value,dict) or value.get('run_id')!=run_id:
            raise RuntimeError('startup failure diagnostic belongs to another run')
        report['native_startup_failure']=dict(value,raw=retained)
    except (OSError,ValueError,RuntimeError) as error:
        report['native_startup_failure_collection_error']=str(error)


def retain_consumer_failure(report, path, run_id, objects):
    # Empty preopened file means no failure. Preserve malformed/partial bytes
    # before parsing; collection must never mask the original process failure.
    try:
        if not path.is_file():return
        with path.open('rb') as stream:raw=stream.read(2049)
        from .replay_evidence import store_bytes
        retained=store_bytes(objects,raw)
        report['consumer_failure_raw']=retained
        if not raw:
            report['consumer_failure_preopened_empty']=True
            return
        if len(raw)>2048:raise RuntimeError('consumer failure diagnostic exceeded bound')
        value=json.loads(raw)
        if not isinstance(value,dict) or value.get('run_id')!=run_id or value.get('version')!=1:
            raise RuntimeError('consumer failure diagnostic identity mismatch')
        report['consumer_failure']=dict(value,raw=retained)
    except (OSError,ValueError,RuntimeError) as error:
        report['consumer_failure_collection_error']=str(error)


AUTHORED_PREFIX = '217:0:0:172:2:0:0;218:1:0:170:2:0:0;219:2:0:168:2:0:0'


def authored_prefix_request(wire):
    if wire != AUTHORED_PREFIX: raise ValueError('short boundary requires the frozen independently authored prefix')
    return '17', 'authored_prefix='+wire+'\n'


def rolling_correction_groups(wire):
    """Return ordered same-arrival edit groups before request publication."""
    if not isinstance(wire,str) or len(wire)>128*600:raise ValueError('invalid rolling correction size')
    if not wire:return []
    rows=wire.split(';')
    if len(rows)>600:raise ValueError('too many rolling corrections')
    previous=None;groups=[]
    limits=(2**64-1,2**64-1,2**31-1,2**32-1,3,2**32-1,2**32-1)
    for row in rows:
        fields=row.split(':')
        if len(fields)!=7 or any(not re.fullmatch(r'[0-9]{1,20}',field) for field in fields):
            raise ValueError('invalid rolling correction fields')
        values=tuple(map(int,fields))
        if any(value>limit for value,limit in zip(values,limits)) or not values[4] or not values[0]:
            raise ValueError('invalid rolling correction range or arrival order')
        if previous is not None:
            if (values[0]<previous[0] or (values[0]==previous[0]
                and (values[1]!=previous[1] or values[2]!=previous[2] or values[3]<=previous[3]))
                or (values[0]>previous[0] and values[1]<=previous[1])):
                raise ValueError('invalid rolling correction group order')
        if previous is None or values[0]!=previous[0]:groups.append([])
        groups[-1].append(row);previous=values
    return groups


def rolling_correction_request(wire):
    """Validate bounded decimal rows before request publication or launch."""
    if not wire:return 13,''
    rolling_correction_groups(wire)
    return 14,'rolling_corrections='+wire+'\n'


def rolling_correction_prefix(wire,revisions):
    """Retain every edit belonging to the first N committed revisions."""
    groups=rolling_correction_groups(wire)
    if not isinstance(revisions,int) or not 0<=revisions<=len(groups):
        raise ValueError('invalid rolling correction revision prefix')
    return ';'.join(row for group in groups[:revisions] for row in group)


# Match the native observer's bounded storage and process ownership reservation.
VFX_OBSERVATION_MAX_EVENTS = 512
VFX_OBSERVATION_SIDECAR_BYTES = 4096 + 512 * 4096 + 32768
VFX_OBSERVATION_OWNED_BYTES = 4 * 1024 * 1024

PHYSICS_OBSERVATION_MAX_EVENTS = 16384
PHYSICS_OBSERVATION_SIDECAR_BYTES = (PHYSICS_OBSERVATION_MAX_EVENTS + 3) * 1024
PHYSICS_OBSERVATION_OWNED_BYTES = 2 * 1024 * 1024


def retain_physics_callback_observation(report,path,run_id,objects):
    """Keep flushed JSONL even on crash/partial write; validate only observed scope."""
    from .replay_evidence import store_bytes
    key='physics_callback_observation'
    report[key+'_phase_complete']=False
    report[key+'_requires_live_status']=True
    try:
        temporary=path.with_name(path.name+'.tmp')
        selected=None
        for candidate,suffix in ((temporary,'_temporary_raw'),(path,'_raw')):
            if not candidate.is_file():continue
            with candidate.open('rb') as stream:raw=stream.read(PHYSICS_OBSERVATION_SIDECAR_BYTES+1)
            report[key+suffix]=store_bytes(objects,raw,'.jsonl')
            selected=raw
        if selected is None:return
        raw=selected
        if len(raw)>PHYSICS_OBSERVATION_SIDECAR_BYTES or not raw.endswith(b'\n'):
            raise ValueError('oversize or unfinished physics observation line')
        lines=raw.splitlines()
        if not lines or len(lines)>PHYSICS_OBSERVATION_MAX_EVENTS+3 or any(len(line)>=1024 for line in lines):
            raise ValueError('physics observation record bound')
        rows=[json.loads(line) for line in lines]
        header=rows[0]
        writer_version=header.get('version')==2
        if (header.get('kind')!='header' or header.get('version') not in (1,2) or header.get('run_id')!=run_id
            or header.get('scope')!='observer_admission_to_trajectory_completion'
            or header.get('max_events')!=PHYSICS_OBSERVATION_MAX_EVENTS
            or header.get('owned_bytes')!=PHYSICS_OBSERVATION_OWNED_BYTES
            or header.get('overlap_sample')!='entry' or header.get('preexisting_calls_covered') is not False
            or header.get('entry_rvas')!=([0x29b4530,0x20113e0,0x21377c0,0x1f605a0] if writer_version else [0x29b4530,0x20113e0])
            or (writer_version and (header.get('writer_filter')!='observed_scene_collections_32' or header.get('pre_discovery_writers_covered') is not False or header.get('max_pending')!=128))
            or header.get('observation_only') is not True or header.get('opaque_scene_identity') is not True
            or any(header.get(k) is not False for k in ('lifetime_proven','recovery_proven',
                'writer_coverage_proven','native_quiescence_proven','whole_process_coverage'))):
            raise ValueError('physics observation identity/scope mismatch')
        def natural(row,name):
            value=row[name]
            if type(value) is not int or not 0<=value<2**64:raise ValueError('invalid '+name)
            return value
        base=natural(header,'image_base')
        if not base or not natural(header,'pid'):raise ValueError('missing process identity')
        stamp=natural(header,'time_ms');active={};phase=None;records=0;last_pair=0;overflow=False
        routes={};threads=set();known_scenes=set();writer_invalid=False
        for row in rows[1:]:
            now=natural(row,'time_ms')
            if now<stamp:raise ValueError('physics observation clock reversed')
            stamp=now
            if row['kind']=='overflow':
                if overflow or phase or row.get('dropped')!=1 or row.get('lower_bound') is not True:
                    raise ValueError('invalid overflow marker')
                overflow=True
                continue
            if row['kind']=='phase':
                if phase or row.get('boundary')!='CompleteTrajectory.physics_status':raise ValueError('invalid phase closure')
                phase=row
                if natural(row,'records')!=records or natural(row,'pending_at_close')!=len(active):
                    raise ValueError('inconsistent phase cutoff')
                if not natural(row,'close_thread'):raise ValueError('missing closing thread')
                natural(row,'end_tick')
                dropped=natural(row,'dropped')
                if bool(dropped)!=overflow or (writer_invalid and not dropped) or row.get('persistence_failed') is not False:
                    raise ValueError('inconsistent phase validity')
                if row.get('complete') is not (not active and not dropped):raise ValueError('false phase completeness')
                continue
            if row['kind']!='event':raise ValueError('unknown physics record')
            records+=1
            if records>PHYSICS_OBSERVATION_MAX_EVENTS:raise ValueError('event overflow')
            for k in ('entry_rva','pair','parent_pair','depth','thread','collection','scene',
                      'scene_type','delta_bits','caller','same_thread_same_scene','other_thread_same_scene'):
                natural(row,k)
            if not row['thread'] or not row['caller'] or row['scene_type']>=2**32 or row['delta_bits']>=2**32:
                raise ValueError('invalid native arguments')
            site=row['entry_rva'];collection=row['collection'];scene=row['scene']
            if site==0x29b4530:
                route={base+0x4095f50:'scene_startup',base+0x4095fc0:'scene_shutdown'}.get(collection,'other_lifecycle_collection')
                if row['delta_bits']:raise ValueError('lifecycle float argument')
            elif site==0x20113e0 or (writer_version and site in (0x21377c0,0x1f605a0)):
                route={0x10:'physics_pre_step',0x80:'physics_step'}.get(collection-scene,'other_physics_collection') if scene else 'other_physics_collection'
            else:raise ValueError('unowned physics entry')
            if row['route']!=route:raise ValueError('invalid physics route')
            if writer_version and site in (0x21377c0,0x1f605a0):
                if scene not in known_scenes or collection-scene not in (0x10,0x80):raise ValueError('writer outside observed scene filter')
                for field in ('handle_argument','receiver','method','returned_pointer','returned_handle','handle_read'):natural(row,field)
                ancestry=row.get('ancestry')
                if not isinstance(ancestry,list) or len(ancestry)!=8 or any(type(p) is not int or not 0<=p<2**64 for p in ancestry):
                    raise ValueError('invalid writer ancestry')
                writer_invalid |= not any(ancestry) or (site==0x21377c0 and row['boundary']=='return' and not row['handle_read'])
                if row['scene_type']!=2**32-1 or row['delta_bits']:raise ValueError('writer has no scene type or delta argument')
                if row['handle_read'] not in (0,1):raise ValueError('invalid output read state')
                if row['boundary']=='entry' and any(row[k] for k in ('returned_pointer','returned_handle','handle_read')):
                    raise ValueError('writer output before return')
                if site==0x1f605a0 and any(row[k] for k in ('receiver','method','returned_pointer','returned_handle','handle_read')):
                    raise ValueError('removal has no receiver or return arguments')
            elif row['boundary']=='entry' and scene and route in ('scene_startup','scene_shutdown','physics_pre_step','physics_step'):
                if len(known_scenes)<32:known_scenes.add(scene)
            threads.add(row['thread']);routes[route]=routes.get(route,0)+1
            pair=row['pair']
            if row['boundary']=='entry':
                if phase or pair!=last_pair+1:raise ValueError('entry outside phase/order')
                last_pair=pair
                same=[e for e in active.values() if e['thread']==row['thread']]
                parent=max(same,key=lambda e:e['depth']) if same else None
                if row['depth']!=(parent['depth']+1 if parent else 1) or row['parent_pair']!=(parent['pair'] if parent else 0):
                    raise ValueError('invalid physics nesting')
                for field,same_thread in (('same_thread_same_scene',True),('other_thread_same_scene',False)):
                    if row[field]!=sum(e['scene']==scene and (e['thread']==row['thread'])==same_thread for e in active.values()):
                        raise ValueError('invalid observed overlap')
                active[pair]=row
            elif row['boundary']=='return':
                entry=active.pop(pair,None)
                if entry is None or any(v!=row.get(k) for k,v in entry.items() if k not in (('boundary','time_ms','returned_pointer','returned_handle','handle_read') if site==0x21377c0 else ('boundary','time_ms'))):
                    raise ValueError('mismatched physics return')
                if any(e['parent_pair']==pair for e in active.values()):raise ValueError('parent returned before nested call')
            else:raise ValueError('invalid physics boundary')
        complete=bool(phase and phase['complete'] and not active and rows[-1] is phase
                      and key+'_raw' in report and key+'_temporary_raw' not in report)
        report[key]=dict(header=header,phase=phase,records=records,pending_pairs=len(active),
                         overflow=overflow,routes=routes,threads=sorted(threads))
        report[key+'_phase_complete']=complete
        report[key+'_requires_live_status']=not complete
    except (OSError,ValueError,RuntimeError,KeyError,TypeError,AttributeError) as error:
        report[key+'_collection_error']=str(error)


def _vfx_first_trace_writer(value):
    """Correlate retained observations, never upgrade them to a task lease."""
    witnesses = {w['pair']: w for w in value.get('registration_ancestry', [])}
    # Native event order is authoritative. Later good rows cannot repair the
    # first exact writer; scope IDs are invocation serials, not generations.
    first = next((e for e in value['events'] if e['boundary'] == 'entry'
                  and e['pair'] in witnesses and witnesses[e['pair']]['trace_start_writer']), None)
    if first is None:
        return None
    witness = witnesses[first['pair']]
    scopes = witness['scopes']
    scope = scopes[0] if len(scopes) == 1 else None
    receipt = dict(pair=first['pair'], result='no_go', reason=witness['reason'],
                   scope_id=scope['id'] if scope else None, earlier_entry_snapshots=[],
                   allocation_generation_recorded=False, hold_resume_transition_recorded=False,
                   ownership_proven=False, before_trace_effects_proven=False)
    if (scope is None or scope['kind'] != 3 or not scope['identity_matches_at_registration']
            or not scope['epoch_read'] or witness['overlap'] or witness['scopes_truncated']):
        return receipt
    for producer in value.get('producers', []):
        if (not producer['returned'] or producer.get('owners_truncated', True)
                or producer['thread'] != scope['thread'] or not producer['epoch_read']
                or producer['observed_application_epoch'] != scope['epoch']
                or not producer['entry_ms'] <= producer['return_ms'] <= first['time_ms']):
            continue
        for entry in producer.get('execution_owners', []):
            if (entry['kind'] != 2 or not entry['task_read'] or entry['id'] >= scope['id']
                    or entry['context'] != scope['task'] or entry['argument'] != scope['argument']
                    or entry['tick_owner'] != scope['owner']
                    or any(entry[k] != scope[k] for k in ('function', 'function_table',
                           'scheduled_task', 'world', 'completion', 'native_thread'))):
                continue
            receipt['earlier_entry_snapshots'].append(dict(producer_id=producer['id'],
                scope_id=entry['id'], snapshot_boundary=producer['owner_snapshot_boundary']))
    # The earlier snapshot was taken inside a producer, not at kind2 entry.
    # Neither matching bindings nor adjacent IDs records hold/retire/ABA state.
    return receipt


def _collection18_writer_receipts(value):
    """Validate bounded entry receipts, never infer absence of native writers."""
    version=value.get('collection18_writer_observation_version')
    witness=value.get('collection18_witness')
    if version is None:
        if witness is not None:raise RuntimeError('collection18 writer version missing')
        return False,False
    if type(version) is not int or version not in (1,2) or value.get('owned_bytes')!=VFX_OBSERVATION_OWNED_BYTES:
        raise RuntimeError('collection18 writer version or reservation differs')
    if witness is None:return False,False
    denied=('snapshot_complete','entry_state_witness_implemented','writer_coverage_proven',
            'allocation_generation_proven','receiver_undo_proven','native_completion_proven',
            'mutation_entry_is_write_proof','sequence_is_allocation_generation')
    flags=('returned','invalidated','reentry_observed','recursive_writer_observed','overflow',
           'unknown_writer','header_valid','pending_at_close','overlap')
    unsigned=('collection','descriptor','storage','thread','entry_sequence','return_sequence')
    if version==2:
        unsigned+=('image_base','observation_start_sequence','close_sequence','retained_writer_count',
                   'attempted_writer_count','omitted_writer_count','tracking_overflow_count',
                   'untracked_at_entry','untracked_at_close','pending_writer_count','pending_writers_at_close')
    if (not isinstance(witness,dict) or witness.get('scope')!='known_native_writers_only'
        or witness.get('selected') is not True or any(witness.get(k) is not False for k in denied)
        or any(type(witness.get(k)) is not bool for k in flags)
        or any(type(witness.get(k)) is not int or not 0<=witness[k]<2**64 for k in unsigned)
        or not witness['thread'] or not witness['entry_sequence']
        or any(type(witness.get(k)) is not int for k in ('count','capacity','recursion'))
        or not isinstance(witness.get('hub'),dict)
        or not isinstance(witness.get('writers'),list) or len(witness['writers'])>32):
        raise RuntimeError('collection18 writer witness shape differs')
    if ((witness['returned'] and witness['return_sequence']<=witness['entry_sequence'])
        or (not witness['returned'] and witness['return_sequence'])
        or (witness['header_valid'] and (not 0<=witness['count']<=witness['capacity']<=64
             or witness['recursion']!=0 or (not witness['storage'] and witness['count']>1)))
        or ((not witness['header_valid'] or any(witness[k] for k in
             ('reentry_observed','overflow','unknown_writer','overlap')) or witness['writers'])
            and not witness['invalidated'])):
        raise RuntimeError('collection18 writer boundary differs')
    if version==1:
        # Historical v1 lacks independent count/closure/association fields.
        # Retain it as incomplete evidence; never mint a new completeness claim.
        return True,False
    rows=witness['writers'];base=witness['image_base'];hub=witness['hub']
    if (not base or base>2**64-0x4438000 or not witness['observation_start_sequence']
        or witness['observation_start_sequence']>=witness['entry_sequence']
        or witness['retained_writer_count']!=len(rows)
        or witness['attempted_writer_count']!=len(rows)+witness['omitted_writer_count']
        or (witness['omitted_writer_count'] and len(rows)!=32)
        or witness['overflow']!=bool(witness['omitted_writer_count'] or witness['tracking_overflow_count'])
        or witness['untracked_at_entry']>witness['tracking_overflow_count']
        or witness['untracked_at_close']>witness['tracking_overflow_count']
        or witness['pending_writer_count']>witness['attempted_writer_count']
        or witness['pending_writers_at_close']>witness['attempted_writer_count']
        or type(hub.get('valid')) is not bool
        or any(type(hub.get(k)) is not int or not 0<=hub[k]<2**64 for k in ('address','table'))
        or any(type(hub.get(k)) is not int or not -(2**31)<=hub[k]<2**31 for k in ('index','serial'))
        or (witness['header_valid'] and (not hub['valid'] or hub['index']<0 or hub['serial']<=0
             or hub['address']!=witness['collection']-0x810 or hub['table']!=base+0x337c2c8
             or (witness['storage'] or witness['collection'])>2**64-4096))):
        raise RuntimeError('collection18 counts or hub identity differ')
    sites={0x3a3b70,0x3ca7a0,0x43d210,0x399df0,0x3a1910,0x2050570,0x3ae520,
           0x912df0,0x2050940,0x3a1c50,0x3a22d0,0x17dfea0,0x399420,0x3a1a70}
    pairs={};sequences={witness['entry_sequence'],witness['observation_start_sequence']}
    if witness['return_sequence']:sequences.add(witness['return_sequence'])
    close=witness['close_sequence']
    if close:
        if close in sequences or close<=witness['entry_sequence']:raise RuntimeError('collection18 close sequence differs')
        sequences.add(close)
    for w in witness['writers']:
        if (not isinstance(w,dict) or any(type(w.get(k)) is not int or not 0<=w[k]<2**64
            for k in ('pair','parent_pair','entry_rva','entry_sequence','return_sequence',
                      'object','argument','caller','thread','raw_result'))
            or any(type(w.get(k)) is not bool for k in ('returned','preexisting','association_unknown'))
            or w['pair']!=len(pairs)+1 or w['entry_rva'] not in sites
            or not w['caller'] or not w['thread'] or not w['entry_sequence']
            or w['entry_sequence'] in sequences
            or (w['returned'] and (w['return_sequence']<=w['entry_sequence'] or w['return_sequence'] in sequences))
            or (not w['returned'] and w['return_sequence'])
            or (w['preexisting']!=(w['entry_sequence']<witness['entry_sequence']))
            or (witness['returned'] and not w['preexisting'] and w['entry_sequence']>=witness['return_sequence'])):
            raise RuntimeError('collection18 writer pair differs')
        parent=pairs.get(w['parent_pair'])
        if w['preexisting'] and w['returned'] and w['return_sequence']<=witness['entry_sequence']:
            raise RuntimeError('collection18 preexisting writer does not overlap')
        if w['thread']==witness['thread'] and witness['returned']:
            if ((w['preexisting'] and w['returned'] and w['return_sequence']<=witness['return_sequence'])
                or (not w['preexisting'] and (not w['returned'] or w['return_sequence']>=witness['return_sequence']))):
                raise RuntimeError('collection18 same-thread interval differs')
        ancestors=[p for p in pairs.values() if p['thread']==w['thread']
                   and p['entry_sequence']<w['entry_sequence']
                   and (not p['returned'] or p['return_sequence']>w['entry_sequence'])]
        expected_parent=max(ancestors,key=lambda p:p['entry_sequence'])['pair'] if ancestors else 0
        if w['parent_pair']!=expected_parent:raise RuntimeError('collection18 writer ancestry differs')
        if (w['parent_pair'] and (parent is None or parent['thread']!=w['thread']
            or parent['entry_sequence']>=w['entry_sequence'] or
            (parent['returned'] and (not w['returned'] or parent['return_sequence']<=w['return_sequence'])))):
            raise RuntimeError('collection18 writer parent differs')
        if w['entry_rva'] in (0x3a22d0,0x17dfea0,0x399420,0x3a1a70):
            storage=witness['storage'] or witness['collection']
            matched=(witness['header_valid'] and 0<=w['object']-storage<witness['count']*0x40
                     and (w['object']-storage)%0x40==0)
            if w['association_unknown']==matched:
                raise RuntimeError('collection18 row association differs')
        else:
            expected=witness['collection']-(0x810 if w['entry_rva']==0x912df0 else 0)
            if w['object']!=expected or w['association_unknown']:
                raise RuntimeError('collection18 writer collection identity differs')
        pairs[w['pair']]=w;sequences.add(w['entry_sequence'])
        if w['return_sequence']:sequences.add(w['return_sequence'])
    recursive=any(w['thread']==witness['thread'] and not w['preexisting'] for w in pairs.values())
    if ((recursive and not witness['recursive_writer_observed'])
        or (not witness['overflow'] and witness['recursive_writer_observed']!=recursive)
        or (any(w['association_unknown'] for w in pairs.values()) and not witness['unknown_writer'])
        or (any(w['thread']!=witness['thread'] or w['preexisting'] for w in pairs.values())
            and not witness['overlap'])):
        raise RuntimeError('collection18 writer correlation differs')
    if any(w['entry_rva']==0x43d210 and w['argument']!=base+0x3c5360 for w in pairs.values()) and not witness['unknown_writer']:
        raise RuntimeError('collection18 unknown callable differs')
    pending=sum(not w['returned'] for w in rows)
    pending_at_close=sum(w['entry_sequence']<close and (not w['returned'] or w['return_sequence']>close) for w in rows) if close else 0
    if (not pending<=witness['pending_writer_count']<=pending+witness['omitted_writer_count']
        or not pending_at_close<=witness['pending_writers_at_close']<=pending_at_close+witness['omitted_writer_count']
        or (not close and (witness['pending_writers_at_close'] or witness['untracked_at_close']))
        or witness['pending_at_close']!=bool(close and (
            not witness['returned'] or witness['return_sequence']>close
            or witness['pending_writers_at_close'] or witness['untracked_at_close']))
        or (isinstance(value.get('phase'),dict) and (value['phase'].get('state')=='closed')!=bool(close))):
        raise RuntimeError('collection18 closure interval differs')
    complete=(witness['returned'] and all(w['returned'] for w in pairs.values())
              and not witness['overflow'] and not witness['pending_at_close'] and not witness['pending_writer_count']
              and not value.get('persistence_failed'))
    return True,complete


def _collection18_material_census(value):
    """Validate entry copies only; never upgrade the existing writer/recovery scope."""
    version = value.get('collection18_material_observation_version')
    witness = value.get('collection18_witness')
    census = witness.get('material_provider_census') if isinstance(witness, dict) else None
    if version is None:
        if census is not None:
            raise RuntimeError('collection18 material version missing')
        return None
    if type(version) is not int or version not in (1, 2, 3) or value.get('collection18_writer_observation_version') != 2:
        raise RuntimeError('collection18 material version differs')
    if witness is None:
        return None

    def reject(reason):
        raise RuntimeError('collection18 material '+reason)

    def integer(v, low=0, high=2**64-1):
        return type(v) is int and low <= v <= high

    bucket_limit = 1 if version == 1 else 128
    if (not isinstance(census, dict) or type(census.get('version')) is not int or census['version'] != version
        or census.get('scope') != 'entry_visible_providers' or census.get('covers_native_start_births') is not False
        or type(census.get('registry_bucket_limit')) is not int or census['registry_bucket_limit'] != bucket_limit
        or type(census.get('entry_copy_valid')) is not bool
        or not integer(census.get('owned_bytes'), 8192, 256*1024)
        or not isinstance(census.get('failure'), str)
        or not re.fullmatch('[a-z_]{0,64}', census['failure'])
        or any(not isinstance(census.get(k), list) for k in ('listeners', 'providers', 'slots'))):
        reject('shape differs')
    # Version1 witnessed only a single bucket. Version2 names the actual count;
    # the capacity limit alone cannot establish a world's hash mask. Version3
    # corrects the receiver class, retaining this same registry contract.
    if ('registry_bucket_count' in census) != (version >= 2):
        reject('registry bucket count version differs')
    buckets = 1 if version == 1 else census['registry_bucket_count']
    if version >= 2:
        if census['entry_copy_valid']:
            if not integer(buckets, 1, bucket_limit) or buckets & (buckets-1):
                reject('registry bucket count differs')
        elif buckets is not None:
            reject('invalid copy retains registry bucket count')
    listeners, providers, slots = (census[k] for k in ('listeners', 'providers', 'slots'))
    if len(listeners) > 64 or len(providers) > 128 or len(slots) > 1024:
        reject('capacity differs')
    if not census['entry_copy_valid']:
        if not census['failure'] or census.get('descriptor_fields') is not None or listeners or providers or slots:
            reject('invalid copy retains valid contents')
        if census['failure'] == 'pending' and witness['returned']:
            reject('returned copy is pending')
        return census
    if (census['failure'] or not witness['header_valid']
        or any(witness[k] for k in ('invalidated', 'reentry_observed', 'overflow', 'overlap',
                                   'pending_at_close', 'unknown_writer', 'writers'))
        or len(listeners) != witness['count']):
        reject('valid copy conflicts with occurrence')
    d = census.get('descriptor_fields')
    if (not isinstance(d, dict)
        or set(d) != {'source_player_index', 'trace_parts_id', 'packed_life', 'life_stop',
                      'length', 'trace_kind_id', 'clut_id', 'brightness'}
        or not integer(d.get('source_player_index'), 0, 7)
        or any(not integer(d.get(k), 0, 255) for k in ('trace_parts_id', 'trace_kind_id', 'clut_id'))
        or any(not integer(d.get(k), -(2**31), 2**31-1) for k in ('packed_life', 'length'))
        or type(d.get('life_stop')) is not bool
        or type(d.get('brightness')) not in (int, float)
        or abs(d['brightness']) > 3.4028234663852886e38 or not math.isfinite(d['brightness'])):
        reject('descriptor differs')
    identities, indices = {}, {}

    def identity(o, nullable=False, table=None):
        if not isinstance(o, dict) or set(o) != {'address', 'table', 'index', 'serial', 'valid'}:
            reject('identity shape differs')
        if nullable and o == {'address': 0, 'table': 0, 'index': -1, 'serial': 0, 'valid': False}:
            if any(type(o[k]) is not int for k in ('address', 'table', 'index', 'serial')) or type(o['valid']) is not bool:
                reject('null identity types differ')
            return
        if (not integer(o['address'], 1) or not integer(o['table'], 1)
            or not integer(o['index'], 0, 2**31-1) or not integer(o['serial'], 1, 2**31-1)
            or o['valid'] is not True or (table is not None and o['table'] != table)):
            reject('identity differs')
        if identities.setdefault(o['address'], o) != o or indices.setdefault(o['index'], o) != o:
            reject('identity generation conflicts')

    def header(h, stride):
        if (not isinstance(h, dict) or set(h) != {'data', 'count', 'capacity'}
            or not integer(h.get('count'), 0, 1024) or not integer(h.get('capacity'), h['count'], 1024)
            or not integer(h.get('data'), int(bool(h['capacity'])), 2**64-1-h['capacity']*stride)):
            reject('array shape differs')

    def registry_bucket(world):
        # Native142018870 mixer, with explicit uint32 arithmetic in Python.
        mask = 0xffffffff
        i = (world >> 4) & mask
        a = ((0x9e3779b9-i) ^ (i << 8)) & mask
        b = (-(a+i) ^ (a >> 13)) & mask
        c = ((i-a-b) ^ (b >> 12)) & mask
        a = ((a-c-b) ^ (c << 16)) & mask
        b = ((b-a-c) ^ (a >> 5)) & mask
        c = ((c-a-b) ^ (b >> 3)) & mask
        a = ((a-c-b) ^ (c << 10)) & mask
        return ((b-a-c) ^ (a >> 15)) & (buckets-1)

    base = witness['image_base']
    expected_providers = []
    registry_rows, worlds = {}, {}
    registry_backing = None
    for row_index, l in zip(range(witness['count']-1, -1, -1), listeners):
        if (not isinstance(l, dict) or type(l.get('row_index')) is not int or l['row_index'] != row_index
            or not integer(l.get('handle'), 1)
            or type(l.get('registry')) is not int or l['registry'] != base+0x406e4e0
            or not integer(l.get('registry_row'), 1, 2**64-0x18)
            or not integer(l.get('registry_row_index'), 0, 63)
            or not integer(l.get('registry_bucket'), 0, buckets-1)
            or not isinstance(l.get('trace_counts'), list) or len(l['trace_counts']) != 2
            or any(not integer(n, 0, 128) for n in l['trace_counts'])):
            reject('listener binding differs')
        for key, table in (('receiver', base+(0x326b2e0 if version >= 3 else 0x3268078)), ('world', None),
                           ('battle_manager', base+0x327aa20), ('selected_fighter', base+0x3268078),
                           ('trace_manager', None), ('trace_root', base+0x3360ca8)):
            identity(l.get(key), table=table)
        if l['registry_bucket'] != registry_bucket(l['world']['address']):
            reject('world registry bucket differs')
        backing = l['registry_row']-l['registry_row_index']*0x18
        if backing <= 0 or (registry_backing is not None and backing != registry_backing):
            reject('registry backing differs')
        registry_backing = backing
        binding = (l['world'], l['battle_manager'])
        if registry_rows.setdefault(l['registry_row_index'], binding) != binding:
            reject('registry row conflicts')
        if worlds.setdefault(l['world']['address'], l['registry_row_index']) != l['registry_row_index']:
            reject('world registry is ambiguous')
        for offset, count in zip((0x418, 0x428), l['trace_counts']):
            expected_providers.extend((row_index, offset, n) for n in range(count))
    if len(expected_providers) != len(providers):
        reject('provider census is incomplete')
    cursor = 0
    for expected, p in zip(expected_providers, providers):
        binding_keys = ('listener_row_index', 'trace_array_offset', 'trace_ref_index')
        if (not isinstance(p, dict) or any(type(p.get(k)) is not int for k in binding_keys)
            or tuple(p[k] for k in binding_keys) != expected
            or not integer(p.get('controller'), 1, 2**64-0x109)
            or type(p.get('state')) is not int or p['state'] != p['controller']+0x10
            or not integer(p.get('slot_begin'), cursor, cursor) or not integer(p.get('slot_count'), 0, 1024)):
            reject('provider order or controller differs')
        identity(p.get('trace_actor'), table=base+0x3361660)
        identity(p.get('provider'), table=base+0x38829c0)
        identity(p.get('asset'), nullable=True)
        header(p.get('overrides'), 8)
        header(p.get('materials'), 0x30)
        if (p['slot_count'] != p['materials']['count'] or cursor+p['slot_count'] > len(slots)
            or (not p['asset']['address'] and p['materials'] != {'data': 0, 'count': 0, 'capacity': 0})):
            reject('provider material count differs')
        for slot_index in range(p['slot_count']):
            s = slots[cursor]
            if (not isinstance(s, dict) or type(s.get('slot_index')) is not int or s['slot_index'] != slot_index
                or any(s.get(k) != p[k] for k in (*binding_keys, 'state', 'controller',
                                                'trace_actor', 'provider', 'asset'))
                or any(type(s.get(k)) is not int for k in (*binding_keys, 'state', 'controller'))
                or s.get('source') not in ('override', 'asset')):
                reject('ordered slot differs')
            for key in ('trace_actor', 'provider', 'asset'):
                identity(s.get(key), nullable=key == 'asset')
            identity(s.get('material'), nullable=True)
            if s['source'] == 'override' and (slot_index >= p['overrides']['count'] or not s['material']['address']):
                reject('override selection differs')
            cursor += 1
    if cursor != len(slots):
        reject('slot census has extra rows')
    return census



def _collection18_zero_serial_diagnostic(value):
    """Read entry-local pointer samples; never return a provider/lease census."""
    witness = value.get('collection18_witness')
    diagnostic = witness.get('zero_serial_diagnostic') if isinstance(witness, dict) else None
    if diagnostic is None:
        return None
    def require(condition):
        if not condition:
            raise RuntimeError('VFX zero-serial diagnostic schema or association differs')
    def integer(n, lo=0, hi=(1 << 64)-1):
        return type(n) is int and lo <= n <= hi
    require(isinstance(diagnostic, dict))
    version = diagnostic.get('version')
    require(type(version) is int and version in (1, 2, 3, 4) and diagnostic.get('scope') == 'zero_serial_entry_pointer_samples')
    # Preserve old observations under their original class policy; do not turn
    # the measured version2 TraceEventHandler rejection into a new admission.
    require((version >= 3) == (value.get('collection18_material_observation_version') == 3))
    receiver_table = 0x326b2e0 if version >= 3 else 0x3268078
    require(diagnostic.get('generation_unknown') is True and diagnostic.get('total_retained_native_bytes') is None)
    for key in ('provider_census_valid', 'ownership_permission', 'resource_completion_proven',
                'rollback_admission', 'writer_exclusion_proven', 'covers_native_start_births'):
        require(diagnostic.get(key) is False)
    for key in ('entry_sequence', 'thread', 'collection', 'descriptor'):
        require(integer(diagnostic.get(key), 1) and diagnostic[key] == witness.get(key))
    require(witness.get('header_valid') is False and witness.get('hub', {}).get('serial') == 0)
    require(witness.get('material_provider_census', {}).get('entry_copy_valid') is False)
    # Version4 expands only the bounded shape detail; historical limits retain
    # their original meaning, including the live version3 capacity rejection.
    row_limit = 192 if version >= 4 else 16
    require(type(diagnostic.get('row_limit')) is int and diagnostic['row_limit'] == row_limit
            and isinstance(diagnostic.get('rows'), list) and len(diagnostic['rows']) <= row_limit)
    require(diagnostic.get('status') in ('sampled', 'rejected', 'pending') and isinstance(diagnostic.get('failure'), str))
    sampled = diagnostic['status'] == 'sampled'
    require(diagnostic.get('entry_samples_match') is sampled and bool(diagnostic['failure']) is not sampled)
    if version == 1:
        require('rejected_type' not in diagnostic)  # Never upgrade a legacy receipt.
    else:
        require('rejected_type' in diagnostic)
        detail = diagnostic['rejected_type']
        subject = {'listener_receiver_vtable': 'listener_receiver',
                   'selected_fighter_vtable': 'selected_fighter'}.get(diagnostic['failure'])
        if diagnostic['status'] != 'rejected' or subject is None:
            require(detail is None)
        else:
            require(isinstance(detail, dict) and set(detail) == {'scope', 'subject', 'listener_row_index',
                    'object_index', 'object_serial', 'vtable_rva', 'image_membership_proven'})
            require(detail['scope'] == 'single_entry_sample' and detail['subject'] == subject
                    and detail['image_membership_proven'] is False)
            require(integer(detail['listener_row_index'], 0, 63) and integer(detail['object_index'], 0, 0x7fffffff)
                    and integer(detail['object_serial'], int(subject == 'listener_receiver'), 0x7fffffff))
            require(detail['vtable_rva'] is None or (integer(detail['vtable_rva'], 0, 0xffffffff)
                    and detail['vtable_rva'] != (receiver_table if subject == 'listener_receiver' else 0x3268078)))
            require(not any(witness.get(k) for k in ('reentry_observed', 'overflow', 'unknown_writer', 'overlap',
                    'pending_at_close', 'attempted_writer_count', 'pending_writer_count', 'untracked_at_entry')))
    if not sampled:
        require(not diagnostic['rows'] and all(diagnostic.get(k) is None for k in
                ('hub', 'collection_header', 'descriptor_fields')))
        if diagnostic['failure'] == 'detail_capacity':
            require(integer(diagnostic.get('matched_count'), row_limit+1, 1024)
                    and integer(diagnostic.get('omitted_count'), 1, 1024)
                    and diagnostic['omitted_count'] == diagnostic['matched_count']-row_limit)
        return diagnostic
    require(not any(witness.get(k) for k in ('reentry_observed', 'overflow', 'unknown_writer',
            'overlap', 'pending_at_close', 'attempted_writer_count', 'untracked_at_entry')))
    require(diagnostic.get('matched_count') == len(diagnostic['rows']) and diagnostic.get('omitted_count') == 0)
    aliases = {}
    def pointer(obj, table=None, weak=False):
        require(isinstance(obj, dict) and set(obj) == {'address', 'table', 'index', 'serial', 'flags',
                                                      'indexed_pointer_checked', 'generation_unknown'})
        require(obj['indexed_pointer_checked'] is True and obj['generation_unknown'] is True)
        require(integer(obj['address'], 1) and integer(obj['table'], 1)
                and integer(obj['index'], 0, 0x7fffffff) and integer(obj['serial'], int(weak), 0x7fffffff)
                and integer(obj['flags'], 0, 0xffffffff) and not (obj['flags'] & 0x18000))
        if table is not None:
            require(obj['table'] == witness['image_base']+table)
        for key in (('address', obj['address']), ('index', obj['index'])):
            require(key not in aliases or aliases[key] == obj)
            aliases[key] = obj
    def header(h, limit, stride, inline_collection=None):
        require(isinstance(h, dict) and set(h) == {'data', 'count', 'capacity'})
        require(integer(h['count'], 0, limit) and integer(h['capacity'], h['count'], limit)
                and integer(h['data']))
        if inline_collection is not None and h['data'] == 0:
            require((h['count'], h['capacity']) in ((0, 0), (1, 1))
                    and integer(inline_collection, 1, (1 << 64)-1-0x40))
        else:
            require((not h['capacity'] or h['data'] > 0)
                    and h['data'] <= (1 << 64)-1-h['capacity']*stride)
    pointer(diagnostic['hub'], 0x337c2c8)
    require(diagnostic['hub']['address'] == witness['collection']-0x810 and diagnostic['hub']['serial'] == 0)
    require(all(diagnostic['hub'][k] == witness['hub'].get(k) for k in ('address','index','serial','table')))
    header(diagnostic['collection_header'], 64, 0x40, inline_collection=witness['collection'])
    d = diagnostic['descriptor_fields']
    require(isinstance(d, dict) and set(d) == {'player','parts','life','stop','length','kind','clut','brightness_bits'})
    require(integer(d['player'], 0, 7) and integer(d['stop'], 0, 1)
            and all(integer(d[k], 0, 255) for k in ('parts','kind','clut'))
            and all(integer(d[k], -(1 << 31), (1 << 31)-1) for k in ('life','length'))
            and integer(d['brightness_bits'], 0, 0xffffffff) and d['brightness_bits'] & 0x7f800000 != 0x7f800000)
    previous_occurrence = (-1, -1)
    for row in diagnostic['rows']:
        require(isinstance(row, dict))
        for name, table in (('receiver', receiver_table), ('level', None), ('world', None), ('battle_manager', 0x327aa20),
                            ('fighter', 0x3268078), ('trace_manager', None), ('trace_root', 0x3360ca8),
                            ('trace_actor', 0x3361660), ('provider', 0x38829c0), ('asset', None), ('material', 0x391ee70)):
            pointer(row.get(name), table, weak=name == 'receiver')
        h = diagnostic['collection_header']
        require(integer(row.get('listener_row_index'), 0, h['count']-1)
                and row.get('listener_row') == (h['data'] or witness['collection'])+row['listener_row_index']*0x40
                and integer(row.get('callable'), 1) and integer(row.get('handle'), 1))
        require(row.get('registry') == witness['image_base']+0x406e4e0
                and integer(row.get('registry_row_index'), 0, 63) and integer(row.get('registry_row'), 1)
                and integer(row.get('registry_bucket_count'), 1, 128)
                and row['registry_bucket_count'] & (row['registry_bucket_count']-1) == 0
                and integer(row.get('registry_bucket'), 0, row['registry_bucket_count']-1))
        require(row.get('trace_array_offset') in (0x418, 0x428) and integer(row.get('trace_ref_index'), 0, 127)
                and integer(row.get('provider_index'), 0, 127) and integer(row.get('controller'), 1)
                and row.get('state') == row['controller']+0x10)
        header(row.get('overrides'), 1024, 8); header(row.get('materials'), 1024, 0x30)
        require(integer(row.get('slot_index'), 0, min(row['overrides']['count'], row['materials']['count'])-1))
        if version >= 4:
            occurrence = (row['provider_index'], row['slot_index'])
            require(occurrence > previous_occurrence)  # No alias deduplication or row reordering.
            previous_occurrence = occurrence
        header(row.get('vector_array'), 0x7fffffff, 0x28)
        vector = row['vector_array']
        require(bool(vector['data']) == bool(vector['capacity']) and vector['data'] % 8 == 0
                and row.get('vector_payload_capacity_bytes') == vector['capacity']*0x28)
        for key, count in (('proxy_occupied', 3), ('cache_occupied', 12)):
            require(isinstance(row.get(key), list) and len(row[key]) == count and all(type(v) is bool for v in row[key]))
    return diagnostic


class _C18DiagnosticObserved(Exception):
    """Private stop signal for an ordinary-forward observation, not a failure/recovery."""
    def __init__(self, receipt):
        self.receipt = receipt


C18_COMBAT_MARKER_FIELDS=('entry_sequence','return_sequence','thread','collection','descriptor',
                         'arm_sequence','arm_frame','arm_epoch','manager','player','selected_frame',
                         'selected_epoch','selected_round_frame','observed_frame','observed_epoch')


def c18_diagnostic_return_marker(tail, run_id):
    keys=C18_COMBAT_MARKER_FIELDS
    pattern=(r"\[ReplayQualification\] C18 combat diagnostic return run_id="+re.escape(run_id)
             + ''.join(' '+key+r'=(\d+)' for key in keys)
             +r" ownership_proven=false completion_proven=false\r?(?:\n|$)")
    rows=re.findall(pattern,tail)
    if not rows:
        return None
    if len(rows)!=1:
        raise RuntimeError('C18 diagnostic return marker is ambiguous')
    receipt=dict(zip(keys,map(int,rows[0])))
    _validate_c18_combat_marker(receipt)
    return receipt


def _validate_c18_combat_marker(receipt):
    if (any(type(receipt.get(k)) is not int or receipt[k]<=0 for k in C18_COMBAT_MARKER_FIELDS)
        or not receipt['arm_sequence']<receipt['entry_sequence']<receipt['return_sequence']
        or receipt['arm_frame']!=170
        or not 170<=receipt['selected_frame']<=220
        or not receipt['selected_frame']<=receipt['observed_frame']<=221
        or not receipt['arm_epoch']<=receipt['selected_epoch']<=receipt['observed_epoch']):
        raise RuntimeError('C18 diagnostic return marker coordinates differ')


def c18_diagnostic_stop_receipt(path, run_id, objects, expected):
    """After owned process exit, bind the retained publication to the stop marker."""
    with path.open('rb') as source:
        raw=source.read(VFX_OBSERVATION_SIDECAR_BYTES+1)
    if len(raw) > VFX_OBSERVATION_SIDECAR_BYTES:
        raise RuntimeError('C18 diagnostic sidecar exceeds its bound')
    try:
        value = json.loads(raw)
    except (ValueError, UnicodeError) as error:
        raise RuntimeError('C18 diagnostic publication is unreadable') from error
    if not isinstance(value, dict) or value.get('run_id') != run_id:
        raise RuntimeError('C18 diagnostic run identity differs')
    witness = value.get('collection18_witness')
    if not isinstance(witness, dict) or not witness.get('selected') or not witness.get('returned'):
        raise RuntimeError('C18 diagnostic selected return is missing')
    if any(witness.get(k)!=expected.get(k) for k in ('entry_sequence','return_sequence','thread','collection','descriptor')):
        raise RuntimeError('C18 diagnostic sidecar differs from native stop marker')
    selection=value.get('collection18_combat_selection')
    if selection is not None or 'arm_sequence' in expected:
        # Legacy artifacts remain readable offline; the live parser accepts only
        # the combat marker. A new artifact cannot be paired with a setup marker.
        required=('arm_sequence','arm_frame','arm_epoch','manager','player','selected_frame',
                  'selected_epoch','selected_round_frame','observed_frame','observed_epoch')
        if any(key not in expected for key in required):
            raise RuntimeError('C18 combat stop marker missing')
        _validate_c18_combat_marker(expected)
        if (not isinstance(selection,dict) or selection.get('version')!=1
            or selection.get('scope')!='trajectory_armed_entry_observation'
            or selection.get('armed') is not True or selection.get('invalidated') is not False
            or selection.get('failure') is not None
            or selection.get('ownership_proven') is not False or selection.get('completion_proven') is not False
            or any(type(selection.get(k)) is not int or selection[k]!=expected[k]
                   for k in ('thread',)+required[:-2])
            or not 0<witness.get('observation_start_sequence',0)<selection['arm_sequence']):
            raise RuntimeError('C18 combat sidecar differs from native stop marker')
    _collection18_writer_receipts(value)
    census = _collection18_material_census(value)
    diagnostic = _collection18_zero_serial_diagnostic(value)
    if 'zero_serial_diagnostic' not in witness or census is None:
        raise RuntimeError('C18 diagnostic source schema missing')
    from .replay_evidence import store_bytes
    return dict(entry_sequence=witness['entry_sequence'], return_sequence=witness['return_sequence'],
                thread=witness['thread'], collection=witness['collection'], descriptor=witness['descriptor'],
                zero_serial_status=diagnostic['status'] if diagnostic else 'not_applicable',
                scope='published callback return only; no native quiescence, ownership or recovery',
                combat_selection=selection,
                phase_complete=False, raw=store_bytes(objects, raw, suffix='.json'))


def _collection18_start_helper_correlation(value):
    """Optional raw call receipt; never upgrades C18 census/ownership/phase gates."""
    witness = value.get('collection18_witness')
    receipt = witness.get('start_helper_correlation') if isinstance(witness, dict) else None
    if receipt is None:
        return None  # Historical receipts and the pre-native sampling publication.
    def uint(v, limit=2**64):
        return type(v) is int and 0 <= v < limit
    def require(ok):
        if not ok:
            raise RuntimeError('collection18 Start/helper correlation differs')
    require(isinstance(receipt, dict) and type(receipt.get('version')) is int and receipt['version'] == 1
            and receipt.get('scope') == 'selected_c18_broadcast_native_interval')
    require(all(receipt.get(k) is False for k in
                ('ownership_proven', 'mid_writes_observed', 'entry_persisted_before_native'))
            and all(k in receipt and receipt[k] is None for k in
                    ('mid_slot', 'native_tick', 'provider_body', 'color_source', 'callback_listener_ordinal')))
    flags = ('ended', 'active', 'complete', 'guard_ready', 'guard_lost', 'preexisting', 'invalidated', 'entry_census_valid')
    require(all(type(receipt.get(k)) is bool for k in flags)
            and all(uint(receipt.get(k)) for k in
                    ('c18_entry_sequence', 'thread', 'attempted', 'omitted', 'foreign_entries', 'pending')))
    require(receipt['c18_entry_sequence'] == witness['entry_sequence']
            and receipt['thread'] == witness['thread'] and receipt['ended'] != receipt['active'])
    rows = receipt.get('calls')
    require(isinstance(rows, list) and len(rows) <= 64
            and receipt['attempted'] == len(rows) + receipt['omitted']
            and (not receipt['omitted'] or len(rows) == 64)
            and receipt['foreign_entries'] <= receipt['attempted'])
    pairs, sequences, families = {}, set(), {'start': 0, 'helper': 0}
    census = witness.get('material_provider_census', {})
    diagnostic = witness.get('zero_serial_diagnostic') or {}
    # The independently validated entry census can be the separate zero-serial
    # diagnostic. Raw receipt completeness does not require a census match.
    entry_valid = bool(census.get('entry_copy_valid')) or bool(diagnostic.get('entry_samples_match'))
    require(receipt['entry_census_valid'] == entry_valid)
    for ordinal, row in enumerate(rows, 1):
        require(isinstance(row, dict) and row.get('family') in families)
        require(all(uint(row.get(k)) for k in ('id', 'parent_id', 'start_id', 'ordinal', 'family_ordinal',
                    'entry_sequence', 'return_sequence', 'thread', 'depth', 'caller', 'component', 'request',
                    'actor', 'provider', 'fname', 'color_pointer'))
                and all(type(row.get(k)) is bool for k in ('returned', 'on_selected_thread', 'provider_read',
                    'provider_link_matches', 'color_read', 'color_stable')))
        bits = row.get('color_bits')
        require(isinstance(bits, list) and len(bits) == 4 and all(uint(b, 2**32) for b in bits))
        families[row['family']] += 1
        require(row['ordinal'] == ordinal and row['family_ordinal'] == families[row['family']]
                and row['id'] > 0 and row['id'] not in pairs
                and row['thread'] > 0 and row['on_selected_thread'] == (row['thread'] == receipt['thread'])
                and row['entry_sequence'] > 0 and row['entry_sequence'] not in sequences
                and (not sequences or row['entry_sequence'] > max(p['entry_sequence'] for p in pairs.values())))
        sequences.add(row['entry_sequence'])
        require((row['returned'] and row['return_sequence'] > row['entry_sequence']
                 and row['return_sequence'] not in sequences)
                or (not row['returned'] and row['return_sequence'] == 0))
        if row['returned']:
            sequences.add(row['return_sequence'])
        parent = pairs.get(row['parent_id'])
        ancestors = [p for p in pairs.values() if p['thread'] == row['thread']
                     and (not p['returned'] or p['return_sequence'] > row['entry_sequence'])]
        if ancestors:
            require(row['parent_id'] == ancestors[-1]['id'])
        elif not receipt['preexisting'] and not receipt['guard_lost'] and not receipt['invalidated']:
            require(row['parent_id'] == 0)
        if parent:
            require(parent['thread'] == row['thread'] and parent['depth']+1 == row['depth']
                    and (not parent['returned'] or (row['returned'] and row['return_sequence'] < parent['return_sequence'])))
        elif row['parent_id']:
            require(receipt['preexisting'] or receipt['guard_lost'] or receipt['invalidated'])
        if row['family'] == 'start':
            require(row['start_id'] == row['id'] and not any(row[k] for k in
                    ('actor', 'provider', 'fname', 'color_pointer', 'color_read', 'color_stable', 'provider_read', 'provider_link_matches')))
        else:
            start = pairs.get(row['start_id'])
            require(not row['component'] and not row['request']
                    and (not row['color_stable'] or row['color_read'])
                    and (not row['provider_link_matches'] or (row['provider_read'] and row['provider'] and row['actor'])))
            if start:
                starts = [p for p in ancestors if p['family'] == 'start']
                require(bool(starts) and start == starts[-1])
            else:
                require(receipt['preexisting'] or receipt['guard_lost'] or receipt['invalidated'])
        for key in ('entry_listener_matches', 'entry_provider_matches'):
            matches = row.get(key)
            require(isinstance(matches, list) and all(uint(n, 128 if key == 'entry_provider_matches' else 64) for n in matches)
                    and matches == sorted(set(matches)) and (not matches or entry_valid))
        pairs[row['id']] = row
    require(receipt['pending'] == sum(not r['returned'] for r in rows))
    require(sum(not r['on_selected_thread'] for r in rows) <= receipt['foreign_entries']
            <= sum(not r['on_selected_thread'] for r in rows) + receipt['omitted'])
    complete = (receipt['ended'] and not receipt['active'] and receipt['guard_ready']
                and not any(receipt[k] for k in ('guard_lost', 'preexisting', 'invalidated', 'omitted', 'foreign_entries', 'pending')))
    require(receipt['complete'] == complete)
    if complete:
        require(all(r['family'] == 'start' or r['color_stable'] for r in rows))
    material = receipt.get('material_calls')
    if material is not None:
        require(isinstance(material, dict) and type(material.get('version')) is int and material['version'] == 1
                and type(material.get('capacity')) is int and material['capacity'] == 128)
        require(all(material.get(k) is False for k in ('count_calls_observed', 'ownership_proven',
                    'vector_row_write_proven', 'concurrent_mutation_excluded'))
                and all(k in material and material[k] is None for k in ('material_source', 'native_tick')))
        require(all(type(material.get(k)) is bool for k in ('ready', 'invalidated', 'reentry', 'complete'))
                and all(uint(material.get(k)) for k in ('attempted', 'omitted', 'foreign_entries', 'pending')))
        require(not any(material[k] for k in ('reentry', 'omitted', 'foreign_entries')) or material['invalidated'])
        calls = material.get('calls')
        require(isinstance(calls, list) and len(calls) <= 128
                and material['attempted'] == len(calls)+material['omitted']
                and (not material['omitted'] or len(calls) == 128))
        children = {}
        for ordinal, r in enumerate(calls, 1):
            require(isinstance(r, dict) and r.get('family') in ('get', 'set'))
            require(all(uint(r.get(k)) for k in ('id', 'helper_id', 'start_id', 'parent_call_id', 'ordinal',
                        'entry_sequence', 'return_sequence', 'thread', 'caller', 'caller_rva', 'wrapper_caller',
                        'wrapper_caller_rva', 'provider', 'mid', 'result', 'getter_id', 'fname', 'color_pointer'))
                    and all(type(r.get(k)) is bool for k in ('returned', 'route_verified', 'getter_linked',
                        'color_read', 'color_stable'))
                    and type(r.get('slot')) is int and -(2**31) <= r['slot'] < 2**31)
            require(isinstance(r.get('color_bits'), list) and len(r['color_bits']) == 4
                    and all(uint(b, 2**32) for b in r['color_bits']))
            require(r['ordinal'] == ordinal and r['id'] == r['entry_sequence'] > 0
                    and r['id'] not in sequences and r['id'] not in children
                    and (not children or r['id'] > max(children)))
            sequences.add(r['entry_sequence'])
            require((r['returned'] and r['return_sequence'] > r['entry_sequence']
                     and r['return_sequence'] not in sequences)
                    or (not r['returned'] and r['return_sequence'] == 0))
            if r['returned']:
                sequences.add(r['return_sequence'])
            helper, start = pairs.get(r['helper_id']), pairs.get(r['start_id'])
            require(helper is not None and helper['family'] == 'helper'
                    and r['start_id'] == helper['start_id'] and r['thread'] == helper['thread'] == receipt['thread']
                    and helper['entry_sequence'] < r['entry_sequence']
                    and (not helper['returned'] or (r['returned'] and r['return_sequence'] < helper['return_sequence'])))
            active = [p for p in children.values() if not p['returned'] or p['return_sequence'] > r['entry_sequence']]
            require(r['parent_call_id'] == (active[-1]['id'] if active else 0))
            if active:
                require(material['reentry'] and material['invalidated'])
                parent = active[-1]
                require(not parent['returned'] or (r['returned'] and r['return_sequence'] < parent['return_sequence']))
            require(not r['color_stable'] or r['color_read'])
            if r['family'] == 'get':
                require(not any(r[k] for k in ('mid', 'getter_id', 'getter_linked', 'fname', 'color_pointer',
                            'color_read', 'color_stable', 'wrapper_caller', 'wrapper_caller_rva'))
                        and not any(r['color_bits']))
                if r['route_verified']:
                    require(r['caller_rva'] == 0x8d5892 and r['provider'] == helper['provider'] and r['slot'] >= 0)
                    previous = [p for p in children.values() if p['family'] == 'get' and p['helper_id'] == r['helper_id']]
                    require(r['slot'] == len(previous))
            else:
                require(not r['provider'] and not r['result'])
                if r['route_verified']:
                    require(r['caller_rva'] == 0x1f45957 and r['wrapper_caller_rva'] == 0x8d58dd
                            and r['caller']-r['caller_rva'] == r['wrapper_caller']-r['wrapper_caller_rva']
                            and r['color_stable'] and helper['color_stable']
                            and r['fname'] == helper['fname'] and r['color_bits'] == helper['color_bits'])
                if r['getter_linked']:
                    g = children.get(r['getter_id'])
                    prior = [p for p in children.values() if p['family'] == 'get' and p['helper_id'] == r['helper_id']]
                    require(r['route_verified'] and material['ready'] and g is not None and prior[-1] == g
                            and g['route_verified'] and g['returned'] and g['return_sequence'] < r['entry_sequence']
                            and g['result'] == r['mid'] != 0 and g['slot'] == r['slot']
                            and sum(p['result'] == r['mid'] for p in prior) == 1
                            and not any(p['getter_id'] == g['id'] for p in children.values() if p['family'] == 'set')
                            and g['caller']-g['caller_rva'] == r['caller']-r['caller_rva'])
                else:
                    require(not r['getter_id'] and r['slot'] == -1 and material['invalidated'])
            if r['route_verified']:
                require(start is not None and start['family'] == 'start' and helper['parent_id'] == start['id']
                        and helper['provider_link_matches'] and not r['parent_call_id'])
            else:
                require(material['invalidated'])
            children[r['id']] = r
        require(material['pending'] == sum(not r['returned'] for r in calls))
        require(material['complete'] == (complete and material['ready'] and not any(material[k] for k in
                    ('invalidated', 'pending', 'omitted', 'foreign_entries'))))
        if material['complete']:
            require(all(r['route_verified'] and (r['family'] == 'get' or r['getter_linked']) for r in calls))
        dispatch = material.get('row_dispatch')
        if dispatch is not None:
            require(isinstance(dispatch, dict) and set(dispatch) == {
                'version', 'capacity', 'cpu_write_branch_only', 'ownership_proven', 'proxy_completion_proven',
                'render_completion_proven', 'gpu_completion_proven', 'ready', 'complete', 'invalidated',
                'reentry', 'attempted', 'omitted', 'foreign_entries', 'unattributed', 'pending', 'calls', 'setters'})
            require(type(dispatch['version']) is int and dispatch['version'] == 1
                    and type(dispatch['capacity']) is int and dispatch['capacity'] == 64
                    and dispatch['cpu_write_branch_only'] is True
                    and all(dispatch[k] is False for k in ('ownership_proven', 'proxy_completion_proven',
                                'render_completion_proven', 'gpu_completion_proven')))
            require(all(type(dispatch[k]) is bool for k in ('ready', 'complete', 'invalidated', 'reentry'))
                    and dispatch['ready'] == material['ready']
                    and all(uint(dispatch[k]) for k in ('attempted', 'omitted', 'foreign_entries', 'unattributed', 'pending')))
            require(not any(dispatch[k] for k in ('reentry', 'omitted', 'foreign_entries', 'unattributed'))
                    or dispatch['invalidated'])
            dispatches = dispatch['calls']
            require(isinstance(dispatches, list) and len(dispatches) <= 64
                    and dispatch['attempted'] == len(dispatches)+dispatch['omitted']+dispatch['unattributed']
                    and (not dispatch['omitted'] or len(dispatches) == 64))
            dispatched = {}
            for ordinal, r in enumerate(dispatches, 1):
                require(isinstance(r, dict) and set(r) == {'id', 'ordinal', 'setter_id', 'getter_id', 'helper_id',
                    'start_id', 'parent_dispatch_id', 'thread', 'entry_sequence', 'return_sequence', 'returned',
                    'caller', 'caller_rva', 'mid', 'fname', 'row_read', 'row_stable', 'matched', 'color_bits'})
                require(all(uint(r[k]) for k in ('id', 'ordinal', 'setter_id', 'getter_id', 'helper_id', 'start_id',
                            'parent_dispatch_id', 'thread', 'entry_sequence', 'return_sequence', 'caller', 'caller_rva', 'mid', 'fname'))
                        and all(type(r[k]) is bool for k in ('returned', 'row_read', 'row_stable', 'matched'))
                        and isinstance(r['color_bits'], list) and len(r['color_bits']) == 4
                        and all(uint(b, 2**32) for b in r['color_bits']))
                require(r['ordinal'] == ordinal and r['id'] == r['entry_sequence'] > 0
                        and r['id'] not in sequences and (not dispatched or r['id'] > max(dispatched)))
                sequences.add(r['entry_sequence'])
                require((r['returned'] and r['return_sequence'] > r['entry_sequence']
                         and r['return_sequence'] not in sequences)
                        or (not r['returned'] and r['return_sequence'] == 0))
                if r['returned']:
                    sequences.add(r['return_sequence'])
                setter = children.get(r['setter_id'])
                require(setter is not None and setter['family'] == 'set'
                        and all(r[k] == setter[k] for k in ('getter_id', 'helper_id', 'start_id', 'thread'))
                        and setter['entry_sequence'] < r['entry_sequence']
                        and (not setter['returned'] or (r['entry_sequence'] < setter['return_sequence']
                             and (not r['returned'] or r['return_sequence'] < setter['return_sequence']))))
                active = [p for p in dispatched.values() if not p['returned'] or p['return_sequence'] > r['entry_sequence']]
                require(r['parent_dispatch_id'] == (active[-1]['id'] if active else 0))
                if active:
                    require(dispatch['reentry'] and dispatch['invalidated'])
                    parent = active[-1]
                    require(not parent['returned'] or (r['returned'] and r['return_sequence'] < parent['return_sequence']))
                require(not r['row_stable'] or r['row_read'])
                if r['matched']:
                    active_material = [p for p in calls if p['entry_sequence'] < r['entry_sequence']
                                       and (not p['returned'] or p['return_sequence'] > r['entry_sequence'])]
                    require(dispatch['ready'] and setter['getter_linked'] and setter['route_verified']
                            and not setter['parent_call_id'] and active_material == [setter]
                            and not r['parent_dispatch_id'] and r['caller_rva'] == 0x1f1e528
                            and r['caller']-r['caller_rva'] == setter['caller']-setter['caller_rva']
                            and r['row_stable'] and r['mid'] == setter['mid']
                            and r['fname'] == setter['fname'] and r['color_bits'] == setter['color_bits'])
                else:
                    require(dispatch['invalidated'])
                dispatched[r['id']] = r
            require(dispatch['pending'] == sum(not r['returned'] for r in dispatches))
            dispatch_complete = material['complete'] and dispatch['ready'] and not any(dispatch[k] for k in
                                ('invalidated', 'pending', 'omitted', 'foreign_entries', 'unattributed'))
            require(dispatch['complete'] == dispatch_complete
                    and (not dispatch_complete or all(r['matched'] and r['returned'] for r in dispatches)))
            setters = dispatch['setters']
            selected_setters = [s for s in calls if s['family'] == 'set']
            require(isinstance(setters, list) and len(setters) == len(selected_setters))
            for result, setter in zip(setters, selected_setters):
                require(isinstance(result, dict) and set(result) == {'setter_id', 'observed_dispatches',
                            'matched_dispatches', 'cpu_row_write_branch'}
                        and all(uint(result[k]) for k in ('setter_id', 'observed_dispatches', 'matched_dispatches'))
                        and result['setter_id'] == setter['id'])
                nested = [r for r in dispatches if r['setter_id'] == setter['id']]
                matched = sum(r['matched'] for r in nested)
                require(result['observed_dispatches'] == len(nested) and result['matched_dispatches'] == matched)
                expected = bool(matched) if dispatch_complete and setter['getter_linked'] and setter['returned'] else None
                require(result['cpu_row_write_branch'] is expected)
    return receipt


def retain_vfx_completion_observation(report,path,run_id,objects):
    """Retain the last flushed publication and any crash-interrupted staging file."""
    from .replay_evidence import store_bytes
    # A healthy last publication is a prefix, not evidence that a subsequent
    # write succeeded. In particular disk/access failure can prevent recording
    # its own error. Pair this artifact with the live status line when available.
    report['vfx_completion_observation_prefix_valid'] = False
    report['vfx_completion_observation_requires_live_status'] = True
    report['vfx_completion_observation_phase_complete'] = False
    report['vfx_collection18_writer_receipts_complete'] = False
    for stale in ('vfx_completion_observation_collection_error','vfx_completion_observation',
                  'vfx_completion_observation_raw','vfx_completion_observation_temporary_raw'):
        report.pop(stale,None)
    report.pop('vfx_first_trace_writer', None)
    report.pop('vfx_collection18_material_provider_census', None)
    report.pop('vfx_collection18_zero_serial_diagnostic', None)
    report.pop('vfx_collection18_start_helper_correlation', None)
    for candidate,key in ((path,'vfx_completion_observation_raw'),
                          (path.with_name(path.name+'.tmp'),'vfx_completion_observation_temporary_raw')):
        try:
            if not candidate.is_file():continue
            with candidate.open('rb') as stream:raw=stream.read(VFX_OBSERVATION_SIDECAR_BYTES+1)
            report[key]=store_bytes(objects,raw)
            if len(raw)>VFX_OBSERVATION_SIDECAR_BYTES:raise RuntimeError('VFX observation exceeds bound; retained prefix only')
            if candidate!=path:continue
            value=json.loads(raw)
            if (not isinstance(value,dict) or value.get('run_id')!=run_id or value.get('version')!=1
                or value.get('observation_only') is not True or value.get('writer_coverage_proven') is not False
                or value.get('synchronized_snapshot') is not False or type(value.get('pid')) is not int
                or value['pid']<=0 or not isinstance(value.get('events'),list)
                or len(value['events'])>VFX_OBSERVATION_MAX_EVENTS
                or any(type(value.get(k)) is not int or value[k]<0 for k in
                    ('dropped','pending_pairs','unstable_records','truncated_records'))
                or type(value.get('persistence_failed')) is not bool
                or any(not isinstance(row,dict) or row.get('entry_rva') not in (0x8c9120,0x3d74d0,0x3ea210,0x9481e0)
                    or row.get('boundary') not in ('entry','return') or not isinstance(row.get('rows'),list)
                    or len(row['rows'])>4 for row in value['events'])):
                raise RuntimeError('VFX observation identity or shape differs')
            writer_version=value.get('shared_writer_observation_version')
            if writer_version is not None:
                routes={0x8c9120:'native_registration',0x3d74d0:'dispatch',
                        0x3ea210:'shared_prune',0x9481e0:'shared_copy'}
                caller_routes={0x3ea210:('other','kismet_add_after_direct_write','kismet_remove_after_direct_write'),
                               0x9481e0:('other','kismet_let_copy')}
                if (type(writer_version) is not int or writer_version!=1
                    or value.get('clear_observed') is not False
                    or value.get('prune_observes_prior_direct_write') is not False
                    or value.get('owned_bytes')!=(VFX_OBSERVATION_OWNED_BYTES
                        if value.get('collection18_writer_observation_version') is not None else 3*1024*1024)
                    or any(row.get('route')!=routes[row['entry_rva']]
                           or row.get('caller_route') not in caller_routes.get(row['entry_rva'],('other',))
                           or any(type(row.get(k)) is not int or row[k]<0 for k in
                                  ('caller','source_collection','active_dispatch_same_thread_same_collection',
                                   'active_dispatch_other_thread_same_collection'))
                           or not row['caller']
                           or (row['entry_rva']!=0x9481e0 and row['source_collection']!=0)
                           or any(type(row.get(k)) is not int or row[k]<row[dispatch] for k,dispatch in
                                  (('active_same_thread_same_collection','active_dispatch_same_thread_same_collection'),
                                   ('active_other_thread_same_collection','active_dispatch_other_thread_same_collection')))
                           for row in value['events'])):
                    raise RuntimeError('VFX shared writer observation shape differs')
            elif any(row['entry_rva'] in (0x3ea210,0x9481e0) for row in value['events']):
                raise RuntimeError('VFX shared writer observation version missing')
            producer_version=value.get('producer_observation_version')
            producer_incomplete=False
            if producer_version is not None:
                producers=value.get('producers')
                if (type(producer_version) is not int or producer_version!=1
                    or value.get('task_attribution_proven') is not False
                    or value.get('producer_entry_persisted_before_native') is not False
                    or not isinstance(producers,list) or len(producers)>128
                    or any(type(value.get(k)) is not int or value[k]<0 for k in
                           ('producer_pending','producer_pending_at_close'))):
                    raise RuntimeError('VFX producer observation shape differs')
                by_id={}
                for p in producers:
                    if (not isinstance(p,dict)
                        or any(type(p.get(k)) is not int or p[k]<0 for k in
                               ('id','parent','entry_rva','caller','thread','depth','entry_ms','return_ms',
                                'event','bound_byte','observed_application_epoch','async_fence','update_active'))
                        or not p['id'] or p['id'] in by_id or not p['caller'] or not p['thread'] or not p['depth']
                        or p['entry_rva'] not in (0x3c5250,0x1f73990)
                        or p['bound_byte']>255 or p['update_active']>255
                        or any(type(p.get(k)) is not bool for k in ('returned','epoch_read','particle_state_read'))
                        or not isinstance(p.get('object'),dict)
                        or (p['returned'] and p['return_ms']<p['entry_ms'])
                        or (not p['returned'] and p['return_ms']!=0)):
                        raise RuntimeError('VFX producer entry differs')
                    by_id[p['id']]=p
                for p in producers:
                    parent=by_id.get(p['parent'])
                    if ((not p['parent'] and p['depth']!=1)
                        or (p['parent'] and (parent is None or parent['id']>=p['id']
                            or parent['thread']!=p['thread'] or parent['depth']+1!=p['depth']
                            or parent['entry_ms']>p['entry_ms']
                            or (parent['returned'] and (not p['returned'] or parent['return_ms']<p['return_ms']))))):
                        raise RuntimeError('VFX producer nesting differs')
                for row in value['events']:
                    pid=row.get('producer_id')
                    p=by_id.get(pid)
                    if (type(pid) is not int or pid<0 or (pid and (p is None
                        or p['thread']!=row.get('thread') or p['entry_ms']>row.get('time_ms',-1)
                        or (p['returned'] and p['return_ms']<row.get('time_ms',-1))))):
                        raise RuntimeError('VFX manager producer link differs')
                if value['producer_pending']!=sum(not p['returned'] for p in producers):
                    raise RuntimeError('VFX producer pending count differs')
                producer_incomplete=bool(value['producer_pending'] or value['producer_pending_at_close'])
            elif 'producers' in value or any('producer_id' in row for row in value['events']):
                raise RuntimeError('VFX producer observation version missing')
            owner_version=value.get('execution_owner_observation_version')
            if owner_version is not None:
                if (type(owner_version) is not int or owner_version!=1 or producer_version!=1
                    or value.get('execution_owner_lease_proven') is not False):
                    raise RuntimeError('VFX execution owner version differs')
                for p in producers:
                    owners=p.get('execution_owners')
                    ancestry=p.get('ancestry')
                    if (p.get('owner_snapshot_boundary')!='producer_entry'
                        or type(p.get('owners_truncated')) is not bool
                        or type(p.get('ancestry_at_capacity')) is not bool
                        or not isinstance(owners,list) or len(owners)>4
                        or not isinstance(ancestry,list) or len(ancestry)>16
                        or any(type(pc) is not int or pc<=0 for pc in ancestry)
                        or p['ancestry_at_capacity']!=(len(ancestry)==16)
                        or (p['owners_truncated'] and not value['dropped'])):
                        raise RuntimeError('VFX execution owner snapshot differs')
                    seen=set()
                    for e in owners:
                        if (not isinstance(e,dict)
                            or any(type(e.get(k)) is not int or e[k]<0 for k in
                                   ('id','kind','context','argument','caller','native_thread','table','function',
                                    'function_table','scheduled_task','world','completion','collection','storage'))
                            or not e['id'] or e['id'] in seen or e['kind'] not in (1,2,3,4)
                            or not e['caller']
                            or any(type(e.get(k)) is not int for k in ('count','capacity','recursion'))
                            or any(type(e.get(k)) is not bool for k in ('task_read','hub_read'))
                            or not isinstance(e.get('tick_owner'),dict) or not isinstance(e.get('hub_owner'),dict)
                            or (e['hub_read'] and (e['kind']!=4 or e['collection']!=e['context']+0x78))
                            or (e['task_read'] and e['kind']==4)):
                            raise RuntimeError('VFX execution owner identity differs')
                        seen.add(e['id'])
            elif any('execution_owners' in p for p in value.get('producers',[])):
                raise RuntimeError('VFX execution owner version missing')
            registration_incomplete=False
            if 'registration_ancestry' in value or 'registration_ancestry_version' in value:
                witnesses=value.get('registration_ancestry')
                if (value.get('registration_ancestry_version')!=1 or not isinstance(witnesses,list) or len(witnesses)>16
                    or type(value.get('registration_scope_overflow')) is not int or value['registration_scope_overflow']<0
                    or type(value.get('registration_pending_at_close')) is not bool):
                    raise RuntimeError('VFX registration ancestry version/capacity differs')
                registration_incomplete=bool(value['registration_scope_overflow'] or value['registration_pending_at_close'])
                entries={e['pair']:e for e in value['events'] if e['entry_rva']==0x8c9120 and e['boundary']=='entry'}
                returns={e['pair'] for e in value['events'] if e['entry_rva']==0x8c9120 and e['boundary']=='return'}
                seen=set()
                for w in witnesses:
                    if (not isinstance(w,dict) or any(type(w.get(k)) is not int or w[k]<0 for k in
                        ('pair','caller','thread','entry_sequence','return_sequence'))
                        or any(type(w.get(k)) is not bool for k in
                               ('returned','trace_start_writer','overlap','scopes_truncated','ancestry_at_capacity'))
                        or w.get('ownership_proven') is not False or w.get('before_trace_effects_proven') is not False
                        or w['pair'] not in entries or w['pair'] in seen
                        or w['caller']!=entries[w['pair']]['caller'] or w['thread']!=entries[w['pair']]['thread']
                        or w['returned']!=(w['pair'] in returns) or not w['entry_sequence']
                        or (w['returned'] and w['return_sequence']<=w['entry_sequence'])
                        or (not w['returned'] and w['return_sequence'])
                        or not isinstance(w.get('scopes'),list) or len(w['scopes'])>2
                        or not isinstance(w.get('ancestry'),list) or len(w['ancestry'])>32
                        or any(type(pc) is not int or pc<=0 for pc in w['ancestry'])
                        or w['ancestry_at_capacity']!=(len(w['ancestry'])==32)):
                        raise RuntimeError('VFX registration ancestry pair differs')
                    seen.add(w['pair']); scope_ids=set()
                    registration_incomplete |= not w['returned'] or w['scopes_truncated']
                    for s in w['scopes']:
                        if (not isinstance(s,dict) or any(type(s.get(k)) is not int or s[k]<0 for k in
                            ('id','kind','task','argument','caller','function','function_table','scheduled_task','world','completion',
                             'thread','native_thread','entry_sequence','return_sequence','epoch','registration_epoch'))
                            or s['kind'] not in (1,2,3) or not s['id'] or s['id'] in scope_ids
                            or any(type(s.get(k)) is not bool for k in ('read','epoch_read','returned','identity_matches_at_registration'))
                            or any(type(s.get(k)) is not int for k in ('registration_owner_index','registration_owner_serial'))
                            or not isinstance(s.get('owner'),dict)
                            or (s['entry_sequence'] and (s['entry_sequence']>=w['entry_sequence'] or s['thread']!=w['thread']))
                            or (s['returned'] and (not w['returned'] or s['return_sequence']<=w['return_sequence']))
                            or (not s['returned'] and s['return_sequence'])
                            or (s['identity_matches_at_registration'] and
                                (not s['read'] or not s['owner'].get('valid') or s['owner'].get('serial',0)<=0
                                 or s['owner'].get('serial')!=s['registration_owner_serial']
                                 or s['owner'].get('index')!=s['registration_owner_index']
                                 or s['task']!=s['scheduled_task'] or s['argument']!=s['task']+0x40))):
                            raise RuntimeError('VFX registration scope identity differs')
                        scope_ids.add(s['id']); registration_incomplete |= not s['returned']
                    scopes=w['scopes']
                    reason=('scope_capacity' if w['scopes_truncated'] else 'missing_scope' if not scopes
                            else 'overlapping_or_nested_scope' if w['overlap'] or len(scopes)!=1
                            else 'continuation_not_entry' if scopes[0]['kind']==3
                            else 'identity_changed' if not scopes[0]['identity_matches_at_registration']
                            else 'stack_prefix_only' if w['ancestry_at_capacity'] or not w['ancestry'] else 'correlation_only')
                    if w.get('reason')!=reason: raise RuntimeError('VFX registration correlation reason differs')
                if len(witnesses)!=min(len(entries),16): raise RuntimeError('VFX registration ancestry missing pair')
            hub_incomplete=False
            if 'disable_hub_witness' in value or 'disable_hub_witness_version' in value:
                if value.get('disable_hub_witness_version')!=1:
                    raise RuntimeError('VFX hub witness version differs')
                one_arg_layout=value.get('disable_hub_onearg_layout_version',0)
                if type(one_arg_layout) is not int or one_arg_layout not in (0,1):
                    raise RuntimeError('VFX hub one-argument layout version differs')
                hub=value.get('disable_hub_witness')
                if hub is not None:
                    if (not isinstance(hub,dict) or hub.get('selected') is not True
                        or hub.get('snapshot_boundary')!='before_original_140400590'
                        or hub.get('selection')!='first_custom_manager_hub_with_selected_disable_producer'
                        or hub.get('writer_coverage_proven') is not False or hub.get('receiver_undo_proven') is not False
                        or hub.get('hypothesis_proven') is not False or hub.get('receiver_b_participants')!='unclassified'
                        or any(type(hub.get(k)) is not bool for k in ('returned','header_read','snapshot_complete','truncated','overlap','writer_overflow','pending_at_close'))
                        or any(type(hub.get(k)) is not int or hub[k]<0 for k in ('id','image_base','dispatcher','collection','storage','scope_id','scope_kind','thread','entry_ms','return_ms'))
                        or hub.get('writer_sites')!=[0x3a3b70,0x3ca7a0] or not hub['image_base']
                        or any(type(hub.get(k)) is not int for k in ('count','capacity','recursion'))
                        or hub['collection'] not in (0,hub['dispatcher']+0x78) or hub['scope_kind'] not in (2,3)
                        or not isinstance(hub.get('rows'),list) or len(hub['rows'])>2
                        or not isinstance(hub.get('writers'),list) or len(hub['writers'])>16):
                        raise RuntimeError('VFX hub witness shape differs')
                    known_states={'known','disabled','unreadable','unstable','unknown_layout','unknown_storage','unknown_receiver'}
                    for i,r in enumerate(hub['rows']):
                        if (not isinstance(r,dict) or r.get('state') not in known_states
                            or any(type(r.get(k)) is not int for k in ('entry','storage','enabled','table','weak_index','weak_serial','callable','handle','producer_id'))
                            or 'bound_byte' not in r
                            or (r.get('bound_byte') is not None if one_arg_layout and r.get('table')==hub['image_base']+0x374b760
                                else type(r.get('bound_byte')) is not int)
                            or r['entry']!=(hub['storage'] or hub['collection'])+i*0x40
                            or not isinstance(r.get('receiver'),dict)):
                            raise RuntimeError('VFX hub row shape differs')
                        if r['state']=='known' and (r['enabled']!=3 or r['table'] not in (hub['image_base']+0x326bfe8,hub['image_base']+0x374b760)
                            or (r['table']==hub['image_base']+0x374b760 and not one_arg_layout)
                            or r['weak_serial']<=0 or not r['callable'] or not r['handle']
                            or r['receiver'].get('valid') is not True or r['receiver'].get('index')!=r['weak_index']
                            or r['receiver'].get('serial')!=r['weak_serial']):
                            raise RuntimeError('VFX hub known receiver differs')
                        if r['producer_id']:
                            p=by_id.get(r['producer_id'])
                            if (r['table']!=hub['image_base']+0x326bfe8 or p is None or p['entry_rva']!=0x3c5250 or p['thread']!=hub['thread'] or r['callable']!=hub['image_base']+0x3c5250
                                or p['bound_byte']!=r['bound_byte'] or p['object']!=r['receiver']
                                or not any(e['id']==hub['id'] and e['kind']==4 for e in p['execution_owners'])):
                                raise RuntimeError('VFX hub producer correlation differs')
                    for i,w in enumerate(hub['writers']):
                        if (not isinstance(w,dict) or w.get('pair')!=i+1 or w.get('entry_rva') not in (0x3a3b70,0x3ca7a0)
                            or any(type(w.get(k)) is not bool for k in ('returned','handle_read','same_thread','scopes_truncated'))
                            or any(type(w.get(k)) is not int or w[k]<0 for k in ('parent_pair','caller','receiver','callable','bound_byte','thread','entry_ms','return_ms','result_raw','output_address','handle'))
                            or w['parent_pair']>i or w['same_thread']!=(w['thread']==hub['thread'])
                            or not isinstance(w.get('receiver_identity'),dict)
                            or not isinstance(w.get('scopes'),list) or len(w['scopes'])>4
                            or any(not isinstance(s,list) or len(s)!=2 or type(s[0]) is not int or s[0]<=0 or s[1] not in (1,2,3,4) for s in w['scopes'])
                            or (w['returned'] and w['return_ms']<w['entry_ms'])):
                            raise RuntimeError('VFX hub writer pair differs')
                    if hub['snapshot_complete'] and (not hub['header_read'] or hub['count']!=2 or len(hub['rows'])!=2
                        or hub['capacity']<2 or hub['recursion']<0 or hub['truncated'] or hub['overlap'] or hub['writer_overflow'] or hub['writers']
                        or any(r['state'] not in ('known','disabled') for r in hub['rows'])):
                        raise RuntimeError('VFX hub incomplete snapshot marked complete')
                    if hub['snapshot_complete'] and not any(r['producer_id'] for r in hub['rows']):
                        raise RuntimeError('VFX hub selected receiver missing')
                    hub_incomplete=not hub['returned'] or not hub['snapshot_complete'] or hub['pending_at_close']
            attachment_version=value.get('attachment_dispatch_observation_version')
            if attachment_version is not None:
                if (type(attachment_version) is not int or attachment_version!=1
                    or value.get('attachment_pre_deactivation_observed') is not False
                    or any(row.get('collection_kind') not in ('vfx_manager_finished','trace_attachment_deactivated')
                           or (row['collection_kind']=='trace_attachment_deactivated' and row['entry_rva']!=0x3d74d0)
                           or not isinstance(row.get('manager'),dict)
                           or row['manager'].get('address')!=row.get('collection',0)-(
                               0x1b8 if row['collection_kind']=='trace_attachment_deactivated' else 0x388)
                           for row in value['events'])):
                    raise RuntimeError('VFX attachment collection identity differs')
            elif any('collection_kind' in row for row in value['events']):
                raise RuntimeError('VFX attachment observation version missing')
            c18_selected,c18_complete=_collection18_writer_receipts(value)
            material_census=_collection18_material_census(value)
            zero_diagnostic=_collection18_zero_serial_diagnostic(value)
            start_helper=_collection18_start_helper_correlation(value)
            report['vfx_completion_observation']=value
            report['vfx_completion_observation_prefix_valid']=not any(value[k] for k in
                ('dropped','persistence_failed','pending_pairs','unstable_records','truncated_records'))
            if producer_version is not None and value['producer_pending']:
                report['vfx_completion_observation_prefix_valid']=False
            if hub_incomplete or registration_incomplete or c18_selected:
                report['vfx_completion_observation_prefix_valid']=False
            # Older sidecars remain prefixes requiring live status. A sealed
            # phase is only observation completeness: it grants no native
            # exclusion, writer census, receiver lease, or B recovery claim.
            phase=value.get('phase')
            if phase is not None:
                if (not isinstance(phase,dict) or phase.get('scope')!='startup_to_trajectory_completion'
                    or phase.get('boundary')!='CompleteTrajectory.vfx_status'
                    or phase.get('state') not in ('open','closed') or type(phase.get('complete')) is not bool
                    or phase.get('whole_process_coverage') is not False
                    or phase.get('native_quiescence_proven') is not False
                    or any(type(phase.get(k)) is not int or phase[k]<0 for k in
                           ('end_tick','close_thread','close_ms','pending_at_close'))
                    or type(value.get('capture_start_ms')) is not int or value['capture_start_ms']<0):
                    raise RuntimeError('VFX observation phase shape differs')
                closed=phase['state']=='closed'
                complete=closed and not phase['pending_at_close'] and not producer_incomplete and report['vfx_completion_observation_prefix_valid']
                if (phase['complete']!=complete
                    or (closed and (not phase['close_thread'] or phase['close_ms']<value['capture_start_ms']))
                    or (not closed and any(phase[k] for k in
                                          ('end_tick','close_thread','close_ms','pending_at_close')))):
                    raise RuntimeError('VFX observation phase closure differs')
                if complete:
                    # Verify a closed set of pairs instead of trusting zero
                    # counters or the producer's completeness bit alone.
                    pairs={}
                    for row in value['events']:
                        if (type(row.get('pair')) is not int or row['pair']<=0
                            or type(row.get('thread')) is not int or row['thread']<=0
                            or type(row.get('time_ms')) is not int
                            or not value['capture_start_ms']<=row['time_ms']<=phase['close_ms']
                            or row.get('rows_complete') is not True
                            or row.get('unstable') is not False or row.get('truncated') is not False
                            or any(type(row.get(k)) is not int or row[k]<0 for k in
                                   ('active_same_thread_same_collection','active_other_thread_same_collection'))):
                            raise RuntimeError('VFX closed phase record differs')
                        pairs.setdefault(row['pair'],[]).append(row)
                    if any(len(rows)!=2 or [r['boundary'] for r in rows]!=['entry','return']
                           or any(rows[0].get(k)!=rows[1].get(k) for k in
                                  ('thread','entry_rva','collection','parent_pair','depth',
                                   'active_same_thread_same_collection','active_other_thread_same_collection'))
                           for rows in pairs.values()):
                        raise RuntimeError('VFX closed phase pair differs')
                    if writer_version is not None and any(
                        any(rows[0][k]!=rows[1][k] for k in
                            ('route','caller_route','caller','source_collection',
                             'active_dispatch_same_thread_same_collection','active_dispatch_other_thread_same_collection'))
                        for rows in pairs.values()):
                        raise RuntimeError('VFX closed shared writer pair differs')
                    if producer_version is not None and any(rows[0]['producer_id']!=rows[1]['producer_id']
                                                            for rows in pairs.values()):
                        raise RuntimeError('VFX closed producer pair differs')
                    if attachment_version is not None and any(rows[0]['collection_kind']!=rows[1]['collection_kind']
                                                              for rows in pairs.values()):
                        raise RuntimeError('VFX closed attachment pair differs')
                    report['vfx_completion_observation_phase_complete']=True
            first_writer = _vfx_first_trace_writer(value)
            if first_writer is not None:
                report['vfx_first_trace_writer'] = first_writer
            report['vfx_collection18_writer_receipts_complete']=c18_complete
            if material_census is not None:
                report['vfx_collection18_material_provider_census']=material_census
            if zero_diagnostic is not None:
                report['vfx_collection18_zero_serial_diagnostic']=zero_diagnostic
            if start_helper is not None:
                report['vfx_collection18_start_helper_correlation']=start_helper
        except (OSError,ValueError,RuntimeError,TypeError) as error:
            report['vfx_completion_observation_collection_error']=str(error)
            report['vfx_collection18_writer_receipts_complete']=False
            report['vfx_completion_observation_prefix_valid']=False
            report['vfx_completion_observation_phase_complete']=False
            report.pop('vfx_collection18_material_provider_census', None)
            report.pop('vfx_collection18_zero_serial_diagnostic', None)
            report.pop('vfx_collection18_start_helper_correlation', None)
    # A staging file means a publication failed or was interrupted, even when
    # the preceding complete JSON necessarily still says persistence_failed=false.
    if path.with_name(path.name+'.tmp').exists():
        report.pop('vfx_collection18_material_provider_census', None)
        report.pop('vfx_collection18_zero_serial_diagnostic', None)
        report.pop('vfx_collection18_start_helper_correlation', None)
        report['vfx_collection18_writer_receipts_complete']=False
        report['vfx_completion_observation_prefix_valid']=False
        report['vfx_completion_observation_phase_complete']=False
    report['vfx_completion_observation_requires_live_status']=not report['vfx_completion_observation_phase_complete']


def retain_niagara_observation(report,path,run_id,objects):
    try:
        if not path.is_file():return
        from .replay_evidence import store_bytes
        with path.open('rb') as stream:raw=stream.read(32769)
        report['niagara_observation_raw']=store_bytes(objects,raw)
        if len(raw)>32768:raise RuntimeError('Niagara observation exceeds bound')
        value=json.loads(raw)
        version=value.get('version') if isinstance(value,dict) else None
        if (not isinstance(value,dict) or value.get('run_id')!=run_id or version not in (1,2)
            or (version==2 and value.get('registration_snapshot')!='pre_original_only')
            or len(value.get('fields',[]))!=26 or not isinstance(value.get('events'),list)
            or len(value['events'])>(24 if version==2 else 16) or any(not isinstance(row,list) or len(row)!=26
                or any(type(x) is not int or not 0<=x<2**64 for x in row) for row in value['events'])):
            raise RuntimeError('Niagara observation identity or shape differs')
        if version==2:
            fields=('helper_rva scope boundary native_tick transaction_phase epoch thread caller caller_rva '
                    'host_world pair input input_index input_serial input_valid input_table component '
                    'component_index component_serial component_valid component_table world world_index '
                    'world_serial world_valid world_table').split()
            rvas=(0x1bcc5a0,0x1bcc730,0x1d58ba0)
            if value['fields']!=fields:raise RuntimeError('Niagara observation fields differ')
            pairs={}
            for row in value['events']:
                if (row[0] not in rvas or row[1]>=4 or row[2]>1
                    or row[10]!=rvas.index(row[0])*4+row[1]
                    or any(row[i]>2 for i in (14,19,24))):
                    raise RuntimeError('Niagara observation producer correlation differs')
                pair=row[10]
                if row[2]==0:
                    if pair in pairs:raise RuntimeError('Niagara observation duplicate entry')
                    pairs[pair]=row
                else:
                    entry=pairs.get(pair)
                    if entry is None:raise RuntimeError('Niagara observation unmatched return')
                    # Registration has no returned component: both records are
                    # the same pre-original copy, not a success/completion test.
                    if row[0]==0x1d58ba0 and (row[:2]!=entry[:2] or row[3:]!=entry[3:]):
                        raise RuntimeError('Niagara registration snapshot changed on return')
                    pairs[pair]=None
        report['niagara_observation']=value
    except (OSError,ValueError,RuntimeError,TypeError) as error:
        report['niagara_observation_collection_error']=str(error)


def historical_capture_interval_supported(report):
    from .replay_fidelity import bounded_checkpoint_window, execution_fallback_case, private_hud_drained_case, trace_render_recovery_case, ground_commit_case
    same_origin_guard=(report["historical_anchor_tick"]==170 and report["historical_advanced_tick"]==300
        and report.get("host_seek_target")==300 and report.get("host_seek")
        and report.get("corrected_inputs") and report.get("source_revision")
        and report.get("source_revision_profile")=="guard201"
        and not report.get("historical_cancel") and not report.get("host_seek_repeat"))
    private_hud_recovery=(report["historical_anchor_tick"]==170 and report["historical_advanced_tick"]==300
        and report.get("host_seek_target")==208 and report.get("host_seek")
        and report.get("historical_cancel") in ("before","after")
        and not any(report.get(k) for k in ("corrected_inputs","changed_inputs","source_revision",
            "host_seek_repeat","seek_advance_failure","seek_settlement_failure")))
    return bounded_checkpoint_window(report) or ground_commit_case(report) or trace_render_recovery_case(report) or private_hud_drained_case(report) or private_hud_recovery or same_origin_guard or execution_fallback_case(report) or report["historical_advanced_tick"] in (
        (220,2504) if report["historical_anchor_tick"]==170 else (211,) if report["checkpoint_pair"] else (209,210))


def expected_historical_rewinds(report: dict):
    if report.get("rolling_cycles"):
        first=report.get("historical_anchor_tick",210)
        return [(first+7+i,first+i) for i in range(report["rolling_cycles"])]
    from .replay_fidelity import bounded_checkpoint_window, execution_fallback_case
    if bounded_checkpoint_window(report):
        # Publication cancellation executes no historical native traversals.
        # A/B installation is not itself a simulation observation rewind.
        return [] if report.get("historical_cancel") and not report.get("consumer_mutation") else [(report["historical_advanced_tick"],report["historical_anchor_tick"])]
    if execution_fallback_case(report): return [(210,205),(210,170)]
    """Traversal plan comes from the requested experiment, never its log."""
    if report.get("completion_repeat") and report.get("historical_cancel")=="after":
        return [(210,205)]
    first = report["host_seek_first_target"] or (209 if report["host_seek_target"] == 208 else 208)
    if report["historical_anchor_tick"] == 170:
        plan = [] if report["historical_cancel"] and not report.get("seek_advance_failure") else [(report["historical_advanced_tick"], 170)]
        if report["host_seek_repeat"] and not report["historical_cancel"]:
            plan.append((first, 170))
        return plan
    if report["checkpoint_pair"]:
        return [(211, 206)] + ([] if report["historical_cancel"] else [(210, 205)])
    if report["host_seek_repeat"]:
        return [(210, 205)] + ([] if report["historical_cancel"] else [(first, 205)])
    return None


def native_launch_options(report):
    if report.get('no_native_threading'):
        # Retained original fault140E6922B and native caller142DE1670 prove
        # raw assets require this pool even with NoAsyncLoadingThread.
        # See rollback-g1-startup-null-queued-pool-cause-2026-09-23.json.
        raise RuntimeError('native queued thread pool is required by raw asset loading; '
                           '-nothreading is unsupported; G1 worker ownership remains unproved')
    options = ('-FixedSeed',) if report['native_fixed_seed'] else ()
    if report.get('single_game_thread'):
        options += ('-ONETHREAD',)
    if report.get('no_async_loading_thread'):
        if not report['native_fixed_seed']:
            raise RuntimeError('native loading diagnostic requires fixed-seed setup')
        options += ('-NoAsyncLoadingThread',)
    return options


def startup_loading_receipt(raw, run_id):
    matches=re.findall(r'startup loading flush complete run_id='+re.escape(run_id)
        +r' calls=(\d+) first_tick=(\d+) native_callbacks=true registration_budget_ms=60000'
        +r' registration_previous_bits=([0-9a-f]{8}) registration_restored=true'
        +r' raw_asset_checks=(\d+) raw_asset_waits=(\d+) raw_asset_completion=true',raw)
    if len(matches)!=1 or not 1<=int(matches[0][0])<=64 or int(matches[0][1])<1 or not 1<=int(matches[0][3])<=65536:
        raise RuntimeError('startup loading flush lacks completed native coverage and registration restoration')
    return dict(calls=int(matches[0][0]),first_tick=int(matches[0][1]),native_callbacks=True,
                registration_budget_ms=60000,registration_previous_bits=matches[0][2],registration_restored=True,
                raw_asset_checks=int(matches[0][3]),raw_asset_waits=int(matches[0][4]),raw_asset_completion=True)


def validate_native_loading_state(state):
    # Native140e3c050/140e65920 admit the background thread separately from EDL.
    # Both constructor140e3bc60 and deferred creation140e57a40 obey this flag.
    # Getter140e515c0 owns the initialized object and its thread pointer at+8.
    if (state.get('enabled') != 0 or state.get('event_driven') not in (0, 1)
            or state.get('thread_created') != 0 or state.get('thread_pointer') != 0
            or any(not isinstance(state.get(name), int) or state[name] >= -1
                   for name in ('cache_epoch', 'object_epoch'))):
        raise RuntimeError('native single-thread loading mode not established')
    return state


def read_native_loading_state(pid, executable, executable_sha256):
    from ctypes import wintypes
    if executable_sha256 != 'f8904e4b04bca3b47bc52a683f6190365d2eb89ee8f44f8072759e9c5e04a553':
        raise RuntimeError('native loading-state offsets require verified SC6 executable')
    rows = [row for row in psutil.Process(pid).memory_maps(grouped=False)
            if row.path and Path(row.path).resolve() == executable.resolve()]
    if not rows: raise RuntimeError('owned executable mapping unavailable')
    base = min(int(row.addr.split('-')[0], 16) for row in rows)
    api = ctypes.WinDLL('kernel32', use_last_error=True)
    api.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    api.OpenProcess.restype = wintypes.HANDLE
    api.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p,
                                     ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
    api.ReadProcessMemory.restype = wintypes.BOOL
    api.CloseHandle.argtypes = [wintypes.HANDLE]
    api.CloseHandle.restype = wintypes.BOOL
    handle = api.OpenProcess(0x10, False, pid) # Read only; no write, suspend or injection rights.
    if not handle: raise ctypes.WinError(ctypes.get_last_error())
    try:
        state = {}
        for key, offset, size in [('enabled',0x429f3fc,1),('cache_epoch',0x429f400,4),
                                  ('event_driven',0x419716a,1),('thread_created',0x429eac0,1),
                                  ('thread_pointer',0x429ef78,8),('object_epoch',0x429f250,4)]:
            value = ctypes.create_string_buffer(size); count = ctypes.c_size_t()
            if not api.ReadProcessMemory(handle, base+offset, value, size, ctypes.byref(count)) or count.value != size:
                raise ctypes.WinError(ctypes.get_last_error())
            state[key] = int.from_bytes(value.raw, 'little', signed=key in ('cache_epoch','object_epoch'))
        return state
    finally:
        if not api.CloseHandle(handle): raise ctypes.WinError(ctypes.get_last_error())


DISABLED_REPLAY_LOADER_SHA256 = "aa8eeee6a86537febdb4f6e3ba6aba7f825534e3f50092f7cbb745365a52a3dd"


def prepare_replay_loader(resources, game_executable: Path) -> dict:
    """Temporarily restore the verified local shim without changing its disabled copy."""
    target = game_executable.resolve().parent / "dwmapi.dll"
    source = target.with_name("dwmapi.dll.DISABLED")
    temporary = not target.exists()
    if temporary:
        # Verified identical to this installation's retained UE4 shim copy.
        # Unknown/missing loaders reject before another uninstrumented launch.
        if not source.is_file() or sha256_file(source) != DISABLED_REPLAY_LOADER_SHA256:
            raise RuntimeError("replay loader absent and disabled shim identity is not verified")
        resources.prepare_file(target, [DISABLED_REPLAY_LOADER_SHA256])
        resources.publish_copy(source, target)
    return {"path": str(target), "sha256": sha256_file(target), "temporary": temporary}


def prepare_replay_overlay(resources, game_executable: Path, report: dict) -> bool:
    if not any(report.get(key) for key in ("probe_interior_pause", "probe_application_pause", "index_checkpoint")):
        return False
    overlay_switch = (game_executable.parent / "disable_gameimgui.txt").resolve()
    if overlay_switch.parent != game_executable.resolve().parent:
        raise RuntimeError("overlay switch escaped the verified game directory")
    resources.prepare_file(overlay_switch, [None])
    overlay_switch.unlink(missing_ok=True)
    return True


def click_owned_replay_control(pid: int, hwnd: int, x: int, y: int) -> dict:
    """Exercise the visible control through Windows input, never a game API."""
    from ctypes import wintypes as w
    user = ctypes.WinDLL("user32", use_last_error=True)
    user.GetWindowThreadProcessId.argtypes = [w.HWND, ctypes.POINTER(w.DWORD)]
    user.GetForegroundWindow.restype = w.HWND
    user.GetAncestor.argtypes = [w.HWND, w.UINT]
    user.GetAncestor.restype = w.HWND
    owner = w.DWORD()
    user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
    if owner.value != pid:
        raise RuntimeError("replay control window is not owned by this run")
    root = user.GetAncestor(hwnd, 2)
    user.GetClassNameW.argtypes=[w.HWND,w.LPWSTR,ctypes.c_int]
    name=ctypes.create_unicode_buffer(128);user.GetClassNameW(hwnd,name,len(name))
    user.SetForegroundWindow.argtypes=[w.HWND]
    user.WindowFromPoint.argtypes=[w.POINT];user.WindowFromPoint.restype=w.HWND
    user.SetWindowPos.argtypes=[w.HWND,w.HWND,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,w.UINT]
    user.GetWindowRect.argtypes=[w.HWND,ctypes.POINTER(w.RECT)]
    foreground = user.GetForegroundWindow()
    # Only the dedicated output thread may be activated while native work is
    # suspended. Never ALT-nudge or synchronously enter the native game queue.
    if foreground != root and name.value=='HorseModReplayOutputUi':
        user.SetForegroundWindow(root)
        deadline=time.monotonic()+0.5
        while user.GetForegroundWindow()!=root and time.monotonic()<deadline:time.sleep(0.01)
    activation_click=False
    class Mouse(ctypes.Structure):
        _fields_ = [("dx", w.LONG), ("dy", w.LONG), ("data", w.DWORD),
                    ("flags", w.DWORD), ("time", w.DWORD), ("extra", ctypes.c_size_t)]
    class Payload(ctypes.Union):
        _fields_ = [("mouse", Mouse)]
    class Input(ctypes.Structure):
        _fields_ = [("type", w.DWORD), ("payload", Payload)]
    user.SendInput.argtypes = [w.UINT, ctypes.POINTER(Input), ctypes.c_int]
    before = w.POINT()
    if not user.GetCursorPos(ctypes.byref(before)):
        raise RuntimeError("could not retain cursor position")
    def button(flags):
        event = Input(0, Payload(mouse=Mouse(0, 0, 0, flags, 0, 0)))
        if user.SendInput(1, ctypes.byref(event), ctypes.sizeof(event)) != 1:
            raise RuntimeError("Windows rejected replay control input")
    released = False
    last_position=(before.x,before.y)
    try:
        if user.GetForegroundWindow()!=root and name.value=='HorseModReplayOutputUi':
            # Click the blank client corner only after exposing and verifying
            # this run's window at that point. The actual control is a separate
            # click after activation, so another foreground app receives none.
            rect=w.RECT()
            if not user.GetWindowRect(root,ctypes.byref(rect)) or not user.SetWindowPos(root,0,0,0,0,0,0x0013):
                raise RuntimeError('could not expose owned replay output')
            point=w.POINT(rect.left+4,rect.top+4)
            if not user.SetCursorPos(point.x,point.y):raise RuntimeError('could not position replay activation input')
            last_position=(point.x,point.y);time.sleep(0.05)
            if user.GetAncestor(user.WindowFromPoint(point),2)!=root:
                raise RuntimeError('replay activation point is occluded')
            button(0x0002);time.sleep(0.08);button(0x0004);activation_click=True
            deadline=time.monotonic()+0.5
            while user.GetForegroundWindow()!=root and time.monotonic()<deadline:time.sleep(0.01)
        if user.GetForegroundWindow()!=root:
            raise RuntimeError(f'replay control lost focus: hwnd={hwnd} root={root} foreground={user.GetForegroundWindow()}')
        if not user.SetCursorPos(x, y):
            raise RuntimeError("could not position replay control input")
        last_position=(x,y)
        time.sleep(0.05)
        user.GetWindowThreadProcessId(hwnd,ctypes.byref(owner))
        if owner.value!=pid or user.GetForegroundWindow()!=root or user.GetAncestor(user.WindowFromPoint(w.POINT(x,y)),2)!=root:
            raise RuntimeError('replay control ownership/focus/point changed before input')
        button(0x0002)
        time.sleep(0.08)
    finally:
        try:
            button(0x0004)
            released = True
            time.sleep(0.08)
        finally:
            current = w.POINT()
            if user.GetCursorPos(ctypes.byref(current)) and (current.x, current.y) == last_position:
                user.SetCursorPos(before.x, before.y)
    return {"method": "Windows SendInput", "pid": pid, "hwnd": hwnd,
            "x": x, "y": y, "button_released": released,
            "window_class":name.value,"foreground_verified":True,"activation_click":activation_click}


def collect_coherence_images(report: dict, report_path: Path, pid: int, text: str) -> None:
    run_id = report["run_id"]
    from PIL import Image
    pattern = (r"coherence image run_id="+re.escape(run_id)
        + r" tick=(\d+) generation=(\d+) held=(true|false) hwnd=(\d+) file=(\S+) width=(\d+) height=(\d+) gpu_complete=true maps=1")
    captured = report.setdefault("coherence_images", {})
    for tick, generation, held, hwnd, name, width, height in re.findall(pattern, text):
        label = ("held" if held == "true" else "native")+tick+"-g"+generation
        if label in captured:
            continue
        root = qualification_root().resolve()
        source = (root/name).resolve()
        if source.parent != root or not name.startswith(run_id+"-coherence-") or source.suffix != ".ppm":
            raise RuntimeError("invalid owned coherence artifact path")
        if source.stat().st_size > 16*1024*1024:
            raise RuntimeError("coherence artifact exceeds bounded storage")
        destination = report_path.with_name(report_path.stem+"-"+label+".png")
        with Image.open(source) as image:
            if image.size != (int(width),int(height)) or image.format != "PPM":
                raise RuntimeError("coherence artifact descriptor mismatch")
            image.save(destination)
        captured[label] = {"path":str(destination.resolve()),"sha256":sha256_file(destination),
            "run_id":run_id,"pid":pid,"hwnd":int(hwnd),"tick":int(tick),"generation":int(generation),
            "held":held=="true","width":int(width),"height":int(height),"gpu_complete":True,
            "capture_method":"owned_dxgi_backbuffer_before_present","diagnostic_gpu_maps":1,
            "scope":"actual output content; held UI is paused display, not resumed native rendering proof"}
        source.unlink()


def focus_owned_replay_window(pid: int) -> dict | None:
    """Acquire focus during setup, before suspending the application pump."""
    from ctypes import wintypes as w
    user = ctypes.WinDLL("user32", use_last_error=True)
    user.GetWindowThreadProcessId.argtypes = [w.HWND, ctypes.POINTER(w.DWORD)]
    user.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, ctypes.c_int]
    user.IsWindowVisible.argtypes = [w.HWND]
    user.SetForegroundWindow.argtypes = [w.HWND]
    user.GetForegroundWindow.restype = w.HWND
    callback_type = ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    windows = []
    @callback_type
    def collect(hwnd, _):
        owner = w.DWORD()
        user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        name = ctypes.create_unicode_buffer(128)
        if (owner.value == pid and user.IsWindowVisible(hwnd)
                and user.GetClassNameW(hwnd, name, len(name)) and name.value == "UnrealWindow"):
            windows.append(hwnd)
        return True
    user.EnumWindows(collect, 0)
    if not windows:
        return None
    if len(windows) != 1:
        raise RuntimeError("interactive replay setup found ambiguous game windows")
    hwnd = windows[0]
    previous = user.GetForegroundWindow()
    user.SetForegroundWindow(hwnd)
    # Foreground activation can be asynchronous across input queues. The game
    # is still running setup here; never perform this wait inside the hold.
    deadline = time.monotonic() + 0.5
    while user.GetForegroundWindow() != hwnd and time.monotonic() < deadline:
        time.sleep(0.01)
    actual = user.GetForegroundWindow()
    activation_input = False
    if actual != hwnd:
        # Windows can deny a background runner foreground permission even
        # after the game is ready. ALT explicitly releases the foreground lock
        # (LockSetForegroundWindow documentation). Do this only during setup,
        # before authored input observation, and always release our key. Never
        # attach the game's input queue to another application's queue.
        if any(user.GetAsyncKeyState(key) & 0x8000 for key in (0x10, 0x11, 0x12)):
            raise RuntimeError("interactive setup focus requires released modifier keys")
        class Keyboard(ctypes.Structure):
            _fields_ = [("vk", w.WORD), ("scan", w.WORD), ("flags", w.DWORD),
                        ("time", w.DWORD), ("extra", ctypes.c_size_t)]
        class Payload(ctypes.Union):
            _fields_ = [("keyboard", Keyboard), ("mouse_storage", ctypes.c_byte * 32)]
        class Input(ctypes.Structure):
            _fields_ = [("type", w.DWORD), ("payload", Payload)]
        user.SendInput.argtypes = [w.UINT, ctypes.POINTER(Input), ctypes.c_int]
        def alt(flags):
            event = Input(1, Payload(keyboard=Keyboard(0x12, 0, flags, 0, 0)))
            if user.SendInput(1, ctypes.byref(event), ctypes.sizeof(event)) != 1:
                raise RuntimeError("Windows rejected setup activation input")
        try:
            alt(0)
            time.sleep(0.05)
            user.SetForegroundWindow(hwnd)
            deadline = time.monotonic() + 0.5
            while user.GetForegroundWindow() != hwnd and time.monotonic() < deadline:
                time.sleep(0.01)
        finally:
            alt(2)
        activation_input = True
        actual = user.GetForegroundWindow()
    if actual != hwnd:
        raise RuntimeError(f"interactive setup focus failed: expected={hwnd} previous={previous} actual={actual}")
    return {"pid": pid, "hwnd": hwnd, "previous_foreground": previous,
            "setup_alt_activation": activation_input, "modifier_released": True}


def run_replay_control(args):
    from .runner import _read_bounded_log_since

    if not 120 <= args.watch_frames <= 36000:
        raise RuntimeError("trajectory control requires 120..36000 samples")
    if getattr(args, "skip_intros", False) and not getattr(args, "include_setup", False):
        raise RuntimeError("native intro skipping requires setup observation; deployment was not started")
    launch_options=native_launch_options(dict(
        native_fixed_seed=bool(getattr(args,'skip_intros',False)),
        no_native_threading=bool(getattr(args,'no_native_threading',False)),
        single_game_thread=bool(getattr(args,'single_game_thread',False)),
        no_async_loading_thread=bool(getattr(args,'no_async_loading_thread',False))))
    framework_deployed = (args.log.parent / "UE4SS.dll").resolve()
    framework_source = getattr(args, "framework", framework_deployed).resolve()
    ucrt_path = getattr(args, "ucrt", Path(os.environ["SystemRoot"]) / "System32" / "ucrtbase.dll").resolve()
    for path in (args.dll, args.replay_mod, args.replay, args.game_executable, framework_source):
        if not path.is_file():
            raise RuntimeError(f"missing control prerequisite: {path}")
    started = time.monotonic()
    report = {"result": "fail", "scope": "passive_replay_trajectory",
              "mode": args.mode, "certifying": False,
              "full_match": bool(getattr(args, "full_match", False)),
              "include_setup": bool(getattr(args, "include_setup", False)),
              "skip_intros": bool(getattr(args, "skip_intros", False)),
              "changed_inputs": bool(getattr(args, "changed_inputs", False)),
              "corrected_inputs": bool(getattr(args, "corrected_inputs", False)),
              "source_revision": bool(getattr(args, "source_revision", False)),
              "source_revision_profile": getattr(args, "source_revision_profile", "historical"),
              "completion_repeat": bool(getattr(args, "completion_repeat", False)),
              "complete_seek_application": bool(getattr(args, "complete_seek_application", False)),
              "capture_cancel": bool(getattr(args, "capture_cancel", False)),
              "particle_owner_copy": bool(getattr(args, "particle_owner_copy", False)),
              "particle_owner_registration": bool(getattr(args, "particle_owner_registration", False)),
              "particle_owner_gpu": bool(getattr(args, "particle_owner_gpu", False)),
              "restore_reuse": bool(getattr(args, "restore_reuse", False)),
              "host_seek_repeat": bool(getattr(args, "host_seek_repeat", False)),
              "checkpoint_pair": bool(getattr(args, "checkpoint_pair", False)),
              "checkpoint_fallback": bool(getattr(args, "checkpoint_fallback", False)),
              "seek_publication_cancel": bool(getattr(args, "seek_publication_cancel", False)),
              "seek_observer_failure": bool(getattr(args, "seek_observer_failure", False)),
              "seek_advance_failure": bool(getattr(args, "seek_advance_failure", False)),
              "seek_drained_cancel": bool(getattr(args, "seek_drained_cancel", False)),
              "seek_settlement_failure": bool(getattr(args, "seek_settlement_failure", False)),
              "seek_ownership_protocol": "retained_B_through_C_v1" if getattr(args,"host_seek",False) else None,
              "checkpoint_pair_tick": 206 if getattr(args, "checkpoint_pair", False) else None,
              "record_index": bool(getattr(args, "record_index", False)),
              "index_end_hold_protocol": (("retained_source_stop_v3" if getattr(args,"host_session",False) and getattr(args,"index_seek",False) else "retained_native_range_v2")
                  if args.mode=="runtime" and bool(getattr(args,"record_index",False)) else None),
              "index_checkpoint": bool(getattr(args, "index_checkpoint", False)),
              "host_index_checkpoint": args.mode=="runtime" and bool(getattr(args,"index_checkpoint",False)),
              "host_index_checkpoint_arming": args.mode=="runtime" and bool(getattr(args,"index_checkpoint",False)),
              "index_extra_checkpoint": int(getattr(args,"index_extra_checkpoint",0)),
              "index_preparation_fallback": bool(getattr(args,"index_preparation_fallback",False)),
              "index_recovery": getattr(args,"index_recovery",""),
              "index_seek": bool(getattr(args, "index_seek", False)),
              "index_seek_target": int(getattr(args,"index_seek_target",208)),
              "observation_target": int(getattr(args,"observation_target",0)),
              "index_sequence": getattr(args,"index_sequence",""),
              "replay_budget_gib": int(getattr(args,"replay_budget_gib",1)),
              "rolling_cycles": int(getattr(args,"rolling_cycles",0)),
              "ground_motion_perturb": bool(getattr(args,"ground_motion_perturb",False)),
              "rolling_corrections": getattr(args,"rolling_corrections",""),
        "authored_prefix":getattr(args,"authored_prefix",""),
              "index_seek_continuation": int(getattr(args,"index_seek_continuation",120)),
              "index_cancel": bool(getattr(args, "index_cancel", False)),
              "native_session_exit": bool(getattr(args,"native_session_exit",False)),
              "host_session": bool(getattr(args,"host_session",False)),
              "host_seek": bool(getattr(args, "host_seek", False)),
              "host_seek_target": int(getattr(args, "host_seek_target", 208)),
              "host_seek_first_target": int(getattr(args, "host_seek_first_target", 0)),
              "seek_preparation_failure": bool(getattr(args,"seek_preparation_failure",False)),
              "pixel_diagnostics": bool(getattr(args, "pixel_diagnostics", False)),
              "coherence_images_requested": bool(getattr(args, "coherence_images", False)),
              "desktop_capture_timing_perturbed": False,
              "coherence_gpu_readbacks": bool(getattr(args, "coherence_images", False)),
              "pass_diagnostics": bool(getattr(args, "pass_diagnostics", False)),
              "probe_empty_interval": bool(getattr(args, "probe_empty_interval", False)),
              "probe_move_state": bool(getattr(args, "probe_move_state", False)),
              "probe_world_pause": bool(getattr(args, "probe_world_pause", False)),
              "probe_interior_pause": bool(getattr(args, "probe_interior_pause", False)),
              "probe_consumer_task": bool(getattr(args, "probe_consumer_task", False)),
              "consumer_mutation": bool(getattr(args, "consumer_mutation", False)),
              "c18_diagnostic": bool(getattr(args, "c18_diagnostic", False)),
              "probe_application_pause": bool(getattr(args, "probe_application_pause", False)),
              "probe_particle_copy": bool(getattr(args, "probe_particle_copy", False)),
              "probe_historical_restore": bool(getattr(args, "probe_historical_restore", False)),
              "historical_cancel": getattr(args, "historical_cancel", ""),
              "historical_advanced_tick": getattr(args, "historical_advanced_tick", 210),
              "historical_anchor_tick": getattr(args, "historical_anchor_tick", 205),
              "historical_exact_advance": getattr(args, "historical_exact_advance", False),
              "historical_single_step": getattr(args, "historical_single_step", False),
              "scene_vector_field_policy": "reject_nonempty_v1",
              "callback_admission_policy": "quiescent_streamable_v2",
              "probe_interactive_controls": bool(getattr(args, "probe_interactive_controls", False)),
              "indexed_control_protocol": ("retained_menu_exit_v1" if getattr(args,"index_recovery","") else "numeric_initial_seek_v2") if getattr(args,"index_seek",False) and getattr(args,"probe_interactive_controls",False) else None,
              "probe_small_restore": bool(getattr(args, "probe_small_restore", False)),
              "identities": {key: sha256_file(path) for key, path in (
                  ("runtime", args.dll), ("observer", args.replay_mod),
                  ("replay", args.replay), ("game", args.game_executable), ("framework", framework_source), ("ucrt", ucrt_path))}}
    report["diagnostic_settings"] = getattr(args, "diagnostic_settings", None)
    report["experiment_profile"] = getattr(args, "experiment_profile", None)
    report["capture_source_retention"] = getattr(args, "capture_source_retention", None)
    if args.mode == "stock" and getattr(args, 'physx', None):
        from .replay_controls_catalog import control_spec
        report["control_spec"] = control_spec(args)
    raw_path = args.report.with_suffix(".UE4SS.log")
    # Bind the exact deployed candidates to reconstructible source contents
    # before acquiring processes or modifying any deployment.
    source_root = Path(__file__).resolve().parents[2]
    report["build_provenance"] = require_build_provenance(
        source_root, source_root / "build_cmake_LessEqual421__Shipping__Win64",
        {"runtime": args.dll, "observer": args.replay_mod, "framework": framework_source})
    physics_path = getattr(args, "physx", None)
    if physics_path:
        physics_path = Path(physics_path).resolve()
        report["identities"]["physx"] = sha256_file(physics_path)
    raw_path.parent.mkdir(parents=True, exist_ok=True)
    cursor = capture_log_offset(args.log)
    run = ReplayRun(args.dll, args.deployed_dll, args.report,
                    runtime_enabled=args.mode == "runtime")
    try:
        with run, ExitStack() as process_diagnostics:
            resources = run.resources
            report["loader"] = prepare_replay_loader(resources,args.game_executable)
            report["overlay_required"]=prepare_replay_overlay(resources,args.game_executable,report)
            if framework_source != framework_deployed:
                resources.prepare_file(framework_deployed, [report["identities"]["framework"]])
                resources.publish_copy(framework_source, framework_deployed)
            from .configuration import read_fields
            from .run_resources import bytes_hash
            report["serial_particles"] = report["skip_intros"]
            fields = read_fields(args.config)
            fields.update(enabled="false", trace="true", replay_seeking="true" if report["host_session"] else "false", correction_probe="false",
                          forced_depth7_qualification="false")
            config_text = "".join(f"{key}={value}\n" for key, value in fields.items())
            resources.prepare_file(args.config, [bytes_hash(config_text)])
            args.config.write_bytes(config_text.encode("utf-8"))
            report["identities"]["config"] = sha256_file(args.config)
            root = qualification_root()
            root.mkdir(parents=True, exist_ok=True)
            diagnostic_settings = getattr(args, 'diagnostic_settings', None) or dict(
                stack_depth=0, mask=0, first_tick=0, last_tick=36000, byte_limit=1048576)
            diagnostic_path = root / "replay_diagnostics.ini"
            diagnostic_text = ''.join(f"{key}={value}\n" for key,value in diagnostic_settings.items())
            resources.prepare_file(diagnostic_path, [bytes_hash(diagnostic_text)])
            resources.publish_text(diagnostic_text, diagnostic_path)
            report['diagnostic_settings'] = diagnostic_settings
            report['diagnostic_reserved_bytes'] = 8192 + (294912 if report['probe_consumer_task'] else 0)
            report['diagnostic_output_limit_bytes'] = 2 * diagnostic_settings['byte_limit']

            if any((root / name).exists() for name in ("replay_request.txt", "replay_result.txt")):
                raise RuntimeError("existing replay request requires cleanup before control")
            resources.prepare_requests(root)
            if report['probe_consumer_task'] or report['consumer_mutation']:
                consumer_failure=root/'consumer_failure.json'
                resources.park_report(consumer_failure,allow_empty=True)
                niagara_observation=root/'niagara_observation.json'
                resources.park_report(niagara_observation,allow_empty=True)
                process_diagnostics.callback(retain_niagara_observation,report,niagara_observation,
                    resources.document['run_id'],args.report.parent/'evidence/objects')
                physics_observation=root/'physics_callback_observation.jsonl'
                resources.park_report(physics_observation)
                resources.prepare_temporary(physics_observation.with_name(physics_observation.name+'.tmp'))
                process_diagnostics.callback(retain_physics_callback_observation,report,physics_observation,
                    resources.document['run_id'],args.report.parent/'evidence/objects')
                process_diagnostics.callback(retain_consumer_failure,report,consumer_failure,
                    resources.document['run_id'],args.report.parent/'evidence/objects')
                report['diagnostic_reserved_bytes']+=4096+65536+VFX_OBSERVATION_OWNED_BYTES+PHYSICS_OBSERVATION_OWNED_BYTES
                # Both the last publication and an interrupted staging file
                # can coexist. Retain each before journal cleanup restores the
                # prior files.
                report['diagnostic_output_limit_bytes']+=32768+2*VFX_OBSERVATION_SIDECAR_BYTES+2*PHYSICS_OBSERVATION_SIDECAR_BYTES
            if report['probe_consumer_task'] or report['consumer_mutation'] or report['c18_diagnostic']:
                vfx_observation=root/'vfx_completion_observation.json'
                resources.park_report(vfx_observation)
                resources.prepare_temporary(vfx_observation.with_name(vfx_observation.name+'.tmp'))
                process_diagnostics.callback(retain_vfx_completion_observation,report,vfx_observation,
                    resources.document['run_id'],args.report.parent/'evidence/objects')
                if report['c18_diagnostic']:
                    report['diagnostic_reserved_bytes']+=VFX_OBSERVATION_OWNED_BYTES
                    report['diagnostic_output_limit_bytes']+=2*VFX_OBSERVATION_SIDECAR_BYTES
            if report['probe_consumer_task']:
                startup_failure=root/'consumer_startup_failure.json'
                resources.park_report(startup_failure)
                resources.prepare_temporary(root/'consumer_startup_failure.tmp')
                process_diagnostics.callback(retain_startup_failure,report,startup_failure,
                    resources.document['run_id'],args.report.parent/'evidence/objects')
            mod_root = args.deployed_dll.parents[2] / "ReplayQualificationMod"
            if mod_root.exists():
                raise RuntimeError("existing qualification bridge requires cleanup before control")
            resources.create_directory(mod_root)
            resources.create_directory(mod_root / "dlls")
            bridge = mod_root / "dlls" / "main.dll"
            resources.prepare_file(bridge, [report["identities"]["observer"]])
            shutil.copy2(args.replay_mod, bridge)
            enabled = mod_root / "enabled.txt"
            resources.prepare_file(enabled, [bytes_hash("")])
            enabled.write_text("")
            run_id = resources.document["run_id"]
            replay_path = str(args.replay.resolve())
            if any(char in replay_path for char in "\r\n"):
                raise RuntimeError("invalid replay path")
            temporary = root / "replay_request.tmp"
            capture_mode = "trajectory_match" if report["full_match"] else "trajectory"
            if getattr(args, "executor", False):
                if args.mode != "runtime":
                    raise RuntimeError("executor capture requires the runtime")
                capture_mode = "executor_match" if report["full_match"] else "executor"
            executor_options = (f"executor_yield_every_tick={str(bool(getattr(args, 'yield_every_tick', False))).lower()}\n"
                                if getattr(args, "executor", False) else "")
            if report["include_setup"]:
                executor_options += "include_setup=true\n"
            if report["observation_target"]:
                if not report["skip_intros"] or not 170<=report["observation_target"]<=35880:
                    raise RuntimeError("invalid bounded read-only observation target")
                executor_options += f"observation_target={report['observation_target']}\n"
            if report["index_checkpoint"]:
                executor_options += "index_checkpoint=true\n"
                if report["index_extra_checkpoint"]:
                    executor_options += f"index_extra_checkpoint={report['index_extra_checkpoint']}\n"
            if report["full_match"] and report["record_index"]:
                executor_options += f"index_seek_target={report['index_seek_target']}\nindex_seek_continuation={report['index_seek_continuation']}\n"
            if report["rolling_cycles"]:
                executor_options += f"rolling_cycles={report['rolling_cycles']}\n"
            if report["ground_motion_perturb"]:
                if not getattr(args,"executor",False) or report["rolling_cycles"]!=407:
                    raise RuntimeError("ground motion diagnostic requires completed rolling407 runtime")
                executor_options += "ground_motion_perturb=true\n"
            if report["replay_budget_gib"]!=1:
                if report["replay_budget_gib"]!=2 or not (report["host_session"] or report["rolling_cycles"]):
                    raise RuntimeError("debug replay budget requires a bounded host-session experiment")
                executor_options += "replay_budget_gib=2\n"
            if report["index_sequence"]:
                executor_options += f"index_sequence={report['index_sequence']}\n"
            if report["index_seek"]:
                executor_options += "index_seek=true\n"
                if report["index_preparation_fallback"]:
                    executor_options += "index_preparation_fallback=true\n"
                if report["index_recovery"]:
                    executor_options += f"index_recovery={report['index_recovery']}\n"
            if report["index_cancel"]:
                executor_options += "index_cancel=true\n"
            if report["native_session_exit"]:
                executor_options += "native_session_exit=true\n"
            if report["host_session"]:
                executor_options += "host_session=true\n"
            for name in ("completion_repeat", "changed_inputs", "corrected_inputs", "source_revision", "complete_seek_application", "capture_cancel", "particle_owner_copy", "particle_owner_registration", "particle_owner_gpu", "restore_reuse", "host_seek", "host_seek_repeat", "checkpoint_pair", "checkpoint_fallback", "seek_publication_cancel", "seek_observer_failure", "seek_advance_failure", "seek_settlement_failure", "seek_drained_cancel", "seek_preparation_failure", "record_index", "pixel_diagnostics", "pass_diagnostics"):
                if report[name]:
                    executor_options += name + "=true\n"
            if report["source_revision_profile"] != "historical":
                if not report["source_revision"] or not report["corrected_inputs"] or report["source_revision_profile"] != "guard201":
                    raise RuntimeError("invalid source revision profile")
                executor_options += "source_revision_profile=guard201\n"
            if report["host_seek"]:
                executor_options += f"host_seek_target={report['host_seek_target']}\n"
                if report["host_seek_first_target"]:
                    executor_options += f"host_seek_first_target={report['host_seek_first_target']}\n"
            if report["coherence_images_requested"]:
                executor_options += "coherence_images=true\n"
            if report["skip_intros"]:
                executor_options += "skip_intros=true\n"
            report['flush_startup_loading'] = bool(getattr(args,'flush_startup_loading',False))
            if report['flush_startup_loading']:
                if not report['skip_intros']: raise RuntimeError('startup flush requires matched native setup')
                executor_options += "flush_startup_loading=true\n"
            if report["probe_empty_interval"]:
                executor_options += "probe_empty_interval=true\n"
            if report["probe_move_state"]:
                executor_options += "probe_move_state=true\n"
            if report["probe_world_pause"]:
                executor_options += "probe_world_pause=true\n"
            if report["probe_interior_pause"]:
                executor_options += "probe_interior_pause=true\n"
            if report["probe_application_pause"]:
                executor_options += "probe_application_pause=true\n"
            if report["probe_consumer_task"]:
                if not report["probe_application_pause"]: raise RuntimeError('consumer task probe requires application pause')
                executor_options += "probe_consumer_task=true\n"
            if report["probe_particle_copy"]:
                executor_options += "probe_particle_copy=true\n"
            if report["probe_historical_restore"]:
                executor_options += "probe_historical_restore=true\n"
                if report["historical_anchor_tick"]!=205:
                    executor_options += f"historical_anchor_tick={report['historical_anchor_tick']}\n"
                if not historical_capture_interval_supported(report):
                    raise RuntimeError("unsupported bounded historical interval")
                executor_options += f"historical_advanced_tick={report['historical_advanced_tick']}\n"
                if report["historical_exact_advance"]:
                    from .replay_fidelity import bounded_checkpoint_window
                    if report["historical_cancel"] and not report["host_seek_repeat"] and not bounded_checkpoint_window(report):
                        raise RuntimeError("exact advancement requires committed A or the bounded publication-cancellation window")
                    executor_options += "historical_exact_advance=true\n"
                    if report["historical_single_step"]: executor_options += "historical_single_step=true\n"
                if report["historical_cancel"]:
                    if report["historical_cancel"] not in ("before", "after"):
                        raise RuntimeError("invalid historical cancellation point")
                    executor_options += "historical_cancel=" + report["historical_cancel"] + "\n"
            if report["probe_interactive_controls"]:
                executor_options += "probe_interactive_controls=true\n"
            if report["probe_small_restore"]:
                executor_options += "probe_small_restore=true\n"
            request_version,correction_options=rolling_correction_request(report["rolling_corrections"])
            if report["authored_prefix"]:
                request_version,correction_options=authored_prefix_request(report["authored_prefix"])
            executor_options+=correction_options
            if report["probe_consumer_task"]: request_version = 15
            if report["consumer_mutation"]:
                from .replay_fidelity import bounded_checkpoint_window
                if not bounded_checkpoint_window(report) or report["rolling_corrections"] or report["probe_consumer_task"]:
                    raise RuntimeError("consumer mutation requires its exclusive A210/B217 execution-recovery case")
                request_version=16
                executor_options+="consumer_mutation=true\n"
            if report['c18_diagnostic']:
                allowed={'include_setup=true','skip_intros=true','flush_startup_loading=true'}
                if (capture_mode!='trajectory' or args.mode!='runtime' or args.watch_frames!=360
                        or not {'include_setup=true','skip_intros=true'} <= set(executor_options.splitlines())
                        or any(line not in allowed for line in executor_options.splitlines())):
                    raise RuntimeError('C18 diagnostic requires exclusive ordinary-forward protocol')
                request_version=19
                executor_options+='c18_diagnostic=true\nc18_selection=combat_170_220\n'
            report['request_protocol']=int(request_version)
            report['native_timing_protocol']=1
            temporary.write_text(f"version={request_version}\nrun_id={run_id}\nreplay_path={replay_path}\n"
                f"watch_frames={args.watch_frames}\ncapture_mode={capture_mode}\n{executor_options}", encoding="utf-8")
            os.replace(temporary, root / "replay_request.txt")
            report["run_id"] = run_id
            report["native_fixed_seed"] = report["skip_intros"]
            report["single_game_thread"] = bool(getattr(args,"single_game_thread",False))
            report["no_native_threading"] = bool(getattr(args,"no_native_threading",False))
            report["no_async_loading_thread"] = bool(getattr(args,"no_async_loading_thread",False))
            pid = run.launch(args.game_executable, launch_options)
            exit_witness = ProcessExitWitness(pid)
            def retain_process_exit():
                try:
                    code = exit_witness.exit_code()
                    report["process_exit"] = {"pid": pid, "code": code,
                        "hex": None if code is None else f"0x{code:08x}",
                        "observed_before_owned_cleanup": True}
                except OSError as error:
                    report["process_exit_diagnostic_error"] = str(error)
                finally:
                    try:
                        exit_witness.close()
                    except OSError as error:
                        report["process_exit_handle_error"] = str(error)
            process_diagnostics.callback(retain_process_exit)
            if report["native_fixed_seed"] and "-fixedseed" not in [part.lower() for part in psutil.Process(pid).cmdline()]:
                raise RuntimeError("Steam did not forward the native fixed-seed option")
            if report['single_game_thread']:
                if '-onethread' not in [part.lower() for part in psutil.Process(pid).cmdline()]:
                    raise RuntimeError('Steam did not forward the single-game-thread option')
                report['single_game_thread_option_forwarded']=True
            if report['no_native_threading']:
                if '-nothreading' not in [part.lower() for part in psutil.Process(pid).cmdline()]:
                    raise RuntimeError('Steam did not forward the no-native-threading option')
                report['native_threading_option_forwarded']=True
            if report['no_async_loading_thread']:
                forwarded = {part.lower() for part in psutil.Process(pid).cmdline()}
                if '-noasyncloadingthread' not in forwarded or '-asyncloadingthread' in forwarded:
                    raise RuntimeError('Steam did not forward the exclusive native loading option')
                report['native_loading_option_forwarded'] = True
            deadline = time.monotonic() + args.timeout
            process = psutil.Process(pid)
            while True:
                paths = {Path(row.path).resolve() for row in process.memory_maps()
                         if row.path and not row.path.startswith("[")}
                if bridge.resolve() in paths:
                    break
                if time.monotonic() >= deadline:
                    raise RuntimeError("passive observer did not load before setup deadline")
                time.sleep(0.25)
            if sha256_file(bridge) != report["identities"]["observer"]:
                raise RuntimeError("loaded observer identity mismatch")
            report["loaded_runtime"] = run.verify_loaded_module(pid)
            loader_path=Path(report["loader"]["path"])
            if (loader_path not in {Path(row.path).resolve() for row in process.memory_maps() if row.path}
                or sha256_file(loader_path)!=report["loader"]["sha256"]):
                raise RuntimeError("loaded replay shim identity mismatch")
            report["loaded_loader"]={**report["loader"],"pid":pid,"process_created":process.create_time(),
                "verification":"owned_process_mapped_file_and_sha256"}
            def verify_framework():
                run.verify_loaded_module(pid)  # Also proves this PID is still owned.
                mapped = {Path(row.path).resolve() for row in process.memory_maps()
                          if row.path and Path(row.path).name.casefold() == "ue4ss.dll"}
                expected = report["identities"]["framework"]
                if (mapped != {framework_deployed} or sha256_file(framework_source) != expected
                        or sha256_file(framework_deployed) != expected):
                    raise RuntimeError("loaded framework identity mismatch")
                return {"path": str(framework_deployed), "sha256": expected, "pid": pid,
                        "process_created": process.create_time(), "verification": "owned_process_mapped_file_and_sha256"}
            report["loaded_framework"] = verify_framework()
            def verify_ucrt():
                run.verify_loaded_module(pid)
                mapped = {Path(row.path).resolve() for row in process.memory_maps()
                          if row.path and Path(row.path).name.casefold() == "ucrtbase.dll"}
                expected = report["identities"]["ucrt"]
                if mapped != {ucrt_path} or sha256_file(ucrt_path) != expected:
                    raise RuntimeError("loaded native CRT identity mismatch")
                return {"path": str(ucrt_path), "sha256": expected, "pid": pid,
                        "process_created": process.create_time(), "verification": "owned_process_mapped_file_and_sha256"}
            report["loaded_ucrt"] = verify_ucrt()
            def verify_physics(wait_for_load=False):
                if not physics_path:
                    return None
                # UE4SS loads before the engine's delay-loaded physics module.
                # Absence during startup may wait; a different mapping may not.
                while True:
                    mapped = {Path(row.path).resolve() for row in process.memory_maps(grouped=False)
                              if row.path and Path(row.path).name.casefold() == "physx3_x64.dll"}
                    if mapped or not wait_for_load or time.monotonic() >= deadline:
                        break
                    time.sleep(0.25)
                if mapped != {physics_path} or sha256_file(physics_path) != report["identities"]["physx"]:
                    raise RuntimeError(f"loaded PhysX identity mismatch: expected={physics_path}, mapped={sorted(map(str, mapped))}")
                return {"path": str(physics_path), "sha256": report["identities"]["physx"], "pid": pid,
                        "process_created": process.create_time(), "verification": "owned_process_mapped_file_and_sha256"}
            if physics_path:
                report["loaded_physx"] = verify_physics(wait_for_load=True)
            report["loaded_observer"] = {"path": str(bridge), "sha256": sha256_file(bridge),
                "pid": pid, "process_created": process.create_time()}
            from .replay_controls import IndexedControlEvents
            control_events=IndexedControlEvents() if report["index_seek"] and report["probe_interactive_controls"] else None
            c18_events=IndexedControlEvents() if report['c18_diagnostic'] else None
            if c18_events is not None:
                c18_events.keys=(b'C18 combat diagnostic return run_id=',)
            diagnostic_log_state = {}
            def guard():
                code = exit_witness.exit_code()
                if code is not None:
                    raise RuntimeError(f"owned control process exited (0x{code:08x})")
                memory = process.memory_info()
                report["peak_process_working_set_bytes"] = max(
                    report.get("peak_process_working_set_bytes", 0),
                    getattr(memory, "peak_wset", memory.rss))
                with args.log.open("rb") as log:
                    log.seek(0, 2)
                    log.seek(max(0, log.tell() - 65536))
                    tail = log.read().decode("utf-8", errors="replace")
                from .replay_diagnostics import scan_new_ground_rejections
                for event in scan_new_ground_rejections(args.log, run_id, diagnostic_log_state):
                    raise RuntimeError(f"ground update admission rejected: {event.get('check')} tick={event.get('tick')}")
                if (report.get('no_async_loading_thread') and 'native_loading_state' not in report
                        and ('startup manager readiness' in tail or 'trajectory ordinal=' in tail)):
                    run.verify_loaded_module(pid)
                    report['native_loading_state'] = read_native_loading_state(pid, args.game_executable, report['identities']['game'])
                    validate_native_loading_state(report['native_loading_state'])
                if f"replay request rejected run_id={run_id} " in tail:
                    raise RuntimeError("native request parser rejected this run's protocol options")
                failure=re.search(r"\[ReplayQualification\] run failed run_id="+re.escape(run_id)+r" reason=([^\s]+)",tail)
                if failure:
                    raise RuntimeError("native observer failed: "+failure.group(1))
                if "replay diagnostic overflow=true" in tail:
                    raise RuntimeError("diagnostic byte budget exhausted; incomplete evidence")
                if report.get("rolling_cycles"):
                    commit_failure=re.search(r"\[HorseMod\] historical commit retirement failed cursor=(\d+) code=(\d+)",tail)
                    if commit_failure:
                        raise RuntimeError("rolling commit retirement failed: cursor="+commit_failure.group(1)+" code="+commit_failure.group(2))
                    marker_failure=re.search(r"\[HorseMod\] physics marker commit preflight rejected check=([^\s]+) code=(\d+)",tail)
                    if marker_failure:
                        raise RuntimeError("rolling physics marker commit preflight failed: "+marker_failure.group(1)+" code="+marker_failure.group(2))
                    surface_failure=re.search(r"\[HorseMod\] replay surface command rejected command=(\d+) tick=(\d+)",tail)
                    if surface_failure:
                        raise RuntimeError("rolling surface command rejected: command="+surface_failure.group(1)+" tick="+surface_failure.group(2))
                    preparation_failure=re.search(r"\[HorseMod\] restore preparation failed participant=([^\s]+) code=(\d+)",tail)
                    if preparation_failure:
                        raise RuntimeError("rolling restore preparation failed: "+preparation_failure.group(1)+" code="+preparation_failure.group(2))
                    undo_failure=re.search(r"\[HorseMod\] historical undo participant=([^\s]+) code=18(?:\s|$)",tail)
                    if undo_failure:
                        raise RuntimeError("rolling native undo failed: "+undo_failure.group(1))
                if report["coherence_images_requested"]:
                    collect_coherence_images(report,args.report,pid,tail)
                if report["index_seek"] and report["probe_interactive_controls"]:
                    from .replay_controls import drive_indexed_controls,capture_indexed_control_hold
                    event_text=control_events.read(args.log)
                    report["indexed_ui_event_reader"]=control_events.witness()
                    capture_indexed_control_hold(report,args.report,pid,event_text)
                    drive_indexed_controls(report,pid,event_text,click_owned_replay_control,focus_owned_replay_window)
                if report["historical_single_step"] and not report["completion_repeat"] and "ui_step_input" not in report:
                    if "ui_step_focus" not in report:
                        focus = focus_owned_replay_window(pid)
                        if focus:
                            report["ui_step_focus"] = focus
                    controls = re.findall(r"replay step control hwnd=(\d+) screen_x=(-?\d+) screen_y=(-?\d+)", tail)
                    if controls and f"historical step control waiting run_id={run_id} tick=208 " in tail:
                        report["ui_step_input"] = click_owned_replay_control(pid, *(int(v) for v in controls[-1]))
                if (report["seek_preparation_failure"] or (report.get("seek_settlement_failure") and report["probe_interactive_controls"])) and "ui_cancel_input" not in report:
                    if "ui_cancel_focus" not in report:
                        focus = focus_owned_replay_window(pid)
                        if focus:
                            report["ui_cancel_focus"] = focus
                    controls = re.findall(r"replay cancel control hwnd=(\d+) screen_x=(-?\d+) screen_y=(-?\d+)", tail)
                    if controls and re.search(r"historical cancel control waiting run_id="+re.escape(run_id)+r" tick=(214|220)",tail):
                        report["ui_cancel_input"] = click_owned_replay_control(pid, *(int(v) for v in controls[-1]))
                if report.get("seek_settlement_failure") and report["probe_interactive_controls"] and "ui_resume_input" not in report:
                    controls=re.findall(r"replay resume control hwnd=(\d+) screen_x=(-?\d+) screen_y=(-?\d+)",tail)
                    if controls and f"historical resume control waiting run_id={run_id} tick=220 recovered_B=true" in tail:
                        report["ui_resume_input"]=click_owned_replay_control(pid,*(int(v) for v in controls[-1]))
                if report["probe_interactive_controls"] and not report["index_seek"] and not report.get("seek_settlement_failure") and "ui_input" not in report:
                    if "ui_focus" not in report and "[GameImGui] DX11 overlay initialised " in tail:
                        focus = focus_owned_replay_window(pid)
                        if focus:
                            report["ui_focus"] = focus
                    # The combat probe reaches its hold much later than the
                    # intro probe. Refresh focus once during the first authored
                    # countdown samples, while the native application pump is
                    # still running and at least 120 ticks remain before hold.
                    recent = re.findall(r"trajectory ordinal=\d+ phase=engine_post sample_version=[2345] frame=(\d+) ([^\r\n]+)", tail)
                    if (not report["probe_small_restore"] and "ui_focus_before_combat" not in report
                            and recent and re.search(r"source_active=true .*round_state=1 .*world_mode=1(?:\s|$)", recent[-1][1])
                            and f"interior pause started run_id={run_id} " not in tail):
                        focus = focus_owned_replay_window(pid)
                        if focus:
                            report["ui_focus_before_combat"] = dict(focus, native_frame=int(recent[-1][0]))
                    control = re.search(r"replay resume control hwnd=(\d+) screen_x=(-?\d+) screen_y=(-?\d+)", tail)
                    if control and f"interior pause started run_id={run_id} " in tail:
                        report["ui_input"] = click_owned_replay_control(pid, *(int(v) for v in control.groups()))
                if report['c18_diagnostic']:
                    from .replay_entry import require_replay_request_healthy
                    require_replay_request_healthy(run_id)
                    # Reuse the bounded incremental log reader so a busy native
                    # logging burst cannot push the one-shot marker out of tail.
                    receipt=c18_diagnostic_return_marker(c18_events.read(args.log),run_id)
                    report['c18_diagnostic_log_reader']=c18_events.witness()
                    if receipt is not None:
                        raise _C18DiagnosticObserved(receipt)
            try:
                result = wait_for_replay_entry(run_id,
                    max(0.1, deadline - time.monotonic()), guard)
            except _C18DiagnosticObserved as observed:
                if not report['c18_diagnostic']:
                    raise
                report['c18_diagnostic_stop']=observed.receipt
            else:
                if report['c18_diagnostic']:
                    # One bounded native forward window exhausted. Never switch
                    # to a historical/consumer probe to obtain a missing row.
                    raise RuntimeError('C18 selected return not observed in bounded forward window')
                if result.reason != "trajectory_control_complete":
                    raise RuntimeError("control completed at the wrong native boundary")
            run.verify_loaded_module(pid)
            verify_framework()
            verify_ucrt()
            verify_physics()
            if sha256_file(bridge) != report["identities"]["observer"]:
                raise RuntimeError("observer changed during control")
            if report['no_async_loading_thread']:
                run.verify_loaded_module(pid)
                report['native_loading_state'] = read_native_loading_state(pid, args.game_executable, report['identities']['game'])
                validate_native_loading_state(report['native_loading_state'])
            if report['flush_startup_loading']:
                raw = args.log.read_text(encoding='utf-8',errors='replace')
                report['startup_loading_flush'] = startup_loading_receipt(raw,run_id)
            report["observations_requested"] = args.watch_frames
            close_game(pid, timeout_seconds=30)
            exit_code = exit_witness.exit_code()
            if exit_code != 0:
                raise RuntimeError(f"owned control process did not exit cleanly after observation: code={exit_code}")
            if report.get('c18_diagnostic', False):
                report['c18_diagnostic_stop']=c18_diagnostic_stop_receipt(vfx_observation,run_id,
                    args.report.parent/'evidence/objects',report['c18_diagnostic_stop'])
            report["result"] = "diagnostic_observed" if report.get("c18_diagnostic", False) else "captured"
    except Exception as error:
        report["failure"] = str(error)
    finally:
        report["elapsed_seconds"] = round(time.monotonic() - started, 3)
        # Full indexing plus historical resimulation emits both traversals.
        # Retain a bounded complete witness rather than silently dropping its prefix.
        log_limit = (256 if report.get("rolling_cycles") in (407,408,600) else 128 if report.get("index_seek") else 64) * 1024 * 1024
        raw = _read_bounded_log_since(args.log, cursor, log_limit)
        raw_path.write_bytes(raw)
        report["raw_log"] = {"path": str(raw_path.resolve()), "sha256": sha256_file(raw_path),
            "limit_bytes": log_limit, "at_capacity": len(raw) >= log_limit}
        if report["coherence_images_requested"] and report.get("loaded_observer"):
            try:
                collect_coherence_images(report,args.report,report["loaded_observer"]["pid"],raw.decode("utf-8",errors="replace"))
            except (OSError,RuntimeError) as error:
                report.update(result="fail",failure="coherence collection: "+str(error))
        if report["probe_historical_restore"]:
            text = raw.decode("utf-8", errors="replace")
            failures = re.findall(r"\[HorseMod\] (historical restore preflight|checkpoint component failed|checkpoint lease failure) ([^\r\n]+)", text)
            if failures:
                kind, fields = failures[0]
                report["first_mechanism_failure"] = dict(re.findall(r"(\w+)=([^\s]+)", fields), failure_kind=kind)
                # UObject full names contain a class separator. Keep the full
                # terminal owner field so the retained raw rejection is usable.
                if kind == "checkpoint lease failure" and " owner=" in fields:
                    report["first_mechanism_failure"]["owner"] = fields.split(" owner=", 1)[1]
            detail = re.findall(r"historical scheduler check=([^\s]+) address=([^\s]+)", text)
            if detail:
                report["scheduler_failure"] = dict(zip(("check", "address"), detail[0]))
            report["historical_milestone_complete"] = False
            report["paused_image_is_native_rendering_proof"] = False
        if report["result"] == "captured":
            from .replay_fidelity import (validate_payload_handoff, validate_initial_snapshot,
                                         validate_trajectory_completion, validate_boundary_capture, expected_recovery_rewind, validate_source_extents, validate_trajectory_timing)
            try:
                report["payload_handoff"] = validate_payload_handoff(
                    raw.decode("utf-8", errors="replace"), report["run_id"])
                report["initial_snapshot"] = validate_initial_snapshot(
                    raw.decode("utf-8", errors="replace"), report["run_id"])
                report["timing"] = validate_trajectory_timing(
                    raw.decode("utf-8", errors="replace"), report["run_id"])
                report["source_extents"] = validate_source_extents(
                    raw.decode("utf-8", errors="replace"), report["run_id"])
                report["completion"] = validate_trajectory_completion(
                    raw.decode("utf-8", errors="replace"), report["run_id"],
                    args.watch_frames, report["full_match"])
                if report.get("index_seek"):
                    from .replay_fidelity import indexed_seek_boundary_streams
                    boundaries=indexed_seek_boundary_streams(raw.decode("utf-8", errors="replace"),report["run_id"],recovery=report.get("index_recovery",""))[0]
                else:
                    from .replay_fidelity import execution_fallback_case
                    expected_rewinds = expected_historical_rewinds(report)
                    boundaries = validate_boundary_capture(raw.decode("utf-8", errors="replace"), report["run_id"],
                        historical_restore=report["probe_historical_restore"] and (not report["historical_cancel"] or report["host_seek_repeat"] or report.get("seek_advance_failure") or report.get("consumer_mutation") or report.get("completion_repeat")),
                        historical_advanced_tick=report["historical_advanced_tick"],
                        expected_rewinds=expected_rewinds,
                        recovery_rewind=expected_recovery_rewind(report,raw.decode("utf-8", errors="replace")),
                        execution_retry=execution_fallback_case(report),
                        retained_execution=report.get("seek_ownership_protocol")=="retained_B_through_C_v1")
                if not boundaries:
                    raise RuntimeError("current observer produced no native boundary evidence")
                report["native_boundary_observations"] = len(boundaries)
            except RuntimeError as error:
                report.update(result="fail", failure=str(error))
        report["cleanup"] = {"complete": hasattr(run, "resources")
            and run.resources.document["state"] == "clean",
            "games_remaining": len(list_game_processes())}
        if b"replay diagnostic overflow=true" in raw:
            report.update(result="fail", failure="diagnostic byte budget exhausted; incomplete evidence")
        if len(raw) >= log_limit:
            report.update(result="fail", failure="raw log reached its bounded retention capacity; complete evidence unavailable")
        if not report["cleanup"]["complete"]:
            report["result"] = "fail"
        write_report(args.report, report)
    print(f"{report['result']}: {args.report}")
    if "failure" in report:
        print(report["failure"])
    return 0 if report["result"] in ("captured", "diagnostic_observed") else 1
