package gatekeeper

import (
	"context"
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"sync"
	"sync/atomic"
	"time"
)

type GKWPResponse struct {
	ID     string          `json:"id"`
	OK     bool            `json:"ok"`
	Result json.RawMessage `json:"result,omitempty"`
	Error  *GKWPError      `json:"error,omitempty"`
}

type GKWPError struct {
	Code    string `json:"code"`
	Message string `json:"message"`
}

func (e *GKWPError) Error() string {
	return fmt.Sprintf("%s: %s", e.Code, e.Message)
}

type TCPClient struct {
	addr       string
	timeout    time.Duration
	mu         sync.Mutex
	conn       net.Conn
	reqCounter uint64
}

func NewTCPClient(addr string, timeout time.Duration) *TCPClient {
	if timeout <= 0 {
		timeout = 5 * time.Second
	}
	return &TCPClient{
		addr:    addr,
		timeout: timeout,
	}
}

func (c *TCPClient) getConn() (net.Conn, error) {
	if c.conn != nil {
		return c.conn, nil
	}
	conn, err := net.DialTimeout("tcp", c.addr, c.timeout)
	if err != nil {
		return nil, fmt.Errorf("dial tcp %s: %w", c.addr, err)
	}
	c.conn = conn
	return c.conn, nil
}

func (c *TCPClient) closeConn() {
	if c.conn != nil {
		_ = c.conn.Close()
		c.conn = nil
	}
}

func (c *TCPClient) Close() error {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.closeConn()
	return nil
}

func (c *TCPClient) Send(ctx context.Context, op string, body any) (*GKWPResponse, error) {
	c.mu.Lock()
	defer c.mu.Unlock()

	reqID := fmt.Sprintf("req_%d", atomic.AddUint64(&c.reqCounter, 1))
	reqMap := map[string]any{
		"id":   reqID,
		"op":   op,
		"body": body,
	}

	payloadBytes, err := json.Marshal(reqMap)
	if err != nil {
		return nil, fmt.Errorf("marshal request: %w", err)
	}

	length := uint32(len(payloadBytes))
	frame := make([]byte, 4+len(payloadBytes))
	binary.BigEndian.PutUint32(frame[0:4], length)
	copy(frame[4:], payloadBytes)

	conn, err := c.getConn()
	if err != nil {
		return nil, err
	}

	deadline := time.Now().Add(c.timeout)
	if d, ok := ctx.Deadline(); ok && d.Before(deadline) {
		deadline = d
	}
	_ = conn.SetDeadline(deadline)

	if _, err := conn.Write(frame); err != nil {
		c.closeConn()
		return nil, fmt.Errorf("write frame: %w", err)
	}

	header := make([]byte, 4)
	if _, err := io.ReadFull(conn, header); err != nil {
		c.closeConn()
		return nil, fmt.Errorf("read response header: %w", err)
	}

	respLen := binary.BigEndian.Uint32(header)
	if respLen > 64*1024*1024 {
		c.closeConn()
		return nil, errors.New("response frame too large")
	}

	respBytes := make([]byte, respLen)
	if _, err := io.ReadFull(conn, respBytes); err != nil {
		c.closeConn()
		return nil, fmt.Errorf("read response body: %w", err)
	}

	var resp GKWPResponse
	if err := json.Unmarshal(respBytes, &resp); err != nil {
		return nil, fmt.Errorf("unmarshal response: %w", err)
	}

	return &resp, nil
}
