package gatekeeper

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"strconv"
	"strings"
	"time"
)

var (
	ErrRateLimited         = errors.New("gatekeeper: rate limit exceeded")
	ErrQuotaExceeded       = errors.New("gatekeeper: quota exceeded")
	ErrReservationNotFound = errors.New("gatekeeper: reservation not found or expired")
	ErrBadRequest          = errors.New("gatekeeper: bad request")
	ErrInternal            = errors.New("gatekeeper: internal server error")
)

type Client struct {
	endpoint   string
	httpClient *http.Client
}

type Option func(*Client)

func WithHTTPClient(httpClient *http.Client) Option {
	return func(c *Client) {
		c.httpClient = httpClient
	}
}

func WithTimeout(timeout time.Duration) Option {
	return func(c *Client) {
		c.httpClient.Timeout = timeout
	}
}

func NewClient(endpoint string, opts ...Option) *Client {
	endpoint = strings.TrimRight(endpoint, "/")
	c := &Client{
		endpoint: endpoint,
		httpClient: &http.Client{
			Timeout: 5 * time.Second,
		},
	}
	for _, opt := range opts {
		opt(c)
	}
	return c
}

type RateLimitRequest struct {
	Key           string `json:"key,omitempty"`
	Tenant        string `json:"tenant,omitempty"`
	Subject       string `json:"subject,omitempty"`
	Resource      string `json:"resource,omitempty"`
	Limit         uint64 `json:"limit"`
	WindowMs      uint64 `json:"window_ms,omitempty"`
	WindowSeconds uint64 `json:"window_seconds,omitempty"`
	Cost          uint64 `json:"cost,omitempty"`
}

type RateLimitResponse struct {
	Allowed       bool   `json:"allowed"`
	Remaining     uint64 `json:"remaining"`
	RetryAfterMs  uint64 `json:"retry_after_ms"`
	Limit         uint64 `json:"-"`
	ResetEpochSec uint64 `json:"-"`
}

func (c *Client) CheckRateLimit(ctx context.Context, req RateLimitRequest) (*RateLimitResponse, error) {
	body, err := json.Marshal(req)
	if err != nil {
		return nil, fmt.Errorf("marshal request: %w", err)
	}

	httpReq, err := http.NewRequestWithContext(ctx, http.MethodPost, c.endpoint+"/v1/rate-limit/check", bytes.NewReader(body))
	if err != nil {
		return nil, fmt.Errorf("create request: %w", err)
	}
	httpReq.Header.Set("Content-Type", "application/json")

	httpResp, err := c.httpClient.Do(httpReq)
	if err != nil {
		return nil, fmt.Errorf("send request: %w", err)
	}
	defer httpResp.Body.Close()

	respBody, err := io.ReadAll(httpResp.Body)
	if err != nil {
		return nil, fmt.Errorf("read body: %w", err)
	}

	var res RateLimitResponse
	if err := json.Unmarshal(respBody, &res); err != nil {
		return nil, fmt.Errorf("unmarshal response: %w", err)
	}

	if lStr := httpResp.Header.Get("X-RateLimit-Limit"); lStr != "" {
		if l, err := strconv.ParseUint(lStr, 10, 64); err == nil {
			res.Limit = l
		}
	}
	if rStr := httpResp.Header.Get("X-RateLimit-Reset"); rStr != "" {
		if r, err := strconv.ParseUint(rStr, 10, 64); err == nil {
			res.ResetEpochSec = r
		}
	}

	if httpResp.StatusCode == http.StatusTooManyRequests {
		return &res, ErrRateLimited
	}
	if httpResp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("unexpected status code %d: %s", httpResp.StatusCode, string(respBody))
	}

	return &res, nil
}

type ReserveQuotaRequest struct {
	Key        string `json:"key"`
	Amount     uint64 `json:"amount"`
	TTLMs      uint64 `json:"ttl_ms,omitempty"`
	TTLSeconds uint64 `json:"ttl_seconds,omitempty"`
}

type ReserveQuotaResponse struct {
	Reserved      bool   `json:"reserved"`
	ReservationID string `json:"reservation_id"`
	Remaining     uint64 `json:"remaining"`
}

func (c *Client) ReserveQuota(ctx context.Context, req ReserveQuotaRequest) (*ReserveQuotaResponse, error) {
	body, err := json.Marshal(req)
	if err != nil {
		return nil, fmt.Errorf("marshal request: %w", err)
	}

	httpReq, err := http.NewRequestWithContext(ctx, http.MethodPost, c.endpoint+"/v1/quota/reserve", bytes.NewReader(body))
	if err != nil {
		return nil, fmt.Errorf("create request: %w", err)
	}
	httpReq.Header.Set("Content-Type", "application/json")

	httpResp, err := c.httpClient.Do(httpReq)
	if err != nil {
		return nil, fmt.Errorf("send request: %w", err)
	}
	defer httpResp.Body.Close()

	respBody, err := io.ReadAll(httpResp.Body)
	if err != nil {
		return nil, fmt.Errorf("read body: %w", err)
	}

	var res ReserveQuotaResponse
	if err := json.Unmarshal(respBody, &res); err != nil {
		return nil, fmt.Errorf("unmarshal response: %w", err)
	}

	if httpResp.StatusCode == http.StatusTooManyRequests {
		return &res, ErrQuotaExceeded
	}
	if httpResp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("unexpected status code %d: %s", httpResp.StatusCode, string(respBody))
	}

	return &res, nil
}

type CommitQuotaRequest struct {
	Key           string `json:"key"`
	ReservationID string `json:"reservation_id"`
	ActualAmount  uint64 `json:"actual_amount,omitempty"`
}

type CommitQuotaResponse struct {
	Committed    bool   `json:"committed"`
	ActualAmount uint64 `json:"actual_amount"`
	Refunded     uint64 `json:"refunded"`
	Remaining    uint64 `json:"remaining"`
}

func (c *Client) CommitQuota(ctx context.Context, req CommitQuotaRequest) (*CommitQuotaResponse, error) {
	body, err := json.Marshal(req)
	if err != nil {
		return nil, fmt.Errorf("marshal request: %w", err)
	}

	httpReq, err := http.NewRequestWithContext(ctx, http.MethodPost, c.endpoint+"/v1/quota/commit", bytes.NewReader(body))
	if err != nil {
		return nil, fmt.Errorf("create request: %w", err)
	}
	httpReq.Header.Set("Content-Type", "application/json")

	httpResp, err := c.httpClient.Do(httpReq)
	if err != nil {
		return nil, fmt.Errorf("send request: %w", err)
	}
	defer httpResp.Body.Close()

	respBody, err := io.ReadAll(httpResp.Body)
	if err != nil {
		return nil, fmt.Errorf("read body: %w", err)
	}

	if httpResp.StatusCode == http.StatusNotFound {
		return nil, ErrReservationNotFound
	}
	if httpResp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("unexpected status code %d: %s", httpResp.StatusCode, string(respBody))
	}

	var res CommitQuotaResponse
	if err := json.Unmarshal(respBody, &res); err != nil {
		return nil, fmt.Errorf("unmarshal response: %w", err)
	}

	return &res, nil
}

type RollbackQuotaRequest struct {
	Key           string `json:"key"`
	ReservationID string `json:"reservation_id"`
}

type RollbackQuotaResponse struct {
	RolledBack bool   `json:"rolled_back"`
	Refunded   uint64 `json:"refunded"`
	Remaining  uint64 `json:"remaining"`
}

func (c *Client) RollbackQuota(ctx context.Context, req RollbackQuotaRequest) (*RollbackQuotaResponse, error) {
	body, err := json.Marshal(req)
	if err != nil {
		return nil, fmt.Errorf("marshal request: %w", err)
	}

	httpReq, err := http.NewRequestWithContext(ctx, http.MethodPost, c.endpoint+"/v1/quota/rollback", bytes.NewReader(body))
	if err != nil {
		return nil, fmt.Errorf("create request: %w", err)
	}
	httpReq.Header.Set("Content-Type", "application/json")

	httpResp, err := c.httpClient.Do(httpReq)
	if err != nil {
		return nil, fmt.Errorf("send request: %w", err)
	}
	defer httpResp.Body.Close()

	respBody, err := io.ReadAll(httpResp.Body)
	if err != nil {
		return nil, fmt.Errorf("read body: %w", err)
	}

	if httpResp.StatusCode == http.StatusNotFound {
		return nil, ErrReservationNotFound
	}
	if httpResp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("unexpected status code %d: %s", httpResp.StatusCode, string(respBody))
	}

	var res RollbackQuotaResponse
	if err := json.Unmarshal(respBody, &res); err != nil {
		return nil, fmt.Errorf("unmarshal response: %w", err)
	}

	return &res, nil
}

func (c *Client) InitQuota(ctx context.Context, key string, quota uint64, ttlMs uint64) error {
	payload := map[string]any{
		"key":   key,
		"quota": quota,
	}
	if ttlMs > 0 {
		payload["ttl_ms"] = ttlMs
	}
	body, err := json.Marshal(payload)
	if err != nil {
		return fmt.Errorf("marshal request: %w", err)
	}

	httpReq, err := http.NewRequestWithContext(ctx, http.MethodPost, c.endpoint+"/v1/quota/init", bytes.NewReader(body))
	if err != nil {
		return fmt.Errorf("create request: %w", err)
	}
	httpReq.Header.Set("Content-Type", "application/json")

	httpResp, err := c.httpClient.Do(httpReq)
	if err != nil {
		return fmt.Errorf("send request: %w", err)
	}
	defer httpResp.Body.Close()

	if httpResp.StatusCode != http.StatusOK {
		b, _ := io.ReadAll(httpResp.Body)
		return fmt.Errorf("init quota status %d: %s", httpResp.StatusCode, string(b))
	}
	return nil
}

func (c *Client) Health(ctx context.Context) (bool, error) {
	httpReq, err := http.NewRequestWithContext(ctx, http.MethodGet, c.endpoint+"/healthz", nil)
	if err != nil {
		return false, fmt.Errorf("create request: %w", err)
	}

	httpResp, err := c.httpClient.Do(httpReq)
	if err != nil {
		return false, fmt.Errorf("send request: %w", err)
	}
	defer httpResp.Body.Close()

	return httpResp.StatusCode == http.StatusOK, nil
}
