# Asyncio Chat Server
This project is an asyncio-based chat server that handles multiple client connections, broadcasts messages, and supports private messaging. The server dynamically assigns ports and manages client connections efficiently.

# Features (in progress)
Asynchronous Handling: Utilizes Python's asyncio for non-blocking I/O operations.

Dynamic Port Assignment: Automatically assigns an available port for the server.

Client Management: Maintains a dictionary of connected clients with their addresses, usernames, and writer objects.

Broadcast Messaging: Sends messages to all connected clients.

Private Messaging: Supports whisper messages to specific clients.

Graceful Shutdown: Handles termination signals to ensure a clean shutdown.

# Code Overview
## Main Components
Client Handling:

handle_client(reader, writer): Manages client connections, reads messages, and handles commands like /quit and /w.

Broadcasting:

broadcast(message): Sends messages to all connected clients and handles errors gracefully.

Private Messaging:

whisper(sender, recipient, message): Sends private messages to specific clients.

Server Initialization:

main(): Starts the server, assigns a dynamic port, and runs the event loop.

Both sides have a 100 size buffer
# To Do
Create a new dictionary file for various commands and communication types