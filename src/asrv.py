import asyncio
import signal

# Replace magic numbers (100 buffer) with variables here?
#
clients = {}  # Dictionary to track client writers in format:
# addr: {
#         "username": username,
#         "writer": writer
#     }

clients_lock = asyncio.Lock()  # Async lock object

async def shutdown(signal, loop, server):
    print(f"Received exit signal {signal.name}...")
    tasks = [t for t in asyncio.all_tasks() if t is not asyncio.current_task()]
    [task.cancel() for task in tasks]

    print("Stopping server...")
    server.close()
    await server.wait_closed()  # Ensure the server socket is fully closed

    print("Cancelling tasks...")
    await asyncio.gather(*tasks, return_exceptions=True)
    loop.stop()
    print("Shutdown complete.")

def generate_unique_username(username, existing_usernames):
    original = username
    count = 1
    while username in existing_usernames:
        username = f"{original}-{count}"
        count += 1
    return username

async def initialize_client(reader, writer): # Handle username and any other setup tasks here
    try:
        # Get the client's username
        username = await reader.read(100)
        username = username.decode().strip()

        if not username: #!!! THIS should probably be handled by client
            print("Client did not provide a username. Closing connection.")
            return None
        #? Use writer and loop for proper username?

        # Ensure unique username
        username = generate_unique_username(username, [c['username'] for c in clients.values()])
        return username
    except Exception as e:
        print(f"Error during initialization: {e}")
        return None


async def handle_client(reader, writer):
    addr = writer.get_extra_info('peername')
    print(f"New connection from {addr}")

    name = await initialize_client(reader, writer)
    if name is None:
        writer.close()
        await writer.wait_closed()
        return

    clients[addr] = {"username": name, "writer": writer}
    print(f"Username '{name}' assigned to {addr}")
    await broadcast(f"{name} has joined the chat!")

    try:
        while True:
            data = await reader.read(100)

            if not data:  # Client disconnected
                print(f"Connection closed by {name}")
                break

            message = data.decode().strip()
            print(f"Received from {name}: {message}")

            if message.startswith("/w"):
                _, recipient, whisper_msg = message.split(' ', 2)
                await whisper(name, recipient, whisper_msg)
            elif message.startswith("/quit"):
                print(f"Connection closed by {name} (quit)")
                await broadcast(f"{name} has left the chat.")
                break
            else:
                print(f"broadcast \"{message}\" from {name}")
                await broadcast(f"{name}: {message}")

    except ConnectionResetError:
        print(f"Connection reset by {name}")
    finally: # Cleanup
        async with clients_lock:  # Safely remove the client without change during iteration
            if addr in clients:
                del clients[addr]
        writer.close()
        await writer.wait_closed()

async def broadcast(message):
    print(f"| Broadcasting: {message}")
    print(f"| Active clients: {list(clients.keys())}\n")

    async with clients_lock: # Lock client list during broadcast
        for addr, writer in clients.items():
            try:
                print(f"Sending to {addr}")
                writer.write(message.encode() + b'\n')
                await writer.drain()

            except Exception as e:
                print(f"Error sending to {addr}: {e}")
                writer.close() # Remove problematic writer
                await writer.wait_closed()
                del clients[addr]

async def whisper(sender, recipient, message):
    print(f"Whispering: {sender} to {recipient}: {message}")
    for addr, writer in clients.items():
        try:
            if recipient in str(addr):
                writer.write(f"[Whisper from {sender}]: {message}".encode() + b'\n')
                await writer.drain()
                break
            else:
                pass
        except Exception as e:
            pass
    else:
        clients[sender].write(f"Recipient {recipient} not found.".encode() + b'\n')
        await clients[sender].drain()

async def main():
    server = await asyncio.start_server(handle_client, '127.0.0.1', 0)

    addr = server.sockets[0].getsockname()
    print(f"Serving on {addr}")

    with open('server_info.txt', 'w') as f:
        f.write(f"{addr[0]}:{addr[1]}") # Write down info into file

    loop = asyncio.get_running_loop()

    for sig in (signal.SIGINT, signal.SIGTERM): # Add signal handling
        loop.add_signal_handler(sig, lambda s=sig: asyncio.create_task(shutdown(s, loop, server)))

    async with server:
        await server.serve_forever()

asyncio.run(main())
