"""Exercise owner direct RTP and linked WebSocket fallback using disposable accounts."""
import os
import pathlib
import select
import struct
import subprocess
import time


def run(login, origin, owner, work, expected_port, home_port):
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
        fallback.until(lambda m: 'proxy TX subscription' in m.get('error', {}).get('msg', ''))
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
        # The connected server issues independent keys and frames its own stream IDs.
        direct.socket.close(); clients.remove(direct); time.sleep(.15)
        home = login('alpha'); clients.append(home); capabilities(home); join(home)
        home_channels = available(home)
        home_rx = subscribe(home, home_channels[0], 'rtp-home')
        home_tx = subscribe(home, home_channels[1], 'rtp-home')
        assert home_rx['direct'] == home_tx['direct'] == f'rtp://127.0.0.1:{home_port}'
        assert home_rx['owner-stream'] == home_rx['stream'] >= 128
        assert home_tx['owner-stream'] == home_tx['stream'] >= 128
        assert home_rx['direct-key'] != rx['direct-key']
        home_rx_peer, home_tx_peer = peer(home_rx), peer(home_tx)
        # Allow the client to authenticate its connected-server UDP path.
        time.sleep(.2)
        source.send_payload(frame(source_tx, payload))
        assert line(home_rx_peer, 'FRAME') == f'FRAME {28+len(payload)} {checksum(payload)}'
        home.send({'msg': {'type': 'cat'}, 'cat': {'cmd': 'ptt', 'room': room, 'vfo': 'A', 'ptt': True}})
        listener.until(lambda m: m.get('cat', {}).get('cmd') == 'ptt' and m['cat'].get('ptt') is True)
        tx_file.write_bytes(frame(home_tx, microphone))
        home_tx_peer.stdin.write('SEND ' + str(tx_file) + '\n'); home_tx_peer.stdin.flush()
        assert line(home_tx_peer, 'SENT') == 'SENT 1'
        _, echoed = listener.until_frame(lambda op, data: op == 2 and data[28:] == microphone)
        assert echoed[11] == listen_tx['stream']
        home_tx_peer.terminate(); home_tx_peer.wait(timeout=3)
        listener.until(lambda m: m.get('cat', {}).get('state', {}).get('ptt') is False)
        home.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'part', 'target': room}})
        home.until(lambda m: m.get('talk', {}).get('cmd') == 'part' and m['talk'].get('room') == room)
        time.sleep(.15); source.send_payload(frame(source_tx, b'home-revoked'))
        assert not select.select([home_rx_peer.stdout], [], [], .3)[0], 'PART left home RTP authorized'
        # Full proxy TX uses the same owner checks without an RTP grant.
        proxy_tx = subscribe(fallback, fallback_channels[1], 'ws')
        assert 'direct-key' not in proxy_tx
        fallback.send({'msg': {'type': 'cat'}, 'cat': {'cmd': 'ptt', 'room': room, 'vfo': 'A', 'ptt': True}})
        listener.until(lambda m: m.get('cat', {}).get('cmd') == 'ptt' and m['cat'].get('ptt') is True)
        fallback.send_payload(frame(proxy_tx, payload))
        _, echoed = listener.until_frame(lambda op, data: op == 2 and data[28:] == payload)
        assert echoed[11] == listen_tx['stream'], 'proxy TX used the home stream on the owner'
        listener.until(lambda m: m.get('cat', {}).get('state', {}).get('ptt') is False)
        print('PASS: full WebSocket proxy TX, fragmented microphone audio and audio-loss PTT release')
        print('PASS: connected-server RTP RX/TX, private keys, stream translation and downstream-loss PTT release')
        print('PASS: two-hop private RTP grants, direct RX/TX, fragmented WS fallback, owner PTT gate, media-loss release and PART revocation')
    finally:
        for p in peers:
            if p.poll() is None: p.terminate()
            p.wait(timeout=3)
        for c in clients: c.socket.close()


def run_proxy(login, work, home_port):
    """The owner's UDP listener is disabled: both paths must use its WS link."""
    room = '#gamma-rig0'
    clients, peers = [], []
    def connection(site, user='TEST'):
        c = login(site, user); clients.append(c)
        c.send({'msg': {'type': 'media'}, 'media': {'cmd': 'capab', 'codecs': 'pc16'}})
        c.until(lambda m: m.get('media', {}).get('cmd') == 'isupport')
        c.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'join', 'target': room}})
        c.until(lambda m: m.get('talk', {}).get('cmd') == 'join' and m['talk'].get('room') == room)
        channels = {}
        while len(channels) < 2:
            channel = c.until(lambda m: m.get('media', {}).get('cmd') == 'available' and
                m['media'].get('control-room') == room and m['media'].get('subsys') == 1)['media']
            channels[channel['dir']] = channel
        assert all('direct' not in ch for ch in channels.values()), 'disabled owner advertised RTP'
        return c, channels
    def subscribe(c, channel, transport):
        c.send({'msg': {'type': 'media'}, 'media': {'cmd': 'subscribe',
            'chan-uuid': channel['chan-uuid'], 'transport': transport}})
        return c.until(lambda m: m.get('media', {}).get('cmd') == 'subscribed' and
            m['media'].get('chan-uuid') == channel['chan-uuid'])['media']
    def ptt(c, state):
        c.send({'msg': {'type': 'cat'}, 'cat': {'cmd': 'ptt', 'room': room, 'vfo': 'A', 'ptt': state}})
    def frame(ch, payload, codec=b'pc16'):
        return struct.pack('!2sBB4sBBBBIIQ', b'RR', 1, 1, codec, 1, ch.get('vfo', 255),
            ch.get('rig', 0), ch['stream'], 1, len(payload), 1) + payload
    def expect_line(p, expected):
        assert select.select([p.stdout], [], [], 5)[0], 'RTP peer timeout: ' + expected
        result = p.stdout.readline().strip()
        assert result.startswith(expected), result
        return result
    def peer(grant):
        p = subprocess.Popen([os.environ['RR_RTP_TEST_PEER']], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, text=True, bufsize=1); peers.append(p)
        p.stdin.write(f"{grant['direct']} {grant['direct-key']} {grant['direct-ssrc']} 0\n"); p.stdin.flush()
        expect_line(p, 'READY')
        return p
    try:
        source, source_channels = connection('gamma', 'SOURCE')
        listener, listen_channels = connection('gamma')
        source_tx = subscribe(source, source_channels[1], 'ws')
        listen_tx = subscribe(listener, listen_channels[1], 'ws')
        proxy, proxy_channels = connection('alpha')
        # Missing owner grants, not only unreachable UDP, must allow proxy TX.
        rx = subscribe(proxy, proxy_channels[0], 'rtp'); assert 'direct-key' not in rx
        tx = subscribe(proxy, proxy_channels[1], 'ws'); assert 'direct-key' not in tx
        rx = subscribe(proxy, proxy_channels[0], 'ws')
        payload = bytes(range(256)) * 24
        microphone = b'\x01\x02' * 320
        source.send_payload(frame(source_tx, payload))
        _, echoed = proxy.until_frame(lambda op, data: op == 2 and data[28:] == payload)
        assert echoed[11] == rx['stream']
        ptt(proxy, True)
        listener.until(lambda m: m.get('cat', {}).get('cmd') == 'ptt' and m['cat'].get('ptt') is True)
        # Keep live audio flowing beyond the startup grace period.
        deadline = time.monotonic() + 3.4
        while time.monotonic() < deadline:
            proxy.send_payload(frame(tx, microphone))
            _, echoed = listener.until_frame(lambda op, data: op == 2 and data[28:] == microphone)
            assert echoed[11] == listen_tx['stream']
            time.sleep(.1)
        ptt(proxy, False)
        listener.until(lambda m: m.get('cat', {}).get('cmd') == 'ptt' and m['cat'].get('ptt') is False)
        # Invalid codecs and repeated key-down cannot extend a silent hold.
        ptt(proxy, True)
        listener.until(lambda m: m.get('cat', {}).get('cmd') == 'ptt' and m['cat'].get('ptt') is True)
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            proxy.send_payload(frame(tx, b'invalid-codec', b'mu08')); time.sleep(.1)
        ptt(proxy, True)
        listener.until(lambda m: m.get('cat', {}).get('state', {}).get('ptt') is False)
        # The browser's WS microphone path also uses these owner-side permissions.
        denied, denied_channels = connection('alpha', 'READER')
        subscribe(denied, denied_channels[1], 'ws')
        ptt(denied, True)
        denied.until(lambda m: m.get('msg', {}).get('type') == 'error')
        denied.socket.close(); clients.remove(denied)
        if home_port is not None:
            home, home_channels = connection('alpha')
            home_rx = subscribe(home, home_channels[0], 'rtp-home')
            home_tx = subscribe(home, home_channels[1], 'rtp-home')
            assert home_rx['direct'] == home_tx['direct'] == f'rtp://127.0.0.1:{home_port}'
            assert home_rx['owner-stream'] == home_rx['stream'] >= 128
            rx_peer, tx_peer = peer(home_rx), peer(home_tx)
            source.send_payload(frame(source_tx, payload))
            expect_line(rx_peer, 'FRAME')
            ptt(home, True)
            listener.until(lambda m: m.get('cat', {}).get('cmd') == 'ptt' and m['cat'].get('ptt') is True)
            tx_file = work / 'home-proxy.frame'; tx_file.write_bytes(frame(home_tx, microphone))
            tx_peer.stdin.write('SEND ' + str(tx_file) + '\n'); tx_peer.stdin.flush()
            assert expect_line(tx_peer, 'SENT') == 'SENT 1'
            _, echoed = listener.until_frame(lambda op, data: op == 2 and data[28:] == microphone)
            assert echoed[11] == listen_tx['stream']
            # Unsubscribe must release TX even while the UDP peer keeps probing.
            home.send({'msg': {'type': 'media'}, 'media': {'cmd': 'unsubscribe', 'chan-uuid': home_tx['chan-uuid']}})
            home.until(lambda m: m.get('media', {}).get('cmd') == 'unsubscribed')
            listener.until(lambda m: m.get('cat', {}).get('state', {}).get('ptt') is False)
        # Closing a proxied holder must revoke PTT without waiting for its media timer.
        ptt(proxy, True)
        listener.until(lambda m: m.get('cat', {}).get('cmd') == 'ptt' and m['cat'].get('ptt') is True)
        proxy.send_payload(frame(tx, microphone))
        listener.until_frame(lambda op, data: op == 2 and data[28:] == microphone)
        proxy.socket.close(); clients.remove(proxy)
        listener.until(lambda m: m.get('cat', {}).get('state', {}).get('ptt') is False)
        print('PASS: owner without UDP, full proxy RX/TX, sustained audio, bad-codec watchdog, permissions and disconnect')
        if home_port is not None:
            print('PASS: connected-server RTP RX/TX over WS links without owner UDP and unsubscribe revocation')
    finally:
        for p in peers:
            if p.poll() is None: p.terminate()
            p.wait(timeout=3)
        for c in clients: c.socket.close()
