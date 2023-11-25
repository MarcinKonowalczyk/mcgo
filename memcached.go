package main

import (
	"flag"
	"fmt"
	"net"
	"os"
	"strconv"
)

const PORT = 11211

// store all connections in a map
var connections map[string]net.Conn

var verbose *bool

func main() {
	// Parse command line arguments
	verbose = flag.Bool("v", false, "Verbose mode")

	flag.Parse()

	log("Starting memcached server...")

	// initialize map
	connections = make(map[string]net.Conn)

	port_listen(PORT)
}

func log(a ...any) {
	if *verbose {
		b := make([]any, len(a)+1)
		b[0] = "[LOG]"
		copy(b[1:], a)
		fmt.Println(b...)
	}
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

		connection_id := conn.RemoteAddr().String()
		log("Connection from ", connection_id)
		connections[connection_id] = conn

		go handleConnection(conn)
	}
}

func handleConnection(conn net.Conn) {
	connection_id := conn.RemoteAddr().String()

	// close connection when this function ends
	defer log("Connection from", connection_id, "closed")
	defer delete(connections, connection_id)
	defer conn.Close()

	log("Connection from", connection_id, "opened")

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
				log("Client closed connection")
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

			log("Message received:", string(message))
		} else {
			// Message is not terminated by \r\n
			if byte_m1 == 6 || byte_m1 == 4 {
				// Ctrl-C or Ctrl-D
				log("Client closed connection")
				break
			} else {
				// Some other message??
				log("Raw bytes received:", message)
			}
		}
		conn.Write([]byte("OK\n"))
	}
}
