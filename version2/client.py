import select
import socket
import struct
import sys
import time

ON_PORT = 5000
MSG_CONNECT = 1
MSG_LINKSTATE = 2
TUPLE_SIZE = 9
LSP_INTERVAL = 10

def recv_all(sock, n):
    data = b""
    while len(data) < n:
        chunk = sock.recv(n - len(data))
        if not chunk:
            return None
        data += chunk
    return data


def parse_link_state(body):
    tuples = []
    for off in range(0, len(body), TUPLE_SIZE):
        letter = chr(body[off])
        ip = socket.inet_ntoa(body[off + 1:off + 5])
        port, cost = struct.unpack("!HH", body[off + 5:off + 9])
        tuples.append((letter, ip, port, cost))
    return tuples

def build_lsp(origin, seq, neighbours):
    pkt = struct.pack("!cIB", origin.encode(), seq, len(neighbours))
    for (letter, ip, port, cost) in neighbours:
        pkt += struct.pack("!cH", letter.encode(), cost)
    return pkt


def parse_lsp(data):
    if len(data) < 6:
        return None
    origin, seq, count = struct.unpack("!cIB", data[:6])
    if len(data) != 6 + 3 * count:
        return None
    links = []
    for k in range(count):
        nid, cost = struct.unpack("!cH", data[6 + 3 * k:9 + 3 * k])
        links.append((nid.decode(), cost))
    return origin.decode(), seq, links


def fmt_links(origin, links):
    return origin + ": " + ",".join(f"{n}={c}" for n, c in links)


if len(sys.argv) != 4:
    print(f"Usage: {sys.argv[0]} <ON_IP> <my_IP> <my_UDP_port>")
    sys.exit(1)

on_ip = sys.argv[1]
my_ip = sys.argv[2]
my_port = int(sys.argv[3])

udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
try:
    udp.bind((my_ip, my_port))
except OSError as e:
    print(f"Cannot bind UDP {my_ip}:{my_port}: {e}")
    sys.exit(1)
print(f"UDP socket bound to {my_ip}:{my_port}")

tcp = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
try:
    tcp.connect((on_ip, ON_PORT))
except OSError as e:
    print(f"Cannot connect to ON {on_ip}:{ON_PORT}: {e}")
    sys.exit(1)
print("Connected to Oracle Node.")

body = socket.inet_aton(my_ip) + struct.pack("!H", my_port)
tcp.sendall(struct.pack("!BH", MSG_CONNECT, len(body)) + body)
print(f"Sent CONNECT ({my_ip}, {my_port})")

my_id = None
neighbours = []
seq = 0
lsdb = {}
next_send = None


def send_lsp():
    global seq
    seq += 1
    pkt = build_lsp(my_id, seq, neighbours)
    lsdb[my_id] = (seq, [(t[0], t[3]) for t in neighbours])
    for (letter, ip, port, cost) in neighbours:
        try:
            udp.sendto(pkt, (ip, port))
        except OSError as e:
            print(f"Could not send LSP to {letter} ({ip}:{port}): {e}")
    print(f"Sent LSP seq={seq} to {','.join(t[0] for t in neighbours)}")


def handle_link_state():
    global my_id, neighbours, next_send
    hdr = recv_all(tcp, 3)
    if hdr is None:
        return False
    msg_type, length = struct.unpack("!BH", hdr)
    body = recv_all(tcp, length) if length > 0 else b""
    if body is None:
        return False

    if msg_type != MSG_LINKSTATE:
        print(f"Ignoring unknown message type {msg_type}")
        return True

    tuples = parse_link_state(body)
    me = [t for t in tuples if t[3] == 0]
    if len(me) != 1:
        print("Malformed LINK-STATE: expected exactly one zero-cost tuple")
        return True

    my_id = me[0][0]
    neighbours = [t for t in tuples if t[3] != 0]
    parts = [f"{my_id}=0"] + [f"{t[0]}={t[3]}" for t in neighbours]
    print("LINK-STATE:", ",".join(parts))

    send_lsp()
    next_send = time.monotonic() + LSP_INTERVAL
    return True


def handle_lsp():
    data, addr = udp.recvfrom(1024)
    lsp = parse_lsp(data)
    if lsp is None:
        print(f"Malformed LSP from {addr}, ignored")
        return
    origin, rseq, links = lsp
    if origin == my_id:
        return
    if origin not in lsdb or rseq > lsdb[origin][0]:
        lsdb[origin] = (rseq, links)
        print(f"Received LSP seq={rseq}  {fmt_links(origin, links)}")


try:
    while True:
        if next_send is None:
            timeout = 1.0
        else:
            timeout = max(0.0, next_send - time.monotonic())

        readable, _, _ = select.select([tcp, udp], [], [], timeout)

        if tcp in readable:
            if not handle_link_state():
                print("Oracle Node closed the connection.")
                break
        if udp in readable:
            handle_lsp()

        if next_send is not None and time.monotonic() >= next_send:
            send_lsp()
            next_send = time.monotonic() + LSP_INTERVAL

except KeyboardInterrupt:
    print("\nExiting.")
finally:
    tcp.close()
    udp.close()