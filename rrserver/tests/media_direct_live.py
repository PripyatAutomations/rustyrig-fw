"""Exercise owner direct RTP and linked WebSocket fallback using disposable accounts."""
import os
import pathlib
import select
import struct
import subprocess
import time


def run(login, origin, owner, work, expected_port):
    room = '#gamma-rig0'
    def join(c):
        c.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'join', 'target': room}})
        c.until(lambda m: m.get('talk', {}).get('cmd') == 'join' and m['talk'].get('room') == room)
    def capabilities(c):
        c.send({'msg': {'type': 'media'}, 'media': {'cmd': 'capab', 'codecs': 'pc16'}})
        c.until(lambda m: m.get('media', {}).get('cmd') == 'isupport')
    def available(c):
        # A JOIN produces owner-scoped discovery; channel direction identifies each pair.
        result = {}
        while len(result) < 2:
            m = c.until(lambda m: m.get('media', {}).get('cmd') == 'available' and
                m['media'].get('control-room') == room and m['media'].get('subsys') == 1)['media']
            result[m['dir']] = m
        return result
    def subscribe(c, channel, transport=None):
        m = {'cmd': 'subscribe', 'chan-uuid': channel['chan-uuid']}
        if transport: m['transport'] = transport
        c.send({'msg': {'type': 'media'}, 'media': m})
        return c.until(lambda m: m.get('media', {}).get('cmd') == 'subscribed' and
            m['media'].get('chan-uuid') == channel['chan-uuid'])['media']
    def frame(channel, payload):
        return struct.pack('!2sBB4sBBBBIIQ', b'RR', 1, 1, b'pc16', 1, channel.get('vfo', 255),
            channel.get('rig', 0), channel['stream'], 1, len(payload), 1) + payload
    def checksum(data):
        h = 2166136261
        for b in data: h = ((h ^ b) * 16777619) & 0xffffffff
        return h
    def line(p, expected):
        until = time.monotonic() + 5
        while time.monotonic() < until:
            if not select.select([p.stdout], [], [], max(.01, until-time.monotonic()))[0]: break
            result = p.stdout.readline().strip()
            if result.startswith(expected): return result
        raise AssertionError('RTP peer did not report ' + expected)
    def peer(grant):
        p = subprocess.Popen([os.environ['RR_RTP_TEST_PEER']], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, text=True, bufsize=1)
        p.stdin.write(f"{grant['direct']} {grant['direct-key']} {grant['direct-ssrc']} 0\n")
        p.stdin.flush(); line(p, 'READY')
        peers.append(p)
        return p
    peers = []
    clients = []
    try:
        # Use fresh sessions so reading earlier control replies cannot consume discovery.
        direct, fallback, source, listener = login('alpha'), login('alpha'), login('gamma', 'SOURCE'), login('gamma')
        clients += [direct, fallback, source, listener]
        for c in clients: capabilities(c)
        join(direct); remote = available(direct)
        join(fallback); fallback_channels = available(fallback)
        join(source); local = available(source)
        join(listener); listen_channels = available(listener)
        rx = subscribe(direct, remote[0], 'direct')
        tx = subscribe(direct, remote[1], 'direct')
        assert rx['direct'] == tx['direct'] == f'rtp://127.0.0.1:{expected_port}', 'wrong range, port collision, or denied pool selected'
        assert 'direct' not in local[0], 'non-admin source was offered an admin-only RTP pool'
        assert rx['stream'] >= 128 and rx['owner-stream'] < 128
        assert 'direct-key' not in remote[0], 'private grant appeared in discovery'
        assert rx['transport'] == tx['transport'] == 'direct'
        plain = subscribe(fallback, fallback_channels[0], 'ws')
        assert 'direct-key' not in plain
        source_tx = subscribe(source, local[1])
        listen_tx = subscribe(listener, listen_channels[1])
        rx_peer, tx_peer = peer(rx), peer(tx)
        payload = bytes(range(256)) * 24  # fragmentation on RTP and the link, independent of codec decode
        source.send_payload(frame(source_tx, payload))
        assert line(rx_peer, 'FRAME') == f'FRAME {28+len(payload)} {checksum(payload)}'
        _, echoed = fallback.until_frame(lambda op, data: op == 2 and data[28:] == payload)
        assert echoed[11] == plain['stream'] and echoed[8] == 0
        # Owner permissions still decide PTT; absence of a live grant fails closed.
        fallback.send({'msg': {'type': 'cat'}, 'cat': {'cmd': 'ptt', 'room': room, 'vfo': 'A', 'ptt': True}})
        fallback.until(lambda m: 'live direct TX' in m.get('error', {}).get('msg', ''))
        direct.send({'msg': {'type': 'cat'}, 'cat': {'cmd': 'ptt', 'room': room, 'vfo': 'A', 'ptt': True}})
        listener.until(lambda m: m.get('cat', {}).get('cmd') == 'ptt' and m['cat'].get('ptt') is True)
        microphone = b'\x01\x02' * 320
        tx_file = work / 'microphone.frame'; tx_file.write_bytes(frame(dict(tx, stream=tx['owner-stream']), microphone))
        tx_peer.stdin.write('SEND ' + str(tx_file) + '\n'); tx_peer.stdin.flush()
        assert line(tx_peer, 'SENT') == 'SENT 1'
        _, echoed = listener.until_frame(lambda op, data: op == 2 and data[28:] == microphone)
        assert echoed[11] == listen_tx['stream']
        # Stopping client-originated keepalives releases remote PTT within the watchdog bound.
        tx_peer.terminate(); tx_peer.wait(timeout=3)
        listener.until(lambda m: m.get('cat', {}).get('state', {}).get('ptt') is False)
        # Revoke a direct RX attachment by leaving its owner room.
        direct.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'part', 'target': room}})
        direct.until(lambda m: m.get('talk', {}).get('cmd') == 'part' and m['talk'].get('room') == room)
        time.sleep(.15)
        source.send_payload(frame(source_tx, b'revoked'))
        assert not select.select([rx_peer.stdout], [], [], .3)[0], 'PART left direct audio authorized'
        print('PASS: two-hop private RTP grants, direct RX/TX, fragmented WS fallback, owner PTT gate, media-loss release and PART revocation')
    finally:
        for p in peers:
            if p.poll() is None: p.terminate()
            p.wait(timeout=3)
        for c in clients: c.socket.close()
