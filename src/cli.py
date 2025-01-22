import socket
import threading
import select

HOST = '127.0.0.1'
PORT = 8085

# Client setup
client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
client.connect((HOST, PORT))
c_name = input("Enter name: ")
client.send(c_name.encode('utf-8'))

def receive_messages():
    while True:
        try:
            with c_lock:
                message = client.recv(1024)
                c_lock.notify_all()
                if not message:
                    break
                print(message.decode('utf-8'))
        except:
            client.close()
            print("connection closed")
            break


def send_messages():
    while True:
        message = input()
        with c_lock:
            c_lock.wait()
            client.send(message.encode('utf-8'))
            # Wait for acknowledgment
            ack_received = False
            for _ in range(3): # Retry up to 3 times
                ready = select.select([client], [], [], 1)
                if ready[0]:
                    with lock:
                        ack = client.recv(1024)
                        if ack == b'ACK':
                            ack_received = True
                            break
                        else:
                            print(ack.decode('utf-8'))
                else:
                    print("Timeout: No acknowledgment received, retrying...")
                    client.send(message.encode('utf-8'))
        if not ack_received:
            print("Acknowledgment not received after 3 retries, exiting...")
            client.close()
            break

lock = threading.Lock()
c_lock = threading.Condition(lock)
threading.Thread(target=receive_messages).start()
threading.Thread(target=send_messages).start()
