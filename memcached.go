package main

import (
	"fmt"
	"net"
	"os"
	"strconv"
)

const PORT = 11211

func main() {
	port_listen(PORT)
}

func port_listen(port int) {
	ln, err := net.Listen("tcp", ":"+strconv.Itoa(port))
	if err != nil {
		fmt.Println("Error listening:", err.Error())
		os.Exit(1)
	}
	defer ln.Close()

	for {
		conn, err := ln.Accept()
		if err != nil {
			fmt.Println("Error accepting:", err.Error())
			os.Exit(1)
		}
		go handleConnection(conn)
		fmt.Println("[LOG] Connection established")
	}
}

func handleConnection(conn net.Conn) {
	// TODO: handle messages longer than 1024 bytes
	buf := make([]byte, 1024)

	for {
		n, err := conn.Read(buf)
		if err != nil {
			fmt.Println("Error reading:", err.Error())
			break
		}
		message := buf[0:n]
		if len(message) < 2 {
			// Still have to check for Ctrl-C or Ctrl-D
			if len(message) == 1 && (message[0] == 6 || message[0] == 4) {
				// Ctrl-C or Ctrl-D
				fmt.Println("[LOG] Client closed connection")
				break
			}
			continue
		}
		byte_m1 := message[len(message)-1]
		byte_m2 := message[len(message)-2]
		if byte_m1 == 10 && byte_m2 == 13 {
			message = message[0 : len(message)-2]
			if len(message) == 0 {
				continue
			}

			any_non_printable := false
			for _, b := range message {
				if b < 32 || b > 126 {
					any_non_printable = true
					break
				}
			}
			if any_non_printable {
				// fmt.Println("[LOG] Ignoring message:", message)
				continue
			}

			fmt.Println("[LOG] Message received:", string(message))
		} else {
			// Message is not terminated by \r\n
			if byte_m1 == 6 || byte_m1 == 4 {
				// Ctrl-C or Ctrl-D
				fmt.Println("[LOG] Client closed connection")
				break
			} else {
				// Some other message??
				fmt.Println("[LOG] Raw bytes received:", message)
			}
		}
		conn.Write([]byte("OK\n"))
	}

	conn.Close()
}
