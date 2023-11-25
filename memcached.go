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
	}
}

func handleConnection(conn net.Conn) {
	buf := make([]byte, 1024)

	for {
		n, err := conn.Read(buf)
		if err != nil {
			fmt.Println("Error reading:", err.Error())
			break
		}
		fmt.Println("[LOG] Message received")
		response := "Message received: " + string(buf[0:n])
		conn.Write([]byte(response))
	}

	conn.Close()
}
