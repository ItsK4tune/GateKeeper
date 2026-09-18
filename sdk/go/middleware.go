package gatekeeper

import (
	"encoding/json"
	"fmt"
	"net"
	"net/http"
	"strconv"
	"strings"
)

type RateLimitConfig struct {
	Limit         uint64
	WindowMs      uint64
	Cost          uint64
	Tenant        string
	Resource      string
	KeyExtractor  func(r *http.Request) string
	OnRateLimited func(w http.ResponseWriter, r *http.Request, resp *RateLimitResponse)
	OnError       func(w http.ResponseWriter, r *http.Request, err error)
	FailOpen      bool
}

func defaultKeyExtractor(r *http.Request) string {
	if xff := r.Header.Get("X-Forwarded-For"); xff != "" {
		parts := strings.Split(xff, ",")
		return strings.TrimSpace(parts[0])
	}
	if xri := r.Header.Get("X-Real-IP"); xri != "" {
		return strings.TrimSpace(xri)
	}
	host, _, err := net.SplitHostPort(r.RemoteAddr)
	if err == nil && host != "" {
		return host
	}
	return r.RemoteAddr
}

func defaultOnRateLimited(w http.ResponseWriter, r *http.Request, resp *RateLimitResponse) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusTooManyRequests)
	_ = json.NewEncoder(w).Encode(map[string]any{
		"error":          "Too Many Requests",
		"allowed":        false,
		"remaining":      0,
		"retry_after_ms": resp.RetryAfterMs,
	})
}

// HTTPMiddleware wraps a standard http.Handler with GateKeeper rate limiting.
func HTTPMiddleware(client *Client, cfg RateLimitConfig) func(http.Handler) http.Handler {
	if cfg.Cost == 0 {
		cfg.Cost = 1
	}
	if cfg.KeyExtractor == nil {
		cfg.KeyExtractor = defaultKeyExtractor
	}
	if cfg.OnRateLimited == nil {
		cfg.OnRateLimited = defaultOnRateLimited
	}

	return func(next http.Handler) http.Handler {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			subject := cfg.KeyExtractor(r)
			req := RateLimitRequest{
				Tenant:   cfg.Tenant,
				Subject:  subject,
				Resource: cfg.Resource,
				Limit:    cfg.Limit,
				WindowMs: cfg.WindowMs,
				Cost:     cfg.Cost,
			}

			resp, err := client.CheckRateLimit(r.Context(), req)
			if err != nil && err != ErrRateLimited {
				if cfg.OnError != nil {
					cfg.OnError(w, r, err)
					return
				}
				if cfg.FailOpen {
					next.ServeHTTP(w, r)
					return
				}
				http.Error(w, "GateKeeper rate limit service error", http.StatusServiceUnavailable)
				return
			}

			if resp != nil {
				w.Header().Set("X-RateLimit-Limit", strconv.FormatUint(resp.Limit, 10))
				w.Header().Set("X-RateLimit-Remaining", strconv.FormatUint(resp.Remaining, 10))
				if resp.ResetEpochSec > 0 {
					w.Header().Set("X-RateLimit-Reset", strconv.FormatUint(resp.ResetEpochSec, 10))
				}
				if !resp.Allowed && resp.RetryAfterMs > 0 {
					retryAfterSec := (resp.RetryAfterMs + 999) / 1000
					w.Header().Set("Retry-After", strconv.FormatUint(retryAfterSec, 10))
				}
			}

			if err == ErrRateLimited || (resp != nil && !resp.Allowed) {
				cfg.OnRateLimited(w, r, resp)
				return
			}

			next.ServeHTTP(w, r)
		})
	}
}

// CheckLimit executes an explicit rate limit check and returns headers and error for custom frameworks (e.g. Gin, Echo, Fiber).
func CheckLimit(client *Client, r *http.Request, cfg RateLimitConfig) (*RateLimitResponse, map[string]string, error) {
	if cfg.Cost == 0 {
		cfg.Cost = 1
	}
	if cfg.KeyExtractor == nil {
		cfg.KeyExtractor = defaultKeyExtractor
	}

	subject := cfg.KeyExtractor(r)
	req := RateLimitRequest{
		Tenant:   cfg.Tenant,
		Subject:  subject,
		Resource: cfg.Resource,
		Limit:    cfg.Limit,
		WindowMs: cfg.WindowMs,
		Cost:     cfg.Cost,
	}

	resp, err := client.CheckRateLimit(r.Context(), req)
	headers := make(map[string]string)
	if resp != nil {
		headers["X-RateLimit-Limit"] = strconv.FormatUint(resp.Limit, 10)
		headers["X-RateLimit-Remaining"] = strconv.FormatUint(resp.Remaining, 10)
		if resp.ResetEpochSec > 0 {
			headers["X-RateLimit-Reset"] = strconv.FormatUint(resp.ResetEpochSec, 10)
		}
		if !resp.Allowed && resp.RetryAfterMs > 0 {
			headers["Retry-After"] = fmt.Sprintf("%d", (resp.RetryAfterMs+999)/1000)
		}
	}
	return resp, headers, err
}
