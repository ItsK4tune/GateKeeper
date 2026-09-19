package driver

import (
	"context"
	"encoding/binary"
	"fmt"
	"io"
	"net"
	"sync/atomic"
	"time"
)

type GKWP2Driver struct{}

func (d *GKWP2Driver) Name() string {
	return "GKWP/2"
}

func (d *GKWP2Driver) Connect(ctx context.Context, addr string) (Session, error) {
	dDialer := net.Dialer{Timeout: 3 * time.Second}
	conn, err := dDialer.DialContext(ctx, "tcp", addr)
	if err != nil {
		return nil, err
	}
	if tcpConn, ok := conn.(*net.TCPConn); ok {
		_ = tcpConn.SetNoDelay(true)
	}
	return &GKWP2Session{conn: conn}, nil
}

type GKWP2Session struct {
	conn      net.Conn
	streamSeq uint32
	reqSeq    uint64
	headerBuf [24]byte
}

func (s *GKWP2Session) Execute(ctx context.Context, req RequestConfig) error {
	var payloadStr string
	switch req.Op {
	case OpPing:
		payloadStr = `{"op":"PING"}`
	case OpRateLimit:
		payloadStr = fmt.Sprintf(`{"op":"GK.RATE_LIMIT","key":"%s","limit":%d,"window_ms":%d,"cost":%d}`,
			req.Key, req.Limit, req.WindowMs, req.Cost)
	case OpSet:
		payloadStr = fmt.Sprintf(`{"op":"SET","key":"%s","value":"%s"}`, req.Key, req.Value)
	case OpGet:
		payloadStr = fmt.Sprintf(`{"op":"GET","key":"%s"}`, req.Key)
	default:
		return fmt.Errorf("unsupported op: %s", req.Op)
	}

	payload := []byte(payloadStr)
	frame := make([]byte, 24+len(payload))

	reqID := atomic.AddUint64(&s.reqSeq, 1)
	streamID := atomic.AddUint32(&s.streamSeq, 2)
	if streamID == 0 {
		streamID = 1
	}

	binary.BigEndian.PutUint16(frame[0:2], 0x474B) // Magic 'GK'
	frame[2] = 0x02                               // Version 2
	frame[3] = 0x08                               // Flag: END_STREAM
	frame[4] = 0x01                               // MsgType: REQUEST
	frame[5] = 0x00
	frame[6] = 0x00
	frame[7] = 0x00
	binary.BigEndian.PutUint64(frame[8:16], reqID)
	binary.BigEndian.PutUint32(frame[16:20], streamID)
	binary.BigEndian.PutUint32(frame[20:24], uint32(len(payload)))
	copy(frame[24:], payload)

	if _, err := s.conn.Write(frame); err != nil {
		return err
	}

	if _, err := io.ReadFull(s.conn, s.headerBuf[:]); err != nil {
		return err
	}

	magic := binary.BigEndian.Uint16(s.headerBuf[0:2])
	if magic != 0x474B {
		return fmt.Errorf("invalid GKWP/2 magic: 0x%04x", magic)
	}

	respLen := binary.BigEndian.Uint32(s.headerBuf[20:24])
	if respLen > 0 {
		respBuf := make([]byte, respLen)
		if _, err := io.ReadFull(s.conn, respBuf); err != nil {
			return err
		}
	}
	return nil
}

func (s *GKWP2Session) Close() error {
	return s.conn.Close()
}
