import socket
import ssl
import subprocess
import sys
import threading

probe, certificate, key = sys.argv[1:]
for secure in (False, True):
    listener = socket.socket()
    listener.bind(('127.0.0.1', 0))
    listener.listen(1)
    listener.settimeout(8)
    port = listener.getsockname()[1]
    errors = []

    def serve():
        try:
            stream, _ = listener.accept()
            stream.settimeout(8)
            if secure:
                context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
                context.load_cert_chain(certificate, key)
                stream = context.wrap_socket(stream, server_side=True)
            with stream:
                incoming = b''
                while b'USER tester 0 * :tester\r\n' not in incoming:
                    chunk = stream.recv(4096)
                    assert chunk, 'client disconnected during registration'
                    incoming += chunk
                    assert len(incoming) < 16384
                assert incoming.startswith(b'PASS secret\r\nCAP LS 302\r\nNICK tester\r\n')
                stream.sendall(b':local 001 tester :welcome\r\nPING :transport-probe\r\n')
                incoming = b''
                while b'PONG :transport-probe\r\n' not in incoming:
                    chunk = stream.recv(4096)
                    assert chunk, 'client disconnected before PONG'
                    incoming += chunk
                    assert len(incoming) < 16384
                stream.sendall(b':local NOTICE tester :probe-complete\r\n')
        except BaseException as error:
            errors.append(error)
        finally:
            listener.close()

    thread = threading.Thread(target=serve, daemon=True)
    thread.start()
    url = ('tls' if secure else 'tcp') + f'://127.0.0.1:{port}'
    result = subprocess.run([probe, url], timeout=12)
    thread.join(timeout=9)
    assert not thread.is_alive(), 'loopback IRC fixture did not finish'
    assert not errors, errors
    assert result.returncode == 0, result.returncode
