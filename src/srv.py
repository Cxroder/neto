import socket
import threading
import select

HOST = '127.0.0.1'
PORT = 8085

hostname = socket.gethostname()
ip_address = socket.gethostbyname(hostname)
print(f"Server IP address: {ip_address}")

# Server setup
server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
try:
    server.bind((HOST, PORT))
except:
    server.bind((HOST, PORT+1))
server.listen(5)
clients = {}

def handle_client(c_socket, c_name):
    print(c_name + " joined")
    while True:
        try:
            message = c_socket.recv(1024)
            print(c_name + " messaged")
            if not message:
                break

            c_socket.send(b'ACK') # send acknowledgment back

            if message.decode('utf-8').startswith("/w"): # Check if whisper
                _, r_name, w_message = message.split(' ', 2)
                if r_name in clients:
                    whisper(c_socket, clients[r_name], w_message)
                else:
                    c_socket.send(f"Error: {r_name} not found\n".encode('utf-8'))
            else:
                broadcast(f"{c_name}: {message}", c_socket)

        except:
            del clients[c_name]
            c_socket.close()
            print(c_name + " left")
            broadcast(f"{c_name} has left the chat", None)
            break

def broadcast(message, s_socket):
    for client in clients.values():
        if client != s_socket:
            client.send(message.encode('utf-8'))

def whisper(s_socket, r_socket, message):
    s_socket.send(f"Whispering to {r_socket.getpeername()}\n".encode('utf-8'))
    r_socket.send(f"Whisper from {s_socket.getpeername()}: {message}\n".encode('utf-8'))

while True:
    c_socket, addr = server.accept()
    c_name = c_socket.recv(1024).decode('utf-8')
    clients[c_name] = c_socket
    broadcast(f"{c_name} has joined the chat", None)
    threading.Thread(target=handle_client, args=(c_socket, c_name)).start()
