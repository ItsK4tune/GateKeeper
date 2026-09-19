package gatekeeper_test

import (
	"bytes"
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"

	gatekeeper "github.com/gatekeeper-kv/gatekeeper/sdk/go"
)

func TestClient_Idempotency(t *testing.T) {
	completed := false
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		if r.URL.Path == "/v1/idempotency/begin" {
			if completed {
				w.WriteHeader(http.StatusOK)
				_ = json.NewEncoder(w).Encode(map[string]any{
					"action":        "REPLAY",
					"owner_token":   "tok_1",
					"response_code": 201,
					"response_body": `{"id":"item_123"}`,
				})
				return
			}
			w.WriteHeader(http.StatusOK)
			_ = json.NewEncoder(w).Encode(map[string]any{
				"action":      "EXECUTE",
				"owner_token": "tok_1",
			})
			return
		}
		if r.URL.Path == "/v1/idempotency/complete" {
			completed = true
			w.WriteHeader(http.StatusOK)
			_ = json.NewEncoder(w).Encode(map[string]any{"completed": true})
			return
		}
		http.NotFound(w, r)
	}))
	defer ts.Close()

	client := gatekeeper.NewClient(ts.URL)

	// Begin
	res, err := client.IdemBegin(context.Background(), gatekeeper.IdempotencyBeginRequest{
		Key:         "order_test_1",
		RequestHash: "hash_test_1",
		TTLMs:       60000,
	})
	if err != nil {
		t.Fatalf("IdemBegin failed: %v", err)
	}
	if res.Action != gatekeeper.ActionExecute {
		t.Fatalf("expected ActionExecute, got %s", res.Action)
	}

	// Complete
	err = client.IdemComplete(context.Background(), gatekeeper.IdempotencyCompleteRequest{
		Key:          "order_test_1",
		OwnerToken:   res.OwnerToken,
		ResponseCode: 201,
		ResponseBody: `{"id":"item_123"}`,
	})
	if err != nil {
		t.Fatalf("IdemComplete failed: %v", err)
	}

	// Replay
	replayRes, err := client.IdemBegin(context.Background(), gatekeeper.IdempotencyBeginRequest{
		Key:         "order_test_1",
		RequestHash: "hash_test_1",
	})
	if err != nil {
		t.Fatalf("IdemBegin replay failed: %v", err)
	}
	if replayRes.Action != gatekeeper.ActionReplay {
		t.Fatalf("expected ActionReplay, got %s", replayRes.Action)
	}
}

func TestIdempotencyMiddleware(t *testing.T) {
	storedAction := "EXECUTE"
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		if r.URL.Path == "/v1/idempotency/begin" {
			if storedAction == "REPLAY" {
				_ = json.NewEncoder(w).Encode(map[string]any{
					"action":        "REPLAY",
					"owner_token":   "tok_mid",
					"response_code": 200,
					"response_body": `{"msg":"cached response"}`,
				})
				return
			}
			_ = json.NewEncoder(w).Encode(map[string]any{
				"action":      "EXECUTE",
				"owner_token": "tok_mid",
			})
			return
		}
		if r.URL.Path == "/v1/idempotency/complete" {
			storedAction = "REPLAY"
			_ = json.NewEncoder(w).Encode(map[string]any{"completed": true})
			return
		}
		http.NotFound(w, r)
	}))
	defer ts.Close()

	client := gatekeeper.NewClient(ts.URL)
	handlerCalled := 0
	backend := http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		handlerCalled++
		w.WriteHeader(http.StatusOK)
		_, _ = w.Write([]byte(`{"msg":"original response"}`))
	})

	mw := gatekeeper.IdempotencyMiddleware(client, gatekeeper.IdempotencyConfig{})
	wrapped := mw(backend)

	// First request -> executes backend
	req1 := httptest.NewRequest(http.MethodPost, "/items", bytes.NewBufferString(`{"name":"test"}`))
	req1.Header.Set("Idempotency-Key", "mid_key_1")
	rec1 := httptest.NewRecorder()
	wrapped.ServeHTTP(rec1, req1)

	if rec1.Code != http.StatusOK {
		t.Fatalf("first request code: %d", rec1.Code)
	}
	if handlerCalled != 1 {
		t.Fatalf("handlerCalled expected 1, got %d", handlerCalled)
	}

	// Second request with same key -> replayed, backend not called
	req2 := httptest.NewRequest(http.MethodPost, "/items", bytes.NewBufferString(`{"name":"test"}`))
	req2.Header.Set("Idempotency-Key", "mid_key_1")
	rec2 := httptest.NewRecorder()
	wrapped.ServeHTTP(rec2, req2)

	if rec2.Code != http.StatusOK {
		t.Fatalf("second request code: %d", rec2.Code)
	}
	if handlerCalled != 1 {
		t.Fatalf("handlerCalled should remain 1 on replay, got %d", handlerCalled)
	}
	if rec2.Header().Get("X-Cache-Lookup") != "HIT" {
		t.Fatalf("expected X-Cache-Lookup: HIT on replay")
	}
}
