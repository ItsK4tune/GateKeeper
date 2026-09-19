package engine

import (
	"context"
	"gatekeeper-bench/driver"
	"sync"
	"sync/atomic"
	"time"
)

type RunnerConfig struct {
	Driver      driver.Driver
	Addr        string
	ReqConfig   driver.RequestConfig
	Concurrency int
	TotalReqs   int64
	Duration    time.Duration
}

func Run(cfg RunnerConfig) (completed int64, failed int64, totalDuration time.Duration, latencies []time.Duration) {
	var stopCh = make(chan struct{})
	if cfg.Duration > 0 {
		go func() {
			time.Sleep(cfg.Duration)
			close(stopCh)
		}()
	}

	reqsPerWorker := cfg.TotalReqs / int64(cfg.Concurrency)
	if reqsPerWorker < 1 {
		reqsPerWorker = 1
	}

	var wg sync.WaitGroup
	var completedCount int64
	var failedCount int64
	var mu sync.Mutex
	allLats := make([]time.Duration, 0, 100000)

	startTime := time.Now()

	for i := 0; i < cfg.Concurrency; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()

			ctx := context.Background()
			session, err := cfg.Driver.Connect(ctx, cfg.Addr)
			if err != nil {
				atomic.AddInt64(&failedCount, reqsPerWorker)
				return
			}
			defer session.Close()

			localLats := make([]time.Duration, 0, 10000)
			var wCompleted int64
			var wFailed int64

			for {
				select {
				case <-stopCh:
					goto done
				default:
				}

				if cfg.Duration == 0 && (wCompleted+wFailed) >= reqsPerWorker {
					break
				}

				start := time.Now()
				err := session.Execute(ctx, cfg.ReqConfig)
				elapsed := time.Since(start)

				if err == nil {
					wCompleted++
					if len(localLats) < 20000 {
						localLats = append(localLats, elapsed)
					}
				} else {
					wFailed++
				}
			}

		done:
			atomic.AddInt64(&completedCount, wCompleted)
			atomic.AddInt64(&failedCount, wFailed)

			mu.Lock()
			allLats = append(allLats, localLats...)
			mu.Unlock()
		}()
	}

	wg.Wait()
	totalDuration = time.Since(startTime)
	completed = atomic.LoadInt64(&completedCount)
	failed = atomic.LoadInt64(&failedCount)
	latencies = allLats
	return
}
