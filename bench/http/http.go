// Benchmark http: net/http, a server and 8 goroutines with a client that keeps
// 8 connections (see http.cro).
package main

import (
	"fmt"
	"io"
	"net"
	"net/http"
	"sync"
	"sync/atomic"
)

func main() {
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		panic(err)
	}
	url := fmt.Sprintf("http://%s/hello", listener.Addr().String())
	go http.Serve(listener, http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		io.WriteString(w, "hello, world")
	}))
	client := &http.Client{Transport: &http.Transport{MaxIdleConnsPerHost: 8}}
	var bytes int64
	var wg sync.WaitGroup
	for c := 0; c < 8; c++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for i := 0; i < 5000; i++ {
				r, err := client.Get(url)
				if err != nil {
					panic(err)
				}
				body, _ := io.ReadAll(r.Body)
				r.Body.Close()
				atomic.AddInt64(&bytes, int64(len(body)))
			}
		}()
	}
	wg.Wait()
	fmt.Printf("http %d\n", bytes)
}
