import socket

SERVER_IP = "10.33.182.242"
PORT = 5000

client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

try:
    client.connect((SERVER_IP, PORT))
    print("Connected to Oracle Node.")

    message = "Hello from Virtual Node A!"
    client.sendall(message.encode())
    print("Sent:", message)

    reply = client.recv(1024).decode()
    print("Reply:", reply)

finally:
    client.close()
