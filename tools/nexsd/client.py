import socket
import sys
import os

SOCKET_PATH = "/tmp/nexsd.sock"

def lint_file(file_path):
    if not os.path.exists(file_path):
        print(f"File not found: {file_path}")
        return

    with open(file_path, "r") as f:
        content = f.read()

    # Protocol: filename\0content
    payload = file_path.encode() + b"\0" + content.encode()

    try:
        client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        client.connect(SOCKET_PATH)
        client.sendall(payload)
        
        response = b""
        while True:
            chunk = client.recv(4096)
            if not chunk:
                break
            response += chunk
        
        print(response.decode())
        client.close()
    except Exception as e:
        print(f"Error connecting to nexsd: {e}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python client.py <file.nx>")
    else:
        lint_file(sys.argv[1])
