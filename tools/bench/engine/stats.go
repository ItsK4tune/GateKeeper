package engine

import (
	"encoding/json"
	"fmt"
	"math"
	"os"
	"sort"
	"time"
)

type BenchmarkReport struct {
	Protocol    string        `json:"protocol"`
	Operation   string        `json:"operation"`
	Target      string        `json:"target"`
	Concurrency int           `json:"concurrency"`
	DurationSec float64       `json:"duration_sec"`
	TotalReqs   int64         `json:"total_requests"`
	Successful  int64         `json:"successful"`
	Failed      int64         `json:"failed"`
	QPS         float64       `json:"qps"`
	LatenciesMs LatencyReport `json:"latencies_ms"`
}

type LatencyReport struct {
	Min   float64 `json:"min"`
	Avg   float64 `json:"avg"`
	P50   float64 `json:"p50"`
	P90   float64 `json:"p90"`
	P95   float64 `json:"p95"`
	P99   float64 `json:"p99"`
	P999  float64 `json:"p999"`
	Max   float64 `json:"max"`
}

func CalculateReport(proto, op, target string, concurrency int, duration time.Duration, completed, failed int64, lats []time.Duration) BenchmarkReport {
	total := completed + failed
	qps := 0.0
	if duration.Seconds() > 0 {
		qps = float64(completed) / duration.Seconds()
	}

	report := BenchmarkReport{
		Protocol:    proto,
		Operation:   op,
		Target:      target,
		Concurrency: concurrency,
		DurationSec: duration.Seconds(),
		TotalReqs:   total,
		Successful:  completed,
		Failed:      failed,
		QPS:         qps,
	}

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

		report.LatenciesMs = LatencyReport{
			Min:  float64(lats[0].Microseconds()) / 1000.0,
			Avg:  float64(avgLat.Microseconds()) / 1000.0,
			P50:  float64(p50.Microseconds()) / 1000.0,
			P90:  float64(p90.Microseconds()) / 1000.0,
			P95:  float64(p95.Microseconds()) / 1000.0,
			P99:  float64(p99.Microseconds()) / 1000.0,
			P999: float64(p999.Microseconds()) / 1000.0,
			Max:  float64(lats[len(lats)-1].Microseconds()) / 1000.0,
		}
	}

	return report
}

func PrintReport(r BenchmarkReport) {
	fmt.Printf("\n--- Benchmark Results ---\n")
	fmt.Printf("Protocol:        %s\n", r.Protocol)
	fmt.Printf("Operation:       %s\n", r.Operation)
	fmt.Printf("Total Time:      %.3f s\n", r.DurationSec)
	fmt.Printf("Total Requests:  %d\n", r.TotalReqs)
	successPct := 0.0
	failPct := 0.0
	if r.TotalReqs > 0 {
		successPct = float64(r.Successful) / float64(r.TotalReqs) * 100
		failPct = float64(r.Failed) / float64(r.TotalReqs) * 100
	}
	fmt.Printf("Successful:      %d (%.2f%%)\n", r.Successful, successPct)
	fmt.Printf("Failed/Errors:   %d (%.2f%%)\n", r.Failed, failPct)
	fmt.Printf("Throughput:      %.2f QPS (requests/sec)\n", r.QPS)

	fmt.Printf("\n--- Latency Distribution ---\n")
	fmt.Printf("  Min:           %.3f ms\n", r.LatenciesMs.Min)
	fmt.Printf("  Avg:           %.3f ms\n", r.LatenciesMs.Avg)
	fmt.Printf("  P50:           %.3f ms\n", r.LatenciesMs.P50)
	fmt.Printf("  P90:           %.3f ms\n", r.LatenciesMs.P90)
	fmt.Printf("  P95:           %.3f ms\n", r.LatenciesMs.P95)
	fmt.Printf("  P99:           %.3f ms\n", r.LatenciesMs.P99)
	fmt.Printf("  P99.9:         %.3f ms\n", r.LatenciesMs.P999)
	fmt.Printf("  Max:           %.3f ms\n", r.LatenciesMs.Max)
	fmt.Printf("====================================================\n")
}

func SaveReportJSON(filepath string, r BenchmarkReport) error {
	data, err := json.MarshalIndent(r, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(filepath, data, 0644)
}
