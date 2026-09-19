package gatekeeper

import (
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/http/httptest"
)

var (
	ErrIdempotencyConflict = errors.New("gatekeeper: idempotency request hash conflict")
	ErrTokenMismatch       = errors.New("gatekeeper: idempotency owner token mismatch")
)

type IdempotencyAction string

const (
	ActionExecute IdempotencyAction = "EXECUTE"
	ActionPark    IdempotencyAction = "PARK"
	ActionReplay  IdempotencyAction = "REPLAY"
)

type IdempotencyBeginRequest struct {
	Key         string `json:"key"`
	RequestHash string `json:"request_hash"`
	TTLMs       uint64 `json:"ttl_ms,omitempty"`
	OwnerToken  string `json:"owner_token,omitempty"`
}

type IdempotencyBeginResponse struct {
	Action       IdempotencyAction `json:"action"`
	OwnerToken   string            `json:"owner_token"`
	ResponseCode int               `json:"response_code,omitempty"`
	ResponseBody string            `json:"response_body,omitempty"`
}

type IdempotencyCompleteRequest struct {
	Key          string `json:"key"`
	OwnerToken   string `json:"owner_token"`
	ResponseCode int    `json:"response_code"`
	ResponseBody string `json:"response_body"`
}

type IdempotencyFailRequest struct {
	Key          string `json:"key"`
	OwnerToken   string `json:"owner_token"`
	ErrorMessage string `json:"error_message"`
}

func (c *Client) IdemBegin(ctx context.Context, req IdempotencyBeginRequest) (*IdempotencyBeginResponse, error) {
	body, err := json.Marshal(req)
	if err != nil {
		return nil, fmt.Errorf("marshal request: %w", err)
	}

	httpReq, err := http.NewRequestWithContext(ctx, http.MethodPost, c.endpoint+"/v1/idempotency/begin", bytes.NewReader(body))
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

	if httpResp.StatusCode == http.StatusConflict {
		return nil, ErrIdempotencyConflict
	}
	if httpResp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("unexpected status code %d: %s", httpResp.StatusCode, string(respBody))
	}

	var res IdempotencyBeginResponse
	if err := json.Unmarshal(respBody, &res); err != nil {
		return nil, fmt.Errorf("unmarshal response: %w", err)
	}

	return &res, nil
}

func (c *Client) IdemComplete(ctx context.Context, req IdempotencyCompleteRequest) error {
	body, err := json.Marshal(req)
	if err != nil {
		return fmt.Errorf("marshal request: %w", err)
	}

	httpReq, err := http.NewRequestWithContext(ctx, http.MethodPost, c.endpoint+"/v1/idempotency/complete", bytes.NewReader(body))
	if err != nil {
		return fmt.Errorf("create request: %w", err)
	}
	httpReq.Header.Set("Content-Type", "application/json")

	httpResp, err := c.httpClient.Do(httpReq)
	if err != nil {
		return fmt.Errorf("send request: %w", err)
	}
	defer httpResp.Body.Close()

	respBody, err := io.ReadAll(httpResp.Body)
	if err != nil {
		return fmt.Errorf("read body: %w", err)
	}

	if httpResp.StatusCode == http.StatusForbidden {
		return ErrTokenMismatch
	}
	if httpResp.StatusCode != http.StatusOK {
		return fmt.Errorf("unexpected status code %d: %s", httpResp.StatusCode, string(respBody))
	}

	return nil
}

func (c *Client) IdemFail(ctx context.Context, req IdempotencyFailRequest) error {
	body, err := json.Marshal(req)
	if err != nil {
		return fmt.Errorf("marshal request: %w", err)
	}

	httpReq, err := http.NewRequestWithContext(ctx, http.MethodPost, c.endpoint+"/v1/idempotency/fail", bytes.NewReader(body))
	if err != nil {
		return fmt.Errorf("create request: %w", err)
	}
	httpReq.Header.Set("Content-Type", "application/json")

	httpResp, err := c.httpClient.Do(httpReq)
	if err != nil {
		return fmt.Errorf("send request: %w", err)
	}
	defer httpResp.Body.Close()

	respBody, err := io.ReadAll(httpResp.Body)
	if err != nil {
		return fmt.Errorf("read body: %w", err)
	}

	if httpResp.StatusCode == http.StatusForbidden {
		return ErrTokenMismatch
	}
	if httpResp.StatusCode != http.StatusOK {
		return fmt.Errorf("unexpected status code %d: %s", httpResp.StatusCode, string(respBody))
	}

	return nil
}

type IdempotencyConfig struct {
	TTLMs          uint64
	HeaderName     string
	HashCalculator func(r *http.Request, body []byte) string
	OnConflict     func(w http.ResponseWriter, r *http.Request)
}

func DefaultHashCalculator(r *http.Request, body []byte) string {
	h := sha256.New()
	h.Write([]byte(r.Method + ":" + r.URL.Path + ":"))
	h.Write(body)
	return hex.EncodeToString(h.Sum(nil))
}

func IdempotencyMiddleware(client *Client, cfg IdempotencyConfig) func(http.Handler) http.Handler {
	if cfg.HeaderName == "" {
		cfg.HeaderName = "Idempotency-Key"
	}
	if cfg.TTLMs == 0 {
		cfg.TTLMs = 60000
	}
	if cfg.HashCalculator == nil {
		cfg.HashCalculator = DefaultHashCalculator
	}
	if cfg.OnConflict == nil {
		cfg.OnConflict = func(w http.ResponseWriter, r *http.Request) {
			w.Header().Set("Content-Type", "application/json")
			w.WriteHeader(http.StatusConflict)
			_ = json.NewEncoder(w).Encode(map[string]any{
				"error":   "ERR_IDEMPOTENCY_CONFLICT",
				"message": "A request with the same idempotency key but different parameters is already in progress or completed.",
			})
		}
	}

	return func(next http.Handler) http.Handler {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			key := r.Header.Get(cfg.HeaderName)
			if key == "" {
				next.ServeHTTP(w, r)
				return
			}

			var bodyBytes []byte
			if r.Body != nil {
				bodyBytes, _ = io.ReadAll(r.Body)
				r.Body = io.NopCloser(bytes.NewBuffer(bodyBytes))
			}

			reqHash := cfg.HashCalculator(r, bodyBytes)
			beginRes, err := client.IdemBegin(r.Context(), IdempotencyBeginRequest{
				Key:         key,
				RequestHash: reqHash,
				TTLMs:       cfg.TTLMs,
			})
			if err != nil {
				if errors.Is(err, ErrIdempotencyConflict) {
					cfg.OnConflict(w, r)
					return
				}
				http.Error(w, "GateKeeper idempotency error: "+err.Error(), http.StatusInternalServerError)
				return
			}

			if beginRes.Action == ActionReplay {
				w.Header().Set("X-Cache-Lookup", "HIT")
				w.Header().Set("Content-Type", "application/json")
				w.WriteHeader(beginRes.ResponseCode)
				_, _ = w.Write([]byte(beginRes.ResponseBody))
				return
			}

			rec := httptest.NewRecorder()
			next.ServeHTTP(rec, r)

			for k, v := range rec.Header() {
				w.Header()[k] = v
			}
			w.WriteHeader(rec.Code)
			respBody := rec.Body.Bytes()
			_, _ = w.Write(respBody)

			if rec.Code < 400 {
				_ = client.IdemComplete(r.Context(), IdempotencyCompleteRequest{
					Key:          key,
					OwnerToken:   beginRes.OwnerToken,
					ResponseCode: rec.Code,
					ResponseBody: string(respBody),
				})
			} else {
				_ = client.IdemFail(r.Context(), IdempotencyFailRequest{
					Key:          key,
					OwnerToken:   beginRes.OwnerToken,
					ErrorMessage: string(respBody),
				})
			}
		})
	}
}
