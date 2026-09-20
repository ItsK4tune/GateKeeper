package driver

import (
	"context"
	"fmt"
)

type OpType string

const (
	OpPing      OpType = "ping"
	OpRateLimit OpType = "rate-limit"
	OpSet       OpType = "set"
	OpGet       OpType = "get"
	OpLock      OpType = "lock-acquire"
)

type RequestConfig struct {
	Op       OpType
	Key      string
	Value    string
	Limit    uint64
	WindowMs uint64
	Cost     uint64
}

type Driver interface {
	Name() string
	Connect(ctx context.Context, addr string) (Session, error)
}

type Session interface {
	Execute(ctx context.Context, req RequestConfig) error
	Close() error
}

func GetDriver(proto string) (Driver, error) {
	switch proto {
	case "gkwp1":
		return &GKWP1Driver{}, nil
	case "gkwp2":
		return &GKWP2Driver{}, nil
	case "resp2":
		return &RESPDriver{Version: 2}, nil
	case "resp3":
		return &RESPDriver{Version: 3}, nil
	case "http1", "http":
		return &HTTP1Driver{}, nil
	default:
		return nil, fmt.Errorf("unsupported protocol: %s (supported: gkwp1, gkwp2, resp2, resp3, http1)", proto)
	}
}
