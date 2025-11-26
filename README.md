import socket

def send_udp(message, ip, port):
    for i in range(5000):
        print("Sending iteration 1: ")
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.sendto(message.encode(), (ip, port))
        sock.close()

# Example usage:
send_udp("Hello via UDP!", "192.168.0.18", 1217)
