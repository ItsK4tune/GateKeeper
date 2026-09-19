package main

import (
	"bytes"
	"encoding/binary"
	"flag"
	"fmt"
	"io"
	"math"
	"net"
	"net/http"
	"os"
	"sort"
	"strings"
	"sync"
	"sync/atomic"
	"time"
)

type Config struct {
	Addr        string
	Proto       string
	Concurrency int
	TotalReqs   int64
	Duration    time.Duration
	Op          string
	Key         string
	ValSize     int
	Pipeline    int
}

type Result struct {
	Completed int64
	Failed    int64
	Latencies []time.Duration
}

func buildGkwpFrame(payload string) []byte {
	b := []byte(payload)
	frame := make([]byte, 4+len(b))
	binary.BigEndian.PutUint32(frame[:4], uint32(len(b)))
	copy(frame[4:], b)
	return frame
}

func runGkwpWorker(cfg Config, reqsPerWorker int64, frame []byte, stopCh <-chan struct{}, res *Result, wg *sync.WaitGroup) {
	defer wg.Done()

	conn, err := net.DialTimeout("tcp", cfg.Addr, 3*time.Second)
	if err != nil {
		atomic.AddInt64(&res.Failed, reqsPerWorker)
		return
	}
	defer conn.Close()

	tcpConn, ok := conn.(*net.TCPConn)
	if ok {
		_ = tcpConn.SetNoDelay(true)
	}

	headerBuf := make([]byte, 4)
	pipeline := cfg.Pipeline
	if pipeline < 1 {
		pipeline = 1
	}

	var batchFrame []byte
	for i := 0; i < pipeline; i++ {
		batchFrame = append(batchFrame, frame...)
	}

	localLatencies := make([]time.Duration, 0, 10000)
	var completed int64
	var failed int64

	for {
		select {
		case <-stopCh:
			goto finish
		default:
		}

		if cfg.Duration == 0 && completed >= reqsPerWorker {
			break
		}

		start := time.Now()
		if _, err := conn.Write(batchFrame); err != nil {
			failed += int64(pipeline)
			break
		}

		readErr := false
		for i := 0; i < pipeline; i++ {
			if _, err := io.ReadFull(conn, headerBuf); err != nil {
				readErr = true
				break
			}
			respLen := binary.BigEndian.Uint32(headerBuf)
			respBuf := make([]byte, respLen)
			if _, err := io.ReadFull(conn, respBuf); err != nil {
				readErr = true
				break
			}
		}

		elapsed := time.Since(start)
		if readErr {
			failed += int64(pipeline)
			break
		}

		completed += int64(pipeline)
		perReqElapsed := elapsed / time.Duration(pipeline)
		if len(localLatencies) < 50000 {
			for i := 0; i < pipeline; i++ {
				localLatencies = append(localLatencies, perReqElapsed)
			}
		}
	}

finish:
	atomic.AddInt64(&res.Completed, completed)
	atomic.AddInt64(&res.Failed, failed)

	resLock.Lock()
	allLatencies = append(allLatencies, localLatencies...)
	resLock.Unlock()
}

func runHttpWorker(cfg Config, reqsPerWorker int64, client *http.Client, url string, method string, body []byte, stopCh <-chan struct{}, res *Result, wg *sync.WaitGroup) {
	defer wg.Done()

	localLatencies := make([]time.Duration, 0, 10000)
	var completed int64
	var failed int64

	for {
		select {
		case <-stopCh:
			goto finish
		default:
		}

		if cfg.Duration == 0 && completed >= reqsPerWorker {
			break
		}

		start := time.Now()
		var req *http.Request
		var err error
		if len(body) > 0 {
			req, err = http.NewRequest(method, url, bytes.NewReader(body))
			req.Header.Set("Content-Type", "application/json")
		} else {
			req, err = http.NewRequest(method, url, nil)
		}

		if err != nil {
			failed++
			continue
		}

		resp, err := client.Do(req)
		if err != nil {
			failed++
			continue
		}
		_, _ = io.Copy(io.Discard, resp.Body)
		resp.Body.Close()

		elapsed := time.Since(start)
		if resp.StatusCode >= 200 && resp.StatusCode < 300 {
			completed++
			if len(localLatencies) < 50000 {
				localLatencies = append(localLatencies, elapsed)
			}
		} else {
			failed++
		}
	}

finish:
	atomic.AddInt64(&res.Completed, completed)
	atomic.AddInt64(&res.Failed, failed)

	resLock.Lock()
	allLatencies = append(allLatencies, localLatencies...)
	resLock.Unlock()
}

var (
	resLock      sync.Mutex
	allLatencies []time.Duration
)

func main() {
	var cfg Config
	flag.StringVar(&cfg.Addr, "addr", "127.0.0.1:63779", "Server address (host:port or URL)")
	flag.StringVar(&cfg.Proto, "proto", "gkwp1", "Protocol: gkwp1 | http")
	flag.IntVar(&cfg.Concurrency, "c", 50, "Number of concurrent workers/connections")
	flag.Int64Var(&cfg.TotalReqs, "n", 100000, "Total number of requests (ignored if -d is set)")
	flag.DurationVar(&cfg.Duration, "d", 0, "Benchmark duration (e.g. 10s). If set, overrides -n")
	flag.StringVar(&cfg.Op, "op", "rate-limit", "Operation: ping | rate-limit | set | get")
	flag.StringVar(&cfg.Key, "key", "bench:key", "Target key")
	flag.IntVar(&cfg.ValSize, "val-size", 64, "Value size in bytes for SET")
	flag.IntVar(&cfg.Pipeline, "pipeline", 1, "Pipelining depth (GKWP only)")
	flag.Parse()

	if cfg.Concurrency <= 0 {
		cfg.Concurrency = 1
	}

	valPayload := strings.Repeat("x", cfg.ValSize)

	fmt.Printf("====================================================\n")
	fmt.Printf(" GateKeeper Benchmark Suite\n")
	fmt.Printf("====================================================\n")
	fmt.Printf("Protocol:    %s\n", strings.ToUpper(cfg.Proto))
	fmt.Printf("Target:      %s\n", cfg.Addr)
	fmt.Printf("Operation:   %s\n", strings.ToUpper(cfg.Op))
	fmt.Printf("Concurrency: %d connections\n", cfg.Concurrency)
	if cfg.Duration > 0 {
		fmt.Printf("Duration:    %v\n", cfg.Duration)
	} else {
		fmt.Printf("Total Reqs:  %d\n", cfg.TotalReqs)
	}
	if cfg.Proto == "gkwp1" && cfg.Pipeline > 1 {
		fmt.Printf("Pipeline:    %d\n", cfg.Pipeline)
	}
	fmt.Printf("----------------------------------------------------\n")

	var stopCh = make(chan struct{})
	if cfg.Duration > 0 {
		go func() {
			time.Sleep(cfg.Duration)
			close(stopCh)
		}()
	}

	res := &Result{}
	var wg sync.WaitGroup

	reqsPerWorker := cfg.TotalReqs / int64(cfg.Concurrency)
	if reqsPerWorker < 1 {
		reqsPerWorker = 1
	}

	startTime := time.Now()

	if cfg.Proto == "gkwp1" {
		var payloadStr string
		switch cfg.Op {
		case "ping":
			payloadStr = `{"id":"b1","op":"PING","body":{}}`
		case "rate-limit":
			payloadStr = fmt.Sprintf(`{"id":"b1","op":"GK.RATE_LIMIT","body":{"key":"%s","limit":100000000,"window_ms":60000,"cost":1}}`, cfg.Key)
		case "set":
			payloadStr = fmt.Sprintf(`{"id":"b1","op":"SET","body":{"key":"%s","value":"%s"}}`, cfg.Key, valPayload)
		case "get":
			payloadStr = fmt.Sprintf(`{"id":"b1","op":"GET","body":{"key":"%s"}}`, cfg.Key)
		default:
			fmt.Fprintf(os.Stderr, "Unknown operation: %s\n", cfg.Op)
			os.Exit(1)
		}

		frame := buildGkwpFrame(payloadStr)

		for i := 0; i < cfg.Concurrency; i++ {
			wg.Add(1)
			go runGkwpWorker(cfg, reqsPerWorker, frame, stopCh, res, &wg)
		}
	} else if cfg.Proto == "http" {
		baseURL := strings.TrimRight(cfg.Addr, "/")
		if !strings.HasPrefix(baseURL, "http://") && !strings.HasPrefix(baseURL, "https://") {
			baseURL = "http://" + baseURL
		}

		transport := &http.Transport{
			MaxIdleConns:        cfg.Concurrency * 2,
			MaxIdleConnsPerHost: cfg.Concurrency * 2,
			IdleConnTimeout:     90 * time.Second,
			DisableCompression:  true,
		}
		client := &http.Client{
			Transport: transport,
			Timeout:   10 * time.Second,
		}

		var targetURL string
		var method string
		var body []byte

		switch cfg.Op {
		case "ping":
			targetURL = baseURL + "/healthz"
			method = "GET"
		case "rate-limit":
			targetURL = baseURL + "/v1/rate-limit/check"
			method = "POST"
			body = []byte(fmt.Sprintf(`{"key":"%s","limit":100000000,"window_ms":60000,"cost":1}`, cfg.Key))
		case "set":
			targetURL = baseURL + "/v1/kv/set"
			method = "POST"
			body = []byte(fmt.Sprintf(`{"key":"%s","value":"%s","ttl_ms":60000}`, cfg.Key, valPayload))
		case "get":
			targetURL = fmt.Sprintf("%s/v1/kv/get?key=%s", baseURL, cfg.Key)
			method = "GET"
		default:
			fmt.Fprintf(os.Stderr, "Unknown operation: %s\n", cfg.Op)
			os.Exit(1)
		}

		for i := 0; i < cfg.Concurrency; i++ {
			wg.Add(1)
			go runHttpWorker(cfg, reqsPerWorker, client, targetURL, method, body, stopCh, res, &wg)
		}
	} else {
		fmt.Fprintf(os.Stderr, "Unknown protocol: %s\n", cfg.Proto)
		os.Exit(1)
	}

	wg.Wait()
	totalDuration := time.Since(startTime)

	completed := atomic.LoadInt64(&res.Completed)
	failed := atomic.LoadInt64(&res.Failed)
	totalDone := completed + failed
	qps := float64(completed) / totalDuration.Seconds()

	fmt.Printf("\n--- Benchmark Results ---\n")
	fmt.Printf("Total Time:      %.3f s\n", totalDuration.Seconds())
	fmt.Printf("Total Requests:  %d\n", totalDone)
	fmt.Printf("Successful:      %d (%.2f%%)\n", completed, float64(completed)/float64(totalDone)*100)
	fmt.Printf("Failed/Errors:   %d (%.2f%%)\n", failed, float64(failed)/float64(totalDone)*100)
	fmt.Printf("Throughput:      %.2f QPS (requests/sec)\n", qps)

	resLock.Lock()
	lats := allLatencies
	resLock.Unlock()

	if len(lats) > 0 {
		sort.Slice(lats, func(i, j int) bool {
			return lats[i] < lats[j]
		})

		var totalLat time.Duration
		for _, l := range lats {
			totalLat += l
		}
		avgLat := totalLat / time.Duration(len(lats))

		p50 := lats[int(math.Round(float64(len(lats)-1)*0.50))]
		p90 := lats[int(math.Round(float64(len(lats)-1)*0.90))]
		p95 := lats[int(math.Round(float64(len(lats)-1)*0.95))]
		p99 := lats[int(math.Round(float64(len(lats)-1)*0.99))]
		p999 := lats[int(math.Round(float64(len(lats)-1)*0.999))]

		fmt.Printf("\n--- Latency Distribution ---\n")
		fmt.Printf("  Min:           %.3f ms\n", float64(lats[0].Microseconds())/1000.0)
		fmt.Printf("  Avg:           %.3f ms\n", float64(avgLat.Microseconds())/1000.0)
		fmt.Printf("  P50:           %.3f ms\n", float64(p50.Microseconds())/1000.0)
		fmt.Printf("  P90:           %.3f ms\n", float64(p90.Microseconds())/1000.0)
		fmt.Printf("  P95:           %.3f ms\n", float64(p95.Microseconds())/1000.0)
		fmt.Printf("  P99:           %.3f ms\n", float64(p99.Microseconds())/1000.0)
		fmt.Printf("  P99.9:         %.3f ms\n", float64(p999.Microseconds())/1000.0)
		fmt.Printf("  Max:           %.3f ms\n", float64(lats[len(lats)-1].Microseconds())/1000.0)
	}
	fmt.Printf("====================================================\n")
}
