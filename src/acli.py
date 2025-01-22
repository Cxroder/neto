#LEARN ABOUT ASYNCIO AND COROUTINES
import asyncio

def read_info():
    with open('server_info.txt', 'r') as f:
        raws = f.read().strip()
        host, port = raws.split(':')
        print(f"Will connect to {host}:{port}")
        return (host, port) # Both strings

async def async_input(prompt):
    return await asyncio.to_thread(input, prompt)

async def listen_for_messages(reader):
    while True:
        #print("[listen start]")
        data = await reader.read(100)
        #print("[read]")
        if not data:  # Connection closed by the server
            print("Server closed the connection.")
            break
        print(f"Received: {data.decode()}> ", end='')

async def send_messages(writer):
    while True:
        message = await async_input("")
        writer.write(message.encode())
        await writer.drain()
        if message == "/quit":
            break

async def main():
    addr = read_info()
    reader, writer = await asyncio.open_connection(addr[0], int(addr[1]))
    name = input("Enter username\n> ", end='')
    writer.write(name.encode())

    listen_task = asyncio.create_task(listen_for_messages(reader))
    await send_messages(writer)

    writer.close()
    await writer.wait_closed()
    listen_task.cancel()  # Cancel the listener task
    try:
        await listen_task
    except asyncio.CancelledError:
        pass  # Suppress the cancellation exception

asyncio.run(main())

