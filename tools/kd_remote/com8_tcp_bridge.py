"""COM8 (KD serial) <-> TCP bridge. Opens the COM port only while a client is connected.

Listens on 127.0.0.1 only; reach it from another PC with an SSH reverse tunnel (see README.md).
Usage: python com8_tcp_bridge.py [--port COM8] [--baud 115200] [--listen 5170]
"""
import argparse
import socket
import sys
import threading
import time

import serial


def log(msg):
    print(time.strftime('%H:%M:%S'), msg, flush=True)


def serve(port, baud, listen):
    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(('127.0.0.1', listen))
    srv.listen(1)
    log(f'listening 127.0.0.1:{listen} -> {port} @ {baud}')
    while True:
        conn, addr = srv.accept()
        conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        log(f'client {addr}; opening {port}')
        try:
            ser = serial.Serial(port, baud, timeout=0.02, write_timeout=2, rtscts=False, dsrdtr=False)
            ser.dtr = False
            ser.rts = False
        except Exception as e:  # port busy / missing: do not touch anything else
            log(f'cannot open {port}: {e}')
            conn.close()
            continue
        stop = threading.Event()
        counts = {'rx': 0, 'tx': 0}

        def serial_to_tcp():
            try:
                while not stop.is_set():
                    data = ser.read(ser.in_waiting or 1)
                    if data:
                        conn.sendall(data)
                        counts['rx'] += len(data)
            except Exception as e:
                log(f'serial->tcp ended: {e}')
            stop.set()

        def tcp_to_serial():
            try:
                while not stop.is_set():
                    data = conn.recv(4096)
                    if not data:
                        break
                    ser.write(data)
                    counts['tx'] += len(data)
            except Exception as e:
                log(f'tcp->serial ended: {e}')
            stop.set()

        threads = [threading.Thread(target=serial_to_tcp, daemon=True),
                   threading.Thread(target=tcp_to_serial, daemon=True)]
        for t in threads:
            t.start()
        stop.wait()
        try:
            conn.close()
        except Exception:
            pass
        for t in threads:
            t.join(timeout=2)
        ser.close()
        log(f'closed {port}; bytes target->host={counts["rx"]} host->target={counts["tx"]}')


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', default='COM8')
    ap.add_argument('--baud', type=int, default=115200)
    ap.add_argument('--listen', type=int, default=5170)
    a = ap.parse_args()
    try:
        serve(a.port, a.baud, a.listen)
    except KeyboardInterrupt:
        sys.exit(0)
