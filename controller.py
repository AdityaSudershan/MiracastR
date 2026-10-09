#!/usr/bin/env python3

import socket

HOST = "127.0.0.1"
PORT = 9999


def send_command(command):
    try:
        with socket.socket(
            socket.AF_INET,
            socket.SOCK_STREAM
        ) as sock:

            sock.settimeout(5)

            sock.connect((HOST, PORT))

            sock.sendall(
                (command + "\n").encode()
            )

            response = sock.recv(1024)

            if response:
                print(
                    "[C++]",
                    response.decode(
                        errors="replace"
                    ).strip()
                )

    except ConnectionRefusedError:
        print(
            "ERROR: C++ sink is not listening on "
            f"{HOST}:{PORT}"
        )

    except Exception as e:
        print("ERROR:", e)


def main():

    print(
        "=========================================="
    )
    print(
        "       Miracast RTSP Controller"
    )
    print(
        "=========================================="
    )

    print()
    print(
        f"Connecting to C++ sink at "
        f"{HOST}:{PORT}"
    )

    while True:

        try:
            command = input(
                "\n[p]ause  [r]esume  [q]uit > "
            ).strip().lower()

        except KeyboardInterrupt:
            print()
            break

        if command == "p":

            send_command("PAUSE")

        elif command == "r":

            send_command("PLAY")

        elif command == "q":

            send_command("QUIT")
            break

        else:

            print(
                "Unknown command."
            )


if __name__ == "__main__":
    main()
