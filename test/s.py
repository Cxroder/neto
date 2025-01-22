import asyncio
import signal

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

async def handle_client(reader, writer):
    addr = writer.get_extra_info('peername')
    print(f"Connected by {addr}")

    while True:
        data = await reader.read(100)  # Read up to 100 bytes
        if data == "/quit":
            print(f"Connection closed by {addr}")
            break
        print(f"Received: {data.decode()} from {addr}")
        writer.write(data)  # Echo back the same data
        await writer.drain()  # Ensure the data is sent

    writer.close()
    await writer.wait_closed()

async def main():
    server = await asyncio.start_server(handle_client, '127.0.0.1', 0)

    addr = server.sockets[0].getsockname()
    print(f"Serving on {addr}")

    # Write server info to a file
    with open('server_info.txt', 'w') as f:
        f.write(f"{addr[0]}:{addr[1]}")

    # Get the current event loop
    loop = asyncio.get_running_loop()

    # Register signal handlers for graceful shutdown
    for sig in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(sig, lambda s=sig: asyncio.create_task(shutdown(s, loop, server)))

    async with server:
        await server.serve_forever()

asyncio.run(main())
