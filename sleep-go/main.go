package main

import (
	"fmt"
	"net"
	"time"
)

func main() {
	fmt.Println("server started")

	socket, err := net.Listen("tcp", "127.0.0.1:55555")
	if err != nil {
		panic(err)
	}

	for {
		conn, err := socket.Accept()
		if err != nil {
			panic(err)
		}
		go handleConnection(conn)
	}
}

func handleConnection(conn net.Conn) {
	defer conn.Close()

	buf := make([]byte, 1024)
	conn.Read(buf)

	time.Sleep(500 * time.Millisecond)

	conn.Write([]byte("HTTP/1.1 200 OK\r\n\r\nOK\r\n"))
}
