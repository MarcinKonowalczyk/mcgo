package main

import (
	"bytes"
	"flag"
	"fmt"
	"net"
	"strconv"
	"strings"
	"time"

	"github.com/galsondor/go-ascii"
)

const DEFAULT_PORT = 11211

// store all connections in a map

// verbose mode of the server (print log messages)
var verbose *bool

type Data struct {
	flags   int
	exptime int
	length  int
	data    string
}

var data map[string]Data

// enum for message types
type MessageType string

const (
	GET     MessageType = "get"
	SET     MessageType = "set"
	DELETE  MessageType = "delete"
	QUIT    MessageType = "quit"
	VERSION MessageType = "version"
	INCR    MessageType = "incr"
	DECR    MessageType = "decr"
)

const MCGO_VERSION = "go0.1.0"

// A connection to a client. This struct is used to hold all the info needed
// to handle a connection. We have the net.Conn object, the connection id
// (the remote address), and some info about the previous message for handling
// message continuations.
type Conn struct {
	net_conn net.Conn
	id       string

	// info about previous message for handling message continuations
	expect_continuation bool
	prev_message        MessageType
	prev_key            string
}

func (conn Conn) Write(a ...any) {
	s := fmt.Sprint(a...) + "\r\n"
	conn.net_conn.Write([]byte(s))
}

func (conn Conn) Read(b []byte) (int, error) {
	return conn.net_conn.Read(b)
}

func (conn Conn) Close() {
	conn.net_conn.Close()
}

var connections map[string]Conn

func main() {
	// Parse command line arguments
	verbose = flag.Bool("v", false, "Verbose mode")
	port := flag.Int("p", DEFAULT_PORT, "Port to listen on")

	flag.Parse()

	log("Starting memcached server")

	// initialize map
	connections = make(map[string]Conn)
	data = make(map[string]Data)

	portListen(*port)
}

func log(a ...any) {
	if *verbose {
		now := time.Now()
		b := make([]any, len(a)+2)
		b[0] = "[LOG]"
		b[1] = now.Format("2006-01-02 15:04:05")
		copy(b[2:], a)
		fmt.Println(b...)
	}
}

// Listen on a port for incoming connections
func portListen(port int) {
	listener, err := net.Listen("tcp", ":"+strconv.Itoa(port))
	if err != nil {
		panic(err)
	}
	defer listener.Close()

	log("Listening on port", port)
	for {
		net_conn, err := listener.Accept()
		if err != nil {
			panic(err)
		}

		connection_id := net_conn.RemoteAddr().String()
		conn := Conn{
			net_conn:            net_conn,
			id:                  connection_id,
			expect_continuation: false,
			prev_message:        GET, // This is just a dummy value
			prev_key:            "",
		}

		connections[connection_id] = conn

		go handleConnection(conn)
	}
}

// Handle a connection. This runs in a separate goroutine. It disposes
// of the connection and removes it from the connections map when it
// is done.
func handleConnection(conn Conn) {
	// close connection when this function ends
	defer log("Connection from", conn.id, "closed")
	defer delete(connections, conn.id)
	defer conn.Close()

	log("Connection from", conn.id, "opened")

	// TODO: handle messages longer than 1024 bytes
	buf := make([]byte, 1024)

	for {
		n, err := conn.Read(buf)
		if err != nil {
			// check if this is a read form a closed connection
			if strings.Contains(err.Error(), "use of closed network connection") {
				break
			} else if strings.Contains(err.Error(), "EOF") {
				// client closed connection
				log("Client closed connection")
				break
			} else {
				// some other error
				panic(err)
			}
		}
		message := buf[0:n]
		if len(message) < 2 {
			// Still have to check for Ctrl-C or Ctrl-D
			if len(message) == 1 && (message[0] == ascii.ACK || message[0] == ascii.EOT) {
				// Ctrl-C or Ctrl-D
				log("Client closed connection")
				break
			}
			continue
		}
		byte_m1 := message[len(message)-1]
		byte_m2 := message[len(message)-2]
		if byte_m2 == ascii.CR && byte_m1 == ascii.LF {
			message = message[0 : len(message)-2]
			if len(message) == 0 {
				continue
			}

			any_non_printable := false
			for _, b := range message {
				if !(ascii.IsPrint(b) || b == ascii.CR || b == ascii.LF) {
					any_non_printable = true
					break
				}
			}

			if any_non_printable {
				fmt.Println("[LOG] Ignoring message:", message)
				continue
			}

			// split message on \r\n
			messages := bytes.Split(message, []byte{ascii.CR, ascii.LF})

			if len(messages) > 1 {
				log("Multiple messages received:")
				for _, m := range messages {
					log(" ", string(m))
				}
			} else {
				log("Message received:", string(messages[0]))
			}

			for _, message := range messages {
				handleMessage(string(message), &conn)
			}

		} else {
			// Message is not terminated by \r\n
			if byte_m1 == ascii.ACK || byte_m1 == ascii.EOT {
				// Ctrl-C or Ctrl-D
				log("Client closed connection")
				break
			} else {
				// Some other message??
				log("Message not terminated by \\r\\n:", message)
			}
		}
		// conn.Write("OK")
	}
}

func handleMessage(message string, conn *Conn) {
	if conn.expect_continuation {
		handleMessageWithContinuation(message, conn)
	} else {
		handleMessageWithoutContinuation(message, conn)
	}
}

// Handle a message from a connection.
func handleMessageWithoutContinuation(message string, conn *Conn) {
	if conn.expect_continuation {
		panic("This function should not be called when a continuation is expected")
	}
	if len(message) < 1 {
		panic("Empty message. Should not happen.")
	}
	message = strings.ToLower(message)
	message_parts := strings.Split(message, " ")
	message_type := MessageType(message_parts[0])
	switch message_type {
	case GET:
		log("GET message")
		if len(message_parts) < 2 {
			// TODO: return error to client
			panic("Wrong number of arguments for GET")
		}
		var key string = message_parts[1]
		if len(key) == 0 {
			panic("Key cannot be empty. This should not happen.")
		}

		data, ok := data[key]
		if !ok {
			// key not found
			// TODO: check what is the correct response here
			log("Key not found:", key)
		} else {
			conn.Write("VALUE " + key + " " + strconv.Itoa(data.flags) + " " + strconv.Itoa(data.length))
			conn.Write(data.data)
		}

		conn.Write("END")

	case SET:
		log("SET message")
		if len(message_parts) < 5 {
			// TODO: return error to client
			panic("Wrong number of arguments for SET")
		}
		var key string = message_parts[1]
		if len(key) == 0 {
			panic("Key cannot be empty. This should not happen.")
		}
		flags, err := strconv.Atoi(message_parts[2])
		if err != nil {
			// TODO: return error to client
			panic(err)
		}

		exptime, err := strconv.Atoi(message_parts[3])
		if err != nil {
			// TODO: return error to client
			panic(err)
		}

		// var _length string = message_parts[4]
		length, err := strconv.Atoi(message_parts[4])
		if err != nil {
			// TODO: return error to client
			panic(err)
		}

		// TODO: don't ignore extra
		var extra string = ""
		if len(message_parts) > 5 {
			// extra is optional
			extra = message_parts[5]
		}

		if len(extra) > 0 {
			log("Extra data in SET message:", extra)
		}

		data[key] = Data{
			flags:   flags,
			exptime: exptime,
			length:  length,
			data:    "",
		}

		conn.prev_message = SET
		conn.prev_key = key
		conn.expect_continuation = true

	case DELETE:
		log("DELETE message")
		if len(message_parts) < 2 {
			// TODO: return error to client
			panic("Wrong number of arguments for DELETE")
		}
		var key string = message_parts[1]
		if len(key) == 0 {
			panic("Key cannot be empty. This should not happen.")
		}

		_, ok := data[key]
		if !ok {
			// key not found
			log("Key not found:", key)
		} else {
			log("Deleting key:", key)
			delete(data, key)
		}

		conn.Write("DELETED")

	case INCR, DECR:
		if len(message_parts) < 3 {
			panic("Wrong number of arguments for INCR/DECR")
		}
		is_incr := message_type == INCR
		key := message_parts[1]
		if len(key) == 0 {
			panic("Key cannot be empty. This should not happen.")
		}
		amount, err := strconv.Atoi(message_parts[2])
		if err != nil {
			panic(err)
		}

		element, ok := data[key]
		if !ok {
			conn.Write("NOT_FOUND")
		} else {
			numeric, err := strconv.Atoi(element.data)
			if err != nil {
				log(fmt.Sprintf("Value for key '%s' is not numeric: %s", key, element.data))
				conn.Write("NOT_FOUND")
			} else {
				if is_incr {
					numeric += amount
				} else {
					numeric -= amount
				}
				element.data = strconv.Itoa(numeric)
				element.length = len(element.data) // update length
				data[key] = element
				conn.Write(strconv.Itoa(numeric))
			}
		}

	case QUIT:
		log("QUIT message")
		// NOTE: This will also stop the connection handler goroutine
		conn.Close()

	case VERSION:
		log("VERSION message")
		conn.Write("VERSION " + MCGO_VERSION)
	default:
		log("Unknown message type:", message_type)
	}
}

func handleMessageWithContinuation(message string, conn *Conn) {
	if !conn.expect_continuation {
		panic("This function should not be called when a continuation is not expected")
	}
	switch conn.prev_message {
	case GET:
		log("GET continuation")
	case SET:
		log("SET continuation")
		if conn.prev_key == "" {
			panic("Previous key is empty. This should not happen.")
		}
		// check prev key is in data map
		if _, ok := data[conn.prev_key]; !ok {
			log("Key not found:", conn.prev_key)
			panic("Key not found")
		}

		datum := data[conn.prev_key]
		if len(datum.data) < datum.length {
			datum.data += message
			data[conn.prev_key] = datum
		} else {
			// data too long
			log("Data too long for key:", conn.prev_key)
			panic("Data too long for key")
		}
		conn.expect_continuation = false

		conn.Write("STORED")
	case DELETE, QUIT, VERSION:
		panic(fmt.Sprintf("Unexpected continuation for message type %s", conn.prev_message))
	default:
		log("Message continuation for unknown message type:", conn.prev_message)
	}
}
