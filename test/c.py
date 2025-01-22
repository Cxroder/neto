import asyncio

'''async def tcp_echo_client():
    reader, writer = await asyncio.open_connection('127.0.0.1', 8888)

    message = "Hello, World!"
    print(f"Sending: {message}")
    writer.write(message.encode())
    await writer.drain()

    data = await reader.read(100)
    print(f"Received: {data.decode()}")

    writer.close()
    await writer.wait_closed()

asyncio.run(tcp_echo_client())'''

def read_info():
    with open('port.txt', 'r') as f:
        raws = int(f.read().strip())
        host, port = raws.split(':')
        print(f"Will connect to {host}:{port}")
        return (host, port) # Both strings

async def listen_for_messages(reader):
    while True:
        data = await reader.read(100)
        if not data:
            continue
        print(data.decode())

async def send_messages(writer):
    while True:
        message = input("> ")
        writer.write(message.encode())
        await writer.drain()
        if message == "/quit":
            break

async def main():
    addr = read_info()
    reader, writer = await asyncio.open_connection(addr[0], int(addr[1]))
    listen_task = asyncio.create_task(listen_for_messages(reader))
    await send_messages(writer)
    writer.close()
    await writer.wait_closed()
    await listen_task

asyncio.run(main())