"""Independent MP4 decoding checks; PyAV is a development-only dependency."""
import argparse
import json
import sys
from pathlib import Path
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'artifacts/python'))
import av
import numpy as np

parser=argparse.ArgumentParser()
parser.add_argument('folder',type=Path)
parser.add_argument('--min-duration',type=float,default=1)
args=parser.parse_args()
reports=[]
for path in sorted(args.folder.glob('*.mp4')):
    c=av.open(str(path));types=[s.type for s in c.streams]
    assert 'video' in types and 'audio' in types,types
    assert c.streams.video[0].codec_context.name=='h264'
    assert c.streams.audio[0].codec_context.name=='aac'
    count={'video':0,'audio':0};first={};last={};keys=[];peak=0.;variance=0.
    for f in c.decode():
        kind='audio' if isinstance(f,av.AudioFrame) else 'video'
        t=float(f.pts*f.time_base)
        assert t>=last.get(kind,-1),f'Non-monotonic {kind}'
        first.setdefault(kind,t);last[kind]=t;count[kind]+=1
        if kind=='audio':peak=max(peak,float(np.abs(f.to_ndarray()).max()))
        else:
            if f.key_frame:keys.append(t)
            if count[kind]==1:
                assert f.key_frame,'First frame is not independently decodable'
                variance=float(f.to_ndarray(format='gray').std())
    assert count['video']>0 and count['audio']>0
    assert last['video']>=args.min_duration-.1
    assert abs(first['audio']-first['video'])<.1,(first,path)
    assert abs(last['audio']-last['video'])<.15,(last,path)
    assert variance>1,'Unexpected blank video'
    assert len(keys)<2 or max(np.diff(keys))<1.1,'GOP too long'
    reports.append({'file':path.name,'frames':count,'first_pts':first,'last_pts':last,'audio_peak':peak,'first_frame_std':variance,'max_keyframe_gap':float(max(np.diff(keys))) if len(keys)>1 else None,'file_bytes':path.stat().st_size})
assert reports,'No MP4 files found'
output=args.folder/'decode-report.json';output.write_text(json.dumps({'decoder':av.__version__,'results':reports},indent=2),encoding='utf-8')
print(json.dumps({'decoded_files':len(reports),'report':str(output)}))
