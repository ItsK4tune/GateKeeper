package gatekeeper_test

import (
	"context"
	"encoding/binary"
	"encoding/json"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"

	gatekeeper "github.com/gatekeeper-kv/gatekeeper/sdk/go"
)

func TestClient_RateLimit(t *testing.T) {
	count := 0
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/healthz" {
			w.WriteHeader(http.StatusOK)
			_, _ = w.Write([]byte(`{"status":"ok"}`))
			return
		}

		if r.URL.Path == "/v1/rate-limit/check" {
			count++
			if count <= 2 {
				w.Header().Set("X-RateLimit-Limit", "2")
				w.Header().Set("X-RateLimit-Remaining", "1")
				w.Header().Set("X-RateLimit-Reset", "1700000060")
				w.WriteHeader(http.StatusOK)
				_ = json.NewEncoder(w).Encode(map[string]any{
					"allowed":        true,
					"remaining":      2 - count,
					"retry_after_ms": 0,
				})
			} else {
				w.Header().Set("X-RateLimit-Limit", "2")
				w.Header().Set("X-RateLimit-Remaining", "0")
				w.Header().Set("Retry-After", "50")
				w.WriteHeader(http.StatusTooManyRequests)
				_ = json.NewEncoder(w).Encode(map[string]any{
					"allowed":        false,
					"remaining":      0,
					"retry_after_ms": 50000,
				})
			}
			return
		}

		http.NotFound(w, r)
	}))
	defer ts.Close()

	client := gatekeeper.NewClient(ts.URL, gatekeeper.WithTimeout(2*time.Second))
	ctx := context.Background()

	ok, err := client.Health(ctx)
	if err != nil || !ok {
		t.Fatalf("Health check failed: ok=%v, err=%v", ok, err)
	}

	res1, err := client.CheckRateLimit(ctx, gatekeeper.RateLimitRequest{
		Key:   "user_1",
		Limit: 2,
	})
	if err != nil {
		t.Fatalf("unexpected error on req 1: %v", err)
	}
	if !res1.Allowed || res1.Limit != 2 {
		t.Fatalf("unexpected res1: %+v", res1)
	}

	res2, err := client.CheckRateLimit(ctx, gatekeeper.RateLimitRequest{
		Key:   "user_1",
		Limit: 2,
	})
	if err != nil {
		t.Fatalf("unexpected error on req 2: %v", err)
	}
	if !res2.Allowed {
		t.Fatalf("unexpected res2: %+v", res2)
	}

	res3, err := client.CheckRateLimit(ctx, gatekeeper.RateLimitRequest{
		Key:   "user_1",
		Limit: 2,
	})
	if err != gatekeeper.ErrRateLimited {
		t.Fatalf("expected ErrRateLimited, got: %v", err)
	}
	if res3 == nil || res3.Allowed {
		t.Fatalf("expected res3 to not be allowed, got: %+v", res3)
	}
}

func TestHTTPMiddleware(t *testing.T) {
	callCount := 0
	mockBackend := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		callCount++
		if callCount == 1 {
			w.Header().Set("X-RateLimit-Limit", "1")
			w.Header().Set("X-RateLimit-Remaining", "0")
			w.WriteHeader(http.StatusOK)
			_ = json.NewEncoder(w).Encode(map[string]any{"allowed": true, "remaining": 0, "retry_after_ms": 0})
		} else {
			w.Header().Set("X-RateLimit-Limit", "1")
			w.Header().Set("X-RateLimit-Remaining", "0")
			w.Header().Set("Retry-After", "10")
			w.WriteHeader(http.StatusTooManyRequests)
			_ = json.NewEncoder(w).Encode(map[string]any{"allowed": false, "remaining": 0, "retry_after_ms": 10000})
		}
	}))
	defer mockBackend.Close()

	client := gatekeeper.NewClient(mockBackend.URL)
	mw := gatekeeper.HTTPMiddleware(client, gatekeeper.RateLimitConfig{
		Limit:    1,
		WindowMs: 10000,
	})

	dummyHandler := http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusOK)
		_, _ = w.Write([]byte("success"))
	})

	server := httptest.NewServer(mw(dummyHandler))
	defer server.Close()

	resp1, err := http.Get(server.URL)
	if err != nil || resp1.StatusCode != http.StatusOK {
		t.Fatalf("req 1 failed: code=%d err=%v", resp1.StatusCode, err)
	}

	resp2, err := http.Get(server.URL)
	if err != nil || resp2.StatusCode != http.StatusTooManyRequests {
		t.Fatalf("req 2 expected 429, got: code=%d err=%v", resp2.StatusCode, err)
	}
}

func TestClient_TCP_GKWP(t *testing.T) {
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("failed to listen tcp: %v", err)
	}
	defer l.Close()

	go func() {
		conn, err := l.Accept()
		if err != nil {
			return
		}
		defer conn.Close()

		for {
			header := make([]byte, 4)
			if _, err := io.ReadFull(conn, header); err != nil {
				return
			}
			reqLen := binary.BigEndian.Uint32(header)
			reqBytes := make([]byte, reqLen)
			if _, err := io.ReadFull(conn, reqBytes); err != nil {
				return
			}

			var req struct {
				ID string `json:"id"`
				OP string `json:"op"`
			}
			_ = json.Unmarshal(reqBytes, &req)

			var respBody []byte
			if req.OP == "PING" {
				respBody = []byte(`{"id":"` + req.ID + `","ok":true,"result":{"pong":true}}`)
			} else if req.OP == "GK.RATE_LIMIT" {
				respBody = []byte(`{"id":"` + req.ID + `","ok":true,"result":{"allowed":true,"remaining":4,"retry_after_ms":0}}`)
			} else {
				respBody = []byte(`{"id":"` + req.ID + `","ok":false,"error":{"code":"UNKNOWN","message":"unknown"}}`)
			}

			outFrame := make([]byte, 4+len(respBody))
			binary.BigEndian.PutUint32(outFrame[0:4], uint32(len(respBody)))
			copy(outFrame[4:], respBody)
			_, _ = conn.Write(outFrame)
		}
	}()

	client := gatekeeper.NewClient(l.Addr().String(), gatekeeper.WithTimeout(2*time.Second))
	defer client.Close()

	ctx := context.Background()
	ok, err := client.Health(ctx)
	if err != nil || !ok {
		t.Fatalf("TCP Health failed: ok=%v, err=%v", ok, err)
	}

	res, err := client.CheckRateLimit(ctx, gatekeeper.RateLimitRequest{
		Key:   "user_tcp",
		Limit: 5,
	})
	if err != nil {
		t.Fatalf("TCP CheckRateLimit failed: %v", err)
	}
	if !res.Allowed || res.Remaining != 4 {
		t.Fatalf("unexpected TCP rate limit result: %+v", res)
	}
}
