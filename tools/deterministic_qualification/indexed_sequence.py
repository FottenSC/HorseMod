"""Bounded retained-session sequence evidence. Never supplies simulation state."""
from __future__ import annotations
import re

TARGETS={1:(208,2510,2511,2512,208),2:(5574,6417,11000,2517)}


def sequence_cases(text: str, identity: str) -> list[dict]:
    pattern=r"indexed sequence case run_id=(\S+) sequence=(\d+) case=(\d+) target=(\d+) B=(\d+) callbacks=(\d+) interval=(\d+) previous_release_completed=(true|false)"
    markers=list(re.finditer(pattern,text))
    completed=list(re.finditer(r"indexed sequence completed run_id=(\S+) sequence=(\d+) cases=(\d+) continuation_ticks_each=120",text))
    if not markers or len(completed)!=1:raise RuntimeError("indexed sequence incomplete")
    kind=int(markers[0][2]);targets=TARGETS.get(kind,())
    if len(markers)!=len(targets) or completed[0].group(1,2,3)!=(identity,str(kind),str(len(targets))):
        raise RuntimeError("indexed sequence coverage differs from requested cases")
    result=[]
    for ordinal,(marker,target) in enumerate(zip(markers,targets)):
        if (marker[1]!=identity or int(marker[2])!=kind or int(marker[3])!=ordinal or int(marker[4])!=target
                or marker[8]!=str(ordinal>0).lower()):raise RuntimeError("indexed sequence case identity/order invalid")
        # The final simulation receipt precedes its HUD/presentation tail.
        # Keep the tail; per-tick parsers still require unique exact observations.
        end=markers[ordinal+1].start() if ordinal+1<len(markers) else len(text)
        part=text[marker.end():end]
        begin=list(re.finditer(r"indexed seek begin run_id=(\S+) origin=(\d+) target=(\d+) supported_index=true host_checkpoint=true callbacks=(\d+) interval=(\d+)",part))
        selected=list(re.finditer(r"indexed checkpoint selected run_id=(\S+) tick=(\d+) extra=(\d+) automatic=true",part))
        suffix=list(re.finditer(r"indexed seek continuation run_id=(\S+) first=(\d+) last=(\d+) ticks=(\d+)",part))
        if len(begin)!=1 or len(selected)!=1 or len(suffix)!=1:raise RuntimeError("indexed sequence case lacks unique seek/selection/continuation")
        origin,callbacks,interval=map(int,marker.group(5,6,7));anchor=int(selected[0][2])
        if (begin[0].group(1,2,3,4,5)!=(identity,str(origin),str(target),str(callbacks),str(interval))
                or selected[0][1]!=identity or not 170<=anchor<=min(origin,target)
                or suffix[0].group(1,2,3,4)!=(identity,str(target+1),str(target+120),'120')
                or not begin[0].start()<selected[0].start()<suffix[0].start()
                or (ordinal and origin!=targets[ordinal-1]+121)):
            raise RuntimeError("indexed sequence B/target/continuation coordinates disagree")
        result.append(dict(case=ordinal,target=target,origin=origin,checkpoint=anchor,interval=interval,
            prefix_callbacks=callbacks,last_tick=target+120,continuation_ticks=120,
            start_offset=marker.start(),end_offset=end,completion_offset=marker.end()+suffix[0].start()))
    if not markers[-1].start()<completed[0].start():raise RuntimeError("indexed sequence completion precedes cases")
    return result


def boundary_streams(text: str, identity: str):
    from .replay_fidelity import retained_source_range,validate_boundary_capture
    cases=sequence_cases(text,identity);supported=retained_source_range(text,identity)
    closures=re.findall(r"index closure run_id=(\S+) replay_tick=(\d+) native_tick=(\d+) callbacks_retained=true",text)
    if (not supported or len(closures)!=1 or closures[0]!=(identity,str(supported['source_tick']),str(supported['last_tick']))
            or cases[0]['origin']!=supported['last_tick'] or any(c['last_tick']>supported['source_tick'] for c in cases)):
        raise RuntimeError("indexed sequence lacks complete retained index origin")
    rows=validate_boundary_capture(text,identity,historical_restore=True,
        expected_rewinds=[(c['origin'],c['checkpoint']) for c in cases],retained_execution=True)
    for case in cases:
        split=case['prefix_callbacks'];before=text[:case['start_offset']]
        end=len(re.findall(r"\[ReplayQualification\] boundary ordinal=",text[:case['completion_offset']]))
        if (len(re.findall(r"\[ReplayQualification\] boundary ordinal=",before))!=split
                or not 0<split<end<=len(rows) or int(rows[split-1]['frame'])!=case['origin']
                or int(rows[end-1]['frame'])!=case['last_tick'] or rows[end-1]['phase']!='actor_tail'):
            raise RuntimeError("indexed sequence physical callback boundaries disagree")
        case['end_callbacks']=end
    prefix=cases[0]['prefix_callbacks']
    return rows,rows[:prefix],rows[prefix:],dict(cases=cases,origin=cases[0]['origin'],
        checkpoint=cases[0]['checkpoint'],target=cases[0]['target'],interval=cases[0]['interval'],
        last_tick=cases[-1]['last_tick'],continuation_ticks=120,sequence=True)
