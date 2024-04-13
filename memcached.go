package main

import (
	"bytes"
	"flag"
	"fmt"
	"net"
	"os"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/galsondor/go-ascii"
)

const DEFAULT_PORT = 11211

// store all connections in a map

// verbose mode of the server (print log messages)
var verbose *bool

type Data struct {
	flags      int
	exptime    int
	length     int
	data       string
	generation int
}

// var data map[string]Data
var data sync.Map

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
	STATS   MessageType = "stats"
)

const MCGO_VERSION = "go0.2.0"

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
	prev_noreply        bool
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

func newConn(net_conn net.Conn, id string) Conn {
	return Conn{
		net_conn:            net_conn,
		id:                  id,
		expect_continuation: false,
		prev_message:        GET, // This is just a dummy value
		prev_key:            "",
	}
}

type Stats struct {
	// curr_items uint64 // calculated from data map
	total_items uint64 // not implemented
	// curr_bytes uint64 // calculated from data map
	// total_bytes uint64 // not implemented
	// curr_conns  uint      // done(?)
	total_conns uint      // done(?)
	get_cmds    uint      // done(?)
	set_cmds    uint      // done(?)
	get_hits    uint      // done(?)
	get_misses  uint      // done(?)
	started     time.Time // done(?)
	// bytes_read    uint64
	// bytes_written uint64
}

var stats Stats

func newStats(now time.Time) Stats {
	return Stats{
		started: now,
	}
}

func main() {
	// Parse command line arguments
	verbose = flag.Bool("v", false, "Verbose mode")
	port := flag.Int("p", DEFAULT_PORT, "Port to listen on")

	flag.Parse()

	log("Starting memcached server")

	connections = make(map[string]Conn)
	data = sync.Map{}
	stats = newStats(time.Now())

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
		conn := newConn(net_conn, connection_id)
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

	// Increment connection stats
	stats.total_conns++

	log("Connection from", conn.id, "opened")

	// TODO: handle messages longer than 1024 bytes
	buf := make([]byte, 1024)

	for {
		n, err := conn.Read(buf)
		if err != nil {
			// check if this is a read form a closed connection
			err_str := err.Error()
			if strings.Contains(err_str, "use of closed network connection") {
				break
			} else if strings.Contains(err_str, "connection reset by peer") {
				// client closed connection
				log("Client closed connection")
				break
			} else if strings.Contains(err_str, "EOF") {
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
	message_parts := strings.Split(message, " ")
	message_type := MessageType(message_parts[0])
	switch message_type {
	case GET:
		log("GET message")
		stats.get_cmds++
		if len(message_parts) < 2 {
			// TODO: return error to client
			conn.Write("CLIENT_ERROR wrong number of arguments for 'get' command")
			return
		}
		var key string = message_parts[1]
		if len(key) == 0 {
			panic("Key cannot be empty. This should not happen.")
		}

		_datum, found := data[key]
		datum, ok := _datum.(Data)
		if !ok {
			panic("Data not of type Data")
		}

		if !found {
			stats.get_misses++
		} else {
			conn.Write("VALUE " + key + " " + strconv.Itoa(datum.flags) + " " + strconv.Itoa(datum.length))
			conn.Write(datum.data)
			stats.get_hits++
		}

		conn.Write("END")

	case SET:
		log("SET message")
		stats.set_cmds++
		if len(message_parts) < 5 {
			conn.Write("CLIENT_ERROR wrong number of arguments for 'set' command")
			return
		}
		var key string = message_parts[1]
		if len(key) == 0 {
			panic("Key cannot be empty. This should not happen.")
		}
		flags, err := strconv.Atoi(message_parts[2])
		if err != nil {
			conn.Write("CLIENT_ERROR invalid flags")
			return
		}

		exptime, err := strconv.Atoi(message_parts[3])
		if err != nil {
			conn.Write("CLIENT_ERROR invalid exptime")
		}

		// var _length string = message_parts[4]
		length, err := strconv.Atoi(message_parts[4])
		if err != nil {
			conn.Write("CLIENT_ERROR invalid length")
		}

		// check if we get noreply argument
		var noreply bool = false
		if len(message_parts) > 5 {
			if message_parts[5] == "noreply" {
				noreply = true
			}
		}

		new := Data{
			flags:   flags,
			exptime: exptime,
			length:  length,
			data:    "",
		}

		_, found := data.Swap(key, new)
		if found {
			// key already existed and got swapped
		} else {
			// new key
			stats.total_items++
		}

		conn.prev_message = SET
		conn.prev_key = key
		conn.prev_noreply = noreply
		conn.expect_continuation = true

	case DELETE:
		log("DELETE message")
		if len(message_parts) < 2 {
			conn.Write("CLIENT_ERROR wrong number of arguments for 'delete' command")
			return
		}
		var key string = message_parts[1]
		if len(key) == 0 {
			panic("Key cannot be empty. This should not happen.")
		}

		var noreply bool = false
		if len(message_parts) > 2 {
			if message_parts[2] == "noreply" {
				noreply = true
			}
		}

		_, found := data.LoadAndDelete(key)
		if !found {
			log("Key not found:", key)
			if !noreply {
				conn.Write("NOT_FOUND")
			}
		} else {
			log("Deleted key:", key)
			if !noreply {
				conn.Write("DELETED")
			}
		}

	case INCR, DECR:
		if len(message_parts) < 3 {
			panic("Wrong number of arguments for INCR/DECR")
		}
		is_incr := message_type == INCR
		if is_incr {
			log("INCR message")
		} else {
			log("DECR message")
		}

		key := message_parts[1]
		if len(key) == 0 {
			panic("Key cannot be empty. This should not happen.")
		}
		amount, err := strconv.Atoi(message_parts[2])
		if err != nil {
			panic(err)
		}

		// check if we get noreply argument
		var noreply bool = false
		if len(message_parts) > 3 {
			if message_parts[3] == "noreply" {
				noreply = true
			}
		}

		// We have to manually lock the data map because we are reading and writing in two steps
		data.Mutex.Lock()
		defer data.Mutex.Unlock()

		// check if key exists
		element, ok := data[key]
		if !ok {
			conn.Write("NOT_FOUND")
			return
		}

		// Pluck a number out of the data
		numeric, err := strconv.Atoi(element.data)
		if err != nil {
			conn.Write("CLIENT_ERROR cannot increment or decrement non-numeric value")
			return
		}

		if is_incr {
			numeric += amount
		} else {
			numeric -= amount
		}

		new_data := strconv.Itoa(int(numeric))
		element.data = new_data
		data.Store(key, element)

		if !noreply {
			// Reply with the new value
			conn.Write(strconv.Itoa(int(numeric)))
		}

	case QUIT:
		log("QUIT message")
		// NOTE: This will also stop the connection handler goroutine
		conn.Close()

	case VERSION:
		log("VERSION message")
		conn.Write("VERSION " + MCGO_VERSION)

	case STATS:
		log("STATS message")

		curr_bytes := 0
		for _, d := range data {
			curr_bytes += d.length
		}

		conn.Write("STAT pid ", os.Getpid())
		conn.Write("STAT uptime ", time.Since(stats.started).Seconds())
		conn.Write("STAT curr_items ", len(data))
		conn.Write("STAT total_items ", stats.total_items)
		conn.Write("STAT bytes ", curr_bytes)
		conn.Write("STAT curr_connections ", len(connections)-1) // ignore listening conn
		conn.Write("STAT total_connections ", stats.total_conns)
		conn.Write("STAT cmd_get ", stats.get_cmds)
		conn.Write("STAT cmd_set ", stats.set_cmds)
		conn.Write("STAT get_hits ", stats.get_hits)
		conn.Write("STAT get_misses ", stats.get_misses)
		conn.Write("END")

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

		if !conn.prev_noreply {
			conn.Write("STORED")
		}
	case DELETE, QUIT, VERSION:
		panic(fmt.Sprintf("Unexpected continuation for message type %s", conn.prev_message))
	default:
		log("Message continuation for unknown message type:", conn.prev_message)
	}
}
