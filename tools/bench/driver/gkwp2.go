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
	var payload []byte
	switch req.Op {
	case OpPing:
		payload = make([]byte, 2)
		binary.BigEndian.PutUint16(payload[0:2], 0x0001)
	case OpGet:
		k := []byte(req.Key)
		payload = make([]byte, 2+2+len(k))
		binary.BigEndian.PutUint16(payload[0:2], 0x0011)
		binary.BigEndian.PutUint16(payload[2:4], uint16(len(k)))
		copy(payload[4:], k)
	case OpSet:
		k := []byte(req.Key)
		v := []byte(req.Value)
		payload = make([]byte, 2+2+len(k)+4+len(v)+8+1)
		binary.BigEndian.PutUint16(payload[0:2], 0x0010)
		binary.BigEndian.PutUint16(payload[2:4], uint16(len(k)))
		copy(payload[4:], k)
		offset := 4 + len(k)
		binary.BigEndian.PutUint32(payload[offset:offset+4], uint32(len(v)))
		copy(payload[offset+4:], v)
		offset += 4 + len(v)
		binary.BigEndian.PutUint64(payload[offset:offset+8], 0) // ttl_ms = 0
		offset += 8
		payload[offset] = 0 // cond = Always
	case OpRateLimit:
		k := []byte(req.Key)
		payload = make([]byte, 2+2+len(k)+8+8+4)
		binary.BigEndian.PutUint16(payload[0:2], 0x0100)
		binary.BigEndian.PutUint16(payload[2:4], uint16(len(k)))
		copy(payload[4:], k)
		offset := 4 + len(k)
		binary.BigEndian.PutUint64(payload[offset:offset+8], req.Limit)
		binary.BigEndian.PutUint64(payload[offset+8:offset+16], req.WindowMs)
		binary.BigEndian.PutUint32(payload[offset+16:offset+20], uint32(req.Cost))
	case OpLock:
		k := []byte(req.Key)
		payload = make([]byte, 2+2+len(k)+8+2+1+8)
		binary.BigEndian.PutUint16(payload[0:2], 0x0130)
		binary.BigEndian.PutUint16(payload[2:4], uint16(len(k)))
		copy(payload[4:], k)
		offset := 4 + len(k)
		binary.BigEndian.PutUint64(payload[offset:offset+8], 60000) // ttl_ms = 60000
		offset += 8
		binary.BigEndian.PutUint16(payload[offset:offset+2], 0) // owner_token len = 0 (auto-generate)
		offset += 2
		payload[offset] = 0 // ephemeral = false
		offset += 1
		binary.BigEndian.PutUint64(payload[offset:offset+8], 0) // session_id = 0
	default:
		return fmt.Errorf("unsupported op: %s", req.Op)
	}

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
