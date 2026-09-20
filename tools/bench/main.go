package main

import (
	"flag"
	"fmt"
	"gatekeeper-bench/driver"
	"gatekeeper-bench/engine"
	"os"
	"strings"
	"time"
)

func main() {
	var (
		proto       string
		addr        string
		concurrency int
		totalReqs   int64
		duration    time.Duration
		opStr       string
		key         string
		valSize     int
		outFile     string
	)

	flag.StringVar(&proto, "proto", "gkwp1", "Protocol: gkwp1 | gkwp2 | resp2 | resp3 | http1")
	flag.StringVar(&addr, "addr", "127.0.0.1:63779", "Target address (host:port or URL)")
	flag.IntVar(&concurrency, "c", 50, "Number of concurrent workers/connections")
	flag.Int64Var(&totalReqs, "n", 50000, "Total number of requests (ignored if -d is set)")
	flag.DurationVar(&duration, "d", 0, "Benchmark duration (e.g. 10s)")
	flag.StringVar(&opStr, "op", "rate-limit", "Operation: ping | rate-limit | set | get")
	flag.StringVar(&key, "key", "bench:key", "Target key")
	flag.IntVar(&valSize, "val-size", 64, "Value size in bytes for SET")
	flag.StringVar(&outFile, "out", "", "Optional output path to save JSON results")
	flag.Parse()

	d, err := driver.GetDriver(proto)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}

	op := driver.OpType(opStr)
	switch op {
	case driver.OpPing, driver.OpRateLimit, driver.OpSet, driver.OpGet, driver.OpLock:
	default:
		fmt.Fprintf(os.Stderr, "Unknown operation: %s\n", opStr)
		os.Exit(1)
	}

	reqCfg := driver.RequestConfig{
		Op:       op,
		Key:      key,
		Value:    strings.Repeat("x", valSize),
		Limit:    100000000,
		WindowMs: 60000,
		Cost:     1,
	}

	fmt.Printf("====================================================\n")
	fmt.Printf(" GateKeeper Universal Benchmark Suite\n")
	fmt.Printf("====================================================\n")
	fmt.Printf("Protocol:    %s\n", d.Name())
	fmt.Printf("Target:      %s\n", addr)
	fmt.Printf("Operation:   %s\n", strings.ToUpper(opStr))
	fmt.Printf("Concurrency: %d connections/workers\n", concurrency)
	if duration > 0 {
		fmt.Printf("Duration:    %v\n", duration)
	} else {
		fmt.Printf("Total Reqs:  %d\n", totalReqs)
	}
	fmt.Printf("----------------------------------------------------\n")

	completed, failed, totalDur, latencies := engine.Run(engine.RunnerConfig{
		Driver:      d,
		Addr:        addr,
		ReqConfig:   reqCfg,
		Concurrency: concurrency,
		TotalReqs:   totalReqs,
		Duration:    duration,
	})

	report := engine.CalculateReport(d.Name(), string(op), addr, concurrency, totalDur, completed, failed, latencies)
	engine.PrintReport(report)

	if outFile != "" {
		if err := engine.SaveReportJSON(outFile, report); err != nil {
			fmt.Fprintf(os.Stderr, "Failed to save JSON report to %s: %v\n", outFile, err)
		} else {
			fmt.Printf("Report saved to %s\n", outFile)
		}
	}
}
