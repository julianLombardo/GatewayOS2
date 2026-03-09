"""
Gateway OS2 Mail Relay - Simple Protocol
Accepts a single message block from QEMU, sends via Gmail.
Protocol: OS sends "FROM\nTO\nSUBJECT\nUSER\nPASS\nBODY" then relay responds "OK" or "ERR:reason"
"""

import smtplib
import socket
import threading
import sys

LISTEN_PORT = 2525
GMAIL_SMTP = "smtp.gmail.com"
GMAIL_PORT = 465

def handle_client(conn, addr):
    sys.stdout.write(f"[RELAY] Connection from {addr}\n")
    sys.stdout.flush()

    try:
        # Read all data until connection closes or we get double newline
        data = b""
        conn.settimeout(30)
        while True:
            try:
                chunk = conn.recv(4096)
                if not chunk:
                    break
                data += chunk
                # Look for end marker
                if b"\nEND\n" in data or b"\r\nEND\r\n" in data:
                    break
            except socket.timeout:
                break

        text = data.decode("utf-8", errors="replace").replace("\r\n", "\n").strip()
        sys.stdout.write(f"[RELAY] Got: {repr(text[:200])}\n")
        sys.stdout.flush()

        # Parse: FROM\nTO\nSUBJECT\nUSER\nPASS\nBODY_LINES...\nEND
        lines = text.split("\n")
        if len(lines) < 6:
            conn.sendall(b"ERR:Not enough fields\n")
            conn.close()
            return

        mail_from = lines[0]
        mail_to = lines[1]
        subject = lines[2]
        gmail_user = lines[3]
        gmail_pass = lines[4]
        # Everything after line 5 until "END" is the body
        body_lines = []
        for i in range(5, len(lines)):
            if lines[i] == "END":
                break
            body_lines.append(lines[i])
        body_text = "\n".join(body_lines)

        sys.stdout.write(f"[RELAY] From: {mail_from}\n")
        sys.stdout.write(f"[RELAY] To: {mail_to}\n")
        sys.stdout.write(f"[RELAY] Subject: {subject}\n")
        sys.stdout.write(f"[RELAY] User: {gmail_user}\n")
        sys.stdout.write(f"[RELAY] Body: {body_text[:100]}\n")
        sys.stdout.flush()

        # Build email
        email_msg = f"From: {mail_from}\r\nTo: {mail_to}\r\nSubject: {subject}\r\nContent-Type: text/plain; charset=UTF-8\r\n\r\n{body_text}\r\n"

        # Send via Gmail
        sys.stdout.write(f"[RELAY] Connecting to Gmail...\n")
        sys.stdout.flush()
        with smtplib.SMTP_SSL(GMAIL_SMTP, GMAIL_PORT, timeout=30) as smtp:
            smtp.login(gmail_user, gmail_pass)
            smtp.sendmail(mail_from, [mail_to], email_msg)

        sys.stdout.write(f"[RELAY] Email sent successfully!\n")
        sys.stdout.flush()
        conn.sendall(b"OK\n")

    except smtplib.SMTPAuthenticationError as e:
        sys.stdout.write(f"[RELAY] Auth failed: {e}\n")
        sys.stdout.flush()
        conn.sendall(b"ERR:Gmail auth failed\n")
    except Exception as e:
        sys.stdout.write(f"[RELAY] Error: {e}\n")
        sys.stdout.flush()
        conn.sendall(f"ERR:{e}\n".encode())
    finally:
        conn.close()
        sys.stdout.write(f"[RELAY] Done\n")
        sys.stdout.flush()

def main():
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("0.0.0.0", LISTEN_PORT))
    server.listen(5)
    sys.stdout.write(f"[RELAY] Listening on port {LISTEN_PORT}\n")
    sys.stdout.flush()

    try:
        while True:
            conn, addr = server.accept()
            t = threading.Thread(target=handle_client, args=(conn, addr))
            t.daemon = True
            t.start()
    except KeyboardInterrupt:
        server.close()

if __name__ == "__main__":
    main()
