# GateKeeper SDK & Middleware (Go & Node.js)

High-performance, distributed rate limiting and two-phase quota reservation SDKs for GateKeeper v1.

---

## Features

- **Sub-millisecond Latency**: Non-blocking asynchronous I/O backed by single-threaded epoll event loop.
- **Distributed Consistency**: Atomic sliding window rate limiting without distributed lock overhead.
- **Standard Rate Limit Headers**:
  - `X-RateLimit-Limit`: Request limit within the window.
  - `X-RateLimit-Remaining`: Remaining request quota.
  - `X-RateLimit-Reset`: Unix epoch seconds when the quota window resets.
  - `Retry-After`: Seconds to wait before retrying (sent on HTTP 429).
- **Two-Phase Quota Reservation (Step 10b)**: Designed for GenAI token reservation, multi-step checkout, and distributed billing with auto-rollback on client disconnect or timeout.
- **Ready-to-use Middlewares**:
  - Go: `net/http`, `gin`, `echo`, `fiber`
  - Node.js: `Express`, `Fastify`, `NestJS`

---

## 1. Go SDK (`sdk/go`)

### Installation

```bash
go get github.com/gatekeeper-kv/gatekeeper/sdk/go
```

### Quickstart

```go
package main

import (
	"context"
	"fmt"
	"log"
	"time"

	gatekeeper "github.com/gatekeeper-kv/gatekeeper/sdk/go"
)

func main() {
	client := gatekeeper.NewClient("http://127.0.0.1:8080", gatekeeper.WithTimeout(2*time.Second))

	ctx := context.Background()

	// 1. Health check
	ok, err := client.Health(ctx)
	if err != nil || !ok {
		log.Fatalf("GateKeeper unavailable: %v", err)
	}

	// 2. Check Rate Limit
	resp, err := client.CheckRateLimit(ctx, gatekeeper.RateLimitRequest{
		Tenant:   "saas_tenant",
		Subject:  "user_102",
		Resource: "api:search",
		Limit:    100,
		WindowMs: 60000, // 1 minute
		Cost:     1,
	})
	if err == gatekeeper.ErrRateLimited {
		fmt.Printf("Rate limited! Retry after %d ms\n", resp.RetryAfterMs)
		return
	}
	if err != nil {
		log.Fatalf("Error checking rate limit: %v", err)
	}

	fmt.Printf("Allowed: %v, Remaining: %d\n", resp.Allowed, resp.Remaining)
}
```

### HTTP Middleware (`net/http`)

```go
package main

import (
	"net/http"

	gatekeeper "github.com/gatekeeper-kv/gatekeeper/sdk/go"
)

func main() {
	client := gatekeeper.NewClient("http://127.0.0.1:8080")

	// Middleware: 100 requests per minute per IP
	limiter := gatekeeper.HTTPMiddleware(client, gatekeeper.RateLimitConfig{
		Limit:    100,
		WindowMs: 60000,
		Cost:     1,
		FailOpen: true, // gracefully allow traffic if GateKeeper is unreachable
	})

	mux := http.NewServeMux()
	mux.HandleFunc("/api/hello", func(w http.ResponseWriter, r *http.Request) {
		w.Write([]byte("Hello, World!"))
	})

	http.ListenAndServe(":3000", limiter(mux))
}
```

### Gin Middleware Integration

```go
func GinRateLimiter(client *gatekeeper.Client, cfg gatekeeper.RateLimitConfig) gin.HandlerFunc {
	return func(c *gin.Context) {
		resp, headers, err := gatekeeper.CheckLimit(client, c.Request, cfg)
		for k, v := range headers {
			c.Header(k, v)
		}
		if err == gatekeeper.ErrRateLimited || (resp != nil && !resp.Allowed) {
			c.AbortWithStatusJSON(http.StatusTooManyRequests, gin.H{
				"error":          "Too Many Requests",
				"retry_after_ms": resp.RetryAfterMs,
			})
			return
		}
		c.Next()
	}
}
```

---

## 2. Node.js SDK (`sdk/nodejs`)

### Installation

```bash
npm install @gatekeeper-kv/client
```

### Quickstart

```javascript
const { GateKeeperClient, RateLimitError } = require('@gatekeeper-kv/client');

const client = new GateKeeperClient({
  endpoint: 'http://127.0.0.1:8080',
  timeoutMs: 3000,
});

async function run() {
  try {
    const res = await client.checkRateLimit({
      tenant: 'acme_corp',
      subject: 'user_456',
      resource: 'inference:llm',
      limit: 10,
      windowMs: 60000,
      cost: 2,
    });
    console.log(`Allowed! Remaining: ${res.remaining}`);
  } catch (err) {
    if (err instanceof RateLimitError) {
      console.warn(`Rate limited! Retry after ${err.info.retryAfterMs}ms`);
    } else {
      console.error('Request failed:', err);
    }
  }
}

run();
```

### Express.js Middleware

```javascript
const express = require('express');
const { GateKeeperClient, createRateLimitMiddleware } = require('@gatekeeper-kv/client');

const app = express();
const client = new GateKeeperClient({ endpoint: 'http://127.0.0.1:8080' });

// Apply 60 requests per minute per IP
app.use(createRateLimitMiddleware(client, {
  limit: 60,
  windowMs: 60000,
  keyExtractor: (req) => req.ip,
}));

app.get('/api/data', (req, res) => {
  res.json({ message: 'Success' });
});

app.listen(3000, () => console.log('Server running on :3000'));
```

### NestJS Interceptor / Guard Example

```typescript
import { Injectable, CanActivate, ExecutionContext, HttpException, HttpStatus } from '@nestjs/common';
import { GateKeeperClient, RateLimitError } from '@gatekeeper-kv/client';

@Injectable()
export class RateLimitGuard implements CanActivate {
  private client = new GateKeeperClient({ endpoint: 'http://127.0.0.1:8080' });

  async canActivate(context: ExecutionContext): Promise<boolean> {
    const req = context.switchToHttp().getRequest();
    const res = context.switchToHttp().getResponse();

    try {
      const result = await this.client.checkRateLimit({
        subject: req.ip,
        limit: 100,
        windowMs: 60000,
      });

      res.setHeader('X-RateLimit-Limit', result.limit);
      res.setHeader('X-RateLimit-Remaining', result.remaining);
      return true;
    } catch (err) {
      if (err instanceof RateLimitError) {
        throw new HttpException({
          statusCode: HttpStatus.TOO_MANY_REQUESTS,
          message: 'Too Many Requests',
          retry_after_ms: err.info.retryAfterMs,
        }, HttpStatus.TOO_MANY_REQUESTS);
      }
      return true; // Fail-open
    }
  }
}
```

---

## 3. Two-Phase Quota Reservation Pattern (GenAI & Multi-step Billing)

```javascript
// Step 1: Reserve maximum anticipated tokens (e.g. 1000 tokens)
const { reservationId, remaining } = await client.reserveQuota({
  key: `tokens:${userId}`,
  amount: 1000,
  ttlMs: 15000, // 15s auto-rollback TTL if LLM stream drops
});

try {
  // Step 2: Stream LLM output from OpenAI / Claude / Gemini
  const actualTokensUsed = await streamLLMResponse(prompt);

  // Step 3: Commit exact tokens used (unused tokens automatically refunded)
  await client.commitQuota({
    key: `tokens:${userId}`,
    reservationId,
    actualAmount: actualTokensUsed, // e.g. 420 tokens
  });
} catch (err) {
  // Step 4: Explicit rollback on failure (or let TTL auto-rollback)
  await client.rollbackQuota({
    key: `tokens:${userId}`,
    reservationId,
  });
}
```
