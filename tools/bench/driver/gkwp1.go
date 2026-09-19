package driver

import (
	"context"
	"encoding/binary"
	"fmt"
	"io"
	"net"
	"time"
)

type GKWP1Driver struct{}

func (d *GKWP1Driver) Name() string {
	return "GKWP/1"
}

func (d *GKWP1Driver) Connect(ctx context.Context, addr string) (Session, error) {
	dDialer := net.Dialer{Timeout: 3 * time.Second}
	conn, err := dDialer.DialContext(ctx, "tcp", addr)
	if err != nil {
		return nil, err
	}
	if tcpConn, ok := conn.(*net.TCPConn); ok {
		_ = tcpConn.SetNoDelay(true)
	}
	return &GKWP1Session{conn: conn}, nil
}

type GKWP1Session struct {
	conn      net.Conn
	headerBuf [4]byte
}

func (s *GKWP1Session) Execute(ctx context.Context, req RequestConfig) error {
	var payloadStr string
	switch req.Op {
	case OpPing:
		payloadStr = `{"id":"b1","op":"PING","body":{}}`
	case OpRateLimit:
		payloadStr = fmt.Sprintf(`{"id":"b1","op":"GK.RATE_LIMIT","body":{"key":"%s","limit":%d,"window_ms":%d,"cost":%d}}`,
			req.Key, req.Limit, req.WindowMs, req.Cost)
	case OpSet:
		payloadStr = fmt.Sprintf(`{"id":"b1","op":"SET","body":{"key":"%s","value":"%s"}}`, req.Key, req.Value)
	case OpGet:
		payloadStr = fmt.Sprintf(`{"id":"b1","op":"GET","body":{"key":"%s"}}`, req.Key)
	default:
		return fmt.Errorf("unsupported op: %s", req.Op)
	}

	payload := []byte(payloadStr)
	frame := make([]byte, 4+len(payload))
	binary.BigEndian.PutUint32(frame[:4], uint32(len(payload)))
	copy(frame[4:], payload)

	if _, err := s.conn.Write(frame); err != nil {
		return err
	}

	if _, err := io.ReadFull(s.conn, s.headerBuf[:]); err != nil {
		return err
	}
	respLen := binary.BigEndian.Uint32(s.headerBuf[:])
	respBuf := make([]byte, respLen)
	if _, err := io.ReadFull(s.conn, respBuf); err != nil {
		return err
	}
	return nil
}

func (s *GKWP1Session) Close() error {
	return s.conn.Close()
}
