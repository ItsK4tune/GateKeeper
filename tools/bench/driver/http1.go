package driver

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"net/http"
	"strings"
	"time"
)

type HTTP1Driver struct{}

func (d *HTTP1Driver) Name() string {
	return "HTTP/1.1"
}

func (d *HTTP1Driver) Connect(ctx context.Context, addr string) (Session, error) {
	baseURL := strings.TrimRight(addr, "/")
	if !strings.HasPrefix(baseURL, "http://") && !strings.HasPrefix(baseURL, "https://") {
		baseURL = "http://" + baseURL
	}

	transport := &http.Transport{
		MaxIdleConns:        100,
		MaxIdleConnsPerHost: 100,
		IdleConnTimeout:     90 * time.Second,
		DisableCompression:  true,
	}

	client := &http.Client{
		Transport: transport,
		Timeout:   10 * time.Second,
	}

	return &HTTP1Session{
		baseURL: baseURL,
		client:  client,
	}, nil
}

type HTTP1Session struct {
	baseURL string
	client  *http.Client
}

func (s *HTTP1Session) Execute(ctx context.Context, req RequestConfig) error {
	var targetURL string
	var method string
	var body []byte

	switch req.Op {
	case OpPing:
		targetURL = s.baseURL + "/healthz"
		method = "GET"
	case OpRateLimit:
		targetURL = s.baseURL + "/v1/rate-limit/check"
		method = "POST"
		body = []byte(fmt.Sprintf(`{"key":"%s","limit":%d,"window_ms":%d,"cost":%d}`,
			req.Key, req.Limit, req.WindowMs, req.Cost))
	case OpSet:
		targetURL = s.baseURL + "/v1/kv/set"
		method = "POST"
		body = []byte(fmt.Sprintf(`{"key":"%s","value":"%s","ttl_ms":60000}`, req.Key, req.Value))
	case OpGet:
		targetURL = fmt.Sprintf("%s/v1/kv/get?key=%s", s.baseURL, req.Key)
		method = "GET"
	default:
		return fmt.Errorf("unsupported op: %s", req.Op)
	}

	var httpReq *http.Request
	var err error
	if len(body) > 0 {
		httpReq, err = http.NewRequestWithContext(ctx, method, targetURL, bytes.NewReader(body))
		httpReq.Header.Set("Content-Type", "application/json")
	} else {
		httpReq, err = http.NewRequestWithContext(ctx, method, targetURL, nil)
	}

	if err != nil {
		return err
	}

	resp, err := s.client.Do(httpReq)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	_, _ = io.Copy(io.Discard, resp.Body)

	if (resp.StatusCode >= 200 && resp.StatusCode < 300) || (req.Op == OpRateLimit && resp.StatusCode == 429) {
		return nil
	}
	return fmt.Errorf("HTTP error status: %d", resp.StatusCode)
}

func (s *HTTP1Session) Close() error {
	s.client.CloseIdleConnections()
	return nil
}
