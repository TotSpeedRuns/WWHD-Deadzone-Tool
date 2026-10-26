"""Read this plugin's published diagnostic record over Aroma TCP Gecko.

Close the Aroma plugin menu in the current boot immediately before running.
Uses FTP only to read the descriptor/config; Gecko only receives peekmultiple.
"""
import argparse, base64, ftplib, io, json, math, socket, struct, time
from pathlib import Path
from datetime import datetime, timezone
from gecko_address import address_argument

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--host', required=True, help='Your Wii U LAN address')
p.add_argument('--phase', choices=['before','after'], required=True)
p.add_argument('--seconds', type=int, default=15)
p.add_argument('--output-dir', type=Path, required=True)
a = p.parse_args()
assert 1 <= a.seconds <= 60
a.output_dir.mkdir(parents=True, exist_ok=True)
buf = io.BytesIO()
with ftplib.FTP(a.host, timeout=8) as ftp:
    ftp.login()
    ftp.retrbinary('RETR /fs/vol/external01/wiiu/environments/aroma/plugins/config/wwhd_deadzone_lab.json', buf.write)
items = json.loads(buf.getvalue())['storageitems']
descriptor = base64.b64decode(items['gecko_diagnostics_v1'], validate=True)
magic, schema, address, size, session = struct.unpack('>5I', descriptor)
assert magic == 0x57445a31 and schema == 1 and size == 300
assert address % 4 == 0 and 0x10000000 <= address < 0xbfff0000
previous_path = a.output_dir/'before.json'
previous = json.loads(previous_path.read_text()) if a.phase == 'after' else None
if previous:
    assert previous['session'] != session, 'Session unchanged: restart not established; close menu after relaunching'

regions=[(address,size)]
pro_region=None
if 'gecko_pro_native_v1' in items:
    pm,ps,pa,pn,pt=struct.unpack('>5I',base64.b64decode(items['gecko_pro_native_v1'],validate=True))
    assert pm==0x50524f31 and ps==1 and pn==240 and pt==session
    assert pa%4==0 and 0x10000000<=pa<0xbfff0000
    pro_region=(pa,pn); regions.append(pro_region)

pad_region=None
if 'gecko_gamepad_native_v1' in items:
    gm,gs,ga,gn,gt=struct.unpack('>5I',base64.b64decode(items['gecko_gamepad_native_v1'],validate=True))
    assert gm==0x47504431 and (gs,gn) in ((1,72),(2,88),(3,92)) and gt==session
    assert ga%4==0 and 0x10000000<=ga<0xbfff0000
    pad_region=(ga,gn);regions.append(pad_region)

sock = socket.create_connection((a.host,7332), timeout=8)
sock.settimeout(8)
def read(start, length):
    assert any(base <= start < start+length <= base+span for base,span in regions)
    assert start % 4 == 0 and length % 4 == 0
    pointers = list(range(start,start+length,4))
    assert len(pointers) <= 32
    command = ('peekmultiple -t u32'+''.join(' -a '+address_argument(x) for x in pointers)).encode()
    sock.sendall(command)
    reply = bytearray()
    while reply.count(b'|') < len(pointers):
        chunk = sock.recv(512)
        if not chunk: raise RuntimeError('Gecko disconnected')
        reply.extend(chunk)
        if len(reply)>len(pointers)*12+100: raise RuntimeError('Unexpected Gecko reply')
    words = bytes(reply).split(b'|')
    assert len(words) == len(pointers)+1 and words[-1] == b''
    assert all(w.isdigit() and int(w)<=0xffffffff for w in words[:-1])
    return b''.join(struct.pack('>I',int(w)) for w in words[:-1])

def header():
    words = struct.unpack('>10I',read(address,40))
    assert words[:3] == (magic, schema, session), 'Live diagnostic record does not match current descriptor'
    title = (words[3]<<32)|words[4]
    assert title in (0x0005000010143500,0x0005000010143599,
                     0x0005000010143600,0x0005000010575a01), 'Supported game not running'
    return {'title':f'{title:016x}', 'replay_calls':words[5], 'actual_clamps':list(words[6:10])}

def expected(x, y, amount):
    if amount <= 0: return [x,y]
    r = math.hypot(x,y); d=amount/100
    if r <= d: return [0.0,0.0]
    factor=(min(r,1)-d)/(1-d)/r
    return [x*factor,y*factor]

report = {'phase':a.phase,'utc':datetime.now(timezone.utc).isoformat(),
    'session':session,'record_address':f'{address:08x}', 'config':items,
    'samples':[], 'read_only':True, 'full_physical_response_tested':False}
def gamepad_native_status():
    if not pad_region:return None
    words=struct.unpack('>'+str(pad_region[1]//4)+'I',read(pad_region[0],pad_region[1]))
    assert words[0]==0x47504431 and words[1] in (1,2,3) and words[2]==session
    result={'layout_validated':bool(words[3]),'state':words[4],'writes':words[5],'verified_reads':words[6],
            'actual_centers':list(words[8:12]),'actual_clamps':list(words[12:16]),'origin_lock':words[16],'cross':words[17]}
    result['resolve_stage']=words[7]
    if len(words)>22: result['origin_ready']=words[22]
    if len(words)>18:
        result['failure']={'word_index':words[18],'actual':f'{words[19]:08x}','expected':f'{words[20]:08x}','mask':f'{words[21]:08x}'}
    if 'named_profiles_v2' in items:
        bank=base64.b64decode(items['named_profiles_v2'],validate=True)
        schema,selected=struct.unpack_from('>2I',bank)
        assert schema==2 and selected<8 and len(bank)==1000
        offset=8+selected*124+88
        saved_schema=struct.unpack_from('>I',bank,offset)[0]
        result['selected_slot']=selected+1
        result['saved_centers_present']=saved_schema==1
        if saved_schema==1:
            clamps=list(struct.unpack_from('>4i',bank,offset+4));centers=list(struct.unpack_from('>4h',bank,offset+20))
            locked,cross=struct.unpack_from('>2I',bank,offset+28)
            result['readback_matches']=words[4] in (4,5) and result['actual_centers']==centers and result['actual_clamps']==clamps and words[16:18]==(locked,cross)
    return result

def native_status():
    if not pro_region: return None
    pa,_=pro_region
    pm,ps,pt,layout=struct.unpack('>4I',read(pa,16))
    assert (pm,ps,pt)==(0x50524f31,1,session)
    channels=[]
    for i in range(4):
        words=struct.unpack('>14I',read(pa+16+i*56,56))
        row={'channel':i,'state':words[0],'verified_reads':words[1],
             'before':[struct.unpack('>i',struct.pack('>I',v))[0] for v in words[2:6]],
             'after':[struct.unpack('>i',struct.pack('>I',v))[0] for v in words[6:10]],
             'writes':words[12],'profile':words[13]}
        if words[13]<4:
            snapshot=base64.b64decode(items[f'pro_native_v1_{words[13]}'])
            saved_origin=list(struct.unpack_from('>4i',snapshot,4))
            identity=struct.pack('>2I',*words[10:12])[:6]
            row['identity_matches']=identity==snapshot[32:38]
            row['origin_readback_matches']=row['after']==saved_origin
        channels.append(row)
    return {'firmware_layout_validated':bool(layout),'channels':channels}
try:
    report['start']=header()
    report['pro_start']=native_status()
    report['gamepad_native_start']=gamepad_native_status()
    end=time.monotonic()+a.seconds
    while time.monotonic()<end:
        for index in range(5):
            start=address+40+index*52
            for attempt in range(5):
                data=read(start,52); seq=struct.unpack_from('>I',data)[0]
                if seq%2 == 0 and read(start,4) == data[:4]: break
            else: continue
            raw=list(struct.unpack_from('>4f',data,4)); out=list(struct.unpack_from('>4f',data,20))
            enabled,left,right,reads=struct.unpack_from('>4I',data,36)
            if reads == 0: continue
            assert enabled in (0,1) and 0<=left<=50 and 0<=right<=50
            assert all(math.isfinite(x) for x in raw+out)
            want=expected(*raw[:2],left if enabled else 0)+expected(*raw[2:],right if enabled else 0)
            configured=[items[f'profile_{index}_{j}'] for j in range(3)]
            report['samples'].append({'channel':index,'reads':reads,'enabled':enabled,
                'left':left,'right':right,'raw':raw,'output':out,
                'config_matches':configured==[bool(enabled),left,right],
                'output_matches':all(abs(x-y)<2e-5 for x,y in zip(out,want))})
        time.sleep(.1)
    report['end']=header()
    report['pro_end']=native_status()
    report['gamepad_native_end']=gamepad_native_status()
    report['channels_observed']=sorted({s['channel'] for s in report['samples']})
    report['nonzero_input_seen']=sorted({s['channel'] for s in report['samples'] if max(map(abs,s['raw']))>.01})
    report['live_profiles_match_saved']=bool(report['samples']) and all(s['config_matches'] for s in report['samples'])
    report['observed_output_matches_settings']=bool(report['samples']) and all(s['output_matches'] for s in report['samples'])
    report['clamp_replay_verified']=False
    if items.get('gamepad_clamps_valid') and items.get('replay_clamps'):
        saved=list(struct.unpack('>4i',base64.b64decode(items['gamepad_clamps_v1'])))
        report['clamp_replay_verified']=(report['end']['replay_calls']>report['start']['replay_calls'] and report['end']['actual_clamps']==saved)
    if previous:
        keys=[f'profile_{i}_{j}' for i in range(5) for j in range(3)]+['gamepad_clamps_v1','gamepad_clamps_valid','replay_clamps']
        keys += [k for k in items if k.startswith('pro_native_v1_') or k in ('named_profiles_v1','named_profiles_v2')]
        report['saved_settings_unchanged']=all(items[k]==previous['config'][k] for k in keys)
        report['new_title_session']=previous['session']!=session
finally:
    sock.close()
    (a.output_dir/(a.phase+'.json')).write_text(json.dumps(report,indent=2)+'\n')
summary={k:v for k,v in report.items() if k not in ('samples','config')}
summary['samples']=len(report['samples'])
print(json.dumps(summary,indent=2))
