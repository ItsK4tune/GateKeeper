package driver

import (
	"bufio"
	"context"
	"fmt"
	"io"
	"net"
	"strconv"
	"time"
)

type RESPDriver struct {
	Version int // 2 or 3
}

func (d *RESPDriver) Name() string {
	return fmt.Sprintf("Redis RESP%d", d.Version)
}

func (d *RESPDriver) Connect(ctx context.Context, addr string) (Session, error) {
	dDialer := net.Dialer{Timeout: 3 * time.Second}
	conn, err := dDialer.DialContext(ctx, "tcp", addr)
	if err != nil {
		return nil, err
	}
	if tcpConn, ok := conn.(*net.TCPConn); ok {
		_ = tcpConn.SetNoDelay(true)
	}

	reader := bufio.NewReaderSize(conn, 32768)
	session := &RESPSession{
		conn:    conn,
		reader:  reader,
		version: d.Version,
	}

	if d.Version == 3 {
		// Handshake HELLO 3
		helloCmd := "*2\r\n$5\r\nHELLO\r\n$1\r\n3\r\n"
		if _, err := conn.Write([]byte(helloCmd)); err != nil {
			conn.Close()
			return nil, err
		}
		if err := session.readResponse(); err != nil {
			conn.Close()
			return nil, err
		}
	}

	return session, nil
}

type RESPSession struct {
	conn    net.Conn
	reader  *bufio.Reader
	version int
}

func (s *RESPSession) Execute(ctx context.Context, req RequestConfig) error {
	var cmd string
	switch req.Op {
	case OpPing:
		cmd = "*1\r\n$4\r\nPING\r\n"
	case OpRateLimit:
		k := req.Key
		l := strconv.FormatUint(req.Limit, 10)
		w := strconv.FormatUint(req.WindowMs, 10)
		c := strconv.FormatUint(req.Cost, 10)
		cmd = fmt.Sprintf("*5\r\n$13\r\nGK.RATE_LIMIT\r\n$%d\r\n%s\r\n$%d\r\n%s\r\n$%d\r\n%s\r\n$%d\r\n%s\r\n",
			len(k), k, len(l), l, len(w), w, len(c), c)
	case OpSet:
		k := req.Key
		v := req.Value
		cmd = fmt.Sprintf("*3\r\n$3\r\nSET\r\n$%d\r\n%s\r\n$%d\r\n%s\r\n",
			len(k), k, len(v), v)
	case OpGet:
		k := req.Key
		cmd = fmt.Sprintf("*2\r\n$3\r\nGET\r\n$%d\r\n%s\r\n", len(k), k)
	default:
		return fmt.Errorf("unsupported op: %s", req.Op)
	}

	if _, err := s.conn.Write([]byte(cmd)); err != nil {
		return err
	}
	return s.readResponse()
}

func (s *RESPSession) readResponse() error {
	prefix, err := s.reader.ReadByte()
	if err != nil {
		return err
	}

	line, err := s.reader.ReadString('\n')
	if err != nil {
		return err
	}

	switch prefix {
	case '+', '-', ':', '#', ',', '(':
		// Single line response
		return nil
	case '_':
		// Null
		return nil
	case '$':
		// Bulk string: $<len>\r\n<data>\r\n
		length, err := strconv.Atoi(line[:len(line)-2])
		if err != nil {
			return err
		}
		if length == -1 {
			return nil
		}
		buf := make([]byte, length+2)
		if _, err := io.ReadFull(s.reader, buf); err != nil {
			return err
		}
		return nil
	case '*', '%', '~', '>':
		// Array / Map / Set / Push
		count, err := strconv.Atoi(line[:len(line)-2])
		if err != nil {
			return err
		}
		if count == -1 {
			return nil
		}
		totalElements := count
		if prefix == '%' {
			totalElements = count * 2
		}
		for i := 0; i < totalElements; i++ {
			if err := s.readResponse(); err != nil {
				return err
			}
		}
		return nil
	default:
		return fmt.Errorf("unknown RESP prefix: %c", prefix)
	}
}

func (s *RESPSession) Close() error {
	return s.conn.Close()
}
